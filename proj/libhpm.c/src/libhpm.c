/**
 * libhpm.c - Hole-Punch-Man direct UDP paths.
 * Summary: Establishes direct UDP connectivity against supplied peer endpoints.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhpm.h"

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
typedef SOCKET hpm_socket_t;
typedef int hpm_socklen_t;
#define HPM_INVALID_SOCKET INVALID_SOCKET
#define hpm_socket_close closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int hpm_socket_t;
typedef socklen_t hpm_socklen_t;
#define HPM_INVALID_SOCKET (-1)
#define hpm_socket_close close
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef KC_HPM_BUILD_VERSION
#define KC_HPM_BUILD_VERSION 0
#endif

#define HPM_DEFAULT_TIMEOUT_MS 5000ULL
#define HPM_PROBE_INTERVAL_MS 100ULL
#define HPM_SETTLE_MS 500ULL
#define HPM_PACKET_SIZE 16U
#define HPM_PACKET_HELLO 1U
#define HPM_PACKET_ACK 2U
#define HPM_MAX_DATAGRAM 65535U

static const unsigned char hpm_magic[6] = { 'K', 'C', 'H', 'P', 'M', '1' };

typedef struct {
    struct sockaddr_storage addr;
    hpm_socklen_t addr_len;
    int saw_hello;
    int saw_ack;
    uint64_t peer_nonce;
} hpm_candidate_t;

struct kc_hpm_path {
    hpm_socket_t fd;
    char local_host[NI_MAXHOST];
    unsigned short local_port;
    char peer_host[NI_MAXHOST];
    unsigned short peer_port;
    uint64_t settle_until_ms;
#ifdef _WIN32
    int wsa_started;
#endif
};

/**
 * Returns the current monotonic time in milliseconds.
 * @return Current time in milliseconds, or 0 on failure.
 */
static uint64_t hpm_now_ms(void)
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
 * Generates a pseudo-random nonce.
 * @return Non-zero nonce value.
 */
static uint64_t hpm_nonce(void)
{
    uint64_t x = hpm_now_ms();
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
static void hpm_store64be(unsigned char out[8], uint64_t value)
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
static uint64_t hpm_load64be(const unsigned char in[8])
{
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8U; ++i) value = (value << 8) | in[i];
    return value;
}

/**
 * Creates a HPM packet.
 * @param packet Output buffer (HPM_PACKET_SIZE bytes).
 * @param type Packet type (HELLO or ACK).
 * @param nonce Nonce value.
 * @return None.
 */
static void hpm_make_packet(unsigned char packet[HPM_PACKET_SIZE],
    unsigned type, uint64_t nonce)
{
    memcpy(packet, hpm_magic, sizeof(hpm_magic));
    packet[6] = (unsigned char)type;
    packet[7] = 0;
    hpm_store64be(packet + 8, nonce);
}

/**
 * Parses a HPM packet.
 * @param data Packet data.
 * @param size Packet size.
 * @param type Output packet type.
 * @param nonce Output nonce value.
 * @return 1 on success, 0 on failure.
 */
static int hpm_parse_packet(const void *data, size_t size,
    unsigned *type, uint64_t *nonce)
{
    const unsigned char *packet = (const unsigned char *)data;
    if (!packet || size != HPM_PACKET_SIZE || !type || !nonce) return 0;
    if (memcmp(packet, hpm_magic, sizeof(hpm_magic)) != 0 || packet[7] != 0)
        return 0;
    if (packet[6] != HPM_PACKET_HELLO && packet[6] != HPM_PACKET_ACK)
        return 0;
    *type = packet[6];
    *nonce = hpm_load64be(packet + 8);
    return 1;
}

/**
 * Waits for a socket to become readable.
 * @param fd Socket to wait on.
 * @param timeout_ms Timeout in milliseconds.
 * @return 1 if readable, 0 on timeout, -1 on error.
 */
static int hpm_wait_readable(hpm_socket_t fd, uint64_t timeout_ms)
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
 * Compares two socket addresses for equality.
 * @param a First address.
 * @param b Second address.
 * @return 1 if equal, 0 otherwise.
 */
