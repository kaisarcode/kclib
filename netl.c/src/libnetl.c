/**
 * libnetl.c - Incoming network listener.
 * Summary: Multiplexed TCP connections and UDP datagrams.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET kc_netl_fd_t;
typedef WSAPOLLFD kc_netl_pollfd_t;
#define KC_NETL_FD_INVALID INVALID_SOCKET
#define KC_NETL_CLOSE(fd) closesocket(fd)
#define KC_NETL_POLL(fds, count, timeout) WSAPoll((fds), (ULONG)(count), (timeout))
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_netl_fd_t;
typedef struct pollfd kc_netl_pollfd_t;
#define KC_NETL_FD_INVALID (-1)
#define KC_NETL_CLOSE(fd) close(fd)
#define KC_NETL_POLL(fds, count, timeout) poll((fds), (nfds_t)(count), (timeout))
#endif

#ifdef MSG_NOSIGNAL
#define KC_NETL_SEND_FLAGS MSG_NOSIGNAL
#else
#define KC_NETL_SEND_FLAGS 0
#endif

#define KC_NETL_BUFFER_SIZE 65536U
#define KC_NETL_DEFAULT_BACKLOG 128
#define KC_NETL_HOST_SIZE 128

struct kc_netl_connection {
    kc_netl_t *listener;
    kc_netl_fd_t fd;
    int closed;
    int want_write;
    char host[KC_NETL_HOST_SIZE];
    unsigned short port;
    struct kc_netl_connection *next;
};

struct kc_netl {
    kc_netl_fd_t fd;
    int protocol;
    int platform_ready;
    int backlog;
    unsigned short port;
    kc_netl_connection_t *connections;
    kc_netl_connection_t *retired;
    kc_netl_pollfd_t *pollfds;
    kc_netl_connection_t **pollmap;
    size_t poll_capacity;
    size_t poll_offset;
    int prefer_accept;
    unsigned char buffer[KC_NETL_BUFFER_SIZE];
    char peer_host[KC_NETL_HOST_SIZE];
};

/**
 * Initialize the platform socket layer.
 * @return KC_NETL_OK on success, otherwise KC_NETL_ENET.
 */
static int kc_netl_platform_open(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0
        ? KC_NETL_OK
        : KC_NETL_ENET;
#else
    return KC_NETL_OK;
#endif
}

/**
 * Release one platform socket-layer reference.
 * @return None.
 */
static void kc_netl_platform_close(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Return whether the latest socket error means try again.
 * @return Nonzero for a would-block condition.
 */
static int kc_netl_would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

/**
 * Put one socket into non-blocking mode.
 * @param fd Socket descriptor.
 * @return KC_NETL_OK on success, otherwise KC_NETL_ENET.
 */
static int kc_netl_nonblocking(kc_netl_fd_t fd) {
#ifdef _WIN32
    u_long mode = 1UL;

    return ioctlsocket(fd, FIONBIO, &mode) == 0
        ? KC_NETL_OK
        : KC_NETL_ENET;
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) return KC_NETL_ENET;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0
        ? KC_NETL_OK
        : KC_NETL_ENET;
#endif
}

/**
 * Disable SIGPIPE for socket writes where the platform exposes the option.
 * @param fd Socket descriptor.
 * @return None.
 */
static void kc_netl_disable_sigpipe(kc_netl_fd_t fd) {
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int one = 1;

    (void)setsockopt(
        fd,
        SOL_SOCKET,
        SO_NOSIGPIPE,
        &one,
        (socklen_t)sizeof(one)
    );
#else
    (void)fd;
#endif
}

/**
 * Copy one numeric peer address into host and port outputs.
 * @param addr Socket address.
 * @param addr_len Socket address size.
 * @param host Destination host buffer.
 * @param host_cap Host buffer capacity.
 * @param port Destination port.
 * @return KC_NETL_OK on success, otherwise KC_NETL_ENET.
 */
static int kc_netl_peer_text(
    const struct sockaddr *addr,
    socklen_t addr_len,
    char *host,
    size_t host_cap,
    unsigned short *port
) {
    char service[16];
    int rc;

    rc = getnameinfo(
        addr,
        addr_len,
        host,
        (socklen_t)host_cap,
        service,
        (socklen_t)sizeof(service),
        NI_NUMERICHOST | NI_NUMERICSERV
    );
    if (rc != 0) return KC_NETL_ENET;
    *port = (unsigned short)strtoul(service, NULL, 10);
    return KC_NETL_OK;
}

