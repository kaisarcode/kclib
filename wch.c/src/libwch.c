/**
 * libwch.c - File and directory change notification library
 * Summary: Resident watcher with native filesystem backends.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libwch.h"

#if !defined(KC_WCH_BUILD_VERSION) || KC_WCH_BUILD_VERSION + 0 == 0
#undef KC_WCH_BUILD_VERSION
#define KC_WCH_BUILD_VERSION 0ULL
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#endif

#ifdef __linux__
#include <sys/inotify.h>
#include <poll.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#define KC_WCH_QUEUE_SIZE 256

#define KC_WCH_NAME_MAX 128
#define KC_WCH_CMD_MAX 4096
#define KC_WCH_PATH_MAX 4096

typedef struct {
    char name[KC_WCH_NAME_MAX];
    char path[KC_WCH_PATH_MAX];
    char command[KC_WCH_CMD_MAX];
    int recursive;
} kc_wch_registration_t;

typedef struct {
    const char *command;
    const char *dir;
    const char *name;
} kc_wch_dispatch_t;

typedef struct kc_wch_native kc_wch_native_t;

typedef struct {
    int type;
    const char *path;
} kc_wch_native_event_t;

typedef void (*kc_wch_native_handler_t)(
    const kc_wch_native_event_t *event,
    void *userdata
);

#define KC_WCH_NATIVE_ADD 0
#define KC_WCH_NATIVE_UPD 1
#define KC_WCH_NATIVE_DEL 2

struct kc_wch_native {
    char *root;
    int recursive;

    char **paths;
    int path_count;
    int path_cap;

    char ev_path[PATH_MAX];

    int backend;

    int has_filter;
    char filter_name[PATH_MAX];

    int q_type[KC_WCH_QUEUE_SIZE];
    char q_path[KC_WCH_QUEUE_SIZE][PATH_MAX];
    int q_used;

    kc_wch_native_handler_t handler;
    void *userdata;
    atomic_int stop;
    atomic_int ready;
    int thread_started;
    int self_close;

#ifdef _WIN32
    HANDLE thread;
#elif !defined(__EMSCRIPTEN__)
    pthread_t thread;
#endif

#ifdef __EMSCRIPTEN__
    struct kc_wch_native *wasm_next;
#endif

#ifdef __linux__
    int ifd;
    int *wds;
    char **wd_paths;
    int wd_count;
    int wd_cap;
    char rbuf[4096];
    int rbuf_off;
    int rbuf_len;
#endif

#ifdef __APPLE__
    int kq;
    int *dir_fds;
    char **dir_paths;
    int dir_count;
    int dir_cap;
#endif

#ifdef _WIN32
    HANDLE hdir;
    char win_buf[4096];
    OVERLAPPED ol;
    int pending;
#endif
};

/**
 * Find a path in the known-paths set.
 * @param w Watcher context.
 * @param p Path to find.
 * @return Index or -1 if not found.
 */
static int path_find(struct kc_wch_native *w, const char *p) {
    for (int i = 0; i < w->path_count; i++) {
        if (strcmp(w->paths[i], p) == 0) return i;
    }
    return -1;
}

/**
 * Add a path to the known-paths set.
 * @param w Watcher context.
 * @param p Path to add.
 * @return 0 on success, -1 on failure.
 */
static int path_add(struct kc_wch_native *w, const char *p) {
    if (path_find(w, p) >= 0) return 0;
    if (w->path_count >= w->path_cap) {
        int nc = w->path_cap ? w->path_cap * 2 : 128;
        char **np = realloc(w->paths, nc * sizeof(char *));
        if (!np) return -1;
        w->paths = np;
        w->path_cap = nc;
    }
    w->paths[w->path_count] = strdup(p);
    if (!w->paths[w->path_count]) return -1;
    w->path_count++;
    return 0;
}

/**
 * Remove a path from the known-paths set.
 * @param w Watcher context.
 * @param p Path to remove.
 * @return None.
 */
static void path_remove(struct kc_wch_native *w, const char *p) {
    int i = path_find(w, p);
    if (i < 0) return;
    free(w->paths[i]);
    w->paths[i] = w->paths[--w->path_count];
}

/**
 * Check if a path passes the optional basename filter.
 * @param w Watcher context.
 * @param path Path to check.
 * @return 1 if path should be reported, 0 if filtered.
 */
static int filter_ok(struct kc_wch_native *w, const char *path) {
    if (!w->has_filter) return 1;
    size_t pl = strlen(path);
    size_t fl = strlen(w->filter_name);
    if (pl < fl) return 0;
    return strcmp(path + pl - fl, w->filter_name) == 0;
}

/**
 * Push an event onto the internal queue.
 * @param w Watcher context.
 * @param type Event type (ADD/UPD/DEL).
 * @param path Event path.
 * @return None.
 */
static void queue_push(struct kc_wch_native *w, int type, const char *path) {
    if (w->q_used >= KC_WCH_QUEUE_SIZE) return;
    w->q_type[w->q_used] = type;
    snprintf(w->q_path[w->q_used], PATH_MAX, "%s", path);
    w->q_used++;
}

/**
 * Pop one event from the queue into the output struct.
 * @param w Watcher context.
 * @param ev Output event struct.
 * @return 1 if event returned, 0 if queue empty.
 */
static int dequeue(struct kc_wch_native *w, kc_wch_native_event_t *ev) {
    while (w->q_used > 0) {
        char *p = w->q_path[0];
        if (!filter_ok(w, p)) {
            w->q_used--;
            memmove(w->q_type, w->q_type + 1, w->q_used * sizeof(int));
            memmove(w->q_path, w->q_path + 1, w->q_used * PATH_MAX);
            continue;
        }
        int t = w->q_type[0];
        snprintf(w->ev_path, PATH_MAX, "%s", p);
        ev->type = t;
        ev->path = w->ev_path;
        w->q_used--;
        memmove(w->q_type, w->q_type + 1, w->q_used * sizeof(int));
        memmove(w->q_path, w->q_path + 1, w->q_used * PATH_MAX);
        return 1;
    }
    return 0;
}

/**
 * Detect and initialize the best native backend for the current platform.
 * @param w Watcher context.
 * @return 1 on success, 0 on failure.
 */
static int try_backend(struct kc_wch_native *w) {
#ifdef __EMSCRIPTEN__
    w->backend = 4;
    return 1;
#endif
#ifdef __linux__
    int ifd = inotify_init();
    if (ifd < 0) return 0;
    int wd = inotify_add_watch(ifd, w->root,
        IN_CREATE | IN_CLOSE_WRITE | IN_DELETE |
        IN_MOVED_TO | IN_MOVED_FROM);
    if (wd < 0) { close(ifd); return 0; }
    w->ifd = ifd;
    w->wds = calloc(64, sizeof(int));
    w->wd_paths = calloc(64, sizeof(char *));
    if (!w->wds || !w->wd_paths) { close(ifd); return 0; }
    w->wd_cap = 64;
    w->wds[0] = wd;
    w->wd_paths[0] = strdup(w->root);
    w->wd_count = 1;
    w->backend = 1;
    return 1;
#endif

#ifdef __APPLE__
    int kq = kqueue();
    if (kq < 0) return 0;
    int fd = open(w->root, O_RDONLY | O_EVTONLY);
    if (fd < 0) { close(kq); return 0; }
    struct kevent ch;
    EV_SET(&ch, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
        NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, 0);
    if (kevent(kq, &ch, 1, NULL, 0, NULL) < 0) {
        close(fd); close(kq); return 0;
    }
    w->kq = kq;
    w->dir_fds = calloc(64, sizeof(int));
    w->dir_paths = calloc(64, sizeof(char *));
    if (!w->dir_fds || !w->dir_paths) { close(fd); close(kq); return 0; }
    w->dir_fds[0] = fd;
    w->dir_paths[0] = strdup(w->root);
    w->dir_count = 1;
    w->dir_cap = 64;
    w->backend = 2;
    return 1;
#endif

#ifdef _WIN32
    w->hdir = CreateFileA(w->root, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS |
        FILE_FLAG_OVERLAPPED, NULL);
    if (w->hdir == INVALID_HANDLE_VALUE) return 0;
    memset(&w->ol, 0, sizeof(w->ol));
    w->ol.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!w->ol.hEvent) { CloseHandle(w->hdir); return 0; }
    w->backend = 3;
    return 1;
#endif

    return 0;
}

#ifdef __linux__
/**
 * Look up a watched directory by its inotify watch descriptor.
 * @param w Watcher context.
 * @param wd Watch descriptor.
 * @return Index into wd arrays, or -1.
 */
static int wd_lookup(struct kc_wch_native *w, int wd) {
    for (int i = 0; i < w->wd_count; i++)
        if (w->wds[i] == wd) return i;
    return -1;
}

/**
 * Add an inotify watch on a directory.
 * @param w Watcher context.
 * @param dir Directory path.
 * @return Watch descriptor, or -1 on failure.
 */
static int wd_add(struct kc_wch_native *w, const char *dir) {
    int wd = inotify_add_watch(w->ifd, dir,
        IN_CREATE | IN_CLOSE_WRITE | IN_DELETE |
        IN_MOVED_TO | IN_MOVED_FROM);
    if (wd < 0) return -1;
    for (int i = 0; i < w->wd_count; i++) {
        if (w->wds[i] == wd) {
            free(w->wd_paths[i]);
            w->wd_paths[i] = strdup(dir);
            return wd;
        }
    }
    if (w->wd_count >= w->wd_cap) {
        int nc = w->wd_cap * 2;
        int *nw = realloc(w->wds, nc * sizeof(int));
        char **np = realloc(w->wd_paths, nc * sizeof(char *));
        if (!nw || !np) return -1;
        w->wds = nw; w->wd_paths = np; w->wd_cap = nc;
    }
    w->wds[w->wd_count] = wd;
    w->wd_paths[w->wd_count] = strdup(dir);
    w->wd_count++;
    return wd;
}

