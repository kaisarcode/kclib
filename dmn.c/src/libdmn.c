/**
 * libdmn.c - IPC Daemon Manager
 * Summary: Core implementation for the dmn library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libdmn.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>

#ifndef KC_DMN_BUILD_VERSION
#define KC_DMN_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_dmn_version(void) {
    return (uint64_t)KC_DMN_BUILD_VERSION;
}

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/un.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#define KC_DMN_BUF     4096
#define KC_DMN_PATH    512

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_FLOAT,
    KC_ENV_TYPE_STR
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_DMN_DIR", offsetof(kc_dmn_options_t, dir), KC_ENV_TYPE_STR },
};
static const int env_config_table_n =
    sizeof(env_config_table) / sizeof(env_config_table[0]);

struct kc_dmn {
    char dir[KC_DMN_PATH];
    kc_dmn_options_t opts;
    volatile sig_atomic_t stop_requested;
    char error[256];
};

/**
 * Sets an error message on the context.
 * @param ctx Context pointer.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 * @return None.
 */
static void kc_dmn_set_error(kc_dmn_t *ctx, const char *fmt, ...) {
    va_list ap;
    if (!ctx || !fmt) return;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, ap);
    va_end(ap);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

#ifndef _WIN32
typedef struct {
    int in_fd;
    int out_fd;
    pid_t pid;
    char cmd[KC_DMN_BUF];
} kc_dmn_backend_t;
#endif

typedef struct {
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    int used;
    char key[KC_DMN_PATH];
} kc_dmn_runner_slot_t;

static kc_dmn_runner_slot_t *g_runner_slots = NULL;
static size_t g_runner_slots_cap = 0;
static int g_runner_slots_initialized = 0;

/**
 * Initializes the global handle table once.
 * @return None.
 */
static void kc_dmn_runner_slots_init(void) {
    if (g_runner_slots_initialized) return;
    g_runner_slots_cap = 16;
    g_runner_slots = (kc_dmn_runner_slot_t *)calloc(g_runner_slots_cap, sizeof(kc_dmn_runner_slot_t));
    if (g_runner_slots) {
        size_t i;
        for (i = 0; i < g_runner_slots_cap; i++) {
#ifdef _WIN32
            g_runner_slots[i].h = INVALID_HANDLE_VALUE;
#else
            g_runner_slots[i].fd = -1;
#endif
            g_runner_slots[i].used = 0;
            g_runner_slots[i].key[0] = '\0';
        }
    }
    g_runner_slots_initialized = 1;
}

/**
 * Allocates a handle entry for a new connection.
 * @param key Daemon key name.
 * @param fd Socket descriptor (POSIX).
 * @param h  Windows handle (Win32).
 * @return Handle index on success, or -1 on failure.
 */
static int kc_dmn_runner_slot_alloc(const char *key, int fd
#ifdef _WIN32
    , HANDLE h
#endif
) {
    size_t i;
    (void)fd;
    kc_dmn_runner_slots_init();
    if (!g_runner_slots) return -1;

    for (i = 0; i < g_runner_slots_cap; i++) {
        if (!g_runner_slots[i].used) {
#ifdef _WIN32
            g_runner_slots[i].h = h;
#else
            g_runner_slots[i].fd = fd;
#endif
            g_runner_slots[i].used = 1;
            snprintf(g_runner_slots[i].key, sizeof(g_runner_slots[i].key), "%s", key);
            return (int)i;
        }
    }

    {
        size_t new_cap = g_runner_slots_cap * 2;
        kc_dmn_runner_slot_t *new_handles = (kc_dmn_runner_slot_t *)realloc(g_runner_slots, new_cap * sizeof(kc_dmn_runner_slot_t));
        if (!new_handles) return -1;
        g_runner_slots = new_handles;

        for (i = g_runner_slots_cap; i < new_cap; i++) {
#ifdef _WIN32
            g_runner_slots[i].h = INVALID_HANDLE_VALUE;
#else
            g_runner_slots[i].fd = -1;
#endif
            g_runner_slots[i].used = 0;
            g_runner_slots[i].key[0] = '\0';
        }

        i = g_runner_slots_cap;
        g_runner_slots_cap = new_cap;
#ifdef _WIN32
        g_runner_slots[i].h = h;
#else
        g_runner_slots[i].fd = fd;
#endif
        g_runner_slots[i].used = 1;
        snprintf(g_runner_slots[i].key, sizeof(g_runner_slots[i].key), "%s", key);
        return (int)i;
    }
}

/**
 * Returns the handle entry for a given handle index.
 * @param handle Handle index.
 * @return Pointer to handle entry, or NULL if invalid.
 */
static kc_dmn_runner_slot_t *kc_dmn_runner_slot_get(int handle) {
    if (handle < 0 || (size_t)handle >= g_runner_slots_cap) return NULL;
    if (!g_runner_slots[handle].used) return NULL;
    return &g_runner_slots[handle];
}

/**
 * Frees a handle entry and closes its descriptor.
 * @param handle Handle index.
 * @return None.
 */
