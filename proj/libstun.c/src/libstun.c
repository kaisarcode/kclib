/**
 * libstun.c - STUN UDP mappings and Binding endpoints.
 * Summary: Discovers UDP mappings and serves STUN Binding requests.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "libstun.h"
#include <limits.h>
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
#include <bcrypt.h>
typedef SOCKET kc_sock_t;
typedef int kc_socklen_t;
typedef HANDLE kc_thread_t;
#define KC_BAD_SOCKET INVALID_SOCKET
#define kc_close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_sock_t;
typedef socklen_t kc_socklen_t;
typedef pthread_t kc_thread_t;
#define KC_BAD_SOCKET (-1)
#define kc_close_socket close
#endif
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef KC_STUN_BUILD_VERSION
#define KC_STUN_BUILD_VERSION 0
#endif
#define STUN_PORT 3478U
#define STUN_TIMEOUT 3000ULL
#define STUN_MAGIC 0x2112A442U
#define STUN_BINDING 0x0001U
#define STUN_SUCCESS 0x0101U
#define STUN_XOR_MAPPED 0x0020U
#define STUN_MAX 65535U

struct kc_stun_mapping {
    kc_sock_t fd;
    struct sockaddr_storage server;
    kc_socklen_t server_len;
    int family;
    char local_host[NI_MAXHOST];
    unsigned short local_port;
    char mapped_host[NI_MAXHOST];
    unsigned short mapped_port;
    unsigned char refresh_id[12];
    int refresh_pending;
#ifdef _WIN32
    int wsa;
#endif
};

struct kc_stun_server {
    kc_sock_t fd4;
    kc_sock_t fd6;
    kc_sock_t wake_rx;
    kc_sock_t wake_tx;
    struct sockaddr_storage wake_addr;
    kc_socklen_t wake_len;
    unsigned short port;
    kc_thread_t thread;
    int thread_started;
#ifdef _WIN32
    int wsa;
#endif
};

static uint16_t rd16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

/**
 * Returns the current monotonic time in milliseconds.
 * @return Current time in milliseconds.
 */
static uint64_t now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ?
        (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL : 0;
#endif
}
static uint64_t deadline_ms(uint64_t timeout) { uint64_t n = now_ms(); return UINT64_MAX - n < timeout ? UINT64_MAX : n + timeout; }
static uint64_t remaining_ms(uint64_t deadline) { uint64_t n = now_ms(); return n >= deadline ? 0 : deadline - n; }

/**
 * Initializes the socket runtime.
 * @return 1 on success, 0 on failure.
 */
static int runtime_start(void) {
#ifdef _WIN32
    WSADATA wsa; return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return 1;
#endif
}

/**
 * Stops the socket runtime.
 * @return void
 */
static void runtime_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Fills the buffer with random bytes.
 * @return 1 on success, 0 on failure.
 */
