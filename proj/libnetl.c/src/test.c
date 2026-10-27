/**
 * test.c - netl public contract tests.
 * Summary: Tests persistent TCP peers and UDP datagram behavior.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET test_fd_t;
typedef HANDLE test_thread_t;
#define TEST_FD_INVALID INVALID_SOCKET
#define TEST_CLOSE(fd) closesocket(fd)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
typedef int test_fd_t;
typedef pthread_t test_thread_t;
#define TEST_FD_INVALID (-1)
#define TEST_CLOSE(fd) close(fd)
#endif

#define TEST_WAIT_STEPS 200
#define TEST_WAIT_MS 10UL

typedef struct {
    atomic_int receive_count;
    atomic_int disconnect_count;
    atomic_int error_count;
    atomic_int udp_reply;
    atomic_int tcp_callback_reply;
    kc_netl_peer_t *peers[12];
    int protocols[12];
    unsigned short ports[12];
    char hosts[12][128];
    unsigned char data[12][64];
    size_t sizes[12];
} receive_state_t;

typedef struct {
    kc_netl_peer_t *peer;
    const void *data;
    size_t data_size;
    int status;
} respond_job_t;

static int test_case_total;
static int test_case_current;

#ifndef __EMSCRIPTEN__
static int case_netl_cli(void);
#endif

/**
 * Verifies one condition.
 * @param label Check label.
 * @param value Condition value.
 * @return Failure count.
 */
static int expect_true(const char *label, int value) {
    if (value) return 0;
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Verifies one integer value.
 * @param label Check label.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Failure count.
 */
static int expect_int(const char *label, int expected, int actual) {
    if (expected == actual) return 0;
    fprintf(
        stderr,
        "FAIL: %s expected=%d actual=%d\n",
        label,
        expected,
        actual
    );
    return 1;
}

/**
 * Verifies one byte sequence.
 * @param label Check label.
 * @param expected Expected bytes.
 * @param expected_size Expected byte count.
 * @param actual Actual bytes.
 * @param actual_size Actual byte count.
 * @return Failure count.
 */
static int expect_bytes(
    const char *label,
    const void *expected,
    size_t expected_size,
    const void *actual,
    size_t actual_size
) {
    if (
        expected_size == actual_size &&
        (
            expected_size == 0U ||
            memcmp(expected, actual, expected_size) == 0
        )
    ) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Prints one top-level test result.
 * @param fail Failure count.
 * @param name Test name.
 * @param description Test description.
 * @return void.
 */
static void case_result(
    int fail,
    const char *name,
    const char *description
) {
    test_case_current++;
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail == 0 ? "PASS" : "FAIL",
        name,
        description
    );
}

typedef int (*case_fn)(void);

/**
 * Runs one top-level test case.
 * @param total_fail Failure accumulator.
 * @param function Test function.
 * @return void.
 */
static void run_case(int *total_fail, case_fn function) {
    if (function() != 0) (*total_fail)++;
}

/**
 * Sleeps for a bounded interval.
 * @param milliseconds Sleep duration.
 * @return void.
 */
static void test_sleep_ms(unsigned long milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;

    delay.tv_sec = (time_t)(milliseconds / 1000UL);
    delay.tv_nsec = (long)((milliseconds % 1000UL) * 1000000UL);
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Waits for an atomic counter to reach one value.
 * @param value Counter.
 * @param expected Required minimum.
 * @return Zero on success, non-zero on timeout.
 */
static int wait_atomic_at_least(atomic_int *value, int expected) {
    int index;

    for (index = 0; index < TEST_WAIT_STEPS; index++) {
        if (atomic_load(value) >= expected) return 0;
        test_sleep_ms(TEST_WAIT_MS);
    }
    return 1;
}

/**
 * Configures a receive timeout for one test socket.
 * @param fd Socket descriptor.
 * @return Zero on success, non-zero on failure.
 */
static int test_socket_timeout(test_fd_t fd) {
#ifdef _WIN32
    DWORD timeout = 2000U;

    return setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char *)&timeout,
        (int)sizeof(timeout)
    ) == 0 ? 0 : 1;
#else
    struct timeval timeout;

    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    return setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        (socklen_t)sizeof(timeout)
    ) == 0 ? 0 : 1;
