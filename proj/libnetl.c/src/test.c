/**
 * test.c - netl public contract tests.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <stdatomic.h>
#include <stdint.h>
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
#define TEST_FD SOCKET
#define TEST_FD_INVALID INVALID_SOCKET
#define TEST_CLOSE(fd) closesocket(fd)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define TEST_FD int
#define TEST_FD_INVALID (-1)
#define TEST_CLOSE(fd) close(fd)
#endif

static int test_case_total = 0;
static int test_case_current = 0;

typedef struct {
    atomic_int receive_count;
    atomic_int disconnect_count;
    atomic_int error_count;
    atomic_int udp_reply;
    kc_netl_peer_t *peers[8];
    int protocols[8];
    unsigned short ports[8];
    char hosts[8][128];
    unsigned char data[8][64];
    size_t sizes[8];
} receive_state_t;

/**
 * Expect true.
 * @return Function result.
 */
static int expect_true(const char *label, int value) {
    if (value) return 0;
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Expect int.
 * @return Function result.
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
 * Expect bytes.
 * @return Function result.
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
        (expected_size == 0U || memcmp(expected, actual, expected_size) == 0)
    ) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Result.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    test_case_current++;
    printf(
        "[%d/%d] %s: %s - %s\n",
        test_case_current,
        test_case_total,
        fail == 0 ? "PASS" : "FAIL",
        name,
        description
    );
}

/**
 * Run case.
 * @return None.
 */
static void run_case(int *total_fail, int (*fn)(void)) {
    if (fn() != 0) (*total_fail)++;
}

/**
 * Sleep ms.
 * @return None.
 */