static int hpm_same_endpoint(const struct sockaddr_storage *a,
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
static int hpm_candidate_index(const hpm_candidate_t *candidates,
    size_t count, const struct sockaddr_storage *addr)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (hpm_same_endpoint(&candidates[i].addr, addr)) return (int)i;
    }
    return -1;
}

/**
 * Determines the address family from options.
 * @param options Connection options.
 * @return AF_INET, AF_INET6, or AF_UNSPEC if undetermined.
 */
static int hpm_choose_family(const kc_hpm_options_t *options)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    size_t i;
    int family = AF_UNSPEC;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;

    if (options->host && options->host[0]) {
        if (getaddrinfo(options->host, NULL, &hints, &result) == 0) {
            struct addrinfo *it;
            for (it = result; it; it = it->ai_next) {
                if (it->ai_family == AF_INET || it->ai_family == AF_INET6) {
                    family = it->ai_family;
                    break;
                }
            }
            freeaddrinfo(result);
            result = NULL;
        }
        if (family != AF_UNSPEC) return family;
    }

    for (i = 0; i < options->peer_count; ++i) {
        char service[16];
        if (!options->peers[i].host || !options->peers[i].host[0] ||
            options->peers[i].port == 0) continue;
        snprintf(service, sizeof(service), "%u", options->peers[i].port);
        if (getaddrinfo(options->peers[i].host, service, &hints, &result) != 0)
            continue;
        {
            struct addrinfo *it;
            for (it = result; it; it = it->ai_next) {
                if (it->ai_family == AF_INET || it->ai_family == AF_INET6) {
                    family = it->ai_family;
                    break;
                }
            }
        }
        freeaddrinfo(result);
        result = NULL;
        if (family != AF_UNSPEC) return family;
    }
    return AF_UNSPEC;
}

/**
 * Resolves all peer endpoints to socket addresses.
 * @param options Connection options.
 * @param family Address family.
 * @param out_candidates Output candidate array.
 * @param out_count Output candidate count.
 * @return KC_HPM_OK on success, error code otherwise.
 */
static int hpm_resolve_peers(const kc_hpm_options_t *options, int family,
    hpm_candidate_t **out_candidates, size_t *out_count)
{
    hpm_candidate_t *items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t i;

    if (!out_candidates || !out_count) return KC_HPM_EINVAL;
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
            hpm_candidate_t *next;
            if (it->ai_addrlen <= 0 || (size_t)it->ai_addrlen >
                sizeof(struct sockaddr_storage)) continue;
            if (count == capacity) {
                size_t next_capacity = capacity ? capacity * 2U : 8U;
                next = (hpm_candidate_t *)realloc(items,
                    next_capacity * sizeof(*items));
                if (!next) {
                    freeaddrinfo(result);
                    free(items);
                    return KC_HPM_ENOMEM;
                }
                items = next;
                capacity = next_capacity;
            }
            memset(&items[count], 0, sizeof(items[count]));
            memcpy(&items[count].addr, it->ai_addr, it->ai_addrlen);
            items[count].addr_len = (hpm_socklen_t)it->ai_addrlen;
            count++;
        }
        freeaddrinfo(result);
    }

    if (count == 0) {
        free(items);
        return KC_HPM_ENET;
    }
    *out_candidates = items;
    *out_count = count;
    return KC_HPM_OK;
}

/**
 * Binds a UDP socket to the local address.
 * @param options Connection options.
 * @param family Address family.
 * @return Socket on success, HPM_INVALID_SOCKET on failure.
 */