static void kc_dmn_runner_slot_free(int handle) {
    if (handle < 0 || (size_t)handle >= g_runner_slots_cap) return;
#ifdef _WIN32
    if (g_runner_slots[handle].h != INVALID_HANDLE_VALUE) {
        CloseHandle(g_runner_slots[handle].h);
        g_runner_slots[handle].h = INVALID_HANDLE_VALUE;
    }
#else
    if (g_runner_slots[handle].fd >= 0) {
        close(g_runner_slots[handle].fd);
        g_runner_slots[handle].fd = -1;
    }
#endif
    g_runner_slots[handle].used = 0;
    g_runner_slots[handle].key[0] = '\0';
}

/**
 * Resolves the runtime directory for socket and PID files.
 * @param out  Output buffer.
 * @param cap  Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_runtime_dir(char *out, size_t cap) {
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD r = GetTempPathA((DWORD)sizeof(tmp), tmp);
    if (r == 0 || r >= (DWORD)sizeof(tmp)) return 1;
    if ((size_t)snprintf(out, cap,
            "%skaisarcode\\dmn", tmp) >= cap) return 1;
    return 0;
#else
    const char *xdg;
    char path[KC_DMN_PATH];
    struct stat st;

    xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0]) {
        if ((size_t)snprintf(out, cap,
                "%s/kaisarcode/dmn", xdg) < cap)
            return 0;
    }
    if ((size_t)snprintf(path, sizeof(path),
            "/run/user/%u", (unsigned)getuid()) < sizeof(path)) {
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            if ((size_t)snprintf(out, cap,
                    "%s/kaisarcode/dmn", path) < cap)
                return 0;
        }
    }
    if ((size_t)snprintf(out, cap,
            "/tmp/kaisarcode/dmn-%u", (unsigned)getuid()) >= cap)
        return 1;
    return 0;
#endif
}

/**
 * Creates the runtime directory if it does not exist.
 * @param path Directory path.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_ensure_dir(const char *path) {
#ifdef _WIN32
    char buf[KC_DMN_PATH];
    char *p;
    if ((size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return 1;
    for (p = buf + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            if (!CreateDirectoryA(buf, NULL)
                    && GetLastError() != ERROR_ALREADY_EXISTS)
                return 1;
            *p = '\\';
        }
    }
    if (!CreateDirectoryA(buf, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS)
        return 1;
    return 0;
#else
    char buf[KC_DMN_PATH];
    char *p;
    if ((size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return 1;
    for (p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0700) != 0 && errno != EEXIST) return 1;
            *p = '/';
        }
    }
    if (mkdir(buf, 0700) != 0 && errno != EEXIST) return 1;
    return 0;
#endif
}

/**
 * Composes the socket or pipe path for a key.
 * On Windows, returns the Named Pipe path ignoring dir.
 * @param dir Runtime directory (POSIX only).
 * @param key Daemon key name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_dmn_sock_path(
    const char *dir, const char *key, char *out, size_t cap
) {
#ifdef _WIN32
    (void)dir;
    if ((size_t)snprintf(out, cap,
            "\\\\.\\pipe\\kc-dmn-%s", key) >= cap)
        return 1;
    return 0;
#else
    if ((size_t)snprintf(out, cap, "%s/%s", dir, key) >= cap)
        return 1;
    return 0;
#endif
}

/**
 * Composes the PID file path for a key.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_dmn_pid_path(
    const char *dir, const char *key, char *out, size_t cap
) {
#ifdef _WIN32
    if ((size_t)snprintf(out, cap,
            "%s\\%s.pid", dir, key) >= cap)
        return 1;
    return 0;
#else
    if ((size_t)snprintf(out, cap, "%s/%s.pid", dir, key) >= cap)
        return 1;
    return 0;
#endif
}

/**
 * Composes the backend PID file path for a key.
 * Stores the PID of the actual backend (child) process.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
#ifndef _WIN32
static int kc_dmn_backend_pid_path(
    const char *dir, const char *key, char *out, size_t cap
) {
    if ((size_t)snprintf(out, cap, "%s/%s.bpid", dir, key) >= cap)
        return 1;
    return 0;
}
#endif

/**
 * Writes a PID value to a file.
 * @param path File path.
 * @param pid  PID value.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_write_pid(const char *path, long pid) {
#ifdef _WIN32
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "%ld\n", pid);
    fclose(f);
    return 0;
#else
    char buf[64];
    int n;
    int fd;

    n = snprintf(buf, sizeof(buf), "%ld\n", pid);
    if (n < 0 || (size_t)n >= sizeof(buf)) return 1;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 1;
    if (write(fd, buf, (size_t)n) != n) { close(fd); return 1; }
    close(fd);
    return 0;
#endif
}

/**
 * Reads a PID value from a file.
 * @param path    File path.
 * @param out_pid Output PID.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_read_pid(const char *path, long *out_pid) {
#ifdef _WIN32
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    if (fscanf(f, "%ld", out_pid) != 1) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
#else
    char buf[64];
    ssize_t n;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) return 1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1;
    buf[n] = '\0';
    *out_pid = strtol(buf, NULL, 10);
    return 0;
#endif
}

#ifndef _WIN32

/**
 * Binds a Unix socket and starts listening.
 * @param sock Socket path.
 * @return Listening descriptor on success, -1 on failure.
 */
