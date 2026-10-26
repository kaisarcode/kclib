/**
 * librdvz.c - Named peer rendezvous relationships.
 * Summary: Cross-process named peers with undirected direct relationships.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librdvz.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include <sys/wait.h>
#include <time.h>
#endif
#endif

#ifndef KC_RDVZ_BUILD_VERSION
#define KC_RDVZ_BUILD_VERSION 0
#endif

#define KC_RDVZ_ID_MAX 128
#define KC_RDVZ_CMD_MAX 4096
#define KC_RDVZ_PATH_MAX 1024
#define KC_RDVZ_MAGIC UINT32_C(0x52445632)

typedef struct {
    char id[KC_RDVZ_ID_MAX];
    char command[KC_RDVZ_CMD_MAX];
    uint64_t generation;
} kc_rdvz_record_t;

typedef struct {
    char first[KC_RDVZ_ID_MAX];
    char second[KC_RDVZ_ID_MAX];
} kc_rdvz_link_t;

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t links;
    uint32_t reserved;
    uint64_t next_generation;
} kc_rdvz_header_t;

typedef struct {
    kc_rdvz_record_t *peers;
    size_t count;
    kc_rdvz_link_t *links;
    size_t link_count;
    uint64_t next_generation;
} kc_rdvz_state_t;

typedef struct {
    char dir[KC_RDVZ_PATH_MAX];
#ifdef _WIN32
    HANDLE file;
#else
    int fd;
#endif
} kc_rdvz_lock_t;

struct kc_rdvz_peer {
    char id[KC_RDVZ_ID_MAX];
    char dir[KC_RDVZ_PATH_MAX];
    uint64_t generation;
    uint64_t event_offset;
    kc_rdvz_handler_t on_connect;
    void *userdata;
    atomic_int stop;
    int started;
#ifdef _WIN32
    HANDLE thread;
#elif !defined(__EMSCRIPTEN__)
    pthread_t thread;
#endif
};

#ifdef _WIN32
static SRWLOCK kc_rdvz_process_lock = SRWLOCK_INIT;
#elif !defined(__EMSCRIPTEN__)
static pthread_mutex_t kc_rdvz_process_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * Validate an identifier accepted as a filename and peer key.
 * @param id Identifier to inspect.
 * @return Nonzero for a valid peer identifier.
 */
static int kc_rdvz_valid(const char *id)
{
    const unsigned char *p;

    if (!id || !id[0] || strlen(id) >= KC_RDVZ_ID_MAX ||
            strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return 0;
    for (p = (const unsigned char *)id; *p; p++) {
        if (isalnum(*p) || *p == '.' || *p == '_' || *p == '-') continue;
        return 0;
    }
    return 1;
}

/**
 * Resolve the per-user local rendezvous directory.
 * @param out Destination path.
 * @param cap Destination capacity.
 * @return Zero on success.
 */
static int kc_rdvz_directory(char *out, size_t cap)
{
    const char *override = getenv("KC_RDVZ_DIR");

    if (override && override[0])
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
#ifdef _WIN32
    {
        char temp[MAX_PATH];
        DWORD count = GetTempPathA((DWORD)sizeof(temp), temp);
        if (!count || count >= (DWORD)sizeof(temp)) return 1;
        return (size_t)snprintf(out, cap, "%skaisarcode\\rdvz.c", temp)
            < cap ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_RUNTIME_DIR");
        char user_dir[KC_RDVZ_PATH_MAX];
        struct stat st;
        if (xdg && xdg[0])
            return (size_t)snprintf(out, cap, "%s/kaisarcode/rdvz.c", xdg)
                < cap ? 0 : 1;
        if ((size_t)snprintf(user_dir, sizeof(user_dir),
                "/run/user/%lu", (unsigned long)getuid()) < sizeof(user_dir) &&
                stat(user_dir, &st) == 0 && S_ISDIR(st.st_mode))
            return (size_t)snprintf(out, cap, "%s/kaisarcode/rdvz.c",
                user_dir) < cap ? 0 : 1;
        return (size_t)snprintf(out, cap, "/tmp/kaisarcode/rdvz.c-%lu",
            (unsigned long)getuid()) < cap ? 0 : 1;
    }
#endif
}

/**
 * Compose a runtime state or event filename.
 * @param out Destination path.
 * @param cap Destination capacity.
 * @param dir Runtime directory.
 * @param name Filename.
 * @return Zero on success.
 */
static int kc_rdvz_path(char *out, size_t cap, const char *dir,
    const char *name)
{
#ifdef _WIN32
    return (size_t)snprintf(out, cap, "%s\\%s", dir, name) < cap ? 0 : 1;
#else
    return (size_t)snprintf(out, cap, "%s/%s", dir, name) < cap ? 0 : 1;
#endif
}

/**
 * Ensure the private runtime directory exists.
 * @param dir Runtime directory path.
 * @return Zero on success.
 */
static int kc_rdvz_mkdir(const char *dir)
{
    char path[KC_RDVZ_PATH_MAX];
    char *p;

    if ((size_t)snprintf(path, sizeof(path), "%s", dir) >= sizeof(path))
        return 1;
    for (p = path + 1; *p; p++) {
#ifdef _WIN32
        if (*p != '/' && *p != '\\') continue;
#else
        if (*p != '/') continue;
#endif
        {
            char saved = *p;
            *p = '\0';
#ifdef _WIN32
            if (!CreateDirectoryA(path, NULL) &&
                    GetLastError() != ERROR_ALREADY_EXISTS) return 1;
#else
            if (mkdir(path, 0700) != 0 && errno != EEXIST) return 1;
#endif
            *p = saved;
        }
    }
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) ||
        GetLastError() == ERROR_ALREADY_EXISTS ? 0 : 1;
#else
    return mkdir(path, 0700) == 0 || errno == EEXIST ? 0 : 1;
#endif
}

