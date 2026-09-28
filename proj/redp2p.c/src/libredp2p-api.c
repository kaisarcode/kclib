/**
 * libredp2p-api.c - REDP2P public capability API.
 * Summary: Thin idx/pub/con handles over the private coordination engine.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p-core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef _WIN32
#include <time.h>
#endif

#define KC_REDP2P_READY_TIMEOUT_MS 10000U

typedef struct {
    redp2p_t *ctx;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int thread_started;
    _Atomic int done;
    int result;
} kc_redp2p_runtime_t;

struct kc_redp2p_idx {
    kc_redp2p_runtime_t runtime;
    char host[256];
    uint16_t port;
};

struct kc_redp2p_client {
    struct kc_redp2p_pub *pub;
    redp2p_fd_t fd;
    struct sockaddr_storage address;
    socklen_t address_len;
    int udp;
    _Atomic int closed;
};

struct kc_redp2p_pub {
    kc_redp2p_runtime_t runtime;
    char index_host[256];
    uint16_t index_port;
    char id[KC_REDP2P_ID_MAX + 1];
    uint16_t port;
    int direct;
    int adapter_platform;
    redp2p_fd_t adapter_fd;
#ifdef _WIN32
    HANDLE adapter_thread;
#else
    pthread_t adapter_thread;
#endif
    int adapter_thread_started;
    _Atomic int adapter_stop;
    kc_redp2p_pub_receive_fn receive;
    void *userdata;
    kc_redp2p_client_t **clients;
    size_t client_count;
    size_t client_cap;
};

struct kc_redp2p_con {
    kc_redp2p_runtime_t runtime;
    char index_host[256];
    uint16_t index_port;
    char id[KC_REDP2P_ID_MAX + 1];
    char self_id[KC_REDP2P_ID_MAX + 1];
    uint16_t port;
    int direct;
    int adapter_platform;
    redp2p_fd_t adapter_fd;
#ifdef _WIN32
    HANDLE adapter_thread;
#else
    pthread_t adapter_thread;
#endif
    int adapter_thread_started;
    _Atomic int adapter_stop;
    kc_redp2p_con_receive_fn receive;
    void *userdata;
};

/**
 * Opens a loopback socket and lets the OS choose a private port when requested.
 * @param type SOCK_STREAM or SOCK_DGRAM.
 * @param listen_socket Whether a stream socket should listen.
 * @param requested Requested port, zero for an ephemeral port.
 * @param port_out Effective bound port.
 * @return Bound socket or REDP2P_FD_INVALID.
 */
static redp2p_fd_t kc_redp2p_loopback_socket(int type, int listen_socket,
    uint16_t requested, uint16_t *port_out)
{
    redp2p_fd_t fd;
    struct sockaddr_in address;
    socklen_t length;
    int reuse;

    fd = socket(AF_INET, type, 0);
    if (REDP2P_ISERR(fd)) return REDP2P_FD_INVALID;
    reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse,
        sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(requested);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        REDP2P_FD_CLOSE(fd);
        return REDP2P_FD_INVALID;
    }
    if (listen_socket && listen(fd, 32) != 0) {
        REDP2P_FD_CLOSE(fd);
        return REDP2P_FD_INVALID;
    }
    length = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        REDP2P_FD_CLOSE(fd);
        return REDP2P_FD_INVALID;
    }
    if (port_out) *port_out = ntohs(address.sin_port);
    return fd;
}

/**
 * Reserves and releases one OS-selected loopback port for direct consumption.
 * @param port_out Effective port.
 * @return KC_REDP2P_OK on success or KC_REDP2P_ENET.
 */
static int kc_redp2p_ephemeral_port(uint16_t *port_out)
{
    redp2p_fd_t tcp_fd;
    redp2p_fd_t udp_fd;
    uint16_t port;
    int attempt;

    if (!port_out) return KC_REDP2P_EINVAL;
    for (attempt = 0; attempt < 32; attempt++) {
        port = 0;
        tcp_fd = kc_redp2p_loopback_socket(SOCK_STREAM, 0, 0, &port);
        if (REDP2P_ISERR(tcp_fd)) continue;
        udp_fd = kc_redp2p_loopback_socket(SOCK_DGRAM, 0, port, NULL);
        if (!REDP2P_ISERR(udp_fd)) {
            REDP2P_FD_CLOSE(udp_fd);
            REDP2P_FD_CLOSE(tcp_fd);
            *port_out = port;
            return KC_REDP2P_OK;
        }
        REDP2P_FD_CLOSE(tcp_fd);
    }
    return KC_REDP2P_ENET;
}

/** Adds one stable publisher client object to the adapter-owned list. */
static kc_redp2p_client_t *kc_redp2p_pub_add_client(kc_redp2p_pub_t *pub,
    redp2p_fd_t fd, int udp, const struct sockaddr_storage *address,
    socklen_t address_len)
{
    kc_redp2p_client_t **grown;
    kc_redp2p_client_t *client;
    size_t cap;

    if (!pub) return NULL;
    if (pub->client_count >= pub->client_cap) {
        cap = pub->client_cap ? pub->client_cap * 2U : 8U;
        if (cap < pub->client_cap ||
            cap > SIZE_MAX / sizeof(pub->clients[0]))
            return NULL;
        grown = (kc_redp2p_client_t **)realloc(pub->clients,
            cap * sizeof(pub->clients[0]));
        if (!grown) return NULL;
        pub->clients = grown;
        pub->client_cap = cap;
    }
    client = (kc_redp2p_client_t *)calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->pub = pub;
    client->fd = fd;
    client->udp = udp;
    if (address) client->address = *address;
    client->address_len = address_len;
    pub->clients[pub->client_count++] = client;
    return client;
}

