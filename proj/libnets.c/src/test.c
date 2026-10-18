/**
 * test.c - libnets public API contract tests.
 * Summary: Tests persistent outbound TCP and UDP endpoint behavior.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <winsock2.h>
#include <windows.h>
typedef SOCKET test_socket_t;
typedef HANDLE test_thread_t;
typedef CRITICAL_SECTION test_mutex_t;
typedef int test_socklen_t;
#define TEST_SOCKET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int test_socket_t;
typedef pthread_t test_thread_t;
typedef pthread_mutex_t test_mutex_t;
typedef socklen_t test_socklen_t;
#define TEST_SOCKET_INVALID (-1)
#endif

#define TEST_HOST "127.0.0.1"
#define TEST_WAIT_STEPS 200
#define TEST_WAIT_MS 25U

typedef struct {
    unsigned short port;
    int protocol;
    unsigned char received[64];
    size_t received_size;
    size_t datagram_sizes[2];
    int result;
    test_thread_t thread;
} test_server_t;

typedef struct {
    test_mutex_t mutex;
    int mutex_ready;
    int protocol;
    kc_nets_t *network;
    unsigned char received[64];
    size_t received_size;
    size_t receive_count;
    size_t datagram_sizes[2];
    int disconnect_count;
    int error_status;
    int done;
} test_state_t;

static int test_case_total;
static int test_case_current;

#ifndef __EMSCRIPTEN__
static int case_nets_cli(void);
#endif

/**
 * Returns a process-specific base port for local tests.
 * @return Local port base.
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
 * @param milliseconds Sleep duration.
 * @return void.
 */
static void sleep_ms(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec value;

    value.tv_sec = (time_t)(milliseconds / 1000U);
    value.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&value, &value) != 0 && errno == EINTR) {
    }
#endif
}

/**
 * Starts socket support for tests.
 * @return Zero on success, non-zero on failure.
 */
static int socket_start(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    return 0;
#endif
}

/**
 * Releases socket support for tests.
 * @return void.
 */
static void socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Closes one test socket.
 * @param socket Socket descriptor.
 * @return void.
 */