/**
 * Remove an inotify watch and its path mapping.
 * @param w Watcher context.
 * @param wd Watch descriptor to remove.
 * @return None.
 */
static void wd_remove(struct kc_wch_native *w, int wd) {
    int i = wd_lookup(w, wd);
    if (i < 0) return;
    free(w->wd_paths[i]);
    if (i < --w->wd_count) {
        w->wds[i] = w->wds[w->wd_count];
        w->wd_paths[i] = w->wd_paths[w->wd_count];
    }
}

/**
 * Recursively scan and add inotify watches to all subdirectories.
 * Also populates the known-paths set.
 * @param w Watcher context.
 * @param dir Directory to scan.
 * @return None.
 */
static void scan_watch_dir(struct kc_wch_native *w, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char fp[PATH_MAX];
        snprintf(fp, PATH_MAX, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(fp, &st)) continue;
        path_add(w, fp);
        if (S_ISDIR(st.st_mode) && w->recursive) {
            wd_add(w, fp);
            scan_watch_dir(w, fp);
        }
    }
    closedir(d);
}

/**
 * Read raw events from the inotify fd with an optional timeout.
 * Buffers multiple events for per-event consumption.
 * @param w Watcher context.
 * @param tmo Timeout in milliseconds (-1 = infinite).
 * @return 0 on data available, 1 on timeout, -1 on error.
 */
static int read_inotify(struct kc_wch_native *w, int tmo) {
    if (w->rbuf_off < w->rbuf_len) return 0;
    struct pollfd pfd = { .fd = w->ifd, .events = POLLIN };
    int pr = poll(&pfd, 1, tmo);
    if (pr < 0) return -1;
    if (pr == 0) return 1;
    ssize_t n = read(w->ifd, w->rbuf, sizeof(w->rbuf));
    if (n <= 0) return -1;
    w->rbuf_off = 0;
    w->rbuf_len = n;
    return 0;
}

/**
 * Parse buffered inotify events and push them into the event queue.
 * @param w Watcher context.
 * @return None.
 */
static void fill_inotify(struct kc_wch_native *w) {
    while (w->rbuf_off < w->rbuf_len) {
        struct inotify_event *iev =
            (struct inotify_event *)(w->rbuf + w->rbuf_off);
        size_t sz = sizeof(struct inotify_event) + iev->len;
        w->rbuf_off += sz;
        int idx = wd_lookup(w, iev->wd);
        if (idx < 0) continue;
        char fp[PATH_MAX];
        if (iev->len && iev->name[0])
            snprintf(fp, PATH_MAX, "%s/%s", w->wd_paths[idx], iev->name);
        else
            snprintf(fp, PATH_MAX, "%s", w->wd_paths[idx]);
        if (iev->mask & (IN_DELETE | IN_MOVED_FROM)) {
            queue_push(w, KC_WCH_NATIVE_DEL, fp);
            path_remove(w, fp);
        } else if (iev->mask & IN_MOVED_TO) {
            if (iev->mask & IN_ISDIR && w->recursive)
                scan_watch_dir(w, fp);
            queue_push(w, KC_WCH_NATIVE_ADD, fp);
            path_add(w, fp);
        } else if (iev->mask & IN_CLOSE_WRITE) {
            queue_push(w, KC_WCH_NATIVE_UPD, fp);
            path_add(w, fp);
        } else if (iev->mask & IN_CREATE) {
            if (iev->mask & IN_ISDIR && w->recursive)
                scan_watch_dir(w, fp);
            queue_push(w, KC_WCH_NATIVE_ADD, fp);
            path_add(w, fp);
        } else if (iev->mask & IN_IGNORED) {
            wd_remove(w, iev->wd);
        }
    }
}
#endif

#ifdef __APPLE__
/**
 * Look up a directory path by its kqueue fd.
 * @param w Watcher context.
 * @param fd Open fd for a watched directory.
 * @return Index into kqueue dir arrays, or -1.
 */
static int kq_lookup(struct kc_wch_native *w, int fd) {
    for (int i = 0; i < w->dir_count; i++)
        if (w->dir_fds[i] == fd) return i;
    return -1;
}

/**
 * Add a kqueue vnode watch on a directory.
 * @param w Watcher context.
 * @param dir Directory path.
 * @return None.
 */
static void kq_add_dir(struct kc_wch_native *w, const char *dir) {
    int fd = open(dir, O_RDONLY | O_EVTONLY);
    if (fd < 0) return;
    struct kevent ch;
    EV_SET(&ch, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
        NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, 0);
    if (kevent(w->kq, &ch, 1, NULL, 0, NULL) < 0) {
        close(fd); return;
    }
    if (w->dir_count >= w->dir_cap) {
        int nc = w->dir_cap * 2;
        int *nf = realloc(w->dir_fds, nc * sizeof(int));
        char **np = realloc(w->dir_paths, nc * sizeof(char *));
        if (!nf || !np) { close(fd); return; }
        w->dir_fds = nf; w->dir_paths = np; w->dir_cap = nc;
    }
    w->dir_fds[w->dir_count] = fd;
    w->dir_paths[w->dir_count] = strdup(dir);
    w->dir_count++;
}

/**
 * Remove a kqueue dir watch and close its fd.
 * @param w Watcher context.
 * @param fd Open fd to remove.
 * @return None.
 */
static void kq_dir_remove(struct kc_wch_native *w, int fd) {
    int i = kq_lookup(w, fd);
    if (i < 0) return;
    close(w->dir_fds[i]);
    free(w->dir_paths[i]);
    if (i < --w->dir_count) {
        w->dir_fds[i] = w->dir_fds[w->dir_count];
        w->dir_paths[i] = w->dir_paths[w->dir_count];
    }
}

/**
 * Comparison function for qsort on path strings.
 * @param a First entry.
 * @param b Second entry.
 * @return strcmp result.
 */
static int scan_entry_cmp(const void *a, const void *b) {
    return strcmp(((const char **)a)[0], ((const char **)b)[0]);
}

/**
 * Scan the watched directory tree, compare with previous state,
 * and push all detected changes into the event queue.
 * @param w Watcher context.
 * @return None.
 */
static void kq_scan_diff(struct kc_wch_native *w) {
    int stack_cap = 1024, stack_cnt = 0;
    char **stack = malloc(stack_cap * sizeof(char *));
    int ent_cap = 1024, ent_cnt = 0;
    char **ents = malloc(ent_cap * sizeof(char *));
    time_t *mtimes = malloc(ent_cap * sizeof(time_t));
    off_t *sizes = malloc(ent_cap * sizeof(off_t));
    if (!stack || !ents || !mtimes || !sizes) {
        free(stack); free(ents); free(mtimes); free(sizes);
        return;
    }
    stack[stack_cnt++] = strdup(w->root);
    while (stack_cnt > 0) {
        char *dp = stack[--stack_cnt];
        DIR *d = opendir(dp);
        if (!d) { free(dp); continue; }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char fp[PATH_MAX];
            snprintf(fp, PATH_MAX, "%s/%s", dp, e->d_name);
            struct stat st;
            if (stat(fp, &st)) continue;
            if (S_ISDIR(st.st_mode) && w->recursive) {
                if (stack_cnt >= stack_cap) {
                    stack_cap *= 2;
                    char **ns = realloc(stack, stack_cap * sizeof(char *));
                    if (!ns) { free(dp); closedir(d); goto cleanup; }
                    stack = ns;
                }
                stack[stack_cnt++] = strdup(fp);
            }
            if (ent_cnt >= ent_cap) {
                ent_cap *= 2;
                char **ne = realloc(ents, ent_cap * sizeof(char *));
                time_t *nm = realloc(mtimes, ent_cap * sizeof(time_t));
                off_t *nz = realloc(sizes, ent_cap * sizeof(off_t));
                if (!ne || !nm || !nz) { free(dp); closedir(d); goto cleanup; }
                ents = ne; mtimes = nm; sizes = nz;
            }
            ents[ent_cnt] = strdup(fp);
            mtimes[ent_cnt] = st.st_mtime;
            sizes[ent_cnt] = st.st_size;
            ent_cnt++;
        }
        closedir(d);
        free(dp);
    }
    if (ent_cnt > 1)
        qsort(ents, ent_cnt, sizeof(char *), scan_entry_cmp);

    char **old = malloc(w->path_count * sizeof(char *));
    time_t *om = malloc(w->path_count * sizeof(time_t));
    off_t *oz = malloc(w->path_count * sizeof(off_t));
    if (!old || !om || !oz) { free(old); free(om); free(oz); goto cleanup; }
    for (int i = 0; i < w->path_count; i++) {
        old[i] = strdup(w->paths[i]);
        if (stat(w->paths[i], &(struct stat){0}) == 0) {
            struct stat st;
            stat(w->paths[i], &st);
            om[i] = st.st_mtime; oz[i] = st.st_size;
        } else {
            om[i] = 0; oz[i] = 0;
        }
    }
    if (w->path_count > 1)
        qsort(old, w->path_count, sizeof(char *), scan_entry_cmp);

    int ci = 0, oi = 0;
    while (ci < ent_cnt || oi < w->path_count) {
        int cmp;
        if (ci >= ent_cnt) cmp = 1;
        else if (oi >= w->path_count) cmp = -1;
        else cmp = strcmp(ents[ci], old[oi]);
        if (cmp < 0) {
            queue_push(w, KC_WCH_NATIVE_ADD, ents[ci]);
            ci++;
        } else if (cmp > 0) {
            queue_push(w, KC_WCH_NATIVE_DEL, old[oi]);
            oi++;
        } else {
            if (mtimes[ci] != om[oi] || sizes[ci] != oz[oi])
                queue_push(w, KC_WCH_NATIVE_UPD, ents[ci]);
            ci++; oi++;
        }
    }

    for (int i = 0; i < w->path_count; i++) free(old[i]);
    free(old); free(om); free(oz);

    for (int i = 0; i < w->path_count; i++) free(w->paths[i]);
    free(w->paths);
    w->paths = ents;
    w->path_count = ent_cnt;
    w->path_cap = ent_cap;
    ents = NULL;
    ent_cnt = 0;