/**
 * Acquire the process-local and cross-process state lock.
 * @param lock Receives the locked runtime directory.
 * @return KC_RDVZ_OK or KC_RDVZ_EIO.
 */
static int kc_rdvz_begin(kc_rdvz_lock_t *lock)
{
    char path[KC_RDVZ_PATH_MAX];

    memset(lock, 0, sizeof(*lock));
#ifdef _WIN32
    AcquireSRWLockExclusive(&kc_rdvz_process_lock);
#else
#ifndef __EMSCRIPTEN__
    (void)pthread_mutex_lock(&kc_rdvz_process_lock);
#endif
#endif
    if (kc_rdvz_directory(lock->dir, sizeof(lock->dir)) ||
            kc_rdvz_mkdir(lock->dir) ||
            kc_rdvz_path(path, sizeof(path), lock->dir, ".lock"))
        goto error;
#ifdef _WIN32
    {
        OVERLAPPED overlapped;
        lock->file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (lock->file == INVALID_HANDLE_VALUE) goto error;
        memset(&overlapped, 0, sizeof(overlapped));
        if (!LockFileEx(lock->file, LOCKFILE_EXCLUSIVE_LOCK, 0,
                MAXDWORD, MAXDWORD, &overlapped)) {
            CloseHandle(lock->file);
            goto error;
        }
    }
#else
    {
        struct flock guard;
        lock->fd = open(path, O_RDWR | O_CREAT, 0600);
        if (lock->fd < 0) goto error;
        memset(&guard, 0, sizeof(guard));
        guard.l_type = F_WRLCK;
        guard.l_whence = SEEK_SET;
        while (fcntl(lock->fd, F_SETLKW, &guard) < 0) {
            if (errno == EINTR) continue;
            close(lock->fd);
            goto error;
        }
    }
#endif
    return KC_RDVZ_OK;

error:
#ifdef _WIN32
    ReleaseSRWLockExclusive(&kc_rdvz_process_lock);
#else
#ifndef __EMSCRIPTEN__
    (void)pthread_mutex_unlock(&kc_rdvz_process_lock);
#endif
#endif
    return KC_RDVZ_EIO;
}

/**
 * Release a state lock.
 * @param lock Active lock.
 * @return None.
 */
static void kc_rdvz_end(kc_rdvz_lock_t *lock)
{
#ifdef _WIN32
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    (void)UnlockFileEx(lock->file, 0, MAXDWORD, MAXDWORD, &overlapped);
    CloseHandle(lock->file);
    ReleaseSRWLockExclusive(&kc_rdvz_process_lock);
#else
    (void)close(lock->fd);
#ifndef __EMSCRIPTEN__
    (void)pthread_mutex_unlock(&kc_rdvz_process_lock);
#endif
#endif
}

