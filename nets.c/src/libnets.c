/**
 * libnets.c - Asynchronous network transfer.
 * Summary: Sends one byte buffer over TCP, UDP, or optional TLS.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libnets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef KC_NETS_OPENSSL
#include <openssl/ssl.h>
#endif

#ifdef _WIN32
typedef SOCKET kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET INVALID_SOCKET
#else
typedef int kc_nets_socket_t;
#define KC_NETS_BAD_SOCKET (-1)
#endif

struct kc_nets {
    char *host;
    unsigned short port;
    int protocol;
    unsigned char *data;
    size_t data_size;
    kc_nets_handler_t handler;
    void *userdata;
    int stopped;
    kc_nets_socket_t socket;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    HANDLE thread;
    DWORD thread_id;
#else
    pthread_mutex_t lock;
    pthread_t thread;
#endif
};

#ifdef _WIN32
static void kc_nets_lock(kc_nets_t *nets) { EnterCriticalSection(&nets->lock); }
static void kc_nets_unlock(kc_nets_t *nets) { LeaveCriticalSection(&nets->lock); }
#else
static void kc_nets_lock(kc_nets_t *nets) { pthread_mutex_lock(&nets->lock); }
static void kc_nets_unlock(kc_nets_t *nets) { pthread_mutex_unlock(&nets->lock); }
#endif

/**
 * Duplicate one string.
 * @param text Source string.
 * @return Owned copy, or NULL.
 */
