/**
 * libnetl.c - Network listener.
 * Summary: Core implementation for registering and serving named network listeners.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
typedef SOCKET kc_netl_fd_t;
typedef HANDLE kc_netl_thread_t;
#  define KC_NETL_FD_INVALID  INVALID_SOCKET
#  define KC_NETL_FD_CLOSE(f) closesocket(f)
#  define KC_NETL_FD_OPEN(f) ((f) >= 0 ? (HANDLE)(intptr_t)(f) : INVALID_HANDLE_VALUE)
#  define KC_NETL_SEP         "\\"
#  define KC_NETL_GETPID()    ((long)GetCurrentProcessId())
#  define KC_NETL_THREAD_CREATE(t, fn, arg) do { *(t) = CreateThread(NULL, 0, (fn), (arg), 0, NULL); } while(0)
#  define KC_NETL_THREAD_JOIN(t) do { if (*(t)) { WaitForSingleObject(*(t), INFINITE); CloseHandle(*(t)); } } while(0)
#  define open _open
#  define close _close
#  define read _read
#  define write _write
#  define O_RDONLY _O_RDONLY
#  define O_WRONLY _O_WRONLY
#  define O_CREAT _O_CREAT
#  define O_TRUNC _O_TRUNC
#else
#  include <dirent.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <signal.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <pthread.h>
typedef int kc_netl_fd_t;
typedef pthread_t kc_netl_thread_t;
#  define KC_NETL_FD_INVALID  (-1)
#  define KC_NETL_FD_CLOSE(f) close(f)
#  define KC_NETL_SEP         "/"
#  define KC_NETL_GETPID()    ((long)getpid())
#  define KC_NETL_THREAD_CREATE(t, fn, arg) pthread_create((t), NULL, (fn), (arg))
#  define KC_NETL_THREAD_JOIN(t) pthread_join(*(t), NULL)
#endif

#define KC_NETL_BUF 65536

typedef struct {
    int used;
    int slot;
    int proto;
    volatile sig_atomic_t stop_requested;
    kc_netl_fd_t fd;
    kc_netl_fd_t cli_fd;
    kc_netl_data_fn callback;
    void *userdata;
    kc_netl_thread_t thread;
    char key[256];
} kc_netl_listener_t;

struct kc_netl {
    char dir[512];
    kc_netl_options_t opts;
    volatile sig_atomic_t stop_requested;
    kc_netl_listener_t *listeners;
    int listeners_cap;
    int listeners_count;
};

#ifdef _WIN32

/**
 * Initializes the Winsock network stack.
 * @return 0 on success, or -1 on failure.
 */
static int kc_netl_platform_init(void) {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
}

/**
 * Cleans up the Winsock network stack.
 * @return none
 */
static void kc_netl_platform_cleanup(void) { WSACleanup(); }

#else

/**
 * Initializes the network stack (no-op on POSIX).
 * @return 0 always.
 */
static int kc_netl_platform_init(void) { return 0; }

/**
 * Cleans up the network stack (no-op on POSIX).
 * @return none
 */
static void kc_netl_platform_cleanup(void) {}

#endif

static int kc_netl_read_meta(
const char *path,
char *addrport, size_t ap_cap,
int *proto,
char *cmd, size_t cmd_cap,
long *pid
);

static int kc_netl_split_addr(
const char *addrport,
char *host, size_t host_cap,
unsigned short *port
);

/**
 * Binds a socket to the given host, port, and protocol.
 * @param host      Bind host or IP. NULL binds all interfaces.
 * @param port      Bind port number.
 * @param proto     KC_NETL_TCP or KC_NETL_UDP.
 * @param reuseport Whether to set SO_REUSEPORT.
 * @return Bound socket, or KC_NETL_FD_INVALID on failure.
 */
static kc_netl_fd_t kc_netl_bind_socket(
const char *host,
unsigned short port,
int proto,
int reuseport
) {
    struct addrinfo hints, *res, *it;
    kc_netl_fd_t fd = KC_NETL_FD_INVALID;
    int opt = 1;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = (proto == KC_NETL_UDP) ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    if (getaddrinfo(
            (host && host[0]) ? host : NULL,
            port_str, &hints, &res) != 0)
        return KC_NETL_FD_INVALID;

    for (it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == KC_NETL_FD_INVALID) continue;
#ifdef _WIN32
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
            (const char *)&opt, sizeof(opt));
        (void)reuseport;
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#  ifdef SO_REUSEPORT
        if (reuseport)
            setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#  else
        (void)reuseport;
#  endif
#endif
        if (bind(fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) break;
        KC_NETL_FD_CLOSE(fd);
        fd = KC_NETL_FD_INVALID;
    }
    freeaddrinfo(res);
    return fd;
}

#ifndef _WIN32

/**
 * Reaps terminated child processes.
 * @param sig Signal number.
 * @return none
 */