#endif
}

/**
 * Connects one test TCP socket to the listener.
 * @param port Listener port.
 * @return Connected socket, or TEST_FD_INVALID.
 */
static test_fd_t test_tcp_connect(unsigned short port) {
    struct sockaddr_in address;
    test_fd_t fd;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == TEST_FD_INVALID) return TEST_FD_INVALID;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (
        connect(
            fd,
            (const struct sockaddr *)&address,
            (socklen_t)sizeof(address)
        ) != 0
    ) {
        TEST_CLOSE(fd);
        return TEST_FD_INVALID;
    }

    (void)test_socket_timeout(fd);
    return fd;
}

/**
 * Reads and verifies one exact TCP response.
 * @param fd Connected socket.
 * @param expected Expected bytes.
 * @param expected_size Expected byte count.
 * @return Failure count.
 */
static int test_socket_expect(
    test_fd_t fd,
    const void *expected,
    size_t expected_size
) {
    unsigned char buffer[256];
    size_t used;

    used = 0U;
    while (used < expected_size) {
        int amount;
        int received;

        amount = (int)(expected_size - used);
        received = recv(fd, (char *)buffer + used, amount, 0);
        if (received <= 0) return 1;
        used += (size_t)received;
    }

    return expect_bytes(
        "socket response bytes",
        expected,
        expected_size,
        buffer,
        used
    );
}

/**
 * Stores one public receive event and optional callback response.
 * @param input Borrowed input event.
 * @param userdata Test state.
 * @return void.
 */
static void receive(kc_netl_peer_t *peer,const void *data,
    size_t data_size,void *userdata) {
    receive_state_t *state;
    int index;

    state = (receive_state_t *)userdata;
    index = atomic_load(&state->receive_count);

    if (index < 12) {
        size_t copy_size;

        copy_size = data_size < sizeof(state->data[index])
            ? data_size
            : sizeof(state->data[index]);
        state->peers[index] = peer;
        state->protocols[index] = kc_netl_peer_protocol(peer);
        state->ports[index] = kc_netl_peer_port(peer);
        snprintf(
            state->hosts[index],
            sizeof(state->hosts[index]),
            "%s",
            kc_netl_peer_host(peer) != NULL ? kc_netl_peer_host(peer) : ""
        );
        if (copy_size != 0U) {
            memcpy(state->data[index], data, copy_size);
        }
        state->sizes[index] = copy_size;
    }

    if (
        kc_netl_peer_protocol(peer) == KC_NETL_TCP &&
        atomic_load(&state->tcp_callback_reply)
    ) {
        (void)kc_netl_send(peer, "callback", 8U);
    }
    if (
        kc_netl_peer_protocol(peer) == KC_NETL_UDP &&
        atomic_load(&state->udp_reply)
    ) {
        (void)kc_netl_send(peer, "pong", 4U);
    }

    atomic_fetch_add(&state->receive_count, 1);
}

/**
 * Stores one TCP disconnect event.
 * @param peer Closing peer.
 * @param userdata Test state.
 * @return void.
 */
static void disconnect(kc_netl_peer_t *peer, void *userdata) {
    receive_state_t *state;

    state = (receive_state_t *)userdata;
    (void)peer;
    atomic_fetch_add(&state->disconnect_count, 1);
}

/**
 * Stores one asynchronous listener failure.
 * @param status Public failure status.
 * @param userdata Test state.
 * @return void.
 */
static void error(int status, void *userdata) {
    receive_state_t *state;

    state = (receive_state_t *)userdata;
    (void)status;
    atomic_fetch_add(&state->error_count, 1);
}

/**
 * Initializes one receive state.
 * @param state State to initialize.
 * @return void.
 */
static void state_init(receive_state_t *state) {
    memset(state, 0, sizeof(*state));
    atomic_init(&state->receive_count, 0);
    atomic_init(&state->disconnect_count, 0);
    atomic_init(&state->error_count, 0);
    atomic_init(&state->udp_reply, 0);
    atomic_init(&state->tcp_callback_reply, 0);
}

#ifdef _WIN32
/**
 * Sends one response from a secondary Windows thread.
 * @param userdata Response job.
 * @return Thread exit code.
 */