static hpm_socket_t hpm_bind_socket(const kc_hpm_options_t *options, int family)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *it;
    char service[16];
    hpm_socket_t fd = HPM_INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(service, sizeof(service), "%u", options->port);
    if (getaddrinfo(options->host && options->host[0] ? options->host : NULL,
        service, &hints, &result) != 0) return HPM_INVALID_SOCKET;

    for (it = result; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd == HPM_INVALID_SOCKET) continue;
        if (bind(fd, it->ai_addr, (hpm_socklen_t)it->ai_addrlen) == 0) break;
        hpm_socket_close(fd);
        fd = HPM_INVALID_SOCKET;
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
static int hpm_send_probe(hpm_socket_t fd,
    const struct sockaddr_storage *addr, hpm_socklen_t addr_len,
    unsigned type, uint64_t nonce)
{
    unsigned char packet[HPM_PACKET_SIZE];
    int sent;
    hpm_make_packet(packet, type, nonce);
    sent = (int)sendto(fd, (const char *)packet, (int)sizeof(packet), 0,
        (const struct sockaddr *)addr, addr_len);
    return sent == (int)sizeof(packet) ? 0 : -1;
}

/**
 * Extracts the port from a socket address.
 * @param addr Socket address.
 * @return Port in host byte order, or 0 if not found.
 */
static unsigned short hpm_addr_port(const struct sockaddr_storage *addr)
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
static int hpm_addr_host(const struct sockaddr_storage *addr,
    hpm_socklen_t addr_len, char *out, size_t out_size)
{
    if (!addr || !out || out_size == 0) return -1;
    return getnameinfo((const struct sockaddr *)addr, addr_len,
        out, (socklen_t)out_size, NULL, 0, NI_NUMERICHOST) == 0 ? 0 : -1;
}

/**
 * Fills the local address information in a path.
 * @param path Path to update.
 * @return None.
 */
static void hpm_fill_local(kc_hpm_path_t *path)
{
    struct sockaddr_storage addr;
    hpm_socklen_t addr_len = (hpm_socklen_t)sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(path->fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        path->local_port = hpm_addr_port(&addr);
        if (hpm_addr_host(&addr, addr_len,
            path->local_host, sizeof(path->local_host)) != 0)
            path->local_host[0] = '\0';
    }
}

/**
 * Returns the library build version.
 * @return Build version number.
 */
uint64_t kc_hpm_version(void)
{
    return (uint64_t)KC_HPM_BUILD_VERSION;
}