static void kc_netl_on_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

/**
 * Accepts TCP connections and forks a handler for each one.
 * @param fd  Bound listening socket.
 * @param cmd Shell command to run per connection.
 * @return KC_NETL_ENET on listen failure.
 */
static int kc_netl_serve_tcp(kc_netl_fd_t fd, const char *cmd) {
    if (listen((int)fd, 128) != 0) return KC_NETL_ENET;
    signal(SIGCHLD, kc_netl_on_sigchld);
    for (;;) {
        kc_netl_fd_t cli = accept((int)fd, NULL, NULL);
        pid_t p;
        if (cli == KC_NETL_FD_INVALID) continue;
        p = fork();
        if (p < 0) {
            KC_NETL_FD_CLOSE(cli);
            continue;
        }
        if (p == 0) {
            KC_NETL_FD_CLOSE(fd);
            if (dup2((int)cli, 0) < 0 || dup2((int)cli, 1) < 0) _exit(1);
            KC_NETL_FD_CLOSE(cli);
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(1);
        }
        KC_NETL_FD_CLOSE(cli);
    }
}

/**
 * Receives UDP datagrams and forks a handler for each one.
 * @param fd  Bound UDP socket.
 * @param cmd Shell command to run per datagram.
 * @return KC_NETL_ENET on fatal error.
 */
static int kc_netl_serve_udp_worker(kc_netl_fd_t fd, const char *cmd) {
    for (;;) {
        char buf[KC_NETL_BUF];
        int pp[2];
        pid_t p;
        const char *bp;
        size_t rem;
        ssize_t nb = recvfrom((int)fd, buf, sizeof(buf), 0, NULL, NULL);
        if (nb < 0) continue;
        if (pipe(pp) != 0) continue;
        p = fork();
        if (p < 0) {
            close(pp[0]);
            close(pp[1]);
            continue;
        }
        if (p == 0) {
            KC_NETL_FD_CLOSE(fd);
            close(pp[1]);
            if (dup2(pp[0], 0) < 0) _exit(1);
            close(pp[0]);
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(1);
        }
        close(pp[0]);
        bp  = buf;
        rem = (size_t)nb;
        while (rem > 0) {
            ssize_t w = write(pp[1], bp, rem);
            if (w <= 0) break;
            bp  += (size_t)w;
            rem -= (size_t)w;
        }
        close(pp[1]);
    }
}

/**
 * Spawns one worker per CPU for UDP and runs the datagram loop.
 * @param host  Bind host (for worker re-binding with SO_REUSEPORT).
 * @param port  Bind port.
 * @param fd    Already bound UDP socket for the first worker.
 * @param cmd   Shell command to run per datagram.
 * @return KC_NETL_ENET on fatal error.
 */
static int kc_netl_serve_udp(
const char *host,
unsigned short port,
kc_netl_fd_t fd,
const char *cmd
) {
    long cp = sysconf(_SC_NPROCESSORS_ONLN);
    size_t wc = (cp > 1) ? (size_t)cp : 1;
    size_t i;

    signal(SIGCHLD, kc_netl_on_sigchld);

    for (i = 1; i < wc; i++) {
        pid_t p = fork();
        if (p < 0) break;
        if (p == 0) {
            kc_netl_fd_t wfd = kc_netl_bind_socket(host, port, KC_NETL_UDP, 1);
            KC_NETL_FD_CLOSE(fd);
            if (wfd == KC_NETL_FD_INVALID) _exit(1);
            kc_netl_serve_udp_worker(wfd, cmd);
            _exit(0);
        }
    }
    return kc_netl_serve_udp_worker(fd, cmd);
}

#else

typedef struct {
    SOCKET sock;
    char   cmd[4096];
} kc_netl_win_tcp_args_t;

typedef struct {
    SOCKET sock;
    HANDLE pipe_wr;
} kc_netl_win_s2p_t;

typedef struct {
    SOCKET sock;
    HANDLE pipe_rd;
} kc_netl_win_p2s_t;

/**
 * Thread that relays socket recv to a child process stdin pipe.
 * @param arg Pointer to kc_netl_win_s2p_t.
 * @return 0 on exit.
 */
static DWORD WINAPI kc_netl_win_relay_s2p(LPVOID arg) {
    kc_netl_win_s2p_t *r = (kc_netl_win_s2p_t *)arg;
    char buf[8192];
    int n;
    DWORD w;
    while ((n = recv(r->sock, buf, sizeof(buf), 0)) > 0)
        if (!WriteFile(r->pipe_wr, buf, (DWORD)n, &w, NULL)) break;
    CloseHandle(r->pipe_wr);
    return 0;
}

/**
 * Thread that relays child process stdout pipe to socket send.
 * @param arg Pointer to kc_netl_win_p2s_t.
 * @return 0 on exit.
 */