static int random_bytes(unsigned char *out, size_t n) {
#ifdef _WIN32
    return n <= ULONG_MAX && BCryptGenRandom(NULL, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    size_t off = 0;
    if (fd < 0) return 0;
    while (off < n) {
        ssize_t got = read(fd, out + off, n - off);
        if (got > 0) off += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else { close(fd); return 0; }
    }
    close(fd); return 1;
#endif
}

/**
 * Parses a STUN server specification into host and port.
 * @return 1 on success, 0 on failure.
 */
static int parse_server(const char *text, char host[NI_MAXHOST], unsigned short *port) {
    const char *p, *end, *colon = NULL, *s;
    char tmp[16], *tail;
    unsigned long v;
    int colons = 0;
    size_t n;
    if (!text || !*text || !port) return 0;
    p = strncmp(text, "stun:", 5) == 0 ? text + 5 : text;
    if (!*p) return 0;
    *port = STUN_PORT;
    if (*p == '[') {
        end = strchr(p + 1, ']');
        if (!end || end == p + 1) return 0;
        n = (size_t)(end - p - 1);
        if (n >= NI_MAXHOST) return 0;
        memcpy(host, p + 1, n); host[n] = 0;
        if (!end[1]) return 1;
        if (end[1] != ':' || !end[2]) return 0;
        p = end + 2;
    } else {
        for (s = p; *s; ++s) if (*s == ':') { colon = s; ++colons; }
        if (colons == 1) {
            n = (size_t)(colon - p);
            if (!n || n >= NI_MAXHOST || !colon[1]) return 0;
            memcpy(host, p, n); host[n] = 0; p = colon + 1;
        } else {
            n = strlen(p); if (!n || n >= NI_MAXHOST) return 0;
            memcpy(host, p, n + 1); return 1;
        }
    }
    n = strlen(p); if (!n || n >= sizeof(tmp)) return 0;
    memcpy(tmp, p, n + 1); v = strtoul(tmp, &tail, 10);
    if (*tail || !v || v > 65535UL) return 0;
    *port = (unsigned short)v; return 1;
}

/**
 * Compares two socket endpoints for equality.
 * @return 1 if equal, 0 otherwise.
 */
static int endpoint_equal(const struct sockaddr_storage *a, const struct sockaddr_storage *b) {
    if (a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *x = (const struct sockaddr_in *)a, *y = (const struct sockaddr_in *)b;
        return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a, *y = (const struct sockaddr_in6 *)b;
        return x->sin6_port == y->sin6_port && x->sin6_scope_id == y->sin6_scope_id && memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(x->sin6_addr)) == 0;
    }
    return 0;
}

/**
 * Converts a socket endpoint to text host and port.
 * @return 1 on success, 0 on failure.
 */
static int endpoint_text(const struct sockaddr_storage *a, kc_socklen_t len, char host[NI_MAXHOST], unsigned short *port) {
    char service[16], *tail;
    unsigned long v;
    if (getnameinfo((const struct sockaddr *)a, len, host, NI_MAXHOST, service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) != 0) return 0;
    v = strtoul(service, &tail, 10); if (*tail || v > 65535UL) return 0;
    *port = (unsigned short)v; return 1;
}

/**
 * Binds a UDP socket to the given host and port.
 * @return Socket descriptor on success, KC_BAD_SOCKET on failure.
 */
static kc_sock_t bind_udp(const char *host, unsigned short port, int family) {
    struct addrinfo hints, *list = NULL, *it;
    char service[16]; kc_sock_t fd = KC_BAD_SOCKET;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = family; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP; hints.ai_flags = AI_PASSIVE;
    snprintf(service, sizeof(service), "%u", port);
    if (getaddrinfo(host && *host ? host : NULL, service, &hints, &list) != 0) return KC_BAD_SOCKET;
    for (it = list; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol); if (fd == KC_BAD_SOCKET) continue;
        if (it->ai_family == AF_INET6) { int one = 1; if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&one, (kc_socklen_t)sizeof(one)) != 0) { kc_close_socket(fd); fd = KC_BAD_SOCKET; continue; } }
        if (bind(fd, it->ai_addr, (kc_socklen_t)it->ai_addrlen) == 0) break;
        kc_close_socket(fd); fd = KC_BAD_SOCKET;
    }
    freeaddrinfo(list); return fd;
}

/**
 * Waits for a socket to become readable.
 * @return 1 if readable, 0 on timeout, -1 on error.
 */
static int wait_readable(kc_sock_t fd, uint64_t ms, int infinite) {
    fd_set set; struct timeval tv, *p = NULL; int rc;
    FD_ZERO(&set); FD_SET(fd, &set);
    if (!infinite) { tv.tv_sec = (long)(ms / 1000ULL); tv.tv_usec = (long)((ms % 1000ULL) * 1000ULL); p = &tv; }
#ifdef _WIN32
    rc = select(0, &set, NULL, NULL, p);
#else
    rc = select(fd + 1, &set, NULL, NULL, p);
#endif
    if (rc >= 0) return rc ? 1 : 0;
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR ? 0 : -1;
#else
    return errno == EINTR ? 0 : -1;
#endif
}