/**
 * Release all dynamically allocated in-memory snapshot storage.
 * @param state State snapshot to clear.
 * @return None.
 */
static void kc_rdvz_release(kc_rdvz_state_t *state)
{
    free(state->peers);
    free(state->links);
    memset(state, 0, sizeof(*state));
}

/**
 * Load the complete current peer and relationship snapshot.
 * @param lock Runtime state lock.
 * @param state Receives the snapshot.
 * @return KC_RDVZ_OK, KC_RDVZ_ENOMEM or KC_RDVZ_EIO.
 */
static int kc_rdvz_load(const kc_rdvz_lock_t *lock, kc_rdvz_state_t *state)
{
    char path[KC_RDVZ_PATH_MAX];
    kc_rdvz_header_t header;
    FILE *file;
    size_t i;
    int rc = KC_RDVZ_EIO;

    memset(state, 0, sizeof(*state));
    state->next_generation = 1U;
    if (kc_rdvz_path(path, sizeof(path), lock->dir, "state.bin"))
        return KC_RDVZ_EIO;
    file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? KC_RDVZ_OK : KC_RDVZ_EIO;
    if (fread(&header, sizeof(header), 1U, file) != 1U ||
            header.magic != KC_RDVZ_MAGIC ||
            !header.next_generation ||
            header.count > 100000U || header.links > 1000000U)
        goto done;
    state->count = header.count;
    state->link_count = header.links;
    state->next_generation = header.next_generation;
    if (state->count) {
        state->peers = (kc_rdvz_record_t *)calloc(state->count,
            sizeof(*state->peers));
        if (!state->peers) { rc = KC_RDVZ_ENOMEM; goto done; }
        if (fread(state->peers, sizeof(*state->peers),
                state->count, file) != state->count) goto done;
    }
    if (state->link_count) {
        state->links = (kc_rdvz_link_t *)calloc(state->link_count,
            sizeof(*state->links));
        if (!state->links) { rc = KC_RDVZ_ENOMEM; goto done; }
        if (fread(state->links, sizeof(*state->links),
                state->link_count, file) != state->link_count) goto done;
    }
    for (i = 0U; i < state->count; i++) {
        if (!memchr(state->peers[i].id, '\0', KC_RDVZ_ID_MAX) ||
                !memchr(state->peers[i].command, '\0', KC_RDVZ_CMD_MAX) ||
                !kc_rdvz_valid(state->peers[i].id)) goto done;
    }
    for (i = 0U; i < state->link_count; i++) {
        if (!memchr(state->links[i].first, '\0', KC_RDVZ_ID_MAX) ||
                !memchr(state->links[i].second, '\0', KC_RDVZ_ID_MAX) ||
                !kc_rdvz_valid(state->links[i].first) ||
                !kc_rdvz_valid(state->links[i].second)) goto done;
    }
    rc = KC_RDVZ_OK;

done:
    (void)fclose(file);
    if (rc != KC_RDVZ_OK) kc_rdvz_release(state);
    return rc;
}

/**
 * Replace the on-disk snapshot atomically while holding its lock.
 * @param lock Active runtime lock.
 * @param state New peer and relationship snapshot.
 * @return KC_RDVZ_OK or KC_RDVZ_EIO.
 */
static int kc_rdvz_save(const kc_rdvz_lock_t *lock,
    const kc_rdvz_state_t *state)
{
    char target[KC_RDVZ_PATH_MAX], temp[KC_RDVZ_PATH_MAX];
    kc_rdvz_header_t header;
    FILE *file;
    int rc = KC_RDVZ_EIO;

    if (state->count > UINT32_MAX || state->link_count > UINT32_MAX ||
            kc_rdvz_path(target, sizeof(target), lock->dir, "state.bin") ||
            kc_rdvz_path(temp, sizeof(temp), lock->dir, "state.tmp"))
        return KC_RDVZ_EIO;
    file = fopen(temp, "wb");
    if (!file) return KC_RDVZ_EIO;
    memset(&header, 0, sizeof(header));
    header.magic = KC_RDVZ_MAGIC;
    header.count = (uint32_t)state->count;
    header.links = (uint32_t)state->link_count;
    header.next_generation = state->next_generation;
    if (fwrite(&header, sizeof(header), 1U, file) != 1U ||
            (state->count && fwrite(state->peers,
                sizeof(*state->peers), state->count, file) != state->count) ||
            (state->link_count && fwrite(state->links,
                sizeof(*state->links), state->link_count, file)
                != state->link_count)) {
        (void)fclose(file);
        (void)remove(temp);
        return KC_RDVZ_EIO;
    }
    if (fclose(file) != 0) {
        (void)remove(temp);
        return KC_RDVZ_EIO;
    }
#ifdef _WIN32
    if (MoveFileExA(temp, target, MOVEFILE_REPLACE_EXISTING))
        rc = KC_RDVZ_OK;
#else
    if (rename(temp, target) == 0) rc = KC_RDVZ_OK;
#endif
    if (rc != KC_RDVZ_OK) (void)remove(temp);
    return rc;
}

