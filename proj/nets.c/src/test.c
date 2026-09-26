/**
 * test.c - libnets public API contract tests.
 * Summary: Tests the asynchronous transfer API and grouped CLI contract.
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
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <process.h>
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define TEST_HOST "127.0.0.1"

#ifndef NETS_TEST_CLI
#define NETS_TEST_CLI ""
#endif

static const unsigned char test_response[] = {'n', 'e', 't', 's', 0, 'o', 'k'};

#ifdef _WIN32
typedef SOCKET test_socket_t;
typedef HANDLE test_thread_t;
#define TEST_BAD_SOCKET INVALID_SOCKET
#else
typedef int test_socket_t;
typedef pthread_t test_thread_t;
#define TEST_BAD_SOCKET (-1)
#endif

typedef struct {
    unsigned short port;
    int protocol;
    int hold_open;
    char received[8192];
    size_t received_size;
    int result;
    test_thread_t thread;
} test_server_t;

typedef struct {
    int done;
    int status;
    unsigned char data[128];
    size_t size;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE condition;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
#endif
} test_result_t;

static int test_case_total;
static int test_case_current;

/**
 * Return a process-specific base port.
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
 * Sleep for a bounded number of milliseconds.
 * @param ms Milliseconds.
 * @return None.
 */
static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec value;

    value.tv_sec = (time_t)(ms / 1000U);
    value.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&value, &value) != 0 && errno == EINTR) {
    }
#endif
}

/**
 * Initialize test socket support.
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
 * Release test socket support.
 * @return None.
 */
static void socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Close a test socket.
 * @param socket Socket.
 * @return None.
 */
static void socket_close(test_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Enable address reuse.
 * @param socket Socket.
 * @return None.
 */
static void socket_reuse(test_socket_t socket) {
    int enabled;

    enabled = 1;
    setsockopt(
        socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&enabled,
        sizeof(enabled)
    );
}

/**
 * Verify one integer result.
 * @param label Check label.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Failure count.
 */
static int expect_int(const char *label, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verify one condition.
 * @param label Check label.
 * @param condition Condition.
 * @return Failure count.
 */
static int expect_true(const char *label, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", label);
        return 1;
    }
    return 0;
}

/**
 * Verify one string.
 * @param label Check label.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return Failure count.
 */
static int expect_string(
    const char *label,
    const char *expected,
    const char *actual
) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf(
            "[FAIL] %s: expected '%s', got '%s'\n",
            label,
            expected,
            actual != NULL ? actual : "NULL"
        );
        return 1;
    }
    return 0;
}

/**
 * Print one top-level test result.
 * @param fail Failure count.
 * @param name Canonical case name.
 * @param detail Case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

typedef int (*case_fn)(void);

/**
 * Run one top-level case.
 * @param rc Failure accumulator.
 * @param fn Case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Initialize one callback result.
 * @param result Result state.
 * @return 0 on success, 1 on failure.
 */
static int result_init(test_result_t *result) {
    memset(result, 0, sizeof(*result));
#ifdef _WIN32
    InitializeCriticalSection(&result->lock);
    InitializeConditionVariable(&result->condition);
    return 0;
#else
    if (pthread_mutex_init(&result->lock, NULL) != 0) return 1;
    if (pthread_cond_init(&result->condition, NULL) != 0) {
        pthread_mutex_destroy(&result->lock);
        return 1;
    }
    return 0;
#endif
}

/**
 * Destroy one callback result.
 * @param result Result state.
 * @return None.
 */
static void result_destroy(test_result_t *result) {
#ifdef _WIN32
    DeleteCriticalSection(&result->lock);
#else
    pthread_cond_destroy(&result->condition);
    pthread_mutex_destroy(&result->lock);
#endif
}

/**
 * Store one terminal callback result.
 * @param status Status.
 * @param data Borrowed bytes.
 * @param size Byte count.
 * @param userdata Result state.
 * @return None.
 */
