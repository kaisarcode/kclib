/**
 * libpunch.c - Punch direct UDP paths.
 * Summary: Establishes direct UDP connectivity against supplied peer endpoints.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libpunch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef HANDLE punch_live_thread_t;
typedef CRITICAL_SECTION punch_live_mutex_t;
typedef CONDITION_VARIABLE punch_live_cond_t;
typedef SOCKET punch_socket_t;
typedef int punch_socklen_t;
#define PUNCH_INVALID_SOCKET INVALID_SOCKET
#define punch_socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef pthread_t punch_live_thread_t;
typedef pthread_mutex_t punch_live_mutex_t;
typedef pthread_cond_t punch_live_cond_t;
typedef int punch_socket_t;
typedef socklen_t punch_socklen_t;
#define PUNCH_INVALID_SOCKET (-1)
#define punch_socket_close close
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef KC_PUNCH_BUILD_VERSION
#define KC_PUNCH_BUILD_VERSION 0
#endif

#define PUNCH_DEFAULT_TIMEOUT_MS 5000ULL
#define PUNCH_PROBE_INTERVAL_MS 100ULL
#define PUNCH_PACKET_SIZE 16U
#define PUNCH_PACKET_HELLO 1U
#define PUNCH_PACKET_ACK 2U
#define PUNCH_MAX_DATAGRAM 65535U

static const unsigned char punch_magic[6] = { 'K', 'C', 'H', 'P', 'M', '1' };

typedef struct {
    struct sockaddr_storage addr;
    punch_socklen_t addr_len;
    int socket_index;
    int saw_hello;
    int saw_ack;
    uint64_t peer_nonce;
} punch_candidate_t;

typedef struct punch_base_path punch_base_path_t;

struct punch_base_path {
    punch_socket_t fd;
    char local_host[NI_MAXHOST];
    unsigned short local_port;
    char peer_host[NI_MAXHOST];
    unsigned short peer_port;
    uint64_t local_nonce;
    uint64_t peer_nonce;
    struct sockaddr_storage peer_addr;
    punch_socklen_t peer_addr_len;
    int borrowed_udp;
    kc_punch_udp_t udp;
#ifdef _WIN32
    int wsa_started;
#endif
};

/**
 * Returns the current monotonic time in milliseconds.
 * @return Current time in milliseconds, or 0 on failure.
 */
static uint64_t punch_now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

/**
 * Adds a timeout to a monotonic timestamp without wrapping.
 * @param now Current monotonic time.
 * @param timeout_ms Timeout duration.
 * @return Saturated deadline.
 */
static uint64_t punch_deadline(uint64_t now, uint64_t timeout_ms)
{
    if (UINT64_MAX - now < timeout_ms) return UINT64_MAX;
    return now + timeout_ms;
}

/**
 * Generates a pseudo-random nonce.
 * @return Non-zero nonce value.
 */
static uint64_t punch_nonce(void)
{
    uint64_t x = punch_now_ms();
#ifdef _WIN32
    x ^= (uint64_t)GetCurrentProcessId() << 32;
#else
    x ^= (uint64_t)getpid() << 32;
#endif
    x ^= (uint64_t)(uintptr_t)&x;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x ? x : 1ULL;
}

/**
 * Stores a 64-bit value in big-endian format.
 * @param out Output buffer (8 bytes).
 * @param value Value to store.
 * @return None.
 */
static void punch_store64be(unsigned char out[8], uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8U; ++i) {
        out[7U - i] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

/**
 * Loads a 64-bit value from big-endian format.
 * @param in Input buffer (8 bytes).
 * @return Decoded value.
 */
static uint64_t punch_load64be(const unsigned char in[8])
{
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8U; ++i) value = (value << 8) | in[i];
    return value;
}

/**
 * Creates a PUNCH packet.
 * @param packet Output buffer (PUNCH_PACKET_SIZE bytes).
 * @param type Packet type (HELLO or ACK).
 * @param nonce Nonce value.
 * @return None.
 */
static void punch_make_packet(unsigned char packet[PUNCH_PACKET_SIZE],
    unsigned type, uint64_t nonce)
{
    memcpy(packet, punch_magic, sizeof(punch_magic));
    packet[6] = (unsigned char)type;
    packet[7] = 0;
    punch_store64be(packet + 8, nonce);
}

/**
 * Parses a PUNCH packet.
 * @param data Packet data.
 * @param size Packet size.
 * @param type Output packet type.
 * @param nonce Output nonce value.
 * @return 1 on success, 0 on failure.
 */
static int punch_parse_packet(const void *data, size_t size,
    unsigned *type, uint64_t *nonce)
{
    const unsigned char *packet = (const unsigned char *)data;
    if (!packet || size != PUNCH_PACKET_SIZE || !type || !nonce) return 0;
    if (memcmp(packet, punch_magic, sizeof(punch_magic)) != 0 || packet[7] != 0)
        return 0;
    if (packet[6] != PUNCH_PACKET_HELLO && packet[6] != PUNCH_PACKET_ACK)
        return 0;
    *type = packet[6];
    *nonce = punch_load64be(packet + 8);
    return 1;
}

/**
 * Waits for a socket to become readable.
 * @param fd Socket to wait on.
 * @param timeout_ms Timeout in milliseconds.
 * @return 1 if readable, 0 on timeout, -1 on error.
 */
static int punch_wait_readable(punch_socket_t fd, uint64_t timeout_ms)
{
    fd_set readfds;
    struct timeval tv;
    int rc;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = (long)(timeout_ms / 1000ULL);
    tv.tv_usec = (long)((timeout_ms % 1000ULL) * 1000ULL);
#ifdef _WIN32
    rc = select(0, &readfds, NULL, NULL, &tv);
#else
    rc = select(fd + 1, &readfds, NULL, NULL, &tv);
#endif
    if (rc > 0) return 1;
    if (rc == 0) return 0;
#ifdef _WIN32
    if (WSAGetLastError() == WSAEINTR) return 0;
#else
    if (errno == EINTR) return 0;
#endif
    return -1;
}

/**
 * Waits for either of two sockets to become readable.
 * @param first First socket.
 * @param second Second socket.
 * @param timeout_ms Timeout in milliseconds.
 * @param out_mask Readiness bit mask (bit 0 = first, bit 1 = second).
 * @return 1 if readable, 0 on timeout, -1 on error.
 */
static int punch_wait_any(punch_socket_t first, punch_socket_t second,
    uint64_t timeout_ms, int *out_mask)
{
    fd_set readfds;
    struct timeval tv;
    int rc;
#ifndef _WIN32
    punch_socket_t maximum = PUNCH_INVALID_SOCKET;
#endif

    if (!out_mask) return -1;
    *out_mask = 0;
    FD_ZERO(&readfds);
    if (first != PUNCH_INVALID_SOCKET) {
        FD_SET(first, &readfds);
#ifndef _WIN32
        maximum = first;
#endif
    }
    if (second != PUNCH_INVALID_SOCKET) {
        FD_SET(second, &readfds);
#ifndef _WIN32
        if (maximum == PUNCH_INVALID_SOCKET || second > maximum) maximum = second;
#endif
    }
#ifndef _WIN32
    if (maximum == PUNCH_INVALID_SOCKET) return -1;
#else
    if (first == PUNCH_INVALID_SOCKET && second == PUNCH_INVALID_SOCKET) return -1;
#endif
    tv.tv_sec = (long)(timeout_ms / 1000ULL);
    tv.tv_usec = (long)((timeout_ms % 1000ULL) * 1000ULL);
#ifdef _WIN32
    rc = select(0, &readfds, NULL, NULL, &tv);
#else
    rc = select(maximum + 1, &readfds, NULL, NULL, &tv);
#endif
    if (rc > 0) {
        if (first != PUNCH_INVALID_SOCKET && FD_ISSET(first, &readfds))
            *out_mask |= 1;
        if (second != PUNCH_INVALID_SOCKET && FD_ISSET(second, &readfds))
            *out_mask |= 2;
        return 1;
    }
    if (rc == 0) return 0;
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR ? 0 : -1;
#else
    return errno == EINTR ? 0 : -1;
#endif
}