static int kc_dmn_bind_listen(const char *sock) {
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sock) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    (void)unlink(sock);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * Writes all bytes to a descriptor.
 * @param fd  Destination descriptor.
 * @param buf Source buffer.
 * @param len Byte count.
 * @return 0 on success, 1 on error.
 */
static int kc_dmn_write_all(int fd, const char *buf, size_t len) {
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) return 1;
        off += (size_t)n;
    }
    return 0;
}

/**
 * Connects to a Unix socket.
 * @param sock Socket path.
 * @return Connected descriptor on success, -1 on failure.
 */
static int kc_dmn_connect_posix(const char *sock) {
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(sock) >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * Detaches daemon stdio from the parent process.
 * @return None.
 */
static void kc_dmn_detach_stdio(void) {
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0) return;
    (void)dup2(nullfd, STDIN_FILENO);
    (void)dup2(nullfd, STDOUT_FILENO);
    (void)dup2(nullfd, STDERR_FILENO);
    if (nullfd > STDERR_FILENO) close(nullfd);
}

/**
 * Stops one resident backend process.
 * @param backend Backend state.
 * @return None.
 */
static void kc_dmn_backend_stop(kc_dmn_backend_t *backend) {
    if (!backend) return;
    if (backend->in_fd >= 0) close(backend->in_fd);
    if (backend->out_fd >= 0) close(backend->out_fd);
    if (backend->pid > 0) {
        (void)kill(-backend->pid, SIGTERM);
        (void)kill(backend->pid, SIGTERM);
        (void)waitpid(backend->pid, NULL, 0);
    }
    backend->in_fd = -1;
    backend->out_fd = -1;
    backend->pid = -1;
    backend->cmd[0] = '\0';
}

/**
 * Starts one resident backend process with pipe-backed stdio.
 * @param cmd Shell command string.
 * @param backend Output backend state.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_backend_start(const char *cmd, kc_dmn_backend_t *backend) {
    int in_p[2];
    int out_p[2];
    pid_t pid;

    backend->in_fd = -1;
    backend->out_fd = -1;
    backend->pid = -1;
    backend->cmd[0] = '\0';
    if (!cmd || !cmd[0]) return 1;
    if (pipe(in_p) != 0) return 1;
    if (pipe(out_p) != 0) {
        close(in_p[0]);
        close(in_p[1]);
        return 1;
    }
    pid = fork();
    if (pid < 0) {
        close(in_p[0]);
        close(in_p[1]);
        close(out_p[0]);
        close(out_p[1]);
        return 1;
    }
    if (pid == 0) {
        setsid();
        if (dup2(in_p[0], STDIN_FILENO) < 0) _exit(1);
        if (dup2(out_p[1], STDOUT_FILENO) < 0) _exit(1);
        if (dup2(out_p[1], STDERR_FILENO) < 0) _exit(1);
        close(in_p[0]);
        close(in_p[1]);
        close(out_p[0]);
        close(out_p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
    close(in_p[0]);
    close(out_p[1]);
    backend->in_fd = in_p[1];
    backend->out_fd = out_p[0];
    backend->pid = pid;
    if ((size_t)snprintf(backend->cmd, sizeof(backend->cmd), "%s", cmd) >=
            sizeof(backend->cmd)) {
        kc_dmn_backend_stop(backend);
        return 1;
    }
    return 0;
}

/**
 * Checks whether one resident backend is alive.
 * @param backend Backend state.
 * @return Non-zero when alive.
 */
static int kc_dmn_backend_alive(const kc_dmn_backend_t *backend) {
    return backend && backend->pid > 0 && kill(backend->pid, 0) == 0;
}

/**
 * Relays one client connection through the resident backend.
 * @param cli Client descriptor.
 * @param backend Resident backend state.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_bridge_client(int cli, kc_dmn_backend_t *backend) {
    int cli_open = 1;
    while (1) {
        fd_set fds;
        int max_fd = cli > backend->out_fd ? cli : backend->out_fd;
        int ready;
        char buf[KC_DMN_BUF];

        FD_ZERO(&fds);
        if (cli_open) FD_SET(cli, &fds);
        FD_SET(backend->out_fd, &fds);
        ready = select(max_fd + 1, &fds, NULL, NULL, NULL);
        if (ready < 0) return 1;
        if (cli_open && FD_ISSET(cli, &fds)) {
            ssize_t n = read(cli, buf, sizeof(buf));
            if (n < 0) return 1;
            if (n == 0) {
                cli_open = 0;
                close(backend->in_fd);
                backend->in_fd = -1;
            } else if (kc_dmn_write_all(backend->in_fd, buf, (size_t)n) != 0) {
                return 1;
            } else if (memchr(buf, 4, (size_t)n) != NULL) {
                cli_open = 0;
            }
        }
        if (FD_ISSET(backend->out_fd, &fds)) {
            ssize_t n = read(backend->out_fd, buf, sizeof(buf));
            if (n <= 0) return 1;
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == 4) {
                    return kc_dmn_write_all(cli, buf, (size_t)i + 1);
                }
            }
            if (kc_dmn_write_all(cli, buf, (size_t)n) != 0) return 1;
        }
    }
}

/**
 * Resident backend serve loop. Runs until killed or backend exits.
 * @param fd  Bound listening descriptor.
 * @param dir Runtime directory (for backend PID file).
 * @param key Daemon key name (for backend PID file).
 * @param cmd Shell command string.
 * @return Does not return normally.
 */
