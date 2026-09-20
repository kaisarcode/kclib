/**
 * libnets.c - Network sender.
 * Summary: Core implementation for sending byte buffers over TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include "libnets.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stddef.h>
#include <signal.h>
#include <stdarg.h>

#ifdef KC_NETS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

struct kc_nets {
    volatile sig_atomic_t stop_requested;
};

/**
 * Initialize a new nets context.
 * @param out Destination context pointer.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_open(kc_nets_t **out) {
    kc_nets_t *ctx;

    if (!out) return KC_NETS_EINVAL;
    *out = NULL;
    ctx = (kc_nets_t *)calloc(1, sizeof(kc_nets_t));
    if (!ctx) return KC_NETS_EINVAL;
    *out = ctx;
    return KC_NETS_OK;
}

/**
 * Release a nets context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_nets_close(kc_nets_t *ctx) {
    if (!ctx) return;
    free(ctx);
}

/**
 * Release response memory returned by nets. Accepts NULL.
 * @param ptr Response allocation to release, or NULL.
 * @return None.
 */
void kc_nets_free(void *ptr) { free(ptr); }

/**
 * Request stop for a specific nets context.
 * @param ctx Context handle.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_stop(kc_nets_t *ctx) {
    if (!ctx) return KC_NETS_EINVAL;
    ctx->stop_requested = 1;
    return KC_NETS_OK;
}

/**
 * Check whether a stop has been requested on the context.
 * @param ctx Context pointer.
 * @return 1 if stop was requested, 0 otherwise.
 */
int kc_nets_stop_requested(const kc_nets_t *ctx) {
    if (!ctx) {
        return 0;
    }

    return ctx->stop_requested ? 1 : 0;
}

#ifdef KC_NETS_OPENSSL

/**
 * Returns a lazily-initialized shared SSL context.
 * @return SSL_CTX pointer, or NULL on failure.
 */
static SSL_CTX *kc_nets_ssl_ctx(void) {
    static SSL_CTX *ctx = NULL;
    if (!ctx) {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
        ctx = SSL_CTX_new(TLS_client_method());
    }
    return ctx;
}
#endif

#ifdef _WIN32
typedef SOCKET kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET INVALID_SOCKET
#else
typedef int kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET -1
#endif

/**
 * Closes one socket.
 * @param sock Socket handle.
 * @return None.
 */
static void kc_nets_close_sock(kc_nets_socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

/**
 * Initializes the socket layer.
 * @return 0 on success, or -1 on failure.
 */
static int kc_nets_platform_init(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

/**
 * Cleans up the socket layer.
 * @return None.
 */
static void kc_nets_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Sends all bytes over a connected TCP socket.
 * @param ctx  Context handle.
 * @param sock Socket handle.
 * @param data Buffer pointer.
 * @param size Buffer size.
 * @return KC_NETS_OK on success, KC_NETS_ENET on error,
 *         KC_NETS_ESTOP if stopped.
 */
static int kc_nets_send_all(kc_nets_t *ctx, kc_nets_socket_t sock, const char *data, size_t size) {
    size_t sent = 0;

    while (sent < size) {
        if (ctx && ctx->stop_requested) return KC_NETS_ESTOP;
#ifdef _WIN32
        int n = send(sock, data + sent, (int)(size - sent), 0);
#else
        ssize_t n = send(sock, data + sent, size - sent, 0);
#endif
        if (n <= 0) return KC_NETS_ENET;
        sent += (size_t)n;
    }
    return KC_NETS_OK;
}

/**
 * Sends bytes through one resolved address and returns the response.
 * @param ctx      Context handle.
 * @param ai       Resolved address.
 * @param proto    Protocol selector.
 * @param data     Buffer pointer.
 * @param size     Buffer size.
 * @param host     Original hostname (for TLS SNI).
 * @param out_data Receives malloc'd response bytes.
 * @param out_size Receives response size.
 * @return KC_NETS_OK on success, or a negative error code.
 */
static int kc_nets_send_addr(
kc_nets_t *ctx,
const struct addrinfo *ai,
int proto,
const void *data,
size_t size,
const char *host,
void **out_data,
size_t *out_size
) {
    kc_nets_socket_t sock;
    int type = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;
    int rc;
    char *resp = NULL;
    size_t resp_used = 0;
#ifndef KC_NETS_OPENSSL
    (void)host;
#endif

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;

    if (ctx && ctx->stop_requested) return KC_NETS_ESTOP;

    sock = socket(ai->ai_family, type, ai->ai_protocol);
    if (sock == KC_NETS_BAD_SOCKET) return KC_NETS_ENET;

    if (proto == KC_NETS_UDP) {
#ifdef _WIN32
        int n = sendto(sock, (const char *)data, (int)size, 0, ai->ai_addr, (int)ai->ai_addrlen);
#else
        ssize_t n = sendto(sock, data, size, 0, ai->ai_addr, ai->ai_addrlen);
#endif
        kc_nets_close_sock(sock);
        return n < 0 || (size_t)n != size ? KC_NETS_ENET : KC_NETS_OK;
    }

    size_t resp_cap = 0;
    (void)resp_cap;

    if (connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
        kc_nets_close_sock(sock);
        return KC_NETS_ENET;
    }

#ifdef KC_NETS_OPENSSL
    if (proto == KC_NETS_TLS) {
        SSL *ssl;
        size_t sent;
        int n;
        char rbuf[65536];

        ssl = SSL_new(kc_nets_ssl_ctx());
        if (!ssl) { kc_nets_close_sock(sock); return KC_NETS_ENET; }
        SSL_set_fd(ssl, (int)sock);
        SSL_set_tlsext_host_name(ssl, host);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl);
            kc_nets_close_sock(sock);
            return KC_NETS_ENET;
        }

        sent = 0;
        while (sent < size) {
            if (ctx && ctx->stop_requested) {
                SSL_free(ssl);
                kc_nets_close_sock(sock);
                free(resp);
                return KC_NETS_ESTOP;
            }
            n = SSL_write(ssl, (const char *)data + sent, (int)(size - sent));
            if (n <= 0) {
                SSL_free(ssl);
                kc_nets_close_sock(sock);
                free(resp);
                return KC_NETS_ENET;
            }
            sent += (size_t)n;
        }

        SSL_shutdown(ssl);
        while ((n = SSL_read(ssl, rbuf, sizeof(rbuf))) > 0) {
            if (resp == NULL || resp_used + (size_t)n > resp_cap) {
                size_t next = resp_cap ? resp_cap * 2 : 65536;
                char *tmp;
                while (next < resp_used + (size_t)n) next *= 2;
                tmp = (char *)realloc(resp, next);
                if (tmp == NULL) {
                    SSL_free(ssl);
                    kc_nets_close_sock(sock);
                    free(resp);
                    return KC_NETS_ENET;
                }
                resp = tmp;
resp_cap = next;
            }
            memcpy(resp + resp_used, rbuf, (size_t)n);
            resp_used += (size_t)n;
        }
        SSL_free(ssl);
        kc_nets_close_sock(sock);
        if (out_data) *out_data = resp;
        if (out_size) *out_size = resp_used;
        return KC_NETS_OK;
    }
#endif

    rc = kc_nets_send_all(ctx, sock, (const char *)data, size);
    if (rc != KC_NETS_OK) {
        kc_nets_close_sock(sock);
        free(resp);
        return rc;
    }
#ifndef _WIN32
    shutdown(sock, SHUT_WR);
    for (;;) {
        char rbuf[65536];
        ssize_t n;
        if (ctx && ctx->stop_requested) {
            kc_nets_close_sock(sock);
            free(resp);
            return KC_NETS_ESTOP;
        }
        n = read(sock, rbuf, sizeof(rbuf));
        if (n <= 0) break;
        if (resp == NULL || resp_used + (size_t)n > resp_cap) {
            size_t next = resp_cap ? resp_cap * 2 : 65536;
            char *tmp;
            while (next < resp_used + (size_t)n) next *= 2;
            tmp = (char *)realloc(resp, next);
            if (tmp == NULL) {
                kc_nets_close_sock(sock);
                free(resp);
                return KC_NETS_ENET;
            }
            resp = tmp;
            resp_cap = next;
        }
        memcpy(resp + resp_used, rbuf, (size_t)n);
        resp_used += (size_t)n;
    }
#endif
    kc_nets_close_sock(sock);
    if (out_data) *out_data = resp;
    if (out_size) *out_size = resp_used;
    return KC_NETS_OK;
}