/**
 * Compares two socket addresses for equality.
 * @param a First address.
 * @param b Second address.
 * @return 1 if equal, 0 otherwise.
 */
static int punch_same_endpoint(const struct sockaddr_storage *a,
    const struct sockaddr_storage *b)
{
    if (!a || !b || a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *aa = (const struct sockaddr_in *)a;
        const struct sockaddr_in *bb = (const struct sockaddr_in *)b;
        return aa->sin_port == bb->sin_port &&
            aa->sin_addr.s_addr == bb->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *aa = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *bb = (const struct sockaddr_in6 *)b;
        return aa->sin6_port == bb->sin6_port &&
            aa->sin6_scope_id == bb->sin6_scope_id &&
            memcmp(&aa->sin6_addr, &bb->sin6_addr, sizeof(aa->sin6_addr)) == 0;
    }
    return 0;
}

/**
 * Finds a candidate index matching the given address.
 * @param candidates Candidate array.
 * @param count Number of candidates.
 * @param addr Address to match.
 * @return Index on success, -1 if not found.
 */
static int punch_candidate_index(const punch_candidate_t *candidates,
    size_t count, const struct sockaddr_storage *addr)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (punch_same_endpoint(&candidates[i].addr, addr)) return (int)i;
    }
    return -1;
}

/**
 * Resolves all peer endpoints to socket addresses.
 * @param options Connection options.
 * @param family Address family.
 * @param out_candidates Output candidate array.
 * @param out_count Output candidate count.
 * @return KC_PUNCH_OK on success, error code otherwise.
 */
static int punch_resolve_peers(const kc_punch_options_t *options, int family,
    punch_candidate_t **out_candidates, size_t *out_count)
{
    punch_candidate_t *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t i;

    if (!out_candidates || !out_count) return KC_PUNCH_EINVAL;
    *out_candidates = NULL;
    *out_count = 0;

    for (i = 0; i < options->peer_count; ++i) {
        struct addrinfo hints;
        struct addrinfo *result = NULL;
        struct addrinfo *it;
        char service[16];

        if (!options->peers[i].host || !options->peers[i].host[0] ||
            options->peers[i].port == 0) continue;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = family;
        hints.ai_socktype = SOCK_DGRAM;
        snprintf(service, sizeof(service), "%u", options->peers[i].port);
        if (getaddrinfo(options->peers[i].host, service, &hints, &result) != 0)
            continue;

        for (it = result; it; it = it->ai_next) {
            punch_candidate_t *next;
            if (it->ai_addrlen <= 0 || (size_t)it->ai_addrlen >
                sizeof(struct sockaddr_storage)) continue;
            if (count == capacity) {
                size_t next_capacity = capacity ? capacity * 2U : 8U;
                next = (punch_candidate_t *)realloc(items,
                    next_capacity * sizeof(*items));
                if (!next) {
                    freeaddrinfo(result);
                    free(items);
                    return KC_PUNCH_ENOMEM;
                }
                items = next;
                capacity = next_capacity;
            }
            memset(&items[count], 0, sizeof(items[count]));
            memcpy(&items[count].addr, it->ai_addr, it->ai_addrlen);
            items[count].addr_len = (punch_socklen_t)it->ai_addrlen;
            count++;
        }
        freeaddrinfo(result);
    }

    if (count == 0) {
        free(items);
        return KC_PUNCH_ENET;
    }
    *out_candidates = items;
    *out_count = count;
    return KC_PUNCH_OK;
}

/**
 * Binds a UDP socket to the local address.
 * @param options Connection options.
 * @param family Address family.
 * @return Socket on success, PUNCH_INVALID_SOCKET on failure.
 */
static punch_socket_t punch_bind_socket(const kc_punch_options_t *options, int family)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    char service[16];
    punch_socket_t fd = PUNCH_INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(service, sizeof(service), "%u", options->port);
    if (getaddrinfo(options->host && options->host[0] ? options->host : NULL,
        service, &hints, &result) != 0) return PUNCH_INVALID_SOCKET;

    for (it = result; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == PUNCH_INVALID_SOCKET) continue;
        if (it->ai_family == AF_INET6) {
            int v6only = 1;
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                (const char *)&v6only, (punch_socklen_t)sizeof(v6only)) != 0) {
                punch_socket_close(fd);
                fd = PUNCH_INVALID_SOCKET;
                continue;
            }
        }
        if (bind(fd, it->ai_addr, (punch_socklen_t)it->ai_addrlen) == 0) break;
        punch_socket_close(fd);
        fd = PUNCH_INVALID_SOCKET;
    }
    freeaddrinfo(result);
    return fd;
}

/**
 * Sends a probe packet to an endpoint.
 * @param fd Socket.
 * @param addr Destination address.
 * @param addr_len Address length.
 * @param type Packet type.
 * @param nonce Nonce value.
 * @return 0 on success, -1 on failure.
 */
static int punch_send_probe(punch_socket_t fd,
    const struct sockaddr_storage *addr, punch_socklen_t addr_len,
    unsigned type, uint64_t nonce)
{
    unsigned char packet[PUNCH_PACKET_SIZE];
    int sent;
    punch_make_packet(packet, type, nonce);
    sent = (int)sendto(fd, (const char *)packet, (int)sizeof(packet), 0,
        (const struct sockaddr *)addr, addr_len);
    return sent == (int)sizeof(packet) ? 0 : -1;
}

/**
 * Extracts the port from a socket address.
 * @param addr Socket address.
 * @return Port in host byte order, or 0 if not found.
 */
static unsigned short punch_addr_port(const struct sockaddr_storage *addr)
{
    if (!addr) return 0;
    if (addr->ss_family == AF_INET)
        return ntohs(((const struct sockaddr_in *)addr)->sin_port);
    if (addr->ss_family == AF_INET6)
        return ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
    return 0;
}

/**
 * Extracts the host address from a socket address.
 * @param addr Socket address.
 * @param addr_len Address length.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @return 0 on success, -1 on failure.
 */
static int punch_addr_host(const struct sockaddr_storage *addr,
    punch_socklen_t addr_len, char *out, size_t out_size)
{
    if (!addr || !out || out_size == 0) return -1;
    return getnameinfo((const struct sockaddr *)addr, addr_len,
        out, (punch_socklen_t)out_size, NULL, 0, NI_NUMERICHOST) == 0 ? 0 : -1;
}

/**
 * Fills the local address information in a path.
 * @param path Path to update.
 * @return None.
 */
static void punch_fill_local(punch_base_path_t *path)
{
    struct sockaddr_storage addr;
    punch_socklen_t addr_len = (punch_socklen_t)sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(path->fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        path->local_port = punch_addr_port(&addr);
        if (punch_addr_host(&addr, addr_len,
            path->local_host, sizeof(path->local_host)) != 0)
            path->local_host[0] = '\0';
    }
}

/**
 * Normalizes one caller-supplied UDP transport status.
 * @param status Transport callback status.
 * @return Public PUNCH status.
 */
static int punch_udp_status(int status)
{
    switch (status) {
        case KC_PUNCH_OK:
        case KC_PUNCH_EINVAL:
        case KC_PUNCH_ENET:
        case KC_PUNCH_ETIMEOUT:
        case KC_PUNCH_ENOMEM:
        case KC_PUNCH_ECLOSED:
            return status;
        default:
            return KC_PUNCH_ENET;
    }
}

/**
 * Resolves the numeric local host of a supplied UDP mapping to its family.
 * @param udp Borrowed UDP mapping adapter.
 * @return AF_INET, AF_INET6, or AF_UNSPEC on failure.
 */
