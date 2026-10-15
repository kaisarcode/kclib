/**
 * test.c - libstun public API contract tests.
 * Summary: Validates STUN discovery, preserved mappings, datagrams, and lifecycle.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libstun.h"

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
typedef HANDLE test_thread_t;
typedef SOCKET test_socket_t;
typedef int test_socklen_t;
#define TEST_INVALID_SOCKET INVALID_SOCKET
#define test_socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef pthread_t test_thread_t;
typedef int test_socket_t;
typedef socklen_t test_socklen_t;
#define TEST_INVALID_SOCKET (-1)
#define test_socket_close close
#endif

#define TEST_STUN_MAGIC 0x2112A442U

static int test_case_total = 5;
static int test_case_current = 0;

typedef struct {
    test_socket_t fd;
    int family;
    int responses;
} test_server_t;

/**
 * Stores one network-order u16 in a test packet.
 * @return void
 */
static void put16(unsigned char *out, size_t off, unsigned short v)
{
    out[off] = (unsigned char)(v >> 8);
    out[off + 1U] = (unsigned char)v;
}

/**
 * Loads one network-order u16 from a test packet.
 * @return The loaded unsigned short value.
 */
static unsigned short get16(const unsigned char *in, size_t off)
{
    return (unsigned short)(((unsigned short)in[off] << 8) | in[off + 1U]);
}

/**
 * Starts the test socket runtime.
 * @return 1 on success, 0 on failure.
 */
static int socket_runtime_start(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return 1;
#endif
}

/**
 * Stops the test socket runtime.
 * @return void
 */
static void socket_runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Waits for a test socket to become readable.
 * @return 1 if readable, 0 otherwise.
 */
static int wait_readable(test_socket_t fd, unsigned timeout_ms)
{
    fd_set readfds;
    struct timeval tv;
    int rc;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = (long)(timeout_ms / 1000U);
    tv.tv_usec = (long)((timeout_ms % 1000U) * 1000U);
#ifdef _WIN32
    rc = select(0, &readfds, NULL, NULL, &tv);
#else
    rc = select(fd + 1, &readfds, NULL, NULL, &tv);
#endif
    return rc > 0;
}

/**
 * Binds one IPv4 or IPv6 loopback UDP socket.
 * @return The bound socket or TEST_INVALID_SOCKET on failure.
 */
static test_socket_t bind_loopback(int family, unsigned short *out_port)
{
    test_socket_t fd;
    if (!out_port) return TEST_INVALID_SOCKET;
    *out_port = 0;
    fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == TEST_INVALID_SOCKET) return TEST_INVALID_SOCKET;
    if (family == AF_INET) {
        struct sockaddr_in addr;
        test_socklen_t len = (test_socklen_t)sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, (const struct sockaddr *)&addr, len) != 0 ||
            getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
            test_socket_close(fd);
            return TEST_INVALID_SOCKET;
        }
        *out_port = ntohs(addr.sin_port);
    } else {
        struct sockaddr_in6 addr;
        test_socklen_t len = (test_socklen_t)sizeof(addr);
        int v6only = 1;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char *)&v6only, (test_socklen_t)sizeof(v6only)) != 0 ||
            bind(fd, (const struct sockaddr *)&addr, len) != 0 ||
            getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
            test_socket_close(fd);
            return TEST_INVALID_SOCKET;
        }
        *out_port = ntohs(addr.sin6_port);
    }
    return fd;
}

/**
 * Builds a Binding Success response that reflects the request source.
 * @return Number of bytes written, or 0 on failure.
 */
static size_t build_response(const unsigned char *request, size_t request_size,
    const struct sockaddr_storage *peer, unsigned char *out, size_t cap)
{
    unsigned char address[16];
    unsigned char mask[16];
    unsigned short port;
    size_t n;
    size_t attr_len;
    size_t total;
    size_t i;

    if (!request || request_size < 20U || !peer || !out) return 0;
    if (peer->ss_family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)peer;
        memcpy(address, &v4->sin_addr, 4);
        port = ntohs(v4->sin_port);
        n = 4;
    } else if (peer->ss_family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)peer;
        memcpy(address, &v6->sin6_addr, 16);
        port = ntohs(v6->sin6_port);
        n = 16;
    } else {
        return 0;
    }
    attr_len = 4U + n;
    total = 24U + attr_len;
    if (cap < total) return 0;
    memset(out, 0, total);
    put16(out, 0, 0x0101);
    put16(out, 2, (unsigned short)(4U + attr_len));
    out[4] = 0x21; out[5] = 0x12; out[6] = 0xa4; out[7] = 0x42;
    memcpy(out + 8, request + 8, 12);
    put16(out, 20, 0x0020);
    put16(out, 22, (unsigned short)attr_len);
    out[24] = 0;
    out[25] = peer->ss_family == AF_INET ? 0x01 : 0x02;
    port ^= (unsigned short)(TEST_STUN_MAGIC >> 16);
    put16(out, 26, port);
    mask[0] = 0x21; mask[1] = 0x12; mask[2] = 0xa4; mask[3] = 0x42;
    memcpy(mask + 4, request + 8, 12);
    for (i = 0; i < n; ++i) out[28 + i] = address[i] ^ mask[i];
    return total;
}