cleanup:
    for (int i = 0; i < stack_cnt; i++) free(stack[i]);
    free(stack);
    free(ents);
    for (int i = 0; i < ent_cnt; i++) free(ents[i]);
    free(mtimes);
    free(sizes);
}

/**
 * Wait for a kqueue event and process it into the event queue.
 * Blocks in kevent(). On NOTE_WRITE triggers a full scan+diff.
 * @param w Watcher context.
 * @param tmo Timeout in milliseconds (-1 = infinite).
 * @return 1 if events queued, 0 on timeout, -1 on error.
 */
static int fill_kqueue(struct kc_wch_native *w, int tmo) {
    struct timespec ts = { .tv_sec = tmo / 1000,
        .tv_nsec = (tmo % 1000) * 1000000L };
    struct timespec *tsp = (tmo < 0) ? NULL : &ts;
    struct kevent ev;
    int n = kevent(w->kq, NULL, 0, &ev, 1, tsp);
    if (n < 0) return -1;
    if (n == 0) return 0;
    int idx = kq_lookup(w, (int)ev.ident);
    if (idx < 0) return 0;
    if (ev.fflags & NOTE_DELETE) {
        queue_push(w, KC_WCH_NATIVE_DEL, w->dir_paths[idx]);
        kq_dir_remove(w, (int)ev.ident);
    } else if (ev.fflags & NOTE_RENAME) {
        queue_push(w, KC_WCH_NATIVE_DEL, w->dir_paths[idx]);
        kq_dir_remove(w, (int)ev.ident);
    } else if (ev.fflags & NOTE_WRITE) {
        kq_scan_diff(w);
    }
    return (w->q_used > 0) ? 1 : 0;
}
#endif

#ifdef _WIN32
/**
 * Issue a ReadDirectoryChangesW request and process the results
 * into the event queue.
 * @param w Watcher context.
 * @param tmo Timeout in milliseconds.
 * @return None.
 */
static void fill_windows(struct kc_wch_native *w, int tmo) {
    if (!w->pending) {
        DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_SIZE;
        HANDLE event = w->ol.hEvent;
        ResetEvent(event);
        memset(&w->ol, 0, sizeof(w->ol));
        w->ol.hEvent = event;
        if (!ReadDirectoryChangesW(w->hdir, w->win_buf, sizeof(w->win_buf),
                w->recursive ? TRUE : FALSE,
                filter, NULL, &w->ol, NULL)) {
            return;
        }
        w->pending = 1;
    }
    DWORD wtmo = tmo < 0 ? INFINITE : (DWORD)tmo;
    if (WaitForSingleObject(w->ol.hEvent, wtmo) != WAIT_OBJECT_0) {
        return;
    }
    DWORD got;
    if (!GetOverlappedResult(w->hdir, &w->ol, &got, FALSE)) return;
    w->pending = 0;

    char *base = w->root;

    FILE_NOTIFY_INFORMATION *fni = (FILE_NOTIFY_INFORMATION *)w->win_buf;
    while (1) {
        int len = fni->FileNameLength / sizeof(WCHAR);
        int mb_len = WideCharToMultiByte(CP_UTF8, 0, fni->FileName, len,
            NULL, 0, NULL, NULL);
        char *name = malloc(mb_len + 1);
        if (name) {
            WideCharToMultiByte(CP_UTF8, 0, fni->FileName, len,
                name, mb_len, NULL, NULL);
            name[mb_len] = '\0';
            char fp[PATH_MAX];
            snprintf(fp, PATH_MAX, "%s/%s", base, name);
            free(name);
            switch (fni->Action) {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_RENAMED_NEW_NAME:
                queue_push(w, KC_WCH_NATIVE_ADD, fp);
                path_add(w, fp);
                break;
            case FILE_ACTION_MODIFIED:
                queue_push(w, KC_WCH_NATIVE_UPD, fp);
                path_add(w, fp);
                break;
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
                queue_push(w, KC_WCH_NATIVE_DEL, fp);
                path_remove(w, fp);
                break;
            }
        }
        if (!fni->NextEntryOffset) break;
        fni = (FILE_NOTIFY_INFORMATION *)((char *)fni + fni->NextEntryOffset);
    }
}
#endif

#ifdef __EMSCRIPTEN__

static kc_wch_native_t *kc_wch_wasm_watchers = NULL;
static int kc_wch_wasm_hooks_installed = 0;

/**
 * Installs filesystem tracking callbacks for the Emscripten virtual filesystem.
 * @return None.
 */
EM_JS(void, kc_wch_wasm_install_hooks, (), {
    if (Module.__kc_wch_hooks_installed) return;
    Module.__kc_wch_hooks_installed = true;

    function emit(type, path) {
        if (!path) return;
        setTimeout(function() {
            var n = lengthBytesUTF8(path) + 1;
            var p = _malloc(n);
            stringToUTF8(path, p, n);
            _kc_wch_wasm_event(type, p);
            _free(p);
        }, 0);
    }

    FS.trackingDelegate['onOpenFile'] = function(path) {
        emit(3, path);
    };
    FS.trackingDelegate['onMakeDirectory'] = function(path) {
        emit(0, path);
    };
    FS.trackingDelegate['onMakeSymlink'] = function(oldpath, newpath) {
        emit(0, newpath);
    };
    FS.trackingDelegate['onWriteToFile'] = function(path) {
        emit(4, path);
    };
    FS.trackingDelegate['willDeletePath'] = function(path) {
        emit(2, path);
    };
    FS.trackingDelegate['onMovePath'] = function(oldpath, newpath) {
        emit(2, oldpath);
        emit(0, newpath);
    };
});

/**
 * Checks whether one VFS path belongs to a watcher.
 * @param w Watcher instance.
 * @param path Changed VFS path.
 * @return 1 when the event belongs to the watcher, otherwise 0.
 */
static int kc_wch_wasm_matches(const kc_wch_native_t *w, const char *path) {
    size_t root_len;

    if (w == NULL || path == NULL) return 0;

    if (w->has_filter) {
        return filter_ok((kc_wch_native_t *)w, path);
    }

    if (strcmp(w->root, path) == 0) return 1;

    root_len = strlen(w->root);
    if (root_len == 0U || strncmp(path, w->root, root_len) != 0) return 0;
    if (path[root_len] != '/') return 0;

    if (w->recursive) return 1;

    return strchr(path + root_len + 1U, '/') == NULL;
}

/**
 * Receives one virtual-filesystem mutation from the JavaScript runtime.
 * @param type Normalized event type.
 * @param path Changed VFS path.
 * @return None.
 */
void kc_wch_wasm_event(int type, const char *path) {
    kc_wch_native_t *w = kc_wch_wasm_watchers;

    while (w != NULL) {
        kc_wch_native_t *next = w->wasm_next;

        if (!atomic_load(&w->stop) &&
                w->handler != NULL &&
                kc_wch_wasm_matches(w, path)) {
            kc_wch_native_event_t event;
            int known = path_find(w, path) >= 0;
            int emit = 1;

            if (type == 3) {
                if (known) {
                    emit = 0;
                } else {
                    event.type = KC_WCH_NATIVE_ADD;
                    (void)path_add(w, path);
                }
            } else if (type == 4) {
                event.type = known ? KC_WCH_NATIVE_UPD : KC_WCH_NATIVE_ADD;
                if (!known) (void)path_add(w, path);
            } else {
                event.type = type;
                if (type == KC_WCH_NATIVE_ADD) {
                    (void)path_add(w, path);
                } else if (type == KC_WCH_NATIVE_DEL) {
                    path_remove(w, path);
                }
            }

            if (emit) {
                event.path = path;
                w->handler(&event, w->userdata);
            }
        }

        w = next;
    }
}

/**
 * Records existing paths below one virtual-filesystem directory.
 * @param w Watcher instance.
 * @param dir Directory to scan.
 * @return None.
 */
static void kc_wch_wasm_scan(kc_wch_native_t *w, const char *dir) {
    DIR *directory = opendir(dir);
    struct dirent *entry;

    if (directory == NULL) return;

    while ((entry = readdir(directory)) != NULL) {
        char path[PATH_MAX];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if ((size_t)snprintf(
                path, sizeof(path), "%s/%s", dir, entry->d_name)
                >= sizeof(path)) {
            continue;
        }

        if (stat(path, &st) != 0) continue;
        (void)path_add(w, path);

        if (w->recursive && S_ISDIR(st.st_mode)) {
            kc_wch_wasm_scan(w, path);
        }
    }

    closedir(directory);
}

/**
 * Adds one watcher to the virtual-filesystem dispatch list.
 * @param w Watcher instance.
 * @return None.
 */
static void kc_wch_wasm_add(kc_wch_native_t *w) {
    if (!kc_wch_wasm_hooks_installed) {
        kc_wch_wasm_install_hooks();
        kc_wch_wasm_hooks_installed = 1;
    }

    w->wasm_next = kc_wch_wasm_watchers;
    kc_wch_wasm_watchers = w;
}

/**
 * Removes one watcher from the virtual-filesystem dispatch list.
 * @param w Watcher instance.
 * @return None.
 */
static void kc_wch_wasm_remove(kc_wch_native_t *w) {
    kc_wch_native_t **cursor = &kc_wch_wasm_watchers;

    while (*cursor != NULL) {
        if (*cursor == w) {
            *cursor = w->wasm_next;
            w->wasm_next = NULL;
            return;
        }
        cursor = &(*cursor)->wasm_next;
    }
}

#endif

/**
 * Waits internally for one normalized event.
 * @param w Watcher instance.
 * @param ev Event output.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return 1 on event, 0 on timeout, or -1 on error.
 */