static int punch_udp_family(const kc_punch_udp_t *udp)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    int family = AF_UNSPEC;

    if (!udp || !udp->local.host || !udp->local.host[0]) return AF_UNSPEC;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(udp->local.host, NULL, &hints, &result) != 0)
        return AF_UNSPEC;
    for (it = result; it; it = it->ai_next) {
        if (it->ai_family == AF_INET || it->ai_family == AF_INET6) {
            family = it->ai_family;
            break;
        }
    }
    freeaddrinfo(result);
    return family;
}

/**
 * Converts a resolved candidate to the public endpoint value used by a
 * supplied UDP mapping.
 * @param candidate Resolved peer candidate.
 * @param host Output numeric host buffer.
 * @param endpoint Output endpoint value borrowing host.
 * @return 1 on success, 0 on failure.
 */
static int punch_candidate_endpoint(const punch_candidate_t *candidate,
    char host[NI_MAXHOST], kc_punch_endpoint_t *endpoint)
{
    if (!candidate || !endpoint ||
        punch_addr_host(&candidate->addr, candidate->addr_len,
            host, NI_MAXHOST) != 0)
        return 0;
    endpoint->host = host;
    endpoint->port = punch_addr_port(&candidate->addr);
    return endpoint->port != 0;
}

/**
 * Resolves a datagram source reported by a supplied UDP mapping.
 * @param datagram Received datagram.
 * @param family Required mapping family.
 * @param out Resolved source address.
 * @return 1 on success, 0 on failure.
 */
static int punch_datagram_addr(const kc_punch_datagram_t *datagram, int family,
    struct sockaddr_storage *out)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    char service[16];
    int ok = 0;

    if (!datagram || !datagram->host || !datagram->host[0] ||
        datagram->port == 0 || !out)
        return 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    snprintf(service, sizeof(service), "%u", datagram->port);
    if (getaddrinfo(datagram->host, service, &hints, &result) != 0) return 0;
    for (it = result; it; it = it->ai_next) {
        if (it->ai_addrlen <= 0 ||
            (size_t)it->ai_addrlen > sizeof(*out))
            continue;
        memset(out, 0, sizeof(*out));
        memcpy(out, it->ai_addr, (size_t)it->ai_addrlen);
        ok = 1;
        break;
    }
    freeaddrinfo(result);
    return ok;
}

/**
 * Sends one PUNCH probe through a caller-supplied UDP mapping.
 * @param udp Borrowed UDP mapping adapter.
 * @param candidate Destination candidate.
 * @param type Probe type.
 * @param nonce Probe nonce.
 * @return Public PUNCH status.
 */
static int punch_udp_send_probe(const kc_punch_udp_t *udp,
    const punch_candidate_t *candidate, unsigned type, uint64_t nonce)
{
    unsigned char packet[PUNCH_PACKET_SIZE];
    char host[NI_MAXHOST];
    kc_punch_endpoint_t endpoint;

    if (!punch_candidate_endpoint(candidate, host, &endpoint))
        return KC_PUNCH_ENET;
    punch_make_packet(packet, type, nonce);
    return punch_udp_status(udp->send(udp->context, &endpoint,
        packet, sizeof(packet)));
}

/**
 * Returns the library build version.
 * @return Build version number.
 */
uint64_t kc_punch_version(void)
{
    return (uint64_t)KC_PUNCH_BUILD_VERSION;
}

/**
 * Establishes a direct bidirectional UDP path to one supplied peer endpoint.
 * @param out Output path handle.
 * @param options Connection options.
 * @return KC_PUNCH_OK on success, otherwise a negative status.
 */
static int punch_base_connect(punch_base_path_t **out, const kc_punch_options_t *options)
{
    punch_candidate_t *candidates = NULL;
    size_t candidate_count = 0;
    punch_socket_t fds[2] = { PUNCH_INVALID_SOCKET, PUNCH_INVALID_SOCKET };
    punch_socket_t fd = PUNCH_INVALID_SOCKET;
    uint64_t nonce;
    uint64_t timeout_ms;
    uint64_t deadline;
    uint64_t last_probe = 0;
    int status = KC_PUNCH_ENET;
    int chosen = -1;
    size_t i;
#ifdef _WIN32
    WSADATA wsa;
    int wsa_started = 0;
#endif

    if (!out || !options || !options->peers || options->peer_count == 0)
        return KC_PUNCH_EINVAL;
    *out = NULL;

#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return KC_PUNCH_ENET;
    wsa_started = 1;
#endif

    status = punch_resolve_peers(options, AF_UNSPEC, &candidates, &candidate_count);
    if (status != KC_PUNCH_OK) goto done;

    for (i = 0; i < candidate_count; ++i) {
        int slot = candidates[i].addr.ss_family == AF_INET6 ? 1 : 0;
        if (fds[slot] == PUNCH_INVALID_SOCKET)
            fds[slot] = punch_bind_socket(options, candidates[i].addr.ss_family);
        candidates[i].socket_index = fds[slot] == PUNCH_INVALID_SOCKET ? -1 : slot;
    }
    if (fds[0] == PUNCH_INVALID_SOCKET && fds[1] == PUNCH_INVALID_SOCKET)
        goto done;

    nonce = punch_nonce();
    timeout_ms = options->timeout_ms ? options->timeout_ms : PUNCH_DEFAULT_TIMEOUT_MS;
    deadline = punch_deadline(punch_now_ms(), timeout_ms);

    while (punch_now_ms() < deadline && chosen < 0) {
        uint64_t now = punch_now_ms();
        uint64_t remaining;
        uint64_t wait_ms;
        int ready;
        int ready_mask = 0;
        int ready_socket;

        if (last_probe == 0 || now - last_probe >= PUNCH_PROBE_INTERVAL_MS) {
            for (i = 0; i < candidate_count; ++i) {
                if (candidates[i].socket_index >= 0)
                    (void)punch_send_probe(fds[candidates[i].socket_index],
                        &candidates[i].addr, candidates[i].addr_len,
                        PUNCH_PACKET_HELLO, nonce);
            }
            last_probe = now;
        }

        now = punch_now_ms();
        if (now >= deadline) break;
        remaining = deadline - now;
        wait_ms = remaining < PUNCH_PROBE_INTERVAL_MS ?
            remaining : PUNCH_PROBE_INTERVAL_MS;
        ready = punch_wait_any(fds[0], fds[1], wait_ms, &ready_mask);
        if (ready < 0) {
            status = KC_PUNCH_ENET;
            goto done;
        }
        if (ready == 0) continue;

        for (ready_socket = 0; ready_socket < 2 && chosen < 0; ++ready_socket) {
            unsigned char packet[256];
            struct sockaddr_storage source;
            punch_socklen_t source_len = (punch_socklen_t)sizeof(source);
            int received;
            int index;
            unsigned type;
            uint64_t packet_nonce;

            if ((ready_mask & (1 << ready_socket)) == 0) continue;
            received = (int)recvfrom(fds[ready_socket], (char *)packet,
                (int)sizeof(packet), 0, (struct sockaddr *)&source, &source_len);
            if (received <= 0) continue;
            index = punch_candidate_index(candidates, candidate_count, &source);
            if (index < 0 || candidates[index].socket_index != ready_socket ||
                !punch_parse_packet(packet, (size_t)received,
                    &type, &packet_nonce)) continue;

            if (type == PUNCH_PACKET_HELLO) {
                candidates[index].saw_hello = 1;
                candidates[index].peer_nonce = packet_nonce;
                (void)punch_send_probe(fds[ready_socket], &source, source_len,
                    PUNCH_PACKET_ACK, packet_nonce);
            } else if (type == PUNCH_PACKET_ACK && packet_nonce == nonce) {
                candidates[index].saw_ack = 1;
            }

            if (candidates[index].saw_hello && candidates[index].saw_ack)
                chosen = index;
        }
    }

    if (chosen < 0) {
        status = KC_PUNCH_ETIMEOUT;
        goto done;
    }

    for (i = 0; i < 3U; ++i) {
        if (candidates[chosen].peer_nonce != 0)
            (void)punch_send_probe(fds[candidates[chosen].socket_index],
                &candidates[chosen].addr, candidates[chosen].addr_len,
                PUNCH_PACKET_ACK, candidates[chosen].peer_nonce);
    }

    fd = fds[candidates[chosen].socket_index];
    fds[candidates[chosen].socket_index] = PUNCH_INVALID_SOCKET;
    if (connect(fd, (const struct sockaddr *)&candidates[chosen].addr,
        candidates[chosen].addr_len) != 0) {
        status = KC_PUNCH_ENET;
        goto done;
    }

    {
        punch_base_path_t *path = (punch_base_path_t *)calloc(1, sizeof(*path));
        if (!path) {
            status = KC_PUNCH_ENOMEM;
            goto done;
        }
        path->fd = fd;
        fd = PUNCH_INVALID_SOCKET;
        path->peer_addr = candidates[chosen].addr;
        path->peer_addr_len = candidates[chosen].addr_len;
        path->peer_port = punch_addr_port(&candidates[chosen].addr);
        if (punch_addr_host(&candidates[chosen].addr,
            candidates[chosen].addr_len, path->peer_host,
            sizeof(path->peer_host)) != 0) path->peer_host[0] = '\0';
        punch_fill_local(path);
        path->local_nonce = nonce;
        path->peer_nonce = candidates[chosen].peer_nonce;
#ifdef _WIN32
        path->wsa_started = wsa_started;
        wsa_started = 0;
#endif
        *out = path;
        status = KC_PUNCH_OK;
    }

done:
    if (fd != PUNCH_INVALID_SOCKET) punch_socket_close(fd);
    for (i = 0; i < 2U; ++i)
        if (fds[i] != PUNCH_INVALID_SOCKET) punch_socket_close(fds[i]);
    free(candidates);
#ifdef _WIN32
    if (wsa_started) WSACleanup();
#endif
    return status;
}

