/**
 * libnets.c - Persistent outbound network transport.
 * Summary: Exchanges raw bytes through persistent TCP or UDP endpoints.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnets.h"

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
typedef SOCKET kc_nets_fd_t;
typedef int kc_nets_socklen_t;
typedef WSAPOLLFD kc_nets_pollfd_t;
typedef CRITICAL_SECTION kc_nets_mutex_t;
typedef HANDLE kc_nets_thread_t;
#define KC_NETS_FD_INVALID INVALID_SOCKET
#define KC_NETS_CLOSE(fd) closesocket(fd)
#define KC_NETS_POLL(fds, count, timeout) \
    WSAPoll((fds), (ULONG)(count), (timeout))
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_nets_fd_t;
typedef socklen_t kc_nets_socklen_t;
typedef struct pollfd kc_nets_pollfd_t;
typedef pthread_mutex_t kc_nets_mutex_t;
typedef pthread_t kc_nets_thread_t;
#define KC_NETS_FD_INVALID (-1)
#define KC_NETS_CLOSE(fd) close(fd)
#define KC_NETS_POLL(fds, count, timeout) \
    poll((fds), (nfds_t)(count), (timeout))
#endif

#ifdef MSG_NOSIGNAL
#define KC_NETS_SEND_FLAGS MSG_NOSIGNAL
#else
#define KC_NETS_SEND_FLAGS 0
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#define KC_NETS_BUFFER_SIZE 65536U
#define KC_NETS_POLL_TIMEOUT_MS 25

typedef struct kc_nets_output {
    unsigned char *data;
    size_t size;
    size_t offset;
    struct sockaddr_storage destination;
    kc_nets_socklen_t destination_size;
    struct kc_nets_output *next;
} kc_nets_output_t;

struct kc_nets {
    kc_nets_fd_t fd;
    int protocol;
    int platform_ready;
    kc_nets_receive_fn receive;
    struct sockaddr_storage default_destination;
    kc_nets_socklen_t default_destination_size;
    unsigned short local_port;
    int udp_family;
    kc_nets_disconnect_fn disconnect;
    kc_nets_error_fn error;
    void *userdata;
    kc_nets_output_t *out_head;
    kc_nets_output_t *out_tail;
    kc_nets_mutex_t mutex;
    int mutex_ready;
    kc_nets_thread_t worker;
#ifdef _WIN32
    DWORD worker_id;
#endif
    int worker_started;
    int stop;
    int terminal;
    int self_close;
};

/**
 * Starts platform socket support.
 * @return KC_NETS_OK on success, otherwise KC_NETS_ENET.
 */
static int kc_nets_platform_open(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0
        ? KC_NETS_OK
        : KC_NETS_ENET;
#else
    return KC_NETS_OK;
#endif
}

/**
 * Releases platform socket support.
 * @return void.
 */
