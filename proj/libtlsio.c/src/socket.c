/**
 * socket.c - Portable tlsio socket transport.
 * Summary: Connects native TCP sockets and moves encrypted TLS bytes.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <mbedtls/net_sockets.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/**
 * Initializes native socket support.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
int kc_tlsio_internal_platform_init(void) {
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
 * Releases native socket support.
 * @return void.
 */
void kc_tlsio_internal_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Closes one native socket.
 * @param socket Native socket.
 * @return void.
 */
void kc_tlsio_internal_socket_close(kc_tlsio_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Connects one TCP socket.
 * @param host Destination host.
 * @param port Destination port.
 * @param out_socket Destination native socket.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
int kc_tlsio_internal_socket_connect(
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
        kc_tlsio_internal_socket_close(socket_handle);
    }

    freeaddrinfo(addresses);
    return rc;
}

/**
 * Sends encrypted bytes through the native socket.
 * @param context Native socket pointer.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return Positive byte count or an Mbed TLS transport error.
 */
int kc_tlsio_internal_send(
    void *context,
    const unsigned char *data,
    size_t size
) {
    kc_tlsio_socket_t socket_handle;
#ifdef _WIN32
    int count;
#else
    int flags;
    ssize_t count;
#endif

    socket_handle = *(kc_tlsio_socket_t *)context;
#ifdef _WIN32
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    count = send(socket_handle, (const char *)data, (int)size, 0);
    if (count == SOCKET_ERROR) return MBEDTLS_ERR_NET_SEND_FAILED;
#else
    flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    count = send(socket_handle, data, size, flags);
    if (count < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
#endif
    return (int)count;
}

/**
 * Receives encrypted bytes from the native socket.
 * @param context Native socket pointer.
 * @param data Destination buffer.
 * @param size Buffer size.
 * @return Non-negative byte count or an Mbed TLS transport error.
 */
int kc_tlsio_internal_recv(
    void *context,
    unsigned char *data,
    size_t size
) {
    kc_tlsio_socket_t socket_handle;
#ifdef _WIN32
    int count;
#else
    ssize_t count;
#endif

    socket_handle = *(kc_tlsio_socket_t *)context;
#ifdef _WIN32
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    count = recv(socket_handle, (char *)data, (int)size, 0);
    if (count == SOCKET_ERROR) return MBEDTLS_ERR_NET_RECV_FAILED;
#else
    count = recv(socket_handle, data, size, 0);
    if (count < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
#endif
    return (int)count;
}