/**
 * Find a named peer's position in one snapshot.
 * @param state Peer snapshot.
 * @param id Peer identifier.
 * @return Index or -1 when absent.
 */
static long kc_rdvz_find(const kc_rdvz_state_t *state, const char *id)
{
    size_t i;

    for (i = 0U; i < state->count; i++)
        if (strcmp(state->peers[i].id, id) == 0) return (long)i;
    return -1;
}

/**
 * Find an unordered relationship's position.
 * @param state Peer snapshot.
 * @param first One endpoint.
 * @param second Other endpoint.
 * @return Index or -1 when absent.
 */
static long kc_rdvz_find_link(const kc_rdvz_state_t *state,
    const char *first, const char *second)
{
    size_t i;

    for (i = 0U; i < state->link_count; i++)
        if ((strcmp(state->links[i].first, first) == 0 &&
                strcmp(state->links[i].second, second) == 0) ||
                (strcmp(state->links[i].first, second) == 0 &&
                strcmp(state->links[i].second, first) == 0))
            return (long)i;
    return -1;
}

/**
 * Check that an opened handle still represents its original peer.
 * @param state Current snapshot.
 * @param peer Open local peer handle.
 * @return KC_RDVZ_OK or KC_RDVZ_ENOENT.
 */
static int kc_rdvz_live(const kc_rdvz_state_t *state,
    const kc_rdvz_peer_t *peer)
{
    long index = kc_rdvz_find(state, peer->id);
    return index >= 0 && state->peers[index].generation == peer->generation
        ? KC_RDVZ_OK : KC_RDVZ_ENOENT;
}

/**
 * Compose the per-peer append-only connection notification file.
 * @param out Destination path.
 * @param cap Destination capacity.
 * @param dir Runtime directory.
 * @param id Peer identifier.
 * @return Zero on success.
 */
static int kc_rdvz_event_path(char *out, size_t cap,
    const char *dir, const char *id)
{
    char name[KC_RDVZ_ID_MAX + 8];

    if ((size_t)snprintf(name, sizeof(name), "%s.events", id) >=
            sizeof(name)) return 1;
    return kc_rdvz_path(out, cap, dir, name);
}

/**
 * Return the current byte length of a peer notification file.
 * @param dir Runtime directory.
 * @param id Peer identifier.
 * @return Byte length, or zero when missing or unreadable.
 */
static uint64_t kc_rdvz_event_size(const char *dir, const char *id)
{
    char path[KC_RDVZ_PATH_MAX];
    FILE *file;
    long size;

    if (kc_rdvz_event_path(path, sizeof(path), dir, id)) return 0U;
    file = fopen(path, "rb");
    if (!file) return 0U;
    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 0U;
    }
    size = ftell(file);
    (void)fclose(file);
    return size < 0 ? 0U : (uint64_t)size;
}

/**
 * Append one successful connection for a live local callback.
 * @param dir Runtime directory.
 * @param id Notified endpoint.
 * @param other Identifier of the peer that connected.
 * @return None.
 */
static void kc_rdvz_notify(const char *dir,
    const char *id, const char *other)
{
    char path[KC_RDVZ_PATH_MAX];
    FILE *file;

    if (kc_rdvz_event_path(path, sizeof(path), dir, id)) return;
    file = fopen(path, "ab");
    if (!file) return;
    (void)fprintf(file, "%s\n", other);
    (void)fclose(file);
}

/**
 * Dispatch new complete connection notifications to one local callback.
 * @param peer Open local peer handle.
 * @return None.
 */
