/**
 * test.c - libnetl public API contract tests.
 * Summary: Tests multiplexed TCP connections, UDP datagrams, and CLI surface.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libnetl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET test_fd_t;
#define TEST_FD_INVALID INVALID_SOCKET
#define TEST_CLOSE closesocket
#define TEST_GETPID _getpid
#define TEST_CLI_NAME "netl.exe"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
typedef int test_fd_t;
#define TEST_FD_INVALID (-1)
#define TEST_CLOSE close
#define TEST_GETPID getpid
#define TEST_CLI_NAME "netl"
#endif

static int test_case_total;
static int test_case_current;
static const char *test_program_path;

/**
 * Print one top-level test result.
 * @param fail Failure count.
 * @param name Canonical case name.
 * @param detail Case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    test_case_current++;
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

/**
 * Run one top-level case.
 * @param rc Failure accumulator.
 * @param fn Case function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    if (fn() != 0) (*rc)++;
}

/**
 * Verify one condition.
 * @param label Check label.
 * @param condition Condition.
 * @return Failure count.
 */
static int expect_true(const char *label, int condition) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Verify one integer result.
 * @param label Check label.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Failure count.
 */
static int expect_int(const char *label, int expected, int actual) {
    if (expected == actual) return 0;
    fprintf(
        stderr,
        "FAIL: %s: expected %d, got %d\n",
        label,
        expected,
        actual
    );
    return 1;
}

/**
 * Verify one byte sequence.
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
        (expected_size == 0U ||
         memcmp(expected, actual, expected_size) == 0)
    ) {
        return 0;
    }

    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Verify one buffer contains text.
 * @param label Check label.
 * @param data Buffer bytes.
 * @param size Buffer size.
 * @param needle Expected text.
 * @return Failure count.
 */