/**
 * Establishes a direct bidirectional UDP path over a supplied live mapping.
 * @param out Output path handle.
 * @param options Peer endpoints and timeout.
 * @param udp Borrowed UDP mapping adapter.
 * @return KC_PUNCH_OK on success, otherwise a negative status.
 */
static int punch_base_connect_over(punch_base_path_t **out, const kc_punch_options_t *options,
    const kc_punch_udp_t *udp)
{
    punch_candidate_t *candidates = NULL;
    size_t candidate_count = 0;
    uint64_t nonce;
    uint64_t timeout_ms;
    uint64_t deadline;
    uint64_t last_probe = 0;
    int family;
    int status = KC_PUNCH_ENET;
    int chosen = -1;
    size_t i;
#ifdef _WIN32
    WSADATA wsa;
    int wsa_started = 0;
#endif

    if (!out || !options || !options->peers || options->peer_count == 0 ||
        !udp || !udp->local.host || !udp->local.host[0] ||
        udp->local.port == 0 || !udp->send || !udp->receive || !udp->release)
        return KC_PUNCH_EINVAL;
    *out = NULL;

#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return KC_PUNCH_ENET;
    wsa_started = 1;
#endif

    family = punch_udp_family(udp);
    if (family != AF_INET && family != AF_INET6) {
        status = KC_PUNCH_EINVAL;
        goto done;
    }
    status = punch_resolve_peers(options, family, &candidates, &candidate_count);
    if (status != KC_PUNCH_OK) goto done;

    nonce = punch_nonce();
    timeout_ms = options->timeout_ms ? options->timeout_ms : PUNCH_DEFAULT_TIMEOUT_MS;
    deadline = punch_deadline(punch_now_ms(), timeout_ms);

    while (punch_now_ms() < deadline && chosen < 0) {
        uint64_t now = punch_now_ms();
        uint64_t remaining;
        uint64_t wait_ms;
        kc_punch_datagram_t datagram;
        struct sockaddr_storage source;
        int receive_status;
        int index;
        unsigned type;
        uint64_t packet_nonce;

        if (last_probe == 0 || now - last_probe >= PUNCH_PROBE_INTERVAL_MS) {
            for (i = 0; i < candidate_count; ++i)
                (void)punch_udp_send_probe(udp, &candidates[i],
                    PUNCH_PACKET_HELLO, nonce);
            last_probe = now;
        }

        now = punch_now_ms();
        if (now >= deadline) break;
        remaining = deadline - now;
        wait_ms = remaining < PUNCH_PROBE_INTERVAL_MS ?
            remaining : PUNCH_PROBE_INTERVAL_MS;
        memset(&datagram, 0, sizeof(datagram));
        receive_status = punch_udp_status(udp->receive(udp->context,
            &datagram, wait_ms));
        if (receive_status == KC_PUNCH_ETIMEOUT) continue;
        if (receive_status != KC_PUNCH_OK) {
            status = receive_status;
            goto done;
        }
        if ((datagram.data_size > 0 && !datagram.data) ||
            datagram.data_size > PUNCH_MAX_DATAGRAM ||
            !punch_datagram_addr(&datagram, family, &source)) {
            udp->release(udp->context, &datagram);
            status = KC_PUNCH_ENET;
            goto done;
        }
        index = punch_candidate_index(candidates, candidate_count, &source);
        if (index >= 0 && punch_parse_packet(datagram.data, datagram.data_size,
            &type, &packet_nonce)) {
            if (type == PUNCH_PACKET_HELLO) {
                candidates[index].saw_hello = 1;
                candidates[index].peer_nonce = packet_nonce;
                (void)punch_udp_send_probe(udp, &candidates[index],
                    PUNCH_PACKET_ACK, packet_nonce);
            } else if (type == PUNCH_PACKET_ACK && packet_nonce == nonce) {
                candidates[index].saw_ack = 1;
            }
            if (candidates[index].saw_hello && candidates[index].saw_ack)
                chosen = index;
        }
        udp->release(udp->context, &datagram);
    }

    if (chosen < 0) {
        status = KC_PUNCH_ETIMEOUT;
        goto done;
    }

    for (i = 0; i < 3U; ++i) {
        if (candidates[chosen].peer_nonce != 0)
            (void)punch_udp_send_probe(udp, &candidates[chosen],
                PUNCH_PACKET_ACK, candidates[chosen].peer_nonce);
    }

    {
        punch_base_path_t *path = (punch_base_path_t *)calloc(1, sizeof(*path));
        if (!path) {
            status = KC_PUNCH_ENOMEM;
            goto done;
        }
        path->fd = PUNCH_INVALID_SOCKET;
        path->borrowed_udp = 1;
        path->udp = *udp;
        snprintf(path->local_host, sizeof(path->local_host), "%s",
            udp->local.host);
        path->local_port = udp->local.port;
        path->udp.local.host = path->local_host;
        path->udp.local.port = path->local_port;
        path->peer_addr = candidates[chosen].addr;
        path->peer_addr_len = candidates[chosen].addr_len;
        path->peer_port = punch_addr_port(&candidates[chosen].addr);
        if (punch_addr_host(&candidates[chosen].addr,
            candidates[chosen].addr_len, path->peer_host,
            sizeof(path->peer_host)) != 0) path->peer_host[0] = '\0';
        path->local_nonce = nonce;
        path->peer_nonce = candidates[chosen].peer_nonce;
#ifdef _WIN32
        path->wsa_started = wsa_started;
        wsa_started = 0;
#endif
        *out = path;
        status = KC_PUNCH_OK;
    }

done:
    free(candidates);
#ifdef _WIN32
    if (wsa_started) WSACleanup();
#endif
    return status;
}

/**
 * Sends one UDP datagram through an established path.
 * @param path Path handle.
 * @param data Data to send.
 * @param data_size Data size.
 * @return KC_PUNCH_OK on success, otherwise a negative status.
 */
