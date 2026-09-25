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

#ifndef _WIN32
#include <time.h>
#endif

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

struct kc_redp2p_pub {
    kc_redp2p_runtime_t runtime;
    char index_host[256];
    uint16_t index_port;
    char id[KC_REDP2P_ID_MAX + 1];
    uint16_t port;
};

struct kc_redp2p_con {
    kc_redp2p_runtime_t runtime;
    char index_host[256];
    uint16_t index_port;
    char id[KC_REDP2P_ID_MAX + 1];
    char self_id[KC_REDP2P_ID_MAX + 1];
    uint16_t port;
};

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

static void kc_redp2p_public_defaults(redp2p_t *ctx)
{
    ctx->prune_interval_s = 60;
    ctx->etimeout_sec = 120;
    ctx->heartbeat_s = 15;
    ctx->punch_poll_ms = 500;
    ctx->pending_ttl_s = 30;
    ctx->max_consumers_per_publisher =
        REDP2P_MAX_CONSUMERS_PER_PUBLISHER;
}

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
        int status = redp2p_set_vip(ctx, text, err, sizeof(err));
        memset(text, 0, size);
        free(text);
        return status;
    }
}

static int kc_redp2p_runtime_wait_ready(kc_redp2p_runtime_t *runtime)
{
    int state;

    for (;;) {
        state = atomic_load(&runtime->ctx->ready_state);
        if (state != 0) break;
        if (atomic_load(&runtime->done)) break;
        kc_redp2p_sleep_tick();
    }
    if (state > 0) return REDP2P_OK;
    if (state < 0) return atomic_load(&runtime->ctx->ready_status);
    return runtime->result == REDP2P_OK ? REDP2P_ERROR : runtime->result;
}

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

static void kc_redp2p_runtime_close(kc_redp2p_runtime_t *runtime)
{
    if (!runtime) return;
    if (runtime->ctx) redp2p_stop(runtime->ctx);
    kc_redp2p_runtime_join(runtime);
    if (runtime->ctx) {
        redp2p_close(runtime->ctx);
        runtime->ctx = NULL;
    }
}

