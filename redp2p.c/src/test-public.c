/**
 * redp2p.c - normalized public capability contract tests.
 * Summary: Exercises the idx/pub/con API and one grouped CLI contract case.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET test_fd_t;
typedef HANDLE test_thread_t;
typedef int test_socklen_t;
#define TEST_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
typedef int test_fd_t;
typedef pthread_t test_thread_t;
typedef socklen_t test_socklen_t;
#define TEST_INVALID (-1)
#endif

#ifndef REDP2P_TEST_CLI
#define REDP2P_TEST_CLI ""
#endif

static int test_current;
static int test_total;
static int test_failed;

static void result(int failed, const char *name, const char *detail)
{
    test_current++;
    printf("[%d/%d] [%s] %s: %s\n", test_current, test_total,
        failed ? "FAIL" : "PASS", name, detail);
    if (failed) test_failed++;
}

static void sleep_ms(unsigned int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static uint64_t monotonic_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U +
        (uint64_t)(ts.tv_nsec / 1000000L);
#endif
}

static void fd_close(test_fd_t fd)
{
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

static int sockets_start(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    return 0;
#endif
}

static void sockets_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static int local_unicast_ipv4(char out[INET_ADDRSTRLEN])
{
    test_fd_t fd;
    struct sockaddr_in target;
    struct sockaddr_in local;
    test_socklen_t len;

    if (!out) return 1;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == TEST_INVALID) return 1;

    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    if (inet_pton(AF_INET, "8.8.8.8", &target.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&target, sizeof(target)) != 0)
    {
        fd_close(fd);
        return 1;
    }

    len = sizeof(local);
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &len) != 0 ||
        local.sin_addr.s_addr == htonl(0x7f000001UL) ||
        !inet_ntop(AF_INET, &local.sin_addr, out, INET_ADDRSTRLEN))
    {
        fd_close(fd);
        return 1;
    }

    fd_close(fd);
    return 0;
}

static uint16_t reserve_port(void)
{
    test_fd_t fd;
    struct sockaddr_in addr;
    test_socklen_t len;
    uint16_t port = 0;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == TEST_INVALID) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        len = sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &len) == 0)
            port = ntohs(addr.sin_port);
    }
    fd_close(fd);
    return port;
}

typedef struct {
    test_fd_t listener;
    uint16_t port;
    int failed;
    test_thread_t thread;
} echo_t;

#ifdef _WIN32
static DWORD WINAPI echo_main(LPVOID arg)
#else
static void *echo_main(void *arg)
#endif
{
    echo_t *echo = (echo_t *)arg;
    test_fd_t client;
    unsigned char buf[4096];
    int n;

    client = accept(echo->listener, NULL, NULL);
    if (client == TEST_INVALID) {
        echo->failed = 1;
    } else {
        n = (int)recv(client, (char *)buf, sizeof(buf), 0);
        if (n <= 0 || send(client, (const char *)buf, n, 0) != n)
            echo->failed = 1;
        fd_close(client);
    }
    fd_close(echo->listener);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int echo_start(echo_t *echo)
{
    struct sockaddr_in addr;
    test_socklen_t len;
    int one = 1;

    memset(echo, 0, sizeof(*echo));
    echo->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (echo->listener == TEST_INVALID) return 1;
    setsockopt(echo->listener, SOL_SOCKET, SO_REUSEADDR,
        (const char *)&one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    addr.sin_port = 0;
    if (bind(echo->listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(echo->listener, 4) != 0)
    {
        fd_close(echo->listener);
        return 1;
    }
    len = sizeof(addr);
    if (getsockname(echo->listener, (struct sockaddr *)&addr, &len) != 0) {
        fd_close(echo->listener);
        return 1;
    }
    echo->port = ntohs(addr.sin_port);
#ifdef _WIN32
    echo->thread = CreateThread(NULL, 0, echo_main, echo, 0, NULL);
    return echo->thread ? 0 : 1;
#else
    return pthread_create(&echo->thread, NULL, echo_main, echo);
#endif
}

static void echo_join(echo_t *echo)
{
#ifdef _WIN32
    WaitForSingleObject(echo->thread, INFINITE);
    CloseHandle(echo->thread);
#else
    pthread_join(echo->thread, NULL);
#endif
}

static int tcp_roundtrip(uint16_t port)
{
    test_fd_t fd;
    struct sockaddr_in addr;
    const char payload[] = "redp2p-public-api";
    char reply[sizeof(payload)];
    size_t used = 0;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == TEST_INVALID) return 1;
#ifdef _WIN32
    {
        DWORD timeout = 200;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
            sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout,
            sizeof(timeout));
    }
#else
    {
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fd_close(fd);
        return 1;
    }
    if (send(fd, payload, (int)sizeof(payload), 0) != (int)sizeof(payload)) {
        fd_close(fd);
        return 1;
    }
    while (used < sizeof(reply)) {
        int n = (int)recv(fd, reply + used, sizeof(reply) - used, 0);
        if (n <= 0) {
            fd_close(fd);
            return 1;
        }
        used += (size_t)n;
    }
    fd_close(fd);
    return memcmp(payload, reply, sizeof(payload)) == 0 ? 0 : 1;
}

static int case_kc_redp2p_api(void)
{
    kc_redp2p_idx_t *idx = NULL;
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_con_t *con = NULL;
    kc_redp2p_idx_entry_t *entries = NULL;
    kc_redp2p_idx_options_t idx_options;
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    echo_t echo;
    char index[320];
    char local_ip[INET_ADDRSTRLEN];
    uint16_t idx_port;
    uint16_t con_port;
    size_t count = 0;
    int failed = 0;
    int status;

    idx_port = reserve_port();
    con_port = reserve_port();
    if (local_unicast_ipv4(local_ip) != 0 || !idx_port || !con_port ||
        echo_start(&echo) != 0)
        failed = 1;

    memset(&idx_options, 0, sizeof(idx_options));
    idx_options.host = local_ip;
    idx_options.port = idx_port;
    idx_options.pow = 0;
    idx_options.max_consumers = 32;

    if (!failed) {
        status = kc_redp2p_idx(&idx, &idx_options);
        if (status != KC_REDP2P_OK || !idx) failed = 1;
    }
    if (!failed) {
        status = kc_redp2p_idx_list(idx, &entries, &count);
        if (status != KC_REDP2P_OK || count != 0 || entries != NULL) {
            fprintf(stderr,
                "kc_redp2p_api: initial idx_list failed (status=%d count=%zu entries=%p)\n",
                status, count, (void *)entries);
            failed = 1;
        }
    }

    snprintf(index, sizeof(index), "%s:%u", local_ip, (unsigned)idx_port);
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "echo";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.port = echo.port;
    if (!failed) {
        status = kc_redp2p_pub(&pub, &pub_options);
        if (status != KC_REDP2P_OK || !pub) failed = 1;
    }

    if (!failed) {
        status = kc_redp2p_idx_list(idx, &entries, &count);
        if (status != KC_REDP2P_OK || count != 1 ||
            !entries || strcmp(entries[0].id, "echo") != 0) {
            fprintf(stderr,
                "kc_redp2p_api: published idx_list failed (status=%d count=%zu id=%s)\n",
                status, count, entries ? entries[0].id : "<null>");
            failed = 1;
        }
        kc_redp2p_free(entries);
        entries = NULL;
        count = 0;
    }

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "echo";
    con_options.index = index;
    con_options.port = con_port;
    if (!failed) {
        status = kc_redp2p_con(&con, &con_options);
        if (status != KC_REDP2P_OK || !con) {
            fprintf(stderr,
                "kc_redp2p_api: con create failed (status=%d)\n", status);
            failed = 1;
        }
    }
    if (!failed) {
        uint64_t deadline = monotonic_ms() + 5000U;
        for (;;) {
            if (tcp_roundtrip(con_port) == 0) break;
            if (monotonic_ms() >= deadline) {
                fprintf(stderr,
                    "kc_redp2p_api: tcp roundtrip timed out\n");
                failed = 1;
                break;
            }
            sleep_ms(50);
        }
    }

    kc_redp2p_con_close(con);
    kc_redp2p_pub_close(pub);
    kc_redp2p_idx_close(idx);
    if (!failed) echo_join(&echo);

    if (strcmp(kc_redp2p_strerror(KC_REDP2P_OK), "OK") != 0) failed = 1;
    kc_redp2p_free(NULL);
    kc_redp2p_con_close(NULL);
    kc_redp2p_pub_close(NULL);
    kc_redp2p_idx_close(NULL);

    result(failed, "kc_redp2p_api",
        "idx/pub/con create a tunnel without exposing coordination plumbing");
    return failed;
}

static int run_cli(char *const argv[])
{
    if (!REDP2P_TEST_CLI[0]) return 0;
#ifdef _WIN32
    return (int)_spawnv(_P_WAIT, REDP2P_TEST_CLI, (const char * const *)argv);
#else
    pid_t pid = fork();
    int status;
    if (pid == 0) {
        execv(REDP2P_TEST_CLI, argv);
        _exit(127);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return 255;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
#endif
}

static int case_kc_redp2p_cli(void)
{
    int failed = 0;
    char *help[] = { (char *)REDP2P_TEST_CLI, (char *)"--help", NULL };
    char *version[] = { (char *)REDP2P_TEST_CLI, (char *)"--version", NULL };
    char *old_con[] = {
        (char *)REDP2P_TEST_CLI,
        (char *)"con",
        (char *)"x@127.0.0.1:1",
        (char *)"--tcp",
        (char *)"9000",
        NULL
    };

    if (REDP2P_TEST_CLI[0]) {
        if (run_cli(help) != 0) failed = 1;
        if (run_cli(version) != 0) failed = 1;
        if (run_cli(old_con) == 0) failed = 1;
    }
    result(failed, "kc_redp2p_cli",
        "CLI exposes idx/pub/con and rejects consumer protocol plumbing");
    return failed;
}

static int case_all(void)
{
    test_current = 0;
    test_total = 2;
    test_failed = 0;
    case_kc_redp2p_api();
    case_kc_redp2p_cli();
    printf("\n%d passed, %d failed\n", test_total - test_failed, test_failed);
    return test_failed;
}

int main(int argc, char **argv)
{
    int rc;

    if (argc != 2) {
        fprintf(stderr, "expected one test case argument\n");
        return 2;
    }
    if (sockets_start() != 0) return 1;
    if (strcmp(argv[1], "all") == 0) rc = case_all();
    else if (strcmp(argv[1], "kc_redp2p_api") == 0) {
        test_current = 0; test_total = 1; test_failed = 0;
        rc = case_kc_redp2p_api();
    } else if (strcmp(argv[1], "kc_redp2p_cli") == 0) {
        test_current = 0; test_total = 1; test_failed = 0;
        rc = case_kc_redp2p_cli();
    } else {
        fprintf(stderr, "unknown test case: %s\n", argv[1]);
        rc = 2;
    }
    sockets_stop();
    return rc;
}