/**
 * Sends bytes to one network address.
 * @param ctx  Context handle.
 * @param host Destination host or IP address.
 * @param port Destination port.
 * @param proto KC_NETS_TCP or KC_NETS_UDP.
 * @param data Buffer to send.
 * @param data_size Buffer size in bytes.
 * @param out_data Receives malloc'd response bytes.
 * @param out_size Receives response size.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
kc_nets_t *ctx,
const char *host,
unsigned short port,
int proto,
const void *data,
size_t data_size,
void **out_data,
size_t *out_size
) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    char port_text[16];
    int rc;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;

    if (!ctx || !host || !host[0] || !data ||
        (proto != KC_NETS_TCP && proto != KC_NETS_UDP && proto != KC_NETS_TLS)) {
        return KC_NETS_EINVAL;
    }
#ifndef KC_NETS_OPENSSL
    if (proto == KC_NETS_TLS) return KC_NETS_ENET;
#endif
    if (kc_nets_platform_init() != 0) return KC_NETS_ENET;
    if (ctx->stop_requested) return KC_NETS_ESTOP;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = proto == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;

    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    rc = getaddrinfo(host, port_text, &hints, &res);
    if (rc != 0) {
        kc_nets_platform_cleanup();
        return KC_NETS_ENET;
    }

    rc = KC_NETS_ENET;
    for (ai = res; ai; ai = ai->ai_next) {
        rc = kc_nets_send_addr(ctx, ai, proto, data, data_size, host, out_data, out_size);
        if (rc == KC_NETS_OK) break;
    }

    freeaddrinfo(res);
    kc_nets_platform_cleanup();
    return rc;
}

/**
 * Returns a static message for a nets status code.
 * @param code Status code.
 * @return Static message.
 */
const char *kc_nets_strerror(int code) {
    switch (code) {
        case KC_NETS_OK: return "ok";
        case KC_NETS_EINVAL: return "invalid argument";
        case KC_NETS_ENET: return "network error";
        case KC_NETS_ESTOP: return "operation stopped";
        default: return "unknown error";
    }
}

#ifndef KC_NETS_BUILD_VERSION
#define KC_NETS_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_nets_version(void) {
    return (uint64_t)KC_NETS_BUILD_VERSION;
}

/**
 * Check if TLS support is compiled in.
 * @return 1 if TLS is available, 0 otherwise.
 */
int kc_nets_tls_available(void) {
#ifdef KC_NETS_OPENSSL
    return 1;
#else
    return 0;
#endif
}
