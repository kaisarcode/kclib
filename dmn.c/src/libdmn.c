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

typedef void (*kc_dmn_row_handler_t)(
    const char *name,
    const char *endpoint,
    void *userdata
);

struct kc_dmn {
    char name[KC_DMN_PATH];
    char dir[KC_DMN_PATH];
    char *cmd;
    unsigned char *eot;
    size_t eot_size;
    kc_dmn_handler_t data_handler;
    void *data_userdata;
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

struct kc_dmn_stream {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
};

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
        if (n < 0 && errno == EINTR) continue;
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
static int kc_dmn_ls_row(const char *dir, const char *key, kc_dmn_row_handler_t cb, void *userdata) {
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
static int kc_dmn_run_list_one(const char *dir, const char *key, kc_dmn_row_handler_t cb, void *userdata) {
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
static int kc_dmn_run_list(const char *dir, kc_dmn_row_handler_t cb, void *userdata) {
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

    if (!out) return KC_DMN_ERROR;
    *out = NULL;

    ctx = (kc_dmn_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return KC_DMN_ERROR;

    if (opts && opts->dir && opts->dir[0]) {
        if ((size_t)snprintf(ctx->dir, sizeof(ctx->dir), "%s", opts->dir) >= sizeof(ctx->dir)) {
            kc_dmn_set_error(ctx, "path too long");
            free(ctx);
            return KC_DMN_ERROR;
        }
    } else {
        if (kc_dmn_runtime_dir(ctx->dir, sizeof(ctx->dir)) != 0) {
            kc_dmn_set_error(ctx, "runtime directory unavailable");
            free(ctx);
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
    free(ctx);
}

/**
 * Allocate an options struct initialized with default values.
 * @return Owned options object, or NULL on allocation failure.
 */
kc_dmn_options_t *kc_dmn_options_default(void) {
    kc_dmn_options_t *opts = (kc_dmn_options_t *)calloc(1, sizeof(*opts));
    return opts;
}

/**
 * Set an options value.
 * @param opts Options object.
 * @param key Option key.
 * @param value Option value, or NULL to reset it.
 * @return KC_DMN_OK on success, KC_DMN_ERROR on failure.
 */
int kc_dmn_options_set(
    kc_dmn_options_t *opts,
    const char *key,
    const char *value
) {
    char *copy;

    if (!opts || !key || strcmp(key, "dir") != 0) return KC_DMN_ERROR;
    if (!value) {
        free(opts->dir);
        opts->dir = NULL;
        return KC_DMN_OK;
    }

    copy = (char *)malloc(strlen(value) + 1);
    if (!copy) return KC_DMN_ERROR;
    memcpy(copy, value, strlen(value) + 1);
    free(opts->dir);
    opts->dir = copy;
    return KC_DMN_OK;
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_dmn_options_free(kc_dmn_options_t *opts) {
    if (!opts) return;
    free(opts->dir);
    free(opts);
}

/**
 * Return the resolved runtime directory for a dmn context.
 * @param ctx Context pointer.
 * @return Runtime directory path, or NULL on invalid input.
 */
const char *kc_dmn_path(const kc_dmn_t *ctx) {
    if (!ctx) {
        return NULL;
    }

    return ctx->dir;
}

/**
 * Return the last error message for a dmn context.
 * @param ctx Context pointer.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_dmn_get_error(const kc_dmn_t *ctx) {
    if (!ctx || ctx->error[0] == '\0') return NULL;
    return ctx->error;
}

/**
 * Register or replace a named daemon command.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param cmd Shell command string for the resident backend.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_update(kc_dmn_t *ctx, const char *key, const char *cmd) {
    if (!ctx) {
        return KC_DMN_ERROR;
    }
    if (!key || !cmd) {
        kc_dmn_set_error(ctx, "invalid update arguments");
        return KC_DMN_ERROR;
    }

    if (kc_dmn_run_update(ctx->dir, key, cmd) != 0) {
        kc_dmn_set_error(ctx, "update failed");
        return KC_DMN_ERROR;
    }
    return KC_DMN_OK;
}

/**
 * Delete a named daemon.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_delete(kc_dmn_t *ctx, const char *key) {
    if (!ctx) {
        return KC_DMN_ERROR;
    }
    if (!key) {
        kc_dmn_set_error(ctx, "invalid delete arguments");
        return KC_DMN_ERROR;
    }

    if (kc_dmn_run_delete(ctx->dir, key) != 0) {
        kc_dmn_set_error(ctx, "delete failed");
        return KC_DMN_ERROR;
    }
    return KC_DMN_OK;
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
int kc_dmn_list(kc_dmn_t *ctx, const char *key, kc_dmn_row_handler_t cb, void *userdata) {
    if (!ctx) {
        return KC_DMN_ERROR;
    }

    if (key) {
        if (kc_dmn_run_list_one(ctx->dir, key, cb, userdata) != 0) {
            kc_dmn_set_error(ctx, "list failed");
            return KC_DMN_ERROR;
        }
        return KC_DMN_OK;
    }

    if (kc_dmn_run_list(ctx->dir, cb, userdata) != 0) {
        kc_dmn_set_error(ctx, "list failed");
        return KC_DMN_ERROR;
    }
    return KC_DMN_OK;
}

/**
 * Connect to a named daemon for relay.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param out Pointer to receive the connection object.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_connect(kc_dmn_t *ctx, const char *key, kc_dmn_stream_t **out) {
    char sock[KC_DMN_PATH];
    kc_dmn_stream_t *conn;

    if (!out) {
        if (ctx) kc_dmn_set_error(ctx, "invalid connect arguments");
        return KC_DMN_ERROR;
    }
    *out = NULL;
    if (!ctx) return KC_DMN_ERROR;
    if (!key) {
        kc_dmn_set_error(ctx, "invalid connect arguments");
        return KC_DMN_ERROR;
    }
    if (kc_dmn_sock_path(ctx->dir, key, sock, sizeof(sock)) != 0) {
        kc_dmn_set_error(ctx, "socket path failed");
        return KC_DMN_ERROR;
    }
    conn = (kc_dmn_stream_t *)calloc(1, sizeof(*conn));
    if (!conn) {
        kc_dmn_set_error(ctx, "connection allocation failed");
        return KC_DMN_ERROR;
    }
#ifdef _WIN32
    conn->handle = CreateFileA(sock, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (conn->handle == INVALID_HANDLE_VALUE) {
        free(conn);
        kc_dmn_set_error(ctx, "connect failed");
        return KC_DMN_ERROR;
    }
#else
    conn->fd = kc_dmn_connect_posix(sock);
    if (conn->fd < 0) {
        free(conn);
        kc_dmn_set_error(ctx, "connect failed");
        return KC_DMN_ERROR;
    }
#endif
    *out = conn;
    return KC_DMN_OK;
}

/**
 * Send data to a connected daemon.
 * @param conn Connection object from kc_dmn_connect.
 * @param data Data to send.
 * @param data_size Data length.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_send(kc_dmn_stream_t *conn, const void *data, size_t data_size) {
#ifdef _WIN32
    size_t off = 0;
#endif

    if (!conn || (!data && data_size > 0)) return KC_DMN_ERROR;
    if (data_size == 0) return KC_DMN_OK;
#ifdef _WIN32
    while (off < data_size) {
        size_t chunk = data_size - off;
        DWORD written;
        if (chunk > (size_t)(DWORD)-1) chunk = (size_t)(DWORD)-1;
        if (!WriteFile(conn->handle, (const char *)data + off,
                (DWORD)chunk, &written, NULL) || written == 0)
            return KC_DMN_ERROR;
        off += (size_t)written;
    }
#else
    if (kc_dmn_write_all(conn->fd, (const char *)data, data_size) != 0)
        return KC_DMN_ERROR;
#endif
    return KC_DMN_OK;
}

/**
 * Receive data from a connected daemon.
 * @param conn Connection object from kc_dmn_connect.
 * @param max_size Maximum receive size.
 * @param out_data Pointer to receive owned data.
 * @param out_size Pointer to receive the received size.
 * @return KC_DMN_OK, KC_DMN_EOF, or KC_DMN_ERROR.
 */
int kc_dmn_recv(kc_dmn_stream_t *conn, size_t max_size,
    void **out_data, size_t *out_size) {
    void *buffer;
    size_t received;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!conn || !out_data || !out_size || max_size == 0)
        return KC_DMN_ERROR;
    buffer = malloc(max_size);
    if (!buffer) return KC_DMN_ERROR;
#ifdef _WIN32
    {
        DWORD br;
        if (max_size > (size_t)(DWORD)-1) {
            free(buffer);
            return KC_DMN_ERROR;
        }
        if (!ReadFile(conn->handle, buffer, (DWORD)max_size, &br, NULL)) {
            DWORD error = GetLastError();
            free(buffer);
            if (error == ERROR_BROKEN_PIPE ||
                    error == ERROR_PIPE_NOT_CONNECTED ||
                    error == ERROR_NO_DATA || error == ERROR_HANDLE_EOF)
                return KC_DMN_EOF;
            return KC_DMN_ERROR;
        }
        received = (size_t)br;
    }
#else
    {
        ssize_t n;
        do {
            n = read(conn->fd, buffer, max_size);
        } while (n < 0 && errno == EINTR);
        if (n < 0) {
            free(buffer);
            return KC_DMN_ERROR;
        }
        if (n == 0) {
            free(buffer);
            return KC_DMN_EOF;
        }
        received = (size_t)n;
    }
#endif
    if (received == 0) {
        free(buffer);
        return KC_DMN_EOF;
    }
    {
        void *result = malloc(received);
        if (!result) {
            free(buffer);
            return KC_DMN_ERROR;
        }
        memcpy(result, buffer, received);
        free(buffer);
        *out_data = result;
        *out_size = received;
    }
    return KC_DMN_OK;
}

/**
 * Disconnect from a daemon.
 * @param conn Connection object from kc_dmn_connect.
 * @return None.
 */
void kc_dmn_disconnect(kc_dmn_stream_t *conn) {
    if (!conn) return;
#ifdef _WIN32
    if (conn->handle != INVALID_HANDLE_VALUE) CloseHandle(conn->handle);
#else
    if (conn->fd >= 0) close(conn->fd);
#endif
    free(conn);
}

/**
 * Free memory returned by a dmn API.
 * @param ptr API-owned pointer, or NULL.
 * @return None.
 */
void kc_dmn_free(void *ptr) {
    free(ptr);
}

/**
 * Send a signal to a managed daemon process.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param signo Signal number (POSIX) or ignored on Windows.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_signal(kc_dmn_t *ctx, const char *key, int signo) {
    if (!ctx) {
        return KC_DMN_ERROR;
    }
    if (!key) {
        kc_dmn_set_error(ctx, "invalid signal arguments");
        return KC_DMN_ERROR;
    }

    if (kc_dmn_run_signal(ctx->dir, key, signo) != 0) {
        kc_dmn_set_error(ctx, "signal failed");
        return KC_DMN_ERROR;
    }
    return KC_DMN_OK;
}