/**
 * Free a connection retained for one CLOSE event lifetime.
 * @param listener Listener handle.
 * @return None.
 */
static void kc_netl_free_retired(kc_netl_t *listener) {
    kc_netl_connection_t *connection;

    while (listener->retired != NULL) {
        connection = listener->retired;
        listener->retired = connection->next;
        free(connection);
    }
}

/**
 * Unlink one active connection from its listener.
 * @param connection Connection handle.
 * @return None.
 */
static void kc_netl_unlink(kc_netl_connection_t *connection) {
    kc_netl_connection_t **cursor;

    if (connection == NULL || connection->listener == NULL) return;
    cursor = &connection->listener->connections;
    while (*cursor != NULL) {
        if (*cursor == connection) {
            *cursor = connection->next;
            connection->next = NULL;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

/**
 * Close one connection descriptor and unlink it.
 * @param connection Connection handle.
 * @return None.
 */
static void kc_netl_connection_shutdown(kc_netl_connection_t *connection) {
    if (connection == NULL || connection->closed) return;
    connection->closed = 1;
    if (connection->fd != KC_NETL_FD_INVALID) {
        KC_NETL_CLOSE(connection->fd);
        connection->fd = KC_NETL_FD_INVALID;
    }
    kc_netl_unlink(connection);
    if (connection->listener != NULL) {
        connection->next = connection->listener->retired;
        connection->listener->retired = connection;
    }
}

/**
 * Open and bind one non-blocking listener socket.
 * @param options Listener options.
 * @param out_fd Receives the socket.
 * @param out_port Receives the bound port.
 * @return KC_NETL_OK on success, otherwise a negative status.
 */
static int kc_netl_bind(
    const kc_netl_options_t *options,
    kc_netl_fd_t *out_fd,
    unsigned short *out_port
) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *item;
    kc_netl_fd_t fd = KC_NETL_FD_INVALID;
    char service[16];
    int one = 1;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = options->protocol == KC_NETL_UDP
        ? SOCK_DGRAM
        : SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(service, sizeof(service), "%u", (unsigned)options->port);
    rc = getaddrinfo(
        options->host != NULL && options->host[0] != '\0'
            ? options->host
            : NULL,
        service,
        &hints,
        &result
    );
    if (rc != 0) return KC_NETL_ENET;

    for (item = result; item != NULL; item = item->ai_next) {
        struct sockaddr_storage bound;
        socklen_t bound_len = (socklen_t)sizeof(bound);
        unsigned short port = 0U;

        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd == KC_NETL_FD_INVALID) continue;
#ifdef _WIN32
        (void)setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            (const char *)&one,
            (int)sizeof(one)
        );
#else
        (void)setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &one,
            (socklen_t)sizeof(one)
        );
#endif
        if (bind(fd, item->ai_addr, (socklen_t)item->ai_addrlen) != 0) {
            KC_NETL_CLOSE(fd);
            fd = KC_NETL_FD_INVALID;
            continue;
        }
        if (
            options->protocol == KC_NETL_TCP &&
            listen(fd, options->backlog > 0
                ? options->backlog
                : KC_NETL_DEFAULT_BACKLOG) != 0
        ) {
            KC_NETL_CLOSE(fd);
            fd = KC_NETL_FD_INVALID;
            continue;
        }
        kc_netl_disable_sigpipe(fd);
        if (kc_netl_nonblocking(fd) != KC_NETL_OK) {
            KC_NETL_CLOSE(fd);
            fd = KC_NETL_FD_INVALID;
            continue;
        }
        if (
            getsockname(
                fd,
                (struct sockaddr *)&bound,
                &bound_len
            ) != 0 ||
            kc_netl_peer_text(
                (const struct sockaddr *)&bound,
                bound_len,
                service,
                sizeof(service),
                &port
            ) != KC_NETL_OK
        ) {
            KC_NETL_CLOSE(fd);
            fd = KC_NETL_FD_INVALID;
            continue;
        }

        *out_fd = fd;
        *out_port = port;
        freeaddrinfo(result);
        return KC_NETL_OK;
    }

    freeaddrinfo(result);
    return KC_NETL_ENET;
}

