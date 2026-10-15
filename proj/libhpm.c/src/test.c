/**
 * test.c - libhpm public API contract tests.
 * Summary: Validates direct and borrowed-UDP paths, datagrams, and lifecycle.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libhpm.h"

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

static int test_case_total = 0;
static int test_case_current = 0;

typedef struct {
    kc_hpm_endpoint_t peer;
    kc_hpm_options_t options;
    kc_hpm_path_t *path;
    int status;
} test_side_t;

/**
 * Prints a test case result.
 * @param fail Failure flag.
 * @param name Test case name.
 * @param detail Test case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs a test case and increments the counter.
 * @param rc Output failure count.
 * @param fn Test function to run.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void))
{
    test_case_current++;
    *rc += fn();
}

/**
 * Checks a condition and reports failure.
 * @param name Test name.
 * @param condition Condition to check.
 * @return 0 on pass, 1 on fail.
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
 * Reports a skipped test case.
 * @param name Test case name.
 * @return None.
 */
static void test_skip(const char *name)
{
    printf("[SKIP] %s\n", name);
}

#ifdef _WIN32
/**
 * Thread function for connect test (Windows).
 * @param arg Test side data.
 * @return 0.
 */
static DWORD WINAPI test_connect_thread(LPVOID arg)
#else
/**
 * Thread function for connect test (POSIX).
 * @param arg Test side data.
 * @return NULL.
 */
static void *test_connect_thread(void *arg)
#endif
{
    test_side_t *side = (test_side_t *)arg;
    side->status = kc_hpm_connect(&side->path, &side->options);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Initializes socket runtime (Windows).
 * @return 1 on success, 0 on failure.
 */
static int test_socket_runtime_start(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return 1;
#endif
}

/**
 * Cleans up socket runtime (Windows).
 * @return None.
 */
static void test_socket_runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Gets an ephemeral port for testing.
 * @param family Address family (AF_INET or AF_INET6).
 * @return Port number, or 0 on failure.
 */
static unsigned short test_ephemeral_port(int family)
{
    test_socket_t fd = TEST_INVALID_SOCKET;
    unsigned short port = 0;

    if (!test_socket_runtime_start()) return 0;
    fd = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == TEST_INVALID_SOCKET) goto done;

    if (family == AF_INET) {
        struct sockaddr_in addr;
        test_socklen_t addr_len = (test_socklen_t)sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fd, (const struct sockaddr *)&addr, addr_len) != 0) goto done;
        if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) goto done;
        port = ntohs(addr.sin_port);
    } else if (family == AF_INET6) {
        struct sockaddr_in6 addr;
        test_socklen_t addr_len = (test_socklen_t)sizeof(addr);
        int v6only = 1;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = 0;
        if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1) goto done;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char *)&v6only, (test_socklen_t)sizeof(v6only)) != 0)
            goto done;
        if (bind(fd, (const struct sockaddr *)&addr, addr_len) != 0) goto done;
        if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) goto done;
        port = ntohs(addr.sin6_port);
    }

done:
    if (fd != TEST_INVALID_SOCKET) test_socket_close(fd);
    test_socket_runtime_stop();
    return port;
}

/**
 * Gets a pair of distinct ephemeral ports.
 * @param family Address family.
 * @param a Output first port.
 * @param b Output second port.
 * @return 1 on success, 0 on failure.
 */
static int test_port_pair(int family, unsigned short *a, unsigned short *b)
{
    unsigned attempt;
    if (!a || !b) return 0;
    *a = 0;
    *b = 0;
    for (attempt = 0; attempt < 16U; ++attempt) {
        *a = test_ephemeral_port(family);
        *b = test_ephemeral_port(family);
        if (*a != 0 && *b != 0 && *a != *b) return 1;
    }
    *a = 0;
    *b = 0;
    return 0;
}

/**
 * Starts a pair of connect threads and waits for completion.
 * @param a Test side A.
 * @param b Test side B.
 * @param a_path Output path A.
 * @param b_path Output path B.
 * @return 0 on success, 1 on failure.
 */