static void kc_dmn_serve_loop(int fd, const char *dir,
    const char *key, const char *cmd) {
    kc_dmn_backend_t backend;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if (kc_dmn_backend_start(cmd, &backend) != 0) _exit(1);
    {
        char bpidpath[KC_DMN_PATH];
        if (kc_dmn_backend_pid_path(dir, key,
                bpidpath, sizeof(bpidpath)) == 0)
            (void)kc_dmn_write_pid(bpidpath, (long)backend.pid);
    }
    while (1) {
        int cli = accept(fd, NULL, NULL);
        if (cli < 0) continue;
        (void)kc_dmn_bridge_client(cli, &backend);
        close(cli);
        if (backend.in_fd < 0 || !kc_dmn_backend_alive(&backend)) {
            kc_dmn_backend_stop(&backend);
            if (kc_dmn_backend_start(cmd, &backend) != 0) _exit(1);
            {
                char bpidpath[KC_DMN_PATH];
                if (kc_dmn_backend_pid_path(dir, key,
                        bpidpath, sizeof(bpidpath)) == 0)
                    (void)kc_dmn_write_pid(
                        bpidpath, (long)backend.pid);
            }
        }
    }
}

/**
 * Starts a daemon that serves one key on a Unix socket.
 * Forks to background, signals the parent when the socket is ready,
 * then enters the serve loop.
 * @param dir    Runtime directory.
 * @param key    Daemon key name.
 * @param cmd    Shell command string for the resident backend.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_update_posix(
    const char *dir, const char *key, const char *cmd
) {
    char sock[KC_DMN_PATH];
    char pidpath[KC_DMN_PATH];
    int ready[2];
    int fd;
    pid_t pid;
    char c;

    if (kc_dmn_sock_path(dir, key, sock, sizeof(sock)) != 0)
        return 1;
    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) != 0)
        return 1;
    if (pipe(ready) != 0) return 1;
    pid = fork();
    if (pid < 0) {
        close(ready[0]);
        close(ready[1]);
        return 1;
    }
    if (pid == 0) {
        close(ready[0]);
        setsid();
        fd = kc_dmn_bind_listen(sock);
        if (fd < 0) {
            if (write(ready[1], "0", 1) != 1) {}
            close(ready[1]);
            _exit(1);
        }
        if (write(ready[1], "1", 1) != 1) {}
        close(ready[1]);
        kc_dmn_detach_stdio();
        kc_dmn_serve_loop(fd, dir, key, cmd);
        _exit(0);
    }
    close(ready[1]);
    if (read(ready[0], &c, 1) != 1 || c != '1') {
        close(ready[0]);
        return 1;
    }
    close(ready[0]);
    return kc_dmn_write_pid(pidpath, (long)pid);
}

#endif

#ifdef _WIN32

/**
 * Writes all bytes to a Windows handle.
 * @param h   Destination handle.
 * @param buf Source buffer.
 * @param len Byte count.
 * @return 0 on success, 1 on error.
 */
static int kc_dmn_write_all_handle(
    HANDLE h, const char *buf, DWORD len
) {
    DWORD off = 0, bw = 0;

    while (off < len) {
        if (!WriteFile(h, buf + off, len - off, &bw, NULL) ||
                bw == 0)
            return 1;
        off += bw;
    }
    return 0;
}