static DWORD WINAPI respond_thread_main(LPVOID userdata) {
    respond_job_t *job;

    job = (respond_job_t *)userdata;
    job->status = kc_netl_send(job->peer, job->data, job->data_size);
    return 0;
}
#else
/**
 * Sends one response from a secondary POSIX thread.
 * @param userdata Response job.
 * @return NULL.
 */
static void *respond_thread_main(void *userdata) {
    respond_job_t *job;

    job = (respond_job_t *)userdata;
    job->status = kc_netl_send(job->peer, job->data, job->data_size);
    return NULL;
}
#endif

/**
 * Sends one peer response from another thread and joins it.
 * @param peer Destination peer.
 * @param data Response bytes.
 * @param data_size Response byte count.
 * @return Public response status, or KC_NETL_ENET on thread failure.
 */
static int respond_from_thread(
    kc_netl_peer_t *peer,
    const void *data,
    size_t data_size
) {
    respond_job_t job;
    test_thread_t thread;

    memset(&job, 0, sizeof(job));
    job.peer = peer;
    job.data = data;
    job.data_size = data_size;
    job.status = KC_NETL_ENET;

#ifdef _WIN32
    thread = CreateThread(NULL, 0, respond_thread_main, &job, 0, NULL);
    if (thread == NULL) return KC_NETL_ENET;
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
        CloseHandle(thread);
        return KC_NETL_ENET;
    }
    CloseHandle(thread);
#else
    if (pthread_create(&thread, NULL, respond_thread_main, &job) != 0) {
        return KC_NETL_ENET;
    }
    if (pthread_join(thread, NULL) != 0) return KC_NETL_ENET;
#endif

    return job.status;
}