static int test_start_pair(test_side_t *a, test_side_t *b,
    kc_hpm_path_t **a_path, kc_hpm_path_t **b_path)
{
    test_thread_t a_thread;
    test_thread_t b_thread;
    int started_a = 0;
    int started_b = 0;

#ifdef _WIN32
    b_thread = CreateThread(NULL, 0, test_connect_thread, b, 0, NULL);
    started_b = b_thread != NULL;
    a_thread = CreateThread(NULL, 0, test_connect_thread, a, 0, NULL);
    started_a = a_thread != NULL;
    if (started_a) WaitForSingleObject(a_thread, INFINITE);
    if (started_b) WaitForSingleObject(b_thread, INFINITE);
    if (started_a) CloseHandle(a_thread);
    if (started_b) CloseHandle(b_thread);
#else
    started_b = pthread_create(&b_thread, NULL, test_connect_thread, b) == 0;
    started_a = pthread_create(&a_thread, NULL, test_connect_thread, a) == 0;
    if (started_a) pthread_join(a_thread, NULL);
    if (started_b) pthread_join(b_thread, NULL);
#endif

    if (started_a && started_b && a->status == KC_HPM_OK &&
        b->status == KC_HPM_OK && a->path && b->path) {
        *a_path = a->path;
        *b_path = b->path;
        return 0;
    }
    kc_hpm_close(a->path);
    kc_hpm_close(b->path);
    return 1;
}

/**
 * Establishes a pair of connected paths for testing (IPv4 with bad peer).
 * @param a_path Output path A.
 * @param b_path Output path B.
 * @return 0 on success, 1 on failure.
 */
static int test_pair(kc_hpm_path_t **a_path, kc_hpm_path_t **b_path)
{
    unsigned attempt;

    *a_path = NULL;
    *b_path = NULL;
    for (attempt = 0; attempt < 8U; ++attempt) {
        test_side_t a;
        test_side_t b;
        kc_hpm_endpoint_t a_peers[2];
        unsigned short a_port;
        unsigned short b_port;
        unsigned short bad_port;

        if (!test_port_pair(AF_INET, &a_port, &b_port)) return 1;
        bad_port = test_ephemeral_port(AF_INET);
        if (bad_port == 0 || bad_port == a_port || bad_port == b_port) continue;

        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a_peers[0].host = "127.0.0.1";
        a_peers[0].port = bad_port;
        a_peers[1].host = "127.0.0.1";
        a_peers[1].port = b_port;
        a.options.host = "127.0.0.1";
        a.options.port = a_port;
        a.options.peers = a_peers;
        a.options.peer_count = 2;
        a.options.timeout_ms = 1500;
        b.peer.host = "127.0.0.1";
        b.peer.port = a_port;
        b.options.host = "127.0.0.1";
        b.options.port = b_port;
        b.options.peers = &b.peer;
        b.options.peer_count = 1;
        b.options.timeout_ms = 1500;

        if (test_start_pair(&a, &b, a_path, b_path) == 0) return 0;
    }
    return 1;
}

/**
 * Establishes a pair of connected paths for testing (IPv6 loopback).
 * @param a_path Output path A.
 * @param b_path Output path B.
 * @return 0 on success, 1 on failure, 2 if IPv6 unavailable.
 */
static int test_pair_ipv6(kc_hpm_path_t **a_path, kc_hpm_path_t **b_path)
{
    unsigned attempt;

    *a_path = NULL;
    *b_path = NULL;
    for (attempt = 0; attempt < 4U; ++attempt) {
        test_side_t a;
        test_side_t b;
        unsigned short a_port;
        unsigned short b_port;

        if (!test_port_pair(AF_INET6, &a_port, &b_port)) return 2;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a.peer.host = "::1";
        a.peer.port = b_port;
        a.options.host = "::1";
        a.options.port = a_port;
        a.options.peers = &a.peer;
        a.options.peer_count = 1;
        a.options.timeout_ms = 1500;
        b.peer.host = "::1";
        b.peer.port = a_port;
        b.options.host = "::1";
        b.options.port = b_port;
        b.options.peers = &b.peer;
        b.options.peer_count = 1;
        b.options.timeout_ms = 1500;

        if (test_start_pair(&a, &b, a_path, b_path) == 0) return 0;
    }
    return 1;
}