static char *kc_nets_dup(const char *text) {
    size_t size;
    char *copy;

    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

/**
 * Return whether stop has been requested.
 * @param nets Transfer.
 * @return Non-zero when stopped.
 */
static int kc_nets_is_stopped(kc_nets_t *nets) {
    int stopped;

    kc_nets_lock(nets);
    stopped = nets->stopped;
    kc_nets_unlock(nets);
    return stopped;
}

/**
 * Publish the currently active socket for interruption.
 * @param nets Transfer.
 * @param socket Active socket.
 * @return None.
 */
static void kc_nets_set_socket(kc_nets_t *nets, kc_nets_socket_t socket) {
    kc_nets_lock(nets);
    nets->socket = socket;
    kc_nets_unlock(nets);
}

/**
 * Clear the active socket before closing it.
 * @param nets Transfer.
 * @param socket Socket being closed.
 * @return None.
 */
static void kc_nets_clear_socket(kc_nets_t *nets, kc_nets_socket_t socket) {
    kc_nets_lock(nets);
    if (nets->socket == socket) nets->socket = KC_NETS_BAD_SOCKET;
    kc_nets_unlock(nets);
}

/**
 * Close one socket.
 * @param socket Socket handle.
 * @return None.
 */
static void kc_nets_socket_close(kc_nets_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Shut down one socket to wake blocking network operations.
 * @param socket Socket handle.
 * @return None.
 */
static void kc_nets_socket_shutdown(kc_nets_socket_t socket) {
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

/**
 * Initialize process socket support.
 * @return 0 on success, -1 on failure.
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
 * Release process socket support.
 * @return None.
 */
static void kc_nets_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Append bytes to a response buffer.
 * @param buffer Owned response buffer pointer.
 * @param used Current used size.
 * @param capacity Current capacity.
 * @param data Bytes to append.
 * @param size Byte count.
 * @return KC_NETS_OK on success, KC_NETS_ENET on allocation failure.
 */
static int kc_nets_append(
    unsigned char **buffer,
    size_t *used,
    size_t *capacity,
    const void *data,
    size_t size
) {
    unsigned char *next;
    size_t cap;

    if (size == 0U) return KC_NETS_OK;
    if (*used + size < *used) return KC_NETS_ENET;
    if (*used + size > *capacity) {
        cap = *capacity != 0U ? *capacity : 65536U;
        while (cap < *used + size) {
            if (cap > ((size_t)-1) / 2U) {
                cap = *used + size;
                break;
            }
            cap *= 2U;
        }
        next = (unsigned char *)realloc(*buffer, cap);
        if (next == NULL) return KC_NETS_ENET;
        *buffer = next;
        *capacity = cap;
    }
    memcpy(*buffer + *used, data, size);
    *used += size;
    return KC_NETS_OK;
}

/**
 * Send all stream bytes.
 * @param nets Transfer.
 * @param socket Connected socket.
 * @return Public status.
 */
static int kc_nets_send_all(kc_nets_t *nets, kc_nets_socket_t socket) {
    size_t sent;

    sent = 0U;
    while (sent < nets->data_size) {
#ifdef _WIN32
        int count;
#else
        ssize_t count;
#endif
        if (kc_nets_is_stopped(nets)) return KC_NETS_ESTOP;
#ifdef _WIN32
        count = send(
            socket,
            (const char *)nets->data + sent,
            (int)(nets->data_size - sent),
            0
        );
#else
        count = send(
            socket,
            (const char *)nets->data + sent,
            nets->data_size - sent,
            0
        );
#endif
        if (count <= 0) {
            return kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
        }
        sent += (size_t)count;
    }
    return KC_NETS_OK;
}

/**
 * Receive a plain TCP response until peer EOF.
 * @param nets Transfer.
 * @param socket Connected socket.
 * @param out_data Receives owned response bytes.
 * @param out_size Receives response size.
 * @return Public status.
 */
static int kc_nets_recv_plain(
    kc_nets_t *nets,
    kc_nets_socket_t socket,
    unsigned char **out_data,
    size_t *out_size
) {
    unsigned char *response;
    size_t used;
    size_t capacity;
    int rc;

    response = NULL;
    used = 0U;
    capacity = 0U;
    rc = KC_NETS_OK;

    for (;;) {
        unsigned char buffer[65536];
#ifdef _WIN32
        int count;
#else
        ssize_t count;
#endif
        if (kc_nets_is_stopped(nets)) {
            rc = KC_NETS_ESTOP;
            break;
        }
#ifdef _WIN32
        count = recv(socket, (char *)buffer, (int)sizeof(buffer), 0);
#else
        count = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (count == 0) break;
        if (count < 0) {
            rc = kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
            break;
        }
        rc = kc_nets_append(
            &response,
            &used,
            &capacity,
            buffer,
            (size_t)count
        );
        if (rc != KC_NETS_OK) break;
    }

    if (rc != KC_NETS_OK) {
        free(response);
        return rc;
    }
    *out_data = response;
    *out_size = used;
    return KC_NETS_OK;
}

#ifdef KC_NETS_OPENSSL
/**
 * Run one TLS stream transfer.
 * @param nets Transfer.
 * @param socket Connected socket.
 * @param out_data Receives owned response bytes.
 * @param out_size Receives response size.
 * @return Public status.
 */
static int kc_nets_tls_transfer(
    kc_nets_t *nets,
    kc_nets_socket_t socket,
    unsigned char **out_data,
    size_t *out_size
) {
    SSL_CTX *context;
    SSL *ssl;
    unsigned char *response;
    size_t used;
    size_t capacity;
    size_t sent;
    int rc;

    context = SSL_CTX_new(TLS_client_method());
    if (context == NULL) return KC_NETS_ENET;
    ssl = SSL_new(context);
    if (ssl == NULL) {
        SSL_CTX_free(context);
        return KC_NETS_ENET;
    }

    SSL_set_fd(ssl, (int)socket);
    SSL_set_tlsext_host_name(ssl, nets->host);
    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        SSL_CTX_free(context);
        return kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
    }

    sent = 0U;
    while (sent < nets->data_size) {
        int count;

        if (kc_nets_is_stopped(nets)) {
            SSL_free(ssl);
            SSL_CTX_free(context);
            return KC_NETS_ESTOP;
        }
        count = SSL_write(
            ssl,
            nets->data + sent,
            (int)(nets->data_size - sent)
        );
        if (count <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(context);
            return kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
        }
        sent += (size_t)count;
    }

    response = NULL;
    used = 0U;
    capacity = 0U;
    rc = KC_NETS_OK;
    SSL_shutdown(ssl);

    for (;;) {
        unsigned char buffer[65536];
        int count;

        if (kc_nets_is_stopped(nets)) {
            rc = KC_NETS_ESTOP;
            break;
        }
        count = SSL_read(ssl, buffer, (int)sizeof(buffer));
        if (count <= 0) break;
        rc = kc_nets_append(
            &response,
            &used,
            &capacity,
            buffer,
            (size_t)count
        );
        if (rc != KC_NETS_OK) break;
    }

    SSL_free(ssl);
    SSL_CTX_free(context);
    if (rc != KC_NETS_OK) {
        free(response);
        return rc;
    }
    *out_data = response;
    *out_size = used;
    return KC_NETS_OK;
}
#endif

/**
 * Try one resolved destination address.
 * @param nets Transfer.
 * @param address Resolved address.
 * @param out_data Receives owned response bytes.
 * @param out_size Receives response size.
 * @return Public status.
 */
static int kc_nets_transfer_address(
    kc_nets_t *nets,
    const struct addrinfo *address,
    unsigned char **out_data,
    size_t *out_size
) {
    kc_nets_socket_t sock;
    int type;
    int rc;

    if (kc_nets_is_stopped(nets)) return KC_NETS_ESTOP;

    type = nets->protocol == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;
    sock = socket(address->ai_family, type, address->ai_protocol);
    if (sock == KC_NETS_BAD_SOCKET) return KC_NETS_ENET;
    kc_nets_set_socket(nets, sock);

    if (kc_nets_is_stopped(nets)) {
        rc = KC_NETS_ESTOP;
    } else if (nets->protocol == KC_NETS_UDP) {
#ifdef _WIN32
        int count = sendto(
            sock,
            (const char *)nets->data,
            (int)nets->data_size,
            0,
            address->ai_addr,
            (int)address->ai_addrlen
        );
#else
        ssize_t count = sendto(
            sock,
            nets->data,
            nets->data_size,
            0,
            address->ai_addr,
            address->ai_addrlen
        );
#endif
        if (count < 0 || (size_t)count != nets->data_size) {
            rc = kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
        } else {
            rc = KC_NETS_OK;
        }
    } else if (connect(sock, address->ai_addr, address->ai_addrlen) != 0) {
        rc = kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
    } else {
#ifdef KC_NETS_OPENSSL
        if (nets->protocol == KC_NETS_TLS) {
            rc = kc_nets_tls_transfer(nets, sock, out_data, out_size);
        } else
#endif
        {
            rc = kc_nets_send_all(nets, sock);
            if (rc == KC_NETS_OK) {
#ifdef _WIN32
                shutdown(sock, SD_SEND);
#else
                shutdown(sock, SHUT_WR);
#endif
                rc = kc_nets_recv_plain(nets, sock, out_data, out_size);
            }
        }
    }

    kc_nets_clear_socket(nets, sock);
    kc_nets_socket_close(sock);
    return rc;
}

/**
 * Execute one transfer on the worker thread.
 * @param nets Transfer.
 * @param out_data Receives owned callback response bytes.
 * @param out_size Receives response size.
 * @return Public terminal status.
 */
static int kc_nets_run(
    kc_nets_t *nets,
    unsigned char **out_data,
    size_t *out_size
) {
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    char port[16];
    int rc;

    *out_data = NULL;
    *out_size = 0U;

#ifndef KC_NETS_OPENSSL
    if (nets->protocol == KC_NETS_TLS) return KC_NETS_ENET;
#endif
    if (kc_nets_platform_init() != 0) return KC_NETS_ENET;
    if (kc_nets_is_stopped(nets)) {
        kc_nets_platform_cleanup();
        return KC_NETS_ESTOP;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype =
        nets->protocol == KC_NETS_UDP ? SOCK_DGRAM : SOCK_STREAM;
    snprintf(port, sizeof(port), "%u", (unsigned int)nets->port);

    addresses = NULL;
    if (getaddrinfo(nets->host, port, &hints, &addresses) != 0) {
        kc_nets_platform_cleanup();
        return kc_nets_is_stopped(nets) ? KC_NETS_ESTOP : KC_NETS_ENET;
    }

    rc = KC_NETS_ENET;
    for (address = addresses; address != NULL; address = address->ai_next) {
        rc = kc_nets_transfer_address(nets, address, out_data, out_size);
        if (rc == KC_NETS_OK || rc == KC_NETS_ESTOP) break;
    }

    freeaddrinfo(addresses);
    kc_nets_platform_cleanup();
    return rc;
}

/**
 * Destroy transfer memory after its thread is no longer externally joinable.
 * @param nets Transfer.
 * @return None.
 */
static void kc_nets_destroy(kc_nets_t *nets) {
#ifdef _WIN32
    DeleteCriticalSection(&nets->lock);
#else
    pthread_mutex_destroy(&nets->lock);
#endif
    free(nets->data);
    free(nets->host);
    free(nets);
}

#ifdef _WIN32
static DWORD WINAPI kc_nets_worker(void *userdata)
#else
static void *kc_nets_worker(void *userdata)
#endif
{
    kc_nets_t *nets;
    unsigned char *response;
    size_t response_size;
    int status;
    nets = (kc_nets_t *)userdata;
    response = NULL;
    response_size = 0U;
    status = kc_nets_run(nets, &response, &response_size);

    nets->handler(status, response, response_size, nets->userdata);
    free(response);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Start one asynchronous network transfer.
 */
int kc_nets_send(
    kc_nets_t **out,
    const char *host,
    unsigned short port,
    int protocol,
    const void *data,
    size_t data_size,
    kc_nets_handler_t handler,
    void *userdata
) {
    kc_nets_t *nets;

    if (out == NULL) return KC_NETS_EINVAL;
    *out = NULL;
    if (host == NULL || host[0] == '\0' || data == NULL || handler == NULL) {
        return KC_NETS_EINVAL;
    }
    if (protocol != KC_NETS_TCP &&
        protocol != KC_NETS_UDP &&
        protocol != KC_NETS_TLS) {
        return KC_NETS_EINVAL;
    }
#ifndef KC_NETS_OPENSSL
    if (protocol == KC_NETS_TLS) return KC_NETS_ENET;
#endif

    nets = (kc_nets_t *)calloc(1, sizeof(*nets));
    if (nets == NULL) return KC_NETS_ENET;
    nets->host = kc_nets_dup(host);
    if (data_size != 0U) {
        nets->data = (unsigned char *)malloc(data_size);
    } else {
        nets->data = (unsigned char *)malloc(1U);
    }
    if (nets->host == NULL || nets->data == NULL) {
        free(nets->data);
        free(nets->host);
        free(nets);
        return KC_NETS_ENET;
    }
    if (data_size != 0U) memcpy(nets->data, data, data_size);

    nets->port = port;
    nets->protocol = protocol;
    nets->data_size = data_size;
    nets->handler = handler;
    nets->userdata = userdata;
    nets->socket = KC_NETS_BAD_SOCKET;

#ifdef _WIN32
    InitializeCriticalSection(&nets->lock);
    nets->thread = CreateThread(
        NULL,
        0,
        kc_nets_worker,
        nets,
        0,
        &nets->thread_id
    );
    if (nets->thread == NULL) {
        kc_nets_destroy(nets);
        return KC_NETS_ENET;
    }
#else
    if (pthread_mutex_init(&nets->lock, NULL) != 0) {
        free(nets->data);
        free(nets->host);
        free(nets);
        return KC_NETS_ENET;
    }
    if (pthread_create(&nets->thread, NULL, kc_nets_worker, nets) != 0) {
        kc_nets_destroy(nets);
        return KC_NETS_ENET;
    }
#endif

    *out = nets;
    return KC_NETS_OK;
}

/**
 * Request graceful interruption of one transfer.
 */
int kc_nets_stop(kc_nets_t *nets) {
    kc_nets_socket_t socket;

    if (nets == NULL) return KC_NETS_EINVAL;

    kc_nets_lock(nets);
    nets->stopped = 1;
    socket = nets->socket;
    if (socket != KC_NETS_BAD_SOCKET) kc_nets_socket_shutdown(socket);
    kc_nets_unlock(nets);
    return KC_NETS_OK;
}

/**
 * Stop if necessary and release one transfer.
 */
void kc_nets_close(kc_nets_t *nets) {
    if (nets == NULL) return;

    kc_nets_stop(nets);
#ifdef _WIN32
    WaitForSingleObject(nets->thread, INFINITE);
    CloseHandle(nets->thread);
#else
    pthread_join(nets->thread, NULL);
#endif
    kc_nets_destroy(nets);
}

/**
 * Return a static message for a public status code.
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

/**
 * Check whether TLS support is compiled in.
 */
int kc_nets_tls_available(void) {
#ifdef KC_NETS_OPENSSL
    return 1;
#else
    return 0;
#endif
}

#ifndef KC_NETS_BUILD_VERSION
#define KC_NETS_BUILD_VERSION 0
#endif

/**
 * Return the build version generated at compile time.
 */
uint64_t kc_nets_version(void) {
    return (uint64_t)KC_NETS_BUILD_VERSION;
}