static void kc_rdvz_poll(kc_rdvz_peer_t *peer)
{
    char path[KC_RDVZ_PATH_MAX];
    char line[KC_RDVZ_ID_MAX + 4];
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    FILE *file;
    long end;
    int rc;

    if (kc_rdvz_event_path(path, sizeof(path), peer->dir, peer->id)) return;
    file = fopen(path, "rb");
    if (!file) return;
    if (fseek(file, 0L, SEEK_END) != 0 || (end = ftell(file)) < 0) {
        (void)fclose(file);
        return;
    }
    if ((uint64_t)end < peer->event_offset) peer->event_offset = 0U;
    if ((uint64_t)end == peer->event_offset ||
            peer->event_offset > (uint64_t)LONG_MAX ||
            fseek(file, (long)peer->event_offset, SEEK_SET) != 0) {
        (void)fclose(file);
        return;
    }

    rc = kc_rdvz_begin(&lock);
    if (rc == KC_RDVZ_OK) {
        rc = kc_rdvz_load(&lock, &state);
        if (rc == KC_RDVZ_OK) {
            rc = kc_rdvz_live(&state, peer);
            kc_rdvz_release(&state);
        }
        kc_rdvz_end(&lock);
    }
    if (rc != KC_RDVZ_OK) {
        (void)fclose(file);
        return;
    }

    while (!atomic_load(&peer->stop)) {
        long pos = ftell(file);
        size_t length;

        if (!fgets(line, sizeof(line), file)) break;
        length = strlen(line);
        if (!length || line[length - 1U] != '\n') {
            if (pos >= 0) peer->event_offset = (uint64_t)pos;
            break;
        }
        line[length - 1U] = '\0';
        if (length > 1U && line[length - 2U] == '\r')
            line[length - 2U] = '\0';
        if (kc_rdvz_valid(line) && peer->on_connect)
            peer->on_connect(line, peer->userdata);
        if (ftell(file) >= 0) peer->event_offset = (uint64_t)ftell(file);
    }
    (void)fclose(file);
}

#ifndef __EMSCRIPTEN__
/**
 * Run the temporary handle's callback notification loop.
 * @param userdata Local peer handle.
 * @return Platform thread result.
 */