static int expect_contains(
    const char *label,
    const void *data,
    size_t size,
    const char *needle
) {
    size_t needle_size = strlen(needle);
    size_t i;

    for (i = 0U; i + needle_size <= size; i++) {
        if (
            memcmp(
                (const unsigned char *)data + i,
                needle,
                needle_size
            ) == 0
        ) {
            return 0;
        }
    }

    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

/**
 * Configure a short receive timeout on one test socket.
 * @param fd Socket descriptor.
 * @return Zero on success, otherwise nonzero.
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
 * Connect one IPv4 TCP client to a listener port.
 * @param port Destination loopback port.
 * @return Connected socket or TEST_FD_INVALID.
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
 * Open one TCP listener and one accepted client connection.
 * @param out_listener Receives listener.
 * @param out_connection Receives accepted connection.
 * @param out_client Receives client socket.
 * @return Zero on success, otherwise nonzero.
 */
static int test_tcp_pair(
    kc_netl_t **out_listener,
    kc_netl_connection_t **out_connection,
    test_fd_t *out_client
) {
    kc_netl_options_t options;
    kc_netl_event_t event;
    kc_netl_t *listener = NULL;
    test_fd_t client = TEST_FD_INVALID;
    int rc;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    rc = kc_netl_open(&listener, &options);
    if (rc != KC_NETL_OK) return 1;

    client = test_tcp_connect(kc_netl_port(listener));
    if (client == TEST_FD_INVALID) {
        kc_netl_close(listener);
        return 1;
    }

    rc = kc_netl_poll(listener, &event, 2000);
    if (
        rc != KC_NETL_OK ||
        event.type != KC_NETL_EVENT_CONNECTION ||
        event.connection == NULL
    ) {
        TEST_CLOSE(client);
        kc_netl_close(listener);
        return 1;
    }

    *out_listener = listener;
    *out_connection = event.connection;
    *out_client = client;
    return 0;
}

/**
 * Read one expected byte sequence from a socket.
 * @param fd Socket descriptor.
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
    size_t used = 0U;

    while (used < expected_size) {
        int received = recv(
            fd,
            (char *)buffer + used,
            (int)(expected_size - used),
            0
        );
        if (received <= 0) return 1;
        used += (size_t)received;
    }

    return expect_bytes(
        "socket bytes",
        expected,
        expected_size,
        buffer,
        used
    );
}

/**
 * Test kc_netl_open.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_open(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = (kc_netl_t *)1;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    fail += expect_int(
        "NULL out",
        KC_NETL_EINVAL,
        kc_netl_open(NULL, &options)
    );
    fail += expect_int(
        "NULL options",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, NULL)
    );
    fail += expect_true("failed open clears output", listener == NULL);

    options.protocol = 99;
    fail += expect_int(
        "invalid protocol",
        KC_NETL_EINVAL,
        kc_netl_open(&listener, &options)
    );

    options.protocol = KC_NETL_TCP;
    fail += expect_int(
        "TCP open",
        KC_NETL_OK,
        kc_netl_open(&listener, &options)
    );
    fail += expect_true("listener allocated", listener != NULL);
    kc_netl_close(listener);

    case_result(fail, "kc_netl_open", "binds one incoming listener");
    return fail != 0;
}

/**
 * Test kc_netl_poll.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_poll(void) {
    kc_netl_options_t options;
    kc_netl_event_t event;
    kc_netl_t *listener = NULL;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;

    fail += expect_int(
        "open",
        KC_NETL_OK,
        kc_netl_open(&listener, &options)
    );
    fail += expect_int(
        "immediate timeout",
        KC_NETL_EAGAIN,
        kc_netl_poll(listener, &event, 0)
    );
    fail += expect_int(
        "NULL listener",
        KC_NETL_EINVAL,
        kc_netl_poll(NULL, &event, 0)
    );
    fail += expect_int(
        "NULL event",
        KC_NETL_EINVAL,
        kc_netl_poll(listener, NULL, 0)
    );
    fail += expect_int(
        "invalid timeout",
        KC_NETL_EINVAL,
        kc_netl_poll(listener, &event, -2)
    );

    kc_netl_close(listener);
    case_result(fail, "kc_netl_poll", "waits for one event without serializing clients");
    return fail != 0;
}

/**
 * Test kc_netl_send.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_send(void) {
    kc_netl_t *listener = NULL;
    kc_netl_connection_t *connection = NULL;
    test_fd_t client = TEST_FD_INVALID;
    size_t sent = 0U;
    int fail = 0;

    if (test_tcp_pair(&listener, &connection, &client) != 0) return 1;

    fail += expect_int(
        "send",
        KC_NETL_OK,
        kc_netl_send(connection, "pong", 4U, &sent)
    );
    fail += expect_true("sent bytes", sent == 4U);
    fail += test_socket_expect(client, "pong", 4U);
    fail += expect_int(
        "NULL connection",
        KC_NETL_EINVAL,
        kc_netl_send(NULL, "x", 1U, &sent)
    );

    TEST_CLOSE(client);
    kc_netl_close(listener);
    case_result(fail, "kc_netl_send", "writes to one specific TCP connection");
    return fail != 0;
}

/**
 * Test kc_netl_sendto.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_sendto(void) {
    kc_netl_options_t options;
    kc_netl_event_t event;
    kc_netl_t *listener = NULL;
    struct sockaddr_in address;
    test_fd_t client = TEST_FD_INVALID;
    char response[8];
    int received;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_UDP;
    if (kc_netl_open(&listener, &options) != KC_NETL_OK) return 1;

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

    if (
        sendto(
            client,
            "ping",
            4,
            0,
            (const struct sockaddr *)&address,
            (socklen_t)sizeof(address)
        ) != 4
    ) {
        TEST_CLOSE(client);
        kc_netl_close(listener);
        return 1;
    }

    fail += expect_int(
        "datagram event",
        KC_NETL_OK,
        kc_netl_poll(listener, &event, 2000)
    );
    fail += expect_int(
        "datagram type",
        KC_NETL_EVENT_DATAGRAM,
        event.type
    );
    fail += expect_bytes(
        "datagram bytes",
        "ping",
        4U,
        event.data,
        event.data_size
    );
    fail += expect_true(
        "datagram peer",
        event.host != NULL && event.port != 0U
    );

    fail += expect_int(
        "sendto reply",
        KC_NETL_OK,
        kc_netl_sendto(
            listener,
            event.host,
            event.port,
            "pong",
            4U
        )
    );
    received = recv(client, response, (int)sizeof(response), 0);
    fail += expect_int("UDP response size", 4, received);
    if (received == 4) {
        fail += expect_bytes("UDP response", "pong", 4U, response, 4U);
    }

    TEST_CLOSE(client);
    kc_netl_close(listener);
    case_result(fail, "kc_netl_sendto", "preserves datagram and peer identity");
    return fail != 0;
}

/**
 * Test kc_netl_connection_close.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_connection_close(void) {
    kc_netl_t *listener = NULL;
    kc_netl_connection_t *connection = NULL;
    test_fd_t client = TEST_FD_INVALID;
    size_t sent = 0U;
    int fail = 0;

    if (test_tcp_pair(&listener, &connection, &client) != 0) return 1;

    kc_netl_connection_close(connection);
    kc_netl_connection_close(connection);
    fail += expect_int(
        "send after close",
        KC_NETL_ECLOSED,
        kc_netl_send(connection, "x", 1U, &sent)
    );
    kc_netl_connection_close(NULL);

    TEST_CLOSE(client);
    kc_netl_close(listener);
    case_result(fail, "kc_netl_connection_close", "closes one client independently");
    return fail != 0;
}

/**
 * Test kc_netl_port.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_port(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = NULL;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;
    if (kc_netl_open(&listener, &options) != KC_NETL_OK) return 1;

    fail += expect_true("ephemeral port selected", kc_netl_port(listener) != 0U);
    fail += expect_true("NULL port", kc_netl_port(NULL) == 0U);

    kc_netl_close(listener);
    case_result(fail, "kc_netl_port", "reports the actual bound port");
    return fail != 0;
}

/**
 * Test kc_netl_close.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_close(void) {
    kc_netl_options_t options;
    kc_netl_t *listener = NULL;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;
    fail += expect_int(
        "open",
        KC_NETL_OK,
        kc_netl_open(&listener, &options)
    );
    kc_netl_close(listener);
    kc_netl_close(NULL);

    case_result(fail, "kc_netl_close", "releases listener and active connections");
    return fail != 0;
}

/**
 * Test multiple TCP clients remain independent and concurrent.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_concurrency(void) {
    kc_netl_options_t options;
    kc_netl_event_t event;
    kc_netl_t *listener = NULL;
    kc_netl_connection_t *connection_a = NULL;
    kc_netl_connection_t *connection_b = NULL;
    test_fd_t client_a = TEST_FD_INVALID;
    test_fd_t client_b = TEST_FD_INVALID;
    size_t sent = 0U;
    int saw_a = 0;
    int saw_b = 0;
    int i;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.host = "127.0.0.1";
    options.protocol = KC_NETL_TCP;
    if (kc_netl_open(&listener, &options) != KC_NETL_OK) return 1;

    client_a = test_tcp_connect(kc_netl_port(listener));
    client_b = test_tcp_connect(kc_netl_port(listener));
    if (client_a == TEST_FD_INVALID || client_b == TEST_FD_INVALID) {
        if (client_a != TEST_FD_INVALID) TEST_CLOSE(client_a);
        if (client_b != TEST_FD_INVALID) TEST_CLOSE(client_b);
        kc_netl_close(listener);
        return 1;
    }

    for (i = 0; i < 2; i++) {
        fail += expect_int(
            "accept concurrent client",
            KC_NETL_OK,
            kc_netl_poll(listener, &event, 2000)
        );
        fail += expect_int(
            "connection event",
            KC_NETL_EVENT_CONNECTION,
            event.type
        );
    }

    (void)send(client_a, "A", 1, 0);
    (void)send(client_b, "B", 1, 0);

    for (i = 0; i < 2; i++) {
        fail += expect_int(
            "client data event",
            KC_NETL_OK,
            kc_netl_poll(listener, &event, 2000)
        );
        fail += expect_int("data type", KC_NETL_EVENT_DATA, event.type);
        if (event.data_size == 1U &&
            ((const char *)event.data)[0] == 'A') {
            connection_a = event.connection;
            saw_a = 1;
        } else if (
            event.data_size == 1U &&
            ((const char *)event.data)[0] == 'B'
        ) {
            connection_b = event.connection;
            saw_b = 1;
        }
    }

    fail += expect_true("received client A", saw_a);
    fail += expect_true("received client B", saw_b);
    fail += expect_true(
        "connections have distinct identity",
        connection_a != NULL &&
        connection_b != NULL &&
        connection_a != connection_b
    );

    if (connection_a != NULL) {
        fail += expect_int(
            "targeted send",
            KC_NETL_OK,
            kc_netl_send(connection_a, "reply-a", 7U, &sent)
        );
        fail += expect_true("targeted send size", sent == 7U);
        fail += test_socket_expect(client_a, "reply-a", 7U);
        kc_netl_connection_close(connection_a);
        fail += expect_int(
            "closed A stays isolated",
            KC_NETL_ECLOSED,
            kc_netl_send(connection_a, "x", 1U, &sent)
        );
    }

    (void)send(client_b, "still", 5, 0);
    fail += expect_int(
        "B remains active",
        KC_NETL_OK,
        kc_netl_poll(listener, &event, 2000)
    );
    fail += expect_int("B data event", KC_NETL_EVENT_DATA, event.type);
    fail += expect_true("B identity retained", event.connection == connection_b);
    fail += expect_bytes("B bytes", "still", 5U, event.data, event.data_size);

    TEST_CLOSE(client_a);
    TEST_CLOSE(client_b);
    kc_netl_close(listener);
    case_result(fail, "kc_netl_concurrency", "keeps simultaneous clients independent");
    return fail != 0;
}

/**
 * Test kc_netl_strerror.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_strerror(void) {
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
        "EAGAIN",
        strcmp(kc_netl_strerror(KC_NETL_EAGAIN), "try again") == 0
    );
    fail += expect_true(
        "ECLOSED",
        strcmp(kc_netl_strerror(KC_NETL_ECLOSED), "connection closed") == 0
    );
    fail += expect_true(
        "ENOMEM",
        strcmp(kc_netl_strerror(KC_NETL_ENOMEM), "out of memory") == 0
    );

    case_result(fail, "kc_netl_strerror", "maps public status values");
    return fail != 0;
}

/**
 * Test kc_netl_version.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_version(void) {
    int fail = 0;

    (void)kc_netl_version();
    case_result(fail, "kc_netl_version", "returns the compiled build version");
    return fail != 0;
}

/**
 * Resolve the staged CLI path beside the test executable.
 * @param out Destination path.
 * @param cap Destination capacity.
 * @return Zero on success, otherwise nonzero.
 */
static int test_cli_path(char *out, size_t cap) {
    const char *slash;
    const char *backslash;
    const char *separator;
    size_t dir_size;
    int written;

    if (test_program_path == NULL) return 1;
    slash = strrchr(test_program_path, '/');
    backslash = strrchr(test_program_path, '\\');
    separator = slash;
    if (
        backslash != NULL &&
        (separator == NULL || backslash > separator)
    ) {
        separator = backslash;
    }

    if (separator == NULL) {
#ifdef _WIN32
        written = snprintf(out, cap, ".\\%s", TEST_CLI_NAME);
#else
        written = snprintf(out, cap, "./%s", TEST_CLI_NAME);
#endif
        return written > 0 && (size_t)written < cap ? 0 : 1;
    }

    dir_size = (size_t)(separator - test_program_path + 1);
    if (dir_size + strlen(TEST_CLI_NAME) + 1U > cap) return 1;
    memcpy(out, test_program_path, dir_size);
    memcpy(
        out + dir_size,
        TEST_CLI_NAME,
        strlen(TEST_CLI_NAME) + 1U
    );
    return 0;
}

/**
 * Read one file into an allocated buffer.
 * @param path File path.
 * @param out Receives allocated bytes.
 * @param out_size Receives byte count.
 * @return Zero on success, otherwise nonzero.
 */
static int test_read_file(
    const char *path,
    unsigned char **out,
    size_t *out_size
) {
    FILE *file;
    long end;
    unsigned char *data;
    size_t size;

    *out = NULL;
    *out_size = 0U;
    file = fopen(path, "rb");
    if (file == NULL) return 1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }

    size = (size_t)end;
    data = (unsigned char *)malloc(size != 0U ? size : 1U);
    if (data == NULL) {
        fclose(file);
        return 1;
    }
    if (size != 0U && fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return 1;
    }

    fclose(file);
    *out = data;
    *out_size = size;
    return 0;
}