/**
 * Named Pipe serve loop. Accepts one connection at a time,
 * spawns the command process, and bridges I/O until the child
 * exits or the pipe breaks.
 * @param pipename Named Pipe path.
 * @param cmd      Command string passed to cmd.exe /c.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_serve_win32(
    const char *pipename, const char *cmd
) {
    SECURITY_ATTRIBUTES sa;
    char cmdstr[KC_DMN_BUF];
    char buf[KC_DMN_BUF];
    char evname[64];
    HANDLE hSignalEvent;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if ((size_t)snprintf(cmdstr, sizeof(cmdstr),
            "cmd.exe /c %s", cmd) >= sizeof(cmdstr))
        return 1;

    {
        DWORD pid = GetCurrentProcessId();
        snprintf(evname, sizeof(evname),
            "Global\\DmnSignal_%lu", pid);
        hSignalEvent = CreateEventA(NULL, FALSE, FALSE, evname);
        (void)hSignalEvent;
    }

    while (1) {
        HANDLE h = CreateNamedPipeA(
            pipename,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, KC_DMN_BUF, KC_DMN_BUF, 0, NULL
        );
        HANDLE in_rd, in_wr, out_rd, out_wr;
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD br, avail, exit_code;

        if (h == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }
        if (!ConnectNamedPipe(h, NULL) &&
                GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            continue;
        }
        if (!CreatePipe(&in_rd, &in_wr, &sa, 0) ||
                !CreatePipe(&out_rd, &out_wr, &sa, 0)) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput  = in_rd;
        si.hStdOutput = out_wr;
        si.hStdError  = out_wr;
        memset(&pi, 0, sizeof(pi));
        if (!CreateProcessA(NULL, cmdstr, NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(in_rd);
            CloseHandle(in_wr);
            CloseHandle(out_rd);
            CloseHandle(out_wr);
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        CloseHandle(in_rd);
        CloseHandle(out_wr);
        CloseHandle(pi.hThread);
        while (1) {
            if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)
                    && avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf)
                    ? avail : (DWORD)sizeof(buf);
                if (ReadFile(h, buf, rd, &br, NULL) && br > 0)
                    (void)kc_dmn_write_all_handle(in_wr, buf, br);
            }
            if (PeekNamedPipe(out_rd, NULL, 0, NULL, &avail, NULL)
                    && avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf)
                    ? avail : (DWORD)sizeof(buf);
                if (ReadFile(out_rd, buf, rd, &br, NULL) && br > 0)
                    (void)kc_dmn_write_all_handle(h, buf, br);
            }
            if (!GetExitCodeProcess(pi.hProcess, &exit_code) ||
                    exit_code != STILL_ACTIVE)
                break;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) &&
                    GetLastError() == ERROR_BROKEN_PIPE)
                break;
            Sleep(5);
        }
        CloseHandle(in_wr);
        CloseHandle(out_rd);
        CloseHandle(pi.hProcess);
        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
    return 0;
}

/**
 * Starts a daemon for one key by self-spawning as a detached
 * background process with the --_serve flag.
 * @param dir Runtime directory for PID files.
 * @param key Daemon key name.
 * @param cmd Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_update_win32(
    const char *dir, const char *key, const char *cmd
) {
    char pipename[KC_DMN_PATH];
    char pidpath[KC_DMN_PATH];
    char exe[MAX_PATH];
    char cmdline[KC_DMN_BUF];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    if (kc_dmn_sock_path(NULL, key, pipename, sizeof(pipename)) != 0)
        return 1;
    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) != 0)
        return 1;
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0)
        return 1;
    if ((size_t)snprintf(cmdline, sizeof(cmdline),
            "\"%s\" --_serve \"%s\" %s", exe, pipename, cmd)
            >= sizeof(cmdline))
        return 1;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
            DETACHED_PROCESS | CREATE_NO_WINDOW,
            NULL, NULL, &si, &pi))
        return 1;
    CloseHandle(pi.hThread);
    if (kc_dmn_write_pid(pidpath, (long)pi.dwProcessId) != 0) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        return 1;
    }
    CloseHandle(pi.hProcess);
    return 0;
}

#endif

/**
 * Registers a command for a key, replacing any existing one.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @param cmd Command string for the resident backend.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_run_update(
    const char *dir, const char *key, const char *cmd
) {
    char pidpath[KC_DMN_PATH];
    long pid;

    if (kc_dmn_ensure_dir(dir) != 0) return 1;
    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) == 0) {
        if (kc_dmn_read_pid(pidpath, &pid) == 0) {
#ifdef _WIN32
            HANDLE h = OpenProcess(
                PROCESS_TERMINATE, FALSE, (DWORD)pid
            );
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#else
            (void)kill((pid_t)pid, SIGTERM);
            {
                char sock[KC_DMN_PATH];
                if (kc_dmn_sock_path(dir, key,
                        sock, sizeof(sock)) == 0)
                    (void)unlink(sock);
            }
            {
                char bpidpath[KC_DMN_PATH];
                if (kc_dmn_backend_pid_path(dir, key,
                        bpidpath, sizeof(bpidpath)) == 0) {
                    long bpid;
                    if (kc_dmn_read_pid(bpidpath, &bpid) == 0) {
                        (void)kill(-(pid_t)bpid, SIGTERM);
                        (void)kill((pid_t)bpid, SIGTERM);
                    }
                    (void)remove(bpidpath);
                }
            }
#endif
            (void)remove(pidpath);
        }
    }
#ifdef _WIN32
    return kc_dmn_update_win32(dir, key, cmd);
#else
    return kc_dmn_update_posix(dir, key, cmd);
#endif
}

/**
 * Removes a daemon key and terminates its process.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_dmn_run_delete(const char *dir, const char *key) {
    char pidpath[KC_DMN_PATH];
#ifndef _WIN32
    char sock[KC_DMN_PATH];
#endif
    long pid;
    int found = 0;

    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) != 0)
        return 0;
    if (kc_dmn_read_pid(pidpath, &pid) == 0) {
        found = 1;
#ifdef _WIN32
        HANDLE h = OpenProcess(
            PROCESS_TERMINATE, FALSE, (DWORD)pid
        );
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#else
        (void)kill((pid_t)pid, SIGTERM);
#endif
    }
#ifndef _WIN32
    if (kc_dmn_sock_path(dir, key, sock, sizeof(sock)) == 0) {
        if (unlink(sock) == 0) found = 1;
    }
    {
        char bpidpath[KC_DMN_PATH];
        if (kc_dmn_backend_pid_path(dir, key,
                bpidpath, sizeof(bpidpath)) == 0) {
            long bpid;
            if (kc_dmn_read_pid(bpidpath, &bpid) == 0) {
                (void)kill(-(pid_t)bpid, SIGTERM);
                (void)kill((pid_t)bpid, SIGTERM);
            }
            (void)remove(bpidpath);
        }
    }
#endif
    if (!found) {
        return 0;
    }
    (void)remove(pidpath);
    return 0;
}

/**
 * Calls the list callback for one daemon.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @param cb Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_ls_row(const char *dir, const char *key, kc_dmn_list_cb cb, void *userdata) {
    char sock[KC_DMN_PATH];

#ifdef _WIN32
    (void)dir;
    if (kc_dmn_sock_path(NULL, key, sock, sizeof(sock)) != 0)
        return 1;
#else
    if (kc_dmn_sock_path(dir, key, sock, sizeof(sock)) != 0)
        return 1;
#endif
    if (cb) cb(key, sock, userdata);
    return 0;
}

/**
 * Checks whether one daemon key is registered.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @return 1 when registered, 0 otherwise.
 */