static int punch_base_send(punch_base_path_t *path, const void *data, size_t data_size)
{
    static const unsigned char empty = 0;
    int sent;

    if (!path) return KC_PUNCH_ECLOSED;
    if (!data && data_size != 0) return KC_PUNCH_EINVAL;
    if (data_size > PUNCH_MAX_DATAGRAM) return KC_PUNCH_EINVAL;
    if (path->borrowed_udp) {
        kc_punch_endpoint_t peer;
        if (!path->udp.send || !path->peer_host[0] || path->peer_port == 0)
            return KC_PUNCH_ECLOSED;
        peer.host = path->peer_host;
        peer.port = path->peer_port;
        return punch_udp_status(path->udp.send(path->udp.context, &peer,
            data_size ? data : &empty, data_size));
    }
    if (path->fd == PUNCH_INVALID_SOCKET) return KC_PUNCH_ECLOSED;
    sent = (int)send(path->fd,
        (const char *)(data_size ? data : &empty), (int)data_size, 0);
    return sent == (int)data_size ? KC_PUNCH_OK : KC_PUNCH_ENET;
}

/**
 * Receives one UDP datagram through a borrowed UDP mapping.
 * @param path Established path.
 * @param out_data Output data buffer.
 * @param out_size Output data size.
 * @param timeout_ms Timeout in milliseconds (0 = wait indefinitely).
 * @return KC_PUNCH_OK on success, otherwise a negative status.
 */
static int punch_receive_over(punch_base_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms)
{
    uint64_t deadline = timeout_ms ? punch_deadline(punch_now_ms(), timeout_ms) : 0;
    int family = path->peer_addr.ss_family;

    if (family != AF_INET && family != AF_INET6) return KC_PUNCH_ECLOSED;
    for (;;) {
        kc_punch_datagram_t datagram;
        struct sockaddr_storage source;
        uint64_t wait_ms = 0;
        int status;
        int is_peer;
        unsigned type;
        uint64_t probe_nonce;
        punch_candidate_t selected;
        unsigned char *buffer;

        if (timeout_ms) {
            uint64_t now = punch_now_ms();
            if (now >= deadline) return KC_PUNCH_ETIMEOUT;
            wait_ms = deadline - now;
        }
        memset(&datagram, 0, sizeof(datagram));
        status = punch_udp_status(path->udp.receive(path->udp.context,
            &datagram, wait_ms));
        if (status != KC_PUNCH_OK) return status;
        if ((datagram.data_size > 0 && !datagram.data) ||
            datagram.data_size > PUNCH_MAX_DATAGRAM ||
            !punch_datagram_addr(&datagram, family, &source)) {
            path->udp.release(path->udp.context, &datagram);
            return KC_PUNCH_ENET;
        }

        memset(&selected, 0, sizeof(selected));
        is_peer = punch_same_endpoint(&source, &path->peer_addr);
        if (!is_peer) {
            path->udp.release(path->udp.context, &datagram);
            continue;
        }

        if (punch_parse_packet(datagram.data, datagram.data_size,
            &type, &probe_nonce)) {
            if (type == PUNCH_PACKET_HELLO && probe_nonce == path->peer_nonce) {
                char host[NI_MAXHOST];
                kc_punch_endpoint_t endpoint;
                selected.addr = source;
                selected.addr_len = family == AF_INET ?
                    (punch_socklen_t)sizeof(struct sockaddr_in) :
                    (punch_socklen_t)sizeof(struct sockaddr_in6);
                if (punch_candidate_endpoint(&selected, host, &endpoint)) {
                    unsigned char ack[PUNCH_PACKET_SIZE];
                    punch_make_packet(ack, PUNCH_PACKET_ACK, probe_nonce);
                    (void)path->udp.send(path->udp.context, &endpoint,
                        ack, sizeof(ack));
                }
            }
            path->udp.release(path->udp.context, &datagram);
            continue;
        }

        buffer = (unsigned char *)malloc(datagram.data_size ?
            datagram.data_size : 1U);
        if (!buffer) {
            path->udp.release(path->udp.context, &datagram);
            return KC_PUNCH_ENOMEM;
        }
        if (datagram.data_size > 0)
            memcpy(buffer, datagram.data, datagram.data_size);
        *out_data = buffer;
        *out_size = datagram.data_size;
        path->udp.release(path->udp.context, &datagram);
        return KC_PUNCH_OK;
    }
}

/**
 * Receives one UDP datagram through an established path.
 * @param path Path handle.
 * @param out_data Output data buffer.
 * @param out_size Output data size.
 * @param timeout_ms Timeout in milliseconds (0 = wait indefinitely).
 * @return KC_PUNCH_OK on success, otherwise a negative status.
 */
static int punch_base_receive(punch_base_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms)
{
    uint64_t deadline = timeout_ms ? punch_deadline(punch_now_ms(), timeout_ms) : 0;

    if (!out_data || !out_size) return KC_PUNCH_EINVAL;
    *out_data = NULL;
    *out_size = 0;
    if (!path) return KC_PUNCH_ECLOSED;
    if (path->borrowed_udp)
        return punch_receive_over(path, out_data, out_size, timeout_ms);
    if (path->fd == PUNCH_INVALID_SOCKET) return KC_PUNCH_ECLOSED;

    for (;;) {
        uint64_t wait_ms;
        int ready;
        unsigned char *buffer;
        int received;

        if (timeout_ms) {
            uint64_t now = punch_now_ms();
            if (now >= deadline) return KC_PUNCH_ETIMEOUT;
            wait_ms = deadline - now;
        } else {
            wait_ms = 1000ULL;
        }
        if (wait_ms > 1000ULL) wait_ms = 1000ULL;
        ready = punch_wait_readable(path->fd, wait_ms);
        if (ready < 0) return KC_PUNCH_ENET;
        if (ready == 0) continue;

        buffer = (unsigned char *)malloc(PUNCH_MAX_DATAGRAM);
        if (!buffer) return KC_PUNCH_ENOMEM;
        received = (int)recv(path->fd, (char *)buffer, PUNCH_MAX_DATAGRAM, 0);
        if (received < 0) {
            free(buffer);
            return KC_PUNCH_ENET;
        }

        {
            unsigned type;
            uint64_t probe_nonce;
            if (punch_parse_packet(buffer, (size_t)received, &type, &probe_nonce)) {
                if (type == PUNCH_PACKET_HELLO && probe_nonce == path->peer_nonce) {
                    unsigned char ack[PUNCH_PACKET_SIZE];
                    punch_make_packet(ack, PUNCH_PACKET_ACK, probe_nonce);
                    (void)send(path->fd, (const char *)ack,
                        (int)sizeof(ack), 0);
                }
                free(buffer);
                continue;
            }
        }

        if (received == 0) {
            unsigned char *small = (unsigned char *)realloc(buffer, 1U);
            if (small) buffer = small;
        } else {
            unsigned char *small = (unsigned char *)realloc(buffer,
                (size_t)received);
            if (small) buffer = small;
        }
        *out_data = buffer;
        *out_size = (size_t)received;
        return KC_PUNCH_OK;
    }
}

/**
 * Returns the local host address.
 * @param path Path handle.
 * @return Local host string or NULL if unknown.
 */
static const char *punch_base_local_host(const punch_base_path_t *path)
{
    return path && path->local_host[0] ? path->local_host : NULL;
}

/**
 * Returns the local UDP port.
 * @param path Path handle.
 * @return Local port or 0 if unknown.
 */
static unsigned short punch_base_local_port(const punch_base_path_t *path)
{
    return path ? path->local_port : 0;
}

/**
 * Returns the peer host address.
 * @param path Path handle.
 * @return Peer host string or NULL if unknown.
 */
static const char *punch_base_peer_host(const punch_base_path_t *path)
{
    return path && path->peer_host[0] ? path->peer_host : NULL;
}

/**
 * Returns the peer UDP port.
 * @param path Path handle.
 * @return Peer port or 0 if unknown.
 */