/**
 * Resolves a peer hostname and port to a socket address.
 * @return 1 on success, 0 on failure.
 */
static int resolve_peer(const char *host, unsigned short port, int family, struct sockaddr_storage *out, kc_socklen_t *len) {
    struct addrinfo hints, *list = NULL, *it; char service[16]; int ok = 0;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = family; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
    snprintf(service, sizeof(service), "%u", port);
    if (getaddrinfo(host, service, &hints, &list) != 0) return 0;
    for (it = list; it; it = it->ai_next) if (it->ai_addrlen > 0 && (size_t)it->ai_addrlen <= sizeof(*out)) { memset(out, 0, sizeof(*out)); memcpy(out, it->ai_addr, (size_t)it->ai_addrlen); *len = (kc_socklen_t)it->ai_addrlen; ok = 1; break; }
    freeaddrinfo(list); return ok;
}

/**
 * Returns the local port of a bound socket.
 * @return Port number in host byte order, or 0 on failure.
 */
static unsigned short socket_port(kc_sock_t fd) {
    struct sockaddr_storage addr; kc_socklen_t len = (kc_socklen_t)sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) return 0;
    if (addr.ss_family == AF_INET) return ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return 0;
}

/**
 * Creates the wake-up socket pair for the server.
 * @return 1 on success, 0 on failure.
 */
static int server_make_wake(kc_stun_server_t *s) {
    int family = s->fd4 != KC_BAD_SOCKET ? AF_INET : AF_INET6;
    const char *host = family == AF_INET ? "127.0.0.1" : "::1";
    s->wake_rx = bind_udp(host, 0, family);
    if (s->wake_rx == KC_BAD_SOCKET) return 0;
    s->wake_len = (kc_socklen_t)sizeof(s->wake_addr);
    if (getsockname(s->wake_rx, (struct sockaddr *)&s->wake_addr, &s->wake_len) != 0) return 0;
    s->wake_tx = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    return s->wake_tx != KC_BAD_SOCKET;
}

/**
 * Validates a STUN Binding Request.
 * @return 1 if valid, 0 otherwise.
 */
static int binding_request_valid(const unsigned char *p, size_t n) {
    uint16_t len;
    if (!p || n < 20 || (p[0] & 0xc0U) || rd16(p) != STUN_BINDING || rd32(p + 4) != STUN_MAGIC) return 0;
    len = rd16(p + 2);
    return !(len & 3U) && 20U + (size_t)len == n;
}

/**
 * Builds a STUN Binding Success response.
 * @return Number of bytes written, or 0 on failure.
 */
static size_t make_success(const unsigned char *request, const struct sockaddr_storage *peer,
    unsigned char *out, size_t cap) {
    unsigned char raw[16], mask[16] = {0x21,0x12,0xa4,0x42};
    unsigned short port; size_t n, attr_len, total, i;
    if (!request || !peer || !out) return 0;
    if (peer->ss_family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)peer;
        memcpy(raw, &v4->sin_addr, 4); port = ntohs(v4->sin_port); n = 4;
    } else if (peer->ss_family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)peer;
        memcpy(raw, &v6->sin6_addr, 16); port = ntohs(v6->sin6_port); n = 16;
    } else return 0;
    attr_len = 4U + n; total = 24U + attr_len;
    if (cap < total) return 0;
    memset(out, 0, total); wr16(out, STUN_SUCCESS); wr16(out + 2, (uint16_t)(4U + attr_len));
    out[4] = 0x21; out[5] = 0x12; out[6] = 0xa4; out[7] = 0x42; memcpy(out + 8, request + 8, 12);
    wr16(out + 20, STUN_XOR_MAPPED); wr16(out + 22, (uint16_t)attr_len); out[24] = 0; out[25] = peer->ss_family == AF_INET ? 1 : 2;
    wr16(out + 26, (uint16_t)(port ^ (STUN_MAGIC >> 16))); memcpy(mask + 4, request + 8, 12);
    for (i = 0; i < n; ++i) out[28 + i] = raw[i] ^ mask[i];
    return total;
}