static int kc_dmn_exists(const char *dir, const char *key) {
#ifdef _WIN32
    char pidpath[KC_DMN_PATH];
    long pid;

    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) != 0)
        return 0;
    return kc_dmn_read_pid(pidpath, &pid) == 0;
#else
    char sock[KC_DMN_PATH];
    struct stat st;

    if (kc_dmn_sock_path(dir, key, sock, sizeof(sock)) != 0)
        return 0;
    return stat(sock, &st) == 0;
#endif
}

/**
 * Lists one registered daemon or reports that it is missing.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @param cb Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on formatting failure.
 */
static int kc_dmn_run_list_one(const char *dir, const char *key, kc_dmn_list_cb cb, void *userdata) {
    if (!kc_dmn_exists(dir, key)) {
        return 0;
    }
    return kc_dmn_ls_row(dir, key, cb, userdata);
}

/**
 * Lists all registered daemon keys in the runtime directory.
 * @param dir Runtime directory.
 * @param cb Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_run_list(const char *dir, kc_dmn_list_cb cb, void *userdata) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[KC_DMN_PATH];
    size_t ext;

    ext = strlen(".pid");
    if ((size_t)snprintf(pattern, sizeof(pattern),
            "%s\\*.pid", dir) >= sizeof(pattern))
        return 1;
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t n = strlen(fd.cFileName);
        if (n > ext) {
            fd.cFileName[n - ext] = '\0';
            if (kc_dmn_ls_row(dir, fd.cFileName, cb, userdata) != 0) {
                FindClose(h);
                return 1;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
#else
    DIR *dp;
    struct dirent *de;

    dp = opendir(dir);
    if (!dp) return 0;
    while ((de = readdir(dp))) {
        size_t n = strlen(de->d_name);
        if (de->d_name[0] == '.') continue;
        if (n > 4 && strcmp(de->d_name + n - 4, ".pid") == 0) continue;
        if (n > 5 && strcmp(de->d_name + n - 5, ".bpid") == 0) continue;
        if (kc_dmn_ls_row(dir, de->d_name, cb, userdata) != 0) {
            closedir(dp);
            return 1;
        }
    }
    closedir(dp);
    return 0;
#endif
}

/**
 * Relays stdin to a registered daemon key and prints the response.
 * @param dir Runtime directory.
 * @param key Daemon key name.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_run_relay(const char *dir, const char *key, kc_dmn_t *ctx) {
#ifdef _WIN32
    char pipename[KC_DMN_PATH];
    char buf[KC_DMN_BUF];
    HANDLE h, hin, hout;
    DWORD br, bw, avail;

    (void)dir;
    (void)ctx;
    if (kc_dmn_sock_path(NULL, key, pipename, sizeof(pipename)) != 0)
        return 1;
    h = CreateFileA(pipename, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 1;
    hin  = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);
    while (ReadFile(hin, buf, sizeof(buf), &br, NULL) && br > 0) {
        if (!WriteFile(h, buf, br, &bw, NULL)) break;
    }
    while (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) &&
            avail > 0) {
        DWORD rd = avail < (DWORD)sizeof(buf)
            ? avail : (DWORD)sizeof(buf);
        if (!ReadFile(h, buf, rd, &br, NULL) || br == 0) break;
        (void)kc_dmn_write_all_handle(hout, buf, br);
    }
    CloseHandle(h);
    return 0;
#else
    char sock[KC_DMN_PATH];
    char buf[KC_DMN_BUF];
    int fd;
    int stdin_open;
    ssize_t n;
    fd_set fds;
    int max_fd;

    if (kc_dmn_sock_path(dir, key, sock, sizeof(sock)) != 0)
        return 1;
    fd = kc_dmn_connect_posix(sock);
    if (fd < 0) return 1;
    stdin_open = 1;
    max_fd = fd > STDIN_FILENO ? fd : STDIN_FILENO;
    while (1) {
        int ready;

        if (ctx && ctx->stop_requested) break;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (stdin_open) FD_SET(STDIN_FILENO, &fds);
        ready = select(max_fd + 1, &fds, NULL, NULL, NULL);
        if (ready < 0) break;
        if (stdin_open && FD_ISSET(STDIN_FILENO, &fds)) {
            n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) {
                stdin_open = 0;
                shutdown(fd, SHUT_WR);
            } else if (kc_dmn_write_all(fd, buf, (size_t)n) != 0) {
                break;
            } else if (memchr(buf, 4, (size_t)n) != NULL) {
                stdin_open = 0;
            }
        }
        if (FD_ISSET(fd, &fds)) {
            n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (kc_dmn_write_all(
                    STDOUT_FILENO, buf, (size_t)n) != 0)
                break;
        }
    }
    close(fd);
    return 0;
#endif
}

/**
 * Sends a signal to a managed daemon process.
 * @param dir   Runtime directory.
 * @param key   Daemon key name.
 * @param signo Signal number (POSIX) or ignored on Windows.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_run_signal(const char *dir, const char *key, int signo) {
    char pidpath[KC_DMN_PATH];
    long pid;

    (void)signo;

#ifdef _WIN32
    if (kc_dmn_pid_path(dir, key, pidpath, sizeof(pidpath)) != 0)
        return 1;
    if (kc_dmn_read_pid(pidpath, &pid) != 0) {
        return 1;
    }
    {
        char evname[64];
        HANDLE hEvent;
        snprintf(evname, sizeof(evname),
            "Global\\DmnSignal_%ld", pid);
        hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, evname);
        if (!hEvent) return 1;
        if (!SetEvent(hEvent)) {
            CloseHandle(hEvent);
            return 1;
        }
        CloseHandle(hEvent);
    }
    return 0;
#else
    if (kc_dmn_backend_pid_path(dir, key,
            pidpath, sizeof(pidpath)) != 0)
        return 1;
    if (kc_dmn_read_pid(pidpath, &pid) != 0) {
        return 1;
    }
    if (kill((pid_t)pid, signo) != 0 && errno != ESRCH)
        return 1;
    return 0;
#endif
}

/**
 * Initialize a new dmn context.
 * @param out Pointer to store the context pointer.
 * @param opts Configuration options.
 * @return KC_DMN_OK on success, KC_DMN_ERROR on failure.
 */