/**
 * Tests listener creation and argument validation.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_open(void) {
    kc_netl_options_t options;
    kc_netl_t *listener;
    receive_state_t state;
    int pending;
    int fail;

    listener = (kc_netl_t *)1;
    pending = -1;
    fail = 0;
    state_init(&state);
    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    fail += expect_int(
        "NULL out",
        KC_NETL_EINVAL,
        kc_netl_open(NULL, &options, receive, disconnect, error, &state)
    );
    fail += expect_int(
        "NULL options",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, NULL, receive, disconnect, error, &state)
    );
    fail += expect_true("failed open clears output", listener == NULL);
    fail += expect_int(
        "NULL receive",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, &options, NULL, disconnect, error, &state)
    );

    options.protocol = 99;
    fail += expect_int(
        "invalid protocol",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, &options, receive, disconnect, error, &state)
    );

    options.protocol = KC_NETL_TCP;
    options.max_pending_connections = &pending;
    fail += expect_int(
        "invalid pending count",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, &options, receive, disconnect, error, &state)
    );

    options.max_pending_connections = NULL;
    fail += expect_int(
        "TCP open",
        KC_NETL_OK,
        kc_netl_open(&listener, &options, receive, disconnect, error, &state)
    );
    fail += expect_true("listener allocated", listener != NULL);
    fail += expect_true("ephemeral port selected", kc_netl_port(listener) != 0U);
    kc_netl_close(listener);
    kc_netl_close(NULL);

    case_result(
        fail,
        "kc_netl_open",
        "opens an operational receiving listener"
    );
    return fail != 0;
}

/**
 * Tests stable TCP peers, ordered responses, and peer closure.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_tcp(void) {
    static const unsigned char binary[] = {'x', 0, 'y'};
    kc_netl_options_t options;
    kc_netl_t *listener;
    receive_state_t state;
    test_fd_t client_a;
    test_fd_t client_b;
    kc_netl_peer_t *peer_a;
    kc_netl_peer_t *peer_b;
    int index;
    int fail;

    listener = NULL;
    client_a = TEST_FD_INVALID;
    client_b = TEST_FD_INVALID;
    peer_a = NULL;
    peer_b = NULL;
    fail = 0;
    state_init(&state);
    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    if (
        kc_netl_open(
            &listener,
            &options,
            receive,
            disconnect,
            error,
            &state
        ) != KC_NETL_OK
    ) {
        return 1;
    }

    client_a = test_tcp_connect(kc_netl_port(listener));
    client_b = test_tcp_connect(kc_netl_port(listener));
    if (client_a == TEST_FD_INVALID || client_b == TEST_FD_INVALID) {
        if (client_a != TEST_FD_INVALID) TEST_CLOSE(client_a);
        if (client_b != TEST_FD_INVALID) TEST_CLOSE(client_b);
        kc_netl_close(listener);
        return 1;
    }

    fail += expect_int("send A", 1, (int)send(client_a, "A", 1, 0));
    fail += expect_int("send B", 1, (int)send(client_b, "B", 1, 0));
    fail += expect_int(
        "two TCP inputs",
        0,
        wait_atomic_at_least(&state.receive_count, 2)
    );

    for (index = 0; index < 2; index++) {
        fail += expect_int("TCP protocol", KC_NETL_TCP, state.protocols[index]);
        fail += expect_true("TCP peer", state.peers[index] != NULL);
        fail += expect_true("TCP host", state.hosts[index][0] != '\0');
        fail += expect_true("TCP port", state.ports[index] != 0U);

        if (state.sizes[index] == 1U && state.data[index][0] == 'A') {
            peer_a = state.peers[index];
        } else if (
            state.sizes[index] == 1U &&
            state.data[index][0] == 'B'
        ) {
            peer_b = state.peers[index];
        }
    }

    fail += expect_true("peer A identified", peer_a != NULL);
    fail += expect_true("peer B identified", peer_b != NULL);
    fail += expect_true("peer identities differ", peer_a != peer_b);

    atomic_store(&state.tcp_callback_reply, 1);
    fail += expect_int("callback input", 2, (int)send(client_b, "cb", 2, 0));
    fail += expect_int(
        "callback receive",
        0,
        wait_atomic_at_least(&state.receive_count, 3)
    );
    fail += test_socket_expect(client_b, "callback", 8U);
    atomic_store(&state.tcp_callback_reply, 0);
    if (atomic_load(&state.receive_count) >= 3) {
        fail += expect_true("B peer retained", state.peers[2] == peer_b);
    }

    if (peer_a != NULL) {
        fail += expect_int(
            "first ordered response",
            KC_NETL_OK,
            kc_netl_send(peer_a, "one", 3U)
        );
        fail += expect_int(
            "second ordered response",
            KC_NETL_OK,
            kc_netl_send(peer_a, "two", 3U)
        );
        fail += test_socket_expect(client_a, "onetwo", 6U);
    }

    if (peer_b != NULL) {
        fail += expect_int(
            "thread response",
            KC_NETL_OK,
            respond_from_thread(peer_b, "thread", 6U)
        );
        fail += test_socket_expect(client_b, "thread", 6U);
    }

    if (peer_a != NULL) {
        fail += expect_int(
            "pending close response",
            KC_NETL_OK,
            kc_netl_send(peer_a, "bye", 3U)
        );
        kc_netl_peer_close(peer_a);
        fail += test_socket_expect(client_a, "bye", 3U);
        fail += expect_int(
            "A disconnect",
            0,
            wait_atomic_at_least(&state.disconnect_count, 1)
        );
    }

    fail += expect_int(
        "binary send",
        (int)sizeof(binary),
        (int)send(client_b, (const char *)binary, (int)sizeof(binary), 0)
    );
    fail += expect_int(
        "binary receive",
        0,
        wait_atomic_at_least(&state.receive_count, 4)
    );
    if (atomic_load(&state.receive_count) >= 4) {
        fail += expect_true("B identity remains stable", state.peers[3] == peer_b);
        fail += expect_bytes(
            "binary input bytes",
            binary,
            sizeof(binary),
            state.data[3],
            state.sizes[3]
        );
    }

    TEST_CLOSE(client_a);
    TEST_CLOSE(client_b);
    fail += expect_int(
        "two disconnects",
        0,
        wait_atomic_at_least(&state.disconnect_count, 2)
    );
    test_sleep_ms(50UL);
    fail += expect_int(
        "disconnect exactly once per TCP peer",
        2,
        atomic_load(&state.disconnect_count)
    );
    fail += expect_int("no listener error", 0, atomic_load(&state.error_count));

    kc_netl_close(listener);
    case_result(
        fail,
        "kc_netl_tcp",
        "keeps peers stable and responses composable"
    );
    return fail != 0;
}

/**
 * Tests closing a listener while one TCP peer remains active.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_listener_close(void) {
    kc_netl_options_t options;
    kc_netl_t *listener;
    receive_state_t state;
    test_fd_t client;
    char byte;
    int received;
    int fail;

    listener = NULL;
    client = TEST_FD_INVALID;
    fail = 0;
    state_init(&state);
    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    if (
        kc_netl_open(
            &listener,
            &options,
            receive,
            disconnect,
            error,
            &state
        ) != KC_NETL_OK
    ) {
        return 1;
    }

    client = test_tcp_connect(kc_netl_port(listener));
    if (client == TEST_FD_INVALID) {
        kc_netl_close(listener);
        return 1;
    }

    fail += expect_int("active send", 4, (int)send(client, "live", 4, 0));
    fail += expect_int(
        "active peer accepted",
        0,
        wait_atomic_at_least(&state.receive_count, 1)
    );

    kc_netl_close(listener);
    listener = NULL;
    received = recv(client, &byte, 1, 0);
    fail += expect_true("active peer socket closed", received <= 0);
    TEST_CLOSE(client);

    case_result(
        fail,
        "kc_netl_listener_close",
        "closes active peers with the listener"
    );
    return fail != 0;
}

/**
 * Tests UDP datagram input and receive-local responses.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_udp(void) {
    static const unsigned char payload[] = {'p', 0, 'g'};
    kc_netl_options_t options;
    kc_netl_t *listener;
    receive_state_t state;
    struct sockaddr_in address;
    test_fd_t client;
    unsigned char response[8];
    int received;
    int fail;

    listener = NULL;
    client = TEST_FD_INVALID;
    fail = 0;
    state_init(&state);
    atomic_store(&state.udp_reply, 1);
    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_UDP;

    if (
        kc_netl_open(
            &listener,
            &options,
            receive,
            disconnect,
            error,
            &state
        ) != KC_NETL_OK
    ) {
        return 1;
    }

    client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client == TEST_FD_INVALID) {
        kc_netl_close(listener);
        return 1;
    }
    (void)test_socket_timeout(client);

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(kc_netl_port(listener));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    fail += expect_int(
        "UDP send",
        (int)sizeof(payload),
        (int)sendto(
            client,
            (const char *)payload,
            (int)sizeof(payload),
            0,
            (const struct sockaddr *)&address,
            (socklen_t)sizeof(address)
        )
    );
    fail += expect_int(
        "UDP receive",
        0,
        wait_atomic_at_least(&state.receive_count, 1)
    );
    fail += expect_int("UDP protocol", KC_NETL_UDP, state.protocols[0]);
    fail += expect_true("UDP peer present", state.peers[0] != NULL);
    fail += expect_true("UDP host present", state.hosts[0][0] != '\0');
    fail += expect_true("UDP port present", state.ports[0] != 0U);
    fail += expect_bytes(
        "UDP input bytes",
        payload,
        sizeof(payload),
        state.data[0],
        state.sizes[0]
    );

    received = recv(client, (char *)response, (int)sizeof(response), 0);
    fail += expect_int("UDP response size", 4, received);
    if (received == 4) {
        fail += expect_bytes("UDP response", "pong", 4U, response, 4U);
    }
    fail += expect_int(
        "UDP has no disconnect",
        0,
        atomic_load(&state.disconnect_count)
    );
    fail += expect_int("no listener error", 0, atomic_load(&state.error_count));

    TEST_CLOSE(client);
    kc_netl_close(listener);
    case_result(
        fail,
        "kc_netl_udp",
        "preserves datagrams and responds to their origins"
    );
    return fail != 0;
}

/**
 * Exercises one persistent outgoing UDP peer against a listener.
 * Both peers receive through the same callback contract and send through
 * kc_netl_send without reopening their socket.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_outbound_udp(void)
{
    kc_netl_t *listener=NULL,*outbound=NULL;
    kc_netl_peer_t *remote=NULL;
    kc_netl_options_t listen_options;
    kc_netl_connect_options_t connect_options;
    receive_state_t server,client;
    int fail=0;
    state_init(&server);
    state_init(&client);
    atomic_store(&server.udp_reply,1);
    memset(&listen_options,0,sizeof(listen_options));
    listen_options.host="127.0.0.1";
    listen_options.protocol=KC_NETL_UDP;
    if(kc_netl_open(&listener,&listen_options,receive,disconnect,error,
        &server)!=KC_NETL_OK)return 1;
    memset(&connect_options,0,sizeof(connect_options));
    connect_options.host="127.0.0.1";
    connect_options.port=kc_netl_port(listener);
    connect_options.protocol=KC_NETL_UDP;
    connect_options.local_host="127.0.0.1";
    fail+=expect_int("outbound UDP open",KC_NETL_OK,
        kc_netl_connect(&outbound,&remote,&connect_options,receive,
            disconnect,error,&client));
    if(remote){
        unsigned short local_port=kc_netl_port(outbound);
        fail+=expect_true("UDP outgoing persistent local port",
            local_port!=0);
        fail+=expect_int("outgoing UDP first send",KC_NETL_OK,
            kc_netl_send(remote,"a",1U));
        fail+=expect_int("outgoing UDP first response",0,
            wait_atomic_at_least(&client.receive_count,1));
        fail+=expect_int("outgoing UDP second send",KC_NETL_OK,
            kc_netl_send(remote,"b",1U));
        fail+=expect_int("outgoing UDP second response",0,
            wait_atomic_at_least(&client.receive_count,2));
        fail+=expect_int("UDP source port retained",local_port,
            server.ports[0]);
        fail+=expect_int("UDP source port stable",local_port,
            server.ports[1]);
        fail+=expect_true("outgoing response contains pong",
            client.sizes[0]==4U&&
            memcmp(client.data[0],"pong",4U)==0);
    }
    kc_netl_close(outbound);
    kc_netl_close(listener);
    case_result(fail,"kc_netl_outbound_udp",
        "uses persistent local socket for repeated bidirectional UDP");
    return fail!=0;
}

/**
 * Exercises one persistent outgoing TCP peer against a listening socket.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_outbound_tcp(void)
{
    kc_netl_t *listener=NULL,*outbound=NULL;
    kc_netl_peer_t *remote=NULL;
    kc_netl_options_t listen_options;
    kc_netl_connect_options_t connect_options;
    receive_state_t server,client;
    int fail=0;
    state_init(&server);
    state_init(&client);
    atomic_store(&server.tcp_callback_reply,1);
    memset(&listen_options,0,sizeof(listen_options));
    listen_options.host="127.0.0.1";
    listen_options.protocol=KC_NETL_TCP;
    if(kc_netl_open(&listener,&listen_options,receive,disconnect,error,
        &server)!=KC_NETL_OK)return 1;
    memset(&connect_options,0,sizeof(connect_options));
    connect_options.host="127.0.0.1";
    connect_options.port=kc_netl_port(listener);
    connect_options.protocol=KC_NETL_TCP;
    fail+=expect_int("outbound TCP connect",KC_NETL_OK,
        kc_netl_connect(&outbound,&remote,&connect_options,receive,
            disconnect,error,&client));
    if(remote){
        fail+=expect_int("outbound TCP send",KC_NETL_OK,
            kc_netl_send(remote,"hello",5U));
        fail+=expect_int("outbound TCP callback response",0,
            wait_atomic_at_least(&client.receive_count,1));
        fail+=expect_true("outbound TCP response",
            client.sizes[0]==8U&&
            memcmp(client.data[0],"callback",8U)==0);
    }
    kc_netl_close(outbound);
    kc_netl_close(listener);
    case_result(fail,"kc_netl_outbound_tcp",
        "keeps one bidirectional TCP connection open across operations");
    return fail!=0;
}

/**
 * Tests public status helpers.
 * @return Zero on success, non-zero on failure.
 */