static unsigned short punch_base_peer_port(const punch_base_path_t *path)
{
    return path ? path->peer_port : 0;
}

/**
 * Frees memory allocated by libpunch.
 * @param ptr Pointer to free (may be NULL).
 * @return None.
 */
void kc_punch_free(void *ptr)
{
    free(ptr);
}

/**
 * Closes and frees an established path.
 * @param path Path handle (may be NULL).
 * @return None.
 */
static void punch_base_close(punch_base_path_t *path)
{
    if (!path) return;
    if (path->fd != PUNCH_INVALID_SOCKET) {
        punch_socket_close(path->fd);
        path->fd = PUNCH_INVALID_SOCKET;
    }
    memset(&path->udp, 0, sizeof(path->udp));
    path->borrowed_udp = 0;
#ifdef _WIN32
    if (path->wsa_started) WSACleanup();
#endif
    free(path);
}

/**
 * Returns a string description for a status code.
 * @param status Status code.
 * @return Static error message string.
 */
const char *kc_punch_strerror(int status)
{
    switch (status) {
        case KC_PUNCH_OK: return "ok";
        case KC_PUNCH_EINVAL: return "invalid argument";
        case KC_PUNCH_ENET: return "network error";
        case KC_PUNCH_ETIMEOUT: return "connection timed out";
        case KC_PUNCH_ENOMEM: return "out of memory";
        case KC_PUNCH_ECLOSED: return "path is closed";
        default: return "unknown punch error";
    }
}

#ifndef PUNCH_LIVE_POLL_MS
#define PUNCH_LIVE_POLL_MS       100ULL
#endif
#ifndef PUNCH_LIVE_KEEPALIVE_MS
#define PUNCH_LIVE_KEEPALIVE_MS 3000ULL
#endif
#ifndef PUNCH_LIVE_TIMEOUT_MS
#define PUNCH_LIVE_TIMEOUT_MS  10000ULL
#endif
#define PUNCH_LIVE_PACKET_SIZE       16U
#define PUNCH_LIVE_PACKET_KEEPALIVE   1U
#define PUNCH_LIVE_PACKET_ACK         2U
#define PUNCH_LIVE_QUEUE_MAX         256U
#define PUNCH_LIVE_QUEUE_BYTES (4U * 1024U * 1024U)

static const unsigned char punch_live_magic[6] = { 'K', 'C', 'H', 'P', 'M', '2' };

typedef struct punch_live_datagram {
    struct punch_live_datagram *next;
    void *data;
    size_t size;
} punch_live_datagram_t;

struct kc_punch_path {
    punch_base_path_t *base;
    punch_live_thread_t thread;
    punch_live_mutex_t mutex;
    punch_live_cond_t cond;
    punch_live_datagram_t *head;
    punch_live_datagram_t *tail;
    size_t queued_count;
    size_t queued_bytes;
    uint64_t token;
    uint64_t last_rx_ms;
    int stopping;
    int terminal_status;
    int thread_started;
};

/**
 * Get current monotonic time in milliseconds.
 * @return Current time in milliseconds.
 */
static uint64_t punch_live_now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

/**
 * Compute deadline with overflow protection.
 * @param now Current time in milliseconds.
 * @param timeout_ms Timeout in milliseconds.
 * @return Deadline in milliseconds, or UINT64_MAX on overflow.
 */
static uint64_t punch_live_deadline(uint64_t now, uint64_t timeout_ms)
{
    return UINT64_MAX - now < timeout_ms ? UINT64_MAX : now + timeout_ms;
}

/**
 * Generate a unique token from address and time.
 * @param address Pointer to use as entropy source.
 * @return Non-zero 64-bit token.
 */
static uint64_t punch_live_token(const void *address)
{
    uint64_t x = punch_live_now_ms() ^ (uint64_t)(uintptr_t)address;
#ifdef _WIN32
    x ^= (uint64_t)GetCurrentProcessId() << 32;
#else
    x ^= (uint64_t)(uintptr_t)&x << 17;
#endif
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x ? x : 1ULL;
}

/**
 * Store 64-bit value as big-endian bytes.
 * @param out Output buffer of 8 bytes.
 * @param value Value to store.
 * @return void
 */
static void punch_live_store64be(unsigned char out[8], uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8U; ++i) {
        out[7U - i] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
}

/**
 * Load 64-bit value from big-endian bytes.
 * @param in Input buffer of 8 bytes.
 * @return Loaded 64-bit value.
 */
static uint64_t punch_live_load64be(const unsigned char in[8])
{
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8U; ++i) value = (value << 8) | in[i];
    return value;
}

/**
 * Construct a live protocol packet.
 * @param packet Output buffer of PUNCH_LIVE_PACKET_SIZE bytes.
 * @param type Packet type.
 * @param token Token value.
 * @return void
 */
static void punch_live_make_packet(unsigned char packet[PUNCH_LIVE_PACKET_SIZE],
    unsigned type, uint64_t token)
{
    memcpy(packet, punch_live_magic, sizeof(punch_live_magic));
    packet[6] = (unsigned char)type;
    packet[7] = 0;
    punch_live_store64be(packet + 8, token);
}

/**
 * Parse a live protocol packet.
 * @param data Packet data.
 * @param size Packet size.
 * @param type Output packet type.
 * @param token Output token value.
 * @return 1 on success, 0 on failure.
 */
static int punch_live_parse_packet(const void *data, size_t size,
    unsigned *type, uint64_t *token)
{
    const unsigned char *packet = (const unsigned char *)data;
    if (!packet || size != PUNCH_LIVE_PACKET_SIZE || !type || !token) return 0;
    if (memcmp(packet, punch_live_magic, sizeof(punch_live_magic)) != 0 ||
        packet[7] != 0) return 0;
    if (packet[6] != PUNCH_LIVE_PACKET_KEEPALIVE &&
        packet[6] != PUNCH_LIVE_PACKET_ACK) return 0;
    *type = packet[6];
    *token = punch_live_load64be(packet + 8);
    return 1;
}

#ifdef _WIN32
/**
 * Initialize mutex.
 * @param mutex Mutex to initialize.
 * @return 0 on success, -1 on failure.
 */
static int punch_live_mutex_init(punch_live_mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
    return 0;
}

/**
 * Destroy mutex.
 * @param mutex Mutex to destroy.
 * @return void
 */
static void punch_live_mutex_destroy(punch_live_mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
}

/**
 * Lock mutex.
 * @param mutex Mutex to lock.
 * @return void
 */
static void punch_live_lock(punch_live_mutex_t *mutex)
{
    EnterCriticalSection(mutex);
}

/**
 * Unlock mutex.
 * @param mutex Mutex to unlock.
 * @return void
 */
static void punch_live_unlock(punch_live_mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
}

/**
 * Initialize condition variable.
 * @param cond Condition variable to initialize.
 * @return void
 */
static void punch_live_cond_init(punch_live_cond_t *cond)
{
    InitializeConditionVariable(cond);
}

/**
 * Signal condition variable.
 * @param cond Condition variable to signal.
 * @return void
 */
static void punch_live_cond_signal(punch_live_cond_t *cond)
{
    WakeAllConditionVariable(cond);
}

/**
 * Wait on condition variable with timeout.
 * @param cond Condition variable to wait on.
 * @param mutex Associated mutex.
 * @param timeout_ms Timeout in milliseconds, 0 for infinite.
 * @return 0 on success, 1 on timeout, -1 on error.
 */
static int punch_live_cond_wait(punch_live_cond_t *cond, punch_live_mutex_t *mutex,
    uint64_t timeout_ms)
{
    DWORD ms = timeout_ms >= (uint64_t)INFINITE ? INFINITE - 1U :
        (DWORD)timeout_ms;
    if (SleepConditionVariableCS(cond, mutex,
        timeout_ms == 0 ? INFINITE : ms)) return 0;
    return GetLastError() == ERROR_TIMEOUT ? 1 : -1;
}
#else
/**
 * Initialize mutex.
 * @param mutex Mutex to initialize.
 * @return 0 on success, -1 on failure.
 */