/**
 * Open one incoming TCP or UDP listener.
 * @param out Receives the listener handle.
 * @param options Listener options.
 * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_open(
    kc_netl_t **out,
    const kc_netl_options_t *options
) {
    kc_netl_t *listener;
    int rc;

    if (out == NULL) return KC_NETL_EINVAL;
    *out = NULL;
    if (
        options == NULL ||
        (options->protocol != KC_NETL_TCP &&
         options->protocol != KC_NETL_UDP) ||
        options->backlog < 0
    ) {
        return KC_NETL_EINVAL;
    }

    listener = (kc_netl_t *)calloc(1, sizeof(*listener));
    if (listener == NULL) return KC_NETL_ENOMEM;
    listener->fd = KC_NETL_FD_INVALID;
    listener->protocol = options->protocol;
    listener->backlog = options->backlog;

    rc = kc_netl_platform_open();
    if (rc != KC_NETL_OK) {
        free(listener);
        return rc;
    }
    listener->platform_ready = 1;

    rc = kc_netl_bind(options, &listener->fd, &listener->port);
    if (rc != KC_NETL_OK) {
        kc_netl_close(listener);
        return rc;
    }

    *out = listener;
    return KC_NETL_OK;
}

/**
 * Count active TCP connections.
 * @param listener Listener handle.
 * @return Number of active connections.
 */
static size_t kc_netl_connection_count(const kc_netl_t *listener) {
    const kc_netl_connection_t *connection;
    size_t count = 0U;

    for (connection = listener->connections;
         connection != NULL;
         connection = connection->next) {
        count++;
    }
    return count;
}

/**
 * Ensure reusable poll storage can describe every active connection.
 * @param listener Listener handle.
 * @param count Required poll entry count.
 * @return KC_NETL_OK on success, otherwise KC_NETL_ENOMEM.
 */
static int kc_netl_poll_reserve(kc_netl_t *listener, size_t count) {
    kc_netl_pollfd_t *new_fds;
    kc_netl_connection_t **new_map;
    size_t capacity;

    if (count <= listener->poll_capacity) return KC_NETL_OK;
    capacity = listener->poll_capacity != 0U
        ? listener->poll_capacity
        : 16U;
    while (capacity < count) {
        if (capacity > ((size_t)-1) / 2U) return KC_NETL_ENOMEM;
        capacity *= 2U;
    }

    new_fds = (kc_netl_pollfd_t *)realloc(
        listener->pollfds,
        capacity * sizeof(*new_fds)
    );
    if (new_fds == NULL) return KC_NETL_ENOMEM;
    listener->pollfds = new_fds;

    new_map = (kc_netl_connection_t **)realloc(
        listener->pollmap,
        capacity * sizeof(*new_map)
    );
    if (new_map == NULL) return KC_NETL_ENOMEM;
    listener->pollmap = new_map;
    listener->poll_capacity = capacity;
    return KC_NETL_OK;
}

/**
 * Return one accepted TCP connection event.
 * @param listener Listener handle.
 * @param event Event destination.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or KC_NETL_ENET.
 */
static int kc_netl_accept(
    kc_netl_t *listener,
    kc_netl_event_t *event
) {
    struct sockaddr_storage peer;
    socklen_t peer_len = (socklen_t)sizeof(peer);
    kc_netl_connection_t *connection;
    kc_netl_fd_t fd;

    fd = accept(
        listener->fd,
        (struct sockaddr *)&peer,
        &peer_len
    );
    if (fd == KC_NETL_FD_INVALID) {
        return kc_netl_would_block() ? KC_NETL_EAGAIN : KC_NETL_ENET;
    }
    kc_netl_disable_sigpipe(fd);
    if (kc_netl_nonblocking(fd) != KC_NETL_OK) {
        KC_NETL_CLOSE(fd);
        return KC_NETL_ENET;
    }

    connection = (kc_netl_connection_t *)calloc(1, sizeof(*connection));
    if (connection == NULL) {
        KC_NETL_CLOSE(fd);
        return KC_NETL_ENOMEM;
    }

    connection->listener = listener;
    connection->fd = fd;
    connection->next = listener->connections;
    listener->connections = connection;
    if (
        kc_netl_peer_text(
            (const struct sockaddr *)&peer,
            peer_len,
            connection->host,
            sizeof(connection->host),
            &connection->port
        ) != KC_NETL_OK
    ) {
        kc_netl_unlink(connection);
        KC_NETL_CLOSE(connection->fd);
        free(connection);
        return KC_NETL_ENET;
    }

    event->type = KC_NETL_EVENT_CONNECTION;
    event->connection = connection;
    event->host = connection->host;
    event->port = connection->port;
    return KC_NETL_OK;
}

