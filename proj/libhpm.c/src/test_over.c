/**
 * test_over.c - libhpm borrowed UDP contract tests.
 * Summary: Validates direct paths over caller-owned live UDP mappings.
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
typedef SOCKET test_socket_t;
typedef int test_socklen_t;
typedef HANDLE test_thread_t;
#define TEST_INVALID_SOCKET INVALID_SOCKET
#define test_socket_close closesocket
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_socket_t;
typedef socklen_t test_socklen_t;
typedef pthread_t test_thread_t;
#define TEST_INVALID_SOCKET (-1)
#define test_socket_close close
#endif

typedef struct {
    test_socket_t fd;
    char host[64];
    unsigned short port;
    unsigned release_count;
#ifdef _WIN32
    int wsa_started;
#endif
} test_udp_t;

typedef struct {
    kc_hpm_path_t *path;
    kc_hpm_options_t options;
    kc_hpm_udp_t udp;
    int status;
} test_side_t;

static int runtime_start(void)
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return 1;
#endif
}

static void runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static int udp_open(test_udp_t *udp)
{
    struct sockaddr_in addr;
    test_socklen_t len = (test_socklen_t)sizeof(addr);

    memset(udp, 0, sizeof(*udp));
    udp->fd = TEST_INVALID_SOCKET;
    if (!runtime_start()) return 0;
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
    if (udp->wsa_started) WSACleanup();
#endif
    memset(udp, 0, sizeof(*udp));
    udp->fd = TEST_INVALID_SOCKET;
    return 0;
}

static void udp_close(test_udp_t *udp)
{
    if (!udp) return;
    if (udp->fd != TEST_INVALID_SOCKET) {
        test_socket_close(udp->fd);
        udp->fd = TEST_INVALID_SOCKET;
    }
#ifdef _WIN32
    if (udp->wsa_started) {
        udp->wsa_started = 0;
        WSACleanup();
    }
#endif
}

static int udp_send(void *context, const kc_hpm_endpoint_t *peer,
    const void *data, size_t size)
{
    test_udp_t *udp = (test_udp_t *)context;
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

static int udp_receive(void *context, kc_hpm_datagram_t *out,
    uint64_t timeout_ms)
{
    test_udp_t *udp = (test_udp_t *)context;
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

static void udp_release(void *context, kc_hpm_datagram_t *packet)
{
    test_udp_t *udp = (test_udp_t *)context;
    if (!packet) return;
    free(packet->host);
    free(packet->data);
    memset(packet, 0, sizeof(*packet));
    if (udp) udp->release_count++;
}

#ifdef _WIN32
static DWORD WINAPI connect_thread(LPVOID arg)
#else
static void *connect_thread(void *arg)
#endif
{
    test_side_t *side = (test_side_t *)arg;
    side->status = kc_hpm_connect_over(&side->path, &side->options, &side->udp);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int connect_pair(test_side_t *a, test_side_t *b)
{
    test_thread_t ta;
    test_thread_t tb;
    int started_a = 0;
    int started_b = 0;
#ifdef _WIN32
    tb = CreateThread(NULL, 0, connect_thread, b, 0, NULL);
    started_b = tb != NULL;
    ta = CreateThread(NULL, 0, connect_thread, a, 0, NULL);
    started_a = ta != NULL;
    if (started_a) WaitForSingleObject(ta, INFINITE);
    if (started_b) WaitForSingleObject(tb, INFINITE);
    if (started_a) CloseHandle(ta);
    if (started_b) CloseHandle(tb);
#else
    started_b = pthread_create(&tb, NULL, connect_thread, b) == 0;
    started_a = pthread_create(&ta, NULL, connect_thread, a) == 0;
    if (started_a) pthread_join(ta, NULL);
    if (started_b) pthread_join(tb, NULL);
#endif
    return started_a && started_b && a->status == KC_HPM_OK &&
        b->status == KC_HPM_OK && a->path && b->path;
}

static int check(int condition, const char *message)
{
    if (condition) return 0;
    fprintf(stderr, "[FAIL] %s\n", message);
    return 1;
}

int main(void)
{
    test_udp_t au;
    test_udp_t bu;
    test_side_t a;
    test_side_t b;
    kc_hpm_endpoint_t a_peer;
    kc_hpm_endpoint_t b_peer;
    kc_hpm_datagram_t raw;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;

    memset(&au, 0, sizeof(au));
    memset(&bu, 0, sizeof(bu));
    au.fd = TEST_INVALID_SOCKET;
    bu.fd = TEST_INVALID_SOCKET;
    if (!udp_open(&au) || !udp_open(&bu)) {
        fprintf(stderr, "[FAIL] could not open test UDP mappings\n");
        udp_close(&au);
        udp_close(&bu);
        return 1;
    }

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
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
    a.udp.local.host = au.host;
    a.udp.local.port = au.port;
    a.udp.send = udp_send;
    a.udp.receive = udp_receive;
    a.udp.release = udp_release;
    b.udp.context = &bu;
    b.udp.local.host = bu.host;
    b.udp.local.port = bu.port;
    b.udp.send = udp_send;
    b.udp.receive = udp_receive;
    b.udp.release = udp_release;

    fail += check(kc_hpm_connect_over(NULL, &a.options, &a.udp) == KC_HPM_EINVAL,
        "NULL output is rejected");
    fail += check(connect_pair(&a, &b), "borrowed UDP pair establishes");
    if (!fail) {
        fail += check(kc_hpm_local_port(a.path) == au.port,
            "path preserves supplied local mapping");
        fail += check(kc_hpm_peer_port(a.path) == bu.port,
            "path selects supplied remote endpoint");
        fail += check(au.release_count != 0 && bu.release_count != 0,
            "received transport datagrams are released");
        fail += check(kc_hpm_send(a.path, "over", 4) == KC_HPM_OK,
            "path sends application datagram");
        fail += check(kc_hpm_receive(b.path, &data, &size, 1000) == KC_HPM_OK &&
            size == 4 && data && memcmp(data, "over", 4) == 0,
            "path receives application datagram");
        kc_hpm_free(data);
        data = NULL;
        size = 0;
    }

    kc_hpm_close(a.path);
    kc_hpm_close(b.path);
    a.path = NULL;
    b.path = NULL;

    fail += check(udp_send(&au, &a_peer, "raw", 3) == KC_HPM_OK,
        "closing HPM path does not close borrowed mapping");
    memset(&raw, 0, sizeof(raw));
    fail += check(udp_receive(&bu, &raw, 1000) == KC_HPM_OK &&
        raw.data_size == 3 && raw.data && memcmp(raw.data, "raw", 3) == 0,
        "borrowed mapping remains usable after path close");
    udp_release(&bu, &raw);

    kc_hpm_free(data);
    kc_hpm_close(a.path);
    kc_hpm_close(b.path);
    udp_close(&au);
    udp_close(&bu);
    runtime_stop();

    if (fail) return 1;
    printf("[PASS] kc_hpm_connect_over: caller-owned UDP mapping preserved\n");
    return 0;
}