/**
 * Establishes a pair of connected paths for testing (mixed IPv4/IPv6).
 * @param a_path Output path A.
 * @param b_path Output path B.
 * @return 0 on success, 1 on failure, 2 if IPv6 unavailable.
 */
static int test_pair_mixed(kc_hpm_path_t **a_path, kc_hpm_path_t **b_path)
{
    unsigned attempt;

    *a_path = NULL;
    *b_path = NULL;
    if (test_ephemeral_port(AF_INET6) == 0) return 2;

    for (attempt = 0; attempt < 4U; ++attempt) {
        test_side_t a;
        test_side_t b;
        kc_hpm_endpoint_t a_peers[2];
        unsigned short a_port;
        unsigned short b_port;
        unsigned short dead_v6_port;

        if (!test_port_pair(AF_INET, &a_port, &b_port)) return 1;
        dead_v6_port = test_ephemeral_port(AF_INET6);
        if (dead_v6_port == 0) return 2;

        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a_peers[0].host = "::1";
        a_peers[0].port = dead_v6_port;
        a_peers[1].host = "127.0.0.1";
        a_peers[1].port = b_port;
        a.options.host = NULL;
        a.options.port = a_port;
        a.options.peers = a_peers;
        a.options.peer_count = 2;
        a.options.timeout_ms = 1500;
        b.peer.host = "127.0.0.1";
        b.peer.port = a_port;
        b.options.host = "127.0.0.1";
        b.options.port = b_port;
        b.options.peers = &b.peer;
        b.options.peer_count = 1;
        b.options.timeout_ms = 1500;

        if (test_start_pair(&a, &b, a_path, b_path) == 0) return 0;
    }
    return 1;
}