/**
 * Return one UDP datagram event.
 * @param listener Listener handle.
 * @param event Event destination.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or KC_NETL_ENET.
 */
static int kc_netl_receive_datagram(
    kc_netl_t *listener,
    kc_netl_event_t *event
) {
    struct sockaddr_storage peer;
    socklen_t peer_len = (socklen_t)sizeof(peer);
    int received;

    received = (int)recvfrom(
        listener->fd,
        (char *)listener->buffer,
        (int)sizeof(listener->buffer),
        0,
        (struct sockaddr *)&peer,
        &peer_len
    );
    if (received < 0) {
        return kc_netl_would_block() ? KC_NETL_EAGAIN : KC_NETL_ENET;
    }
    if (
        kc_netl_peer_text(
            (const struct sockaddr *)&peer,
            peer_len,
            listener->peer_host,
            sizeof(listener->peer_host),
            &event->port
        ) != KC_NETL_OK
    ) {
        return KC_NETL_ENET;
    }

    event->type = KC_NETL_EVENT_DATAGRAM;
    event->data = listener->buffer;
    event->data_size = (size_t)received;
    event->host = listener->peer_host;
    return KC_NETL_OK;
}

/**
 * Return one data or close event for a ready TCP connection.
 * @param listener Listener handle.
 * @param connection Ready connection.
 * @param event Event destination.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or KC_NETL_ENET.
 */
static int kc_netl_receive_connection(
    kc_netl_t *listener,
    kc_netl_connection_t *connection,
    kc_netl_event_t *event
) {
    int received;

    received = (int)recv(
        connection->fd,
        (char *)listener->buffer,
        (int)sizeof(listener->buffer),
        0
    );
    if (received > 0) {
        event->type = KC_NETL_EVENT_DATA;
        event->connection = connection;
        event->data = listener->buffer;
        event->data_size = (size_t)received;
        event->host = connection->host;
        event->port = connection->port;
        return KC_NETL_OK;
    }
    if (received < 0 && kc_netl_would_block()) {
        return KC_NETL_EAGAIN;
    }
    if (received < 0) {
        kc_netl_connection_shutdown(connection);
        event->type = KC_NETL_EVENT_CLOSE;
        event->connection = connection;
        event->host = connection->host;
        event->port = connection->port;
        return KC_NETL_OK;
    }

    kc_netl_connection_shutdown(connection);
    event->type = KC_NETL_EVENT_CLOSE;
    event->connection = connection;
    event->host = connection->host;
    event->port = connection->port;
    return KC_NETL_OK;
}

/**
 * Wait for one listener event while all connections remain active.
 * @param listener Listener handle.
 * @param event Event destination.
 * @param timeout_ms Timeout in milliseconds.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or a negative status.
 */