static DWORD WINAPI kc_netl_win_relay_p2s(LPVOID arg) {
    kc_netl_win_p2s_t *r = (kc_netl_win_p2s_t *)arg;
    char buf[8192];
    DWORD n;
    while (ReadFile(r->pipe_rd, buf, sizeof(buf), &n, NULL) && n > 0)
        send(r->sock, buf, (int)n, 0);
    return 0;
}

/**
 * Thread that handles one accepted TCP connection.
 * Spawns a child process with stdio bridged to the socket via pipes.
 * @param arg Heap-allocated kc_netl_win_tcp_args_t (freed here).
 * @return 0 on exit.
 */
static DWORD WINAPI kc_netl_win_tcp_worker(LPVOID arg) {
    kc_netl_win_tcp_args_t *a = (kc_netl_win_tcp_args_t *)arg;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    HANDLE pin_r, pin_w, pout_r, pout_w;
    kc_netl_win_s2p_t rs2p;
    kc_netl_win_p2s_t rp2s;
    HANDLE t1 = NULL, t2 = NULL;

    if (!CreatePipe(&pin_r, &pin_w, &sa, 0)) goto done;
    if (!CreatePipe(&pout_r, &pout_w, &sa, 0)) {
        CloseHandle(pin_r); CloseHandle(pin_w); goto done;
    }
    SetHandleInformation(pin_w,  HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(pout_r, HANDLE_FLAG_INHERIT, 0);

    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = pin_r;
    si.hStdOutput = pout_w;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessA(NULL, a->cmd, NULL, NULL, TRUE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pin_r);  CloseHandle(pin_w);
        CloseHandle(pout_r); CloseHandle(pout_w);
        goto done;
    }
    CloseHandle(pin_r);
    CloseHandle(pout_w);

    rs2p.sock    = a->sock;
    rs2p.pipe_wr = pin_w;
    rp2s.sock    = a->sock;
    rp2s.pipe_rd = pout_r;

    t1 = CreateThread(NULL, 0, kc_netl_win_relay_s2p, &rs2p, 0, NULL);
    t2 = CreateThread(NULL, 0, kc_netl_win_relay_p2s, &rp2s, 0, NULL);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (t1) { WaitForSingleObject(t1, 5000); CloseHandle(t1); }
    if (t2) { WaitForSingleObject(t2, 5000); CloseHandle(t2); }
    CloseHandle(pout_r);
done:
    closesocket(a->sock);
    free(a);
    return 0;
}

/**
 * Accepts TCP connections and spawns a worker thread for each.
 * @param fd   Bound listening TCP socket.
 * @param cmd  Shell command to run per connection.
 * @return KC_NETL_ENET on fatal error.
 */
static int kc_netl_win_serve_tcp(SOCKET fd, const char *cmd) {
    listen(fd, 128);
    for (;;) {
        kc_netl_win_tcp_args_t *a;
        HANDLE th;
        SOCKET cli = accept(fd, NULL, NULL);
        if (cli == INVALID_SOCKET) continue;
        a = (kc_netl_win_tcp_args_t *)malloc(sizeof(*a));
        if (!a) { closesocket(cli); continue; }
        a->sock = cli;
        snprintf(a->cmd, sizeof(a->cmd), "%s", cmd);
        th = CreateThread(NULL, 0, kc_netl_win_tcp_worker, a, 0, NULL);
        if (th) CloseHandle(th);
        else    { closesocket(cli); free(a); }
    }
    return KC_NETL_ENET;
}

/**
 * Receives UDP datagrams and spawns one child process per datagram.
 * @param fd   Bound UDP socket.
 * @param cmd  Shell command to run per datagram.
 * @return KC_NETL_ENET on fatal error.
 */
static int kc_netl_win_serve_udp(SOCKET fd, const char *cmd) {
    for (;;) {
        char buf[KC_NETL_BUF];
        int nb = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
        HANDLE pin_r, pin_w;
        STARTUPINFOA si = {0};
        PROCESS_INFORMATION pi = {0};
        char cmdline[4096];
        DWORD nw;

        if (nb <= 0) continue;
        if (!CreatePipe(&pin_r, &pin_w, &sa, 0)) continue;
        SetHandleInformation(pin_w, HANDLE_FLAG_INHERIT, 0);

        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = pin_r;
        si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

        snprintf(cmdline, sizeof(cmdline), "%s", cmd);
        if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pin_r);
            WriteFile(pin_w, buf, (DWORD)nb, &nw, NULL);
            CloseHandle(pin_w);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            CloseHandle(pin_r);
            CloseHandle(pin_w);
        }
    }
    return KC_NETL_ENET;
}

#endif