static int case_kc_netl_status(void) {
    int fail;

    fail = 0;
    fail += expect_true("OK", strcmp(kc_netl_strerror(KC_NETL_OK), "ok") == 0);
    fail += expect_true(
        "EINVAL",
        strcmp(kc_netl_strerror(KC_NETL_EINVAL), "invalid argument") == 0
    );
    fail += expect_true(
        "ENET",
        strcmp(kc_netl_strerror(KC_NETL_ENET), "network error") == 0
    );
    fail += expect_true(
        "ECLOSED",
        strcmp(kc_netl_strerror(KC_NETL_ECLOSED), "peer closed") == 0
    );
    fail += expect_true(
        "ENOMEM",
        strcmp(kc_netl_strerror(KC_NETL_ENOMEM), "out of memory") == 0
    );
    fail += expect_int(
        "NULL peer respond",
        KC_NETL_EINVAL,
        kc_netl_send(NULL, "x", 1U)
    );
    (void)kc_netl_version();

    case_result(
        fail,
        "kc_netl_status",
        "maps status values and validates closed inputs"
    );
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "netl"
#endif

/**
 * Run the netl CLI with output discarded.
 * @param args Arguments appended after the executable.
 * @return 0 on success, 1 on failure.
 */
static int netl_cli_run(const char *args) {
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args);
#endif
    return system(command) == 0 ? 0 : 1;
}