/**
 * Processes one incoming STUN request and sends a response.
 * @return void
 */
static void server_reply(kc_sock_t fd) {
    unsigned char request[1280], response[64]; struct sockaddr_storage peer;
    kc_socklen_t peer_len = (kc_socklen_t)sizeof(peer); int got; size_t size;
#ifdef _WIN32
    got = recvfrom(fd, (char *)request, (int)sizeof(request), 0, (struct sockaddr *)&peer, &peer_len);
#else
    got = (int)recvfrom(fd, request, sizeof(request), 0, (struct sockaddr *)&peer, &peer_len);
#endif
    if (got < 20 || !binding_request_valid(request, (size_t)got)) return;
    size = make_success(request, &peer, response, sizeof(response)); if (!size) return;
#ifdef _WIN32
    (void)sendto(fd, (const char *)response, (int)size, 0, (const struct sockaddr *)&peer, peer_len);
#else
    (void)sendto(fd, response, size, 0, (const struct sockaddr *)&peer, peer_len);
#endif
}

#ifdef _WIN32
/**
 * Server thread entry point (Windows).
 * @return Thread exit code.
 */
static DWORD WINAPI server_thread(LPVOID arg)
#else
/**
 * Server thread entry point (POSIX).
 * @return Thread exit code.
 */