static int kc_wch_wait(kc_wch_native_t *w, kc_wch_native_event_t *ev, int timeout_ms) {
#ifdef __EMSCRIPTEN__
    (void)timeout_ms;
#endif
    if (w == NULL || ev == NULL) return -1;
    if (dequeue(w, ev)) return 1;
    ev->type = -1;
    ev->path = NULL;
#ifdef __linux__
    if (w->backend == 1) {
        int rc = read_inotify(w, timeout_ms);
        if (rc < 0) return -1;
        if (rc > 0) return 0;
        fill_inotify(w);
        return dequeue(w, ev) ? 1 : 0;
    }
#endif
#ifdef __APPLE__
    if (w->backend == 2) {
        int rc = fill_kqueue(w, timeout_ms);
        if (rc < 0) return -1;
        return dequeue(w, ev) ? 1 : 0;
    }
#endif
#ifdef _WIN32
    if (w->backend == 3) {
        fill_windows(w, timeout_ms);
        return dequeue(w, ev) ? 1 : 0;
    }
#endif
    return -1;
}

/**
 * Releases all watcher resources.
 * @param w Watcher instance.
 * @return None.
 */
static void kc_wch_release(kc_wch_native_t *w) {
    int i;

    for (i = 0; i < w->path_count; i++) free(w->paths[i]);
    free(w->paths);
#ifdef __linux__
    if (w->backend == 1) {
        for (i = 0; i < w->wd_count; i++) free(w->wd_paths[i]);
        free(w->wds);
        free(w->wd_paths);
        close(w->ifd);
    }
#endif
#ifdef __APPLE__
    if (w->backend == 2) {
        for (i = 0; i < w->dir_count; i++) {
            close(w->dir_fds[i]);
            free(w->dir_paths[i]);
        }
        free(w->dir_fds);
        free(w->dir_paths);
        close(w->kq);
    }
#endif
#ifdef _WIN32
    if (w->backend == 3) {
        CloseHandle(w->ol.hEvent);
        CloseHandle(w->hdir);
    }
    if (w->thread_started && w->thread != NULL) {
        CloseHandle(w->thread);
    }
#endif
    free(w->root);
    free(w);
}

#ifndef __EMSCRIPTEN__
/**
 * Runs the asynchronous watcher loop.
 * @param arg Watcher instance.
 * @return Platform thread result.
 */