/**
 * Test CLI argument handling for listener administration.
 * @return 0 on success, 1 on failure.
 */
static int case_netl_cli(void) {
    int fail = 0;

    fail += netl_cli_run("--help");
    fail += netl_cli_run("--version");
    fail += netl_cli_run("") == 0;
    fail += netl_cli_run("sample 8080") == 0;
    fail += netl_cli_run("sample 8080 true --unknown") == 0;
    fail += netl_cli_run("bad/name -d") == 0;
    fail += netl_cli_run("nonexistent") == 0;
    fail += netl_cli_run("sender -c 127.0.0.1:invalid -t udp") == 0;
    fail += netl_cli_run("sender -c badendpoint -t udp") == 0;
    {
        char create[256],listed[256],remove[256];
        unsigned short port=(unsigned short)(20000U+
            ((unsigned)kc_netl_version()%15000U));
        snprintf(create,sizeof(create),
            "contract_%u -c 127.0.0.1:%u -t udp",
            (unsigned)port,(unsigned)port);
        snprintf(listed,sizeof(listed),"-l contract_%u",(unsigned)port);
        snprintf(remove,sizeof(remove),"contract_%u -d",(unsigned)port);
        fail += netl_cli_run(create);
        fail += netl_cli_run(listed);
        fail += netl_cli_run(remove);
    }
    case_result(fail, "netl_cli",
        "validates listener administration arguments and errors");
    return fail != 0;
}
#endif