/**
 * Test case for kc_hpm_connect.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_connect(void)
{
    const char *name = "kc_hpm_connect";
    const char *detail = "establishes a direct UDP path from supplied endpoints";
    kc_hpm_path_t *a = NULL;
    kc_hpm_path_t *b = NULL;
    kc_hpm_options_t invalid;
    kc_hpm_endpoint_t peer;
    kc_hpm_options_t timeout_options;
    unsigned short timeout_port;
    int ipv6_rc;
    int mixed_rc;
    int fail = 0;

    memset(&invalid, 0, sizeof(invalid));
    fail += expect_true("rejects NULL destination",
        kc_hpm_connect(NULL, &invalid) == KC_HPM_EINVAL);
    fail += expect_true("rejects NULL options",
        kc_hpm_connect(&a, NULL) == KC_HPM_EINVAL);
    fail += expect_true("rejects empty peer set",
        kc_hpm_connect(&a, &invalid) == KC_HPM_EINVAL);
    peer.host = NULL;
    peer.port = 1;
    invalid.peers = &peer;
    invalid.peer_count = 1;
    fail += expect_true("rejects invalid peer",
        kc_hpm_connect(&a, &invalid) == KC_HPM_ENET);
    peer.host = "not-a-valid-hpm-host.invalid";
    fail += expect_true("rejects invalid host",
        kc_hpm_connect(&a, &invalid) == KC_HPM_ENET);
    peer.host = "127.0.0.1";
    peer.port = 0;
    fail += expect_true("rejects invalid port",
        kc_hpm_connect(&a, &invalid) == KC_HPM_ENET);

    timeout_port = test_ephemeral_port(AF_INET);
    memset(&timeout_options, 0, sizeof(timeout_options));
    peer.port = timeout_port;
    timeout_options.host = "127.0.0.1";
    timeout_options.peers = &peer;
    timeout_options.peer_count = 1;
    timeout_options.timeout_ms = 30;
    a = (kc_hpm_path_t *)(uintptr_t)1;
    fail += expect_true("unreachable peer times out", timeout_port != 0 &&
        kc_hpm_connect(&a, &timeout_options) == KC_HPM_ETIMEOUT);
    fail += expect_true("failure clears path", a == NULL);

    fail += expect_true("IPv4 loopback pair establishes", test_pair(&a, &b) == 0);
    if (a && b) {
        fail += expect_true("A reports local host", kc_hpm_local_host(a) != NULL);
        fail += expect_true("A reports local port", kc_hpm_local_port(a) != 0);
        fail += expect_true("A reports peer host", kc_hpm_peer_host(a) != NULL);
        fail += expect_true("A reports peer port", kc_hpm_peer_port(a) != 0);
        fail += expect_true("A selected B endpoint",
            kc_hpm_peer_port(a) == kc_hpm_local_port(b));
        fail += expect_true("B selected A endpoint",
            kc_hpm_peer_port(b) == kc_hpm_local_port(a));
    }
    kc_hpm_close(a);
    kc_hpm_close(b);
    a = NULL;
    b = NULL;

    ipv6_rc = test_pair_ipv6(&a, &b);
    if (ipv6_rc == 2) {
        test_skip("IPv6 loopback unavailable");
    } else {
        fail += expect_true("IPv6 loopback pair establishes", ipv6_rc == 0);
    }
    kc_hpm_close(a);
    kc_hpm_close(b);
    a = NULL;
    b = NULL;

    mixed_rc = test_pair_mixed(&a, &b);
    if (mixed_rc == 2) {
        test_skip("mixed IPv4/IPv6 requires IPv6 loopback");
    } else {
        fail += expect_true("mixed-family list reaches viable IPv4 peer",
            mixed_rc == 0 && a && b &&
            kc_hpm_peer_port(a) == kc_hpm_local_port(b));
    }
    kc_hpm_close(a);
    kc_hpm_close(b);

    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Test case for kc_hpm_datagrams.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_datagrams(void)
{
    const char *name = "kc_hpm_datagrams";
    const char *detail = "sends and receives UDP datagrams through the path";
    kc_hpm_path_t *a = NULL;
    kc_hpm_path_t *b = NULL;
    void *data = NULL;
    size_t size = 0;
    unsigned char binary[] = { 0, 1, 0, 255 };
    unsigned char control_like[16] = {
        'K', 'C', 'H', 'P', 'M', '1', 1, 1,
        0, 0, 0, 0, 0, 0, 0, 1
    };
    unsigned char stale_control[16] = {
        'K', 'C', 'H', 'P', 'M', '1', 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    unsigned char large[32768];
    unsigned i;
    int fail = 0;

    fail += expect_true("loopback pair establishes", test_pair(&a, &b) == 0);
    if (a && b) {
        fail += expect_true("send succeeds",
            kc_hpm_send(a, "hello", 5) == KC_HPM_OK);
        fail += expect_true("receive succeeds",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK);
        fail += expect_true("received size matches", size == 5);
        fail += expect_true("received bytes match",
            data && size == 5 && memcmp(data, "hello", 5) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("control-sized application datagram succeeds",
            kc_hpm_send(a, control_like, sizeof(control_like)) == KC_HPM_OK);
        fail += expect_true("control-like application datagram is delivered",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            size == sizeof(control_like) &&
            memcmp(data, control_like, sizeof(control_like)) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("stale control packet can be sent",
            kc_hpm_send(a, stale_control, sizeof(stale_control)) == KC_HPM_OK);
        fail += expect_true("application data after stale control can be sent",
            kc_hpm_send(a, "after-stale", 11) == KC_HPM_OK);
        fail += expect_true("stale control does not leak to application",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            size == 11 && memcmp(data, "after-stale", 11) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("reverse send succeeds",
            kc_hpm_send(b, binary, sizeof(binary)) == KC_HPM_OK);
        fail += expect_true("reverse binary receive succeeds",
            kc_hpm_receive(a, &data, &size, 1000) == KC_HPM_OK);
        fail += expect_true("binary datagram is preserved", data &&
            size == sizeof(binary) && memcmp(data, binary, sizeof(binary)) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("empty datagram succeeds",
            kc_hpm_send(a, NULL, 0) == KC_HPM_OK);
        fail += expect_true("empty datagram is preserved",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            data && size == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        for (i = 0; i < sizeof(large); ++i) large[i] = (unsigned char)i;
        fail += expect_true("large datagram succeeds",
            kc_hpm_send(a, large, sizeof(large)) == KC_HPM_OK);
        fail += expect_true("large datagram boundary is preserved",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            size == sizeof(large) && memcmp(data, large, sizeof(large)) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("datagram above API maximum is rejected",
            kc_hpm_send(a, large, 65536U) == KC_HPM_EINVAL);
        fail += expect_true("first consecutive datagram succeeds",
            kc_hpm_send(a, "one", 3) == KC_HPM_OK);
        fail += expect_true("second consecutive datagram succeeds",
            kc_hpm_send(a, "two", 3) == KC_HPM_OK);
        fail += expect_true("first datagram remains separate",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            size == 3 && memcmp(data, "one", 3) == 0);
        kc_hpm_free(data);
        data = NULL;
        fail += expect_true("second datagram remains separate",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK &&
            size == 3 && memcmp(data, "two", 3) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;

        fail += expect_true("invalid send data is rejected",
            kc_hpm_send(a, NULL, 1) == KC_HPM_EINVAL);
        fail += expect_true("invalid receive output is rejected",
            kc_hpm_receive(a, NULL, &size, 1) == KC_HPM_EINVAL);
        fail += expect_true("receive timeout is observable",
            kc_hpm_receive(b, &data, &size, 20) == KC_HPM_ETIMEOUT);
        fail += expect_true("timeout clears output", data == NULL && size == 0);
    }
    kc_hpm_free(data);
    kc_hpm_close(a);
    kc_hpm_close(b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Test case for kc_hpm_lifecycle.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_lifecycle(void)
{
    const char *name = "kc_hpm_lifecycle";
    const char *detail = "reports errors, version, and accepts NULL cleanup";
    kc_hpm_path_t *first_a = NULL;
    kc_hpm_path_t *first_b = NULL;
    kc_hpm_path_t *second_a = NULL;
    kc_hpm_path_t *second_b = NULL;
    int fail = 0;

    kc_hpm_close(NULL);
    kc_hpm_free(NULL);
    fail += expect_true("version is nonzero", kc_hpm_version() != 0U);
    fail += expect_true("known status has message",
        strcmp(kc_hpm_strerror(KC_HPM_ETIMEOUT), "connection timed out") == 0);
    fail += expect_true("all public statuses have messages",
        kc_hpm_strerror(KC_HPM_OK) && kc_hpm_strerror(KC_HPM_EINVAL) &&
        kc_hpm_strerror(KC_HPM_ENET) && kc_hpm_strerror(KC_HPM_ENOMEM) &&
        kc_hpm_strerror(KC_HPM_ECLOSED));
    fail += expect_true("unknown status has message",
        kc_hpm_strerror(-999) != NULL);
    fail += expect_true("closed path rejects send",
        kc_hpm_send(NULL, "x", 1) == KC_HPM_ECLOSED);
    fail += expect_true("closed path rejects receive",
        kc_hpm_receive(NULL, NULL, NULL, 1) == KC_HPM_EINVAL);

    fail += expect_true("first independent pair establishes",
        test_pair(&first_a, &first_b) == 0);
    kc_hpm_close(first_a);
    kc_hpm_close(first_b);
    fail += expect_true("second independent pair establishes",
        test_pair(&second_a, &second_b) == 0);
    fail += expect_true("second pair has current endpoint",
        second_a && second_b && kc_hpm_local_port(second_a) != 0 &&
        kc_hpm_peer_port(second_a) == kc_hpm_local_port(second_b));
    kc_hpm_close(second_a);
    kc_hpm_close(second_b);

    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

typedef struct {
    test_socket_t fd;
    char host[64];
    unsigned short port;
    unsigned release_count;
#ifdef _WIN32
    int wsa_started;
#endif
} test_over_udp_t;

typedef struct {
    kc_hpm_path_t *path;
    kc_hpm_options_t options;
    kc_hpm_udp_t udp;
    int status;
} test_over_side_t;

/**
 * Opens a caller-owned UDP mapping for connect-over testing.
 * @return 1 on success, 0 on failure.
 */