static void *server_thread(void *arg)
#endif
{
    kc_stun_server_t *s = (kc_stun_server_t *)arg;
    for (;;) {
        fd_set set; int rc;
#ifndef _WIN32
        kc_sock_t maxfd = s->wake_rx;
#endif
        FD_ZERO(&set); FD_SET(s->wake_rx, &set);
        if (s->fd4 != KC_BAD_SOCKET) { FD_SET(s->fd4, &set);
#ifndef _WIN32
            if (s->fd4 > maxfd) maxfd = s->fd4;
#endif
        }
        if (s->fd6 != KC_BAD_SOCKET) { FD_SET(s->fd6, &set);
#ifndef _WIN32
            if (s->fd6 > maxfd) maxfd = s->fd6;
#endif
        }
#ifdef _WIN32
        rc = select(0, &set, NULL, NULL, NULL);
#else
        rc = select(maxfd + 1, &set, NULL, NULL, NULL);
#endif
        if (rc < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            break;
        }
        if (FD_ISSET(s->wake_rx, &set)) { char byte[8]; (void)recvfrom(s->wake_rx, byte, sizeof(byte), 0, NULL, NULL); break; }
        if (s->fd4 != KC_BAD_SOCKET && FD_ISSET(s->fd4, &set)) server_reply(s->fd4);
        if (s->fd6 != KC_BAD_SOCKET && FD_ISSET(s->fd6, &set)) server_reply(s->fd6);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Builds a STUN Binding Request packet.
 * @return void
 */
static void make_request(unsigned char out[20], const unsigned char id[12]) {
    memset(out, 0, 20); wr16(out, STUN_BINDING); out[4] = 0x21; out[5] = 0x12; out[6] = 0xa4; out[7] = 0x42; memcpy(out + 8, id, 12);
}

/**
 * Sends a STUN Binding Request.
 * @return KC_STUN_OK on success, KC_STUN_ENET on failure.
 */
static int send_binding(kc_stun_mapping_t *m, unsigned char id[12]) {
    unsigned char packet[20]; int n;
    if (!random_bytes(id, 12)) return KC_STUN_ENET;
    make_request(packet, id);
#ifdef _WIN32
    n = sendto(m->fd, (const char *)packet, 20, 0, (const struct sockaddr *)&m->server, m->server_len);
#else
    n = (int)sendto(m->fd, packet, 20, 0, (const struct sockaddr *)&m->server, m->server_len);
#endif
    return n == 20 ? KC_STUN_OK : KC_STUN_ENET;
}

/**
 * Parses a STUN Binding Success response.
 * @return 1 on success, 0 on failure, -1 on malformed.
 */
static int parse_response(const unsigned char *p, size_t n, const unsigned char id[12], char host[NI_MAXHOST], unsigned short *port) {
    size_t end, off; uint16_t len;
    if (n < 20 || (p[0] & 0xc0U) || rd16(p) != STUN_SUCCESS || rd32(p + 4) != STUN_MAGIC || memcmp(p + 8, id, 12)) return 0;
    len = rd16(p + 2); if ((len & 3U) || 20U + len > n) return -1; end = 20U + len;
    for (off = 20; off + 4 <= end;) {
        uint16_t type = rd16(p + off), alen = rd16(p + off + 2); size_t value = off + 4, padded = ((size_t)alen + 3U) & ~3U;
        if (value + alen > end || value + padded > end) return -1;
        if (type == STUN_XOR_MAPPED) {
            unsigned char raw[16], mask[16] = {0x21,0x12,0xa4,0x42}; size_t sz; int af;
            if (alen < 8 || p[value] != 0) return -1;
            if (p[value + 1] == 1) { sz = 4; af = AF_INET; } else if (p[value + 1] == 2) { sz = 16; af = AF_INET6; } else return -1;
            if (alen < 4U + sz) return -1;
            *port = (unsigned short)(rd16(p + value + 2) ^ (STUN_MAGIC >> 16));
            memcpy(mask + 4, id, 12); memcpy(raw, p + value + 4, sz);
            for (size_t i = 0; i < sz; ++i) raw[i] ^= mask[i];
            return inet_ntop(af, raw, host, NI_MAXHOST) ? 1 : -1;
        }
        off = value + padded;
    }
    return -1;
}

/**
 * Performs a STUN transaction with retry logic.
 * @return KC_STUN_OK on success, error code on failure.
 */
static int transact(kc_stun_mapping_t *m, uint64_t timeout) {
    unsigned char id[12], packet[STUN_MAX + 1U]; uint64_t end; int rc;
    rc = send_binding(m, id); if (rc != KC_STUN_OK) return rc; end = deadline_ms(timeout);
    for (;;) {
        struct sockaddr_storage from; kc_socklen_t flen = (kc_socklen_t)sizeof(from); uint64_t left = remaining_ms(end); int ready, got, parsed;
        if (!left) return KC_STUN_ETIMEOUT;
        ready = wait_readable(m->fd, left, 0);
        if (ready < 0) return KC_STUN_ENET;
        if (!ready) return KC_STUN_ETIMEOUT;
#ifdef _WIN32
        got = recvfrom(m->fd, (char *)packet, (int)sizeof(packet), 0, (struct sockaddr *)&from, &flen);
#else
        got = (int)recvfrom(m->fd, packet, sizeof(packet), 0, (struct sockaddr *)&from, &flen);
#endif
        if (got < 0) return KC_STUN_ENET;
        if (!endpoint_equal(&from, &m->server)) continue;
        parsed = parse_response(packet, (size_t)got, id, m->mapped_host, &m->mapped_port); if (parsed > 0) return KC_STUN_OK; if (parsed < 0) return KC_STUN_EPROTO;
    }
}

/**
 * Starts a STUN server listener.
 * @return KC_STUN_OK on success, error code on failure.
 */
int kc_stun_listen(kc_stun_server_t **out, const kc_stun_server_options_t *o) {
    kc_stun_server_t *s; unsigned short port;
    if (!out || !o) return KC_STUN_EINVAL;
    *out = NULL;
    if (!runtime_start()) return KC_STUN_ENET;
    s = (kc_stun_server_t *)calloc(1, sizeof(*s));
    if (!s) { runtime_stop(); return KC_STUN_ENOMEM; }
    s->fd4 = KC_BAD_SOCKET; s->fd6 = KC_BAD_SOCKET; s->wake_rx = KC_BAD_SOCKET; s->wake_tx = KC_BAD_SOCKET;
#ifdef _WIN32
    s->wsa = 1;
#endif
    s->fd4 = bind_udp(o->host, o->port, AF_INET);
    port = s->fd4 != KC_BAD_SOCKET ? socket_port(s->fd4) : o->port;
    s->fd6 = bind_udp(o->host, port, AF_INET6);
    if (s->fd4 == KC_BAD_SOCKET && s->fd6 == KC_BAD_SOCKET) { kc_stun_server_close(s); return KC_STUN_ENET; }
    s->port = s->fd4 != KC_BAD_SOCKET ? socket_port(s->fd4) : socket_port(s->fd6);
    if (!s->port || !server_make_wake(s)) { kc_stun_server_close(s); return KC_STUN_ENET; }
#ifdef _WIN32
    s->thread = CreateThread(NULL, 0, server_thread, s, 0, NULL);
    if (!s->thread) { kc_stun_server_close(s); return KC_STUN_ENET; }
#else
    if (pthread_create(&s->thread, NULL, server_thread, s) != 0) { kc_stun_server_close(s); return KC_STUN_ENET; }
#endif
    s->thread_started = 1; *out = s; return KC_STUN_OK;
}

/**
 * Returns the local port of the STUN server.
 * @return Port number in host byte order, or 0 if not available.
 */
unsigned short kc_stun_server_port(const kc_stun_server_t *s) { return s ? s->port : 0; }

/**
 * Closes the STUN server and cleans up resources.
 * @return void
 */
void kc_stun_server_close(kc_stun_server_t *s) {
    if (!s) return;
    if (s->thread_started) {
        const char byte = 0;
#ifdef _WIN32
        (void)sendto(s->wake_tx, &byte, 1, 0, (const struct sockaddr *)&s->wake_addr, s->wake_len);
        WaitForSingleObject(s->thread, INFINITE); CloseHandle(s->thread);
#else
        (void)sendto(s->wake_tx, &byte, 1, 0, (const struct sockaddr *)&s->wake_addr, s->wake_len);
        pthread_join(s->thread, NULL);
#endif
        s->thread_started = 0;
    }
    if (s->fd4 != KC_BAD_SOCKET) kc_close_socket(s->fd4);
    if (s->fd6 != KC_BAD_SOCKET) kc_close_socket(s->fd6);
    if (s->wake_rx != KC_BAD_SOCKET) kc_close_socket(s->wake_rx);
    if (s->wake_tx != KC_BAD_SOCKET) kc_close_socket(s->wake_tx);
#ifdef _WIN32
    if (s->wsa) runtime_stop();
#endif
    free(s);
}

/**
 * Discovers a STUN mapping.
 * @return KC_STUN_OK on success, error code on failure.
 */
int kc_stun_discover(kc_stun_mapping_t **out, const kc_stun_options_t *o) {
    char server_host[NI_MAXHOST], service[16]; unsigned short server_port; struct addrinfo hints, *list = NULL, *it; uint64_t timeout, end; int result = KC_STUN_ENET, started = 0;
    if (!out || !o || !o->server || !*o->server) return KC_STUN_EINVAL;
    *out = NULL;
    if (!parse_server(o->server, server_host, &server_port)) return KC_STUN_EINVAL;
    if (!runtime_start()) return KC_STUN_ENET;
    started = 1;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP; snprintf(service, sizeof(service), "%u", server_port);
    if (getaddrinfo(server_host, service, &hints, &list) != 0) { runtime_stop(); return KC_STUN_ENET; }
    timeout = o->timeout_ms ? o->timeout_ms : STUN_TIMEOUT; end = deadline_ms(timeout);
    for (it = list; it; it = it->ai_next) {
        kc_stun_mapping_t *m; kc_sock_t fd; struct sockaddr_storage local; kc_socklen_t llen = (kc_socklen_t)sizeof(local); uint64_t left, slice;
        if ((it->ai_family != AF_INET && it->ai_family != AF_INET6) || it->ai_addrlen <= 0 || (size_t)it->ai_addrlen > sizeof(struct sockaddr_storage)) continue;
        left = remaining_ms(end); if (!left) { result = KC_STUN_ETIMEOUT; break; } slice = it->ai_next && left > 1000 ? 1000 : left;
        fd = bind_udp(o->host, o->port, it->ai_family); if (fd == KC_BAD_SOCKET) continue;
        m = (kc_stun_mapping_t *)calloc(1, sizeof(*m)); if (!m) { kc_close_socket(fd); result = KC_STUN_ENOMEM; break; }
        m->fd = fd; m->family = it->ai_family; memcpy(&m->server, it->ai_addr, (size_t)it->ai_addrlen); m->server_len = (kc_socklen_t)it->ai_addrlen;
#ifdef _WIN32
        m->wsa = 1;
#endif
        if (getsockname(fd, (struct sockaddr *)&local, &llen) != 0 || !endpoint_text(&local, llen, m->local_host, &m->local_port)) { kc_stun_close(m); started = 0; result = KC_STUN_ENET; break; }
        result = transact(m, slice); if (result == KC_STUN_OK) { *out = m; started = 0; break; }
        kc_stun_close(m); started = 0; if (!runtime_start()) { result = KC_STUN_ENET; break; } started = 1;
    }
    freeaddrinfo(list); if (started) runtime_stop(); return *out ? KC_STUN_OK : result;
}

/**
 * Refreshes a STUN mapping.
 * @return KC_STUN_OK on success, error code on failure.
 */
int kc_stun_refresh(kc_stun_mapping_t *m) { int rc; if (!m) return KC_STUN_EINVAL; if (m->fd == KC_BAD_SOCKET) return KC_STUN_ECLOSED; rc = send_binding(m, m->refresh_id); if (rc == KC_STUN_OK) m->refresh_pending = 1; return rc; }

/**
 * Sends data through a STUN mapping.
 * @return KC_STUN_OK on success, error code on failure.
 */
int kc_stun_send(kc_stun_mapping_t *m, const kc_stun_endpoint_t *peer, const void *data, size_t size) {
    struct sockaddr_storage to; kc_socklen_t len; int n;
    if (!m || !peer || !peer->host || !*peer->host || !peer->port || (size && !data) || size > STUN_MAX || size > INT_MAX) return KC_STUN_EINVAL;
    if (m->fd == KC_BAD_SOCKET) return KC_STUN_ECLOSED;
    if (!resolve_peer(peer->host, peer->port, m->family, &to, &len)) return KC_STUN_ENET;
#ifdef _WIN32
    n = sendto(m->fd, (const char *)data, (int)size, 0, (const struct sockaddr *)&to, len);
#else
    n = (int)sendto(m->fd, data, size, 0, (const struct sockaddr *)&to, len);
#endif
    return n == (int)size ? KC_STUN_OK : KC_STUN_ENET;
}

/**
 * Receives data through a STUN mapping.
 * @return KC_STUN_OK on success, error code on failure.
 */
int kc_stun_receive(kc_stun_mapping_t *m, kc_stun_datagram_t *out, uint64_t timeout) {
    unsigned char packet[STUN_MAX + 1U]; uint64_t end = timeout ? deadline_ms(timeout) : 0;
    if (!m || !out) return KC_STUN_EINVAL;
    if (m->fd == KC_BAD_SOCKET) return KC_STUN_ECLOSED;
    memset(out, 0, sizeof(*out));
    for (;;) {
        struct sockaddr_storage from; kc_socklen_t flen = (kc_socklen_t)sizeof(from); uint64_t left = timeout ? remaining_ms(end) : 0; int ready, got; char host[NI_MAXHOST]; unsigned short port; size_t hlen;
        if (timeout && !left) return KC_STUN_ETIMEOUT;
        ready = wait_readable(m->fd, left, !timeout);
        if (ready < 0) return KC_STUN_ENET;
        if (!ready) return KC_STUN_ETIMEOUT;
#ifdef _WIN32
        got = recvfrom(m->fd, (char *)packet, (int)sizeof(packet), 0, (struct sockaddr *)&from, &flen);
#else
        got = (int)recvfrom(m->fd, packet, sizeof(packet), 0, (struct sockaddr *)&from, &flen);
#endif
        if (got < 0) return KC_STUN_ENET;
        if (endpoint_equal(&from, &m->server) && got >= 20 && !(packet[0] & 0xc0U) && rd32(packet + 4) == STUN_MAGIC) {
            if (m->refresh_pending && !memcmp(packet + 8, m->refresh_id, 12)) { char mapped[NI_MAXHOST]; unsigned short mapped_port; if (parse_response(packet, (size_t)got, m->refresh_id, mapped, &mapped_port) > 0) { memcpy(m->mapped_host, mapped, strlen(mapped) + 1); m->mapped_port = mapped_port; } m->refresh_pending = 0; }
            continue;
        }
        if (!endpoint_text(&from, flen, host, &port)) return KC_STUN_ENET;
        hlen = strlen(host);
        out->host = (char *)malloc(hlen + 1); out->data = malloc(got > 0 ? (size_t)got : 1U);
        if (!out->host || !out->data) { kc_stun_datagram_free(out); return KC_STUN_ENOMEM; }
        memcpy(out->host, host, hlen + 1); if (got > 0) memcpy(out->data, packet, (size_t)got); out->port = port; out->data_size = (size_t)got; return KC_STUN_OK;
    }
}
const char *kc_stun_local_host(const kc_stun_mapping_t *m) { return m && m->fd != KC_BAD_SOCKET && *m->local_host ? m->local_host : NULL; }
unsigned short kc_stun_local_port(const kc_stun_mapping_t *m) { return m && m->fd != KC_BAD_SOCKET ? m->local_port : 0; }
const char *kc_stun_mapped_host(const kc_stun_mapping_t *m) { return m && m->fd != KC_BAD_SOCKET && *m->mapped_host ? m->mapped_host : NULL; }
unsigned short kc_stun_mapped_port(const kc_stun_mapping_t *m) { return m && m->fd != KC_BAD_SOCKET ? m->mapped_port : 0; }
void kc_stun_datagram_free(kc_stun_datagram_t *d) { if (!d) return; free(d->host); free(d->data); memset(d, 0, sizeof(*d)); }

/**
 * Closes a STUN mapping and cleans up resources.
 * @return void
 */
void kc_stun_close(kc_stun_mapping_t *m) {
    if (!m) return;
    if (m->fd != KC_BAD_SOCKET) kc_close_socket(m->fd);
#ifdef _WIN32
    if (m->wsa) runtime_stop();
#endif
    free(m);
}
const char *kc_stun_strerror(int s) { switch (s) { case KC_STUN_OK: return "ok"; case KC_STUN_EINVAL: return "invalid argument"; case KC_STUN_ENET: return "network error"; case KC_STUN_ETIMEOUT: return "timeout"; case KC_STUN_EPROTO: return "invalid STUN response"; case KC_STUN_ENOMEM: return "out of memory"; case KC_STUN_ECLOSED: return "mapping closed"; default: return "unknown status"; } }
uint64_t kc_stun_version(void) { return (uint64_t)KC_STUN_BUILD_VERSION; }
