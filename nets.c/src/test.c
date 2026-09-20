/**
 * test.c - libnets public API contract tests.
 * Summary: Tests each exported nets function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnets.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define TEST_HOST "127.0.0.1"

#ifndef _WIN32
static const unsigned char test_response[] = {'n', 'e', 't', 's', 0, 'o', 'k'};
#endif

#ifdef _WIN32
typedef SOCKET test_socket_t;
typedef HANDLE test_thread_t;
#define TEST_BAD_SOCKET INVALID_SOCKET
#else
typedef int test_socket_t;
typedef pthread_t test_thread_t;
#define TEST_BAD_SOCKET -1
#endif

typedef struct {
    unsigned short port;
    int proto;
    char received[8192];
    size_t received_size;
    int result;
    test_thread_t thread;
} test_server_t;

/**
 * Returns a process-specific base port.
 * @return Port base.
 */
static unsigned short port_base(void) {
#ifdef _WIN32
    return (unsigned short)(25000UL + ((unsigned long)_getpid() % 20000UL));
#else
    return (unsigned short)(25000UL + ((unsigned long)getpid() % 20000UL));
#endif
}

/**
 * Sleeps for a bounded number of milliseconds.
 * @param ms Milliseconds to sleep.
 * @return None.
 */
static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
#endif
}

/**
 * Initializes process socket state for test-owned sockets.
 * @return 0 on success, 1 on failure.
 */