static int test_over_open(test_over_udp_t *udp)
{
    struct sockaddr_in addr;
    test_socklen_t len = (test_socklen_t)sizeof(addr);

    memset(udp, 0, sizeof(*udp));
    udp->fd = TEST_INVALID_SOCKET;
    if (!test_socket_runtime_start()) return 0;
#ifdef _WIN32
    udp->wsa_started = 1;
#endif
    udp->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp->fd == TEST_INVALID_SOCKET) goto fail;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(udp->fd, (const struct sockaddr *)&addr, len) != 0 ||
        getsockname(udp->fd, (struct sockaddr *)&addr, &len) != 0)
        goto fail;
    strcpy(udp->host, "127.0.0.1");
    udp->port = ntohs(addr.sin_port);
    return udp->port != 0;

fail:
    if (udp->fd != TEST_INVALID_SOCKET) test_socket_close(udp->fd);
#ifdef _WIN32
    if (udp->wsa_started) test_socket_runtime_stop();
#endif
    memset(udp, 0, sizeof(*udp));
    udp->fd = TEST_INVALID_SOCKET;
    return 0;
}

/**
 * Closes a caller-owned UDP mapping used by connect-over testing.
 * @return None.
 */
static void test_over_close(test_over_udp_t *udp)
{
    if (!udp) return;
    if (udp->fd != TEST_INVALID_SOCKET) test_socket_close(udp->fd);
#ifdef _WIN32
    if (udp->wsa_started) test_socket_runtime_stop();
#endif
    memset(udp, 0, sizeof(*udp));
    udp->fd = TEST_INVALID_SOCKET;
}