/** Finds or creates the stable client identity for one UDP backend endpoint. */
static kc_redp2p_client_t *kc_redp2p_pub_udp_client(kc_redp2p_pub_t *pub,
    const struct sockaddr_storage *address, socklen_t address_len)
{
    size_t i;

    for (i = 0; i < pub->client_count; i++) {
        kc_redp2p_client_t *client = pub->clients[i];
        if (client && client->udp && !atomic_load(&client->closed) &&
            redp2p_sockaddr_equal(&client->address, address))
            return client;
    }
    return kc_redp2p_pub_add_client(pub, pub->adapter_fd, 1, address,
        address_len);
}

/** Emits one publisher receive event. */
static void kc_redp2p_pub_emit(kc_redp2p_pub_t *pub,
    kc_redp2p_client_t *client, const void *data, size_t size)
{
    kc_redp2p_pub_input_t input;

    if (!pub || !pub->receive || !client || atomic_load(&client->closed))
        return;
    input.client = client;
    input.data = data;
    input.size = size;
    pub->receive(&input, pub->userdata);
}

/** Runs the direct publisher loopback adapter. */
#ifdef _WIN32
static DWORD WINAPI kc_redp2p_pub_adapter_worker(LPVOID arg)
#else
static void *kc_redp2p_pub_adapter_worker(void *arg)
#endif
{
    kc_redp2p_pub_t *pub = (kc_redp2p_pub_t *)arg;
    unsigned char buffer[REDP2P_BUF];
    redp2p_pollfd_t *fds = NULL;

    while (!atomic_load(&pub->adapter_stop)) {
        size_t count = 1;
        size_t i;
        int ready;

        if (!pub->direct || REDP2P_ISERR(pub->adapter_fd)) break;
        if (pub->runtime.ctx->proto == REDP2P_PROTO_TCP) {
            for (i = 0; i < pub->client_count; i++) {
                kc_redp2p_client_t *client = pub->clients[i];
                if (client && !client->udp && !atomic_load(&client->closed) &&
                    !REDP2P_ISERR(client->fd))
                    count++;
            }
        }
        fds = (redp2p_pollfd_t *)realloc(fds, count * sizeof(*fds));
        if (!fds) break;
        memset(fds, 0, count * sizeof(*fds));
        fds[0].fd = pub->adapter_fd;
        fds[0].events = REDP2P_POLLIN;
        count = 1;
        if (pub->runtime.ctx->proto == REDP2P_PROTO_TCP) {
            for (i = 0; i < pub->client_count; i++) {
                kc_redp2p_client_t *client = pub->clients[i];
                if (!client || client->udp || atomic_load(&client->closed) ||
                    REDP2P_ISERR(client->fd))
                    continue;
                fds[count].fd = client->fd;
                fds[count].events = REDP2P_POLLIN;
                count++;
            }
        }
        ready = redp2p_poll_wait(fds, count, 100);
        if (ready <= 0) continue;

        if (redp2p_poll_readable(&fds[0])) {
            if (pub->runtime.ctx->proto == REDP2P_PROTO_TCP) {
                struct sockaddr_storage address;
                socklen_t length = sizeof(address);
                redp2p_fd_t fd = accept(pub->adapter_fd,
                    (struct sockaddr *)&address, &length);
                if (!REDP2P_ISERR(fd))
                    (void)kc_redp2p_pub_add_client(pub, fd, 0, &address,
                        length);
            } else {
                struct sockaddr_storage address;
                socklen_t length = sizeof(address);
                int n = (int)recvfrom(pub->adapter_fd, (char *)buffer,
                    sizeof(buffer), 0, (struct sockaddr *)&address, &length);
                if (n >= 0) {
                    kc_redp2p_client_t *client =
                        kc_redp2p_pub_udp_client(pub, &address, length);
                    if (client)
                        kc_redp2p_pub_emit(pub, client, buffer, (size_t)n);
                }
            }
        }
        if (pub->runtime.ctx->proto == REDP2P_PROTO_TCP) {
            size_t p = 1;
            for (i = 0; i < pub->client_count && p < count; i++) {
                kc_redp2p_client_t *client = pub->clients[i];
                int n;
                if (!client || client->udp || atomic_load(&client->closed) ||
                    REDP2P_ISERR(client->fd))
                    continue;
                if (!redp2p_poll_readable(&fds[p++])) continue;
                n = redp2p_sock_read(client->fd, (char *)buffer,
                    (int)sizeof(buffer));
                if (n <= 0) {
                    kc_redp2p_client_close(client);
                    continue;
                }
                kc_redp2p_pub_emit(pub, client, buffer, (size_t)n);
            }
        }
    }
    free(fds);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/** Runs the direct consumer receive adapter. */
#ifdef _WIN32
static DWORD WINAPI kc_redp2p_con_adapter_worker(LPVOID arg)
#else
static void *kc_redp2p_con_adapter_worker(void *arg)
#endif
{
    kc_redp2p_con_t *con = (kc_redp2p_con_t *)arg;
    unsigned char buffer[REDP2P_BUF];
    redp2p_pollfd_t fd;

    while (!atomic_load(&con->adapter_stop) &&
        !REDP2P_ISERR(con->adapter_fd))
    {
        int ready;
        int n;

        memset(&fd, 0, sizeof(fd));
        fd.fd = con->adapter_fd;
        fd.events = REDP2P_POLLIN;
        ready = redp2p_poll_wait(&fd, 1, 100);
        if (ready <= 0 || !redp2p_poll_readable(&fd)) continue;
        n = redp2p_sock_read(con->adapter_fd, (char *)buffer,
            (int)sizeof(buffer));
        if (n <= 0) break;
        if (con->receive)
            con->receive(buffer, (size_t)n, con->userdata);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/** Starts a platform thread for the publisher data adapter. */
static int kc_redp2p_pub_adapter_thread_start(kc_redp2p_pub_t *pub)
{
#ifdef _WIN32
    pub->adapter_thread = CreateThread(NULL, 0, kc_redp2p_pub_adapter_worker,
        pub, 0, NULL);
    if (!pub->adapter_thread) return KC_REDP2P_ERROR;
#else
    if (pthread_create(&pub->adapter_thread, NULL,
        kc_redp2p_pub_adapter_worker, pub) != 0)
        return KC_REDP2P_ERROR;
#endif
    pub->adapter_thread_started = 1;
    return KC_REDP2P_OK;
}

/** Starts a platform thread for the consumer data adapter. */
static int kc_redp2p_con_adapter_thread_start(kc_redp2p_con_t *con)
{
#ifdef _WIN32
    con->adapter_thread = CreateThread(NULL, 0, kc_redp2p_con_adapter_worker,
        con, 0, NULL);
    if (!con->adapter_thread) return KC_REDP2P_ERROR;
#else
    if (pthread_create(&con->adapter_thread, NULL,
        kc_redp2p_con_adapter_worker, con) != 0)
        return KC_REDP2P_ERROR;
#endif
    con->adapter_thread_started = 1;
    return KC_REDP2P_OK;
}

/** Joins a publisher adapter thread if one was started. */
static void kc_redp2p_pub_adapter_join(kc_redp2p_pub_t *pub)
{
    if (!pub || !pub->adapter_thread_started) return;
#ifdef _WIN32
    WaitForSingleObject(pub->adapter_thread, INFINITE);
    CloseHandle(pub->adapter_thread);
    pub->adapter_thread = NULL;
#else
    pthread_join(pub->adapter_thread, NULL);
#endif
    pub->adapter_thread_started = 0;
}

/** Joins a consumer adapter thread if one was started. */
static void kc_redp2p_con_adapter_join(kc_redp2p_con_t *con)
{
    if (!con || !con->adapter_thread_started) return;
#ifdef _WIN32
    WaitForSingleObject(con->adapter_thread, INFINITE);
    CloseHandle(con->adapter_thread);
    con->adapter_thread = NULL;
#else
    pthread_join(con->adapter_thread, NULL);
#endif
    con->adapter_thread_started = 0;
}

/** Opens the direct consumer's connection to its private local adapter. */
static int kc_redp2p_con_adapter_open(kc_redp2p_con_t *con)
{
    struct sockaddr_in address;
    int type;

    if (!con || !con->runtime.ctx) return KC_REDP2P_EINVAL;
    type = con->runtime.ctx->proto == REDP2P_PROTO_TCP
        ? SOCK_STREAM : SOCK_DGRAM;
    con->adapter_fd = socket(AF_INET, type, 0);
    if (REDP2P_ISERR(con->adapter_fd)) return KC_REDP2P_ENET;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(con->port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    if (connect(con->adapter_fd, (const struct sockaddr *)&address,
        sizeof(address)) != 0)
    {
        REDP2P_FD_CLOSE(con->adapter_fd);
        con->adapter_fd = REDP2P_FD_INVALID;
        return KC_REDP2P_ENET;
    }
    return KC_REDP2P_OK;
}

/** Stops and releases the direct publisher adapter. */
static void kc_redp2p_pub_adapter_close(kc_redp2p_pub_t *pub)
{
    size_t i;

    if (!pub) return;
    atomic_store(&pub->adapter_stop, 1);
    kc_redp2p_pub_adapter_join(pub);
    if (!REDP2P_ISERR(pub->adapter_fd)) {
        REDP2P_FD_CLOSE(pub->adapter_fd);
        pub->adapter_fd = REDP2P_FD_INVALID;
    }
    for (i = 0; i < pub->client_count; i++) {
        kc_redp2p_client_t *client = pub->clients[i];
        if (!client) continue;
        if (!client->udp && !REDP2P_ISERR(client->fd) &&
            !atomic_load(&client->closed))
            REDP2P_FD_CLOSE(client->fd);
        memset(client, 0, sizeof(*client));
        free(client);
    }
    free(pub->clients);
    pub->clients = NULL;
    pub->client_count = 0;
    pub->client_cap = 0;
    if (pub->adapter_platform) {
        redp2p_platform_cleanup();
        pub->adapter_platform = 0;
    }
}

/** Stops and releases the direct consumer adapter. */
static void kc_redp2p_con_adapter_close(kc_redp2p_con_t *con)
{
    if (!con) return;
    atomic_store(&con->adapter_stop, 1);
    kc_redp2p_con_adapter_join(con);
    if (!REDP2P_ISERR(con->adapter_fd)) {
        REDP2P_FD_CLOSE(con->adapter_fd);
        con->adapter_fd = REDP2P_FD_INVALID;
    }
    if (con->adapter_platform) {
        redp2p_platform_cleanup();
        con->adapter_platform = 0;
    }
}

/**
 * Sleeps briefly while waiting for a public runtime to become ready.
 * @return None.
 */
static void kc_redp2p_sleep_tick(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/**
 * Returns a monotonic millisecond timestamp.
 * @return Monotonic milliseconds.
 */
static uint64_t kc_redp2p_now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U +
        (uint64_t)(ts.tv_nsec / 1000000L);
#endif
}

/**
 * Parses one decimal TCP or UDP port.
 * @param text Port text.
 * @param out Destination port.
 * @return 1 on success, 0 on invalid input.
 */
static int kc_redp2p_parse_port(const char *text, uint16_t *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    value = strtoul(text, &end, 10);
    if (*end != '\0' || value == 0 || value > 65535) return 0;
    *out = (uint16_t)value;
    return 1;
}

/**
 * Parses an index endpoint into host and port components.
 * @param text Index endpoint text.
 * @param host Destination host buffer.
 * @param port Destination port.
 * @return 1 on success, 0 on invalid input.
 */
static int kc_redp2p_parse_index(const char *text, char host[256],
    uint16_t *port)
{
    const char *colon;
    size_t len;

    if (!text || !text[0] || !host || !port) return 0;
    *port = KC_REDP2P_PORT_DEFAULT;

    if (text[0] == '[') {
        const char *end = strchr(text + 1, ']');
        if (!end || end == text + 1) return 0;
        len = (size_t)(end - text - 1);
        if (len >= 256) return 0;
        memcpy(host, text + 1, len);
        host[len] = '\0';
        if (end[1] == '\0') return 1;
        if (end[1] != ':' || !kc_redp2p_parse_port(end + 2, port)) return 0;
        return 1;
    }

    colon = strrchr(text, ':');
    if (colon && strchr(text, ':') == colon) {
        len = (size_t)(colon - text);
        if (len == 0 || len >= 256 || !kc_redp2p_parse_port(colon + 1, port))
            return 0;
        memcpy(host, text, len);
        host[len] = '\0';
        return 1;
    }

    len = strlen(text);
    if (len == 0 || len >= 256) return 0;
    memcpy(host, text, len + 1);
    return 1;
}

/**
 * Generates one internal consumer identifier.
 * @param out Destination identifier buffer.
 * @return 1 on success, 0 on random source failure.
 */
static int kc_redp2p_make_self_id(char out[KC_REDP2P_ID_MAX + 1])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char random[8];
    size_t i;

    if (redp2p_fill_random(random, sizeof(random)) != 0) return 0;
    out[0] = 'c';
    for (i = 0; i < sizeof(random); i++) {
        out[1 + i * 2] = hex[random[i] >> 4];
        out[2 + i * 2] = hex[random[i] & 15];
    }
    out[17] = '\0';
    memset(random, 0, sizeof(random));
    return 1;
}

/**
 * Applies stable public runtime defaults independent of environment tuning.
 * @param ctx Runtime context.
 * @return None.
 */
static void kc_redp2p_public_defaults(redp2p_t *ctx)
{
    ctx->sweep = REDP2P_SWEEP_DEFAULT;
    ctx->prune_interval_s = 60;
    ctx->etimeout_sec = 120;
    ctx->heartbeat_s = 15;
    ctx->punch_poll_ms = 500;
    ctx->pending_ttl_s = 30;
    ctx->max_consumers_per_publisher =
        REDP2P_MAX_CONSUMERS_PER_PUBLISHER;
}

/**
 * Converts structured VIP options into the private index representation.
 * @param ctx Index context.
 * @param vips VIP entries.
 * @param count VIP entry count.
 * @return REDP2P_OK on success or an error code.
 */
static int kc_redp2p_apply_vips(redp2p_t *ctx,
    const kc_redp2p_vip_t *vips, size_t count)
{
    char *text;
    size_t size;
    size_t used;
    size_t i;
    char err[256];

    if (count == 0) return REDP2P_OK;
    if (!vips) return REDP2P_EINVAL;

    size = 1;
    for (i = 0; i < count; i++) {
        if (!vips[i].id || !vips[i].pass ||
            !redp2p_is_valid_id(vips[i].id) ||
            !redp2p_is_valid_pass_token(vips[i].pass))
            return REDP2P_EINVAL;
        if (strlen(vips[i].id) > SIZE_MAX - size - strlen(vips[i].pass) - 2)
            return REDP2P_ERROR;
        size += strlen(vips[i].id) + strlen(vips[i].pass) + 2;
    }
    text = (char *)malloc(size);
    if (!text) return REDP2P_ERROR;
    text[0] = '\0';
    used = 0;
    for (i = 0; i < count; i++) {
        int n = snprintf(text + used, size - used, "%s%s %s",
            i ? " " : "", vips[i].id, vips[i].pass);
        if (n < 0 || (size_t)n >= size - used) {
            free(text);
            return REDP2P_ERROR;
        }
        used += (size_t)n;
    }
    err[0] = '\0';
    {
        int status = redp2p_idx_set_vips(ctx, text, err, sizeof(err));
        memset(text, 0, size);
        free(text);
        return status;
    }
}

/**
 * Waits until one worker reports readiness or a bounded failure.
 * @param runtime Public runtime wrapper.
 * @return REDP2P_OK on readiness or an error code.
 */
static int kc_redp2p_runtime_wait_ready(kc_redp2p_runtime_t *runtime)
{
    uint64_t deadline;
    int state;

    deadline = kc_redp2p_now_ms() + KC_REDP2P_READY_TIMEOUT_MS;
    for (;;) {
        state = atomic_load(&runtime->ctx->ready_state);
        if (state != 0) break;
        if (atomic_load(&runtime->done)) break;
        if (kc_redp2p_now_ms() >= deadline) return REDP2P_ETIMEOUT;
        kc_redp2p_sleep_tick();
    }
    if (state > 0) return REDP2P_OK;
    if (state < 0) return atomic_load(&runtime->ctx->ready_status);
    return runtime->result == REDP2P_OK ? REDP2P_ERROR : runtime->result;
}

/**
 * Joins one started public runtime worker.
 * @param runtime Public runtime wrapper.
 * @return None.
 */
static void kc_redp2p_runtime_join(kc_redp2p_runtime_t *runtime)
{
    if (!runtime || !runtime->thread_started) return;
#ifdef _WIN32
    WaitForSingleObject(runtime->thread, INFINITE);
    CloseHandle(runtime->thread);
    runtime->thread = NULL;
#else
    pthread_join(runtime->thread, NULL);
#endif
    runtime->thread_started = 0;
}

/**
 * Stops, joins, and destroys one public runtime.
 * @param runtime Public runtime wrapper.
 * @return None.
 */
static void kc_redp2p_runtime_close(kc_redp2p_runtime_t *runtime)
{
    if (!runtime) return;
    if (runtime->ctx) redp2p_context_request_stop(runtime->ctx);
    kc_redp2p_runtime_join(runtime);
    if (runtime->ctx) {
        redp2p_context_destroy(runtime->ctx);
        runtime->ctx = NULL;
    }
}

#ifdef _WIN32
/**
 * Runs one public index worker thread.
 * @param arg Index handle.
 * @return Platform thread result.
 */
static DWORD WINAPI kc_redp2p_idx_worker(LPVOID arg)
#else
/**
 * Runs one public index worker thread.
 * @param arg Index handle.
 * @return NULL after the worker exits.
 */
static void *kc_redp2p_idx_worker(void *arg)
#endif
{
    kc_redp2p_idx_t *idx = (kc_redp2p_idx_t *)arg;
    idx->runtime.result = redp2p_idx_run(idx->runtime.ctx,
        idx->host[0] ? idx->host : NULL, idx->port);
    atomic_store(&idx->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
/**
 * Runs one public publisher worker thread.
 * @param arg Publisher handle.
 * @return Platform thread result.
 */
static DWORD WINAPI kc_redp2p_pub_worker(LPVOID arg)
#else
/**
 * Runs one public publisher worker thread.
 * @param arg Publisher handle.
 * @return NULL after the worker exits.
 */
static void *kc_redp2p_pub_worker(void *arg)
#endif
{
    kc_redp2p_pub_t *pub = (kc_redp2p_pub_t *)arg;
    pub->runtime.result = redp2p_pub_run(pub->runtime.ctx, pub->index_host,
        pub->index_port, pub->id, pub->port);
    atomic_store(&pub->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
/**
 * Runs one public consumer worker thread.
 * @param arg Consumer handle.
 * @return Platform thread result.
 */
static DWORD WINAPI kc_redp2p_con_worker(LPVOID arg)
#else
/**
 * Runs one public consumer worker thread.
 * @param arg Consumer handle.
 * @return NULL after the worker exits.
 */
static void *kc_redp2p_con_worker(void *arg)
#endif
{
    kc_redp2p_con_t *con = (kc_redp2p_con_t *)arg;
    con->runtime.result = redp2p_con_run(con->runtime.ctx, con->index_host,
        con->index_port, con->self_id, con->id, con->port);
    atomic_store(&con->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Starts the index worker thread.
 * @param idx Index handle.
 * @return REDP2P_OK on success or an error code.
 */
static int kc_redp2p_thread_start_idx(kc_redp2p_idx_t *idx)
{
#ifdef _WIN32
    idx->runtime.thread = CreateThread(NULL, 0, kc_redp2p_idx_worker, idx, 0,
        NULL);
    if (!idx->runtime.thread) return REDP2P_ERROR;
#else
    if (pthread_create(&idx->runtime.thread, NULL, kc_redp2p_idx_worker, idx)
        != 0) return REDP2P_ERROR;
#endif
    idx->runtime.thread_started = 1;
    return REDP2P_OK;
}

/**
 * Starts the publisher worker thread.
 * @param pub Publisher handle.
 * @return REDP2P_OK on success or an error code.
 */
static int kc_redp2p_thread_start_pub(kc_redp2p_pub_t *pub)
{
#ifdef _WIN32
    pub->runtime.thread = CreateThread(NULL, 0, kc_redp2p_pub_worker, pub, 0,
        NULL);
    if (!pub->runtime.thread) return REDP2P_ERROR;
#else
    if (pthread_create(&pub->runtime.thread, NULL, kc_redp2p_pub_worker, pub)
        != 0) return REDP2P_ERROR;
#endif
    pub->runtime.thread_started = 1;
    return REDP2P_OK;
}

/**
 * Starts the consumer worker thread.
 * @param con Consumer handle.
 * @return REDP2P_OK on success or an error code.
 */
static int kc_redp2p_thread_start_con(kc_redp2p_con_t *con)
{
#ifdef _WIN32
    con->runtime.thread = CreateThread(NULL, 0, kc_redp2p_con_worker, con, 0,
        NULL);
    if (!con->runtime.thread) return REDP2P_ERROR;
#else
    if (pthread_create(&con->runtime.thread, NULL, kc_redp2p_con_worker, con)
        != 0) return REDP2P_ERROR;
#endif
    con->runtime.thread_started = 1;
    return REDP2P_OK;
}

/**
 * Starts an index runtime from public options.
 * @param out Destination index handle.
 * @param options Optional index options.
 * @return KC_REDP2P_OK on success or a public status code.
 */
int kc_redp2p_idx(kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options)
{
    kc_redp2p_idx_t *idx;
    uint16_t port;
    int status;

    if (!out || (options && options->pow > REDP2P_POW_MAX))
        return KC_REDP2P_EINVAL;
    *out = NULL;
    idx = (kc_redp2p_idx_t *)calloc(1, sizeof(*idx));
    if (!idx) return KC_REDP2P_ERROR;

    if (options && options->host) {
        if (!options->host[0] || strlen(options->host) >= sizeof(idx->host)) {
            free(idx);
            return KC_REDP2P_EINVAL;
        }
        memcpy(idx->host, options->host, strlen(options->host) + 1);
    }
    port = options && options->port ? options->port : KC_REDP2P_PORT_DEFAULT;
    idx->port = port;

    status = redp2p_context_create(&idx->runtime.ctx);
    if (status != REDP2P_OK) {
        free(idx);
        return status;
    }
    kc_redp2p_public_defaults(idx->runtime.ctx);
    if (options) {
        if (options->seats) {
            status = redp2p_idx_set_capacity(idx->runtime.ctx, *options->seats);
            if (status != REDP2P_OK) goto fail;
        }
        status = redp2p_idx_set_pow(idx->runtime.ctx, (int)options->pow);
        if (status != REDP2P_OK) goto fail;
        if (options->pass) {
            status = redp2p_set_registration_pass(idx->runtime.ctx, options->pass);
            if (status != REDP2P_OK) goto fail;
        }
        status = kc_redp2p_apply_vips(idx->runtime.ctx, options->vips,
            options->vip_count);
        if (status != REDP2P_OK) goto fail;
        status = redp2p_idx_set_max_consumers(idx->runtime.ctx,
            options->max_consumers);
        if (status != REDP2P_OK) goto fail;
    }

    atomic_store(&idx->runtime.ctx->ready_state, 0);
    atomic_store(&idx->runtime.ctx->ready_status, REDP2P_ERROR);
    status = kc_redp2p_thread_start_idx(idx);
    if (status != REDP2P_OK) goto fail;
    status = kc_redp2p_runtime_wait_ready(&idx->runtime);
    if (status != REDP2P_OK) {
        kc_redp2p_runtime_close(&idx->runtime);
        free(idx);
        return status;
    }
    *out = idx;
    return KC_REDP2P_OK;

fail:
    redp2p_context_destroy(idx->runtime.ctx);
    free(idx);
    return status;
}

/**
 * Starts publication of one local service.
 * @param out Destination publisher handle.
 * @param options Publisher options.
 * @return KC_REDP2P_OK on success or a public status code.
 */
int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options)
{
    kc_redp2p_pub_t *pub;
    int status;

    if (!out || !options || !options->id || !options->index ||
        !redp2p_is_valid_id(options->id) ||
        (options->protocol != KC_REDP2P_TCP &&
        options->protocol != KC_REDP2P_UDP))
        return KC_REDP2P_EINVAL;
    *out = NULL;
    pub = (kc_redp2p_pub_t *)calloc(1, sizeof(*pub));
    if (!pub) return KC_REDP2P_ERROR;
    pub->adapter_fd = REDP2P_FD_INVALID;
    pub->direct = options->port == 0;
    pub->receive = options->receive;
    pub->userdata = options->userdata;
    if (pub->direct && !pub->receive) {
        free(pub);
        return KC_REDP2P_EINVAL;
    }
    if (!pub->direct && pub->receive) {
        free(pub);
        return KC_REDP2P_EINVAL;
    }
    if (!kc_redp2p_parse_index(options->index, pub->index_host,
        &pub->index_port)) {
        free(pub);
        return KC_REDP2P_EINVAL;
    }
    memcpy(pub->id, options->id, strlen(options->id) + 1);

    status = redp2p_context_create(&pub->runtime.ctx);
    if (status != REDP2P_OK) goto fail_no_ctx;
    kc_redp2p_public_defaults(pub->runtime.ctx);
    status = redp2p_pub_set_protocol(pub->runtime.ctx, options->protocol);
    if (status != REDP2P_OK) goto fail;

    pub->port = options->port;
    if (pub->direct) {
        int type = options->protocol == KC_REDP2P_TCP
            ? SOCK_STREAM : SOCK_DGRAM;

        if (redp2p_platform_init() != 0) {
            status = KC_REDP2P_ENET;
            goto fail;
        }
        pub->adapter_platform = 1;
        pub->adapter_fd = kc_redp2p_loopback_socket(type,
            options->protocol == KC_REDP2P_TCP, 0, &pub->port);
        if (REDP2P_ISERR(pub->adapter_fd) || pub->port == 0) {
            status = KC_REDP2P_ENET;
            goto fail_adapter;
        }
    }
    status = redp2p_set_local_port(pub->runtime.ctx, pub->port);
    if (status != REDP2P_OK) goto fail_adapter;
    if (options->pass) {
        status = redp2p_set_registration_pass(pub->runtime.ctx, options->pass);
        if (status != REDP2P_OK) goto fail_adapter;
    }
    if (options->stun) {
        status = redp2p_set_stun_server(pub->runtime.ctx, options->stun);
        if (status != REDP2P_OK) goto fail_adapter;
    }

    atomic_store(&pub->runtime.ctx->ready_state, 0);
    atomic_store(&pub->runtime.ctx->ready_status, REDP2P_ERROR);
    status = kc_redp2p_thread_start_pub(pub);
    if (status != REDP2P_OK) goto fail_adapter;
    status = kc_redp2p_runtime_wait_ready(&pub->runtime);
    if (status != REDP2P_OK) {
        kc_redp2p_runtime_close(&pub->runtime);
        pub->runtime.ctx = NULL;
        goto fail_adapter_no_ctx;
    }
    if (pub->direct) {
        status = kc_redp2p_pub_adapter_thread_start(pub);
        if (status != KC_REDP2P_OK) {
            kc_redp2p_runtime_close(&pub->runtime);
            pub->runtime.ctx = NULL;
            goto fail_adapter_no_ctx;
        }
    }
    *out = pub;
    return KC_REDP2P_OK;

fail_adapter:
    kc_redp2p_pub_adapter_close(pub);
fail:
    if (pub->runtime.ctx) {
        redp2p_context_destroy(pub->runtime.ctx);
        pub->runtime.ctx = NULL;
    }
fail_adapter_no_ctx:
    kc_redp2p_pub_adapter_close(pub);
fail_no_ctx:
    free(pub);
    return status;
}

/**
 * Starts one local consumer tunnel.
 * @param out Destination consumer handle.
 * @param options Consumer options.
 * @return KC_REDP2P_OK on success or a public status code.
 */
int kc_redp2p_con(kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options)
{
    kc_redp2p_con_t *con;
    int status;

    if (!out || !options || !options->id || !options->index ||
        !redp2p_is_valid_id(options->id))
        return KC_REDP2P_EINVAL;
    *out = NULL;
    con = (kc_redp2p_con_t *)calloc(1, sizeof(*con));
    if (!con) return KC_REDP2P_ERROR;
    con->adapter_fd = REDP2P_FD_INVALID;
    con->direct = options->port == 0;
    con->receive = options->receive;
    con->userdata = options->userdata;
    if (!con->direct && con->receive) {
        free(con);
        return KC_REDP2P_EINVAL;
    }
    if (!kc_redp2p_parse_index(options->index, con->index_host,
        &con->index_port) || !kc_redp2p_make_self_id(con->self_id)) {
        free(con);
        return KC_REDP2P_EINVAL;
    }
    memcpy(con->id, options->id, strlen(options->id) + 1);
    con->port = options->port;

    if (con->direct) {
        if (redp2p_platform_init() != 0) {
            free(con);
            return KC_REDP2P_ENET;
        }
        con->adapter_platform = 1;
        status = kc_redp2p_ephemeral_port(&con->port);
        if (status != KC_REDP2P_OK) goto fail_no_ctx;
    }

    status = redp2p_context_create(&con->runtime.ctx);
    if (status != REDP2P_OK) goto fail_no_ctx;
    kc_redp2p_public_defaults(con->runtime.ctx);
    status = redp2p_set_local_port(con->runtime.ctx, con->port);
    if (status != REDP2P_OK) goto fail;
    if (options->stun) {
        status = redp2p_set_stun_server(con->runtime.ctx, options->stun);
        if (status != REDP2P_OK) goto fail;
    }

    atomic_store(&con->runtime.ctx->ready_state, 0);
    atomic_store(&con->runtime.ctx->ready_status, REDP2P_ERROR);
    status = kc_redp2p_thread_start_con(con);
    if (status != REDP2P_OK) goto fail;
    status = kc_redp2p_runtime_wait_ready(&con->runtime);
    if (status != REDP2P_OK) {
        kc_redp2p_runtime_close(&con->runtime);
        con->runtime.ctx = NULL;
        goto fail_no_ctx;
    }
    if (con->direct) {
        status = kc_redp2p_con_adapter_open(con);
        if (status != KC_REDP2P_OK) {
            kc_redp2p_runtime_close(&con->runtime);
            con->runtime.ctx = NULL;
            goto fail_no_ctx;
        }
        if (con->receive) {
            status = kc_redp2p_con_adapter_thread_start(con);
            if (status != KC_REDP2P_OK) {
                kc_redp2p_con_adapter_close(con);
                kc_redp2p_runtime_close(&con->runtime);
                con->runtime.ctx = NULL;
                goto fail_no_ctx;
            }
        }
    }
    *out = con;
    return KC_REDP2P_OK;

fail:
    if (con->runtime.ctx) {
        redp2p_context_destroy(con->runtime.ctx);
        con->runtime.ctx = NULL;
    }
fail_no_ctx:
    kc_redp2p_con_adapter_close(con);
    free(con);
    return status;
}

/**
 * Returns a snapshot of fresh publisher identifiers from one index runtime.
 * @param idx Index handle.
 * @param out_entries Destination allocated entry array.
 * @param out_count Destination entry count.
 * @return KC_REDP2P_OK on success or a public status code.
 */
int kc_redp2p_idx_list(kc_redp2p_idx_t *idx,
    kc_redp2p_idx_entry_t **out_entries, size_t *out_count)
{
    kc_redp2p_idx_entry_t *entries;
    redp2p_t *ctx;
    uint64_t now;
    size_t count;
    size_t i;
    size_t out_i;

    if (!idx || !out_entries || !out_count) return KC_REDP2P_EINVAL;
    *out_entries = NULL;
    *out_count = 0;
    ctx = idx->runtime.ctx;
    if (!ctx) return KC_REDP2P_EINVAL;

    redp2p_lock(ctx);
    now = redp2p_now_s();
    count = 0;
    for (i = 0; i < ctx->n_peers; i++) {
        if (now < ctx->peers[i].peer.last_seen ||
            now - ctx->peers[i].peer.last_seen <= ctx->etimeout_sec)
            count++;
    }
    if (count == 0) {
        redp2p_unlock(ctx);
        return KC_REDP2P_OK;
    }
    if (count > SIZE_MAX / sizeof(*entries)) {
        redp2p_unlock(ctx);
        return KC_REDP2P_ERROR;
    }
    entries = (kc_redp2p_idx_entry_t *)calloc(count, sizeof(*entries));
    if (!entries) {
        redp2p_unlock(ctx);
        return KC_REDP2P_ERROR;
    }
    out_i = 0;
    for (i = 0; i < ctx->n_peers; i++) {
        if (now >= ctx->peers[i].peer.last_seen &&
            now - ctx->peers[i].peer.last_seen > ctx->etimeout_sec)
            continue;
        memcpy(entries[out_i].id, ctx->peers[i].peer.id,
            strlen(ctx->peers[i].peer.id) + 1);
        out_i++;
    }
    redp2p_unlock(ctx);
    *out_entries = entries;
    *out_count = out_i;
    return KC_REDP2P_OK;
}

/**
 * Stops and releases one index runtime.
 * @param idx Index handle.
 * @return None.
 */
void kc_redp2p_idx_close(kc_redp2p_idx_t *idx)
{
    if (!idx) return;
    kc_redp2p_runtime_close(&idx->runtime);
    free(idx);
}

/**
 * Stops and releases one publisher runtime.
 * @param pub Publisher handle.
 * @return None.
 */
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub)
{
    if (!pub) return;
    kc_redp2p_pub_adapter_close(pub);
    kc_redp2p_runtime_close(&pub->runtime);
    free(pub);
}

/**
 * Stops and releases one consumer runtime.
 * @param con Consumer handle.
 * @return None.
 */
void kc_redp2p_con_close(kc_redp2p_con_t *con)
{
    if (!con) return;
    kc_redp2p_con_adapter_close(con);
    kc_redp2p_runtime_close(&con->runtime);
    free(con);
}

/**
 * Writes an arbitrary TCP byte sequence in bounded native chunks.
 * @return KC_REDP2P_OK on success or KC_REDP2P_ENET.
 */
static int kc_redp2p_send_stream(redp2p_fd_t fd, const void *data, size_t size)
{
    const unsigned char *cursor = (const unsigned char *)data;

    while (size > 0) {
        int chunk = size > (size_t)INT_MAX ? INT_MAX : (int)size;
        if (redp2p_write_all(fd, (const char *)cursor, chunk) != 0)
            return KC_REDP2P_ENET;
        cursor += (size_t)chunk;
        size -= (size_t)chunk;
    }
    return KC_REDP2P_OK;
}

/**
 * Sends data through a direct consumer capability.
 * @param con Direct consumer capability.
 * @param data Application bytes.
 * @param size Byte count.
 * @return KC_REDP2P_OK on success or a negative status.
 */
int kc_redp2p_con_send(kc_redp2p_con_t *con, const void *data, size_t size)
{
    int sent;

    if (!con || !con->direct || REDP2P_ISERR(con->adapter_fd) ||
        (!data && size > 0))
        return KC_REDP2P_EINVAL;
    if (size == 0) return KC_REDP2P_OK;
    if (con->runtime.ctx &&
        con->runtime.ctx->proto == REDP2P_PROTO_TCP)
        return kc_redp2p_send_stream(con->adapter_fd, data, size);
    if (size > REDP2P_UDP_PAYLOAD_MAX) return KC_REDP2P_EINVAL;
    sent = (int)send(con->adapter_fd, (const char *)data, (int)size, 0);
    return sent == (int)size ? KC_REDP2P_OK : KC_REDP2P_ENET;
}

/**
 * Responds to one publisher client.
 * @param client Stable publisher-side client identity.
 * @param data Application bytes.
 * @param size Byte count.
 * @return KC_REDP2P_OK on success or a negative status.
 */
int kc_redp2p_client_respond(kc_redp2p_client_t *client,
    const void *data, size_t size)
{
    int sent;

    if (!client || !client->pub || atomic_load(&client->closed) ||
        (!data && size > 0))
        return KC_REDP2P_EINVAL;
    if (size == 0) return KC_REDP2P_OK;
    if (!client->udp)
        return kc_redp2p_send_stream(client->fd, data, size);
    if (size > REDP2P_UDP_PAYLOAD_MAX) return KC_REDP2P_EINVAL;
    sent = (int)sendto(client->pub->adapter_fd, (const char *)data,
        (int)size, 0, (const struct sockaddr *)&client->address,
        client->address_len);
    return sent == (int)size ? KC_REDP2P_OK : KC_REDP2P_ENET;
}

/**
 * Closes one publisher-side client identity.
 * @param client Client to close.
 * @return None.
 */
void kc_redp2p_client_close(kc_redp2p_client_t *client)
{
    if (!client || atomic_exchange(&client->closed, 1)) return;
    if (!client->udp && !REDP2P_ISERR(client->fd)) {
        REDP2P_FD_CLOSE(client->fd);
        client->fd = REDP2P_FD_INVALID;
    }
}

/**
 * Releases memory returned by the public API.
 * @param ptr Allocated memory.
 * @return None.
 */
void kc_redp2p_free(void *ptr)
{
    free(ptr);
}

/**
 * Maps one public status code to a stable description.
 * @param status Public status code.
 * @return Stable static description.
 */
const char *kc_redp2p_strerror(int status)
{
    switch (status) {
        case KC_REDP2P_OK:       return "OK";
        case KC_REDP2P_ERROR:    return "general error";
        case KC_REDP2P_ENET:     return "network error";
        case KC_REDP2P_ENOENT:   return "publisher not found";
        case KC_REDP2P_ETIMEOUT: return "timeout";
        case KC_REDP2P_EFULL:    return "index capacity reached";
        case KC_REDP2P_EINVAL:   return "invalid argument";
        case KC_REDP2P_EPROTO:   return "protocol error";
        case KC_REDP2P_EAUTH:    return "authentication failed";
        case KC_REDP2P_EVERSION: return "unsupported protocol version";
        case KC_REDP2P_EPUNCH:   return "direct connectivity failed";
        case KC_REDP2P_EEXIST:   return "publisher already registered";
        case KC_REDP2P_EUNSUPPORTED: return "unsupported publisher transport";
        default:                 return "unknown error";
    }
}

/**
 * Returns the build version of the public library.
 * @return Build version value.
 */
uint64_t kc_redp2p_version(void)
{
    return redp2p_version();
}