/**
 * Runs the local STUN responder.
 * @return Thread exit code (Windows: DWORD, POSIX: void*).
 */
#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID arg)
#else
static void *server_thread(void *arg)
#endif
{
    test_server_t *server = (test_server_t *)arg;
    int i;
    for (i = 0; i < server->responses; ++i) {
        unsigned char request[512];
        unsigned char response[512];
        struct sockaddr_storage peer;
        test_socklen_t peer_len = (test_socklen_t)sizeof(peer);
        int received;
        size_t response_size;
#ifdef _WIN32
        received = recvfrom(server->fd, (char *)request, (int)sizeof(request), 0,
            (struct sockaddr *)&peer, &peer_len);
#else
        received = (int)recvfrom(server->fd, request, sizeof(request), 0,
            (struct sockaddr *)&peer, &peer_len);
#endif
        if (received < 20) break;
        response_size = build_response(request, (size_t)received, &peer,
            response, sizeof(response));
        if (!response_size) break;
#ifdef _WIN32
        (void)sendto(server->fd, (const char *)response, (int)response_size, 0,
            (const struct sockaddr *)&peer, peer_len);
#else
        (void)sendto(server->fd, response, response_size, 0,
            (const struct sockaddr *)&peer, peer_len);
#endif
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Starts one local STUN responder thread.
 * @return 1 on success, 0 on failure.
 */
static int thread_start(test_thread_t *thread, test_server_t *server)
{
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, server_thread, server, 0, NULL);
    return *thread != NULL;
#else
    return pthread_create(thread, NULL, server_thread, server) == 0;
#endif
}

/**
 * Waits for one local STUN responder thread.
 * @return void
 */
static void thread_join(test_thread_t thread)
{
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
}

/**
 * Reports one failed test assertion.
 * @return 1 on failure, 0 on success.
 */
static int expect_true(const char *name, int condition)
{
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Prints one contract-test case result.
 * @return void
 */
static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Validates live IPv4 discovery, refresh, send, and receive behavior.
 * @return 1 on failure, 0 on success.
 */
static int case_discover_ipv4(void)
{
    const char *name = "kc_stun_discover";
    const char *detail = "discovers and preserves an IPv4 UDP mapping";
    test_server_t server;
    test_thread_t thread;
    unsigned short server_port = 0;
    unsigned short peer_port = 0;
    char server_text[64];
    kc_stun_options_t options;
    kc_stun_mapping_t *mapping = NULL;
    kc_stun_endpoint_t peer_endpoint;
    kc_stun_datagram_t datagram;
    test_socket_t peer = TEST_INVALID_SOCKET;
    struct sockaddr_in mapping_addr;
    const char outbound[] = "out";
    const char inbound[] = "in";
    char peer_buffer[16];
    int received;
    int fail = 0;

    memset(&server, 0, sizeof(server));
    server.fd = bind_loopback(AF_INET, &server_port);
    server.family = AF_INET;
    server.responses = 2;
    if (server.fd == TEST_INVALID_SOCKET || !thread_start(&thread, &server)) {
        if (server.fd != TEST_INVALID_SOCKET) test_socket_close(server.fd);
        case_result(1, name, "could not start local STUN server");
        return 1;
    }

    snprintf(server_text, sizeof(server_text), "%s:%u", "127.0.0.1", server_port);
    memset(&options, 0, sizeof(options));
    options.server = server_text;
    options.host = "127.0.0.1";
    options.timeout_ms = 1000;
    fail += expect_true("IPv4 discovery succeeds",
        kc_stun_discover(&mapping, &options) == KC_STUN_OK && mapping != NULL);
    if (mapping) {
        fail += expect_true("local host is loopback",
            kc_stun_local_host(mapping) &&
            strcmp(kc_stun_local_host(mapping), "127.0.0.1") == 0);
        fail += expect_true("mapped host is loopback",
            kc_stun_mapped_host(mapping) &&
            strcmp(kc_stun_mapped_host(mapping), "127.0.0.1") == 0);
        fail += expect_true("mapped port matches preserved local mapping",
            kc_stun_mapped_port(mapping) == kc_stun_local_port(mapping));

        peer = bind_loopback(AF_INET, &peer_port);
        fail += expect_true("peer socket created", peer != TEST_INVALID_SOCKET);
        if (peer != TEST_INVALID_SOCKET) {
            peer_endpoint.host = "127.0.0.1";
            peer_endpoint.port = peer_port;
            fail += expect_true("send uses preserved mapping",
                kc_stun_send(mapping, &peer_endpoint, outbound,
                    sizeof(outbound) - 1U) == KC_STUN_OK);
            if (wait_readable(peer, 1000)) {
                received = (int)recvfrom(peer, peer_buffer, sizeof(peer_buffer), 0,
                    NULL, NULL);
            } else {
                received = -1;
            }
            fail += expect_true("peer receives mapping datagram",
                received == (int)(sizeof(outbound) - 1U) &&
                memcmp(peer_buffer, outbound, sizeof(outbound) - 1U) == 0);

            fail += expect_true("refresh request sent",
                kc_stun_refresh(mapping) == KC_STUN_OK);
            memset(&mapping_addr, 0, sizeof(mapping_addr));
            mapping_addr.sin_family = AF_INET;
            mapping_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            mapping_addr.sin_port = htons(kc_stun_local_port(mapping));
#ifdef _WIN32
            (void)sendto(peer, inbound, (int)(sizeof(inbound) - 1U), 0,
                (const struct sockaddr *)&mapping_addr,
                (int)sizeof(mapping_addr));
#else
            (void)sendto(peer, inbound, sizeof(inbound) - 1U, 0,
                (const struct sockaddr *)&mapping_addr,
                (socklen_t)sizeof(mapping_addr));
#endif
            memset(&datagram, 0, sizeof(datagram));
            fail += expect_true("receive filters STUN and returns peer datagram",
                kc_stun_receive(mapping, &datagram, 1000) == KC_STUN_OK &&
                datagram.host && strcmp(datagram.host, "127.0.0.1") == 0 &&
                datagram.port == peer_port &&
                datagram.data_size == sizeof(inbound) - 1U &&
                memcmp(datagram.data, inbound, sizeof(inbound) - 1U) == 0);
            kc_stun_datagram_free(&datagram);
        }
    }

    if (peer != TEST_INVALID_SOCKET) test_socket_close(peer);
    kc_stun_close(mapping);
    thread_join(thread);
    test_socket_close(server.fd);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Validates IPv6 mapping discovery when IPv6 loopback is available.
 * @return 1 on failure, 0 on success.
 */
static int case_discover_ipv6(void)
{
    const char *name = "IPv6 discovery";
    const char *detail = "discovers an IPv6 loopback mapping when available";
    test_server_t server;
    test_thread_t thread;
    unsigned short server_port = 0;
    char server_text[80];
    kc_stun_options_t options;
    kc_stun_mapping_t *mapping = NULL;
    int fail = 0;

    memset(&server, 0, sizeof(server));
    server.fd = bind_loopback(AF_INET6, &server_port);
    if (server.fd == TEST_INVALID_SOCKET) {
        printf("[SKIP] %s\n", name);
        case_result(0, name, detail);
        return 0;
    }
    server.family = AF_INET6;
    server.responses = 1;
    if (!thread_start(&thread, &server)) {
        test_socket_close(server.fd);
        case_result(1, name, "could not start local IPv6 STUN server");
        return 1;
    }
    snprintf(server_text, sizeof(server_text), "[::1]:%u", server_port);
    memset(&options, 0, sizeof(options));
    options.server = server_text;
    options.host = "::1";
    options.timeout_ms = 1000;
    fail += expect_true("IPv6 discovery succeeds",
        kc_stun_discover(&mapping, &options) == KC_STUN_OK && mapping != NULL);
    if (mapping) {
        fail += expect_true("mapped IPv6 host is loopback",
            kc_stun_mapped_host(mapping) &&
            strcmp(kc_stun_mapped_host(mapping), "::1") == 0);
        fail += expect_true("IPv6 mapped port matches local port",
            kc_stun_mapped_port(mapping) == kc_stun_local_port(mapping));
    }
    kc_stun_close(mapping);
    thread_join(thread);
    test_socket_close(server.fd);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Validates the autonomous STUN Binding listener and its wire response.
 * @return 1 on failure, 0 on success.
 */
static int case_listen(void)
{
    const char *name = "kc_stun_listen";
    const char *detail = "serves UDP Binding requests on a live endpoint";
    kc_stun_server_options_t server_options;
    kc_stun_server_t *server = NULL;
    kc_stun_mapping_t *mapping = NULL;
    kc_stun_options_t options;
    test_socket_t client = TEST_INVALID_SOCKET;
    unsigned short client_port = 0;
    unsigned short server_port = 0;
    unsigned char request[20];
    unsigned char response[64];
    struct sockaddr_in destination;
    char server_text[64];
    int received = -1;
    int fail = 0;
    size_t i;

    memset(&server_options, 0, sizeof(server_options));
    fail += expect_true("NULL listener output rejected",
        kc_stun_listen(NULL, &server_options) == KC_STUN_EINVAL);
    fail += expect_true("NULL listener options rejected",
        kc_stun_listen(&server, NULL) == KC_STUN_EINVAL);
    fail += expect_true("NULL listener reports no port",
        kc_stun_server_port(NULL) == 0);

    server_options.host = NULL;
    server_options.port = 0;
    fail += expect_true("listener starts",
        kc_stun_listen(&server, &server_options) == KC_STUN_OK && server != NULL);
    if (!server) {
        case_result(1, name, detail);
        return 1;
    }
    server_port = kc_stun_server_port(server);
    fail += expect_true("listener reports assigned port", server_port != 0);

    client = bind_loopback(AF_INET, &client_port);
    fail += expect_true("raw client created", client != TEST_INVALID_SOCKET);
    if (client != TEST_INVALID_SOCKET) {
        memset(request, 0, sizeof(request));
        put16(request, 0, 0x0001);
        request[4] = 0x21; request[5] = 0x12; request[6] = 0xa4; request[7] = 0x42;
        for (i = 0; i < 12U; ++i) request[8U + i] = (unsigned char)(i + 1U);
        memset(&destination, 0, sizeof(destination));
        destination.sin_family = AF_INET;
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        destination.sin_port = htons(server_port);
#ifdef _WIN32
        (void)sendto(client, (const char *)request, (int)sizeof(request), 0,
            (const struct sockaddr *)&destination, (int)sizeof(destination));
#else
        (void)sendto(client, request, sizeof(request), 0,
            (const struct sockaddr *)&destination, (socklen_t)sizeof(destination));
#endif
        if (wait_readable(client, 1000)) {
#ifdef _WIN32
            received = recvfrom(client, (char *)response, (int)sizeof(response), 0,
                NULL, NULL);
#else
            received = (int)recvfrom(client, response, sizeof(response), 0,
                NULL, NULL);
#endif
        }
        fail += expect_true("raw Binding response received", received == 32);
        if (received == 32) {
            unsigned short mapped_port = (unsigned short)(get16(response, 26) ^
                (TEST_STUN_MAGIC >> 16));
            unsigned char mapped[4];
            mapped[0] = response[28] ^ 0x21;
            mapped[1] = response[29] ^ 0x12;
            mapped[2] = response[30] ^ 0xa4;
            mapped[3] = response[31] ^ 0x42;
            fail += expect_true("Binding response type is success",
                get16(response, 0) == 0x0101 && get16(response, 2) == 12);
            fail += expect_true("Binding response preserves transaction id",
                memcmp(response + 8, request + 8, 12) == 0);
            fail += expect_true("Binding response contains XOR-MAPPED-ADDRESS",
                get16(response, 20) == 0x0020 && get16(response, 22) == 8 &&
                response[24] == 0 && response[25] == 1);
            fail += expect_true("Binding response reflects source endpoint",
                mapped_port == client_port && mapped[0] == 127 && mapped[1] == 0 &&
                mapped[2] == 0 && mapped[3] == 1);
        }
    }

    snprintf(server_text, sizeof(server_text), "%s:%u", "127.0.0.1", server_port);
    memset(&options, 0, sizeof(options));
    options.server = server_text;
    options.host = "127.0.0.1";
    options.timeout_ms = 1000;
    fail += expect_true("library discovery works against library listener",
        kc_stun_discover(&mapping, &options) == KC_STUN_OK && mapping != NULL);
    if (mapping) {
        fail += expect_true("listener reports discovery source mapping",
            kc_stun_mapped_host(mapping) &&
            strcmp(kc_stun_mapped_host(mapping), "127.0.0.1") == 0 &&
            kc_stun_mapped_port(mapping) == kc_stun_local_port(mapping));
    }
    kc_stun_close(mapping);
    mapping = NULL;

    {
        unsigned short probe_port = 0;
        test_socket_t probe = bind_loopback(AF_INET6, &probe_port);
        if (probe != TEST_INVALID_SOCKET) {
            test_socket_close(probe);
            snprintf(server_text, sizeof(server_text), "[::1]:%u", server_port);
            memset(&options, 0, sizeof(options));
            options.server = server_text;
            options.host = "::1";
            options.timeout_ms = 1000;
            fail += expect_true("wildcard listener accepts IPv6 Binding",
                kc_stun_discover(&mapping, &options) == KC_STUN_OK && mapping != NULL);
            kc_stun_close(mapping);
            mapping = NULL;
        }
    }

    if (client != TEST_INVALID_SOCKET) test_socket_close(client);
    kc_stun_server_close(server);
    kc_stun_server_close(NULL);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Validates invalid arguments and bounded discovery timeout behavior.
 * @return 1 on failure, 0 on success.
 */
static int case_errors(void)
{
    const char *name = "errors and timeout";
    const char *detail = "rejects invalid input and bounds discovery";
    kc_stun_mapping_t *mapping = NULL;
    kc_stun_options_t options;
    unsigned short port = 0;
    test_socket_t silent = TEST_INVALID_SOCKET;
    char server_text[64];
    int fail = 0;

    memset(&options, 0, sizeof(options));
    fail += expect_true("NULL output rejected",
        kc_stun_discover(NULL, &options) == KC_STUN_EINVAL);
    fail += expect_true("NULL options rejected",
        kc_stun_discover(&mapping, NULL) == KC_STUN_EINVAL);
    options.server = ":";
    fail += expect_true("malformed server rejected",
        kc_stun_discover(&mapping, &options) == KC_STUN_EINVAL);

    silent = bind_loopback(AF_INET, &port);
    if (silent != TEST_INVALID_SOCKET) {
        snprintf(server_text, sizeof(server_text), "%s:%u", "127.0.0.1", port);
        memset(&options, 0, sizeof(options));
        options.server = server_text;
        options.host = "127.0.0.1";
        options.timeout_ms = 50;
        fail += expect_true("silent STUN server times out",
            kc_stun_discover(&mapping, &options) == KC_STUN_ETIMEOUT &&
            mapping == NULL);
        test_socket_close(silent);
    }
    fail += expect_true("NULL refresh rejected",
        kc_stun_refresh(NULL) == KC_STUN_EINVAL);
    fail += expect_true("NULL send rejected",
        kc_stun_send(NULL, NULL, NULL, 0) == KC_STUN_EINVAL);
    fail += expect_true("NULL receive rejected",
        kc_stun_receive(NULL, NULL, 0) == KC_STUN_EINVAL);
    kc_stun_close(NULL);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Validates public status strings and the generated build version.
 * @return 1 on failure, 0 on success.
 */
static int case_metadata(void)
{
    const char *name = "metadata";
    const char *detail = "exposes stable status text and build version";
    int fail = 0;
    fail += expect_true("success status text",
        strcmp(kc_stun_strerror(KC_STUN_OK), "ok") == 0);
    fail += expect_true("protocol status text",
        strcmp(kc_stun_strerror(KC_STUN_EPROTO), "invalid STUN response") == 0);
    fail += expect_true("unknown status text",
        strcmp(kc_stun_strerror(999), "unknown status") == 0);
    fail += expect_true("version available", kc_stun_version() != 0);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs the portable public API contract test suite.
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char **argv)
{
    int rc = 0;
    (void)argc;
    (void)argv;
    if (!socket_runtime_start()) {
        fprintf(stderr, "socket runtime unavailable\n");
        return 1;
    }
    test_case_current++; rc += case_discover_ipv4();
    test_case_current++; rc += case_discover_ipv6();
    test_case_current++; rc += case_listen();
    test_case_current++; rc += case_errors();
    test_case_current++; rc += case_metadata();
    socket_runtime_stop();
    return rc ? 1 : 0;
}