/**
 * Sends one datagram through a caller-owned UDP mapping.
 * @return KC_HPM_OK on success, error code on failure.
 */
static int test_over_send(void *context, const kc_hpm_endpoint_t *peer,
    const void *data, size_t size)
{
    test_over_udp_t *udp = (test_over_udp_t *)context;
    struct sockaddr_in addr;
    int sent;

    if (!udp || udp->fd == TEST_INVALID_SOCKET || !peer || !peer->host ||
        peer->port == 0 || (size > 0 && !data) || size > 65535U)
        return KC_HPM_EINVAL;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer->port);
    if (inet_pton(AF_INET, peer->host, &addr.sin_addr) != 1)
        return KC_HPM_ENET;
    sent = (int)sendto(udp->fd, (const char *)data, (int)size, 0,
        (const struct sockaddr *)&addr, (test_socklen_t)sizeof(addr));
    return sent == (int)size ? KC_HPM_OK : KC_HPM_ENET;
}

/**
 * Receives one datagram through a caller-owned UDP mapping.
 * @return KC_HPM_OK on success, error code on failure.
 */
static int test_over_receive(void *context, kc_hpm_datagram_t *out,
    uint64_t timeout_ms)
{
    test_over_udp_t *udp = (test_over_udp_t *)context;
    struct sockaddr_in from;
    test_socklen_t from_len = (test_socklen_t)sizeof(from);
    unsigned char packet[65535];
    fd_set set;
    struct timeval tv;
    struct timeval *tvp = NULL;
    int ready;
    int received;

    if (!udp || udp->fd == TEST_INVALID_SOCKET || !out) return KC_HPM_EINVAL;
    memset(out, 0, sizeof(*out));
    FD_ZERO(&set);
    FD_SET(udp->fd, &set);
    if (timeout_ms != 0) {
        tv.tv_sec = (long)(timeout_ms / 1000ULL);
        tv.tv_usec = (long)((timeout_ms % 1000ULL) * 1000ULL);
        tvp = &tv;
    }
#ifdef _WIN32
    ready = select(0, &set, NULL, NULL, tvp);
#else
    ready = select(udp->fd + 1, &set, NULL, NULL, tvp);
#endif
    if (ready == 0) return KC_HPM_ETIMEOUT;
    if (ready < 0) return KC_HPM_ENET;
    received = (int)recvfrom(udp->fd, (char *)packet, (int)sizeof(packet), 0,
        (struct sockaddr *)&from, &from_len);
    if (received < 0) return KC_HPM_ENET;
    out->host = (char *)malloc(10U);
    out->data = malloc(received > 0 ? (size_t)received : 1U);
    if (!out->host || !out->data) {
        free(out->host);
        free(out->data);
        memset(out, 0, sizeof(*out));
        return KC_HPM_ENOMEM;
    }
    strcpy(out->host, "127.0.0.1");
    out->port = ntohs(from.sin_port);
    if (received > 0) memcpy(out->data, packet, (size_t)received);
    out->data_size = (size_t)received;
    return KC_HPM_OK;
}