/**
 * Establishes a direct bidirectional UDP path to one supplied peer endpoint.
 * @param out Output path handle.
 * @param options Connection options.
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_connect(kc_hpm_path_t **out, const kc_hpm_options_t *options)
{
    hpm_candidate_t *candidates = NULL;
    size_t candidate_count = 0;
    hpm_socket_t fd = HPM_INVALID_SOCKET;
    uint64_t nonce;
    uint64_t timeout_ms;
    uint64_t deadline;
    uint64_t last_probe = 0;
    int family;
    int status = KC_HPM_ENET;
    int chosen = -1;
    size_t i;
#ifdef _WIN32
    WSADATA wsa;
    int wsa_started = 0;
#endif

    if (!out || !options || !options->peers || options->peer_count == 0)
        return KC_HPM_EINVAL;
    *out = NULL;

#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return KC_HPM_ENET;
    wsa_started = 1;
#endif

    family = hpm_choose_family(options);
    if (family == AF_UNSPEC) goto done;
    status = hpm_resolve_peers(options, family, &candidates, &candidate_count);
    if (status != KC_HPM_OK) goto done;

    fd = hpm_bind_socket(options, family);
    if (fd == HPM_INVALID_SOCKET) {
        status = KC_HPM_ENET;
        goto done;
    }

    nonce = hpm_nonce();
    timeout_ms = options->timeout_ms ? options->timeout_ms : HPM_DEFAULT_TIMEOUT_MS;
    deadline = hpm_now_ms() + timeout_ms;

    while (hpm_now_ms() < deadline) {
        uint64_t now = hpm_now_ms();
        uint64_t remaining;
        uint64_t wait_ms;
        int ready;

        if (last_probe == 0 || now - last_probe >= HPM_PROBE_INTERVAL_MS) {
            for (i = 0; i < candidate_count; ++i)
                (void)hpm_send_probe(fd, &candidates[i].addr,
                    candidates[i].addr_len, HPM_PACKET_HELLO, nonce);
            last_probe = now;
        }

        now = hpm_now_ms();
        if (now >= deadline) break;
        remaining = deadline - now;
        wait_ms = remaining < HPM_PROBE_INTERVAL_MS ?
            remaining : HPM_PROBE_INTERVAL_MS;
        ready = hpm_wait_readable(fd, wait_ms);
        if (ready < 0) {
            status = KC_HPM_ENET;
            goto done;
        }
        if (ready > 0) {
            unsigned char packet[256];
            struct sockaddr_storage source;
            hpm_socklen_t source_len = (hpm_socklen_t)sizeof(source);
            int received = (int)recvfrom(fd, (char *)packet, (int)sizeof(packet),
                0, (struct sockaddr *)&source, &source_len);
            int index;
            unsigned type;
            uint64_t packet_nonce;

            if (received <= 0) continue;
            index = hpm_candidate_index(candidates, candidate_count, &source);
            if (index < 0 || !hpm_parse_packet(packet, (size_t)received,
                &type, &packet_nonce)) continue;

            if (type == HPM_PACKET_HELLO) {
                candidates[index].saw_hello = 1;
                candidates[index].peer_nonce = packet_nonce;
                (void)hpm_send_probe(fd, &source, source_len,
                    HPM_PACKET_ACK, packet_nonce);
            } else if (type == HPM_PACKET_ACK && packet_nonce == nonce) {
                candidates[index].saw_ack = 1;
            }

            if (candidates[index].saw_hello && candidates[index].saw_ack) {
                chosen = index;
                break;
            }
        }
    }

    if (chosen < 0) {
        status = KC_HPM_ETIMEOUT;
        goto done;
    }

    for (i = 0; i < 3U; ++i) {
        if (candidates[chosen].peer_nonce != 0)
            (void)hpm_send_probe(fd, &candidates[chosen].addr,
                candidates[chosen].addr_len, HPM_PACKET_ACK,
                candidates[chosen].peer_nonce);
    }

    if (connect(fd, (const struct sockaddr *)&candidates[chosen].addr,
        candidates[chosen].addr_len) != 0) {
        status = KC_HPM_ENET;
        goto done;
    }

    {
        kc_hpm_path_t *path = (kc_hpm_path_t *)calloc(1, sizeof(*path));
        if (!path) {
            status = KC_HPM_ENOMEM;
            goto done;
        }
        path->fd = fd;
        fd = HPM_INVALID_SOCKET;
        path->peer_port = hpm_addr_port(&candidates[chosen].addr);
        if (hpm_addr_host(&candidates[chosen].addr,
            candidates[chosen].addr_len, path->peer_host,
            sizeof(path->peer_host)) != 0) path->peer_host[0] = '\0';
        hpm_fill_local(path);
        path->settle_until_ms = hpm_now_ms() + HPM_SETTLE_MS;
#ifdef _WIN32
        path->wsa_started = wsa_started;
        wsa_started = 0;
#endif
        *out = path;
        status = KC_HPM_OK;
    }

done:
    if (fd != HPM_INVALID_SOCKET) hpm_socket_close(fd);
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
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_send(kc_hpm_path_t *path, const void *data, size_t data_size)
{
    static const unsigned char empty = 0;
    int sent;
    if (!path || path->fd == HPM_INVALID_SOCKET) return KC_HPM_ECLOSED;
    if (!data && data_size != 0) return KC_HPM_EINVAL;
    if (data_size > HPM_MAX_DATAGRAM) return KC_HPM_EINVAL;
    sent = (int)send(path->fd,
        (const char *)(data_size ? data : &empty), (int)data_size, 0);
    return sent == (int)data_size ? KC_HPM_OK : KC_HPM_ENET;
}

/**
 * Receives one UDP datagram through an established path.
 * @param path Path handle.
 * @param out_data Output data buffer.
 * @param out_size Output data size.
 * @param timeout_ms Timeout in milliseconds (0 = wait indefinitely).
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_receive(kc_hpm_path_t *path, void **out_data,
    size_t *out_size, uint64_t timeout_ms)
{
    uint64_t deadline = timeout_ms ? hpm_now_ms() + timeout_ms : 0;

    if (!out_data || !out_size) return KC_HPM_EINVAL;
    *out_data = NULL;
    *out_size = 0;
    if (!path || path->fd == HPM_INVALID_SOCKET) return KC_HPM_ECLOSED;

    for (;;) {
        uint64_t wait_ms;
        int ready;
        unsigned char *buffer;
        int received;

        if (timeout_ms) {
            uint64_t now = hpm_now_ms();
            if (now >= deadline) return KC_HPM_ETIMEOUT;
            wait_ms = deadline - now;
        } else {
            wait_ms = 1000ULL;
        }
        if (wait_ms > 1000ULL) wait_ms = 1000ULL;
        ready = hpm_wait_readable(path->fd, wait_ms);
        if (ready < 0) return KC_HPM_ENET;
        if (ready == 0) {
            if (timeout_ms) continue;
            else continue;
        }

        buffer = (unsigned char *)malloc(HPM_MAX_DATAGRAM);
        if (!buffer) return KC_HPM_ENOMEM;
        received = (int)recv(path->fd, (char *)buffer, HPM_MAX_DATAGRAM, 0);
        if (received < 0) {
            free(buffer);
            return KC_HPM_ENET;
        }

        if (hpm_now_ms() <= path->settle_until_ms) {
            unsigned type;
            uint64_t probe_nonce;
            if (hpm_parse_packet(buffer, (size_t)received, &type, &probe_nonce)) {
                if (type == HPM_PACKET_HELLO) {
                    unsigned char ack[HPM_PACKET_SIZE];
                    hpm_make_packet(ack, HPM_PACKET_ACK, probe_nonce);
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
        return KC_HPM_OK;
    }
}

/**
 * Returns the local host address.
 * @param path Path handle.
 * @return Local host string or NULL if unknown.
 */