int kc_netl_poll(
    kc_netl_t *listener,
    kc_netl_event_t *event,
    int timeout_ms
) {
    kc_netl_pollfd_t *fds;
    kc_netl_connection_t **map;
    kc_netl_connection_t *connection;
    size_t count;
    size_t index;
    int ready;
    int rc = KC_NETL_EAGAIN;

    if (listener == NULL || event == NULL || timeout_ms < -1) {
        return KC_NETL_EINVAL;
    }
    memset(event, 0, sizeof(*event));
    kc_netl_free_retired(listener);

    count = listener->protocol == KC_NETL_TCP
        ? kc_netl_connection_count(listener)
        : 0U;
    rc = kc_netl_poll_reserve(listener, count + 1U);
    if (rc != KC_NETL_OK) return rc;

    fds = listener->pollfds;
    map = listener->pollmap;
    memset(fds, 0, (count + 1U) * sizeof(*fds));
    memset(map, 0, (count + 1U) * sizeof(*map));

    fds[0].fd = listener->fd;
    fds[0].events = POLLIN;
    index = 1U;
    for (connection = listener->connections;
         connection != NULL;
         connection = connection->next) {
        fds[index].fd = connection->fd;
        fds[index].events = POLLIN;
        if (connection->want_write) fds[index].events |= POLLOUT;
        map[index] = connection;
        index++;
    }

    ready = KC_NETL_POLL(
        fds,
        count + 1U,
        timeout_ms < 0 ? -1 : timeout_ms
    );
    if (ready == 0) {
        return KC_NETL_EAGAIN;
    }
    if (ready < 0) {
        return KC_NETL_ENET;
    }

    if (listener->protocol == KC_NETL_UDP) {
        if ((fds[0].revents & POLLIN) != 0) {
            return kc_netl_receive_datagram(listener, event);
        }
        if ((fds[0].revents & (POLLERR | POLLNVAL)) != 0) {
            return KC_NETL_ENET;
        }
        return KC_NETL_EAGAIN;
    }

    if ((fds[0].revents & POLLIN) != 0 && listener->prefer_accept) {
        rc = kc_netl_accept(listener, event);
        if (rc == KC_NETL_OK) listener->prefer_accept = 0;
        return rc;
    }

    if (count != 0U) {
        size_t offset = listener->poll_offset % count;
        size_t step;

        for (step = 0U; step < count; step++) {
            index = 1U + ((offset + step) % count);
            if (fds[index].revents == 0) continue;

            connection = map[index];
            if ((fds[index].revents & POLLIN) != 0) {
                rc = kc_netl_receive_connection(
                    listener,
                    connection,
                    event
                );
            } else if (
                connection->want_write &&
                (fds[index].revents & POLLOUT) != 0
            ) {
                connection->want_write = 0;
                event->type = KC_NETL_EVENT_WRITABLE;
                event->connection = connection;
                event->host = connection->host;
                event->port = connection->port;
                rc = KC_NETL_OK;
            } else if (
                (fds[index].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0
            ) {
                kc_netl_connection_shutdown(connection);
                event->type = KC_NETL_EVENT_CLOSE;
                event->connection = connection;
                event->host = connection->host;
                event->port = connection->port;
                rc = KC_NETL_OK;
            }

            if (rc == KC_NETL_OK) {
                listener->poll_offset = (offset + step + 1U) % count;
                listener->prefer_accept = 1;
                break;
            }
        }
    }

    if (rc != KC_NETL_OK && (fds[0].revents & POLLIN) != 0) {
        rc = kc_netl_accept(listener, event);
        if (rc == KC_NETL_OK) listener->prefer_accept = 0;
    }
    if (
        rc != KC_NETL_OK &&
        (fds[0].revents & (POLLERR | POLLNVAL)) != 0
    ) {
        return KC_NETL_ENET;
    }
    return rc;
}

/**
 * Attempt a non-blocking send on one TCP connection.
 * @param connection TCP connection.
 * @param data Bytes to send.
 * @param data_size Number of bytes requested.
 * @param out_sent Receives bytes sent.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, KC_NETL_ECLOSED, or an error.
 */
int kc_netl_send(
    kc_netl_connection_t *connection,
    const void *data,
    size_t data_size,
    size_t *out_sent
) {
    int sent;

    if (out_sent != NULL) *out_sent = 0U;
    if (
        connection == NULL ||
        out_sent == NULL ||
        (data == NULL && data_size != 0U)
    ) {
        return KC_NETL_EINVAL;
    }
    if (connection->closed) return KC_NETL_ECLOSED;
    if (data_size > (size_t)INT_MAX) return KC_NETL_EINVAL;
    if (data_size == 0U) return KC_NETL_OK;

    sent = (int)send(
        connection->fd,
        (const char *)data,
        (int)data_size,
        KC_NETL_SEND_FLAGS
    );
    if (sent < 0) {
        if (kc_netl_would_block()) {
            connection->want_write = 1;
            return KC_NETL_EAGAIN;
        }
        kc_netl_connection_shutdown(connection);
        return KC_NETL_ENET;
    }
    *out_sent = (size_t)sent;
    connection->want_write = (size_t)sent < data_size;
    return KC_NETL_OK;
}

/**
 * Attempt a non-blocking UDP send to one peer.
 * @param listener UDP listener.
 * @param host Destination host or address.
 * @param port Destination port.
 * @param data Datagram bytes.
 * @param data_size Datagram size.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or an error.
 */
int kc_netl_sendto(
    kc_netl_t *listener,
    const char *host,
    unsigned short port,
    const void *data,
    size_t data_size
) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *item;
    char service[16];
    int rc;
    int sent;

    if (
        listener == NULL ||
        listener->protocol != KC_NETL_UDP ||
        host == NULL ||
        host[0] == '\0' ||
        port == 0U ||
        (data == NULL && data_size != 0U)
    ) {
        return KC_NETL_EINVAL;
    }
    if (data_size > (size_t)INT_MAX) return KC_NETL_EINVAL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    rc = getaddrinfo(host, service, &hints, &result);
    if (rc != 0) return KC_NETL_ENET;

    rc = KC_NETL_ENET;
    for (item = result; item != NULL; item = item->ai_next) {
        sent = (int)sendto(
            listener->fd,
            (const char *)data,
            (int)data_size,
            0,
            item->ai_addr,
            (socklen_t)item->ai_addrlen
        );
        if (sent == (int)data_size) {
            rc = KC_NETL_OK;
            break;
        }
        if (sent < 0 && kc_netl_would_block()) {
            rc = KC_NETL_EAGAIN;
            break;
        }
    }

    freeaddrinfo(result);
    return rc;
}