#ifdef _WIN32
static DWORD WINAPI kc_rdvz_worker(LPVOID userdata)
#else
static void *kc_rdvz_worker(void *userdata)
#endif
{
    kc_rdvz_peer_t *peer = (kc_rdvz_peer_t *)userdata;

    while (!atomic_load(&peer->stop)) {
        kc_rdvz_poll(peer);
#ifdef _WIN32
        Sleep(25);
#else
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 25000000L;
        (void)nanosleep(&delay, NULL);
#endif
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
#endif

/**
 * Construct a local handle and optionally subscribe to future connections.
 * @param out Destination handle.
 * @param dir Runtime directory.
 * @param record Current peer record.
 * @param callback Optional local connection callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or KC_RDVZ_ENOMEM/KC_RDVZ_EIO.
 */
static int kc_rdvz_handle(kc_rdvz_peer_t **out, const char *dir,
    const kc_rdvz_record_t *record, kc_rdvz_handler_t callback,
    void *userdata)
{
    kc_rdvz_peer_t *peer;

    peer = (kc_rdvz_peer_t *)calloc(1U, sizeof(*peer));
    if (!peer) return KC_RDVZ_ENOMEM;
    memcpy(peer->id, record->id, sizeof(peer->id));
    if ((size_t)snprintf(peer->dir, sizeof(peer->dir), "%s", dir) >=
            sizeof(peer->dir)) {
        free(peer);
        return KC_RDVZ_EIO;
    }
    peer->generation = record->generation;
    peer->on_connect = callback;
    peer->userdata = userdata;
    peer->event_offset = kc_rdvz_event_size(dir, peer->id);
    atomic_init(&peer->stop, 0);
#ifndef __EMSCRIPTEN__
    if (callback) {
#ifdef _WIN32
        peer->thread = CreateThread(NULL, 0, kc_rdvz_worker, peer, 0, NULL);
        if (!peer->thread) { free(peer); return KC_RDVZ_EIO; }
#else
        if (pthread_create(&peer->thread, NULL, kc_rdvz_worker, peer) != 0) {
            free(peer);
            return KC_RDVZ_EIO;
        }
#endif
        peer->started = 1;
    }
#endif
    *out = peer;
    return KC_RDVZ_OK;
}

/**
 * Launch a configured command with the other peer ID without waiting for it.
 * @param command Configured executable or shell command.
 * @param other Other peer identifier, appended as a positional argument.
 * @return None.
 */
static void kc_rdvz_command(const char *command, const char *other)
{
    char line[KC_RDVZ_CMD_MAX + KC_RDVZ_ID_MAX + 8];

    if (!command || !command[0] || !kc_rdvz_valid(other)) return;
    if ((size_t)snprintf(line, sizeof(line), "%s %s",
            command, other) >= sizeof(line)) return;
#ifdef _WIN32
    {
        char shell[sizeof(line) + 16];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;

        if ((size_t)snprintf(shell, sizeof(shell),
                "cmd.exe /C %s", line) >= sizeof(shell)) return;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (CreateProcessA(NULL, shell, NULL, NULL, FALSE,
                DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL,
                &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
#elif !defined(__EMSCRIPTEN__)
    {
        pid_t pid = fork();
        if (pid < 0) return;
        if (pid == 0) {
            pid_t child = fork();
            if (child < 0) _exit(1);
            if (child > 0) _exit(0);
            (void)setsid();
            execl("/bin/sh", "sh", "-c", line, (char *)NULL);
            _exit(127);
        }
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
    }
#else
    (void)line;
#endif
}

/**
 * Create or update one resident named peer.
 * @param out Destination local handle.
 * @param id Peer name.
 * @param options Optional persistent command and local callback.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_create(kc_rdvz_peer_t **out, const char *id,
    const kc_rdvz_options_t *options)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    kc_rdvz_record_t *next;
    kc_rdvz_record_t record;
    char event_path[KC_RDVZ_PATH_MAX];
    const char *command = options ? options->cmd : NULL;
    long index;
    int rc;

    if (out) *out = NULL;
    if (!out || !kc_rdvz_valid(id) ||
            (command && (strlen(command) >= KC_RDVZ_CMD_MAX ||
                strchr(command, '\n') || strchr(command, '\r'))))
        return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc != KC_RDVZ_OK) { kc_rdvz_end(&lock); return rc; }
    index = kc_rdvz_find(&state, id);
    if (index >= 0) {
        memset(state.peers[index].command, 0, KC_RDVZ_CMD_MAX);
        if (command) (void)snprintf(state.peers[index].command,
            KC_RDVZ_CMD_MAX, "%s", command);
    } else {
        if (state.next_generation == UINT64_MAX ||
                state.count >= UINT32_MAX) {
            rc = KC_RDVZ_EIO;
            goto done;
        }
        next = (kc_rdvz_record_t *)realloc(state.peers,
            (state.count + 1U) * sizeof(*next));
        if (!next) { rc = KC_RDVZ_ENOMEM; goto done; }
        state.peers = next;
        memset(&state.peers[state.count], 0, sizeof(*next));
        (void)snprintf(state.peers[state.count].id,
            KC_RDVZ_ID_MAX, "%s", id);
        if (command) (void)snprintf(state.peers[state.count].command,
            KC_RDVZ_CMD_MAX, "%s", command);
        state.peers[state.count].generation = state.next_generation++;
        index = (long)state.count++;
    }
    record = state.peers[index];
    if (kc_rdvz_event_path(event_path, sizeof(event_path), lock.dir, id)) {
        rc = KC_RDVZ_EIO;
        goto done;
    }
    {
        FILE *events = fopen(event_path, "ab");
        if (!events) { rc = KC_RDVZ_EIO; goto done; }
        if (fclose(events) != 0) { rc = KC_RDVZ_EIO; goto done; }
    }
    rc = kc_rdvz_save(&lock, &state);
    if (rc == KC_RDVZ_OK) rc = kc_rdvz_handle(out, lock.dir, &record,
        options ? options->on_connect : NULL,
        options ? options->userdata : NULL);

done:
    kc_rdvz_release(&state);
    kc_rdvz_end(&lock);
    return rc;
}

/**
 * Open a local handle to an existing registered peer.
 * @param out Destination local handle.
 * @param id Peer identifier.
 * @param on_connect Optional connection callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_open(kc_rdvz_peer_t **out, const char *id,
    kc_rdvz_handler_t on_connect, void *userdata)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    long index;
    int rc;

    if (out) *out = NULL;
    if (!out || !kc_rdvz_valid(id)) return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc != KC_RDVZ_OK) { kc_rdvz_end(&lock); return rc; }
    index = kc_rdvz_find(&state, id);
    rc = index < 0 ? KC_RDVZ_ENOENT : kc_rdvz_handle(out,
        lock.dir, &state.peers[index], on_connect, userdata);
    kc_rdvz_release(&state);
    kc_rdvz_end(&lock);
    return rc;
}

/**
 * Return a local handle's peer ID.
 * @param peer Local handle.
 * @return Borrowed peer ID or NULL.
 */
const char *kc_rdvz_peer_id(const kc_rdvz_peer_t *peer)
{
    return peer ? peer->id : NULL;
}

/**
 * Enumerate every registered peer.
 * @param visit Peer visitor.
 * @param userdata Visitor context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_list(kc_rdvz_visit_t visit, void *userdata)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    size_t i;
    int rc;

    if (!visit) return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    kc_rdvz_end(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    for (i = 0U; i < state.count; i++)
        visit(state.peers[i].id, userdata);
    kc_rdvz_release(&state);
    return KC_RDVZ_OK;
}

/**
 * Enumerate only directly linked neighbors of a peer.
 * @param peer Existing local peer handle.
 * @param visit Neighbor visitor.
 * @param userdata Visitor context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_peer_list(const kc_rdvz_peer_t *peer,
    kc_rdvz_visit_t visit, void *userdata)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    size_t i;
    int rc;

    if (!peer || !visit) return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc == KC_RDVZ_OK) rc = kc_rdvz_live(&state, peer);
    kc_rdvz_end(&lock);
    if (rc != KC_RDVZ_OK) { kc_rdvz_release(&state); return rc; }
    for (i = 0U; i < state.link_count; i++) {
        if (strcmp(state.links[i].first, peer->id) == 0)
            visit(state.links[i].second, userdata);
        else if (strcmp(state.links[i].second, peer->id) == 0)
            visit(state.links[i].first, userdata);
    }
    kc_rdvz_release(&state);
    return KC_RDVZ_OK;
}

/**
 * Establish one direct symmetric relationship.
 * @param peer Initiating peer handle.
 * @param other Other registered peer ID.
 * @param callback Optional initiating-operation callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_connect(kc_rdvz_peer_t *peer, const char *other,
    kc_rdvz_handler_t callback, void *userdata)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    kc_rdvz_link_t *next;
    char left_command[KC_RDVZ_CMD_MAX], right_command[KC_RDVZ_CMD_MAX];
    long a, b;
    int rc;

    if (!peer || !kc_rdvz_valid(other) || strcmp(peer->id, other) == 0)
        return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc != KC_RDVZ_OK) { kc_rdvz_end(&lock); return rc; }
    rc = kc_rdvz_live(&state, peer);
    if (rc != KC_RDVZ_OK) goto done;
    a = kc_rdvz_find(&state, peer->id);
    b = kc_rdvz_find(&state, other);
    if (b < 0) { rc = KC_RDVZ_ENOENT; goto done; }
    if (kc_rdvz_find_link(&state, peer->id, other) >= 0) {
        rc = KC_RDVZ_EEXIST;
        goto done;
    }
    if (state.link_count >= UINT32_MAX) { rc = KC_RDVZ_EIO; goto done; }
    next = (kc_rdvz_link_t *)realloc(state.links,
        (state.link_count + 1U) * sizeof(*next));
    if (!next) { rc = KC_RDVZ_ENOMEM; goto done; }
    state.links = next;
    memset(&state.links[state.link_count], 0, sizeof(*next));
    (void)snprintf(state.links[state.link_count].first,
        KC_RDVZ_ID_MAX, "%s", peer->id);
    (void)snprintf(state.links[state.link_count].second,
        KC_RDVZ_ID_MAX, "%s", other);
    state.link_count++;
    memcpy(left_command, state.peers[a].command, sizeof(left_command));
    memcpy(right_command, state.peers[b].command, sizeof(right_command));
    rc = kc_rdvz_save(&lock, &state);
    if (rc == KC_RDVZ_OK) {
        kc_rdvz_notify(lock.dir, peer->id, other);
        kc_rdvz_notify(lock.dir, other, peer->id);
    }

done:
    kc_rdvz_release(&state);
    kc_rdvz_end(&lock);
    if (rc == KC_RDVZ_OK) {
        kc_rdvz_command(left_command, other);
        kc_rdvz_command(right_command, peer->id);
        if (callback) callback(other, userdata);
    }
    return rc;
}

/**
 * Remove exactly one symmetric relationship, retaining both peers.
 * @param peer Initiating peer handle.
 * @param other Other registered peer ID.
 * @param callback Optional initiating-operation callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_peer_disconnect(kc_rdvz_peer_t *peer, const char *other,
    kc_rdvz_handler_t callback, void *userdata)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    long index;
    int rc;

    if (!peer || !kc_rdvz_valid(other) || strcmp(peer->id, other) == 0)
        return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc != KC_RDVZ_OK) { kc_rdvz_end(&lock); return rc; }
    rc = kc_rdvz_live(&state, peer);
    if (rc != KC_RDVZ_OK) goto done;
    index = kc_rdvz_find_link(&state, peer->id, other);
    if (index < 0) { rc = KC_RDVZ_ENOENT; goto done; }
    memmove(&state.links[index], &state.links[index + 1],
        (state.link_count - (size_t)index - 1U) * sizeof(*state.links));
    state.link_count--;
    rc = kc_rdvz_save(&lock, &state);

done:
    kc_rdvz_release(&state);
    kc_rdvz_end(&lock);
    if (rc == KC_RDVZ_OK && callback) callback(other, userdata);
    return rc;
}

/**
 * Remove one named peer and every direct relationship to it.
 * @param id Peer identifier.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_disconnect(const char *id)
{
    kc_rdvz_lock_t lock;
    kc_rdvz_state_t state;
    char event_path[KC_RDVZ_PATH_MAX];
    long index;
    size_t i;
    int rc;

    if (!kc_rdvz_valid(id)) return KC_RDVZ_EINVAL;
    rc = kc_rdvz_begin(&lock);
    if (rc != KC_RDVZ_OK) return rc;
    rc = kc_rdvz_load(&lock, &state);
    if (rc != KC_RDVZ_OK) { kc_rdvz_end(&lock); return rc; }
    index = kc_rdvz_find(&state, id);
    if (index < 0) { rc = KC_RDVZ_ENOENT; goto done; }
    i = 0U;
    while (i < state.link_count) {
        if (strcmp(state.links[i].first, id) == 0 ||
                strcmp(state.links[i].second, id) == 0) {
            memmove(&state.links[i], &state.links[i + 1U],
                (state.link_count - i - 1U) * sizeof(*state.links));
            state.link_count--;
        } else {
            i++;
        }
    }
    memmove(&state.peers[index], &state.peers[index + 1],
        (state.count - (size_t)index - 1U) * sizeof(*state.peers));
    state.count--;
    rc = kc_rdvz_save(&lock, &state);
    if (rc == KC_RDVZ_OK &&
            kc_rdvz_event_path(event_path, sizeof(event_path),
                lock.dir, id) == 0) (void)remove(event_path);

done:
    kc_rdvz_release(&state);
    kc_rdvz_end(&lock);
    return rc;
}

/**
 * Release a local handle without deleting the named peer.
 * @param peer Local handle, or NULL.
 * @return None.
 */
void kc_rdvz_close(kc_rdvz_peer_t *peer)
{
    if (!peer) return;
    atomic_store(&peer->stop, 1);
#ifndef __EMSCRIPTEN__
    if (peer->started) {
#ifdef _WIN32
        (void)WaitForSingleObject(peer->thread, INFINITE);
        CloseHandle(peer->thread);
#else
        (void)pthread_join(peer->thread, NULL);
#endif
    }
#endif
    free(peer);
}

/**
 * Describe one public status code.
 * @param status Public status.
 * @return Static description.
 */
const char *kc_rdvz_strerror(int status)
{
    switch (status) {
        case KC_RDVZ_OK: return "success";
        case KC_RDVZ_ENOENT: return "peer or relationship not found";
        case KC_RDVZ_EINVAL: return "invalid argument";
        case KC_RDVZ_ENOMEM: return "out of memory";
        case KC_RDVZ_EEXIST: return "relationship already exists";
        case KC_RDVZ_EIO: return "rendezvous state unavailable";
        default: return "unknown error";
    }
}

/**
 * Return the generated build version.
 * @return Build version.
 */
uint64_t kc_rdvz_version(void)
{
    return (uint64_t)KC_RDVZ_BUILD_VERSION;
}