/**
 * Run one simple CLI invocation and capture stdout and stderr.
 * @param arg Optional single CLI argument.
 * @param out Receives stdout bytes.
 * @param out_size Receives stdout size.
 * @param err Receives stderr bytes.
 * @param err_size Receives stderr size.
 * @return Child process exit code, or -1 for harness failure.
 */
static int test_cli_run(
    const char *arg,
    unsigned char **out,
    size_t *out_size,
    unsigned char **err,
    size_t *err_size
) {
    char cli[1024];
    char out_path[128];
    char err_path[128];
    long pid = (long)TEST_GETPID();
    int rc;

    *out = NULL;
    *out_size = 0U;
    *err = NULL;
    *err_size = 0U;
    if (test_cli_path(cli, sizeof(cli)) != 0) return -1;

    snprintf(out_path, sizeof(out_path), "netl-cli-%ld-out.tmp", pid);
    snprintf(err_path, sizeof(err_path), "netl-cli-%ld-err.tmp", pid);

#ifdef _WIN32
    {
        const char *argv[3];
        int out_fd;
        int err_fd;
        int save_out;
        int save_err;

        argv[0] = cli;
        argv[1] = arg != NULL && arg[0] != '\0' ? arg : NULL;
        argv[2] = NULL;

        out_fd = _open(
            out_path,
            _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
            _S_IREAD | _S_IWRITE
        );
        err_fd = _open(
            err_path,
            _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
            _S_IREAD | _S_IWRITE
        );
        if (out_fd < 0 || err_fd < 0) {
            if (out_fd >= 0) _close(out_fd);
            if (err_fd >= 0) _close(err_fd);
            return -1;
        }

        save_out = _dup(1);
        save_err = _dup(2);
        if (save_out < 0 || save_err < 0) {
            if (save_out >= 0) _close(save_out);
            if (save_err >= 0) _close(save_err);
            _close(out_fd);
            _close(err_fd);
            return -1;
        }

        fflush(stdout);
        fflush(stderr);
        _dup2(out_fd, 1);
        _dup2(err_fd, 2);
        _close(out_fd);
        _close(err_fd);
        rc = (int)_spawnv(_P_WAIT, cli, (const char * const *)argv);
        fflush(stdout);
        fflush(stderr);
        _dup2(save_out, 1);
        _dup2(save_err, 2);
        _close(save_out);
        _close(save_err);
    }
#else
    {
        char command[4096];

        if (arg != NULL && arg[0] != '\0') {
            snprintf(
                command,
                sizeof(command),
                "\"%s\" %s > \"%s\" 2> \"%s\"",
                cli,
                arg,
                out_path,
                err_path
            );
        } else {
            snprintf(
                command,
                sizeof(command),
                "\"%s\" > \"%s\" 2> \"%s\"",
                cli,
                out_path,
                err_path
            );
        }
        rc = system(command);
    }
#endif

    if (
        test_read_file(out_path, out, out_size) != 0 ||
        test_read_file(err_path, err, err_size) != 0
    ) {
        free(*out);
        free(*err);
        *out = NULL;
        *err = NULL;
        *out_size = 0U;
        *err_size = 0U;
        rc = -1;
    }

    remove(out_path);
    remove(err_path);
    return rc;
}

