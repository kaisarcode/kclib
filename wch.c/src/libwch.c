/**
 * libwch.c - File and directory change notification library
 * Summary: Portable native file watcher - inotify, kqueue, ReadDirectoryChangesW.
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
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdatomic.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
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

struct kc_wch {
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

    kc_wch_handler_t handler;
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
    struct kc_wch *wasm_next;
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
static int path_find(struct kc_wch *w, const char *p) {
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
static int path_add(struct kc_wch *w, const char *p) {
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
static void path_remove(struct kc_wch *w, const char *p) {
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
static int filter_ok(struct kc_wch *w, const char *path) {
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
static void queue_push(struct kc_wch *w, int type, const char *path) {
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
static int dequeue(struct kc_wch *w, kc_wch_event_t *ev) {
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
static int try_backend(struct kc_wch *w) {
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
static int wd_lookup(struct kc_wch *w, int wd) {
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
static int wd_add(struct kc_wch *w, const char *dir) {
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
static void wd_remove(struct kc_wch *w, int wd) {
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
static void scan_watch_dir(struct kc_wch *w, const char *dir) {
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
static int read_inotify(struct kc_wch *w, int tmo) {
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
static void fill_inotify(struct kc_wch *w) {
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
            queue_push(w, KC_WCH_DEL, fp);
            path_remove(w, fp);
        } else if (iev->mask & IN_MOVED_TO) {
            if (iev->mask & IN_ISDIR && w->recursive)
                scan_watch_dir(w, fp);
            queue_push(w, KC_WCH_ADD, fp);
            path_add(w, fp);
        } else if (iev->mask & IN_CLOSE_WRITE) {
            queue_push(w, KC_WCH_UPD, fp);
            path_add(w, fp);
        } else if (iev->mask & IN_CREATE) {
            if (iev->mask & IN_ISDIR && w->recursive)
                scan_watch_dir(w, fp);
            queue_push(w, KC_WCH_ADD, fp);
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
static int kq_lookup(struct kc_wch *w, int fd) {
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
static void kq_add_dir(struct kc_wch *w, const char *dir) {
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
static void kq_dir_remove(struct kc_wch *w, int fd) {
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
static void kq_scan_diff(struct kc_wch *w) {
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
            queue_push(w, KC_WCH_ADD, ents[ci]);
            ci++;
        } else if (cmp > 0) {
            queue_push(w, KC_WCH_DEL, old[oi]);
            oi++;
        } else {
            if (mtimes[ci] != om[oi] || sizes[ci] != oz[oi])
                queue_push(w, KC_WCH_UPD, ents[ci]);
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
static int fill_kqueue(struct kc_wch *w, int tmo) {
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
        queue_push(w, KC_WCH_DEL, w->dir_paths[idx]);
        kq_dir_remove(w, (int)ev.ident);
    } else if (ev.fflags & NOTE_RENAME) {
        queue_push(w, KC_WCH_DEL, w->dir_paths[idx]);
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
static void fill_windows(struct kc_wch *w, int tmo) {
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
                queue_push(w, KC_WCH_ADD, fp);
                path_add(w, fp);
                break;
            case FILE_ACTION_MODIFIED:
                queue_push(w, KC_WCH_UPD, fp);
                path_add(w, fp);
                break;
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME:
                queue_push(w, KC_WCH_DEL, fp);
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

static kc_wch_t *kc_wch_wasm_watchers = NULL;
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
static int kc_wch_wasm_matches(const kc_wch_t *w, const char *path) {
    size_t root_len;

    if (w == NULL || path == NULL) return 0;

    if (w->has_filter) {
        return filter_ok((kc_wch_t *)w, path);
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
    kc_wch_t *w = kc_wch_wasm_watchers;

    while (w != NULL) {
        kc_wch_t *next = w->wasm_next;

        if (!atomic_load(&w->stop) &&
                w->handler != NULL &&
                kc_wch_wasm_matches(w, path)) {
            kc_wch_event_t event;
            int known = path_find(w, path) >= 0;
            int emit = 1;

            if (type == 3) {
                if (known) {
                    emit = 0;
                } else {
                    event.type = KC_WCH_ADD;
                    (void)path_add(w, path);
                }
            } else if (type == 4) {
                event.type = known ? KC_WCH_UPD : KC_WCH_ADD;
                if (!known) (void)path_add(w, path);
            } else {
                event.type = type;
                if (type == KC_WCH_ADD) {
                    (void)path_add(w, path);
                } else if (type == KC_WCH_DEL) {
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
static void kc_wch_wasm_scan(kc_wch_t *w, const char *dir) {
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
static void kc_wch_wasm_add(kc_wch_t *w) {
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
static void kc_wch_wasm_remove(kc_wch_t *w) {
    kc_wch_t **cursor = &kc_wch_wasm_watchers;

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
static int kc_wch_wait(kc_wch_t *w, kc_wch_event_t *ev, int timeout_ms) {
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
static void kc_wch_release(kc_wch_t *w) {
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
    kc_wch_t *w = (kc_wch_t *)arg;

    while (!atomic_load(&w->ready)) {
    }

    while (!atomic_load(&w->stop)) {
        kc_wch_event_t ev;
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
int kc_wch_open(
    kc_wch_t **out,
    const char *path,
    const kc_wch_options_t *options
) {
    struct kc_wch *w;
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
int kc_wch_on(kc_wch_t *w, kc_wch_handler_t handler, void *userdata) {
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
void kc_wch_close(kc_wch_t *w) {
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

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_wch_version(void) {
    return (uint64_t)KC_WCH_BUILD_VERSION;
}