static int punch_live_mutex_init(punch_live_mutex_t *mutex)
{
    return pthread_mutex_init(mutex, NULL);
}

/**
 * Destroy mutex.
 * @param mutex Mutex to destroy.
 * @return void
 */
static void punch_live_mutex_destroy(punch_live_mutex_t *mutex)
{
    (void)pthread_mutex_destroy(mutex);
}

/**
 * Lock mutex.
 * @param mutex Mutex to lock.
 * @return void
 */
static void punch_live_lock(punch_live_mutex_t *mutex)
{
    (void)pthread_mutex_lock(mutex);
}

/**
 * Unlock mutex.
 * @param mutex Mutex to unlock.
 * @return void
 */
static void punch_live_unlock(punch_live_mutex_t *mutex)
{
    (void)pthread_mutex_unlock(mutex);
}

/**
 * Initialize condition variable.
 * @param cond Condition variable to initialize.
 * @return void
 */
static void punch_live_cond_init(punch_live_cond_t *cond)
{
    (void)pthread_cond_init(cond, NULL);
}

/**
 * Destroy condition variable.
 * @param cond Condition variable to destroy.
 * @return void
 */
static void punch_live_cond_destroy(punch_live_cond_t *cond)
{
    (void)pthread_cond_destroy(cond);
}

/**
 * Signal condition variable.
 * @param cond Condition variable to signal.
 * @return void
 */
static void punch_live_cond_signal(punch_live_cond_t *cond)
{
    (void)pthread_cond_broadcast(cond);
}

/**
 * Wait on condition variable with timeout.
 * @param cond Condition variable to wait on.
 * @param mutex Associated mutex.
 * @param timeout_ms Timeout in milliseconds, 0 for infinite.
 * @return 0 on success, 1 on timeout, -1 on error.
 */
static int punch_live_cond_wait(punch_live_cond_t *cond, punch_live_mutex_t *mutex,
    uint64_t timeout_ms)
{
    int rc;
    if (timeout_ms == 0) {
        rc = pthread_cond_wait(cond, mutex);
        return rc == 0 ? 0 : -1;
    }
    {
        struct timespec ts;
        uint64_t ns;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return -1;
        ns = (uint64_t)ts.tv_nsec + (timeout_ms % 1000ULL) * 1000000ULL;
        ts.tv_sec += (time_t)(timeout_ms / 1000ULL + ns / 1000000000ULL);
        ts.tv_nsec = (long)(ns % 1000000000ULL);
        rc = pthread_cond_timedwait(cond, mutex, &ts);
    }
    if (rc == 0) return 0;
    return rc == ETIMEDOUT ? 1 : -1;
}
#endif

/**
 * Set terminal status on path.
 * @param path Path to update.
 * @param status Terminal status to set.
 * @return void
 */
static void punch_live_set_terminal(kc_punch_path_t *path, int status)
{
    punch_live_lock(&path->mutex);
    if (path->terminal_status == KC_PUNCH_OK)
        path->terminal_status = status == KC_PUNCH_OK ? KC_PUNCH_ENET : status;
    punch_live_cond_signal(&path->cond);
    punch_live_unlock(&path->mutex);
}

/**
 * Check if path should stop.
 * @param path Path to check.
 * @return 1 if should stop, 0 otherwise.
 */
static int punch_live_should_stop(kc_punch_path_t *path)
{
    int stop;
    punch_live_lock(&path->mutex);
    stop = path->stopping || path->terminal_status != KC_PUNCH_OK;
    punch_live_unlock(&path->mutex);
    return stop;
}

/**
 * Note receive time on path.
 * @param path Path to update.
 * @return void
 */
static void punch_live_note_rx(kc_punch_path_t *path)
{
    punch_live_lock(&path->mutex);
    path->last_rx_ms = punch_live_now_ms();
    punch_live_unlock(&path->mutex);
}

/**
 * Queue data on path.
 * @param path Path to queue on.
 * @param data Data to queue (ownership transferred).
 * @param size Size of data.
 * @return 0 on success, error code on failure.
 */
static int punch_live_queue(kc_punch_path_t *path, void *data, size_t size)
{
    punch_live_datagram_t *item;
    int keep = 0;

    item = (punch_live_datagram_t *)calloc(1, sizeof(*item));
    if (!item) return KC_PUNCH_ENOMEM;
    item->data = data;
    item->size = size;

    punch_live_lock(&path->mutex);
    if (!path->stopping && path->terminal_status == KC_PUNCH_OK &&
        path->queued_count < PUNCH_LIVE_QUEUE_MAX &&
        size <= PUNCH_LIVE_QUEUE_BYTES - path->queued_bytes) {
        if (path->tail) path->tail->next = item;
        else path->head = item;
        path->tail = item;
        path->queued_count++;
        path->queued_bytes += size;
        keep = 1;
        punch_live_cond_signal(&path->cond);
    }
    punch_live_unlock(&path->mutex);

    if (!keep) {
        free(item);
        return KC_PUNCH_ETIMEOUT;
    }
    return KC_PUNCH_OK;
}

/**
 * Send control packet on path.
 * @param path Path to send on.
 * @param type Packet type.
 * @param token Token value.
 * @return 0 on success, error code on failure.
 */
static int punch_live_send_control(kc_punch_path_t *path, unsigned type,
    uint64_t token)
{
    unsigned char packet[PUNCH_LIVE_PACKET_SIZE];
    punch_live_make_packet(packet, type, token);
    return punch_base_send(path->base, packet, sizeof(packet));
}

/**
 * Live path worker thread.
 * @param argument Path pointer.
 * @return Thread exit value.
 */