/**
 * Test the shipped CLI surface as one grouped contract.
 * @return Zero on success, otherwise nonzero.
 */
static int case_kc_netl_cli(void) {
    unsigned char *out = NULL;
    unsigned char *err = NULL;
    size_t out_size = 0U;
    size_t err_size = 0U;
    int rc;
    int fail = 0;

    rc = test_cli_run("--version", &out, &out_size, &err, &err_size);
    fail += expect_int("CLI version exit", 0, rc);
    fail += expect_contains("CLI version", out, out_size, "netl build ");
    free(out);
    free(err);

    out = NULL;
    err = NULL;
    rc = test_cli_run("--help", &out, &out_size, &err, &err_size);
    fail += expect_int("CLI help exit", 0, rc);
    fail += expect_contains("CLI help", out, out_size, "Usage:");
    free(out);
    free(err);

    out = NULL;
    err = NULL;
    rc = test_cli_run("", &out, &out_size, &err, &err_size);
    fail += expect_true("CLI missing args fails", rc != 0);
    fail += expect_contains("CLI missing args usage", out, out_size, "Usage:");
    free(out);
    free(err);

    case_result(fail, "kc_netl_cli", "covers direct listener CLI help and version");
    return fail != 0;
}

/**
 * Run all contract cases.
 * @return Zero on success, otherwise nonzero.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 11;
    test_case_current = 0;
    run_case(&rc, case_kc_netl_open);
    run_case(&rc, case_kc_netl_poll);
    run_case(&rc, case_kc_netl_send);
    run_case(&rc, case_kc_netl_sendto);
    run_case(&rc, case_kc_netl_connection_close);
    run_case(&rc, case_kc_netl_port);
    run_case(&rc, case_kc_netl_close);
    run_case(&rc, case_kc_netl_concurrency);
    run_case(&rc, case_kc_netl_strerror);
    run_case(&rc, case_kc_netl_version);
    run_case(&rc, case_kc_netl_cli);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    test_program_path = argv[0];

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_netl_open") == 0) return case_kc_netl_open();
    if (strcmp(argv[1], "kc_netl_poll") == 0) return case_kc_netl_poll();
    if (strcmp(argv[1], "kc_netl_send") == 0) return case_kc_netl_send();
    if (strcmp(argv[1], "kc_netl_sendto") == 0) return case_kc_netl_sendto();
    if (strcmp(argv[1], "kc_netl_connection_close") == 0) {
        return case_kc_netl_connection_close();
    }
    if (strcmp(argv[1], "kc_netl_port") == 0) return case_kc_netl_port();
    if (strcmp(argv[1], "kc_netl_close") == 0) return case_kc_netl_close();
    if (strcmp(argv[1], "kc_netl_concurrency") == 0) {
        return case_kc_netl_concurrency();
    }
    if (strcmp(argv[1], "kc_netl_strerror") == 0) {
        return case_kc_netl_strerror();
    }
    if (strcmp(argv[1], "kc_netl_version") == 0) {
        return case_kc_netl_version();
    }
    if (strcmp(argv[1], "kc_netl_cli") == 0) return case_kc_netl_cli();

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