static void kc_nets_platform_close(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Returns whether the last socket operation would block.
 * @return Non-zero when the last socket error means would-block.
 */
static int kc_nets_would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

/**
 * Returns whether the last socket operation was interrupted.
 * @return Non-zero when the last socket error means interrupted.
 */
static int kc_nets_interrupted(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

/**
 * Classifies peer-specific UDP reachability errors. These do not invalidate
 * the local socket or its mapping and therefore must not terminate the
 * persistent sender when another destination may still be reachable.
 * @param code Platform socket error.
 * @return Non-zero when the error is per-remote-endpoint.
 */
static int kc_nets_udp_peer_error(int code)
{
#ifdef _WIN32
    return code==WSAECONNRESET||code==WSAECONNREFUSED||
        code==WSAEHOSTUNREACH||code==WSAENETUNREACH;
#else
    return code==ECONNREFUSED||code==EHOSTUNREACH||code==ENETUNREACH;
#endif
}

/**
 * Returns the most recent socket error in the current thread.
 * @return Platform socket error.
 */
static int kc_nets_socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/**
 * Makes one socket nonblocking.
 * @param fd Socket descriptor.
 * @return KC_NETS_OK on success, otherwise KC_NETS_ENET.
 */
static int kc_nets_nonblocking(kc_nets_fd_t fd) {
#ifdef _WIN32
    u_long mode = 1UL;
    return ioctlsocket(fd, FIONBIO, &mode) == 0
        ? KC_NETS_OK
        : KC_NETS_ENET;
#else
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) return KC_NETS_ENET;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0
        ? KC_NETS_OK
        : KC_NETS_ENET;
#endif
}

/**
 * Disables SIGPIPE delivery for one socket where supported.
 * @param fd Socket descriptor.
 * @return void.
 */
static void kc_nets_disable_sigpipe(kc_nets_fd_t fd) {
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int enabled = 1;

    (void)setsockopt(
        fd,
        SOL_SOCKET,
        SO_NOSIGPIPE,
        &enabled,
        (socklen_t)sizeof(enabled)
    );
#else
    (void)fd;
#endif
}

/**
 * Opens one endpoint mutex.
 * @param mutex Destination mutex.
 * @return Zero on success, non-zero on failure.
 */
static int kc_nets_mutex_open(kc_nets_mutex_t *mutex) {
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

/**
 * Releases one endpoint mutex.
 * @param mutex Mutex to release.
 * @return void.
 */
static void kc_nets_mutex_close(kc_nets_mutex_t *mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    (void)pthread_mutex_destroy(mutex);
#endif
}

/**
 * Locks one endpoint.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_lock(kc_nets_t *nets) {
#ifdef _WIN32
    EnterCriticalSection(&nets->mutex);
#else
    (void)pthread_mutex_lock(&nets->mutex);
#endif
}

/**
 * Unlocks one endpoint.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_unlock(kc_nets_t *nets) {
#ifdef _WIN32
    LeaveCriticalSection(&nets->mutex);
#else
    (void)pthread_mutex_unlock(&nets->mutex);
#endif
}

/**
 * Requests socket shutdown without releasing the descriptor.
 * @param fd Socket descriptor.
 * @return void.
 */
static void kc_nets_socket_shutdown(kc_nets_fd_t fd) {
#ifdef _WIN32
    (void)shutdown(fd, SD_BOTH);
#else
    (void)shutdown(fd, SHUT_RDWR);
#endif
}

/**
 * Allocates one copied output buffer.
 * @param data Source bytes.
 * @param size Byte count.
 * @return New output buffer, or NULL on allocation failure.
 */
static kc_nets_output_t *kc_nets_output_new(
    const void *data,
    size_t size
) {
    kc_nets_output_t *output;
    size_t allocation_size;

    output = (kc_nets_output_t *)calloc(1, sizeof(*output));
    if (output == NULL) return NULL;

    allocation_size = size != 0U ? size : 1U;
    output->data = (unsigned char *)malloc(allocation_size);
    if (output->data == NULL) {
        free(output);
        return NULL;
    }
    if (size != 0U) memcpy(output->data, data, size);
    output->size = size;
    return output;
}

/**
 * Releases an output queue.
 * @param output First queued output.
 * @return void.
 */
static void kc_nets_output_free(kc_nets_output_t *output) {
    while (output != NULL) {
        kc_nets_output_t *next = output->next;

        free(output->data);
        free(output);
        output = next;
    }
}

/**
 * Connects one socket to the configured destination.
 * @param options Endpoint options.
 * @param out_fd Destination socket descriptor.
 * @return KC_NETS_OK on success, otherwise KC_NETS_ENET.
 */
static int kc_nets_connect(
    const kc_nets_options_t *options,
    kc_nets_t *nets
) {
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *item;
    char service[16];
    int rc;

    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=options->protocol==KC_NETS_UDP?SOCK_DGRAM:SOCK_STREAM;
    snprintf(service,sizeof(service),"%u",(unsigned)options->port);
    result=NULL;
    rc=getaddrinfo(options->host,service,&hints,&result);
    if(rc!=0)return KC_NETS_ENET;
    for(item=result;item!=NULL;item=item->ai_next){
        kc_nets_fd_t fd=socket(item->ai_family,item->ai_socktype,item->ai_protocol);
        if(fd==KC_NETS_FD_INVALID)continue;
        kc_nets_disable_sigpipe(fd);
        if(options->protocol==KC_NETS_UDP){
            struct addrinfo bind_hints,*local=NULL,*it;
            char port_text[16];
            int bound=0;
            memset(&bind_hints,0,sizeof(bind_hints));
            bind_hints.ai_family=item->ai_family;
            bind_hints.ai_socktype=SOCK_DGRAM;
            bind_hints.ai_flags=AI_PASSIVE;
            snprintf(port_text,sizeof(port_text),"%u",(unsigned)options->local_port);
            if(getaddrinfo(options->local_host&&*options->local_host?
                options->local_host:NULL,port_text,&bind_hints,&local)!=0){
                KC_NETS_CLOSE(fd);continue;
            }
            for(it=local;it!=NULL;it=it->ai_next){
#ifdef _WIN32
                if(bind(fd,it->ai_addr,(int)it->ai_addrlen)==0){bound=1;break;}
#else
                if(bind(fd,it->ai_addr,it->ai_addrlen)==0){bound=1;break;}
#endif
            }
            freeaddrinfo(local);
            if(!bound){KC_NETS_CLOSE(fd);continue;}
            if(kc_nets_nonblocking(fd)!=KC_NETS_OK){KC_NETS_CLOSE(fd);continue;}
            memcpy(&nets->default_destination,item->ai_addr,item->ai_addrlen);
            nets->default_destination_size=(kc_nets_socklen_t)item->ai_addrlen;
            nets->udp_family=item->ai_family;
            {
                struct sockaddr_storage local_addr;
                kc_nets_socklen_t len=(kc_nets_socklen_t)sizeof(local_addr);
                if(getsockname(fd,(struct sockaddr *)&local_addr,&len)!=0){
                    KC_NETS_CLOSE(fd);continue;
                }
                nets->local_port=local_addr.ss_family==AF_INET?
                    ntohs(((struct sockaddr_in *)&local_addr)->sin_port):
                    ntohs(((struct sockaddr_in6 *)&local_addr)->sin6_port);
            }
            nets->fd=fd;
            freeaddrinfo(result);
            return KC_NETS_OK;
        }
#ifdef _WIN32
        rc=connect(fd,item->ai_addr,(int)item->ai_addrlen);
#else
        rc=connect(fd,item->ai_addr,item->ai_addrlen);
#endif
        if(rc==0&&kc_nets_nonblocking(fd)==KC_NETS_OK){
            nets->fd=fd;
            freeaddrinfo(result);
            return KC_NETS_OK;
        }
        KC_NETS_CLOSE(fd);
    }
    freeaddrinfo(result);
    return KC_NETS_ENET;
}

/**
 * Returns whether one endpoint can still exchange bytes.
 * @param nets Endpoint.
 * @return Non-zero while the endpoint is active.
 */
static int kc_nets_active(kc_nets_t *nets) {
    int active;

    kc_nets_lock(nets);
    active = !nets->stop && !nets->terminal;
    kc_nets_unlock(nets);
    return active;
}

/**
 * Returns whether one endpoint has queued output.
 * @param nets Endpoint.
 * @return Non-zero when output is queued.
 */
static int kc_nets_has_output(kc_nets_t *nets) {
    int has_output;

    kc_nets_lock(nets);
    has_output = nets->out_head != NULL;
    kc_nets_unlock(nets);
    return has_output;
}

/**
 * Delivers received bytes when the endpoint remains active.
 * @param nets Endpoint.
 * @param data Received bytes.
 * @param size Byte count.
 * @return void.
 */
static void kc_nets_deliver(
    kc_nets_t *nets,
    const void *data,
    size_t size,
    const kc_nets_endpoint_t *peer
) {
    kc_nets_receive_fn receive;
    void *userdata;

    receive = NULL;
    userdata = NULL;

    kc_nets_lock(nets);
    if (!nets->stop && !nets->terminal) {
        receive = nets->receive;
        userdata = nets->userdata;
    }
    kc_nets_unlock(nets);

    if (receive != NULL) receive(data, size, peer, userdata);
}

/**
 * Ends one TCP endpoint after a remote disconnect.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_disconnected(kc_nets_t *nets) {
    kc_nets_disconnect_fn disconnect;
    void *userdata;

    disconnect = NULL;
    userdata = NULL;

    kc_nets_lock(nets);
    if (!nets->stop && !nets->terminal) {
        nets->terminal = 1;
        disconnect = nets->disconnect;
        userdata = nets->userdata;
    }
    kc_nets_unlock(nets);

    if (disconnect != NULL) disconnect(userdata);
}

/**
 * Ends one endpoint after an asynchronous network failure.
 * @param nets Endpoint.
 * @param status Public failure status.
 * @return void.
 */
static void kc_nets_fail(kc_nets_t *nets, int status) {
    kc_nets_error_fn error;
    void *userdata;

    error = NULL;
    userdata = NULL;

    kc_nets_lock(nets);
    if (!nets->stop && !nets->terminal) {
        nets->terminal = 1;
        error = nets->error;
        userdata = nets->userdata;
    }
    kc_nets_unlock(nets);

    if (error != NULL) error(status, userdata);
}

/**
 * Receives one TCP stream chunk.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_receive_tcp(kc_nets_t *nets) {
    unsigned char buffer[KC_NETS_BUFFER_SIZE];
    int received;

    received = (int)recv(
        nets->fd,
        (char *)buffer,
        (int)sizeof(buffer),
        0
    );
    if (received > 0) {
        kc_nets_deliver(nets, buffer, (size_t)received, NULL);
        return;
    }
    if (received == 0) {
        kc_nets_disconnected(nets);
        return;
    }
    if (!kc_nets_would_block()) kc_nets_fail(nets, KC_NETS_ENET);
}

/**
 * Receives one UDP datagram.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_receive_udp(kc_nets_t *nets) {
    unsigned char buffer[KC_NETS_BUFFER_SIZE];
    struct sockaddr_storage source;
    kc_nets_socklen_t source_size=(kc_nets_socklen_t)sizeof(source);
    int received=(int)recvfrom(nets->fd,(char *)buffer,(int)sizeof(buffer),
        0,(struct sockaddr *)&source,&source_size);
    if(received>=0){
        char host[NI_MAXHOST],service[16];
        kc_nets_endpoint_t peer;
        if(getnameinfo((struct sockaddr *)&source,source_size,
            host,(kc_nets_socklen_t)sizeof(host),
            service,(kc_nets_socklen_t)sizeof(service),
            NI_NUMERICHOST|NI_NUMERICSERV)!=0){
            kc_nets_fail(nets,KC_NETS_ENET);
            return;
        }
        peer.host=host;
        peer.port=(unsigned short)strtoul(service,NULL,10);
        kc_nets_deliver(nets,buffer,(size_t)received,&peer);
        return;
    }
    if(!kc_nets_would_block()&&
        !kc_nets_udp_peer_error(kc_nets_socket_error()))
        kc_nets_fail(nets,KC_NETS_ENET);
}

/**
 * Flushes queued TCP stream bytes.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_flush_tcp(kc_nets_t *nets) {
    int failed;

    failed = 0;
    kc_nets_lock(nets);
    while (
        nets->out_head != NULL &&
        !nets->stop &&
        !nets->terminal
    ) {
        kc_nets_output_t *output;
        size_t remain;
        int amount;
        int sent;

        output = nets->out_head;
        remain = output->size - output->offset;
        amount = remain > (size_t)INT_MAX ? INT_MAX : (int)remain;

        if (amount == 0) {
            nets->out_head = output->next;
            if (nets->out_head == NULL) nets->out_tail = NULL;
            free(output->data);
            free(output);
            continue;
        }

        sent = (int)send(
            nets->fd,
            (const char *)output->data + output->offset,
            amount,
            KC_NETS_SEND_FLAGS
        );
        if (sent > 0) {
            output->offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && kc_nets_would_block()) break;

        failed = 1;
        break;
    }
    kc_nets_unlock(nets);

    if (failed) kc_nets_fail(nets, KC_NETS_ENET);
}

/**
 * Flushes queued UDP datagrams.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_flush_udp(kc_nets_t *nets) {
    for (;;) {
        kc_nets_output_t *output;
        int sent;

        kc_nets_lock(nets);
        if (nets->stop || nets->terminal) {
            kc_nets_unlock(nets);
            return;
        }

        output = nets->out_head;
        if (output == NULL) {
            kc_nets_unlock(nets);
            return;
        }

        sent = (int)sendto(nets->fd,(const char *)output->data,
            (int)output->size,KC_NETS_SEND_FLAGS,
            (const struct sockaddr *)&output->destination,
            output->destination_size);
        if (sent == (int)output->size) {
            nets->out_head = output->next;
            if (nets->out_head == NULL) nets->out_tail = NULL;
            kc_nets_unlock(nets);
            free(output->data);
            free(output);
            continue;
        }
        if (sent < 0 && kc_nets_would_block()) {
            kc_nets_unlock(nets);
            return;
        }
        if(sent<0&&kc_nets_udp_peer_error(kc_nets_socket_error())){
            nets->out_head=output->next;
            if(nets->out_head==NULL)nets->out_tail=NULL;
            kc_nets_unlock(nets);
            free(output->data);free(output);
            continue;
        }

        kc_nets_unlock(nets);
        kc_nets_fail(nets, KC_NETS_ENET);
        return;
    }
}

/**
 * Closes the worker-owned socket.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_worker_socket_close(kc_nets_t *nets) {
    kc_nets_fd_t fd;

    fd = KC_NETS_FD_INVALID;

    kc_nets_lock(nets);
    if (nets->fd != KC_NETS_FD_INVALID) {
        fd = nets->fd;
        nets->fd = KC_NETS_FD_INVALID;
    }
    kc_nets_unlock(nets);

    if (fd != KC_NETS_FD_INVALID) KC_NETS_CLOSE(fd);
}

/**
 * Runs one endpoint worker loop.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_worker_run(kc_nets_t *nets) {
    while (kc_nets_active(nets)) {
        kc_nets_pollfd_t pollfd;
        int ready;

        memset(&pollfd, 0, sizeof(pollfd));
        pollfd.fd = nets->fd;
        pollfd.events = POLLIN;
        if (kc_nets_has_output(nets)) pollfd.events |= POLLOUT;

        ready = KC_NETS_POLL(&pollfd, 1U, KC_NETS_POLL_TIMEOUT_MS);
        if (ready < 0) {
            if (kc_nets_interrupted()) continue;
            kc_nets_fail(nets, KC_NETS_ENET);
            break;
        }
        if (ready == 0) continue;
        if (!kc_nets_active(nets)) break;

        if ((pollfd.revents & POLLIN) != 0) {
            if (nets->protocol == KC_NETS_UDP) {
                kc_nets_receive_udp(nets);
            } else {
                kc_nets_receive_tcp(nets);
            }
        }
        if (!kc_nets_active(nets)) break;

        if ((pollfd.revents & POLLOUT) != 0) {
            if (nets->protocol == KC_NETS_UDP) {
                kc_nets_flush_udp(nets);
            } else {
                kc_nets_flush_tcp(nets);
            }
        }
        if (!kc_nets_active(nets)) break;

        if ((pollfd.revents & (POLLERR | POLLNVAL)) != 0) {
            if(nets->protocol==KC_NETS_UDP&&
                (pollfd.revents&POLLNVAL)==0){
                int code=0;
#ifdef _WIN32
                int bytes=(int)sizeof(code);
                if(getsockopt(nets->fd,SOL_SOCKET,SO_ERROR,
                    (char *)&code,&bytes)==0&&
                    (code==0||kc_nets_udp_peer_error(code)))continue;
#else
                socklen_t bytes=(socklen_t)sizeof(code);
                if(getsockopt(nets->fd,SOL_SOCKET,SO_ERROR,
                    &code,&bytes)==0&&
                    (code==0||kc_nets_udp_peer_error(code)))continue;
#endif
            }
            kc_nets_fail(nets, KC_NETS_ENET);
            break;
        }
        if ((pollfd.revents & POLLHUP) != 0) {
            if(nets->protocol==KC_NETS_TCP){
                kc_nets_disconnected(nets);
                break;
            }
        }
    }
}

/**
 * Destroys one endpoint after its worker is no longer joinable.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_destroy(kc_nets_t *nets) {
    if (nets == NULL) return;

    if (nets->fd != KC_NETS_FD_INVALID) {
        KC_NETS_CLOSE(nets->fd);
        nets->fd = KC_NETS_FD_INVALID;
    }
    kc_nets_output_free(nets->out_head);
    if (nets->mutex_ready) {
        kc_nets_mutex_close(&nets->mutex);
        nets->mutex_ready = 0;
    }
    if (nets->platform_ready) {
        kc_nets_platform_close();
        nets->platform_ready = 0;
    }
    free(nets);
}

#ifdef _WIN32
/**
 * Runs one outbound endpoint worker thread.
 * @param arg Endpoint pointer.
 * @return Thread exit code.
 */
static DWORD WINAPI kc_nets_worker_entry(LPVOID arg) {
    kc_nets_t *nets;
    int self_close;

    nets = (kc_nets_t *)arg;
    kc_nets_worker_run(nets);
    kc_nets_worker_socket_close(nets);

    kc_nets_lock(nets);
    self_close = nets->self_close;
    kc_nets_unlock(nets);

    if (self_close) {
        CloseHandle(nets->worker);
        nets->worker = NULL;
        nets->worker_started = 0;
        kc_nets_destroy(nets);
    }
    return 0;
}
#else
/**
 * Runs one outbound endpoint worker thread.
 * @param arg Endpoint pointer.
 * @return NULL.
 */
static void *kc_nets_worker_entry(void *arg) {
    kc_nets_t *nets;
    int self_close;

    nets = (kc_nets_t *)arg;
    kc_nets_worker_run(nets);
    kc_nets_worker_socket_close(nets);

    kc_nets_lock(nets);
    self_close = nets->self_close;
    kc_nets_unlock(nets);

    if (self_close) {
        (void)pthread_detach(pthread_self());
        nets->worker_started = 0;
        kc_nets_destroy(nets);
    }
    return NULL;
}
#endif

/**
 * Starts one endpoint worker.
 * @param nets Endpoint.
 * @return KC_NETS_OK on success, otherwise KC_NETS_ENET.
 */
static int kc_nets_worker_start(kc_nets_t *nets) {
#ifdef _WIN32
    nets->worker = CreateThread(
        NULL,
        0,
        kc_nets_worker_entry,
        nets,
        0,
        &nets->worker_id
    );
    if (nets->worker == NULL) return KC_NETS_ENET;
#else
    if (
        pthread_create(
            &nets->worker,
            NULL,
            kc_nets_worker_entry,
            nets
        ) != 0
    ) {
        return KC_NETS_ENET;
    }
#endif
    nets->worker_started = 1;
    return KC_NETS_OK;
}

/**
 * Joins one endpoint worker.
 * @param nets Endpoint.
 * @return void.
 */
static void kc_nets_worker_join(kc_nets_t *nets) {
    if (!nets->worker_started) return;
#ifdef _WIN32
    WaitForSingleObject(nets->worker, INFINITE);
    CloseHandle(nets->worker);
    nets->worker = NULL;
#else
    (void)pthread_join(nets->worker, NULL);
#endif
    nets->worker_started = 0;
}

/**
 * Opens one persistent outbound TCP or UDP endpoint.
 * @param out Destination endpoint pointer.
 * @param options Endpoint options.
 * @param receive Incoming byte callback.
 * @param disconnect Optional TCP disconnect callback.
 * @param error Optional asynchronous failure callback.
 * @param userdata Consumer callback data.
 * @return KC_NETS_OK on success, otherwise a negative status.
 */
static int kc_nets_open_internal(
    kc_nets_t **out,
    const kc_nets_options_t *options,
    kc_nets_receive_fn receive,
    kc_nets_disconnect_fn disconnect,
    kc_nets_error_fn error,
    void *userdata
) {
    kc_nets_t *nets;
    int rc;

    if (out == NULL) return KC_NETS_EINVAL;
    *out = NULL;
    if (
        options == NULL ||
        options->host == NULL ||
        options->host[0] == '\0' ||
        options->port == 0U ||
        (
            options->protocol != KC_NETS_TCP &&
            options->protocol != KC_NETS_UDP
        ) ||
        receive == NULL
    ) {
        return KC_NETS_EINVAL;
    }

    nets = (kc_nets_t *)calloc(1, sizeof(*nets));
    if (nets == NULL) return KC_NETS_ENOMEM;
    nets->fd = KC_NETS_FD_INVALID;
    nets->protocol = options->protocol;
    nets->receive = receive;
    nets->disconnect = disconnect;
    nets->error = error;
    nets->userdata = userdata;

    rc = kc_nets_platform_open();
    if (rc != KC_NETS_OK) {
        free(nets);
        return rc;
    }
    nets->platform_ready = 1;

    if (kc_nets_mutex_open(&nets->mutex) != 0) {
        kc_nets_destroy(nets);
        return KC_NETS_ENET;
    }
    nets->mutex_ready = 1;

    rc = kc_nets_connect(options, nets);
    if (rc != KC_NETS_OK) {
        kc_nets_destroy(nets);
        return rc;
    }

    rc = kc_nets_worker_start(nets);
    if (rc != KC_NETS_OK) {
        kc_nets_destroy(nets);
        return rc;
    }

    *out = nets;
    return KC_NETS_OK;
}

/**
 * Opens the established legacy TCP/UDP endpoint contract.
 * @param out Endpoint handle.
 * @param options Destination configuration.
 * @param receive Received bytes callback.
 * @param disconnect TCP disconnect callback.
 * @param error Error callback.
 * @param userdata User context.
 * @return Public status.
 */
int kc_nets_open(kc_nets_t **out,const kc_nets_options_t *options,
    kc_nets_receive_fn receive,kc_nets_disconnect_fn disconnect,
    kc_nets_error_fn error,void *userdata)
{
    return kc_nets_open_internal(out,options,receive,disconnect,error,userdata);
}

/**
 * Queues bytes for one open endpoint.
 * @param nets Endpoint.
 * @param data Source bytes.
 * @param data_size Byte count.
 * @return KC_NETS_OK on success, otherwise a negative status.
 */
int kc_nets_send(
    kc_nets_t *nets,
    const void *data,
    size_t data_size
) {
    kc_nets_output_t *output;

    if (nets == NULL || (data == NULL && data_size != 0U)) {
        return KC_NETS_EINVAL;
    }
    if (nets->protocol == KC_NETS_UDP && data_size > 65507U) {
        return KC_NETS_EINVAL;
    }

    output = kc_nets_output_new(data, data_size);
    if (output == NULL) return KC_NETS_ENOMEM;
    if(nets->protocol==KC_NETS_UDP){
        output->destination=nets->default_destination;
        output->destination_size=nets->default_destination_size;
    }

    kc_nets_lock(nets);
    if (nets->stop || nets->terminal) {
        kc_nets_unlock(nets);
        kc_nets_output_free(output);
        return KC_NETS_ECLOSED;
    }

    if (nets->out_tail != NULL) {
        nets->out_tail->next = output;
    } else {
        nets->out_head = output;
    }
    nets->out_tail = output;
    kc_nets_unlock(nets);

    return KC_NETS_OK;
}

/**
 * Queues an independent datagram to an arbitrary destination.
 * @param nets Persistent UDP sender.
 * @param host Destination host.
 * @param port Destination port.
 * @param data Datagram bytes.
 * @param data_size Byte count.
 * @return KC_NETS_OK or a negative status.
 */
int kc_nets_send_to(kc_nets_t *nets,const kc_nets_endpoint_t *peer,
    const void *data,size_t data_size)
{
    struct addrinfo hints,*result,*item;
    char service[16];
    kc_nets_output_t *output;
    if(!nets||nets->protocol!=KC_NETS_UDP||!peer||!peer->host||
        !*peer->host||!peer->port||(!data&&data_size)||
        data_size>65507U)return KC_NETS_EINVAL;
    memset(&hints,0,sizeof(hints));hints.ai_family=nets->udp_family;
    hints.ai_socktype=SOCK_DGRAM;
    snprintf(service,sizeof(service),"%u",(unsigned)peer->port);
    if(getaddrinfo(peer->host,service,&hints,&result)!=0)return KC_NETS_ENET;
    item=result;
    if(!item||item->ai_addrlen>sizeof(struct sockaddr_storage)){
        freeaddrinfo(result);return KC_NETS_ENET;
    }
    output=kc_nets_output_new(data,data_size);
    if(!output){freeaddrinfo(result);return KC_NETS_ENOMEM;}
    memcpy(&output->destination,item->ai_addr,item->ai_addrlen);
    output->destination_size=(kc_nets_socklen_t)item->ai_addrlen;
    freeaddrinfo(result);
    kc_nets_lock(nets);
    if(nets->stop||nets->terminal){
        kc_nets_unlock(nets);kc_nets_output_free(output);return KC_NETS_ECLOSED;
    }
    if(nets->out_tail)nets->out_tail->next=output;
    else nets->out_head=output;
    nets->out_tail=output;
    kc_nets_unlock(nets);
    return KC_NETS_OK;
}

/**
 * Returns the locally bound UDP port.
 * @param nets Open endpoint.
 * @return The local port, or zero.
 */
unsigned short kc_nets_local_port(const kc_nets_t *nets)
{
    return nets?nets->local_port:0;
}

/**
 * Closes and releases one outbound endpoint.
 * @param nets Endpoint, or NULL.
 * @return void.
 */
void kc_nets_close(kc_nets_t *nets) {
    int from_worker;

    if (nets == NULL) return;

#ifdef _WIN32
    from_worker = nets->worker_started &&
        GetCurrentThreadId() == nets->worker_id;
#else
    from_worker = nets->worker_started &&
        pthread_equal(pthread_self(), nets->worker);
#endif

    kc_nets_lock(nets);
    nets->stop = 1;
    if (from_worker) nets->self_close = 1;
    if (nets->fd != KC_NETS_FD_INVALID) {
        kc_nets_socket_shutdown(nets->fd);
    }
    kc_nets_unlock(nets);

    if (from_worker) return;

    kc_nets_worker_join(nets);
    kc_nets_destroy(nets);
}

/**
 * Returns a static message for one public status code.
 * @param status Public status code.
 * @return Static status message.
 */
const char *kc_nets_strerror(int status) {
    switch (status) {
        case KC_NETS_OK: return "ok";
        case KC_NETS_EINVAL: return "invalid argument";
        case KC_NETS_ENET: return "network error";
        case KC_NETS_ECLOSED: return "endpoint closed";
        case KC_NETS_ENOMEM: return "out of memory";
        default: return "unknown error";
    }
}

#ifndef KC_NETS_BUILD_VERSION
#define KC_NETS_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_nets_version(void) {
    return (uint64_t)KC_NETS_BUILD_VERSION;
}
