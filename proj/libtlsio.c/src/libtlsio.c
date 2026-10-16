/**
 * libtlsio.c - Authenticated TLS byte connection.
 * Summary: Connects to TLS servers and exchanges verified plaintext bytes.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtlsio.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET (-1)
#endif

#ifndef KC_TLSIO_BUILD_VERSION
#define KC_TLSIO_BUILD_VERSION 0
#endif

#define KC_TLSIO_READ_SIZE 65536U

struct kc_tlsio {
    kc_tlsio_socket_t socket;
    SSL_CTX *context;
    SSL *ssl;
    int platform_ready;
};

/**
 * Initializes platform socket support.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
static int kc_tlsio_platform_init(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0
        ? KC_TLSIO_OK
        : KC_TLSIO_ENET;
#else
    return KC_TLSIO_OK;
#endif
}

/**
 * Releases platform socket support.
 * @return void.
 */
static void kc_tlsio_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Closes one native socket.
 * @param socket Native socket.
 * @return void.
 */
static void kc_tlsio_socket_close(kc_tlsio_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Connects a TCP socket to one resolved host and port.
 * @param host Destination host.
 * @param port Destination port.
 * @param out_socket Destination native socket.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
static int kc_tlsio_socket_connect(
    const char *host,
    unsigned short port,
    kc_tlsio_socket_t *out_socket
) {
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    char service[16];
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%u", (unsigned int)port);

    addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) {
        return KC_TLSIO_ENET;
    }

    *out_socket = KC_TLSIO_BAD_SOCKET;
    rc = KC_TLSIO_ENET;
    for (address = addresses; address != NULL; address = address->ai_next) {
        kc_tlsio_socket_t socket_handle;

        socket_handle = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );
        if (socket_handle == KC_TLSIO_BAD_SOCKET) continue;
        if (connect(
                socket_handle,
                address->ai_addr,
                address->ai_addrlen
            ) == 0) {
            *out_socket = socket_handle;
            rc = KC_TLSIO_OK;
            break;
        }
        kc_tlsio_socket_close(socket_handle);
    }

    freeaddrinfo(addresses);
    return rc;
}

/**
 * Reports whether one host string is an IPv4 or IPv6 address.
 * @param host Host string.
 * @return Non-zero for an IP address, otherwise zero.
 */
static int kc_tlsio_host_is_ip(const char *host) {
    unsigned char storage[sizeof(struct in6_addr)];

    if (inet_pton(AF_INET, host, storage) == 1) return 1;
    return inet_pton(AF_INET6, host, storage) == 1;
}

/**
 * Configures SNI and peer identity verification.
 * @param ssl OpenSSL connection.
 * @param host Expected peer host.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ETLS on failure.
 */
static int kc_tlsio_configure_identity(SSL *ssl, const char *host) {
    X509_VERIFY_PARAM *verify;

    verify = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(
        verify,
        X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
    );

    if (kc_tlsio_host_is_ip(host)) {
        if (X509_VERIFY_PARAM_set1_ip_asc(verify, host) != 1) {
            return KC_TLSIO_ETLS;
        }
        return KC_TLSIO_OK;
    }

    if (SSL_set_tlsext_host_name(ssl, host) != 1) return KC_TLSIO_ETLS;
    if (SSL_set1_host(ssl, host) != 1) return KC_TLSIO_ETLS;
    return KC_TLSIO_OK;
}

/**
 * Maps one failed TLS handshake to a public status.
 * @param ssl OpenSSL connection.
 * @return KC_TLSIO_EVERIFY for peer verification failures, otherwise ETLS.
 */
static int kc_tlsio_handshake_error(SSL *ssl) {
    long verify;

    verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK && verify != X509_V_ERR_UNSPECIFIED) {
        return KC_TLSIO_EVERIFY;
    }
    return KC_TLSIO_ETLS;
}