#ifdef _WIN32
static DWORD WINAPI kc_wch_worker(void *arg) {
#else
/**
 * Runs the asynchronous watcher loop on POSIX platforms.
 * @param arg Watcher instance.
 * @return Thread result pointer.
 */
static void *kc_wch_worker(void *arg) {
#endif
    kc_wch_native_t *w = (kc_wch_native_t *)arg;

    while (!atomic_load(&w->ready)) {
    }

    while (!atomic_load(&w->stop)) {
        kc_wch_native_event_t ev;
        int rc = kc_wch_wait(w, &ev, 100);

        if (rc < 0) break;
        if (rc > 0 && w->handler != NULL) {
            w->handler(&ev, w->userdata);
        }
    }

    if (w->self_close) {
#ifndef _WIN32
        pthread_detach(pthread_self());
#endif
        kc_wch_release(w);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#endif

/**
 * Opens one watcher instance.
 * @param out Output pointer for the new watcher.
 * @param path File or directory to watch.
 * @param options Optional watcher options.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
static int kc_wch_native_open(
    kc_wch_native_t **out,
    const char *path,
    const kc_wch_options_t *options
) {
    struct kc_wch_native *w;
    int exists = 0;
    int is_directory = 0;
    int recursive = options != NULL && options->recursive != 0;

    if (out == NULL) return KC_WCH_ERROR;
    *out = NULL;
    if (path == NULL || path[0] == '\0') return KC_WCH_ERROR;

#ifdef _WIN32
    {
        DWORD attrs = GetFileAttributesA(path);

        if (attrs != INVALID_FILE_ATTRIBUTES) {
            exists = 1;
            is_directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
    }
#else
    {
        struct stat st;

        if (stat(path, &st) == 0) {
            exists = 1;
            is_directory = S_ISDIR(st.st_mode);
        }
    }
#endif

    w = calloc(1, sizeof(*w));
    if (w == NULL) return KC_WCH_ERROR;
    atomic_init(&w->stop, 0);
    atomic_init(&w->ready, 0);

    if (exists && is_directory) {
        w->root = strdup(path);
    } else {
        char parent[PATH_MAX];
        char *slash;
        const char *name;

        snprintf(parent, PATH_MAX, "%s", path);
        slash = strrchr(parent, '/');
#ifdef _WIN32
        {
            char *backslash = strrchr(parent, '\\');

            if (backslash != NULL && (slash == NULL || backslash > slash)) {
                slash = backslash;
            }
        }
#endif
        if (slash != NULL) {
#ifdef _WIN32
            char separator = *slash;
#endif

            name = slash + 1;
            w->filter_name[0] = '/';
            snprintf(w->filter_name + 1, PATH_MAX - 1, "%s", name);
            *slash = '\0';

            if (parent[0] == '\0') {
                snprintf(parent, PATH_MAX, "/");
            }
#ifdef _WIN32
            if (strlen(parent) == 2U && parent[1] == ':') {
                parent[2] = separator;
                parent[3] = '\0';
            }
#endif
        } else {
            w->filter_name[0] = '/';
            snprintf(w->filter_name + 1, PATH_MAX - 1, "%s", path);
            snprintf(parent, PATH_MAX, ".");
        }

#ifdef _WIN32
        {
            DWORD attrs = GetFileAttributesA(parent);

            if (attrs == INVALID_FILE_ATTRIBUTES ||
                    (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                free(w);
                return KC_WCH_ERROR;
            }
        }
#else
        {
            struct stat st;

            if (stat(parent, &st) != 0 || !S_ISDIR(st.st_mode)) {
                free(w);
                return KC_WCH_ERROR;
            }
        }
#endif

        w->has_filter = 1;
        w->root = strdup(parent);
    }

    if (w->root == NULL) {
        free(w);
        return KC_WCH_ERROR;
    }

    w->recursive = recursive;
    if (!try_backend(w)) {
        free(w->root);
        free(w);
        return KC_WCH_ERROR;
    }

#ifdef __linux__
    if (w->backend == 1) {
        scan_watch_dir(w, w->root);
    }
#endif
#ifdef __EMSCRIPTEN__
    if (w->backend == 4 && !w->has_filter) {
        kc_wch_wasm_scan(w, w->root);
    }
#endif

    *out = w;
    return KC_WCH_OK;
}

/**
 * Registers the event handler and starts asynchronous observation.
 * @param w Watcher instance.
 * @param handler Event handler.
 * @param userdata Opaque caller value.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
static int kc_wch_native_on(kc_wch_native_t *w, kc_wch_native_handler_t handler, void *userdata) {
    if (w == NULL || handler == NULL || w->thread_started) {
        return KC_WCH_ERROR;
    }

    w->handler = handler;
    w->userdata = userdata;
#ifdef __EMSCRIPTEN__
    kc_wch_wasm_add(w);
#elif defined(_WIN32)
    w->thread = CreateThread(NULL, 0, kc_wch_worker, w, 0, NULL);
    if (w->thread == NULL) {
        w->handler = NULL;
        w->userdata = NULL;
        return KC_WCH_ERROR;
    }
#else
    if (pthread_create(&w->thread, NULL, kc_wch_worker, w) != 0) {
        w->handler = NULL;
        w->userdata = NULL;
        return KC_WCH_ERROR;
    }
#endif
    w->thread_started = 1;
    atomic_store(&w->ready, 1);
    return KC_WCH_OK;
}

/**
 * Closes one watcher and releases its resources.
 * @param w Watcher instance.
 * @return None.
 */
static void kc_wch_native_close(kc_wch_native_t *w) {
    if (w == NULL) return;

    atomic_store(&w->stop, 1);
    if (!w->thread_started) {
        kc_wch_release(w);
        return;
    }

#ifdef __EMSCRIPTEN__
    kc_wch_wasm_remove(w);
    kc_wch_release(w);
    return;
#elif defined(_WIN32)
    if (GetCurrentThreadId() == GetThreadId(w->thread)) {
        w->self_close = 1;
        return;
    }
    WaitForSingleObject(w->thread, INFINITE);
#else
    if (pthread_equal(pthread_self(), w->thread)) {
        w->self_close = 1;
        return;
    }
    pthread_join(w->thread, NULL);
#endif

    kc_wch_release(w);
}

struct kc_wch {
    char name[KC_WCH_NAME_MAX];
    char dir[KC_WCH_PATH_MAX];
    char *path;
    char *cmd;
    int recursive;
    kc_wch_handler_t handlers[3];
    void *userdata[3];
    atomic_int stop;
    int thread_started;
    uint64_t event_offset;
#ifdef _WIN32
    HANDLE thread;
#elif !defined(__EMSCRIPTEN__)
    pthread_t thread;
#endif
};

typedef struct {
    char **names;
    size_t count;
    size_t capacity;
    int failed;
} kc_wch_name_list_t;

/**
 * Duplicate one string.
 * @param text Source string.
 * @return Owned copy, or NULL on failure.
 */
static char *kc_wch_strdup(const char *text) {
    size_t size;
    char *copy;

    if (text == NULL) return NULL;
    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (copy == NULL) return NULL;
    memcpy(copy, text, size);
    return copy;
}

/**
 * Validate one resident watcher name.
 * @param name Watcher name.
 * @return Nonzero when valid.
 */
static int kc_wch_name_valid(const char *name) {
    const unsigned char *p;

    if (name == NULL || name[0] == '\0' ||
            strlen(name) >= KC_WCH_NAME_MAX) {
        return 0;
    }
    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Validate one metadata line value.
 * @param value Value to validate.
 * @return Nonzero when valid.
 */
static int kc_wch_line_valid(const char *value) {
    return value != NULL &&
        value[0] != '\0' &&
        strchr(value, '\n') == NULL &&
        strchr(value, '\r') == NULL;
}

/**
 * Resolve the default resident runtime directory.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_default_dir(char *out, size_t cap) {
#ifdef _WIN32
    char temp[MAX_PATH];
    DWORD size = GetTempPathA((DWORD)sizeof(temp), temp);

    if (size == 0 || size >= (DWORD)sizeof(temp)) return 1;
    return (size_t)snprintf(out, cap, "%swch.c", temp) < cap ? 0 : 1;
#else
    const char *xdg = getenv("XDG_RUNTIME_DIR");

    if (xdg != NULL && xdg[0] != '\0') {
        return (size_t)snprintf(out, cap, "%s/wch.c", xdg) < cap ? 0 : 1;
    }
    return (size_t)snprintf(
        out,
        cap,
        "/tmp/wch.c-%lu",
        (unsigned long)getuid()
    ) < cap ? 0 : 1;
#endif
}

/**
 * Resolve a configured or default resident runtime directory.
 * @param dir Configured directory, or NULL.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_resolve_dir(
    const char *dir,
    char *out,
    size_t cap
) {
    if (dir != NULL && dir[0] != '\0') {
        return (size_t)snprintf(out, cap, "%s", dir) < cap ? 0 : 1;
    }
    return kc_wch_default_dir(out, cap);
}

/**
 * Ensure one resident runtime directory exists.
 * @param dir Runtime directory.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_ensure_dir(const char *dir) {
#ifdef _WIN32
    char buffer[KC_WCH_PATH_MAX];
    char *p;

    if ((size_t)snprintf(buffer, sizeof(buffer), "%s", dir) >=
            sizeof(buffer)) {
        return 1;
    }
    for (p = buffer + 1; *p != '\0'; p++) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;

            *p = '\0';
            if (!CreateDirectoryA(buffer, NULL) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) {
                return 1;
            }
            *p = saved;
        }
    }
    if (!CreateDirectoryA(buffer, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
        return 1;
    }
    return 0;
#else
    char buffer[KC_WCH_PATH_MAX];
    char *p;

    if ((size_t)snprintf(buffer, sizeof(buffer), "%s", dir) >=
            sizeof(buffer)) {
        return 1;
    }
    for (p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0700) != 0 && errno != EEXIST) return 1;
            *p = '/';
        }
    }
    return mkdir(buffer, 0700) == 0 || errno == EEXIST ? 0 : 1;
#endif
}

/**
 * Compose one resident state path.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @param suffix State file suffix.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_state_path(
    char *out,
    size_t cap,
    const char *dir,
    const char *name,
    const char *suffix
) {
#ifdef _WIN32
    return (size_t)snprintf(
        out,
        cap,
        "%s\\%s%s",
        dir,
        name,
        suffix
    ) < cap ? 0 : 1;
#else
    return (size_t)snprintf(
        out,
        cap,
        "%s/%s%s",
        dir,
        name,
        suffix
    ) < cap ? 0 : 1;
#endif
}

/**
 * Remove trailing newline bytes from one mutable string.
 * @param text Mutable string.
 * @return None.
 */
static void kc_wch_chomp(char *text) {
    size_t length;

    if (text == NULL) return;
    length = strlen(text);
    while (length > 0U &&
            (text[length - 1U] == '\n' ||
             text[length - 1U] == '\r')) {
        text[--length] = '\0';
    }
}

/**
 * Persist one resident watcher registration.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_write_registration(
    const char *dir,
    const kc_wch_registration_t *registration
) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            registration->name,
            ".meta"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "wb");
    if (file == NULL) return 1;
    if (fprintf(
            file,
            "%d\n%s\n%s\n",
            registration->recursive,
            registration->path,
            registration->command
        ) < 0) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Read one resident watcher registration.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @param registration Output registration.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_read_registration(
    const char *dir,
    const char *name,
    kc_wch_registration_t *registration
) {
    char path[KC_WCH_PATH_MAX];
    char recursive[32];
    FILE *file;

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".meta"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "rb");
    if (file == NULL) return 1;

    memset(registration, 0, sizeof(*registration));
    snprintf(
        registration->name,
        sizeof(registration->name),
        "%s",
        name
    );
    if (fgets(recursive, sizeof(recursive), file) == NULL ||
            fgets(
                registration->path,
                sizeof(registration->path),
                file
            ) == NULL ||
            fgets(
                registration->command,
                sizeof(registration->command),
                file
            ) == NULL) {
        fclose(file);
        return 1;
    }
    fclose(file);

    kc_wch_chomp(recursive);
    kc_wch_chomp(registration->path);
    kc_wch_chomp(registration->command);
    registration->recursive = atoi(recursive) != 0;
    return 0;
}

/**
 * Write one resident watcher process identifier.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @param pid Process identifier.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_write_pid(
    const char *dir,
    const char *name,
    long pid
) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".pid"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "wb");
    if (file == NULL) return 1;
    if (fprintf(file, "%ld\n", pid) < 0) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Read one resident watcher process identifier.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @param out_pid Output process identifier.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_read_pid(
    const char *dir,
    const char *name,
    long *out_pid
) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".pid"
        ) != 0) {
        return 1;
    }
    file = fopen(path, "rb");
    if (file == NULL) return 1;
    if (fscanf(file, "%ld", out_pid) != 1) {
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

/**
 * Test whether one resident watcher process is alive.
 * @param pid Process identifier.
 * @return Nonzero when alive.
 */
static int kc_wch_pid_alive(long pid) {
#ifdef _WIN32
    HANDLE process;
    DWORD code;

    if (pid <= 0) return 0;
    process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        (DWORD)pid
    );
    if (process == NULL) return 0;
    if (!GetExitCodeProcess(process, &code)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);
    return code == STILL_ACTIVE;
#else
    if (pid <= 0) return 0;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

/**
 * Stop one resident watcher process.
 * @param pid Process identifier.
 * @return None.
 */
static void kc_wch_stop_pid(long pid) {
#ifdef _WIN32
    HANDLE process;

    if (pid <= 0) return;
    process = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (process != NULL) {
        TerminateProcess(process, 0);
        CloseHandle(process);
    }
#else
    if (pid > 0) (void)kill((pid_t)pid, SIGTERM);
#endif
}

/**
 * Remove resident watcher state files.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return None.
 */
static void kc_wch_remove_state(
    const char *dir,
    const char *name
) {
    static const char *suffixes[] = {
        ".meta",
        ".pid",
        ".events"
    };
    char path[KC_WCH_PATH_MAX];
    size_t i;

    for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (kc_wch_state_path(
                path,
                sizeof(path),
                dir,
                name,
                suffixes[i]
            ) == 0) {
            (void)remove(path);
        }
    }
}

/**
 * Reset the private resident event stream.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_reset_events(
    const char *dir,
    const char *name
) {
    char path[KC_WCH_PATH_MAX];
#ifdef _WIN32
    HANDLE file;
#else
    int fd;
#endif

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".events"
        ) != 0) {
        return 1;
    }
#ifdef _WIN32
    file = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) return 1;
    CloseHandle(file);
#else
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 1;
    close(fd);
#endif
    return 0;
}

/**
 * Append one normalized event to the private event stream.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @param type Normalized event type.
 * @param path Event path.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_append_event(
    const char *dir,
    const char *name,
    unsigned char type,
    const char *path
) {
    char event_path[KC_WCH_PATH_MAX];
    unsigned char *record;
    uint32_t path_size;
    size_t record_size;
    int rc = 1;

    if (path == NULL) return 1;
    if (strlen(path) > UINT32_MAX) return 1;
    path_size = (uint32_t)strlen(path);
    record_size = 1U + sizeof(path_size) + (size_t)path_size;
    record = (unsigned char *)malloc(record_size);
    if (record == NULL) return 1;

    record[0] = type;
    memcpy(record + 1U, &path_size, sizeof(path_size));
    if (path_size > 0U) {
        memcpy(
            record + 1U + sizeof(path_size),
            path,
            path_size
        );
    }

    if (kc_wch_state_path(
            event_path,
            sizeof(event_path),
            dir,
            name,
            ".events"
        ) != 0) {
        free(record);
        return 1;
    }

#ifdef _WIN32
    {
        HANDLE file = CreateFileA(
            event_path,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        DWORD written = 0;

        if (file != INVALID_HANDLE_VALUE) {
            if (record_size <= (size_t)UINT32_MAX &&
                    WriteFile(
                        file,
                        record,
                        (DWORD)record_size,
                        &written,
                        NULL
                    ) &&
                    written == (DWORD)record_size) {
                rc = 0;
            }
            CloseHandle(file);
        }
    }
#else
    {
        int fd = open(
            event_path,
            O_WRONLY | O_CREAT | O_APPEND,
            0600
        );

        if (fd >= 0) {
            size_t offset = 0U;

            while (offset < record_size) {
                ssize_t written = write(
                    fd,
                    record + offset,
                    record_size - offset
                );
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) break;
                offset += (size_t)written;
            }
            rc = offset == record_size ? 0 : 1;
            close(fd);
        }
    }
#endif
    free(record);
    return rc;
}

/**
 * Map one native event type to the resident event index.
 * @param type Native event type.
 * @return Event index, or negative one when invalid.
 */
static int kc_wch_event_index(int type) {
    if (type == KC_WCH_NATIVE_ADD) return 0;
    if (type == KC_WCH_NATIVE_UPD) return 1;
    if (type == KC_WCH_NATIVE_DEL) return 2;
    return -1;
}

/**
 * Map one resident event index to its command label.
 * @param index Event index.
 * @return Borrowed event label.
 */
static const char *kc_wch_event_label(int index) {
    if (index == 1) return "upd";
    if (index == 2) return "del";
    return "add";
}

#ifndef _WIN32
/**
 * Append one POSIX shell-quoted argument.
 * @param out Output command buffer.
 * @param cap Output buffer capacity.
 * @param pos Current output position.
 * @param value Argument value.
 * @return Zero on success, nonzero on overflow.
 */
static int kc_wch_shell_arg(
    char *out,
    size_t cap,
    size_t *pos,
    const char *value
) {
    size_t i;

    if (*pos + 1U >= cap) return 1;
    out[(*pos)++] = '\'';

    for (i = 0U; value[i] != '\0'; i++) {
        if (value[i] == '\'') {
            static const char escape[] = "'\\''";
            size_t size = sizeof(escape) - 1U;

            if (*pos + size >= cap) return 1;
            memcpy(out + *pos, escape, size);
            *pos += size;
        } else {
            if (*pos + 1U >= cap) return 1;
            out[(*pos)++] = value[i];
        }
    }

    if (*pos + 2U > cap) return 1;
    out[(*pos)++] = '\'';
    out[*pos] = '\0';
    return 0;
}
#endif

/**
 * Dispatch one native event from the resident watcher.
 * @param event Native watcher event.
 * @param userdata Resident dispatch configuration.
 * @return None.
 */
static void kc_wch_dispatch_event(
    const kc_wch_native_event_t *event,
    void *userdata
) {
    kc_wch_dispatch_t *dispatch = (kc_wch_dispatch_t *)userdata;
    int index;
    const char *label;

    if (event == NULL || event->path == NULL || dispatch == NULL) return;
    index = kc_wch_event_index(event->type);
    if (index < 0) return;
    label = kc_wch_event_label(index);

    (void)kc_wch_append_event(
        dispatch->dir,
        dispatch->name,
        (unsigned char)index,
        event->path
    );

    if (dispatch->command == NULL || dispatch->command[0] == '\0') return;

#ifdef _WIN32
    {
        char command[
            KC_WCH_CMD_MAX + KC_WCH_PATH_MAX + 128
        ];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;

        if ((size_t)snprintf(
                command,
                sizeof(command),
                "cmd.exe /c %s %s \"%s\"",
                dispatch->command,
                label,
                event->path
            ) >= sizeof(command)) {
            return;
        }
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (CreateProcessA(
                NULL,
                command,
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &startup,
                &process
            )) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
#else
    {
        pid_t pid = fork();

        if (pid == 0) {
            char command[
                KC_WCH_CMD_MAX + KC_WCH_PATH_MAX + 128
            ];
            size_t pos;
            size_t base = strlen(dispatch->command);

            if (base + 2U >= sizeof(command)) _exit(127);
            memcpy(command, dispatch->command, base);
            pos = base;
            command[pos++] = ' ';
            command[pos] = '\0';

            if (kc_wch_shell_arg(
                    command,
                    sizeof(command),
                    &pos,
                    label
                ) != 0) {
                _exit(127);
            }
            if (pos + 1U >= sizeof(command)) _exit(127);
            command[pos++] = ' ';
            command[pos] = '\0';
            if (kc_wch_shell_arg(
                    command,
                    sizeof(command),
                    &pos,
                    event->path
                ) != 0) {
                _exit(127);
            }
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(127);
        }
    }
#endif
}

/**
 * Run one resident watcher from persisted registration data.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return Process exit status.
 */
static int kc_wch_serve_resident(
    const char *dir,
    const char *name
) {
#ifdef __EMSCRIPTEN__
    (void)dir;
    (void)name;
    return 1;
#else
    kc_wch_registration_t registration;
    kc_wch_options_t options;
    kc_wch_dispatch_t dispatch;
    kc_wch_native_t *native = NULL;

    if (kc_wch_read_registration(
            dir,
            name,
            &registration
        ) != 0) {
        return 1;
    }

#ifndef _WIN32
    signal(SIGCHLD, SIG_IGN);
#endif

    memset(&options, 0, sizeof(options));
    options.recursive = registration.recursive;
    if (kc_wch_native_open(
            &native,
            registration.path,
            &options
        ) != KC_WCH_OK) {
        return 1;
    }

    dispatch.command = registration.command;
    dispatch.dir = dir;
    dispatch.name = name;
    if (kc_wch_native_on(
            native,
            kc_wch_dispatch_event,
            &dispatch
        ) != KC_WCH_OK) {
        kc_wch_native_close(native);
        return 1;
    }

#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;) pause();
#endif

    kc_wch_native_close(native);
    return 0;
#endif
}

#ifdef KC_WCH_CLI
/**
 * Run the private resident service entry for the CLI.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return Process exit status.
 */
int kc_wch_internal_serve(
    const char *dir,
    const char *name
) {
    return kc_wch_serve_resident(dir, name);
}
#endif

#ifdef _WIN32
/**
 * Locate the companion Windows wch executable.
 * @param out Output executable path.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_companion_exe(char *out, size_t cap) {
    const char *override = getenv("KC_WCH_EXE");
    HMODULE module;
    char module_path[KC_WCH_PATH_MAX];
    char *slash;
    char *backslash;
    DWORD size;

    if (override != NULL && override[0] != '\0') {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)&kc_wch_create,
            &module
        )) {
        return 1;
    }
    size = GetModuleFileNameA(
        module,
        module_path,
        (DWORD)sizeof(module_path)
    );
    if (size == 0 || size >= (DWORD)sizeof(module_path)) return 1;

    slash = strrchr(module_path, '/');
    backslash = strrchr(module_path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
    if (slash != NULL) {
        slash[1] = '\0';
    } else {
        module_path[0] = '\0';
    }

    return (size_t)snprintf(
        out,
        cap,
        "%swch.exe",
        module_path
    ) < cap ? 0 : 1;
}
#endif

/**
 * Start or replace one resident watcher process.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_run_update(
    const char *dir,
    const kc_wch_registration_t *registration
) {
    long old_pid;

#ifdef __EMSCRIPTEN__
    (void)dir;
    (void)registration;
    return 1;
#else
    if (kc_wch_ensure_dir(dir) != 0) return 1;
    if (kc_wch_read_pid(
            dir,
            registration->name,
            &old_pid
        ) == 0) {
        kc_wch_stop_pid(old_pid);
    }
    if (kc_wch_write_registration(dir, registration) != 0 ||
            kc_wch_reset_events(dir, registration->name) != 0) {
        return 1;
    }

#ifdef _WIN32
    {
        char exe[KC_WCH_PATH_MAX];
        char command[
            KC_WCH_PATH_MAX * 2 + KC_WCH_NAME_MAX + 64
        ];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;

        if (kc_wch_companion_exe(exe, sizeof(exe)) != 0) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
        if ((size_t)snprintf(
                command,
                sizeof(command),
                "\"%s\" --_serve \"%s\" \"%s\"",
                exe,
                registration->name,
                dir
            ) >= sizeof(command)) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }

        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);

        if (!CreateProcessA(
                NULL,
                command,
                NULL,
                NULL,
                FALSE,
                DETACHED_PROCESS | CREATE_NO_WINDOW,
                NULL,
                NULL,
                &startup,
                &process
            )) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }

        CloseHandle(process.hThread);
        if (kc_wch_write_pid(
                dir,
                registration->name,
                (long)process.dwProcessId
            ) != 0) {
            TerminateProcess(process.hProcess, 0);
            CloseHandle(process.hProcess);
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
        CloseHandle(process.hProcess);
    }
#else
    {
        pid_t pid = fork();

        if (pid < 0) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
        if (pid == 0) {
            if (setsid() < 0) _exit(1);
            _exit(kc_wch_serve_resident(
                dir,
                registration->name
            ));
        }
        if (kc_wch_write_pid(
                dir,
                registration->name,
                (long)pid
            ) != 0) {
            (void)kill(pid, SIGTERM);
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
    }
#endif
    return 0;
#endif
}

/**
 * Delete one resident watcher process and its state.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return Zero on success.
 */
static int kc_wch_run_delete(
    const char *dir,
    const char *name
) {
    long pid;

    if (kc_wch_read_pid(dir, name, &pid) == 0) {
        kc_wch_stop_pid(pid);
    }
    kc_wch_remove_state(dir, name);
    return 0;
}

/**
 * Load persisted configuration into one local handle.
 * @param w Watcher handle.
 * @return Zero on success, nonzero when the registration is absent.
 */
static int kc_wch_load_handle(kc_wch_t *w) {
    kc_wch_registration_t registration;
    char *path;
    char *cmd;

    if (w == NULL) return 1;
    if (kc_wch_read_registration(
            w->dir,
            w->name,
            &registration
        ) != 0) {
        free(w->path);
        free(w->cmd);
        w->path = NULL;
        w->cmd = NULL;
        w->recursive = 0;
        return 1;
    }

    path = kc_wch_strdup(registration.path);
    cmd = kc_wch_strdup(registration.command);
    if (path == NULL || cmd == NULL) {
        free(path);
        free(cmd);
        return 1;
    }
    free(w->path);
    free(w->cmd);
    w->path = path;
    w->cmd = cmd;
    w->recursive = registration.recursive;
    return 0;
}

/**
 * Return the current size of one private event stream.
 * @param dir Runtime directory.
 * @param name Watcher name.
 * @return Current stream size.
 */
static uint64_t kc_wch_event_size(
    const char *dir,
    const char *name
) {
    char path[KC_WCH_PATH_MAX];

    if (kc_wch_state_path(
            path,
            sizeof(path),
            dir,
            name,
            ".events"
        ) != 0) {
        return 0U;
    }
#ifdef _WIN32
    {
        WIN32_FILE_ATTRIBUTE_DATA data;
        ULARGE_INTEGER size;

        if (!GetFileAttributesExA(
                path,
                GetFileExInfoStandard,
                &data
            )) {
            return 0U;
        }
        size.LowPart = data.nFileSizeLow;
        size.HighPart = data.nFileSizeHigh;
        return (uint64_t)size.QuadPart;
    }
#else
    {
        struct stat st;

        if (stat(path, &st) != 0 || st.st_size < 0) return 0U;
        return (uint64_t)st.st_size;
    }
#endif
}

/**
 * Read one event record after the handle event offset.
 * @param w Watcher handle.
 * @param out_type Output event index.
 * @param out_path Output event path buffer.
 * @param cap Output path capacity.
 * @return One on event, zero when none, or negative one on error.
 */
static int kc_wch_read_event(
    kc_wch_t *w,
    int *out_type,
    char *out_path,
    size_t cap
) {
    char path[KC_WCH_PATH_MAX];
    unsigned char header[1U + sizeof(uint32_t)];
    uint32_t path_size;
    uint64_t file_size;

    if (w == NULL || out_type == NULL ||
            out_path == NULL || cap == 0U) {
        return -1;
    }
    if (kc_wch_state_path(
            path,
            sizeof(path),
            w->dir,
            w->name,
            ".events"
        ) != 0) {
        return -1;
    }

    file_size = kc_wch_event_size(w->dir, w->name);
    if (file_size < w->event_offset) {
        w->event_offset = 0U;
    }
    if (file_size - w->event_offset < sizeof(header)) return 0;

#ifdef _WIN32
    {
        HANDLE file = CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        LARGE_INTEGER offset;
        DWORD read_size;

        if (file == INVALID_HANDLE_VALUE) return 0;
        offset.QuadPart = (LONGLONG)w->event_offset;
        if (!SetFilePointerEx(
                file,
                offset,
                NULL,
                FILE_BEGIN
            ) ||
                !ReadFile(
                    file,
                    header,
                    (DWORD)sizeof(header),
                    &read_size,
                    NULL
                ) ||
                read_size != (DWORD)sizeof(header)) {
            CloseHandle(file);
            return 0;
        }
        memcpy(&path_size, header + 1U, sizeof(path_size));
        if ((uint64_t)path_size >
                file_size - w->event_offset - sizeof(header)) {
            CloseHandle(file);
            return 0;
        }
        if ((size_t)path_size + 1U > cap) {
            CloseHandle(file);
            return -1;
        }
        if (path_size > 0U &&
                (!ReadFile(
                    file,
                    out_path,
                    path_size,
                    &read_size,
                    NULL
                ) ||
                read_size != path_size)) {
            CloseHandle(file);
            return 0;
        }
        CloseHandle(file);
    }
#else
    {
        int fd = open(path, O_RDONLY);
        ssize_t got;
        size_t offset;

        if (fd < 0) return 0;
        if (lseek(
                fd,
                (off_t)w->event_offset,
                SEEK_SET
            ) < 0) {
            close(fd);
            return -1;
        }
        offset = 0U;
        while (offset < sizeof(header)) {
            got = read(
                fd,
                header + offset,
                sizeof(header) - offset
            );
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) {
                close(fd);
                return 0;
            }
            offset += (size_t)got;
        }
        memcpy(&path_size, header + 1U, sizeof(path_size));
        if ((uint64_t)path_size >
                file_size - w->event_offset - sizeof(header)) {
            close(fd);
            return 0;
        }
        if ((size_t)path_size + 1U > cap) {
            close(fd);
            return -1;
        }
        offset = 0U;
        while (offset < (size_t)path_size) {
            got = read(
                fd,
                out_path + offset,
                (size_t)path_size - offset
            );
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) {
                close(fd);
                return 0;
            }
            offset += (size_t)got;
        }
        close(fd);
    }
#endif

    out_path[path_size] = '\0';
    *out_type = (int)header[0];
    w->event_offset += sizeof(header) + (uint64_t)path_size;
    return 1;
}

/**
 * Sleep briefly between resident event stream checks.
 * @return None.
 */
static void kc_wch_subscription_sleep(void) {
#ifdef _WIN32
    Sleep(25);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 25000000L;
    nanosleep(&delay, NULL);
#endif
}

#ifndef __EMSCRIPTEN__
/**
 * Run one local temporary subscription receiver.
 * @param arg Watcher handle.
 * @return Platform thread result.
 */
#ifdef _WIN32
static DWORD WINAPI kc_wch_subscription_worker(void *arg) {
#else
static void *kc_wch_subscription_worker(void *arg) {
#endif
    kc_wch_t *w = (kc_wch_t *)arg;

    while (!atomic_load(&w->stop)) {
        char path[KC_WCH_PATH_MAX];
        int type;
        int rc = kc_wch_read_event(
            w,
            &type,
            path,
            sizeof(path)
        );

        if (rc < 0) break;
        if (rc == 0) {
            kc_wch_subscription_sleep();
            continue;
        }
        if (type >= 0 && type < 3 &&
                w->handlers[type] != NULL) {
            w->handlers[type](
                path,
                w->userdata[type]
            );
        }
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
#endif

/**
 * Start the temporary subscription worker when required.
 * @param w Watcher handle.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
static int kc_wch_start_subscription(kc_wch_t *w) {
    if (w == NULL) return KC_WCH_ERROR;
    if (w->thread_started) return KC_WCH_OK;
#ifdef __EMSCRIPTEN__
    return KC_WCH_ERROR;
#elif defined(_WIN32)
    w->event_offset = kc_wch_event_size(w->dir, w->name);
    w->thread = CreateThread(
        NULL,
        0,
        kc_wch_subscription_worker,
        w,
        0,
        NULL
    );
    if (w->thread == NULL) return KC_WCH_ERROR;
#else
    w->event_offset = kc_wch_event_size(w->dir, w->name);
    if (pthread_create(
            &w->thread,
            NULL,
            kc_wch_subscription_worker,
            w
        ) != 0) {
        return KC_WCH_ERROR;
    }
#endif
    w->thread_started = 1;
    return KC_WCH_OK;
}

/**
 * Collect one watcher name for listing.
 * @param list Name collection.
 * @param name Watcher name.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_name_list_add(
    kc_wch_name_list_t *list,
    const char *name
) {
    char **names;
    char *copy;
    size_t capacity;

    if (list->count == list->capacity) {
        capacity = list->capacity == 0U
            ? 8U
            : list->capacity * 2U;
        names = (char **)realloc(
            list->names,
            capacity * sizeof(*names)
        );
        if (names == NULL) return 1;
        list->names = names;
        list->capacity = capacity;
    }
    copy = kc_wch_strdup(name);
    if (copy == NULL) return 1;
    list->names[list->count++] = copy;
    return 0;
}

/**
 * Release one watcher name collection.
 * @param list Name collection.
 * @return None.
 */
static void kc_wch_name_list_clear(kc_wch_name_list_t *list) {
    size_t i;

    if (list == NULL) return;
    for (i = 0U; i < list->count; i++) free(list->names[i]);
    free(list->names);
    memset(list, 0, sizeof(*list));
}

/**
 * Collect watcher registration names from one runtime directory.
 * @param dir Runtime directory.
 * @param list Output name collection.
 * @return Zero on success, nonzero on failure.
 */
static int kc_wch_collect_names(
    const char *dir,
    kc_wch_name_list_t *list
) {
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE find;
    char pattern[KC_WCH_PATH_MAX];

    if ((size_t)snprintf(
            pattern,
            sizeof(pattern),
            "%s\\*.meta",
            dir
        ) >= sizeof(pattern)) {
        return 1;
    }
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t length = strlen(data.cFileName);

        if (length > 5U &&
                strcmp(
                    data.cFileName + length - 5U,
                    ".meta"
                ) == 0) {
            data.cFileName[length - 5U] = '\0';
            if (kc_wch_name_list_add(
                    list,
                    data.cFileName
                ) != 0) {
                FindClose(find);
                return 1;
            }
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *directory = opendir(dir);
    struct dirent *entry;

    if (directory == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);

        if (length > 5U &&
                strcmp(
                    entry->d_name + length - 5U,
                    ".meta"
                ) == 0) {
            char name[KC_WCH_NAME_MAX];

            if (length - 5U >= sizeof(name)) continue;
            memcpy(name, entry->d_name, length - 5U);
            name[length - 5U] = '\0';
            if (kc_wch_name_list_add(list, name) != 0) {
                closedir(directory);
                return 1;
            }
        }
    }
    closedir(directory);
#endif
    return 0;
}

/**
 * Create or replace one named resident watcher.
 * @param name Watcher name.
 * @param options Creation options.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_create(
    const char *name,
    const kc_wch_options_t *options
) {
    char dir[KC_WCH_PATH_MAX];
    kc_wch_registration_t registration;

    if (!kc_wch_name_valid(name) ||
            options == NULL ||
            !kc_wch_line_valid(options->path) ||
            !kc_wch_line_valid(options->cmd)) {
        return KC_WCH_ERROR;
    }
    if (kc_wch_resolve_dir(
            options->dir,
            dir,
            sizeof(dir)
        ) != 0) {
        return KC_WCH_ERROR;
    }

    memset(&registration, 0, sizeof(registration));
    if ((size_t)snprintf(
            registration.name,
            sizeof(registration.name),
            "%s",
            name
        ) >= sizeof(registration.name) ||
            (size_t)snprintf(
                registration.path,
                sizeof(registration.path),
                "%s",
                options->path
            ) >= sizeof(registration.path) ||
            (size_t)snprintf(
                registration.command,
                sizeof(registration.command),
                "%s",
                options->cmd
            ) >= sizeof(registration.command)) {
        return KC_WCH_ERROR;
    }
    registration.recursive = options->recursive != 0;
    return kc_wch_run_update(
        dir,
        &registration
    ) == 0 ? KC_WCH_OK : KC_WCH_ERROR;
}

/**
 * Open one local handle bound to a watcher name.
 * @param out Output watcher handle.
 * @param name Watcher name.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_open(
    kc_wch_t **out,
    const char *name
) {
    kc_wch_t *w;

    if (out == NULL) return KC_WCH_ERROR;
    *out = NULL;
    if (!kc_wch_name_valid(name)) return KC_WCH_ERROR;

    w = (kc_wch_t *)calloc(1, sizeof(*w));
    if (w == NULL) return KC_WCH_ERROR;
    if ((size_t)snprintf(
            w->name,
            sizeof(w->name),
            "%s",
            name
        ) >= sizeof(w->name) ||
            kc_wch_default_dir(
                w->dir,
                sizeof(w->dir)
            ) != 0) {
        free(w);
        return KC_WCH_ERROR;
    }
    atomic_init(&w->stop, 0);
    (void)kc_wch_load_handle(w);
    *out = w;
    return KC_WCH_OK;
}

/**
 * List registered resident watchers.
 * @param dir Runtime directory, or NULL.
 * @param out_entries Output entry array.
 * @param out_count Output entry count.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_list(
    const char *dir,
    kc_wch_entry_t **out_entries,
    size_t *out_count
) {
    char resolved[KC_WCH_PATH_MAX];
    kc_wch_name_list_t names;
    kc_wch_registration_t *registrations = NULL;
    int *running = NULL;
    kc_wch_entry_t *entries;
    char *cursor;
    size_t bytes;
    size_t valid;
    size_t i;

    if (out_entries != NULL) *out_entries = NULL;
    if (out_count != NULL) *out_count = 0U;
    if (out_entries == NULL || out_count == NULL)
        return KC_WCH_ERROR;
    if (kc_wch_resolve_dir(
            dir,
            resolved,
            sizeof(resolved)
        ) != 0) {
        return KC_WCH_ERROR;
    }

    memset(&names, 0, sizeof(names));
    if (kc_wch_collect_names(resolved, &names) != 0) {
        kc_wch_name_list_clear(&names);
        return KC_WCH_ERROR;
    }
    if (names.count == 0U) return KC_WCH_OK;

    registrations = (kc_wch_registration_t *)calloc(
        names.count,
        sizeof(*registrations)
    );
    running = (int *)calloc(names.count, sizeof(*running));
    if (registrations == NULL || running == NULL) {
        free(registrations);
        free(running);
        kc_wch_name_list_clear(&names);
        return KC_WCH_ERROR;
    }

    bytes = 0U;
    valid = 0U;
    for (i = 0U; i < names.count; i++) {
        long pid = 0;

        if (kc_wch_read_registration(
                resolved,
                names.names[i],
                &registrations[valid]
            ) != 0) {
            continue;
        }
        if (kc_wch_read_pid(
                resolved,
                names.names[i],
                &pid
            ) == 0) {
            running[valid] = kc_wch_pid_alive(pid);
        }
        bytes += strlen(registrations[valid].name) + 1U;
        bytes += strlen(registrations[valid].path) + 1U;
        bytes += strlen(registrations[valid].command) + 1U;
        valid++;
    }

    if (valid == 0U) {
        free(registrations);
        free(running);
        kc_wch_name_list_clear(&names);
        return KC_WCH_OK;
    }

    if (valid > ((size_t)-1) / sizeof(*entries) ||
            bytes > (size_t)-1 - valid * sizeof(*entries)) {
        free(registrations);
        free(running);
        kc_wch_name_list_clear(&names);
        return KC_WCH_ERROR;
    }
    entries = (kc_wch_entry_t *)malloc(
        valid * sizeof(*entries) + bytes
    );
    if (entries == NULL) {
        free(registrations);
        free(running);
        kc_wch_name_list_clear(&names);
        return KC_WCH_ERROR;
    }
    cursor = (char *)(entries + valid);

    for (i = 0U; i < valid; i++) {
        size_t size = strlen(registrations[i].name) + 1U;

        memcpy(cursor, registrations[i].name, size);
        entries[i].name = cursor;
        cursor += size;

        size = strlen(registrations[i].path) + 1U;
        memcpy(cursor, registrations[i].path, size);
        entries[i].path = cursor;
        cursor += size;

        size = strlen(registrations[i].command) + 1U;
        memcpy(cursor, registrations[i].command, size);
        entries[i].cmd = cursor;
        cursor += size;

        entries[i].recursive = registrations[i].recursive;
        entries[i].running = running[i];
    }

    free(registrations);
    free(running);
    kc_wch_name_list_clear(&names);
    *out_entries = entries;
    *out_count = valid;
    return KC_WCH_OK;
}

/**
 * Delete one named resident watcher.
 * @param name Watcher name.
 * @param dir Runtime directory, or NULL.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_delete(
    const char *name,
    const char *dir
) {
    char resolved[KC_WCH_PATH_MAX];

    if (!kc_wch_name_valid(name)) return KC_WCH_ERROR;
    if (kc_wch_resolve_dir(
            dir,
            resolved,
            sizeof(resolved)
        ) != 0) {
        return KC_WCH_ERROR;
    }
    (void)kc_wch_run_delete(resolved, name);
    return KC_WCH_OK;
}

/**
 * Replace the watched path.
 * @param w Watcher handle.
 * @param path Watched path.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_path(
    kc_wch_t *w,
    const char *path
) {
    kc_wch_registration_t registration;

    if (w == NULL || !kc_wch_line_valid(path))
        return KC_WCH_ERROR;
    if (kc_wch_read_registration(
            w->dir,
            w->name,
            &registration
        ) != 0) {
        return KC_WCH_NOT_FOUND;
    }
    if ((size_t)snprintf(
            registration.path,
            sizeof(registration.path),
            "%s",
            path
        ) >= sizeof(registration.path)) {
        return KC_WCH_ERROR;
    }
    if (kc_wch_run_update(w->dir, &registration) != 0)
        return KC_WCH_ERROR;
    return kc_wch_load_handle(w) == 0
        ? KC_WCH_OK
        : KC_WCH_ERROR;
}

/**
 * Return the configured watched path.
 * @param w Watcher handle.
 * @return Borrowed path string, or NULL.
 */
const char *kc_wch_get_path(const kc_wch_t *w) {
    return w != NULL ? w->path : NULL;
}

/**
 * Replace the persistent event command.
 * @param w Watcher handle.
 * @param cmd Event command.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_cmd(
    kc_wch_t *w,
    const char *cmd
) {
    kc_wch_registration_t registration;

    if (w == NULL || !kc_wch_line_valid(cmd))
        return KC_WCH_ERROR;
    if (kc_wch_read_registration(
            w->dir,
            w->name,
            &registration
        ) != 0) {
        return KC_WCH_NOT_FOUND;
    }
    if ((size_t)snprintf(
            registration.command,
            sizeof(registration.command),
            "%s",
            cmd
        ) >= sizeof(registration.command)) {
        return KC_WCH_ERROR;
    }
    if (kc_wch_run_update(w->dir, &registration) != 0)
        return KC_WCH_ERROR;
    return kc_wch_load_handle(w) == 0
        ? KC_WCH_OK
        : KC_WCH_ERROR;
}

/**
 * Return the configured event command.
 * @param w Watcher handle.
 * @return Borrowed command string, or NULL.
 */
const char *kc_wch_get_cmd(const kc_wch_t *w) {
    return w != NULL ? w->cmd : NULL;
}

/**
 * Change the runtime directory targeted by a watcher handle.
 * @param w Watcher handle.
 * @param dir Runtime directory, or NULL.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_set_dir(
    kc_wch_t *w,
    const char *dir
) {
    char resolved[KC_WCH_PATH_MAX];

    if (w == NULL) return KC_WCH_ERROR;
    if (kc_wch_resolve_dir(
            dir,
            resolved,
            sizeof(resolved)
        ) != 0) {
        return KC_WCH_ERROR;
    }
    if ((size_t)snprintf(
            w->dir,
            sizeof(w->dir),
            "%s",
            resolved
        ) >= sizeof(w->dir)) {
        return KC_WCH_ERROR;
    }
    (void)kc_wch_load_handle(w);
    return KC_WCH_OK;
}

/**
 * Return the runtime directory targeted by a watcher handle.
 * @param w Watcher handle.
 * @return Borrowed runtime directory, or NULL.
 */
const char *kc_wch_get_dir(const kc_wch_t *w) {
    return w != NULL ? w->dir : NULL;
}

/**
 * Replace recursive observation mode.
 * @param w Watcher handle.
 * @param recursive Nonzero enables recursive mode.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_recursive(
    kc_wch_t *w,
    int recursive
) {
    kc_wch_registration_t registration;

    if (w == NULL) return KC_WCH_ERROR;
    if (kc_wch_read_registration(
            w->dir,
            w->name,
            &registration
        ) != 0) {
        return KC_WCH_NOT_FOUND;
    }
    registration.recursive = recursive != 0;
    if (kc_wch_run_update(w->dir, &registration) != 0)
        return KC_WCH_ERROR;
    return kc_wch_load_handle(w) == 0
        ? KC_WCH_OK
        : KC_WCH_ERROR;
}

/**
 * Return recursive observation mode.
 * @param w Watcher handle.
 * @return Nonzero when recursive, otherwise zero.
 */
int kc_wch_get_recursive(const kc_wch_t *w) {
    return w != NULL && w->recursive != 0;
}

/**
 * Register or clear one temporary event subscription.
 * @param w Watcher handle.
 * @param event Event name.
 * @param handler Event handler, or NULL.
 * @param userdata Opaque handler data.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_on(
    kc_wch_t *w,
    const char *event,
    kc_wch_handler_t handler,
    void *userdata
) {
    int index;

    if (w == NULL || event == NULL) return KC_WCH_ERROR;
    if (strcmp(event, "add") == 0) index = 0;
    else if (strcmp(event, "upd") == 0) index = 1;
    else if (strcmp(event, "del") == 0) index = 2;
    else return KC_WCH_ERROR;

    w->handlers[index] = handler;
    w->userdata[index] = userdata;
    if (handler == NULL) return KC_WCH_OK;
    if (kc_wch_read_registration(
            w->dir,
            w->name,
            &(kc_wch_registration_t){0}
        ) != 0) {
        w->handlers[index] = NULL;
        w->userdata[index] = NULL;
        return KC_WCH_NOT_FOUND;
    }
    return kc_wch_start_subscription(w);
}

/**
 * Release memory returned by wch.
 * @param ptr Owned allocation, or NULL.
 * @return None.
 */
void kc_wch_free(void *ptr) {
    free(ptr);
}

/**
 * Close and release one local watcher handle.
 * @param w Watcher handle, or NULL.
 * @return None.
 */
void kc_wch_close(kc_wch_t *w) {
    if (w == NULL) return;

    atomic_store(&w->stop, 1);
#ifndef __EMSCRIPTEN__
    if (w->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(w->thread, INFINITE);
        CloseHandle(w->thread);
#else
        pthread_join(w->thread, NULL);
#endif
    }
#endif
    free(w->path);
    free(w->cmd);
    free(w);
}

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_wch_version(void) {
    return (uint64_t)KC_WCH_BUILD_VERSION;
}