/**
 * Binds to an address and serves incoming connections in a blocking loop.
 * TCP: forks one handler per connection with stdin and stdout on the socket.
 * UDP: forks one handler per datagram with stdin piped from the packet.
 * Never returns on success.
 * @param host  Bind host or IP. NULL or empty string binds all interfaces.
 * @param port  Bind port.
 * @param proto KC_NETL_TCP or KC_NETL_UDP.
 * @param cmd   Shell command executed per connection or datagram.
 * @return Negative error code on setup failure.
 */
int kc_netl_serve(
const char *host,
unsigned short port,
int proto,
const char *cmd
) {
    kc_netl_fd_t fd;
    int rc;

    if (!cmd || !cmd[0]) return KC_NETL_ERROR;
    if (proto != KC_NETL_TCP && proto != KC_NETL_UDP) return KC_NETL_ERROR;
    if (kc_netl_platform_init() != 0) return KC_NETL_ENET;

    fd = kc_netl_bind_socket(host, port, proto, proto == KC_NETL_UDP);
    if (fd == KC_NETL_FD_INVALID) {
        kc_netl_platform_cleanup();
        return KC_NETL_ENET;
    }

#ifndef _WIN32
    if (proto == KC_NETL_UDP)
        rc = kc_netl_serve_udp(host, port, fd, cmd);
    else
        rc = kc_netl_serve_tcp(fd, cmd);
#else
    if (proto == KC_NETL_UDP)
        rc = kc_netl_win_serve_udp(fd, cmd);
    else
        rc = kc_netl_win_serve_tcp(fd, cmd);
#endif

    KC_NETL_FD_CLOSE(fd);
    kc_netl_platform_cleanup();
    return rc;
}

/**
 * Returns a static string for a netl error code.
 * @param code Error code.
 * @return Static string.
 */
const char *kc_netl_strerror(int code) {
    switch (code) {
        case KC_NETL_OK:    return "ok";
        case KC_NETL_ERROR: return "error";
        case KC_NETL_ENET:  return "network error";
        default:            return "unknown error";
    }
}

/**
 * Creates a directory if it does not exist.
 * @param path Directory path to create.
 * @return None.
 */
static void kc_netl_mkdir(const char *path) {
#ifndef _WIN32
    mkdir(path, 0700);
#else
    CreateDirectoryA(path, NULL);
#endif
}

/**
 * Resolves the metadata directory into buf.
 * @param buf Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, -1 on failure.
 */