static void test_sleep_ms(unsigned long ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(ms / 1000UL);
    delay.tv_nsec = (long)((ms % 1000UL) * 1000000UL);
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Wait atomic at least.
 * @return Function result.
 */
static int wait_atomic_at_least(atomic_int *value, int expected) {
    int i;
    for (i = 0; i < 200; i++) {
        if (atomic_load(value) >= expected) return 0;
        test_sleep_ms(10UL);
    }
    return 1;
}

/**
 * Socket timeout.
 * @return Function result.
 */
static int test_socket_timeout(TEST_FD fd) {
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
 * Tcp connect.
 * @return Function result.
 */
static TEST_FD test_tcp_connect(unsigned short port) {
    struct sockaddr_in address;
    TEST_FD fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

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
 * Socket expect.
 * @return Function result.
 */
static int test_socket_expect(
    TEST_FD fd,
    const void *expected,
    size_t expected_size
) {
    unsigned char buffer[256];
    size_t used = 0U;

    while (used < expected_size) {
        int amount = (int)(expected_size - used);
        int received = recv(fd, (char *)buffer + used, amount, 0);
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
 * On input.
 * @return None.
 */
static void receive(const kc_netl_input_t *input, void *userdata) {
    receive_state_t *state = (receive_state_t *)userdata;
    int index = atomic_load(&state->receive_count);

    if (index < 8) {
        size_t copy_size = input->data_size < sizeof(state->data[index])
            ? input->data_size
            : sizeof(state->data[index]);

        state->peers[index] = input->peer;
        state->protocols[index] = input->protocol;
        state->ports[index] = input->port;
        snprintf(
            state->hosts[index],
            sizeof(state->hosts[index]),
            "%s",
            input->host != NULL ? input->host : ""
        );
        if (copy_size != 0U) memcpy(state->data[index], input->data, copy_size);
        state->sizes[index] = copy_size;
    }

    if (input->protocol == KC_NETL_UDP && atomic_load(&state->udp_reply)) {
        (void)kc_netl_respond(input->peer, "pong", 4U);
    }

    atomic_fetch_add(&state->receive_count, 1);
}

/**
 * On close.
 * @return None.
 */
static void disconnect(kc_netl_peer_t *peer, void *userdata) {
    receive_state_t *state = (receive_state_t *)userdata;
    (void)peer;
    atomic_fetch_add(&state->disconnect_count, 1);
}

/**
 * On error.
 * @return None.
 */
static void error(int status, void *userdata) {
    receive_state_t *state = (receive_state_t *)userdata;
    (void)status;
    atomic_fetch_add(&state->error_count, 1);
}

/**
 * State init.
 * @return None.
 */
static void state_init(receive_state_t *state) {
    memset(state, 0, sizeof(*state));
    atomic_init(&state->receive_count, 0);
    atomic_init(&state->disconnect_count, 0);
    atomic_init(&state->error_count, 0);
    atomic_init(&state->udp_reply, 0);
}

/**
 * Kc netl open.
 * @return Function result.
 */
static int case_kc_netl_open(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = (kc_netl_t *)1;
    receive_state_t state;
    int fail = 0;

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
    fail += expect_int(
        "TCP open",
        KC_NETL_OK,
        kc_netl_open(&listener, &options, receive, disconnect, error, &state)
    );
    fail += expect_true("listener allocated", listener != NULL);
    fail += expect_true(
        "ephemeral port selected",
        kc_netl_port(listener) != 0U
    );
    kc_netl_close(listener);
    kc_netl_close(NULL);

    case_result(fail, "kc_netl_open", "opens an operational receiving listener");
    return fail != 0;
}

/**
 * Kc netl tcp.
 * @return Function result.
 */
static int case_kc_netl_tcp(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = NULL;
    receive_state_t state;
    TEST_FD client_a = TEST_FD_INVALID;
    TEST_FD client_b = TEST_FD_INVALID;
    kc_netl_peer_t *peer_a = NULL;
    kc_netl_peer_t *peer_b = NULL;
    int i;
    int fail = 0;

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

    (void)send(client_a, "A", 1, 0);
    (void)send(client_b, "B", 1, 0);
    fail += expect_int(
        "two TCP inputs",
        0,
        wait_atomic_at_least(&state.receive_count, 2)
    );

    for (i = 0; i < 2; i++) {
        fail += expect_int("TCP protocol", KC_NETL_TCP, state.protocols[i]);
        fail += expect_true("TCP peer", state.peers[i] != NULL);
        fail += expect_true("TCP host", state.hosts[i][0] != '\0');
        fail += expect_true("TCP port", state.ports[i] != 0U);

        if (state.sizes[i] == 1U && state.data[i][0] == 'A') {
            peer_a = state.peers[i];
        } else if (state.sizes[i] == 1U && state.data[i][0] == 'B') {
            peer_b = state.peers[i];
        }
    }

    fail += expect_true("peer A identified", peer_a != NULL);
    fail += expect_true("peer B identified", peer_b != NULL);
    fail += expect_true("peer identities differ", peer_a != peer_b);

    if (peer_a != NULL) {
        fail += expect_int(
            "respond A",
            KC_NETL_OK,
            kc_netl_respond(peer_a, "reply-a", 7U)
        );
        fail += test_socket_expect(client_a, "reply-a", 7U);
    }

    (void)send(client_b, "still", 5, 0);
    fail += expect_int(
        "third TCP input",
        0,
        wait_atomic_at_least(&state.receive_count, 3)
    );
    if (atomic_load(&state.receive_count) >= 3) {
        fail += expect_true("B peer retained", state.peers[2] == peer_b);
        fail += expect_bytes(
            "B next bytes",
            "still",
            5U,
            state.data[2],
            state.sizes[2]
        );
    }

    if (peer_a != NULL) {
        fail += expect_int(
            "queue final A response",
            KC_NETL_OK,
            kc_netl_respond(peer_a, "bye", 3U)
        );
        kc_netl_peer_close(peer_a);
        fail += test_socket_expect(client_a, "bye", 3U);
        fail += expect_int(
            "A disconnect",
            0,
            wait_atomic_at_least(&state.disconnect_count, 1)
        );
    }

    (void)send(client_b, "ok", 2, 0);
    fail += expect_int(
        "B stays active",
        0,
        wait_atomic_at_least(&state.receive_count, 4)
    );
    if (atomic_load(&state.receive_count) >= 4) {
        fail += expect_true(
            "B identity still retained",
            state.peers[3] == peer_b
        );
    }

    TEST_CLOSE(client_a);
    TEST_CLOSE(client_b);
    (void)wait_atomic_at_least(&state.disconnect_count, 2);
    fail += expect_int("no listener error", 0, atomic_load(&state.error_count));

    kc_netl_close(listener);
    case_result(
        fail,
        "kc_netl_tcp",
        "delivers peer-scoped TCP input and responses"
    );
    return fail != 0;
}

/**
 * Kc netl udp.
 * @return Function result.
 */
static int case_kc_netl_udp(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = NULL;
    receive_state_t state;
    struct sockaddr_in address;
    TEST_FD client = TEST_FD_INVALID;
    char response[8];
    int received;
    int fail = 0;

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
        4,
        (int)sendto(
            client,
            "ping",
            4,
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
        "ping",
        4U,
        state.data[0],
        state.sizes[0]
    );

    received = recv(client, response, (int)sizeof(response), 0);
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
        "responds to the exact UDP origin without sendto"
    );
    return fail != 0;
}

/**
 * Kc netl status.
 * @return Function result.
 */
static int case_kc_netl_status(void) {
    int fail = 0;

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
    (void)kc_netl_version();

    case_result(
        fail,
        "kc_netl_status",
        "maps status values and exposes build version"
    );
    return fail != 0;
}

/**
 * All.
 * @return Function result.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 4;
    test_case_current = 0;
    run_case(&rc, case_kc_netl_open);
    run_case(&rc, case_kc_netl_tcp);
    run_case(&rc, case_kc_netl_udp);
    run_case(&rc, case_kc_netl_status);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Main.
 * @return Function result.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_netl_open") == 0) return case_kc_netl_open();
    if (strcmp(argv[1], "kc_netl_tcp") == 0) return case_kc_netl_tcp();
    if (strcmp(argv[1], "kc_netl_udp") == 0) return case_kc_netl_udp();
    if (strcmp(argv[1], "kc_netl_status") == 0) return case_kc_netl_status();

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