#ifdef _WIN32
static DWORD WINAPI kc_redp2p_idx_worker(LPVOID arg)
#else
static void *kc_redp2p_idx_worker(void *arg)
#endif
{
    kc_redp2p_idx_t *idx = (kc_redp2p_idx_t *)arg;
    idx->runtime.result = redp2p_serve_index(idx->runtime.ctx, idx->host,
        idx->port);
    atomic_store(&idx->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI kc_redp2p_pub_worker(LPVOID arg)
#else
static void *kc_redp2p_pub_worker(void *arg)
#endif
{
    kc_redp2p_pub_t *pub = (kc_redp2p_pub_t *)arg;
    pub->runtime.result = redp2p_wait(pub->runtime.ctx, pub->index_host,
        pub->index_port, pub->id, pub->port);
    atomic_store(&pub->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI kc_redp2p_con_worker(LPVOID arg)
#else
static void *kc_redp2p_con_worker(void *arg)
#endif
{
    kc_redp2p_con_t *con = (kc_redp2p_con_t *)arg;
    con->runtime.result = redp2p_connect(con->runtime.ctx, con->index_host,
        con->index_port, con->self_id, con->id, con->port);
    atomic_store(&con->runtime.done, 1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

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

int kc_redp2p_idx(kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options)
{
    kc_redp2p_idx_t *idx;
    uint16_t port;
    int status;

    if (!out) return KC_REDP2P_EINVAL;
    *out = NULL;
    idx = (kc_redp2p_idx_t *)calloc(1, sizeof(*idx));
    if (!idx) return KC_REDP2P_ERROR;

    if (options && options->host) {
        if (!options->host[0] || strlen(options->host) >= sizeof(idx->host)) {
            free(idx);
            return KC_REDP2P_EINVAL;
        }
        memcpy(idx->host, options->host, strlen(options->host) + 1);
    } else {
        memcpy(idx->host, "0.0.0.0", 8);
    }
    port = options && options->port ? options->port : KC_REDP2P_PORT_DEFAULT;
    idx->port = port;

    status = redp2p_open(&idx->runtime.ctx);
    if (status != REDP2P_OK) {
        free(idx);
        return status;
    }
    if (options) {
        if (options->seats) {
            status = redp2p_set_seats(idx->runtime.ctx, *options->seats);
            if (status != REDP2P_OK) goto fail;
        }
        status = redp2p_set_pow(idx->runtime.ctx, (int)options->pow);
        if (status != REDP2P_OK) goto fail;
        if (options->pass) {
            status = redp2p_set_pass(idx->runtime.ctx, options->pass);
            if (status != REDP2P_OK) goto fail;
        }
        status = kc_redp2p_apply_vips(idx->runtime.ctx, options->vips,
            options->vip_count);
        if (status != REDP2P_OK) goto fail;
        status = redp2p_set_max_consumers_per_publisher(idx->runtime.ctx,
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
    redp2p_close(idx->runtime.ctx);
    free(idx);
    return status;
}

int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options)
{
    kc_redp2p_pub_t *pub;
    int status;

    if (!out || !options || !options->id || !options->index ||
        !redp2p_is_valid_id(options->id) || options->port == 0 ||
        (options->protocol != KC_REDP2P_TCP &&
        options->protocol != KC_REDP2P_UDP))
        return KC_REDP2P_EINVAL;
    *out = NULL;
    pub = (kc_redp2p_pub_t *)calloc(1, sizeof(*pub));
    if (!pub) return KC_REDP2P_ERROR;
    if (!kc_redp2p_parse_index(options->index, pub->index_host,
        &pub->index_port)) {
        free(pub);
        return KC_REDP2P_EINVAL;
    }
    memcpy(pub->id, options->id, strlen(options->id) + 1);
    pub->port = options->port;

    status = redp2p_open(&pub->runtime.ctx);
    if (status != REDP2P_OK) goto fail_no_ctx;
    status = redp2p_set_protocol(pub->runtime.ctx, options->protocol);
    if (status != REDP2P_OK) goto fail;
    status = redp2p_set_port(pub->runtime.ctx, options->port);
    if (status != REDP2P_OK) goto fail;
    if (options->pass) {
        status = redp2p_set_pass(pub->runtime.ctx, options->pass);
        if (status != REDP2P_OK) goto fail;
    }
    if (options->stun) {
        status = redp2p_set_stun_url(pub->runtime.ctx, options->stun);
        if (status != REDP2P_OK) goto fail;
    }

    atomic_store(&pub->runtime.ctx->ready_state, 0);
    atomic_store(&pub->runtime.ctx->ready_status, REDP2P_ERROR);
    status = kc_redp2p_thread_start_pub(pub);
    if (status != REDP2P_OK) goto fail;
    status = kc_redp2p_runtime_wait_ready(&pub->runtime);
    if (status != REDP2P_OK) {
        kc_redp2p_runtime_close(&pub->runtime);
        free(pub);
        return status;
    }
    *out = pub;
    return KC_REDP2P_OK;

fail:
    redp2p_close(pub->runtime.ctx);
fail_no_ctx:
    free(pub);
    return status;
}

int kc_redp2p_con(kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options)
{
    kc_redp2p_con_t *con;
    int status;

    if (!out || !options || !options->id || !options->index ||
        !redp2p_is_valid_id(options->id) || options->port == 0)
        return KC_REDP2P_EINVAL;
    *out = NULL;
    con = (kc_redp2p_con_t *)calloc(1, sizeof(*con));
    if (!con) return KC_REDP2P_ERROR;
    if (!kc_redp2p_parse_index(options->index, con->index_host,
        &con->index_port) || !kc_redp2p_make_self_id(con->self_id)) {
        free(con);
        return KC_REDP2P_EINVAL;
    }
    memcpy(con->id, options->id, strlen(options->id) + 1);
    con->port = options->port;

    status = redp2p_open(&con->runtime.ctx);
    if (status != REDP2P_OK) goto fail_no_ctx;
    status = redp2p_set_port(con->runtime.ctx, options->port);
    if (status != REDP2P_OK) goto fail;
    if (options->stun) {
        status = redp2p_set_stun_url(con->runtime.ctx, options->stun);
        if (status != REDP2P_OK) goto fail;
    }

    atomic_store(&con->runtime.ctx->ready_state, 0);
    atomic_store(&con->runtime.ctx->ready_status, REDP2P_ERROR);
    status = kc_redp2p_thread_start_con(con);
    if (status != REDP2P_OK) goto fail;
    status = kc_redp2p_runtime_wait_ready(&con->runtime);
    if (status != REDP2P_OK) {
        kc_redp2p_runtime_close(&con->runtime);
        free(con);
        return status;
    }
    *out = con;
    return KC_REDP2P_OK;

fail:
    redp2p_close(con->runtime.ctx);
fail_no_ctx:
    free(con);
    return status;
}

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

void kc_redp2p_idx_close(kc_redp2p_idx_t *idx)
{
    if (!idx) return;
    kc_redp2p_runtime_close(&idx->runtime);
    free(idx);
}

void kc_redp2p_pub_close(kc_redp2p_pub_t *pub)
{
    if (!pub) return;
    kc_redp2p_runtime_close(&pub->runtime);
    free(pub);
}

void kc_redp2p_con_close(kc_redp2p_con_t *con)
{
    if (!con) return;
    kc_redp2p_runtime_close(&con->runtime);
    free(con);
}

void kc_redp2p_free(void *ptr)
{
    free(ptr);
}

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
        default:                 return "unknown error";
    }
}

uint64_t kc_redp2p_version(void)
{
    return redp2p_version();
}