static void socket_close(test_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Receives an exact byte count from one TCP socket.
 * @param socket Socket descriptor.
 * @param data Destination bytes.
 * @param size Required byte count.
 * @return Zero on success, non-zero on failure.
 */
static int socket_receive_all(
    test_socket_t socket,
    unsigned char *data,
    size_t size
) {
    size_t used;

    used = 0U;
    while (used < size) {
        int received;

        received = (int)recv(
            socket,
            (char *)data + used,
            (int)(size - used),
            0
        );
        if (received <= 0) return 1;
        used += (size_t)received;
    }
    return 0;
}

/**
 * Sends an exact byte count through one TCP socket.
 * @param socket Socket descriptor.
 * @param data Source bytes.
 * @param size Byte count.
 * @return Zero on success, non-zero on failure.
 */
static int socket_send_all(
    test_socket_t socket,
    const unsigned char *data,
    size_t size
) {
    size_t sent_size;

    sent_size = 0U;
    while (sent_size < size) {
        int sent;

        sent = (int)send(
            socket,
            (const char *)data + sent_size,
            (int)(size - sent_size),
            0
        );
        if (sent <= 0) return 1;
        sent_size += (size_t)sent;
    }
    return 0;
}

/**
 * Opens one test mutex.
 * @param mutex Destination mutex.
 * @return Zero on success, non-zero on failure.
 */
static int mutex_open(test_mutex_t *mutex) {
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

/**
 * Releases one test mutex.
 * @param mutex Mutex to release.
 * @return void.
 */
static void mutex_close(test_mutex_t *mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    (void)pthread_mutex_destroy(mutex);
#endif
}

/**
 * Locks one test mutex.
 * @param mutex Mutex to lock.
 * @return void.
 */
static void mutex_lock(test_mutex_t *mutex) {
#ifdef _WIN32
    EnterCriticalSection(mutex);
#else
    (void)pthread_mutex_lock(mutex);
#endif
}

/**
 * Unlocks one test mutex.
 * @param mutex Mutex to unlock.
 * @return void.
 */
static void mutex_unlock(test_mutex_t *mutex) {
#ifdef _WIN32
    LeaveCriticalSection(mutex);
#else
    (void)pthread_mutex_unlock(mutex);
#endif
}

/**
 * Verifies one integer value.
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
 * Verifies one condition.
 * @param label Check label.
 * @param condition Condition to verify.
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
 * Prints one top-level test result.
 * @param fail Failure count.
 * @param name Test name.
 * @param detail Test detail.
 * @return void.
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
 * Runs one top-level test case.
 * @param result Failure accumulator.
 * @param function Test function.
 * @return void.
 */
static void run_case(int *result, case_fn function) {
    test_case_current++;
    *result += function();
}

/**
 * Initializes one callback state object.
 * @param state State object.
 * @param protocol Endpoint protocol.
 * @return Zero on success, non-zero on failure.
 */
static int state_open(test_state_t *state, int protocol) {
    memset(state, 0, sizeof(*state));
    state->protocol = protocol;
    state->error_status = KC_NETS_OK;
    if (mutex_open(&state->mutex) != 0) return 1;
    state->mutex_ready = 1;
    return 0;
}

/**
 * Releases one callback state object.
 * @param state State object.
 * @return void.
 */
static void state_close(test_state_t *state) {
    if (!state->mutex_ready) return;
    mutex_close(&state->mutex);
    state->mutex_ready = 0;
}

/**
 * Waits until one callback state reaches completion.
 * @param state State object.
 * @return Zero on completion, non-zero on timeout.
 */
static int state_wait(test_state_t *state) {
    int step;

    for (step = 0; step < TEST_WAIT_STEPS; step++) {
        int done;

        mutex_lock(&state->mutex);
        done = state->done;
        mutex_unlock(&state->mutex);
        if (done) return 0;
        sleep_ms(TEST_WAIT_MS);
    }
    return 1;
}

/**
 * Stores bytes received by the public receive callback.
 * @param data Received bytes.
 * @param data_size Byte count.
 * @param userdata Test state.
 * @return void.
 */
static void receive_callback(
    const void *data,
    size_t data_size,
    void *userdata
) {
    test_state_t *state;
    int send_second;

    state = (test_state_t *)userdata;
    send_second = 0;

    mutex_lock(&state->mutex);
    if (
        state->received_size + data_size <= sizeof(state->received) &&
        data_size != 0U
    ) {
        memcpy(state->received + state->received_size, data, data_size);
    }
    if (state->receive_count < 2U) {
        state->datagram_sizes[state->receive_count] = data_size;
    }
    state->received_size += data_size;
    state->receive_count++;
    if (
        state->protocol == KC_NETS_TCP &&
        state->receive_count == 1U &&
        state->network != NULL
    ) {
        send_second = 1;
    }
    if (
        state->protocol == KC_NETS_UDP &&
        state->receive_count == 2U
    ) {
        state->done = 1;
    }
    mutex_unlock(&state->mutex);

    if (send_second) {
        (void)kc_nets_send(state->network, "second", 6U);
    }
}

/**
 * Stores one remote TCP disconnect notification.
 * @param userdata Test state.
 * @return void.
 */
static void disconnect_callback(void *userdata) {
    test_state_t *state;

    state = (test_state_t *)userdata;
    mutex_lock(&state->mutex);
    state->disconnect_count++;
    state->done = 1;
    mutex_unlock(&state->mutex);
}

/**
 * Stores one asynchronous endpoint failure.
 * @param status Public failure status.
 * @param userdata Test state.
 * @return void.
 */
static void error_callback(int status, void *userdata) {
    test_state_t *state;

    state = (test_state_t *)userdata;
    mutex_lock(&state->mutex);
    state->error_status = status;
    state->done = 1;
    mutex_unlock(&state->mutex);
}

/**
 * Discards bytes for argument validation tests.
 * @param data Received bytes.
 * @param data_size Byte count.
 * @param userdata Callback data.
 * @return void.
 */
static void discard_receive(
    const void *data,
    size_t data_size,
    void *userdata
) {
    (void)data;
    (void)data_size;
    (void)userdata;
}

/**
 * Runs one local TCP or UDP peer.
 * @param userdata Server state.
 * @return Platform thread result.
 */
#ifdef _WIN32
static DWORD WINAPI server_main(LPVOID userdata)
#else
static void *server_main(void *userdata)
#endif
{
    static const unsigned char response_one[] = {'r', '1'};
    static const unsigned char response_two[] = {'r', '2', 0};
    test_server_t *server;
    test_socket_t listener;
    struct sockaddr_in address;
    int reuse;

    server = (test_server_t *)userdata;
    server->result = 1;
    listener = socket(
        AF_INET,
        server->protocol == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM,
        0
    );
    if (listener == TEST_SOCKET_INVALID) goto done;

    reuse = 1;
    (void)setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&reuse,
        sizeof(reuse)
    );

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(server->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (
        bind(
            listener,
            (const struct sockaddr *)&address,
            sizeof(address)
        ) != 0
    ) {
        socket_close(listener);
        goto done;
    }

    if (server->protocol == KC_NETS_UDP) {
        int index;

        for (index = 0; index < 2; index++) {
            struct sockaddr_in peer;
            test_socklen_t peer_size;
            int received;

            peer_size = (test_socklen_t)sizeof(peer);
            received = (int)recvfrom(
                listener,
                (char *)server->received + server->received_size,
                (int)(sizeof(server->received) - server->received_size),
                0,
                (struct sockaddr *)&peer,
                &peer_size
            );
            if (received < 0) {
                socket_close(listener);
                goto done;
            }
            server->datagram_sizes[index] = (size_t)received;
            if (
                sendto(
                    listener,
                    (const char *)server->received + server->received_size,
                    received,
                    0,
                    (const struct sockaddr *)&peer,
                    peer_size
                ) != received
            ) {
                socket_close(listener);
                goto done;
            }
            server->received_size += (size_t)received;
        }

        server->result = 0;
        socket_close(listener);
        goto done;
    }

    if (listen(listener, 1) != 0) {
        socket_close(listener);
        goto done;
    }

    {
        test_socket_t client;

        client = accept(listener, NULL, NULL);
        if (client == TEST_SOCKET_INVALID) {
            socket_close(listener);
            goto done;
        }

        if (
            socket_receive_all(client, server->received, 5U) != 0 ||
            socket_send_all(
                client,
                response_one,
                sizeof(response_one)
            ) != 0 ||
            socket_receive_all(client, server->received + 5U, 6U) != 0 ||
            socket_send_all(
                client,
                response_two,
                sizeof(response_two)
            ) != 0
        ) {
            socket_close(client);
            socket_close(listener);
            goto done;
        }

        server->received_size = 11U;
        server->result = 0;
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
 * Starts one local peer thread.
 * @param server Server state.
 * @param protocol KC_NETS_TCP or KC_NETS_UDP.
 * @param port Local port.
 * @return Zero on success, non-zero on failure.
 */
static int server_start(
    test_server_t *server,
    int protocol,
    unsigned short port
) {
    memset(server, 0, sizeof(*server));
    server->protocol = protocol;
    server->port = port;
    server->result = 1;

#ifdef _WIN32
    server->thread = CreateThread(NULL, 0, server_main, server, 0, NULL);
    if (server->thread == NULL) return 1;
#else
    if (pthread_create(&server->thread, NULL, server_main, server) != 0) {
        return 1;
    }
#endif
    sleep_ms(100U);
    return 0;
}

/**
 * Joins one local peer thread.
 * @param server Server state.
 * @return Server result.
 */
static int server_join(test_server_t *server) {
#ifdef _WIN32
    if (WaitForSingleObject(server->thread, 5000U) != WAIT_OBJECT_0) return 1;
    CloseHandle(server->thread);
#else
    if (pthread_join(server->thread, NULL) != 0) return 1;
#endif
    return server->result;
}

/**
 * Sends the first TCP payload from another thread.
 * @param userdata Test state.
 * @return Platform thread result.
 */
#ifdef _WIN32
static DWORD WINAPI send_main(LPVOID userdata)
#else
static void *send_main(void *userdata)
#endif
{
    test_state_t *state;

    state = (test_state_t *)userdata;
    if (kc_nets_send(state->network, "first", 5U) != KC_NETS_OK) {
        mutex_lock(&state->mutex);
        state->error_status = KC_NETS_ENET;
        state->done = 1;
        mutex_unlock(&state->mutex);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Tests public argument validation and status strings.
 * @return Zero on success, non-zero on failure.
 */
static int case_validation(void) {
    const char *name = "validation";
    const char *detail = "rejects invalid endpoint arguments";
    kc_nets_t *network;
    kc_nets_options_t options;
    int fail;

    network = NULL;
    memset(&options, 0, sizeof(options));
    options.host = TEST_HOST;
    options.port = 9U;
    options.protocol = KC_NETS_TCP;
    fail = 0;

    fail += expect_int(
        "open NULL out",
        KC_NETS_EINVAL,
        kc_nets_open(NULL, &options, discard_receive, NULL, NULL, NULL)
    );
    fail += expect_int(
        "open NULL options",
        KC_NETS_EINVAL,
        kc_nets_open(&network, NULL, discard_receive, NULL, NULL, NULL)
    );
    options.host = "";
    fail += expect_int(
        "open empty host",
        KC_NETS_EINVAL,
        kc_nets_open(&network, &options, discard_receive, NULL, NULL, NULL)
    );
    options.host = TEST_HOST;
    options.port = 0U;
    fail += expect_int(
        "open zero port",
        KC_NETS_EINVAL,
        kc_nets_open(&network, &options, discard_receive, NULL, NULL, NULL)
    );
    options.port = 9U;
    options.protocol = 999;
    fail += expect_int(
        "open invalid protocol",
        KC_NETS_EINVAL,
        kc_nets_open(&network, &options, discard_receive, NULL, NULL, NULL)
    );
    options.protocol = KC_NETS_TCP;
    fail += expect_int(
        "open NULL receive",
        KC_NETS_EINVAL,
        kc_nets_open(&network, &options, NULL, NULL, NULL, NULL)
    );
    fail += expect_int(
        "send NULL endpoint",
        KC_NETS_EINVAL,
        kc_nets_send(NULL, "x", 1U)
    );
    fail += expect_true(
        "closed status string",
        strcmp(kc_nets_strerror(KC_NETS_ECLOSED), "endpoint closed") == 0
    );

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests persistent TCP exchange and callback composition.
 * @return Zero on success, non-zero on failure.
 */
static int case_tcp(void) {
    static const unsigned char expected_reply[] = {'r', '1', 'r', '2', 0};
    const char *name = "tcp";
    const char *detail = "keeps one ordered stream across repeated sends";
    kc_nets_options_t options;
    test_server_t server;
    test_state_t state;
    test_thread_t sender;
    unsigned short port;
    int fail;

    fail = 0;
    port = (unsigned short)(port_base() + 1U);
    if (
        state_open(&state, KC_NETS_TCP) != 0 ||
        server_start(&server, KC_NETS_TCP, port) != 0
    ) {
        case_result(1, name, detail);
        return 1;
    }

    options.host = TEST_HOST;
    options.port = port;
    options.protocol = KC_NETS_TCP;
    fail += expect_int(
        "TCP open",
        KC_NETS_OK,
        kc_nets_open(
            &state.network,
            &options,
            receive_callback,
            disconnect_callback,
            error_callback,
            &state
        )
    );

    if (state.network != NULL) {
#ifdef _WIN32
        sender = CreateThread(NULL, 0, send_main, &state, 0, NULL);
        if (sender == NULL) {
            fail++;
        } else {
            (void)WaitForSingleObject(sender, 5000U);
            CloseHandle(sender);
        }
#else
        if (pthread_create(&sender, NULL, send_main, &state) != 0) {
            fail++;
        } else {
            (void)pthread_join(sender, NULL);
        }
#endif
        fail += expect_int("TCP completion", 0, state_wait(&state));
        kc_nets_close(state.network);
        state.network = NULL;
    }

    fail += expect_int("TCP server", 0, server_join(&server));
    fail += expect_true(
        "TCP sent bytes",
        server.received_size == 11U &&
        memcmp(server.received, "firstsecond", 11U) == 0
    );
    fail += expect_true(
        "TCP received bytes",
        state.received_size == sizeof(expected_reply) &&
        memcmp(state.received, expected_reply, sizeof(expected_reply)) == 0
    );
    fail += expect_true("TCP multiple receives", state.receive_count >= 2U);
    fail += expect_int("TCP disconnect once", 1, state.disconnect_count);
    fail += expect_int("TCP no error", KC_NETS_OK, state.error_status);

    state_close(&state);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests repeated UDP datagrams and binary payloads.
 * @return Zero on success, non-zero on failure.
 */
static int case_udp(void) {
    static const unsigned char first[] = {'a', 0, 'b'};
    static const unsigned char second[] = {'c', 'd'};
    static const unsigned char expected[] = {'a', 0, 'b', 'c', 'd'};
    const char *name = "udp";
    const char *detail = "preserves datagram boundaries across repeated sends";
    kc_nets_options_t options;
    test_server_t server;
    test_state_t state;
    unsigned short port;
    int fail;

    fail = 0;
    port = (unsigned short)(port_base() + 2U);
    if (
        state_open(&state, KC_NETS_UDP) != 0 ||
        server_start(&server, KC_NETS_UDP, port) != 0
    ) {
        case_result(1, name, detail);
        return 1;
    }

    options.host = TEST_HOST;
    options.port = port;
    options.protocol = KC_NETS_UDP;
    fail += expect_int(
        "UDP open",
        KC_NETS_OK,
        kc_nets_open(
            &state.network,
            &options,
            receive_callback,
            disconnect_callback,
            error_callback,
            &state
        )
    );

    if (state.network != NULL) {
        fail += expect_int(
            "UDP first send",
            KC_NETS_OK,
            kc_nets_send(state.network, first, sizeof(first))
        );
        fail += expect_int(
            "UDP second send",
            KC_NETS_OK,
            kc_nets_send(state.network, second, sizeof(second))
        );
        fail += expect_int("UDP completion", 0, state_wait(&state));
        kc_nets_close(state.network);
        state.network = NULL;
    }

    fail += expect_int("UDP server", 0, server_join(&server));
    fail += expect_true(
        "UDP server bytes",
        server.received_size == sizeof(expected) &&
        memcmp(server.received, expected, sizeof(expected)) == 0
    );
    fail += expect_true(
        "UDP server boundaries",
        server.datagram_sizes[0] == sizeof(first) &&
        server.datagram_sizes[1] == sizeof(second)
    );
    fail += expect_true(
        "UDP received bytes",
        state.received_size == sizeof(expected) &&
        memcmp(state.received, expected, sizeof(expected)) == 0
    );
    fail += expect_true(
        "UDP receive boundaries",
        state.datagram_sizes[0] == sizeof(first) &&
        state.datagram_sizes[1] == sizeof(second)
    );
    fail += expect_int("UDP no disconnect", 0, state.disconnect_count);
    fail += expect_int("UDP no error", KC_NETS_OK, state.error_status);

    state_close(&state);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Runs the libnets public contract test program.
 * @return Zero when all cases pass, otherwise one.
 */
int main(void) {
    int result;

    if (socket_start() != 0) return 1;

    result = 0;
    test_case_total = 4;
    test_case_current = 0;
    run_case(&result, case_validation);
    run_case(&result, case_tcp);
    run_case(&result, case_udp);
#ifndef __EMSCRIPTEN__
    run_case(&result, case_nets_cli);
#endif

    socket_stop();
    printf("%d passed, %d failed\n", test_case_total - result, result);
    return result == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "nets"
#endif

#ifdef _WIN32
#define NETS_CLI_DEV_NULL "NUL"
#else
#define NETS_CLI_DEV_NULL "/dev/null"
#endif

static int nets_cli_current;
static int nets_cli_failures;

/**
 * Prints one nets CLI result.
 * @param name Test name.
 * @param detail Tested behavior.
 * @param ok Nonzero when the test passed.
 * @return None.
 */
static void nets_cli_result(const char *name, const char *detail, int ok) {
    nets_cli_current++;
    if (!ok) nets_cli_failures++;
    printf("[%d/12] [%s] %s: %s\n", nets_cli_current,
        ok ? "PASS" : "FAIL", name, detail);
}

/**
 * Executes nets with one argument string.
 * @param args Command arguments.
 * @return Shell exit status.
 */
static int nets_cli_run(const char *args) {
    char command[4096];
    int count;

    count = snprintf(command, sizeof(command), "%s %s > " NETS_CLI_DEV_NULL
        " 2> " NETS_CLI_DEV_NULL, KC_CLI_PATH, args);
    if (count < 0 || (size_t)count >= sizeof(command)) return 1;
    return system(command);
}

/**
 * Runs the nets CLI contract tests.
 * @return Zero when every test passes.
 */
static int case_nets_cli(void) {
    nets_cli_current = 0;
    nets_cli_failures = 0;
    nets_cli_result("help", "long help succeeds", nets_cli_run("--help") == 0);
    nets_cli_result("short help", "short help succeeds", nets_cli_run("-h") == 0);
    nets_cli_result("version", "long version succeeds",
        nets_cli_run("--version") == 0);
    nets_cli_result("short version", "short version succeeds",
        nets_cli_run("-v") == 0);
    nets_cli_result("missing endpoint", "empty invocation fails",
        nets_cli_run("") != 0);
    nets_cli_result("missing port", "host without port fails",
        nets_cli_run("localhost") != 0);
    nets_cli_result("zero port", "port zero fails",
        nets_cli_run("localhost 0") != 0);
    nets_cli_result("invalid port", "non-numeric port fails",
        nets_cli_run("localhost invalid") != 0);
    nets_cli_result("invalid transport", "transport requires tcp or udp",
        nets_cli_run("localhost 9 --transport tls") != 0);
    nets_cli_result("transport value", "transport requires a following value",
        nets_cli_run("localhost 9 --transport") != 0);
    nets_cli_result("removed tls option", "TLS option is not accepted",
        nets_cli_run("localhost 9 --tls") != 0);
    nets_cli_result("extra positional", "extra endpoint arguments fail",
        nets_cli_run("localhost 9 extra") != 0);
    printf("%d passed, %d failed\n", 12 - nets_cli_failures,
        nets_cli_failures);
    return nets_cli_failures == 0 ? 0 : 1;
}
#endif