static void result_handler(
    int status,
    const void *data,
    size_t size,
    void *userdata
) {
    test_result_t *result;
    size_t copy_size;

    result = (test_result_t *)userdata;
    copy_size = size < sizeof(result->data) ? size : sizeof(result->data);

#ifdef _WIN32
    EnterCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
#endif
    result->status = status;
    result->size = size;
    if (data != NULL && copy_size > 0U) {
        memcpy(result->data, data, copy_size);
    }
    result->done = 1;
#ifdef _WIN32
    WakeConditionVariable(&result->condition);
    LeaveCriticalSection(&result->lock);
#else
    pthread_cond_signal(&result->condition);
    pthread_mutex_unlock(&result->lock);
#endif
}

/**
 * Wait for one terminal callback.
 * @param result Result state.
 * @return Status.
 */
static int result_wait(test_result_t *result) {
    int status;

#ifdef _WIN32
    EnterCriticalSection(&result->lock);
    while (!result->done) {
        SleepConditionVariableCS(&result->condition, &result->lock, INFINITE);
    }
    status = result->status;
    LeaveCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
    while (!result->done) {
        pthread_cond_wait(&result->condition, &result->lock);
    }
    status = result->status;
    pthread_mutex_unlock(&result->lock);
#endif
    return status;
}

/**
 * Run one local TCP or UDP test server.
 * @param userdata Test server state.
 * @return Platform thread return value.
 */
#ifdef _WIN32
static DWORD WINAPI server_main(void *userdata)
#else
static void *server_main(void *userdata)
#endif
{
    test_server_t *server;
    test_socket_t listener;
    struct sockaddr_in address;
    int count;

    server = (test_server_t *)userdata;
    listener = socket(
        AF_INET,
        server->protocol == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM,
        0
    );
    if (listener == TEST_BAD_SOCKET) {
        server->result = 1;
        goto done;
    }

    socket_reuse(listener);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(server->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            listener,
            (const struct sockaddr *)&address,
            sizeof(address)
        ) != 0) {
        socket_close(listener);
        server->result = 1;
        goto done;
    }

    if (server->protocol == KC_NETS_UDP) {
        count = (int)recv(
            listener,
            server->received,
            (int)sizeof(server->received),
            0
        );
        if (count > 0) server->received_size = (size_t)count;
        server->result = count > 0 ? 0 : 1;
        socket_close(listener);
        goto done;
    }

    if (listen(listener, 1) != 0) {
        socket_close(listener);
        server->result = 1;
        goto done;
    }

    {
        test_socket_t client;

        client = accept(listener, NULL, NULL);
        if (client == TEST_BAD_SOCKET) {
            socket_close(listener);
            server->result = 1;
            goto done;
        }

        count = (int)recv(
            client,
            server->received,
            (int)sizeof(server->received),
            0
        );
        if (count > 0) server->received_size = (size_t)count;

        if (server->hold_open) {
            sleep_ms(3000U);
        } else if (count > 0) {
            count = (int)send(
                client,
                (const char *)test_response,
                (int)sizeof(test_response),
                0
            );
        }

        server->result = server->received_size > 0U ? 0 : 1;
        socket_close(client);
    }

    socket_close(listener);

done:
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Start one local test server.
 * @param server Server state.
 * @param protocol Protocol.
 * @param port Port.
 * @param hold_open Whether to hold TCP open.
 * @return 0 on success, 1 on failure.
 */
static int server_start(
    test_server_t *server,
    int protocol,
    unsigned short port,
    int hold_open
) {
    memset(server, 0, sizeof(*server));
    server->protocol = protocol;
    server->port = port;
    server->hold_open = hold_open;
    server->result = 1;

#ifdef _WIN32
    server->thread = CreateThread(NULL, 0, server_main, server, 0, NULL);
    if (server->thread == NULL) return 1;
#else
    if (pthread_create(&server->thread, NULL, server_main, server) != 0) return 1;
#endif
    sleep_ms(150U);
    return 0;
}