/**
 * Opens one authenticated TLS connection.
 * @param out Destination connection pointer.
 * @param options Connection options.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_connect(
    kc_tlsio_t **out,
    const kc_tlsio_options_t *options
) {
    kc_tlsio_t *tls;
    unsigned short port;
    int rc;

    if (out == NULL) return KC_TLSIO_EINVAL;
    *out = NULL;
    if (options == NULL || options->host == NULL || options->host[0] == '\0') {
        return KC_TLSIO_EINVAL;
    }

    tls = (kc_tlsio_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) return KC_TLSIO_ENOMEM;
    tls->socket = KC_TLSIO_BAD_SOCKET;

    rc = kc_tlsio_platform_init();
    if (rc != KC_TLSIO_OK) {
        free(tls);
        return rc;
    }
    tls->platform_ready = 1;

    port = options->port == 0U ? 443U : options->port;
    rc = kc_tlsio_socket_connect(options->host, port, &tls->socket);
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }

    tls->context = SSL_CTX_new(TLS_client_method());
    if (tls->context == NULL) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    if (SSL_CTX_set_min_proto_version(tls->context, TLS1_2_VERSION) != 1) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    SSL_CTX_set_verify(tls->context, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(tls->context) != 1) {
        kc_tlsio_close(tls);
        return KC_TLSIO_EVERIFY;
    }

    tls->ssl = SSL_new(tls->context);
    if (tls->ssl == NULL) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    if (kc_tlsio_configure_identity(tls->ssl, options->host) != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    if (SSL_set_fd(tls->ssl, (int)tls->socket) != 1) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    if (SSL_connect(tls->ssl) != 1) {
        rc = kc_tlsio_handshake_error(tls->ssl);
        kc_tlsio_close(tls);
        return rc;
    }

    *out = tls;
    return KC_TLSIO_OK;
}

/**
 * Writes all supplied bytes to one TLS connection.
 * @param tls Connection handle.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_write(kc_tlsio_t *tls, const void *data, size_t size) {
    const unsigned char *bytes;
    size_t sent;

    if (tls == NULL || tls->ssl == NULL) return KC_TLSIO_EINVAL;
    if (data == NULL && size != 0U) return KC_TLSIO_EINVAL;
    if (size == 0U) return KC_TLSIO_OK;

    bytes = (const unsigned char *)data;
    sent = 0U;
    while (sent < size) {
        size_t remaining;
        int chunk;
        int count;

        remaining = size - sent;
        chunk = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        count = SSL_write(tls->ssl, bytes + sent, chunk);
        if (count <= 0) return KC_TLSIO_ETLS;
        sent += (size_t)count;
    }
    return KC_TLSIO_OK;
}

/**
 * Reads the next available TLS plaintext chunk.
 * @param tls Connection handle.
 * @param out_data Destination for allocated plaintext bytes.
 * @param out_size Destination plaintext byte count.
 * @return KC_TLSIO_OK, KC_TLSIO_EOF, or a negative status code.
 */
int kc_tlsio_read(
    kc_tlsio_t *tls,
    void **out_data,
    size_t *out_size
) {
    unsigned char *buffer;
    int count;
    int error;

    if (out_data == NULL || out_size == NULL) return KC_TLSIO_EINVAL;
    *out_data = NULL;
    *out_size = 0U;
    if (tls == NULL || tls->ssl == NULL) return KC_TLSIO_EINVAL;

    buffer = (unsigned char *)malloc(KC_TLSIO_READ_SIZE);
    if (buffer == NULL) return KC_TLSIO_ENOMEM;

    count = SSL_read(tls->ssl, buffer, (int)KC_TLSIO_READ_SIZE);
    if (count > 0) {
        *out_data = buffer;
        *out_size = (size_t)count;
        return KC_TLSIO_OK;
    }

    free(buffer);
    error = SSL_get_error(tls->ssl, count);
    if (error == SSL_ERROR_ZERO_RETURN) return KC_TLSIO_EOF;
    return KC_TLSIO_ETLS;
}

/**
 * Closes and releases one TLS connection.
 * @param tls Connection handle, or NULL.
 * @return void.
 */
void kc_tlsio_close(kc_tlsio_t *tls) {
    if (tls == NULL) return;
    if (tls->ssl != NULL) {
        SSL_shutdown(tls->ssl);
        SSL_free(tls->ssl);
    }
    if (tls->context != NULL) SSL_CTX_free(tls->context);
    if (tls->socket != KC_TLSIO_BAD_SOCKET) {
        kc_tlsio_socket_close(tls->socket);
    }
    if (tls->platform_ready) kc_tlsio_platform_cleanup();
    free(tls);
}

/**
 * Releases one allocation returned by tlsio.
 * @param ptr Allocation pointer, or NULL.
 * @return void.
 */
void kc_tlsio_free(void *ptr) {
    free(ptr);
}

/**
 * Returns a static message for one public status code.
 * @param code Public status code.
 * @return Static status message.
 */
const char *kc_tlsio_strerror(int code) {
    switch (code) {
        case KC_TLSIO_OK:
            return "success";
        case KC_TLSIO_EOF:
            return "peer closed the TLS connection";
        case KC_TLSIO_EINVAL:
            return "invalid argument";
        case KC_TLSIO_ENET:
            return "network connection failed";
        case KC_TLSIO_ETLS:
            return "TLS operation failed";
        case KC_TLSIO_EVERIFY:
            return "TLS peer verification failed";
        case KC_TLSIO_ENOMEM:
            return "memory allocation failed";
        default:
            return "unknown tlsio status";
    }
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tlsio_version(void) {
    return (uint64_t)KC_TLSIO_BUILD_VERSION;
}