/**
 * Runs all public contract cases.
 * @return Failure count.
 */
static int case_all(void) {
    int result;

    result = 0;
    test_case_total = 8;
    test_case_current = 0;
    run_case(&result, case_kc_netl_open);
    run_case(&result, case_kc_netl_tcp);
    run_case(&result, case_kc_netl_listener_close);
    run_case(&result, case_kc_netl_udp);
    run_case(&result, case_kc_netl_outbound_udp);
    run_case(&result, case_kc_netl_outbound_tcp);
    run_case(&result, case_kc_netl_status);
#ifndef __EMSCRIPTEN__
    run_case(&result, case_netl_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - result, result);
    return result;
}

/**
 * Runs one requested test case.
 * @param argc Argument count.
 * @param argv Argument values.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_netl_open") == 0) return case_kc_netl_open();
    if (strcmp(argv[1], "kc_netl_tcp") == 0) return case_kc_netl_tcp();
    if (
        strcmp(argv[1], "kc_netl_listener_close") == 0
    ) {
        return case_kc_netl_listener_close();
    }
    if (strcmp(argv[1], "kc_netl_udp") == 0) return case_kc_netl_udp();
    if (strcmp(argv[1], "kc_netl_outbound_udp") == 0) return case_kc_netl_outbound_udp();
    if (strcmp(argv[1], "kc_netl_outbound_tcp") == 0) return case_kc_netl_outbound_tcp();
    if (strcmp(argv[1], "kc_netl_status") == 0) return case_kc_netl_status();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "netl_cli") == 0) return case_netl_cli();
#endif

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