#ifdef _WIN32
static DWORD WINAPI punch_live_worker(LPVOID argument)
#else
static void *punch_live_worker(void *argument)
#endif
{
    kc_punch_path_t *path = (kc_punch_path_t *)argument;
    uint64_t next_keepalive = punch_live_now_ms() + PUNCH_LIVE_KEEPALIVE_MS;

    while (!punch_live_should_stop(path)) {
        void *data = NULL;
        size_t size = 0;
        uint64_t now = punch_live_now_ms();
        int status;

        if (now >= next_keepalive) {
            status = punch_live_send_control(path, PUNCH_LIVE_PACKET_KEEPALIVE,
                path->token);
            if (status != KC_PUNCH_OK) {
                punch_live_set_terminal(path, status);
                break;
            }
            next_keepalive = now + PUNCH_LIVE_KEEPALIVE_MS;
        }

        status = punch_base_receive(path->base, &data, &size, PUNCH_LIVE_POLL_MS);
        if (status == KC_PUNCH_ETIMEOUT) {
            punch_live_lock(&path->mutex);
            now = punch_live_now_ms();
            if (!path->stopping && path->terminal_status == KC_PUNCH_OK &&
                now - path->last_rx_ms >= PUNCH_LIVE_TIMEOUT_MS) {
                path->terminal_status = KC_PUNCH_ETIMEOUT;
                punch_live_cond_signal(&path->cond);
            }
            punch_live_unlock(&path->mutex);
            continue;
        }
        if (status != KC_PUNCH_OK) {
            punch_live_set_terminal(path, status);
            break;
        }

        punch_live_note_rx(path);
        {
            unsigned type;
            uint64_t token;
            if (punch_live_parse_packet(data, size, &type, &token)) {
                if (type == PUNCH_LIVE_PACKET_KEEPALIVE) {
                    status = punch_live_send_control(path,
                        PUNCH_LIVE_PACKET_ACK, token);
                    if (status != KC_PUNCH_OK)
                        punch_live_set_terminal(path, status);
                }
                kc_punch_free(data);
                continue;
            }
        }

        status = punch_live_queue(path, data, size);
        if (status == KC_PUNCH_ENOMEM) {
            kc_punch_free(data);
            punch_live_set_terminal(path, status);
            break;
        }
        if (status != KC_PUNCH_OK) kc_punch_free(data);
    }

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Free queued data on path.
 * @param path Path to free queue on.
 * @return void
 */
static void punch_live_free_queue(kc_punch_path_t *path)
{
    punch_live_datagram_t *item = path->head;
    while (item) {
        punch_live_datagram_t *next = item->next;
        kc_punch_free(item->data);
        free(item);
        item = next;
    }
    path->head = NULL;
    path->tail = NULL;
    path->queued_count = 0;
    path->queued_bytes = 0;
}

/**
 * Start live path.
 * @param out Output path pointer.
 * @param base Base path.
 * @return 0 on success, error code on failure.
 */
static int punch_live_start(kc_punch_path_t **out, punch_base_path_t *base)
{
    kc_punch_path_t *path;
    if (!out || !base) return KC_PUNCH_EINVAL;
    *out = NULL;

    path = (kc_punch_path_t *)calloc(1, sizeof(*path));
    if (!path) return KC_PUNCH_ENOMEM;
    path->base = base;
    path->token = punch_live_token(path);
    path->last_rx_ms = punch_live_now_ms();
    path->terminal_status = KC_PUNCH_OK;
    if (punch_live_mutex_init(&path->mutex) != 0) {
        free(path);
        return KC_PUNCH_ENET;
    }
    punch_live_cond_init(&path->cond);
#ifdef _WIN32
    path->thread = CreateThread(NULL, 0, punch_live_worker, path, 0, NULL);
    if (!path->thread) {
        punch_live_mutex_destroy(&path->mutex);
        free(path);
        return KC_PUNCH_ENET;
    }
#else
    if (pthread_create(&path->thread, NULL, punch_live_worker, path) != 0) {
        punch_live_cond_destroy(&path->cond);
        punch_live_mutex_destroy(&path->mutex);
        free(path);
        return KC_PUNCH_ENET;
    }
#endif
    path->thread_started = 1;
    *out = path;
    return KC_PUNCH_OK;
}

/**
 * Connect to PUNCH server.
 * @param out Output path handle.
 * @param options Connection options.
 * @return 0 on success, error code on failure.
 */
int kc_punch_connect(kc_punch_path_t **out, const kc_punch_options_t *options)
{
    punch_base_path_t *base = NULL;
    int status;
    if (!out) return KC_PUNCH_EINVAL;
    *out = NULL;
    status = punch_base_connect(&base, options);
    if (status != KC_PUNCH_OK) return status;
    status = punch_live_start(out, base);
    if (status != KC_PUNCH_OK) punch_base_close(base);
    return status;
}

/**
 * Connect to PUNCH server over existing UDP socket.
 * @param out Output path handle.
 * @param options Connection options.
 * @param udp Existing UDP socket.
 * @return 0 on success, error code on failure.
 */
int kc_punch_connect_over(kc_punch_path_t **out, const kc_punch_options_t *options,
    const kc_punch_udp_t *udp)
{
    punch_base_path_t *base = NULL;
    int status;
    if (!out) return KC_PUNCH_EINVAL;
    *out = NULL;
    status = punch_base_connect_over(&base, options, udp);
    if (status != KC_PUNCH_OK) return status;
    status = punch_live_start(out, base);
    if (status != KC_PUNCH_OK) punch_base_close(base);
    return status;
}

/**
 * Send data on path.
 * @param path Path to send on.
 * @param data Data to send.
 * @param data_size Size of data.
 * @return 0 on success, error code on failure.
 */
int kc_punch_send(kc_punch_path_t *path, const void *data, size_t data_size)
{
    int terminal;
    if (!path) return KC_PUNCH_ECLOSED;
    punch_live_lock(&path->mutex);
    terminal = path->stopping ? KC_PUNCH_ECLOSED : path->terminal_status;
    punch_live_unlock(&path->mutex);
    if (terminal != KC_PUNCH_OK) return terminal;
    return punch_base_send(path->base, data, data_size);
}

/**
 * Receive data from path.
 * @param path Path to receive from.
 * @param out_data Output data pointer.
 * @param out_size Output data size.
 * @param timeout_ms Timeout in milliseconds, 0 for infinite.
 * @return 0 on success, error code on failure.
 */
int kc_punch_receive(kc_punch_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms)
{
    uint64_t deadline = timeout_ms ? punch_live_deadline(punch_live_now_ms(), timeout_ms) : 0;
    if (!out_data || !out_size) return KC_PUNCH_EINVAL;
    *out_data = NULL;
    *out_size = 0;
    if (!path) return KC_PUNCH_ECLOSED;

    punch_live_lock(&path->mutex);
    for (;;) {
        punch_live_datagram_t *item;
        int terminal;
        int wait_status;

        if (path->head) {
            item = path->head;
            path->head = item->next;
            if (!path->head) path->tail = NULL;
            path->queued_count--;
            path->queued_bytes -= item->size;
            *out_data = item->data;
            *out_size = item->size;
            free(item);
            punch_live_unlock(&path->mutex);
            return KC_PUNCH_OK;
        }

        terminal = path->stopping ? KC_PUNCH_ECLOSED : path->terminal_status;
        if (terminal != KC_PUNCH_OK) {
            punch_live_unlock(&path->mutex);
            return terminal;
        }

        if (timeout_ms) {
            uint64_t now = punch_live_now_ms();
            uint64_t remaining;
            if (now >= deadline) {
                punch_live_unlock(&path->mutex);
                return KC_PUNCH_ETIMEOUT;
            }
            remaining = deadline - now;
            wait_status = punch_live_cond_wait(&path->cond, &path->mutex,
                remaining);
            if (wait_status == 1) {
                punch_live_unlock(&path->mutex);
                return KC_PUNCH_ETIMEOUT;
            }
        } else {
            wait_status = punch_live_cond_wait(&path->cond, &path->mutex, 0);
        }
        if (wait_status < 0) {
            punch_live_unlock(&path->mutex);
            return KC_PUNCH_ENET;
        }
    }
}

/**
 * Get local host of path.
 * @param path Path to query.
 * @return Local host string, or NULL if not available.
 */
const char *kc_punch_local_host(const kc_punch_path_t *path)
{
    return path ? punch_base_local_host(path->base) : NULL;
}

/**
 * Get local port of path.
 * @param path Path to query.
 * @return Local port, or 0 if not available.
 */
unsigned short kc_punch_local_port(const kc_punch_path_t *path)
{
    return path ? punch_base_local_port(path->base) : 0;
}

/**
 * Get peer host of path.
 * @param path Path to query.
 * @return Peer host string, or NULL if not available.
 */
const char *kc_punch_peer_host(const kc_punch_path_t *path)
{
    return path ? punch_base_peer_host(path->base) : NULL;
}

/**
 * Get peer port of path.
 * @param path Path to query.
 * @return Peer port, or 0 if not available.
 */
unsigned short kc_punch_peer_port(const kc_punch_path_t *path)
{
    return path ? punch_base_peer_port(path->base) : 0;
}

/**
 * Close path.
 * @param path Path to close.
 * @return void
 */
void kc_punch_close(kc_punch_path_t *path)
{
    if (!path) return;
    punch_live_lock(&path->mutex);
    path->stopping = 1;
    punch_live_cond_signal(&path->cond);
    punch_live_unlock(&path->mutex);

    if (path->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(path->thread, INFINITE);
        CloseHandle(path->thread);
#else
        (void)pthread_join(path->thread, NULL);
#endif
        path->thread_started = 0;
    }

    punch_live_lock(&path->mutex);
    punch_live_free_queue(path);
    punch_live_unlock(&path->mutex);
#ifdef _WIN32
    punch_live_mutex_destroy(&path->mutex);
#else
    punch_live_cond_destroy(&path->cond);
    punch_live_mutex_destroy(&path->mutex);
#endif
    punch_base_close(path->base);
    free(path);
}