const char *kc_hpm_local_host(const kc_hpm_path_t *path)
{
    return path && path->local_host[0] ? path->local_host : NULL;
}

/**
 * Returns the local UDP port.
 * @param path Path handle.
 * @return Local port or 0 if unknown.
 */
unsigned short kc_hpm_local_port(const kc_hpm_path_t *path)
{
    return path ? path->local_port : 0;
}

/**
 * Returns the peer host address.
 * @param path Path handle.
 * @return Peer host string or NULL if unknown.
 */
const char *kc_hpm_peer_host(const kc_hpm_path_t *path)
{
    return path && path->peer_host[0] ? path->peer_host : NULL;
}

/**
 * Returns the peer UDP port.
 * @param path Path handle.
 * @return Peer port or 0 if unknown.
 */
unsigned short kc_hpm_peer_port(const kc_hpm_path_t *path)
{
    return path ? path->peer_port : 0;
}

/**
 * Frees memory allocated by libhpm.
 * @param ptr Pointer to free (may be NULL).
 * @return None.
 */
void kc_hpm_free(void *ptr)
{
    free(ptr);
}

/**
 * Closes and frees an established path.
 * @param path Path handle (may be NULL).
 * @return None.
 */
void kc_hpm_close(kc_hpm_path_t *path)
{
    if (!path) return;
    if (path->fd != HPM_INVALID_SOCKET) {
        hpm_socket_close(path->fd);
        path->fd = HPM_INVALID_SOCKET;
    }
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
const char *kc_hpm_strerror(int status)
{
    switch (status) {
        case KC_HPM_OK: return "ok";
        case KC_HPM_EINVAL: return "invalid argument";
        case KC_HPM_ENET: return "network error";
        case KC_HPM_ETIMEOUT: return "connection timed out";
        case KC_HPM_ENOMEM: return "out of memory";
        case KC_HPM_ECLOSED: return "path is closed";
        default: return "unknown hpm error";
    }
}