static int kc_netl_data_dir(char *buf, size_t cap) {
    const char *base;
    int n;
#ifndef _WIN32
    base = getenv("XDG_DATA_HOME");
    if (base && base[0])
        n = snprintf(buf, cap, "%s/netl", base);
    else {
        base = getenv("HOME");
        if (!base || !base[0]) return -1;
        n = snprintf(buf, cap, "%s/.local/share/netl", base);
    }
#else
    base = getenv("APPDATA");
    if (!base || !base[0]) return -1;
    n = snprintf(buf, cap, "%s\\netl", base);
#endif
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

/**
 * Opens a netl context, resolving the metadata directory.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_open(kc_netl_t **out, const kc_netl_options_t *opts) {
    kc_netl_t *ctx;
    if (!out || !opts) return KC_NETL_ERROR;
    ctx = (kc_netl_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return KC_NETL_ERROR;
    ctx->opts = *opts;
    ctx->listeners = NULL;
    ctx->listeners_cap = 0;
    ctx->listeners_count = 0;
    if (kc_netl_data_dir(ctx->dir, sizeof(ctx->dir)) != 0) {
        free(ctx);
        return KC_NETL_ERROR;
    }
    kc_netl_mkdir(ctx->dir);
    *out = ctx;
    return KC_NETL_OK;
}

/**
 * Releases a netl context.
 * @param ctx Context to close and free.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_close(kc_netl_t *ctx) {
    int i;
    if (!ctx) return KC_NETL_ERROR;
    for (i = 0; i < ctx->listeners_count; i++) {
        if (ctx->listeners[i].used) {
            kc_netl_close_listener(ctx, i);
        }
    }
    free(ctx->listeners);
    kc_netl_options_free(&ctx->opts);
    free(ctx);
    return KC_NETL_OK;
}

/**
 * Finds a free slot in the listener array.
 * @param ctx Open context.
 * @return Slot index, or -1 if allocation failed.
 */
static int kc_netl_listener_alloc(kc_netl_t *ctx) {
    int i;
    for (i = 0; i < ctx->listeners_count; i++) {
        if (!ctx->listeners[i].used) return i;
    }
    if (ctx->listeners_count >= ctx->listeners_cap) {
        int new_cap = ctx->listeners_cap == 0 ? 4 : ctx->listeners_cap * 2;
        kc_netl_listener_t *new_arr = (kc_netl_listener_t *)realloc(
            ctx->listeners, (size_t)new_cap * sizeof(kc_netl_listener_t));
        if (!new_arr) return -1;
        ctx->listeners = new_arr;
        ctx->listeners_cap = new_cap;
    }
    memset(&ctx->listeners[ctx->listeners_count], 0, sizeof(kc_netl_listener_t));
    return ctx->listeners_count++;
}

/**
 * Closes a listener socket and cleans up.
 * @param l Listener entry.
 * @return None.
 */
static void kc_netl_listener_cleanup(kc_netl_listener_t *l) {
    if (l->cli_fd != KC_NETL_FD_INVALID && l->cli_fd != 0) {
        KC_NETL_FD_CLOSE(l->cli_fd);
    }
    if (l->fd != KC_NETL_FD_INVALID && l->fd != 0) {
        KC_NETL_FD_CLOSE(l->fd);
    }
    memset(l, 0, sizeof(*l));
    l->fd = KC_NETL_FD_INVALID;
    l->cli_fd = KC_NETL_FD_INVALID;
}

/**
 * Thread function for listener I/O loop.
 * @param arg Pointer to listener entry.
 * @return NULL.
 */
static void *kc_netl_listener_thread(void *arg) {
    kc_netl_listener_t *l = (kc_netl_listener_t *)arg;
    char buf[KC_NETL_BUF];
    fd_set rfds;
    struct timeval tv;
    int r;

    (void)kc_netl_platform_init();

    if (l->proto == KC_NETL_TCP) {
        if (listen((int)l->fd, 128) != 0) {
            kc_netl_listener_cleanup(l);
            return NULL;
        }
        for (;;) {
            if (l->stop_requested) break;
            FD_ZERO(&rfds);
            FD_SET((unsigned)(l->fd), &rfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            r = select((int)(l->fd) + 1, &rfds, NULL, NULL, &tv);
            if (r < 0) break;
            if (r == 0) continue;
            if (!FD_ISSET((unsigned)(l->fd), &rfds)) continue;

            if (l->cli_fd != KC_NETL_FD_INVALID) {
                KC_NETL_FD_CLOSE(l->cli_fd);
            }
            l->cli_fd = accept((int)l->fd, NULL, NULL);
            if (l->cli_fd == KC_NETL_FD_INVALID) continue;

            for (;;) {
                if (l->stop_requested) {
                    if (l->cli_fd != KC_NETL_FD_INVALID) {
                        KC_NETL_FD_CLOSE(l->cli_fd);
                        l->cli_fd = KC_NETL_FD_INVALID;
                    }
                    kc_netl_listener_cleanup(l);
                    return NULL;
                }
                FD_ZERO(&rfds);
                FD_SET((unsigned)(l->cli_fd), &rfds);
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                r = select((int)(l->cli_fd) + 1, &rfds, NULL, NULL, &tv);
                if (r < 0) break;
                if (r == 0) continue;
                if (!FD_ISSET((unsigned)(l->cli_fd), &rfds)) continue;

                r = (int)recv((int)l->cli_fd, (char *)buf, (int)sizeof(buf) - 1, 0);
                if (r <= 0) break;
                if (l->callback(buf, (size_t)r, l->userdata) != 0) break;
            }
            if (l->cli_fd != KC_NETL_FD_INVALID) {
                KC_NETL_FD_CLOSE(l->cli_fd);
                l->cli_fd = KC_NETL_FD_INVALID;
            }
        }
    } else {
        for (;;) {
            if (l->stop_requested) break;
            FD_ZERO(&rfds);
            FD_SET((unsigned)(l->fd), &rfds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            r = select((int)(l->fd) + 1, &rfds, NULL, NULL, &tv);
            if (r < 0) break;
            if (r == 0) continue;
            if (!FD_ISSET((unsigned)(l->fd), &rfds)) continue;

            r = (int)recvfrom((int)l->fd, (char *)buf, (int)sizeof(buf) - 1, 0, NULL, NULL);
            if (r <= 0) continue;
            if (l->callback(buf, (size_t)r, l->userdata) != 0) break;
        }
    }

    kc_netl_listener_cleanup(l);
    return NULL;
}

#ifdef _WIN32
/**
 * Windows thread wrapper for listener thread.
 * @param arg Pointer to listener entry.
 * @return 0.
 */
static DWORD WINAPI kc_netl_listener_thread_win(LPVOID arg) {
    kc_netl_listener_thread(arg);
    return 0;
}
#endif

/**
 * Starts a non-blocking listener for a registered key.
 * When data arrives, the callback is invoked with the data.
 * @param ctx Open context.
 * @param key Registration name.
 * @param callback Function called when data arrives.
 * @param userdata Opaque pointer passed to callback.
 * @return Non-negative handle on success, or negative error code.
 */
int kc_netl_listen(kc_netl_t *ctx, const char *key, kc_netl_data_fn callback, void *userdata) {
    char path[600], addrport[256], cmd[KC_NETL_BUF], host[256];
    unsigned short port;
    int proto;
    int n;
    kc_netl_fd_t fd;
    int slot;

    if (!ctx || !key || !key[0] || !callback) return KC_NETL_ERROR;
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0) {
        return KC_NETL_ERROR;
    }
    if (kc_netl_split_addr(addrport, host, sizeof(host), &port) != 0) return KC_NETL_ERROR;
    if (kc_netl_platform_init() != 0) return KC_NETL_ENET;

    fd = kc_netl_bind_socket(host, port, proto, 0);
    if (fd == KC_NETL_FD_INVALID) {
        kc_netl_platform_cleanup();
        return KC_NETL_ENET;
    }

    slot = kc_netl_listener_alloc(ctx);
    if (slot < 0) {
        KC_NETL_FD_CLOSE(fd);
        kc_netl_platform_cleanup();
        return KC_NETL_ERROR;
    }

    ctx->listeners[slot].used = 1;
    ctx->listeners[slot].slot = slot;
    ctx->listeners[slot].proto = proto;
    ctx->listeners[slot].fd = fd;
    ctx->listeners[slot].cli_fd = KC_NETL_FD_INVALID;
    ctx->listeners[slot].callback = callback;
    ctx->listeners[slot].userdata = userdata;
    ctx->listeners[slot].stop_requested = 0;
    snprintf(ctx->listeners[slot].key, sizeof(ctx->listeners[slot].key), "%s", key);

#ifdef _WIN32
    KC_NETL_THREAD_CREATE(&ctx->listeners[slot].thread, kc_netl_listener_thread_win, &ctx->listeners[slot]);
#else
    KC_NETL_THREAD_CREATE(&ctx->listeners[slot].thread, kc_netl_listener_thread, &ctx->listeners[slot]);
#endif

    if (!ctx->listeners[slot].thread) {
        kc_netl_listener_cleanup(&ctx->listeners[slot]);
        return KC_NETL_ERROR;
    }

    return slot;
}

/**
 * Sends data through an active listener socket.
 * @param ctx Open context.
 * @param handle Listener handle from kc_netl_listen.
 * @param buf Data buffer.
 * @param len Number of bytes to send.
 * @return Number of bytes sent, or negative error code.
 */
int kc_netl_send(kc_netl_t *ctx, int handle, const char *buf, size_t len) {
    kc_netl_listener_t *l;

    if (!ctx || handle < 0 || handle >= ctx->listeners_count) return KC_NETL_ERROR;
    l = &ctx->listeners[handle];
    if (!l->used) return KC_NETL_ERROR;

    if (l->proto == KC_NETL_UDP) {
        return (int)send((int)l->fd, buf, (int)len, 0);
    } else {
        if (l->cli_fd == KC_NETL_FD_INVALID) return KC_NETL_ERROR;
        return (int)send((int)l->cli_fd, buf, (int)len, 0);
    }
}

/**
 * Closes an active listener and releases its resources.
 * @param ctx Open context.
 * @param handle Listener handle from kc_netl_listen.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_close_listener(kc_netl_t *ctx, int handle) {
    kc_netl_listener_t *l;

    if (!ctx || handle < 0 || handle >= ctx->listeners_count) return KC_NETL_ERROR;
    l = &ctx->listeners[handle];
    if (!l->used) return KC_NETL_ERROR;

    l->stop_requested = 1;
    if (l->cli_fd != KC_NETL_FD_INVALID) {
        KC_NETL_FD_CLOSE(l->cli_fd);
        l->cli_fd = KC_NETL_FD_INVALID;
    }
    if (l->fd != KC_NETL_FD_INVALID) {
        KC_NETL_FD_CLOSE(l->fd);
        l->fd = KC_NETL_FD_INVALID;
    }

    if (l->thread) {
        KC_NETL_THREAD_JOIN(&l->thread);
        l->thread = 0;
    }

    kc_netl_listener_cleanup(l);
    return KC_NETL_OK;
}

/**
 * Returns the resolved metadata directory path.
 * @param ctx Open context.
 * @return Pointer to the internal path string.
 */
const char *kc_netl_path(kc_netl_t *ctx) {
    return ctx ? ctx->dir : NULL;
}

/**
 * Registers or replaces a named network listener.
 * Stores host:port, protocol, and command in the metadata directory.
 * @param ctx   Open context.
 * @param key   Registration name.
 * @param host  Bind host or IP.
 * @param port  Bind port.
 * @param proto KC_NETL_TCP or KC_NETL_UDP.
 * @param cmd   Command to store, or NULL for callback model (stored as "-").
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_update(kc_netl_t *ctx, const char *key, const char *host, unsigned short port, int proto, const char *cmd) {
    char path[600];
    char buf[512];
    int n;
    int fd;
    ssize_t written;
    const char *cmd_to_store;

    if (!ctx || !key || !key[0] || !host || !host[0]) return KC_NETL_ERROR;
    if (proto != KC_NETL_TCP && proto != KC_NETL_UDP) return KC_NETL_ERROR;
    cmd_to_store = (cmd && cmd[0]) ? cmd : "-";
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    (void)kc_netl_stop(ctx, key);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return KC_NETL_ERROR;
    n = snprintf(buf, sizeof(buf), "%s:%u\n%s\n%s\n0\n",
        host, (unsigned)port,
        proto == KC_NETL_UDP ? "udp" : "tcp",
        cmd_to_store);
    written = write(fd, buf, (size_t)n);
    close(fd);
    if (written != n) return KC_NETL_ERROR;
    return KC_NETL_OK;
}

/**
 * Reads a metadata file into the provided buffers.
 * @param path     Metadata file path.
 * @param addrport Output addr:port buffer.
 * @param ap_cap   Capacity of addrport.
 * @param proto    Output protocol pointer.
 * @param cmd      Output command buffer.
 * @param cmd_cap  Capacity of cmd.
 * @return 0 on success, -1 on failure.
 */
static int kc_netl_read_meta(
const char *path,
char *addrport, size_t ap_cap,
int *proto,
char *cmd, size_t cmd_cap,
long *pid
) {
    char buf[1024];
    char line[64];
    ssize_t n;
    int fd;
    char *p;
    size_t left;
    int line_num = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    p = buf;
    left = (size_t)n;
    line_num = 0;

    while (line_num < 4) {
        char *line_end;
        size_t line_len;

        line_end = strchr(p, '\n');
        if (!line_end) line_end = p + strlen(p);
        line_len = (size_t)(line_end - p);
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        while (line_len > 0 && (line[line_len-1] == '\r' || line[line_len-1] == '\n')) {
            line[--line_len] = '\0';
        }

        if (line_len == 0) {
            p = line_end + 1;
            left = (size_t)(n - (p - buf));
            line_num++;
            continue;
        }

        switch (line_num) {
            case 0:
                if (line_len >= (size_t)ap_cap) { return -1; }
                memcpy(addrport, line, line_len + 1);
                break;
            case 1:
                *proto = (strcmp(line, "udp") == 0) ? KC_NETL_UDP : KC_NETL_TCP;
                break;
            case 2:
                if (line_len >= (size_t)cmd_cap) { return -1; }
                memcpy(cmd, line, line_len + 1);
                break;
            case 3:
                if (pid) *pid = atol(line);
                break;
        }

        p = line_end + 1;
        left = (size_t)(n - (p - buf));
        if (left == 0 || p >= buf + n) break;
        line_num++;
    }

    return 0;
}

/**
 * Splits an addr:port string into host and port.
 * @param addrport Input string in host:port format.
 * @param host     Output host buffer.
 * @param host_cap Host buffer capacity.
 * @param port     Output port pointer.
 * @return 0 on success, -1 on failure.
 */
static int kc_netl_split_addr(
const char *addrport,
char *host, size_t host_cap,
unsigned short *port
) {
    const char *colon;
    char *end;
    unsigned long value;
    size_t n;

    colon = strrchr(addrport, ':');
    if (!colon || colon == addrport || !colon[1]) return -1;
    n = (size_t)(colon - addrport);
    if (n == 0 || n >= host_cap) return -1;
    memcpy(host, addrport, n);
    host[n] = '\0';
    value = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || value == 0 || value > 65535) return -1;
    *port = (unsigned short)value;
    return 0;
}

/**
 * Starts a registered listener. Blocking on success, never returns.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_ERROR on failure.
 */
int kc_netl_exec(kc_netl_t *ctx, const char *key) {
    char path[600], addrport[256], cmd[KC_NETL_BUF], host[256];
    unsigned short port;
    int proto;
    int n;

    if (!ctx || !key || !key[0]) return KC_NETL_ERROR;
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0) {
        return KC_NETL_ERROR;
    }
    if (kc_netl_split_addr(addrport, host, sizeof(host), &port) != 0) return KC_NETL_ERROR;

    kc_netl_set_pid(ctx, key, KC_NETL_GETPID());

    return kc_netl_serve(host, port, proto, cmd);
}