/**
 * Releases a datagram allocated by the caller-owned UDP test mapping.
 * @return None.
 */
static void test_over_release(void *context, kc_hpm_datagram_t *packet)
{
    test_over_udp_t *udp = (test_over_udp_t *)context;
    if (!packet) return;
    free(packet->host);
    free(packet->data);
    memset(packet, 0, sizeof(*packet));
    if (udp) udp->release_count++;
}

#ifdef _WIN32
/**
 * Connect-over thread entry point for Windows.
 * @return Thread exit code.
 */
static DWORD WINAPI test_over_connect_thread(LPVOID arg)
#else
/**
 * Connect-over thread entry point for POSIX.
 * @return NULL.
 */
static void *test_over_connect_thread(void *arg)
#endif
{
    test_over_side_t *side = (test_over_side_t *)arg;
    side->status = kc_hpm_connect_over(&side->path, &side->options, &side->udp);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Establishes a pair of paths over caller-owned UDP mappings.
 * @return 1 on success, 0 on failure.
 */
static int test_over_pair(test_over_side_t *a, test_over_side_t *b)
{
    test_thread_t ta;
    test_thread_t tb;
    int started_a = 0;
    int started_b = 0;

#ifdef _WIN32
    tb = CreateThread(NULL, 0, test_over_connect_thread, b, 0, NULL);
    started_b = tb != NULL;
    ta = CreateThread(NULL, 0, test_over_connect_thread, a, 0, NULL);
    started_a = ta != NULL;
    if (started_a) WaitForSingleObject(ta, INFINITE);
    if (started_b) WaitForSingleObject(tb, INFINITE);
    if (started_a) CloseHandle(ta);
    if (started_b) CloseHandle(tb);
#else
    started_b = pthread_create(&tb, NULL, test_over_connect_thread, b) == 0;
    started_a = pthread_create(&ta, NULL, test_over_connect_thread, a) == 0;
    if (started_a) pthread_join(ta, NULL);
    if (started_b) pthread_join(tb, NULL);
#endif
    return started_a && started_b && a->status == KC_HPM_OK &&
        b->status == KC_HPM_OK && a->path && b->path;
}

/**
 * Tests kc_hpm_connect_over with caller-owned UDP mappings.
 * @return 0 on pass, 1 on failure.
 */
static int case_kc_hpm_connect_over(void)
{
    const char *name = "kc_hpm_connect_over";
    const char *detail = "preserves caller-owned UDP mappings";
    test_over_udp_t au;
    test_over_udp_t bu;
    test_over_side_t a;
    test_over_side_t b;
    kc_hpm_endpoint_t a_peer;
    kc_hpm_endpoint_t b_peer;
    kc_hpm_datagram_t raw;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;

    memset(&au, 0, sizeof(au));
    memset(&bu, 0, sizeof(bu));
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    au.fd = TEST_INVALID_SOCKET;
    bu.fd = TEST_INVALID_SOCKET;
    if (!test_over_open(&au) || !test_over_open(&bu)) {
        fail = 1;
        goto done;
    }
    a_peer.host = bu.host;
    a_peer.port = bu.port;
    b_peer.host = au.host;
    b_peer.port = au.port;
    a.options.peers = &a_peer;
    a.options.peer_count = 1;
    a.options.timeout_ms = 1500;
    b.options.peers = &b_peer;
    b.options.peer_count = 1;
    b.options.timeout_ms = 1500;
    a.udp.context = &au;
    a.udp.local = a_peer;
    a.udp.local.host = au.host;
    a.udp.local.port = au.port;
    a.udp.send = test_over_send;
    a.udp.receive = test_over_receive;
    a.udp.release = test_over_release;
    b.udp.context = &bu;
    b.udp.local = b_peer;
    b.udp.local.host = bu.host;
    b.udp.local.port = bu.port;
    b.udp.send = test_over_send;
    b.udp.receive = test_over_receive;
    b.udp.release = test_over_release;

    fail += expect_true("rejects NULL borrowed output",
        kc_hpm_connect_over(NULL, &a.options, &a.udp) == KC_HPM_EINVAL);
    fail += expect_true("borrowed UDP pair establishes", test_over_pair(&a, &b));
    if (!fail) {
        fail += expect_true("path preserves supplied local mapping",
            kc_hpm_local_port(a.path) == au.port);
        fail += expect_true("path selects supplied remote endpoint",
            kc_hpm_peer_port(a.path) == bu.port);
        fail += expect_true("received transport datagrams are released",
            au.release_count != 0 && bu.release_count != 0);
        fail += expect_true("path sends application datagram",
            kc_hpm_send(a.path, "over", 4) == KC_HPM_OK);
        fail += expect_true("path receives application datagram",
            kc_hpm_receive(b.path, &data, &size, 1000) == KC_HPM_OK &&
            size == 4 && data && memcmp(data, "over", 4) == 0);
        kc_hpm_free(data);
        data = NULL;
    }
    kc_hpm_close(a.path);
    kc_hpm_close(b.path);
    a.path = NULL;
    b.path = NULL;
    memset(&raw, 0, sizeof(raw));
    fail += expect_true("closing path preserves borrowed mapping",
        test_over_send(&au, &a_peer, "raw", 3) == KC_HPM_OK &&
        test_over_receive(&bu, &raw, 1000) == KC_HPM_OK &&
        raw.data_size == 3 && raw.data && memcmp(raw.data, "raw", 3) == 0);
    test_over_release(&bu, &raw);

done:
    kc_hpm_free(data);
    kc_hpm_close(a.path);
    kc_hpm_close(b.path);
    test_over_close(&au);
    test_over_close(&bu);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs all test cases.
 * @return Number of failures.
 */
static int case_all(void)
{
    int rc = 0;
    test_case_total = 4;
    test_case_current = 0;
    run_case(&rc, case_kc_hpm_connect);
    run_case(&rc, case_kc_hpm_datagrams);
    run_case(&rc, case_kc_hpm_lifecycle);
    run_case(&rc, case_kc_hpm_connect_over);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hpm_connect") == 0) return case_kc_hpm_connect();
    if (strcmp(argv[1], "kc_hpm_datagrams") == 0) return case_kc_hpm_datagrams();
    if (strcmp(argv[1], "kc_hpm_lifecycle") == 0) return case_kc_hpm_lifecycle();
    if (strcmp(argv[1], "kc_hpm_connect_over") == 0)
        return case_kc_hpm_connect_over();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