/**
 * Join one local server.
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
    return server->result;
}

/**
 * Test kc_nets_send.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_send(void) {
    const char *name = "kc_nets_send";
    const char *detail = "launches async TCP and UDP transfers with terminal callbacks";
    kc_nets_t *transfer;
    test_result_t result;
    test_server_t server;
    unsigned short tcp_port;
    unsigned short udp_port;
    int fail;

    transfer = NULL;
    tcp_port = (unsigned short)(port_base() + 1U);
    udp_port = (unsigned short)(port_base() + 2U);
    fail = 0;

    fail += expect_int(
        "send NULL out",
        KC_NETS_EINVAL,
        kc_nets_send(NULL, TEST_HOST, 9, KC_NETS_TCP, "x", 1, result_handler, NULL)
    );
    fail += expect_int(
        "send NULL host",
        KC_NETS_EINVAL,
        kc_nets_send(&transfer, NULL, 9, KC_NETS_TCP, "x", 1, result_handler, NULL)
    );
    fail += expect_int(
        "send NULL data",
        KC_NETS_EINVAL,
        kc_nets_send(&transfer, TEST_HOST, 9, KC_NETS_TCP, NULL, 1, result_handler, NULL)
    );
    fail += expect_int(
        "send NULL handler",
        KC_NETS_EINVAL,
        kc_nets_send(&transfer, TEST_HOST, 9, KC_NETS_TCP, "x", 1, NULL, NULL)
    );
    fail += expect_int(
        "send invalid protocol",
        KC_NETS_EINVAL,
        kc_nets_send(&transfer, TEST_HOST, 9, 999, "x", 1, result_handler, NULL)
    );

    if (socket_start() != 0 || result_init(&result) != 0) {
        case_result(1, name, detail);
        return 1;
    }

    if (server_start(&server, KC_NETS_TCP, tcp_port, 0) != 0) {
        fail++;
    } else {
        transfer = NULL;
        fail += expect_int(
            "launch TCP",
            KC_NETS_OK,
            kc_nets_send(
                &transfer,
                TEST_HOST,
                tcp_port,
                KC_NETS_TCP,
                "hello tcp",
                9,
                result_handler,
                &result
            )
        );
        if (transfer != NULL) {
            fail += expect_int("TCP callback status", KC_NETS_OK, result_wait(&result));
            fail += expect_true("TCP response size", result.size == sizeof(test_response));
            if (result.size == sizeof(test_response)) {
                fail += expect_true(
                    "TCP response bytes",
                    memcmp(result.data, test_response, sizeof(test_response)) == 0
                );
            }
            kc_nets_close(transfer);
        }
        fail += expect_int("TCP server", 0, server_join(&server));
        fail += expect_true("TCP payload", server.received_size == 9U);
    }

    result_destroy(&result);
    if (result_init(&result) != 0) fail++;
    if (server_start(&server, KC_NETS_UDP, udp_port, 0) != 0) {
        fail++;
    } else {
        transfer = NULL;
        fail += expect_int(
            "launch UDP",
            KC_NETS_OK,
            kc_nets_send(
                &transfer,
                TEST_HOST,
                udp_port,
                KC_NETS_UDP,
                "hello udp",
                9,
                result_handler,
                &result
            )
        );
        if (transfer != NULL) {
            fail += expect_int("UDP callback status", KC_NETS_OK, result_wait(&result));
            fail += expect_true("UDP response empty", result.size == 0U);
            kc_nets_close(transfer);
        }
        fail += expect_int("UDP server", 0, server_join(&server));
        fail += expect_true("UDP payload", server.received_size == 9U);
    }

    result_destroy(&result);
    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_nets_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_stop(void) {
    const char *name = "kc_nets_stop";
    const char *detail = "interrupts an active transfer and reports KC_NETS_ESTOP";
    kc_nets_t *transfer;
    test_result_t result;
    test_server_t server;
    unsigned short port;
    int fail;

    transfer = NULL;
    port = (unsigned short)(port_base() + 3U);
    fail = 0;

    fail += expect_int("stop NULL", KC_NETS_EINVAL, kc_nets_stop(NULL));
    if (socket_start() != 0 || result_init(&result) != 0) {
        case_result(1, name, detail);
        return 1;
    }

    if (server_start(&server, KC_NETS_TCP, port, 1) != 0) {
        fail++;
    } else {
        fail += expect_int(
            "launch stoppable transfer",
            KC_NETS_OK,
            kc_nets_send(
                &transfer,
                TEST_HOST,
                port,
                KC_NETS_TCP,
                "stop",
                4,
                result_handler,
                &result
            )
        );
        sleep_ms(250U);
        if (transfer != NULL) {
            fail += expect_int("stop transfer", KC_NETS_OK, kc_nets_stop(transfer));
            fail += expect_int("stop repeated", KC_NETS_OK, kc_nets_stop(transfer));
            fail += expect_int(
                "stopped callback status",
                KC_NETS_ESTOP,
                result_wait(&result)
            );
            kc_nets_close(transfer);
        }
        fail += expect_int("stop server", 0, server_join(&server));
    }

    result_destroy(&result);
    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_nets_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_close(void) {
    const char *name = "kc_nets_close";
    const char *detail = "accepts NULL and releases a completed transfer";
    kc_nets_t *transfer;
    test_result_t result;
    test_server_t server;
    unsigned short port;
    int fail;

    transfer = NULL;
    port = (unsigned short)(port_base() + 4U);
    fail = 0;
    kc_nets_close(NULL);

    if (socket_start() != 0 || result_init(&result) != 0) {
        case_result(1, name, detail);
        return 1;
    }

    if (server_start(&server, KC_NETS_UDP, port, 0) != 0) {
        fail++;
    } else {
        fail += expect_int(
            "launch before close",
            KC_NETS_OK,
            kc_nets_send(
                &transfer,
                TEST_HOST,
                port,
                KC_NETS_UDP,
                "close",
                5,
                result_handler,
                &result
            )
        );
        if (transfer != NULL) {
            fail += expect_int("complete before close", KC_NETS_OK, result_wait(&result));
            kc_nets_close(transfer);
        }
        fail += expect_int("close server", 0, server_join(&server));
    }

    result_destroy(&result);
    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_nets_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_strerror(void) {
    const char *name = "kc_nets_strerror";
    const char *detail = "maps public status codes to static messages";
    int fail;

    fail = 0;
    fail += expect_string("OK", "ok", kc_nets_strerror(KC_NETS_OK));
    fail += expect_string("EINVAL", "invalid argument", kc_nets_strerror(KC_NETS_EINVAL));
    fail += expect_string("ENET", "network error", kc_nets_strerror(KC_NETS_ENET));
    fail += expect_string("ESTOP", "operation stopped", kc_nets_strerror(KC_NETS_ESTOP));
    fail += expect_string("unknown", "unknown error", kc_nets_strerror(999));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_nets_tls_available.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_tls_available(void) {
    const char *name = "kc_nets_tls_available";
    const char *detail = "reports a stable boolean TLS capability";
    int first;
    int second;
    int fail;

    first = kc_nets_tls_available();
    second = kc_nets_tls_available();
    fail = 0;
    fail += expect_true("TLS availability boolean", first == 0 || first == 1);
    fail += expect_int("TLS availability stable", first, second);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test kc_nets_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_version(void) {
    const char *name = "kc_nets_version";
    const char *detail = "returns a non-zero generated build version";
    int fail;

    fail = expect_true("version non-zero", kc_nets_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test the grouped CLI contract.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_nets_cli(void) {
    const char *name = "kc_nets_cli";
    const char *detail = "covers help, version, errors, TCP, UDP, and URL-shaped targets";
    test_server_t server;
    unsigned short tcp_port;
    unsigned short udp_port;
    unsigned short url_port;
    char command[2048];
    int fail;
    int rc;

    fail = 0;
    tcp_port = (unsigned short)(port_base() + 10U);
    udp_port = (unsigned short)(port_base() + 11U);
    url_port = (unsigned short)(port_base() + 12U);

    if (NETS_TEST_CLI[0] == '\0') {
        case_result(1, name, detail);
        return 1;
    }

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --help > NUL 2>&1", NETS_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --help > /dev/null 2>&1", NETS_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI help succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --version > NUL 2>&1", NETS_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --version > /dev/null 2>&1", NETS_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI version succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --bad > NUL 2>&1", NETS_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --bad > /dev/null 2>&1", NETS_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI invalid option fails", rc != 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" > NUL 2>&1", NETS_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" > /dev/null 2>&1", NETS_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI missing target fails", rc != 0);

    if (socket_start() != 0) {
        case_result(1, name, detail);
        return 1;
    }

    if (server_start(&server, KC_NETS_TCP, tcp_port, 0) != 0) {
        fail++;
    } else {
#ifdef _WIN32
        snprintf(command, sizeof(command),
            "echo cli|\"%s\" 127.0.0.1:%u > NUL 2>&1",
            NETS_TEST_CLI, (unsigned int)tcp_port);
#else
        snprintf(command, sizeof(command),
            "printf cli | \"%s\" 127.0.0.1:%u > /dev/null 2>&1",
            NETS_TEST_CLI, (unsigned int)tcp_port);
#endif
        rc = system(command);
        fail += expect_true("CLI TCP succeeds", rc == 0);
        fail += expect_int("CLI TCP server", 0, server_join(&server));
        fail += expect_true("CLI TCP sends stdin", server.received_size > 0U);
    }

    if (server_start(&server, KC_NETS_UDP, udp_port, 0) != 0) {
        fail++;
    } else {
#ifdef _WIN32
        snprintf(command, sizeof(command),
            "echo cli|\"%s\" 127.0.0.1:%u --udp > NUL 2>&1",
            NETS_TEST_CLI, (unsigned int)udp_port);
#else
        snprintf(command, sizeof(command),
            "printf cli | \"%s\" 127.0.0.1:%u --udp > /dev/null 2>&1",
            NETS_TEST_CLI, (unsigned int)udp_port);
#endif
        rc = system(command);
        fail += expect_true("CLI UDP succeeds", rc == 0);
        fail += expect_int("CLI UDP server", 0, server_join(&server));
        fail += expect_true("CLI UDP sends stdin", server.received_size > 0U);
    }

    if (server_start(&server, KC_NETS_TCP, url_port, 0) != 0) {
        fail++;
    } else {
#ifdef _WIN32
        snprintf(command, sizeof(command),
            "echo cli|\"%s\" tcp://127.0.0.1:%u > NUL 2>&1",
            NETS_TEST_CLI, (unsigned int)url_port);
#else
        snprintf(command, sizeof(command),
            "printf cli | \"%s\" tcp://127.0.0.1:%u > /dev/null 2>&1",
            NETS_TEST_CLI, (unsigned int)url_port);
#endif
        rc = system(command);
        fail += expect_true("CLI URL-shaped TCP succeeds", rc == 0);
        fail += expect_int("CLI URL-shaped server", 0, server_join(&server));
    }

    socket_stop();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Run all test cases.
 * @return Failure count.
 */
static int case_all(void) {
    int rc;

    rc = 0;
    test_case_total = 7;
    test_case_current = 0;
    run_case(&rc, case_kc_nets_send);
    run_case(&rc, case_kc_nets_stop);
    run_case(&rc, case_kc_nets_close);
    run_case(&rc, case_kc_nets_strerror);
    run_case(&rc, case_kc_nets_tls_available);
    run_case(&rc, case_kc_nets_version);
    run_case(&rc, case_kc_nets_cli);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test executable entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_nets_send") == 0) return case_kc_nets_send();
    if (strcmp(argv[1], "kc_nets_stop") == 0) return case_kc_nets_stop();
    if (strcmp(argv[1], "kc_nets_close") == 0) return case_kc_nets_close();
    if (strcmp(argv[1], "kc_nets_strerror") == 0) return case_kc_nets_strerror();
    if (strcmp(argv[1], "kc_nets_tls_available") == 0) return case_kc_nets_tls_available();
    if (strcmp(argv[1], "kc_nets_version") == 0) return case_kc_nets_version();
    if (strcmp(argv[1], "kc_nets_cli") == 0) return case_kc_nets_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