int kc_dmn_open(kc_dmn_t **out, const kc_dmn_options_t *opts) {
    kc_dmn_t *ctx;

    if (!out || !opts) return KC_DMN_ERROR;

    ctx = (kc_dmn_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return KC_DMN_ERROR;

    ctx->opts = *opts;
    ctx->opts.dir = opts->dir ? strdup(opts->dir) : NULL;

    if (ctx->opts.dir && ctx->opts.dir[0]) {
        if ((size_t)snprintf(ctx->dir, sizeof(ctx->dir), "%s", ctx->opts.dir) >= sizeof(ctx->dir)) {
            kc_dmn_set_error(ctx, "path too long");
            return KC_DMN_ERROR;
        }
    } else {
        if (kc_dmn_runtime_dir(ctx->dir, sizeof(ctx->dir)) != 0) {
            kc_dmn_set_error(ctx, "runtime directory unavailable");
            return KC_DMN_ERROR;
        }
    }

    *out = ctx;
    return KC_DMN_OK;
}

/**
 * Release a dmn context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_dmn_close(kc_dmn_t *ctx) {
    if (!ctx) return;
    kc_dmn_options_free(&ctx->opts);
    free(ctx);
}

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_dmn_options_t kc_dmn_options_default(void) {
    kc_dmn_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_dmn_options_load_env(kc_dmn_options_t *opts) {
    int i;
    if (!opts) return;
    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;
        if (!val) continue;
        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_FLOAT: {
                float v = strtof(val, &end);
                if (end != val && *end == '\0') {
                    *(float *)((char *)opts + env_config_table[i].offset) = v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                char **p = (char **)((char *)opts + env_config_table[i].offset);
                free(*p);
                *p = strdup(val);
                break;
            }
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_dmn_options_free(kc_dmn_options_t *opts) {
    if (!opts) return;
    free(opts->dir);
    opts->dir = NULL;
}

/**
 * Request stop for a specific dmn context.
 * @param ctx Context pointer.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_stop(kc_dmn_t *ctx) {
    if (!ctx) return KC_DMN_ERROR;
    ctx->stop_requested = 1;
    return KC_DMN_OK;
}

/**
 * Return the resolved runtime directory for a dmn context.
 * @param ctx Context pointer.
 * @return Runtime directory path, or NULL on invalid input.
 */
const char *kc_dmn_path(kc_dmn_t *ctx) {
    if (!ctx) {
        return NULL;
    }

    return ctx->dir;
}

#ifdef _WIN32
/**
 * Serve one Windows named pipe daemon process.
 * @param pipename Named Pipe path.
 * @param cmd Command string.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_serve(const char *pipename, const char *cmd) {
    if (!pipename || !cmd) {
        return KC_DMN_ERROR;
    }

    return kc_dmn_serve_win32(pipename, cmd) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}
#endif

/**
 * Register or replace a named daemon command.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param cmd Shell command string for the resident backend.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_update(kc_dmn_t *ctx, const char *key, const char *cmd) {
    if (!ctx || !key || !cmd) {
        return KC_DMN_ERROR;
    }

    return kc_dmn_run_update(ctx->dir, key, cmd) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}

/**
 * Delete a named daemon.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_delete(kc_dmn_t *ctx, const char *key) {
    if (!ctx || !key) {
        return KC_DMN_ERROR;
    }

    return kc_dmn_run_delete(ctx->dir, key) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}

/**
 * List registered daemons.
 * Calls cb(key, sock, userdata) per entry.
 * @param ctx Context pointer.
 * @param key Optional daemon key name, or NULL for all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_list(kc_dmn_t *ctx, const char *key, kc_dmn_list_cb cb, void *userdata) {
    if (!ctx) {
        return KC_DMN_ERROR;
    }

    if (key) {
        return kc_dmn_run_list_one(ctx->dir, key, cb, userdata) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
    }

    return kc_dmn_run_list(ctx->dir, cb, userdata) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}

/**
 * Relay stdin/stdout through a named daemon.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_relay(kc_dmn_t *ctx, const char *key) {
    if (!ctx || !key) {
        return KC_DMN_ERROR;
    }

    return kc_dmn_run_relay(ctx->dir, key, ctx) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}

/**
 * Connect to a named daemon for relay.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param out_handle Pointer to receive the handle.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_connect(kc_dmn_t *ctx, const char *key, int *out_handle) {
    char sock[KC_DMN_PATH];

    if (!ctx || !key || !out_handle) return KC_DMN_ERROR;
    *out_handle = -1;
    if (kc_dmn_sock_path(ctx->dir, key, sock, sizeof(sock)) != 0)
        return KC_DMN_ERROR;
#ifdef _WIN32
    {
        HANDLE h = CreateFileA(sock, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) return KC_DMN_ERROR;
        *out_handle = kc_dmn_runner_slot_alloc(key, -1, h);
        if (*out_handle < 0) { CloseHandle(h); return KC_DMN_ERROR; }
    }
#else
    {
        int fd = kc_dmn_connect_posix(sock);
        if (fd < 0) return KC_DMN_ERROR;
        *out_handle = kc_dmn_runner_slot_alloc(key, fd);
        if (*out_handle < 0) { close(fd); return KC_DMN_ERROR; }
    }
#endif
    return KC_DMN_OK;
}

/**
 * Send data to a connected daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @param data Data to send.
 * @param len Data length.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_send(int handle, const void *data, size_t len) {
    kc_dmn_runner_slot_t *h;

    if (!data && len > 0) return KC_DMN_ERROR;
    h = kc_dmn_runner_slot_get(handle);
    if (!h) return KC_DMN_ERROR;
    if (len == 0) return KC_DMN_OK;
#ifdef _WIN32
    {
        DWORD bw;
        if (!WriteFile(h->h, data, (DWORD)len, &bw, NULL) || bw != len)
            return KC_DMN_ERROR;
    }
#else
    if (kc_dmn_write_all(h->fd, data, len) != 0) return KC_DMN_ERROR;
#endif
    return KC_DMN_OK;
}

/**
 * Receive data from a connected daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @param buf Output buffer.
 * @param cap Output buffer capacity.
 * @return Number of bytes received, or -1 on failure.
 */
int kc_dmn_recv(int handle, void *buf, size_t cap) {
    kc_dmn_runner_slot_t *h;

    if (!buf || cap == 0) return -1;
    h = kc_dmn_runner_slot_get(handle);
    if (!h) return -1;
#ifdef _WIN32
    {
        DWORD br;
        if (!ReadFile(h->h, buf, (DWORD)cap, &br, NULL)) return -1;
        return (int)br;
    }
#else
    {
        ssize_t n = read(h->fd, buf, cap);
        return (int)n;
    }
#endif
}

/**
 * Disconnect from a daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_disconnect(int handle) {
    if (kc_dmn_runner_slot_get(handle) == NULL) return KC_DMN_ERROR;
    kc_dmn_runner_slot_free(handle);
    return KC_DMN_OK;
}

/**
 * Send a signal to a managed daemon process.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param signo Signal number (POSIX) or ignored on Windows.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_signal(kc_dmn_t *ctx, const char *key, int signo) {
    if (!ctx || !key) {
        return KC_DMN_ERROR;
    }

    return kc_dmn_run_signal(ctx->dir, key, signo) == 0 ? KC_DMN_OK : KC_DMN_ERROR;
}