/**
 * Close one TCP connection without closing its listener.
 * @param connection Connection handle.
 * @return None.
 */
void kc_netl_connection_close(kc_netl_connection_t *connection) {
    if (connection == NULL) return;
    kc_netl_connection_shutdown(connection);
}

#ifdef KC_NETL_CLI
/**
 * Transfer one TCP socket to the command-line process dispatcher.
 * This helper is compiled only into the netl executable and is not public ABI.
 * @param connection Connection handle.
 * @return Native socket value, or -1 for an invalid connection.
 */
intptr_t kc_netl_cli_take_connection(kc_netl_connection_t *connection) {
    kc_netl_fd_t fd;

    if (connection == NULL || connection->closed) return (intptr_t)-1;
    fd = connection->fd;
    connection->fd = KC_NETL_FD_INVALID;
    connection->closed = 1;
    kc_netl_unlink(connection);
    connection->next = connection->listener->retired;
    connection->listener->retired = connection;
    return (intptr_t)fd;
}
#endif

/**
 * Return the bound listener port.
 * @param listener Listener handle.
 * @return Bound port or zero.
 */
unsigned short kc_netl_port(const kc_netl_t *listener) {
    return listener != NULL ? listener->port : 0U;
}

/**
 * Close all connections and release one listener.
 * @param listener Listener handle.
 * @return None.
 */
void kc_netl_close(kc_netl_t *listener) {
    kc_netl_connection_t *connection;

    if (listener == NULL) return;
    while (listener->connections != NULL) {
        connection = listener->connections;
        listener->connections = connection->next;
        connection->next = NULL;
        if (connection->fd != KC_NETL_FD_INVALID) {
            KC_NETL_CLOSE(connection->fd);
        }
        free(connection);
    }
    kc_netl_free_retired(listener);
    free(listener->pollfds);
    free(listener->pollmap);
    if (listener->fd != KC_NETL_FD_INVALID) {
        KC_NETL_CLOSE(listener->fd);
    }
    if (listener->platform_ready) kc_netl_platform_close();
    free(listener);
}

/**
 * Return a static message for one public status code.
 * @param status Status code.
 * @return Static error string.
 */
const char *kc_netl_strerror(int status) {
    switch (status) {
        case KC_NETL_OK: return "ok";
        case KC_NETL_EINVAL: return "invalid argument";
        case KC_NETL_ENET: return "network error";
        case KC_NETL_EAGAIN: return "try again";
        case KC_NETL_ECLOSED: return "connection closed";
        case KC_NETL_ENOMEM: return "out of memory";
        default: return "unknown error";
    }
}

#ifndef KC_NETL_BUILD_VERSION
#define KC_NETL_BUILD_VERSION 0
#endif

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_netl_version(void) {
    return (uint64_t)KC_NETL_BUILD_VERSION;
}