/**
 * Lists all registrations or one named registration.
 * Calls cb(key, addrport, userdata) per entry.
 * @param ctx Open context.
 * @param key Registration name, or NULL to list all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_list(kc_netl_t *ctx, const char *key, kc_netl_list_cb cb, void *userdata) {
    char path[600], addrport[256], cmd[KC_NETL_BUF];
    int proto;
    int n;

    if (!ctx) return KC_NETL_ERROR;

    if (key && key[0]) {
        n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
        if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
        if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0)
            return KC_NETL_ERROR;
        if (cb) cb(key, addrport, userdata);
        return KC_NETL_OK;
    }

#ifndef _WIN32
    {
        DIR *d;
        struct dirent *e;

        d = opendir(ctx->dir);
        if (!d) return KC_NETL_OK;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, e->d_name);
            if (n <= 0 || (size_t)n >= sizeof(path)) continue;
            if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0) continue;
            if (cb) cb(e->d_name, addrport, userdata);
        }
        closedir(d);
    }
#else
    {
        WIN32_FIND_DATAA fd_data;
        HANDLE h;
        char pattern[600];

        n = snprintf(pattern, sizeof(pattern), "%s\\*", ctx->dir);
        if (n <= 0 || (size_t)n >= sizeof(pattern)) return KC_NETL_ERROR;
        h = FindFirstFileA(pattern, &fd_data);
        if (h == INVALID_HANDLE_VALUE) return KC_NETL_OK;
        do {
            if (fd_data.cFileName[0] == '.') continue;
            n = snprintf(path, sizeof(path), "%s\\%s", ctx->dir, fd_data.cFileName);
            if (n <= 0 || (size_t)n >= sizeof(path)) continue;
            if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0) continue;
            if (cb) cb(fd_data.cFileName, addrport, userdata);
        } while (FindNextFileA(h, &fd_data));
        FindClose(h);
    }
#endif

    return KC_NETL_OK;
}

/**
 * Removes a named registration.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_delete(kc_netl_t *ctx, const char *key) {
    char path[600];
    int n;

    if (!ctx || !key || !key[0]) return KC_NETL_ERROR;
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    (void)remove(path);
    return KC_NETL_OK;
}

/**
 * Stops a registered listener by signaling its stored PID.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_stop(kc_netl_t *ctx, const char *key) {
    char path[600], addrport[256], cmd[KC_NETL_BUF];
    int proto;
    long pid = 0;
    int n;

    if (!ctx || !key || !key[0]) return KC_NETL_ERROR;
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), &pid) != 0) {
        return KC_NETL_ERROR;
    }
    if (pid > 0) {
#ifndef _WIN32
        struct timespec delay;
        int i;
        delay.tv_sec = 0;
        delay.tv_nsec = 10000000L;
        (void)kill((pid_t)pid, SIGTERM);
        for (i = 0; i < 100 && kill((pid_t)pid, 0) == 0; i++) {
            nanosleep(&delay, NULL);
        }
        if (kill((pid_t)pid, 0) == 0) (void)kill((pid_t)pid, SIGKILL);
#else
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h) {
            TerminateProcess(h, 0);
            WaitForSingleObject(h, 1000);
            CloseHandle(h);
        }
#endif
    }
    return KC_NETL_OK;
}

/**
 * Updates the stored PID for a registration.
 * @param ctx Open context.
 * @param key Registration name.
 * @param pid Process ID.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_set_pid(kc_netl_t *ctx, const char *key, long pid) {
    char path[600], addrport[256], cmd[KC_NETL_BUF], host[256];
    unsigned short port;
    int proto;
    int n;
    int fd;
    ssize_t written;
    char buf[1024];

    if (!ctx || !key || !key[0]) return KC_NETL_ERROR;
    n = snprintf(path, sizeof(path), "%s" KC_NETL_SEP "%s", ctx->dir, key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return KC_NETL_ERROR;
    if (kc_netl_read_meta(path, addrport, sizeof(addrport), &proto, cmd, sizeof(cmd), NULL) != 0) {
        return KC_NETL_ERROR;
    }
    if (kc_netl_split_addr(addrport, host, sizeof(host), &port) != 0) return KC_NETL_ERROR;

    n = snprintf(buf, sizeof(buf), "%s:%u\n%s\n%s\n%ld\n",
        host, (unsigned)port,
        proto == KC_NETL_UDP ? "udp" : "tcp",
        cmd, pid);
    if (n < 0 || (size_t)n >= sizeof(buf)) return KC_NETL_ERROR;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return KC_NETL_ERROR;
    written = write(fd, buf, (size_t)n);
    close(fd);
    if (written != n) return KC_NETL_ERROR;
    return KC_NETL_OK;
}

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_netl_options_t kc_netl_options_default(void) {
    kc_netl_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_netl_options_load_env(kc_netl_options_t *opts) {
    (void)opts;
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_netl_options_free(kc_netl_options_t *opts) {
    if (!opts) return;
}

/**
 * Request stop for a specific netl context.
 * @param ctx Context pointer.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_request_stop(kc_netl_t *ctx) {
    if (!ctx) return KC_NETL_ERROR;
    ctx->stop_requested = 1;
    return KC_NETL_OK;
}

#ifndef KC_NETL_BUILD_VERSION
#define KC_NETL_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_netl_version(void) {
    return (uint64_t)KC_NETL_BUILD_VERSION;
}