static int socket_start(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

/**
 * Stops process socket state for test-owned sockets.
 * @return 0 on success.
 */
static int socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

/**
 * Closes one socket.
 * @param fd Socket descriptor.
 * @return 0 on success.
 */
static int socket_close(test_socket_t fd) {
#ifdef _WIN32
    return closesocket(fd) == 0 ? 0 : 1;
#else
    return close(fd) == 0 ? 0 : 1;
#endif
}

/**
 * Sets a receive timeout on one socket.
 * @param fd Socket descriptor.
 * @param ms Timeout in milliseconds.
 * @return None.
 */
static void socket_timeout(test_socket_t fd, unsigned int ms) {
#ifdef _WIN32
    DWORD tv;

    tv = (DWORD)ms;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;

    tv.tv_sec = (time_t)(ms / 1000U);
    tv.tv_usec = (suseconds_t)(ms % 1000U) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif
}

/**
 * Enables local socket reuse.
 * @param fd Socket descriptor.
 * @return None.
 */
static void socket_reuse(test_socket_t fd) {
    int one;

    one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test behavior detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

#ifdef _WIN32
/**
 * Runs one TCP or UDP receiver thread.
 * @param arg Server state pointer.
 * @return Thread return value.
 */
static DWORD WINAPI server_main(void *arg)
#else
/**
 * Runs one TCP or UDP receiver thread.
 * @param arg Server state pointer.
 * @return Thread return value.
 */
static void *server_main(void *arg)
#endif
{
    test_server_t *server;
    test_socket_t fd;
    struct sockaddr_in addr;
    int n;

    server = (test_server_t *)arg;
    fd = socket(AF_INET, server->proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd == TEST_BAD_SOCKET) {
        server->result = 1;
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    socket_reuse(fd);
    socket_timeout(fd, 5000U);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server->port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        socket_close(fd);
        server->result = 1;
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    if (server->proto == KC_NETS_TCP) {
        test_socket_t client;

        if (listen(fd, 1) != 0) {
            socket_close(fd);
            server->result = 1;
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }
        client = accept(fd, NULL, NULL);
        if (client == TEST_BAD_SOCKET) {
            socket_close(fd);
            server->result = 1;
#ifdef _WIN32
            return 0;
#else
            return NULL;
#endif
        }
        socket_timeout(client, 5000U);
        n = (int)recv(client, server->received, (int)sizeof(server->received), 0);
        if (n > 0) server->received_size = (size_t)n;
        if (server->received_size > 0) {
#ifdef _WIN32
            server->result = 0;
#else
            n = (int)send(client, (const char *)test_response,
                (int)sizeof(test_response), 0);
            server->result = n == (int)sizeof(test_response) ? 0 : 1;
#endif
        }
        socket_close(client);
    } else {
        n = (int)recv(fd, server->received, (int)sizeof(server->received), 0);
        if (n > 0) server->received_size = (size_t)n;
    }
    socket_close(fd);
    if (server->received_size == 0) server->result = 1;
    else if (server->proto == KC_NETS_UDP) server->result = 0;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Starts one test server thread.
 * @param server Server state.
 * @param proto Protocol.
 * @param port Listen port.
 * @return 0 on success, 1 on failure.
 */
static int server_start(test_server_t *server, int proto, unsigned short port) {
    memset(server, 0, sizeof(*server));
    server->proto = proto;
    server->port = port;
    server->result = 1;
#ifdef _WIN32
    server->thread = CreateThread(NULL, 0, server_main, server, 0, NULL);
    if (server->thread == NULL) return 1;
#else
    if (pthread_create(&server->thread, NULL, server_main, server) != 0) return 1;
#endif
    sleep_ms(200U);
    return 0;
}

/**
 * Joins one test server thread.
 * @param server Server state.
 * @return 0 on success, 1 on failure.
 */
static int server_join(test_server_t *server) {
#ifdef _WIN32
    if (WaitForSingleObject(server->thread, 10000U) != WAIT_OBJECT_0) return 1;
    CloseHandle(server->thread);
#else
    if (pthread_join(server->thread, NULL) != 0) return 1;
#endif
    return server->result == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_open(void) {
    const char *name = "kc_nets_open";
    const char *detail = "validates and allocates context";
    kc_nets_t *ctx;
    int fail;

    ctx = NULL;
    fail = 0;
    fail += expect_int("open NULL out", KC_NETS_EINVAL, kc_nets_open(NULL));
    fail += expect_int("open valid context", KC_NETS_OK, kc_nets_open(&ctx));
    fail += expect_true("open sets context", ctx != NULL);
    kc_nets_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_close(void) {
    const char *name = "kc_nets_close";
    const char *detail = "releases a context";
    kc_nets_t *ctx;
    int fail;

    ctx = NULL;
    fail = 0;
    fail += expect_int("open before close", KC_NETS_OK, kc_nets_open(&ctx));
    kc_nets_close(NULL);
    kc_nets_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_stop(void) {
    const char *name = "kc_nets_stop";
    const char *detail = "is idempotent on a context";
    kc_nets_t *ctx;
    int fail;

    ctx = NULL;
    fail = 0;
    fail += expect_int("stop NULL", KC_NETS_EINVAL, kc_nets_stop(NULL));
    fail += expect_int("open before stop", KC_NETS_OK, kc_nets_open(&ctx));
    fail += expect_int("stop context", KC_NETS_OK, kc_nets_stop(ctx));
    fail += expect_int("stop context repeated", KC_NETS_OK, kc_nets_stop(ctx));
    kc_nets_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_stop_requested.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_stop_requested(void) {
    const char *name = "kc_nets_stop_requested";
    const char *detail = "tracks stop state on a context";
    kc_nets_t *ctx;
    int fail;

    ctx = NULL;
    fail = 0;
    fail += expect_int("stop_requested NULL is 0", 0, kc_nets_stop_requested(NULL));
    fail += expect_int("open before stop", KC_NETS_OK, kc_nets_open(&ctx));
    fail += expect_int("fresh context not stopped", 0, kc_nets_stop_requested(ctx));
    fail += expect_int("stop context", KC_NETS_OK, kc_nets_stop(ctx));
    fail += expect_int("stopped context reported", 1, kc_nets_stop_requested(ctx));
    kc_nets_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_send.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_send(void) {
    const char *name = "kc_nets_send";
    const char *detail = "delivers tcp and udp payloads";
    kc_nets_t *ctx;
    kc_nets_t *stopped;
    test_server_t server;
    unsigned short tcp_port;
    unsigned short udp_port;
    void *resp = NULL;
    size_t resp_size = 0;
    int fail;

    ctx = NULL;
    stopped = NULL;
    tcp_port = (unsigned short)(port_base() + 1U);
    udp_port = (unsigned short)(port_base() + 20U);
    fail = 0;

    if (socket_start() != 0) {
        case_result(1, name, detail);
        return 1;
    }

    fail += expect_int("send NULL ctx", KC_NETS_EINVAL,
        kc_nets_send(NULL, TEST_HOST, 9, KC_NETS_TCP, "x", 1, NULL, NULL));
    fail += expect_int("open for send", KC_NETS_OK, kc_nets_open(&ctx));
    fail += expect_int("send NULL host", KC_NETS_EINVAL,
        kc_nets_send(ctx, NULL, 9, KC_NETS_TCP, "x", 1, NULL, NULL));
    fail += expect_int("send empty host", KC_NETS_EINVAL,
        kc_nets_send(ctx, "", 9, KC_NETS_TCP, "x", 1, NULL, NULL));
    fail += expect_int("send NULL data", KC_NETS_EINVAL,
        kc_nets_send(ctx, TEST_HOST, 9, KC_NETS_TCP, NULL, 1, NULL, NULL));
    fail += expect_int("send invalid proto", KC_NETS_EINVAL,
        kc_nets_send(ctx, TEST_HOST, 9, 999, "x", 1, NULL, NULL));
    fail += expect_int("send unresolvable host", KC_NETS_ENET,
        kc_nets_send(ctx, "invalid.invalid", 9, KC_NETS_TCP, "x", 1, NULL, NULL));
    fail += expect_int("open stopped context", KC_NETS_OK,
        kc_nets_open(&stopped));
    fail += expect_int("stop context", KC_NETS_OK, kc_nets_stop(stopped));
    fail += expect_int("send stopped context", KC_NETS_ESTOP,
        kc_nets_send(stopped, TEST_HOST, 9, KC_NETS_TCP, "x", 1, NULL, NULL));
    kc_nets_close(stopped);

    if (server_start(&server, KC_NETS_TCP, tcp_port) != 0) {
        fail += 1;
    } else {
        resp = NULL;
        resp_size = 0;
        fail += expect_int("tcp send", KC_NETS_OK,
            kc_nets_send(ctx, TEST_HOST, tcp_port, KC_NETS_TCP, "hello tcp", 9, &resp, &resp_size));
        fail += expect_int("tcp server join", 0, server_join(&server));
        fail += expect_true("tcp payload size", server.received_size == 9U);
        fail += expect_true("tcp payload bytes",
            memcmp(server.received, "hello tcp", 9) == 0);
#ifdef _WIN32
        fail += expect_true("tcp response empty", resp == NULL && resp_size == 0U);
#else
        fail += expect_true("tcp response allocated", resp != NULL);
        fail += expect_true("tcp response size", resp_size == sizeof(test_response));
        if (resp != NULL && resp_size == sizeof(test_response)) {
            fail += expect_true("tcp response bytes",
                memcmp(resp, test_response, sizeof(test_response)) == 0);
        }
#endif
        kc_nets_free(resp);
    }

    if (server_start(&server, KC_NETS_UDP, udp_port) != 0) {
        fail += 1;
    } else {
        resp = NULL;
        resp_size = 0;
        fail += expect_int("udp send", KC_NETS_OK,
            kc_nets_send(ctx, TEST_HOST, udp_port, KC_NETS_UDP, "hello udp", 9, &resp, &resp_size));
        fail += expect_int("udp server join", 0, server_join(&server));
        fail += expect_true("udp payload size", server.received_size == 9U);
        fail += expect_true("udp payload bytes",
            memcmp(server.received, "hello udp", 9) == 0);
        fail += expect_true("udp response empty", resp == NULL && resp_size == 0U);
        kc_nets_free(resp);
    }

    kc_nets_close(ctx);
    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_free(void) {
    const char *name = "kc_nets_free";
    const char *detail = "releases returned response and accepts NULL";
    kc_nets_t *ctx;
    test_server_t server;
    unsigned short port;
    void *response;
    size_t response_size;
    int fail;

    ctx = NULL;
    port = (unsigned short)(port_base() + 2U);
    response = NULL;
    response_size = 0;
    fail = 0;

    if (socket_start() != 0) {
        case_result(1, name, detail);
        return 1;
    }

    fail += expect_int("open for free", KC_NETS_OK, kc_nets_open(&ctx));
    if (server_start(&server, KC_NETS_TCP, port) != 0) {
        fail += 1;
    } else {
        fail += expect_int("send for free", KC_NETS_OK,
            kc_nets_send(ctx, TEST_HOST, port, KC_NETS_TCP, "free", 4,
                &response, &response_size));
        fail += expect_int("free server join", 0, server_join(&server));
#ifdef _WIN32
        fail += expect_true("free response empty",
            response == NULL && response_size == 0U);
#else
        fail += expect_true("free response allocated", response != NULL);
        fail += expect_true("free response size",
            response_size == sizeof(test_response));
        if (response != NULL && response_size == sizeof(test_response)) {
            fail += expect_true("free response bytes",
                memcmp(response, test_response, sizeof(test_response)) == 0);
        }
#endif
    }
    kc_nets_free(response);
    kc_nets_free(NULL);
    kc_nets_close(ctx);
    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_strerror(void) {
    const char *name = "kc_nets_strerror";
    const char *detail = "maps codes to messages";
    int fail;

    fail = 0;
    fail += expect_string("strerror OK", "ok", kc_nets_strerror(KC_NETS_OK));
    fail += expect_string("strerror EINVAL", "invalid argument",
        kc_nets_strerror(KC_NETS_EINVAL));
    fail += expect_string("strerror ENET", "network error",
        kc_nets_strerror(KC_NETS_ENET));
    fail += expect_string("strerror ESTOP", "operation stopped",
        kc_nets_strerror(KC_NETS_ESTOP));
    fail += expect_string("strerror unknown", "unknown error",
        kc_nets_strerror(999));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_version(void) {
    const char *name = "kc_nets_version";
    const char *detail = "returns a non-zero build timestamp";
    int fail;

    fail = expect_true(name, kc_nets_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_nets_tls_available.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_tls_available(void) {
    const char *name = "kc_nets_tls_available";
    const char *detail = "reports a stable availability flag";
    int a;
    int b;
    int fail;

    a = kc_nets_tls_available();
    b = kc_nets_tls_available();
    fail = 0;
    fail += expect_true("tls availability is boolean", a == 0 || a == 1);
    fail += expect_int("tls availability is stable", a, b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 9;
    test_case_current = 0;
    run_case(&rc, case_kc_nets_open);
    run_case(&rc, case_kc_nets_close);
    run_case(&rc, case_kc_nets_stop);
    run_case(&rc, case_kc_nets_stop_requested);
    run_case(&rc, case_kc_nets_send);
    run_case(&rc, case_kc_nets_free);
    run_case(&rc, case_kc_nets_strerror);
    run_case(&rc, case_kc_nets_version);
    run_case(&rc, case_kc_nets_tls_available);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_nets_open") == 0) return case_kc_nets_open();
    if (strcmp(argv[1], "kc_nets_close") == 0) return case_kc_nets_close();
    if (strcmp(argv[1], "kc_nets_stop") == 0) return case_kc_nets_stop();
    if (strcmp(argv[1], "kc_nets_stop_requested") == 0) return case_kc_nets_stop_requested();
    if (strcmp(argv[1], "kc_nets_send") == 0) return case_kc_nets_send();
    if (strcmp(argv[1], "kc_nets_free") == 0) return case_kc_nets_free();
    if (strcmp(argv[1], "kc_nets_strerror") == 0) return case_kc_nets_strerror();
    if (strcmp(argv[1], "kc_nets_version") == 0) return case_kc_nets_version();
    if (strcmp(argv[1], "kc_nets_tls_available") == 0) return case_kc_nets_tls_available();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
