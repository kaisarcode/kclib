/**
 * libnetl.c - Incoming network listener.
 * Summary: Callback-driven TCP/UDP listener with transport mechanics kept private.
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
#include <windows.h>
typedef SOCKET kc_netl_fd_t;
typedef WSAPOLLFD kc_netl_pollfd_t;
typedef CRITICAL_SECTION kc_netl_mutex_t;
typedef HANDLE kc_netl_thread_t;
#define KC_NETL_FD_INVALID INVALID_SOCKET
#define KC_NETL_CLOSE(fd) closesocket(fd)
#define KC_NETL_POLL(fds, count, timeout) WSAPoll((fds), (ULONG)(count), (timeout))
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_netl_fd_t;
typedef struct pollfd kc_netl_pollfd_t;
typedef pthread_mutex_t kc_netl_mutex_t;
typedef pthread_t kc_netl_thread_t;
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
#define KC_NETL_DEFAULT_MAX_PENDING_CONNECTIONS 128
#define KC_NETL_HOST_SIZE 128
#define KC_NETL_POLL_TIMEOUT_MS 25

typedef struct kc_netl_output {
    unsigned char *data;
    size_t size;
    size_t offset;
    struct sockaddr_storage address;
    socklen_t address_size;
    struct kc_netl_output *next;
} kc_netl_output_t;

struct kc_netl_peer {
    kc_netl_t *listener;
    int protocol;
    kc_netl_fd_t fd;
    int closed;
    int closing;
    int taken;
    char host[KC_NETL_HOST_SIZE];
    unsigned short port;
    struct sockaddr_storage address;
    socklen_t address_size;
    kc_netl_output_t *out_head;
    kc_netl_output_t *out_tail;
    struct kc_netl_peer *next;
};

struct kc_netl {
    kc_netl_fd_t fd;
    int protocol;
    int platform_ready;
    int max_pending_connections;
    unsigned short port;

    kc_netl_handler_t handler;
    kc_netl_close_handler_t close_handler;
    kc_netl_error_handler_t error_handler;
    void *userdata;

    kc_netl_peer_t *peers;
    kc_netl_output_t *udp_out_head;
    kc_netl_output_t *udp_out_tail;

    kc_netl_pollfd_t *pollfds;
    kc_netl_peer_t **pollmap;
    size_t poll_capacity;

    kc_netl_mutex_t mutex;
    int mutex_ready;
    kc_netl_thread_t worker;
    int worker_started;
    int stop;
    int failed;

#ifdef KC_NETL_CLI
    void (*cli_accept_handler)(kc_netl_peer_t *peer, void *userdata);
    void *cli_accept_userdata;
#endif
};

static int kc_netl_platform_open(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? KC_NETL_OK : KC_NETL_ENET;
#else
    return KC_NETL_OK;
#endif
}

static void kc_netl_platform_close(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static int kc_netl_would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int kc_netl_nonblocking(kc_netl_fd_t fd) {
#ifdef _WIN32
    u_long mode = 1UL;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? KC_NETL_OK : KC_NETL_ENET;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return KC_NETL_ENET;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? KC_NETL_OK : KC_NETL_ENET;
#endif
}

static void kc_netl_disable_sigpipe(kc_netl_fd_t fd) {
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, (socklen_t)sizeof(one));
#else
    (void)fd;
#endif
}

static int kc_netl_mutex_open(kc_netl_mutex_t *mutex) {
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

static void kc_netl_mutex_close(kc_netl_mutex_t *mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    (void)pthread_mutex_destroy(mutex);
#endif
}

static void kc_netl_lock(kc_netl_t *listener) {
#ifdef _WIN32
    EnterCriticalSection(&listener->mutex);
#else
    (void)pthread_mutex_lock(&listener->mutex);
#endif
}

static void kc_netl_unlock(kc_netl_t *listener) {
#ifdef _WIN32
    LeaveCriticalSection(&listener->mutex);
#else
    (void)pthread_mutex_unlock(&listener->mutex);
#endif
}

static int kc_netl_peer_text(
    const struct sockaddr *addr,
    socklen_t addr_len,
    char *host,
    size_t host_cap,
    unsigned short *port
) {
    char service[16];
    int rc = getnameinfo(
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

static void kc_netl_output_free(kc_netl_output_t *output) {
    while (output != NULL) {
        kc_netl_output_t *next = output->next;
        free(output->data);
        free(output);
        output = next;
    }
}

static kc_netl_output_t *kc_netl_output_new(
    const void *data,
    size_t size
) {
    kc_netl_output_t *output = (kc_netl_output_t *)calloc(1, sizeof(*output));

    if (output == NULL) return NULL;
    if (size != 0U) {
        output->data = (unsigned char *)malloc(size);
        if (output->data == NULL) {
            free(output);
            return NULL;
        }
        memcpy(output->data, data, size);
    }
    output->size = size;
    return output;
}

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
    hints.ai_socktype = options->protocol == KC_NETL_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    snprintf(service, sizeof(service), "%u", (unsigned)options->port);
    rc = getaddrinfo(
        options->host != NULL && options->host[0] != '\0' ? options->host : NULL,
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
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (int)sizeof(one));
#else
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));
#endif
        if (bind(fd, item->ai_addr, (socklen_t)item->ai_addrlen) != 0) {
            KC_NETL_CLOSE(fd);
            fd = KC_NETL_FD_INVALID;
            continue;
        }
        if (
            options->protocol == KC_NETL_TCP &&
            listen(
                fd,
                options->max_pending_connections != NULL
                    ? *options->max_pending_connections
                    : KC_NETL_DEFAULT_MAX_PENDING_CONNECTIONS
            ) != 0
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
            getsockname(fd, (struct sockaddr *)&bound, &bound_len) != 0 ||
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

static size_t kc_netl_peer_count(const kc_netl_t *listener) {
    const kc_netl_peer_t *peer;
    size_t count = 0U;

    for (peer = listener->peers; peer != NULL; peer = peer->next) {
        if (!peer->closed && !peer->taken) count++;
    }
    return count;
}

static int kc_netl_poll_reserve(kc_netl_t *listener, size_t count) {
    kc_netl_pollfd_t *new_fds;
    kc_netl_peer_t **new_map;
    size_t capacity;

    if (count <= listener->poll_capacity) return KC_NETL_OK;
    capacity = listener->poll_capacity != 0U ? listener->poll_capacity : 16U;
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

    new_map = (kc_netl_peer_t **)realloc(
        listener->pollmap,
        capacity * sizeof(*new_map)
    );
    if (new_map == NULL) return KC_NETL_ENOMEM;
    listener->pollmap = new_map;
    listener->poll_capacity = capacity;
    return KC_NETL_OK;
}

static void kc_netl_peer_detach(
    kc_netl_t *listener,
    kc_netl_peer_t *peer
) {
    kc_netl_peer_t **cursor = &listener->peers;

    while (*cursor != NULL) {
        if (*cursor == peer) {
            *cursor = peer->next;
            peer->next = NULL;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void kc_netl_peer_finish(
    kc_netl_t *listener,
    kc_netl_peer_t *peer,
    int notify
) {
    kc_netl_close_handler_t handler = NULL;
    void *userdata = NULL;

    kc_netl_lock(listener);
    if (!peer->closed) {
        peer->closed = 1;
        if (peer->fd != KC_NETL_FD_INVALID) {
            KC_NETL_CLOSE(peer->fd);
            peer->fd = KC_NETL_FD_INVALID;
        }
        kc_netl_peer_detach(listener, peer);
        handler = notify ? listener->close_handler : NULL;
        userdata = listener->userdata;
    }
    kc_netl_unlock(listener);

    if (handler != NULL) handler(peer, userdata);
    kc_netl_output_free(peer->out_head);
    free(peer);
}

static void kc_netl_fail(kc_netl_t *listener, int status) {
    kc_netl_error_handler_t handler = NULL;
    void *userdata = NULL;

    kc_netl_lock(listener);
    if (!listener->failed) {
        listener->failed = 1;
        listener->stop = 1;
        handler = listener->error_handler;
        userdata = listener->userdata;
    }
    kc_netl_unlock(listener);

    if (handler != NULL) handler(status, userdata);
}

static int kc_netl_accept_peer(kc_netl_t *listener) {
    struct sockaddr_storage address;
    socklen_t address_size = (socklen_t)sizeof(address);
    kc_netl_peer_t *peer;
    kc_netl_fd_t fd;

    fd = accept(listener->fd, (struct sockaddr *)&address, &address_size);
    if (fd == KC_NETL_FD_INVALID) {
        return kc_netl_would_block() ? KC_NETL_OK : KC_NETL_ENET;
    }

    kc_netl_disable_sigpipe(fd);
    if (kc_netl_nonblocking(fd) != KC_NETL_OK) {
        KC_NETL_CLOSE(fd);
        return KC_NETL_ENET;
    }

    peer = (kc_netl_peer_t *)calloc(1, sizeof(*peer));
    if (peer == NULL) {
        KC_NETL_CLOSE(fd);
        return KC_NETL_ENOMEM;
    }

    peer->listener = listener;
    peer->protocol = KC_NETL_TCP;
    peer->fd = fd;
    peer->address = address;
    peer->address_size = address_size;
    if (
        kc_netl_peer_text(
            (const struct sockaddr *)&address,
            address_size,
            peer->host,
            sizeof(peer->host),
            &peer->port
        ) != KC_NETL_OK
    ) {
        KC_NETL_CLOSE(fd);
        free(peer);
        return KC_NETL_ENET;
    }

    kc_netl_lock(listener);
    peer->next = listener->peers;
    listener->peers = peer;
    kc_netl_unlock(listener);

#ifdef KC_NETL_CLI
    if (listener->cli_accept_handler != NULL) {
        listener->cli_accept_handler(peer, listener->cli_accept_userdata);
    }
#endif

    return KC_NETL_OK;
}

static void kc_netl_receive_tcp(
    kc_netl_t *listener,
    kc_netl_peer_t *peer
) {
    unsigned char buffer[KC_NETL_BUFFER_SIZE];
    int received;

    received = (int)recv(
        peer->fd,
        (char *)buffer,
        (int)sizeof(buffer),
        0
    );
    if (received > 0) {
        kc_netl_input_t input;

        memset(&input, 0, sizeof(input));
        input.peer = peer;
        input.protocol = KC_NETL_TCP;
        input.host = peer->host;
        input.port = peer->port;
        input.data = buffer;
        input.data_size = (size_t)received;
        listener->handler(&input, listener->userdata);
        return;
    }

    if (received < 0 && kc_netl_would_block()) return;
    kc_netl_peer_finish(listener, peer, 1);
}

static void kc_netl_receive_udp(kc_netl_t *listener) {
    unsigned char buffer[KC_NETL_BUFFER_SIZE];
    struct sockaddr_storage address;
    socklen_t address_size = (socklen_t)sizeof(address);
    kc_netl_peer_t peer;
    kc_netl_input_t input;
    int received;

    received = (int)recvfrom(
        listener->fd,
        (char *)buffer,
        (int)sizeof(buffer),
        0,
        (struct sockaddr *)&address,
        &address_size
    );
    if (received < 0) {
        if (!kc_netl_would_block()) kc_netl_fail(listener, KC_NETL_ENET);
        return;
    }

    memset(&peer, 0, sizeof(peer));
    peer.listener = listener;
    peer.protocol = KC_NETL_UDP;
    peer.fd = listener->fd;
    peer.address = address;
    peer.address_size = address_size;
    if (
        kc_netl_peer_text(
            (const struct sockaddr *)&address,
            address_size,
            peer.host,
            sizeof(peer.host),
            &peer.port
        ) != KC_NETL_OK
    ) {
        kc_netl_fail(listener, KC_NETL_ENET);
        return;
    }

    memset(&input, 0, sizeof(input));
    input.peer = &peer;
    input.protocol = KC_NETL_UDP;
    input.host = peer.host;
    input.port = peer.port;
    input.data = buffer;
    input.data_size = (size_t)received;
    listener->handler(&input, listener->userdata);
    peer.closed = 1;
}

static void kc_netl_flush_tcp(
    kc_netl_t *listener,
    kc_netl_peer_t *peer
) {
    int failed = 0;

    kc_netl_lock(listener);
    while (peer->out_head != NULL && !peer->closed && !peer->closing) {
        kc_netl_output_t *output = peer->out_head;
        size_t remain = output->size - output->offset;
        int amount = remain > (size_t)INT_MAX ? INT_MAX : (int)remain;
        int sent;

        if (amount == 0) {
            peer->out_head = output->next;
            if (peer->out_head == NULL) peer->out_tail = NULL;
            free(output->data);
            free(output);
            continue;
        }

        sent = (int)send(
            peer->fd,
            (const char *)output->data + output->offset,
            amount,
            KC_NETL_SEND_FLAGS
        );
        if (sent > 0) {
            output->offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && kc_netl_would_block()) break;
        failed = 1;
        peer->closing = 1;
        break;
    }
    kc_netl_unlock(listener);

    if (failed) kc_netl_peer_finish(listener, peer, 1);
}

static void kc_netl_flush_udp(kc_netl_t *listener) {
    for (;;) {
        kc_netl_output_t *output;
        int sent;

        kc_netl_lock(listener);
        output = listener->udp_out_head;
        if (output == NULL) {
            kc_netl_unlock(listener);
            return;
        }

        sent = (int)sendto(
            listener->fd,
            (const char *)output->data,
            (int)output->size,
            0,
            (const struct sockaddr *)&output->address,
            output->address_size
        );

        if (sent == (int)output->size) {
            listener->udp_out_head = output->next;
            if (listener->udp_out_head == NULL) listener->udp_out_tail = NULL;
            kc_netl_unlock(listener);
            free(output->data);
            free(output);
            continue;
        }

        if (sent < 0 && kc_netl_would_block()) {
            kc_netl_unlock(listener);
            return;
        }

        listener->udp_out_head = output->next;
        if (listener->udp_out_head == NULL) listener->udp_out_tail = NULL;
        kc_netl_unlock(listener);
        free(output->data);
        free(output);
        kc_netl_fail(listener, KC_NETL_ENET);
        return;
    }
}

static int kc_netl_should_stop(kc_netl_t *listener) {
    int stop;

    kc_netl_lock(listener);
    stop = listener->stop;
    kc_netl_unlock(listener);
    return stop;
}

static void kc_netl_close_requested_peers(kc_netl_t *listener) {
    for (;;) {
        kc_netl_peer_t *peer;
        kc_netl_peer_t *target = NULL;

        kc_netl_lock(listener);
        for (peer = listener->peers; peer != NULL; peer = peer->next) {
            if (
                peer->closing &&
                !peer->closed &&
                peer->out_head == NULL
            ) {
                target = peer;
                break;
            }
        }
        kc_netl_unlock(listener);

        if (target == NULL) return;
        kc_netl_peer_finish(listener, target, 1);
    }
}

static void kc_netl_worker_run(kc_netl_t *listener) {
    while (!kc_netl_should_stop(listener)) {
        size_t count;
        size_t index;
        kc_netl_peer_t *peer;
        int ready;
        int rc;

        kc_netl_close_requested_peers(listener);
        if (kc_netl_should_stop(listener)) break;

        kc_netl_lock(listener);
        count = listener->protocol == KC_NETL_TCP
            ? kc_netl_peer_count(listener)
            : 0U;
        kc_netl_unlock(listener);

        rc = kc_netl_poll_reserve(listener, count + 1U);
        if (rc != KC_NETL_OK) {
            kc_netl_fail(listener, rc);
            break;
        }

        memset(listener->pollfds, 0, (count + 1U) * sizeof(*listener->pollfds));
        memset(listener->pollmap, 0, (count + 1U) * sizeof(*listener->pollmap));
        listener->pollfds[0].fd = listener->fd;
        listener->pollfds[0].events = POLLIN;

        kc_netl_lock(listener);
        if (listener->protocol == KC_NETL_UDP && listener->udp_out_head != NULL) {
            listener->pollfds[0].events |= POLLOUT;
        }

        index = 1U;
        for (peer = listener->peers; peer != NULL && index <= count; peer = peer->next) {
            if (peer->closed || peer->taken) continue;
            listener->pollfds[index].fd = peer->fd;
            listener->pollfds[index].events = peer->closing ? 0 : POLLIN;
            if (peer->out_head != NULL) listener->pollfds[index].events |= POLLOUT;
            listener->pollmap[index] = peer;
            index++;
        }
        kc_netl_unlock(listener);

        ready = KC_NETL_POLL(
            listener->pollfds,
            count + 1U,
            KC_NETL_POLL_TIMEOUT_MS
        );
        if (ready < 0) {
            kc_netl_fail(listener, KC_NETL_ENET);
            break;
        }
        if (ready == 0) continue;

        if (listener->protocol == KC_NETL_UDP) {
            if ((listener->pollfds[0].revents & POLLIN) != 0) {
                kc_netl_receive_udp(listener);
            }
            if ((listener->pollfds[0].revents & POLLOUT) != 0) {
                kc_netl_flush_udp(listener);
            }
            if (
                (listener->pollfds[0].revents & (POLLERR | POLLNVAL)) != 0
            ) {
                kc_netl_fail(listener, KC_NETL_ENET);
            }
            continue;
        }

        if ((listener->pollfds[0].revents & POLLIN) != 0) {
            rc = kc_netl_accept_peer(listener);
            if (rc != KC_NETL_OK) {
                kc_netl_fail(listener, rc);
                break;
            }
        }
        if ((listener->pollfds[0].revents & (POLLERR | POLLNVAL)) != 0) {
            kc_netl_fail(listener, KC_NETL_ENET);
            break;
        }

        for (index = 1U; index <= count; index++) {
            short events = listener->pollfds[index].revents;
            peer = listener->pollmap[index];
            if (peer == NULL || events == 0) continue;

            if ((events & POLLIN) != 0) {
                kc_netl_receive_tcp(listener, peer);
                continue;
            }
            if ((events & POLLOUT) != 0) {
                kc_netl_flush_tcp(listener, peer);
                continue;
            }
            if ((events & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                kc_netl_peer_finish(listener, peer, 1);
            }
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI kc_netl_worker_entry(LPVOID arg) {
    kc_netl_worker_run((kc_netl_t *)arg);
    return 0;
}
#else
static void *kc_netl_worker_entry(void *arg) {
    kc_netl_worker_run((kc_netl_t *)arg);
    return NULL;
}
#endif

static int kc_netl_worker_start(kc_netl_t *listener) {
#ifdef _WIN32
    listener->worker = CreateThread(
        NULL,
        0,
        kc_netl_worker_entry,
        listener,
        0,
        NULL
    );
    if (listener->worker == NULL) return KC_NETL_ENET;
#else
    if (pthread_create(&listener->worker, NULL, kc_netl_worker_entry, listener) != 0) {
        return KC_NETL_ENET;
    }
#endif
    listener->worker_started = 1;
    return KC_NETL_OK;
}

static void kc_netl_worker_join(kc_netl_t *listener) {
    if (!listener->worker_started) return;
#ifdef _WIN32
    WaitForSingleObject(listener->worker, INFINITE);
    CloseHandle(listener->worker);
    listener->worker = NULL;
#else
    (void)pthread_join(listener->worker, NULL);
#endif
    listener->worker_started = 0;
}

static void kc_netl_destroy(kc_netl_t *listener) {
    kc_netl_peer_t *peer;

    if (listener == NULL) return;

    while (listener->peers != NULL) {
        peer = listener->peers;
        listener->peers = peer->next;
        if (peer->fd != KC_NETL_FD_INVALID) KC_NETL_CLOSE(peer->fd);
        kc_netl_output_free(peer->out_head);
        free(peer);
    }

    kc_netl_output_free(listener->udp_out_head);
    free(listener->pollfds);
    free(listener->pollmap);

    if (listener->fd != KC_NETL_FD_INVALID) {
        KC_NETL_CLOSE(listener->fd);
        listener->fd = KC_NETL_FD_INVALID;
    }

    if (listener->mutex_ready) {
        kc_netl_mutex_close(&listener->mutex);
        listener->mutex_ready = 0;
    }
    if (listener->platform_ready) {
        kc_netl_platform_close();
        listener->platform_ready = 0;
    }

    free(listener);
}

static int kc_netl_open_internal(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_handler_t handler,
    kc_netl_close_handler_t close_handler,
    kc_netl_error_handler_t error_handler,
    void *userdata
#ifdef KC_NETL_CLI
    ,
    void (*cli_accept_handler)(kc_netl_peer_t *peer, void *userdata),
    void *cli_accept_userdata
#endif
) {
    kc_netl_t *listener;
    int rc;

    if (out == NULL) return KC_NETL_EINVAL;
    *out = NULL;
    if (
        options == NULL ||
        handler == NULL ||
        (
            options->protocol != KC_NETL_TCP &&
            options->protocol != KC_NETL_UDP
        ) ||
        (
            options->max_pending_connections != NULL &&
            *options->max_pending_connections < 0
        )
    ) {
        return KC_NETL_EINVAL;
    }

    listener = (kc_netl_t *)calloc(1, sizeof(*listener));
    if (listener == NULL) return KC_NETL_ENOMEM;
    listener->fd = KC_NETL_FD_INVALID;
    listener->protocol = options->protocol;
    listener->max_pending_connections =
        options->max_pending_connections != NULL
            ? *options->max_pending_connections
            : KC_NETL_DEFAULT_MAX_PENDING_CONNECTIONS;
    listener->handler = handler;
    listener->close_handler = close_handler;
    listener->error_handler = error_handler;
    listener->userdata = userdata;
#ifdef KC_NETL_CLI
    listener->cli_accept_handler = cli_accept_handler;
    listener->cli_accept_userdata = cli_accept_userdata;
#endif

    rc = kc_netl_platform_open();
    if (rc != KC_NETL_OK) {
        free(listener);
        return rc;
    }
    listener->platform_ready = 1;

    if (kc_netl_mutex_open(&listener->mutex) != 0) {
        kc_netl_destroy(listener);
        return KC_NETL_ENET;
    }
    listener->mutex_ready = 1;

    rc = kc_netl_bind(options, &listener->fd, &listener->port);
    if (rc != KC_NETL_OK) {
        kc_netl_destroy(listener);
        return rc;
    }

    rc = kc_netl_worker_start(listener);
    if (rc != KC_NETL_OK) {
        kc_netl_destroy(listener);
        return rc;
    }

    *out = listener;
    return KC_NETL_OK;
}

int kc_netl_open(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_handler_t handler,
    kc_netl_close_handler_t close_handler,
    kc_netl_error_handler_t error_handler,
    void *userdata
) {
    return kc_netl_open_internal(
        out,
        options,
        handler,
        close_handler,
        error_handler,
        userdata
#ifdef KC_NETL_CLI
        ,
        NULL,
        NULL
#endif
    );
}

int kc_netl_respond(
    kc_netl_peer_t *peer,
    const void *data,
    size_t data_size
) {
    kc_netl_t *listener;
    kc_netl_output_t *output;

    if (
        peer == NULL ||
        peer->listener == NULL ||
        (data == NULL && data_size != 0U)
    ) {
        return KC_NETL_EINVAL;
    }

    listener = peer->listener;
    if (peer->protocol == KC_NETL_UDP && data_size > (size_t)INT_MAX) {
        return KC_NETL_EINVAL;
    }

    output = kc_netl_output_new(data, data_size);
    if (output == NULL) return KC_NETL_ENOMEM;

    kc_netl_lock(listener);
    if (peer->closed || peer->closing || peer->taken || listener->stop) {
        kc_netl_unlock(listener);
        kc_netl_output_free(output);
        return KC_NETL_ECLOSED;
    }

    if (peer->protocol == KC_NETL_TCP) {
        if (peer->out_tail != NULL) {
            peer->out_tail->next = output;
        } else {
            peer->out_head = output;
        }
        peer->out_tail = output;
    } else {
        output->address = peer->address;
        output->address_size = peer->address_size;
        if (listener->udp_out_tail != NULL) {
            listener->udp_out_tail->next = output;
        } else {
            listener->udp_out_head = output;
        }
        listener->udp_out_tail = output;
    }
    kc_netl_unlock(listener);

    return KC_NETL_OK;
}

void kc_netl_peer_close(kc_netl_peer_t *peer) {
    kc_netl_t *listener;

    if (peer == NULL || peer->listener == NULL) return;
    listener = peer->listener;

    kc_netl_lock(listener);
    if (!peer->closed) peer->closing = 1;
    kc_netl_unlock(listener);
}

unsigned short kc_netl_port(const kc_netl_t *listener) {
    return listener != NULL ? listener->port : 0U;
}

void kc_netl_close(kc_netl_t *listener) {
    if (listener == NULL) return;

    kc_netl_lock(listener);
    listener->stop = 1;
    kc_netl_unlock(listener);

    kc_netl_worker_join(listener);
    kc_netl_destroy(listener);
}

#ifdef KC_NETL_CLI
int kc_netl_cli_open(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_handler_t handler,
    void *userdata,
    void (*accept_handler)(kc_netl_peer_t *peer, void *userdata),
    void *accept_userdata
) {
    return kc_netl_open_internal(
        out,
        options,
        handler,
        NULL,
        NULL,
        userdata,
        accept_handler,
        accept_userdata
    );
}

intptr_t kc_netl_cli_take_peer(kc_netl_peer_t *peer) {
    kc_netl_t *listener;
    kc_netl_fd_t fd;

    if (
        peer == NULL ||
        peer->listener == NULL ||
        peer->protocol != KC_NETL_TCP
    ) {
        return (intptr_t)-1;
    }

    listener = peer->listener;
    kc_netl_lock(listener);
    if (peer->closed || peer->closing || peer->taken) {
        kc_netl_unlock(listener);
        return (intptr_t)-1;
    }

    fd = peer->fd;
    peer->fd = KC_NETL_FD_INVALID;
    peer->taken = 1;
    peer->closing = 1;
    kc_netl_unlock(listener);
    return (intptr_t)fd;
}
#endif

const char *kc_netl_strerror(int status) {
    switch (status) {
        case KC_NETL_OK: return "ok";
        case KC_NETL_EINVAL: return "invalid argument";
        case KC_NETL_ENET: return "network error";
        case KC_NETL_ECLOSED: return "peer closed";
        case KC_NETL_ENOMEM: return "out of memory";
        default: return "unknown error";
    }
}

#ifndef KC_NETL_BUILD_VERSION
#define KC_NETL_BUILD_VERSION 0
#endif

uint64_t kc_netl_version(void) {
    return (uint64_t)KC_NETL_BUILD_VERSION;
}
