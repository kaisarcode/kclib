/**
 * libredp2p-idx.c - REDP2P.
 * Summary: Temporary index state and HTTP coordination server.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p-core.h"

#include "monocypher.h"
#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

#define REDP2P_MAX_CONNECTIONS      128
#define REDP2P_RATE_SOURCES_MAX 4096U
#define REDP2P_RATE_SOURCE_IDLE_MS 120000U
#define REDP2P_RATE_SOURCE_CAP 20000U
#define REDP2P_RATE_TARGET_CAP 40000U

static const unsigned char REDP2P_CHALLENGE_DOMAIN[] = "REDP2P-CHALLENGE";

_Static_assert(sizeof(REDP2P_CHALLENGE_DOMAIN) - 1 == 16,
    "Challenge domain must not include a NUL byte");

/**
 * Stores one normalized source IP and its last rate-limit activity.
 */
struct redp2p_rate_source {
    unsigned char address[16];
    int family;
    redp2p_rate_bucket_t bucket;
};

typedef struct {
    redp2p_t *ctx;
    redp2p_fd_t listener_fd;
    fd_set readable_fds;
    int max_fd;
    int platform_initialized;
    uint64_t last_prune;
} redp2p_index_runtime_t;

/**
 * Compares fixed-size byte strings without data-dependent early exit.
 * @param a First byte string.
 * @param b Second byte string.
 * @param len Byte count.
 * @return 1 when equal, 0 otherwise.
 */
static int redp2p_constant_time_equal(const unsigned char *a,
    const unsigned char *b, size_t len)
{
    unsigned char diff;
    size_t i;

    if (!a || !b) return 0;
    diff = 0;
    for (i = 0; i < len; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

/**
 * Validates the recovered fixed-width publisher control secret.
 * @param secret Recovered 16-byte control secret.
 * @return 1 when every byte is ASCII hexadecimal, 0 otherwise.
 */
static int redp2p_index_control_secret_valid(
const unsigned char secret[REDP2P_KEY_SZ])
{
    size_t i;

    if (!secret) return 0;
    for (i = 0; i < REDP2P_KEY_SZ; i++) {
        if (redp2p_hex_decode_nibble((char)secret[i]) < 0) return 0;
    }
    return 1;
}

/**
 * Builds the authenticated registration challenge message.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param out Output buffer.
 * @param out_len Output byte count.
 * @return 1 on success, 0 on overflow.
 */
static int redp2p_challenge_mac_input(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, unsigned char *out,
    size_t *out_len)
{
    size_t used;

    if (!nonce || !out || !out_len) return 0;
    used = 0;
    if (!redp2p_append_bytes(out, 64, &used,
        REDP2P_CHALLENGE_DOMAIN, sizeof(REDP2P_CHALLENGE_DOMAIN) - 1) ||
        !redp2p_append_bytes(out, 64, &used, nonce, 32) ||
        !redp2p_append_u64_be(out, 64, &used, issued_at) ||
        !redp2p_append_u64_be(out, 64, &used, expires_at)) return 0;
    *out_len = used;
    return 1;
}

/**
 * Verifies one register proof-of-work challenge.
 * @param nonce      Raw challenge nonce.
 * @param issued_at  Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id         Service identifier.
 * @param solution   Candidate uint64 solution.
 * @param bits       Difficulty target.
 * @return 1 on success, 0 on failure.
 */
static int redp2p_verify_register_pow(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    uint64_t solution, int bits)
{
    unsigned char hash[32];

    if (bits < 0 || bits > 32 || !redp2p_hash_register_pow(nonce, issued_at,
        expires_at, id, solution, hash)) return 0;
    bits = redp2p_count_leading_zero_bits(hash) >= bits;
    crypto_wipe(hash, sizeof(hash));
    return bits;
}

/**
 * Returns the host part of one stored socket address as canonical text.
 * @param addr Stored socket address.
 * @param out  Output buffer.
 * @param size Output buffer size.
 * @return 1 on success, 0 for an unsupported or invalid address.
 */
static int redp2p_sockaddr_host(const struct sockaddr_storage *addr, char *out,
    size_t size)
{
    if (!addr || !out || size == 0) return 0;
    if (addr->ss_family == AF_INET)
        return inet_ntop(AF_INET,
            &((const struct sockaddr_in *)addr)->sin_addr, out,
            (socklen_t)size) != NULL;
    if (addr->ss_family == AF_INET6)
        return inet_ntop(AF_INET6,
            &((const struct sockaddr_in6 *)addr)->sin6_addr, out,
            (socklen_t)size) != NULL;
    return 0;
}

/**
 * Writes one HTTP/1.1 response and closes semantics for the caller.
 * @param fd           Socket to write.
 * @param status       HTTP status code.
 * @param reason       Status reason phrase.
 * @param content_type Response Content-Type.
 * @param body         Response body.
 * @return REDP2P_OK on success, or a negative error code.
 */
static int redp2p_http_write_response(redp2p_fd_t fd, int status,
    const char *reason, const char *content_type, const char *body)
{
    char head[REDP2P_HTTP_LINE_MAX * 6];
    int n;

    if (!reason || !content_type || !body) return REDP2P_EINVAL;
    n = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, content_type, (int)strlen(body));
    if (n < 0 || (size_t)n >= sizeof(head)) return REDP2P_ERROR;
    if (redp2p_write_all(fd, head, n) != 0) return REDP2P_ENET;
    if (redp2p_write_all(fd, body, (int)strlen(body)) != 0) return REDP2P_ENET;
    return REDP2P_OK;
}

/**
 * Returns whether the byte is ASCII whitespace.
 * @param ch Input byte.
 * @return 1 when whitespace, 0 otherwise.
 */
static int redp2p_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
        ch == '\f' || ch == '\v';
}

/**
 * Trims leading and trailing ASCII whitespace in place.
 * @param text Mutable string buffer.
 * @return Pointer to the first non-whitespace byte.
 */
static char *redp2p_trim(char *text) {
    char *end;

    if (!text) return NULL;
    while (*text && redp2p_is_space(*text)) text++;
    end = text + strlen(text);
    while (end > text && redp2p_is_space(end[-1])) end--;
    *end = '\0';
    return text;
}

/**
 * Finds one VIP seat by identifier.
 * @param ctx Open context.
 * @param id Reserved seat identifier.
 * @return VIP index on success, SIZE_MAX when missing.
 */
static size_t redp2p_find_vip(redp2p_t *ctx, const char *id) {
    size_t i;

    if (!ctx || !id) return SIZE_MAX;
    for (i = 0; i < ctx->n_vips; i++) {
        if (strcmp(ctx->vips[i].id, id) == 0) return i;
    }
    return SIZE_MAX;
}

/**
 * Recomputes non-VIP capacity after seats or VIP reservations change.
 * @param ctx Open context.
 * @return None.
 */
static void redp2p_update_nonvip_cap(redp2p_t *ctx) {
    if (!ctx) return;
    if (ctx->n_vips >= ctx->n_peers_cap) {
        ctx->nonvip_cap = 0;
    } else {
        ctx->nonvip_cap = ctx->n_peers_cap - ctx->n_vips;
    }
}

/**
 * Ensures peer storage can hold the requested number of online peers.
 * @param ctx  Open context.
 * @param need Required peer slots.
 * @return REDP2P_OK on success, REDP2P_ERROR on allocation failure.
 */
static int redp2p_ensure_peer_storage(redp2p_t *ctx, size_t need) {
    redp2p_index_peer_t *peers;
    size_t cap;
    size_t limit;

    if (!ctx) return REDP2P_ERROR;
    limit = SIZE_MAX / sizeof(*ctx->peers);
    if (ctx->seats_set) limit = ctx->n_peers_cap;
    if (need > limit) return REDP2P_EFULL;
    if (need <= ctx->peers_alloc) return REDP2P_OK;
    cap = ctx->peers_alloc > 0 ? ctx->peers_alloc : 8;
    if (cap > limit) cap = limit;
    while (cap < need) {
        if (cap > limit / 2) {
            cap = limit;
        } else {
            cap *= 2;
        }
    }
    if (cap > SIZE_MAX / sizeof(*ctx->peers)) return REDP2P_ERROR;
    peers = (redp2p_index_peer_t *)realloc(ctx->peers, cap * sizeof(*ctx->peers));
    if (!peers) return REDP2P_ERROR;
    ctx->peers = peers;
    ctx->peers_alloc = cap;
    return REDP2P_OK;
}

/**
 * Counts online peers that do not have a reserved VIP seat.
 * @param ctx Open context.
 * @return Number of online non-VIP peers.
 */
static size_t redp2p_count_nonvip_peers(redp2p_t *ctx) {
    size_t i;
    size_t count;

    if (!ctx) return 0;
    count = 0;
    for (i = 0; i < ctx->n_peers; i++) {
        if (redp2p_find_vip(ctx, ctx->peers[i].peer.id) == SIZE_MAX) count++;
    }
    return count;
}

/**
 * Adds one VIP seat definition to the context.
 * @param ctx Open context.
 * @param id Reserved seat identifier.
 * @param pass Reserved seat password.
 * @param err Output error buffer.
 * @param err_cap Output error buffer capacity.
 * @return REDP2P_OK on success, REDP2P_ERROR on validation failure.
 */
static int redp2p_add_vip(redp2p_t *ctx, const char *id, const char *pass,
    char *err, size_t err_cap)
{
    redp2p_vip_entry_t *vips;
    size_t cap;

    if (!ctx || !id || !pass) return REDP2P_ERROR;
    if (!redp2p_is_valid_id(id)) {
        if (err && err_cap > 0)
            snprintf(err, err_cap, "REDP2P_VIP invalid id '%s'", id);
        return REDP2P_ERROR;
    }
    if (!redp2p_is_valid_pass_token(pass)) {
        if (err && err_cap > 0)
            snprintf(err, err_cap, "REDP2P_VIP invalid password for id '%s'", id);
        return REDP2P_ERROR;
    }
    if (redp2p_find_vip(ctx, id) != SIZE_MAX) {
        if (err && err_cap > 0)
            snprintf(err, err_cap, "REDP2P_VIP redefines reserved id '%s'", id);
        return REDP2P_ERROR;
    }
    if (ctx->n_vips >= ctx->vips_cap) {
        if (ctx->vips_cap > SIZE_MAX / 2) {
            if (err && err_cap > 0)
                snprintf(err, err_cap, "REDP2P_VIP table capacity overflow");
            return REDP2P_ERROR;
        }
        cap = ctx->vips_cap > 0 ? ctx->vips_cap * 2 : 8;
        if (cap > SIZE_MAX / sizeof(*ctx->vips)) {
            if (err && err_cap > 0)
                snprintf(err, err_cap, "REDP2P_VIP table allocation overflow");
            return REDP2P_ERROR;
        }
        vips = (redp2p_vip_entry_t *)realloc(ctx->vips,
            cap * sizeof(*ctx->vips));
        if (!vips) {
            if (err && err_cap > 0)
                snprintf(err, err_cap, "failed to allocate REDP2P_VIP table");
            return REDP2P_ERROR;
        }
        ctx->vips = vips;
        ctx->vips_cap = cap;
    }
    strncpy(ctx->vips[ctx->n_vips].id, id, REDP2P_ID_MAX);
    ctx->vips[ctx->n_vips].id[REDP2P_ID_MAX] = '\0';
    strncpy(ctx->vips[ctx->n_vips].pass, pass, REDP2P_PASS_MAX);
    ctx->vips[ctx->n_vips].pass[REDP2P_PASS_MAX] = '\0';
    ctx->n_vips++;
    return REDP2P_OK;
}

/**
 * Set seats.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_seats(redp2p_t *ctx, size_t seats) {
    if (!ctx) return REDP2P_EINVAL;
    if (seats > SIZE_MAX / sizeof(*ctx->peers)) {
        redp2p_set_error(ctx, "seats exceeds the platform allocation range");
        return REDP2P_EINVAL;
    }
    ctx->n_peers_cap = seats;
    ctx->seats_set = 1;
    redp2p_update_nonvip_cap(ctx);
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Sets the per-publisher consumer safety window.
 * This is a safety bound against pathological punch_req accumulation, not a
 * normal traffic-shaping limit.
 * @param ctx Open context.
 * @param n   Per-publisher consumer window; 0 restores the default of 32.
 * @return REDP2P_OK on success, REDP2P_EINVAL on error.
 */
int redp2p_set_max_consumers_per_publisher(redp2p_t *ctx, size_t n) {
    if (!ctx) return REDP2P_EINVAL;
    ctx->max_consumers_per_publisher = n == 0
        ? REDP2P_MAX_PENDING_CALLS_PER_PUBLISHER : n;
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Set pow.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_pow(redp2p_t *ctx, int bits) {
    if (!ctx) return REDP2P_EINVAL;
    if (bits < 0 || bits > REDP2P_POW_MAX) {
        redp2p_set_error(ctx, "pow must be between 0 and %d", REDP2P_POW_MAX);
        return REDP2P_EINVAL;
    }
    ctx->pow_bits = bits;
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Parses the VIP seat map from one whitespace-separated string.
 * @param ctx Open context.
 * @param vip Whitespace-separated id/pass pairs.
 * @param err Output error buffer.
 * @param err_cap Output error buffer capacity.
 * @return REDP2P_OK on success, REDP2P_ERROR on parse failure.
 */
int redp2p_set_vip(
redp2p_t *ctx,
const char *vip,
char *err,
size_t err_cap
)
{
    char *copy;
    char *cursor;

    if (!ctx) return REDP2P_ERROR;
    if (ctx->vips)
        crypto_wipe(ctx->vips, ctx->vips_cap * sizeof(*ctx->vips));
    free(ctx->vips);
    ctx->vips = NULL;
    ctx->n_vips = 0;
    ctx->vips_cap = 0;
    redp2p_update_nonvip_cap(ctx);
    if (!vip || !vip[0]) return REDP2P_OK;
    copy = (char *)malloc(strlen(vip) + 1);
    if (!copy) {
        if (err && err_cap > 0)
            snprintf(err, err_cap, "failed to allocate REDP2P_VIP buffer");
        return REDP2P_ERROR;
    }
    strcpy(copy, vip);
    cursor = redp2p_trim(copy);
    while (cursor && *cursor) {
        char *id;
        char *pass;

        id = cursor;
        while (*cursor && !redp2p_is_space(*cursor)) cursor++;
        if (*cursor) *cursor++ = '\0';
        while (*cursor && redp2p_is_space(*cursor)) cursor++;
        if (!*cursor) {
            if (err && err_cap > 0)
                snprintf(err, err_cap, "REDP2P_VIP has odd token count");
            crypto_wipe(copy, strlen(vip) + 1);
            free(copy);
            if (ctx->vips)
                crypto_wipe(ctx->vips,
                    ctx->vips_cap * sizeof(*ctx->vips));
            free(ctx->vips);
            ctx->vips = NULL;
            ctx->n_vips = 0;
            ctx->vips_cap = 0;
            redp2p_update_nonvip_cap(ctx);
            return REDP2P_ERROR;
        }
        pass = cursor;
        while (*cursor && !redp2p_is_space(*cursor)) cursor++;
        if (*cursor) *cursor++ = '\0';
        while (*cursor && redp2p_is_space(*cursor)) cursor++;
        if (redp2p_add_vip(ctx, id, pass, err, err_cap) != REDP2P_OK) {
            crypto_wipe(copy, strlen(vip) + 1);
            free(copy);
            if (ctx->vips)
                crypto_wipe(ctx->vips,
                    ctx->vips_cap * sizeof(*ctx->vips));
            free(ctx->vips);
            ctx->vips = NULL;
            ctx->n_vips = 0;
            ctx->vips_cap = 0;
            redp2p_update_nonvip_cap(ctx);
            return REDP2P_ERROR;
        }
    }
    crypto_wipe(copy, strlen(vip) + 1);
    free(copy);
    redp2p_update_nonvip_cap(ctx);
    return REDP2P_OK;
}

/**
 * Find peer.
 * @return Peer index on success, SIZE_MAX when missing.
 */
static size_t redp2p_find_peer(redp2p_t *ctx, const char *id) {
    size_t i;

    for (i = 0; i < ctx->n_peers; i++) {
        if (strcmp(ctx->peers[i].peer.id, id) == 0)
            return i;
    }
    return SIZE_MAX;
}

/**
 * Evict stale.
 * @return Status code.
 */
static void redp2p_evict_stale(redp2p_t *ctx) {
    uint64_t now;
    size_t i;

    now = redp2p_now_s();
    i = ctx->n_peers;
    while (i > 0) {
        i--;
        if (now - ctx->peers[i].peer.last_seen > ctx->etimeout_sec) {
            if (i < ctx->n_peers - 1) {
                memmove(&ctx->peers[i], &ctx->peers[i + 1],
                    (ctx->n_peers - i - 1) * sizeof(ctx->peers[0]));
            }
            ctx->n_peers--;
        }
    }
}

/**
 * Reports whether one peer record is expired by TTL.
 * @param ctx   Open index context.
 * @param index Peer index.
 * @return 1 when expired or out of range, 0 when fresh.
 */
static int redp2p_peer_is_stale(redp2p_t *ctx, size_t index) {
    uint64_t now;

    if (index >= ctx->n_peers) return 1;
    now = redp2p_now_s();
    return now - ctx->peers[index].peer.last_seen > ctx->etimeout_sec;
}

/**
 * Add peer.
 * @return 0 on success, -1 on error.
 */
static int redp2p_add_peer(redp2p_t *ctx, const char *id, const char *key)
{
    size_t id_len;
    size_t idx;
    int is_vip;

    idx = redp2p_find_peer(ctx, id);
    if (idx != SIZE_MAX) return REDP2P_EEXIST;

    is_vip = redp2p_find_vip(ctx, id) != SIZE_MAX;
    if (ctx->seats_set && ctx->n_peers >= ctx->n_peers_cap)
        return REDP2P_EFULL;
    if (ctx->n_peers >= SIZE_MAX / sizeof(*ctx->peers))
        return REDP2P_ERROR;
    if (ctx->seats_set && !is_vip &&
        redp2p_count_nonvip_peers(ctx) >= ctx->nonvip_cap)
        return REDP2P_EFULL;
    if (redp2p_ensure_peer_storage(ctx, ctx->n_peers + 1) != REDP2P_OK)
        return REDP2P_ERROR;

    id_len = strlen(id);
    if (id_len > REDP2P_ID_MAX)
        id_len = REDP2P_ID_MAX;
    memcpy(ctx->peers[ctx->n_peers].peer.id, id, id_len);
    ctx->peers[ctx->n_peers].peer.id[id_len] = '\0';

    ctx->peers[ctx->n_peers].peer.last_seen = redp2p_now_s();
    memcpy(ctx->peers[ctx->n_peers].peer.key, key, REDP2P_KEY_SZ + 1);
    ctx->peers[ctx->n_peers].peer.sequence = 0;
    ctx->peers[ctx->n_peers].peer.proto = 0;
    ctx->peers[ctx->n_peers].peer.udp_port = 0;
    ctx->peers[ctx->n_peers].peer.n_candidates = 0;
    ctx->peers[ctx->n_peers].punch_bucket.credit = REDP2P_RATE_TARGET_CAP;
    ctx->peers[ctx->n_peers].punch_bucket.updated_ms = redp2p_now_ms();
    ctx->n_peers++;
    return REDP2P_OK;
}

/**
 * Remove peer.
 * @return 0 on success, -1 on error.
 */
static int redp2p_remove_peer(redp2p_t *ctx, const char *id) {
    size_t idx;

    idx = redp2p_find_peer(ctx, id);
    if (idx == SIZE_MAX) return REDP2P_ENOENT;
    if (idx < ctx->n_peers - 1) {
        memmove(&ctx->peers[idx], &ctx->peers[idx + 1],
            (ctx->n_peers - idx - 1) * sizeof(ctx->peers[0]));
    }
    ctx->n_peers--;
    return REDP2P_OK;
}

/**
 * Removes one pending call by index.
 * @param ctx Open index context.
 * @param idx Pending call index.
 * @return None.
 */
static void redp2p_pending_call_remove(redp2p_t *ctx, int idx) {
    if (idx < 0 || idx >= ctx->n_pending_calls) return;
    ctx->pending_calls[idx] = ctx->pending_calls[--ctx->n_pending_calls];
    crypto_wipe(&ctx->pending_calls[ctx->n_pending_calls],
        sizeof(ctx->pending_calls[ctx->n_pending_calls]));
}

/**
 * Copies a bounded string with NUL termination.
 * @param dst    Destination buffer.
 * @param dst_cap Destination capacity.
 * @param src    Source string.
 * @return None.
 */
static void redp2p_bounded_copy(char *dst, size_t dst_cap, const char *src) {
    size_t len;

    if (!dst || dst_cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len >= dst_cap) len = dst_cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/**
 * Evicts expired pending calls.
 * @param ctx Open index context.
 * @return None.
 */
static void redp2p_pending_call_evict_stale(redp2p_t *ctx) {
    uint64_t now;
    int i;

    now = redp2p_now_s();
    for (i = 0; i < ctx->n_pending_calls; ) {
        if (now - ctx->pending_calls[i].ts > ctx->pending_ttl_s) {
            redp2p_pending_call_remove(ctx, i);
        } else {
            i++;
        }
    }
}

/**
 * Adds one pending call, growing the store on demand within the global limit.
 * @param ctx          Open index context.
 * @param caller_id    Requesting consumer identifier.
 * @param target_id    Target publisher identifier.
 * @param sess_id      Session identifier.
 * @param candidates   Caller candidate array.
 * @param n_candidates Caller candidate count.
 * @return 1 on success, 0 when the global limit is reached or growth fails.
 */
static int redp2p_pending_call_add(redp2p_t *ctx, const char *caller_id,
    const char *target_id, const char *sess_id,
    const redp2p_candidate_t *candidates, int n_candidates)
{
    int idx;

    if (ctx->n_pending_calls >= REDP2P_MAX_PENDING_CALLS_GLOBAL) return 0;
    if (ctx->n_pending_calls >= (int)ctx->pending_calls_cap) {
        size_t new_cap;
        redp2p_pending_call_t *grown;

        if (ctx->pending_calls_cap == 0) {
            new_cap = 16U;
        } else {
            if (ctx->pending_calls_cap > SIZE_MAX / 2U) return 0;
            new_cap = ctx->pending_calls_cap * 2U;
        }
        if (new_cap > REDP2P_MAX_PENDING_CALLS_GLOBAL)
            new_cap = REDP2P_MAX_PENDING_CALLS_GLOBAL;
        if (new_cap < ctx->pending_calls_cap ||
            new_cap > SIZE_MAX / sizeof(ctx->pending_calls[0]))
            return 0;
        grown = (redp2p_pending_call_t *)realloc(ctx->pending_calls,
            new_cap * sizeof(ctx->pending_calls[0]));
        if (!grown) return 0;
        ctx->pending_calls = grown;
        ctx->pending_calls_cap = new_cap;
    }
    idx = ctx->n_pending_calls++;
    redp2p_bounded_copy(ctx->pending_calls[idx].caller_id,
        sizeof(ctx->pending_calls[idx].caller_id), caller_id);
    redp2p_bounded_copy(ctx->pending_calls[idx].target_id,
        sizeof(ctx->pending_calls[idx].target_id), target_id);
    redp2p_bounded_copy(ctx->pending_calls[idx].sess_id,
        sizeof(ctx->pending_calls[idx].sess_id), sess_id);
    ctx->pending_calls[idx].n_candidates = n_candidates;
    if (n_candidates > 0) {
        memcpy(ctx->pending_calls[idx].candidates, candidates,
            (size_t)n_candidates * sizeof(candidates[0]));
    }
    ctx->pending_calls[idx].ts = redp2p_now_s();
    return 1;
}

/**
 * Counts pending calls addressed to one target identifier.
 * @param ctx       Open index context.
 * @param target_id Target publisher identifier.
 * @return Number of non-expired pending calls for that target.
 */
static int redp2p_pending_call_count_for_target(redp2p_t *ctx,
    const char *target_id)
{
    uint64_t now;
    int count;
    int i;

    now = redp2p_now_s();
    count = 0;
    for (i = 0; i < ctx->n_pending_calls; i++) {
        if (now - ctx->pending_calls[i].ts <= ctx->pending_ttl_s &&
            strcmp(ctx->pending_calls[i].target_id, target_id) == 0)
        {
            count++;
        }
    }
    return count;
}

/**
 * Copies up to REDP2P_PUNCH_POLL_MAX pending calls for one publisher identifier
 * without consuming them. Calls beyond the protocol maximum remain pending.
 * @param ctx       Open index context.
 * @param target_id Target publisher identifier.
 * @param out       Output pending call array.
 * @param out_n     Output pending call count.
 * @return None.
 */
static void redp2p_pending_call_collect(redp2p_t *ctx,
    const char *target_id, redp2p_pending_call_t *out, int *out_n)
{
    int i;

    *out_n = 0;
    for (i = 0; i < ctx->n_pending_calls; i++) {
        if (strcmp(ctx->pending_calls[i].target_id, target_id) == 0) {
            if (*out_n < REDP2P_PUNCH_POLL_MAX) {
                out[*out_n] = ctx->pending_calls[i];
                (*out_n)++;
            }
        }
    }
}

/**
 * Consumes the first count pending calls for one publisher identifier,
 * matching the call order redp2p_pending_call_collect returns.
 * Removal happens from the highest recorded index downward because
 * redp2p_pending_call_remove swaps in the array tail; descending removal keeps
 * the lower recorded indices valid.
 * @param ctx       Open index context.
 * @param target_id Target publisher identifier.
 * @param count     Number of leading matching calls to remove.
 * @return None.
 */
static void redp2p_pending_call_remove_first_n(redp2p_t *ctx,
    const char *target_id, int count)
{
    int idx[REDP2P_PUNCH_POLL_MAX];
    int found;
    int i;

    found = 0;
    for (i = 0; i < ctx->n_pending_calls && found < count; i++) {
        if (strcmp(ctx->pending_calls[i].target_id, target_id) == 0)
            idx[found++] = i;
    }
    for (i = found - 1; i >= 0; i--)
        redp2p_pending_call_remove(ctx, idx[i]);
}

/**
 * Appends one in-flight index connection to the bounded connection table.
 * @param ctx      Locked index context.
 * @param fd       Socket whose descriptor ownership transfers on success.
 * @param peer     Trusted source address, may be NULL for unknown sources.
 * @param peer_len Length of the source address in bytes.
 * @return REDP2P_OK on success, or REDP2P_EFULL when the table is full.
 */
static int redp2p_index_conn_add(redp2p_t *ctx, redp2p_fd_t fd,
    const struct sockaddr_storage *peer, socklen_t peer_len) {
    redp2p_index_conn_t *new_conns;
    int new_cap;

    if (ctx->n_conns >= REDP2P_MAX_CONNECTIONS) return REDP2P_EFULL;
    if (ctx->n_conns >= ctx->conns_cap) {
        new_cap = ctx->conns_cap == 0 ? 16 : ctx->conns_cap * 2;
        if (new_cap > REDP2P_MAX_CONNECTIONS) new_cap = REDP2P_MAX_CONNECTIONS;
        new_conns = (redp2p_index_conn_t *)realloc(ctx->conns,
            (size_t)new_cap * sizeof(*ctx->conns));
        if (!new_conns) return REDP2P_ERROR;
        ctx->conns = new_conns;
        ctx->conns_cap = new_cap;
    }
    memset(&ctx->conns[ctx->n_conns], 0, sizeof(ctx->conns[ctx->n_conns]));
    ctx->conns[ctx->n_conns].fd = fd;
    ctx->conns[ctx->n_conns].ts = redp2p_now_ms();
    if (peer && (peer->ss_family == AF_INET || peer->ss_family == AF_INET6)) {
        ctx->conns[ctx->n_conns].peer_addr = *peer;
        ctx->conns[ctx->n_conns].peer_addr_len = peer_len;
    }
    ctx->n_conns++;
    return REDP2P_OK;
}

/**
 * Closes and removes one in-flight index connection by index.
 * @param ctx   Locked index context.
 * @param index Connection index to remove.
 * @return None.
 */
static void redp2p_index_conn_remove(redp2p_t *ctx, int index) {
    if (index < 0 || index >= ctx->n_conns) return;
    REDP2P_FD_CLOSE(ctx->conns[index].fd);
    ctx->conns[index] = ctx->conns[--ctx->n_conns];
}

/**
 * Sends one JSON index response and releases the value.
 * @param fd     Request socket.
 * @param status HTTP status code.
 * @param reason Status reason phrase.
 * @param value  JSON response value, consumed.
 * @return REDP2P_OK on success, or a negative error code.
 */
static int redp2p_index_respond(redp2p_fd_t fd, int status,
    const char *reason, JSON_Value *value)
{
    char buf[REDP2P_BUF];
    size_t size;
    int result;

    if (!value) return REDP2P_ERROR;
    size = json_serialization_size(value);
    if (size == 0 || size >= sizeof(buf)) {
        json_value_free(value);
        return REDP2P_EFULL;
    }
    if (json_serialize_to_buffer(value, buf, sizeof(buf)) != JSONSuccess) {
        json_value_free(value);
        return REDP2P_ERROR;
    }
    json_value_free(value);
    result = redp2p_http_write_response(fd, status, reason,
        "application/json", buf);
    crypto_wipe(buf, sizeof(buf));
    return result;
}

/**
 * Sends one JSON index error reply.
 * @param fd     Request socket.
 * @param status HTTP status code.
 * @param code   JSON error code.
 * @return None.
 */
static void redp2p_index_respond_error(redp2p_fd_t fd, int status,
    const char *code)
{
    const char *reason;
    JSON_Value *value;
    JSON_Object *obj;

    switch (status) {
        case 400: reason = "Bad Request"; break;
        case 403: reason = "Forbidden"; break;
        case 404: reason = "Not Found"; break;
        case 429: reason = "Too Many Requests"; break;
        case 500: reason = "Internal Server Error"; break;
        case 503: reason = "Service Unavailable"; break;
        default: reason = "Error"; break;
    }
    value = json_value_init_object();
    if (!value) return;
    obj = json_value_get_object(value);
    json_object_set_boolean(obj, "ok", 0);
    json_object_set_string(obj, "error", code);
    redp2p_index_respond(fd, status, reason, value);
}

/**
 * Rejects one JSON object containing duplicate field names.
 * @param obj Request object.
 * @return 1 when every field name is unique, 0 on a duplicate.
 */
static int redp2p_index_json_unique_fields(const JSON_Object *obj) {
    size_t count;
    size_t i;
    size_t j;

    count = json_object_get_count(obj);
    for (i = 0; i < count; i++) {
        const char *name;

        name = json_object_get_name(obj, i);
        for (j = i + 1; j < count; j++) {
            if (strcmp(name, json_object_get_name(obj, j)) == 0) return 0;
        }
    }
    return 1;
}

/**
 * Extracts and validates one bounded identifier field.
 * @param obj    Request object.
 * @param field  Field name.
 * @param id     Output identifier.
 * @param id_cap Output identifier capacity.
 * @return 1 on success, 0 when missing or of the wrong type, -1 when
 *         present but invalid.
 */
static int redp2p_index_require_id(JSON_Object *obj, const char *field,
    char *id, size_t id_cap)
{
    const char *value;

    if (!json_object_has_value_of_type(obj, field, JSONString)) return 0;
    value = json_object_get_string(obj, field);
    if (!value || strlen(value) >= id_cap || !redp2p_is_valid_id(value)) return -1;
    memcpy(id, value, strlen(value) + 1);
    return 1;
}

/**
 * Requires one valid identifier or replies with the matching error code.
 * @param req    Request object.
 * @param fd     Request socket.
 * @param id     Output identifier.
 * @param id_cap Output identifier capacity.
 * @return 1 on success, 0 after replying.
 */
static int redp2p_index_require_id_response(JSON_Object *req,
    redp2p_fd_t fd, char *id, size_t id_cap)
{
    int result;

    result = redp2p_index_require_id(req, "id", id, id_cap);
    if (result <= 0) {
        redp2p_index_respond_error(fd, 400,
            result == 0 ? "bad_request" : "invalid_id");
        return 0;
    }
    return 1;
}

/**
 * Extracts one positive protocol sequence representable as a JSON number.
 * @param obj Request object.
 * @param sequence Output control sequence.
 * @return 1 on success, 0 on malformed input.
 */
static int redp2p_index_require_sequence(JSON_Object *obj,
    uint64_t *sequence)
{
    double value;

    if (!obj || !sequence || !json_object_has_value_of_type(obj, "seq",
        JSONNumber))
        return 0;
    value = json_object_get_number(obj, "seq");
    if (value < 1.0 || value > 9007199254740991.0 ||
        value != (double)(uint64_t)value)
        return 0;
    *sequence = (uint64_t)value;
    return 1;
}

/**
 * Parses a server request candidate list under the index destination policy.
 *
 * Requests may only submit host candidates naming reachable unicast
 * endpoints; the list is canonicalized, de-duplicated, and sorted using the
 * same ordering the client already applies, so authenticated proofs computed
 * over the normalized list match between both sides.
 *
 * @param obj       Request JSON object.
 * @param field     Candidate array field name.
 * @param out       Output candidate array.
 * @param out_count Output candidate count.
 * @return 1 on success, 0 when the list is malformed or fails policy.
 */
static int redp2p_index_parse_request_candidates(JSON_Object *obj,
    const char *field, redp2p_candidate_t *out, int *out_count)
{
    struct in_addr ipv4;
    struct in6_addr ipv6;
    int i;

    if (!redp2p_parse_candidates(obj, field, out, out_count)) return 0;
    for (i = 0; i < *out_count; i++) {
        if (out[i].type != REDP2P_CAND_HOST) return 0;
        if (inet_pton(AF_INET, out[i].addr, &ipv4) == 1) {
            if (!redp2p_candidate_dest_allowed(AF_INET, &ipv4, out[i].port))
                return 0;
            inet_ntop(AF_INET, &ipv4, out[i].addr, sizeof(out[i].addr));
        } else if (inet_pton(AF_INET6, out[i].addr, &ipv6) == 1) {
            if (!redp2p_candidate_dest_allowed(AF_INET6, &ipv6, out[i].port))
                return 0;
            inet_ntop(AF_INET6, &ipv6, out[i].addr, sizeof(out[i].addr));
        } else {
            return 0;
        }
    }
    if (!redp2p_normalize_candidates(out, out_count)) return 0;
    if (*out_count > REDP2P_PEER_CANDIDATES_MAX) return 0;
    return 1;
}

/**
 * Handles one challenge request, issuing a stateless proof challenge.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_challenge(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    unsigned char nonce[32];
    unsigned char mac[32];
    unsigned char input[64];
    unsigned char index_skey[32];
    unsigned char index_pkey[32];
    char nonce_hex[65];
    char mac_hex[65];
    char pkey_hex[65];
    uint64_t issued_at;
    uint64_t expires_at;
    size_t input_len;
    JSON_Value *reply;
    JSON_Object *out;

    memset(nonce, 0, sizeof(nonce));
    memset(mac, 0, sizeof(mac));
    memset(input, 0, sizeof(input));
    memset(index_skey, 0, sizeof(index_skey));
    memset(index_pkey, 0, sizeof(index_pkey));
    memset(nonce_hex, 0, sizeof(nonce_hex));
    memset(mac_hex, 0, sizeof(mac_hex));
    memset(pkey_hex, 0, sizeof(pkey_hex));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) return;
    issued_at = (uint64_t)time(NULL);
    expires_at = issued_at + 60;
    if (redp2p_fill_random(nonce, sizeof(nonce)) != 0 ||
        !redp2p_hex_encode(nonce, sizeof(nonce), nonce_hex,
            sizeof(nonce_hex)) || !redp2p_challenge_mac_input(nonce,
            issued_at, expires_at, input, &input_len))
    {
        crypto_wipe(nonce, sizeof(nonce));
        crypto_wipe(mac, sizeof(mac));
        crypto_wipe(input, sizeof(input));
        crypto_wipe(index_skey, sizeof(index_skey));
        crypto_wipe(nonce_hex, sizeof(nonce_hex));
        crypto_wipe(mac_hex, sizeof(mac_hex));
        crypto_wipe(pkey_hex, sizeof(pkey_hex));
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    redp2p_hmac_sha256_bytes(ctx->challenge_key, sizeof(ctx->challenge_key),
        input, input_len, mac);
    if (!redp2p_registration_index_key(ctx->challenge_key, nonce, issued_at,
        expires_at, index_skey, index_pkey) || !redp2p_hex_encode(mac,
        sizeof(mac), mac_hex, sizeof(mac_hex)) || !redp2p_hex_encode(index_pkey,
        sizeof(index_pkey), pkey_hex, sizeof(pkey_hex))) {
        crypto_wipe(nonce, sizeof(nonce));
        crypto_wipe(mac, sizeof(mac));
        crypto_wipe(input, sizeof(input));
        crypto_wipe(index_skey, sizeof(index_skey));
        crypto_wipe(nonce_hex, sizeof(nonce_hex));
        crypto_wipe(mac_hex, sizeof(mac_hex));
        crypto_wipe(pkey_hex, sizeof(pkey_hex));
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    reply = json_value_init_object();
    if (!reply) {
        crypto_wipe(nonce, sizeof(nonce));
        crypto_wipe(mac, sizeof(mac));
        crypto_wipe(input, sizeof(input));
        crypto_wipe(index_skey, sizeof(index_skey));
        crypto_wipe(nonce_hex, sizeof(nonce_hex));
        crypto_wipe(mac_hex, sizeof(mac_hex));
        crypto_wipe(pkey_hex, sizeof(pkey_hex));
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    out = json_value_get_object(reply);
    json_object_set_boolean(out, "ok", 1);
    json_object_set_string(out, "nonce", nonce_hex);
    json_object_set_number(out, "issued_at", (double)issued_at);
    json_object_set_number(out, "expires_at", (double)expires_at);
    json_object_set_string(out, "mac", mac_hex);
    json_object_set_string(out, "pkey", pkey_hex);
    json_object_set_number(out, "bits", (double)ctx->pow_bits);
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(mac, sizeof(mac));
    crypto_wipe(input, sizeof(input));
    crypto_wipe(index_skey, sizeof(index_skey));
    crypto_wipe(nonce_hex, sizeof(nonce_hex));
    crypto_wipe(mac_hex, sizeof(mac_hex));
    crypto_wipe(pkey_hex, sizeof(pkey_hex));
    redp2p_index_respond(fd, 200, "OK", reply);
}

/**
 * Selects the admission password without exposing the reservation class.
 * @param ctx Locked index context.
 * @param id Publisher identifier.
 * @return Borrowed VIP password for a reserved ID, otherwise global password.
 */
static const char *redp2p_index_password(redp2p_t *ctx, const char *id) {
    size_t vip;

    vip = redp2p_find_vip(ctx, id);
    return vip == SIZE_MAX ? ctx->pass : ctx->vips[vip].pass;
}

/**
 * Handles one register request, verifying proof of work and upserting.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_register(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    char nonce_hex[65];
    char mac_hex[65];
    char solution_hex[17];
    char proof[65];
    char pkey_hex[65];
    char encrypted_secret_hex[65];
    char access_proof[65];
    char expected_access[65];
    const char *password;
    char key[REDP2P_KEY_STR_SZ];
    unsigned char nonce[32];
    unsigned char mac[32];
    unsigned char received_mac[32];
    unsigned char received_proof[32];
    unsigned char expected_proof[32];
    unsigned char solution_raw[8];
    unsigned char publisher_pkey[32];
    unsigned char encrypted_secret[32];
    unsigned char input[384];
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    double proto;
    unsigned short udp_port;
    uint64_t issued_at;
    uint64_t expires_at;
    uint64_t solution;
    uint64_t now;
    size_t input_len;
    int n_candidates;
    int add_result;

    memset(nonce_hex, 0, sizeof(nonce_hex));
    memset(mac_hex, 0, sizeof(mac_hex));
    memset(solution_hex, 0, sizeof(solution_hex));
    memset(proof, 0, sizeof(proof));
    memset(pkey_hex, 0, sizeof(pkey_hex));
    memset(encrypted_secret_hex, 0, sizeof(encrypted_secret_hex));
    memset(access_proof, 0, sizeof(access_proof));
    memset(expected_access, 0, sizeof(expected_access));
    memset(key, 0, sizeof(key));
    memset(nonce, 0, sizeof(nonce));
    memset(mac, 0, sizeof(mac));
    memset(received_mac, 0, sizeof(received_mac));
    memset(received_proof, 0, sizeof(received_proof));
    memset(expected_proof, 0, sizeof(expected_proof));
    memset(solution_raw, 0, sizeof(solution_raw));
    memset(publisher_pkey, 0, sizeof(publisher_pkey));
    memset(encrypted_secret, 0, sizeof(encrypted_secret));
    memset(input, 0, sizeof(input));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) {
        goto cleanup;
    }
    if (!redp2p_json_require_hex(req, "nonce", nonce_hex, sizeof(nonce_hex),
            64) || !redp2p_json_require_hex(req, "mac", mac_hex,
            sizeof(mac_hex), 64) || !redp2p_json_require_hex(req,
            "pow_solution", solution_hex, sizeof(solution_hex), 16) ||
        !redp2p_json_require_hex(req, "proof", proof, sizeof(proof), 64) ||
        !redp2p_json_require_hex(req, "pkey", pkey_hex, sizeof(pkey_hex),
            64) || !redp2p_json_require_hex(req, "secret",
            encrypted_secret_hex, sizeof(encrypted_secret_hex), 64))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    if (json_object_has_value(req, "access_proof") &&
        !redp2p_json_require_lower_hex(req, "access_proof", access_proof,
            sizeof(access_proof), 64))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    if (!json_object_has_value_of_type(req, "proto", JSONNumber) ||
        !json_object_has_value_of_type(req, "udp_port", JSONNumber) ||
        !redp2p_json_require_u64(req, "issued_at", &issued_at) ||
        !redp2p_json_require_u64(req, "expires_at", &expires_at) ||
        !redp2p_hex_decode(nonce_hex, nonce, sizeof(nonce)) ||
        !redp2p_hex_decode(mac_hex, received_mac, sizeof(received_mac)) ||
        !redp2p_hex_decode(proof, received_proof, sizeof(received_proof)) ||
        !redp2p_hex_decode(solution_hex, solution_raw, sizeof(solution_raw)))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    solution = ((uint64_t)solution_raw[0] << 56) |
        ((uint64_t)solution_raw[1] << 48) |
        ((uint64_t)solution_raw[2] << 40) |
        ((uint64_t)solution_raw[3] << 32) |
        ((uint64_t)solution_raw[4] << 24) |
        ((uint64_t)solution_raw[5] << 16) |
        ((uint64_t)solution_raw[6] << 8) | (uint64_t)solution_raw[7];
    now = (uint64_t)time(NULL);
    if (expires_at <= issued_at || expires_at - issued_at != 60 ||
        issued_at > now + 5 || now > expires_at ||
        !redp2p_challenge_mac_input(nonce, issued_at, expires_at, input,
            &input_len))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    redp2p_hmac_sha256_bytes(ctx->challenge_key, sizeof(ctx->challenge_key),
        input, input_len, mac);
    if (!redp2p_constant_time_equal(mac, received_mac, sizeof(mac))) {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    proto = json_object_get_number(req, "proto");
    if (!redp2p_json_require_port(json_object_get_number(req, "udp_port"),
        &udp_port))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    if ((proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP) ||
        !redp2p_index_parse_request_candidates(req, "candidates", candidates,
            &n_candidates))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    if (!redp2p_verify_register_pow(nonce, issued_at, expires_at, id,
        solution, ctx->pow_bits))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    if (!redp2p_hex_decode(pkey_hex, publisher_pkey, sizeof(publisher_pkey)) ||
        !redp2p_hex_decode(encrypted_secret_hex, encrypted_secret,
            sizeof(encrypted_secret)))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        goto cleanup;
    }
    if (!redp2p_registration_secret_decrypt(ctx->challenge_key, nonce,
        issued_at, expires_at, publisher_pkey, encrypted_secret,
        (unsigned char *)key) || !redp2p_index_control_secret_valid(
            (const unsigned char *)key))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    key[REDP2P_KEY_SZ] = '\0';
    if (!redp2p_register_message(nonce, issued_at,
        expires_at, id, key, (int)proto, udp_port, candidates, n_candidates,
        solution, input, &input_len))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    redp2p_hmac_sha256_bytes((const unsigned char *)key, strlen(key), input,
        input_len, expected_proof);
    if (!redp2p_constant_time_equal(expected_proof, received_proof,
        sizeof(expected_proof)))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    password = redp2p_index_password(ctx, id);
    if (password[0] && (!access_proof[0] ||
        !redp2p_admission_proof(password, input, input_len, expected_access) ||
        !redp2p_constant_time_equal((const unsigned char *)expected_access,
            (const unsigned char *)access_proof, 64)))
    {
        redp2p_index_respond_error(fd, 403, "auth_failed");
        goto cleanup;
    }
    redp2p_evict_stale(ctx);
    add_result = redp2p_add_peer(ctx, id, key);
    if (add_result == REDP2P_OK) {
        size_t peer_index;

        peer_index = redp2p_find_peer(ctx, id);
        if (peer_index != SIZE_MAX) {
            JSON_Value *reply;
            JSON_Object *out;

            ctx->peers[peer_index].peer.proto = (int)proto;
            ctx->peers[peer_index].peer.udp_port = udp_port;
            ctx->peers[peer_index].peer.n_candidates = n_candidates;
            if (n_candidates > 0) {
                memcpy(ctx->peers[peer_index].peer.candidates, candidates,
                    (size_t)n_candidates * sizeof(candidates[0]));
            }
            ctx->peers[peer_index].peer.last_seen = redp2p_now_s();
            reply = json_value_init_object();
            if (!reply) {
                redp2p_index_respond_error(fd, 500, "internal");
                goto cleanup;
            }
            out = json_value_get_object(reply);
            json_object_set_boolean(out, "ok", 1);
            redp2p_index_respond(fd, 200, "OK", reply);
            goto cleanup;
        }
        redp2p_remove_peer(ctx, id);
        redp2p_index_respond_error(fd, 500, "internal");
    } else if (add_result == REDP2P_EEXIST) {
        redp2p_index_respond_error(fd, 409, "already_registered");
    } else if (add_result == REDP2P_EFULL) {
        redp2p_index_respond_error(fd, 503, "table_full");
    } else {
        redp2p_index_respond_error(fd, 500, "internal");
    }
cleanup:
    crypto_wipe(nonce_hex, sizeof(nonce_hex));
    crypto_wipe(mac_hex, sizeof(mac_hex));
    crypto_wipe(solution_hex, sizeof(solution_hex));
    crypto_wipe(proof, sizeof(proof));
    crypto_wipe(pkey_hex, sizeof(pkey_hex));
    crypto_wipe(encrypted_secret_hex, sizeof(encrypted_secret_hex));
    crypto_wipe(access_proof, sizeof(access_proof));
    crypto_wipe(expected_access, sizeof(expected_access));
    crypto_wipe(key, sizeof(key));
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(mac, sizeof(mac));
    crypto_wipe(received_mac, sizeof(received_mac));
    crypto_wipe(received_proof, sizeof(received_proof));
    crypto_wipe(expected_proof, sizeof(expected_proof));
    crypto_wipe(solution_raw, sizeof(solution_raw));
    crypto_wipe(publisher_pkey, sizeof(publisher_pkey));
    crypto_wipe(encrypted_secret, sizeof(encrypted_secret));
    crypto_wipe(input, sizeof(input));
}

/**
 * Handles one heartbeat request with a sequenced possession proof.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_heartbeat(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    char proof[65];
    char expected[65];
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    double proto;
    unsigned short udp_port;
    int n_candidates;
    size_t peer_index;
    uint64_t sequence;
    int diff;
    int i;

    memset(proof, 0, sizeof(proof));
    memset(expected, 0, sizeof(expected));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) {
        crypto_wipe(proof, sizeof(proof));
        return;
    }
    if (!redp2p_index_require_sequence(req, &sequence) ||
        !redp2p_json_require_hex(req, "proof", proof, sizeof(proof), 64) ||
        !json_object_has_value_of_type(req, "proto", JSONNumber) ||
        !json_object_has_value_of_type(req, "udp_port", JSONNumber))
    {
        crypto_wipe(proof, sizeof(proof));
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    proto = json_object_get_number(req, "proto");
    if (!redp2p_json_require_port(json_object_get_number(req, "udp_port"),
        &udp_port))
    {
        crypto_wipe(proof, sizeof(proof));
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    if ((proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP) ||
        !redp2p_index_parse_request_candidates(req, "candidates", candidates,
            &n_candidates))
    {
        crypto_wipe(proof, sizeof(proof));
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    redp2p_evict_stale(ctx);
    peer_index = redp2p_find_peer(ctx, id);
    if (peer_index == SIZE_MAX) {
        crypto_wipe(proof, sizeof(proof));
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    if (sequence <= ctx->peers[peer_index].peer.sequence ||
        !redp2p_control_proof(ctx->peers[peer_index].peer.key, "heartbeat", id,
            sequence, (int)proto, udp_port, candidates,
            n_candidates, expected))
    {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    diff = 0;
    for (i = 0; i < 64; i++) diff |= proof[i] ^ expected[i];
    if (diff != 0) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    ctx->peers[peer_index].peer.proto = (int)proto;
    ctx->peers[peer_index].peer.udp_port = udp_port;
    ctx->peers[peer_index].peer.n_candidates = n_candidates;
    if (n_candidates > 0) {
        memcpy(ctx->peers[peer_index].peer.candidates, candidates,
            (size_t)n_candidates * sizeof(candidates[0]));
    }
    ctx->peers[peer_index].peer.last_seen = redp2p_now_s();
    ctx->peers[peer_index].peer.sequence = sequence;
    crypto_wipe(proof, sizeof(proof));
    crypto_wipe(expected, sizeof(expected));
    {
        JSON_Value *reply;
        JSON_Object *out;

        reply = json_value_init_object();
        if (!reply) {
            redp2p_index_respond_error(fd, 500, "internal");
            return;
        }
        out = json_value_get_object(reply);
        json_object_set_boolean(out, "ok", 1);
        redp2p_index_respond(fd, 200, "OK", reply);
    }
}

/**
 * Handles one lookup request, filtering expired records without writing.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_lookup(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    size_t peer_index;

    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) return;
    peer_index = redp2p_find_peer(ctx, id);
    if (peer_index == SIZE_MAX || redp2p_peer_is_stale(ctx, peer_index)) {
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    {
        JSON_Value *reply;
        JSON_Object *out;

        reply = json_value_init_object();
        if (!reply) {
            redp2p_index_respond_error(fd, 500, "internal");
            return;
        }
        out = json_value_get_object(reply);
        json_object_set_boolean(out, "ok", 1);
        json_object_set_string(out, "id", ctx->peers[peer_index].peer.id);
        json_object_set_number(out, "proto",
            (double)ctx->peers[peer_index].peer.proto);
        json_object_set_number(out, "udp_port",
            (double)ctx->peers[peer_index].peer.udp_port);
        redp2p_append_candidates(out, "candidates",
            ctx->peers[peer_index].peer.candidates,
            ctx->peers[peer_index].peer.n_candidates);
        json_object_set_number(out, "last_seen",
            (double)ctx->peers[peer_index].peer.last_seen);
        redp2p_index_respond(fd, 200, "OK", reply);
    }
}

/**
 * Handles one list request, returning non-expired publisher identifiers.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_list(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    JSON_Value *reply;
    JSON_Object *out;
    JSON_Value *array_value;
    JSON_Array *array;
    size_t i;

    (void)req;
    reply = json_value_init_object();
    if (!reply) {
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    out = json_value_get_object(reply);
    array_value = json_value_init_array();
    array = json_value_get_array(array_value);
    for (i = 0; i < ctx->n_peers; i++) {
        if (!redp2p_peer_is_stale(ctx, i))
            json_array_append_string(array, ctx->peers[i].peer.id);
    }
    json_object_set_value(out, "ids", array_value);
    json_object_set_boolean(out, "ok", 1);
    redp2p_index_respond(fd, 200, "OK", reply);
}

/**
 * Handles one deregister request with a sequenced possession proof.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_deregister(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    char proof[65];
    char expected[65];
    size_t peer_index;
    uint64_t sequence;
    int diff;
    int i;

    memset(proof, 0, sizeof(proof));
    memset(expected, 0, sizeof(expected));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        return;
    }
    if (!redp2p_index_require_sequence(req, &sequence) ||
        !redp2p_json_require_hex(req, "proof", proof, sizeof(proof), 64))
    {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    redp2p_evict_stale(ctx);
    peer_index = redp2p_find_peer(ctx, id);
    if (peer_index == SIZE_MAX || sequence <= ctx->peers[peer_index].peer.sequence ||
        !redp2p_control_proof(ctx->peers[peer_index].peer.key, "deregister", id,
            sequence, 0, 0, NULL, 0, expected))
    {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    diff = 0;
    for (i = 0; i < 64; i++) diff |= proof[i] ^ expected[i];
    if (diff != 0) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    redp2p_remove_peer(ctx, id);
    crypto_wipe(proof, sizeof(proof));
    crypto_wipe(expected, sizeof(expected));
    {
        JSON_Value *reply;
        JSON_Object *out;

        reply = json_value_init_object();
        if (!reply) {
            redp2p_index_respond_error(fd, 500, "internal");
            return;
        }
        out = json_value_get_object(reply);
        json_object_set_boolean(out, "ok", 1);
        redp2p_index_respond(fd, 200, "OK", reply);
    }
}

/**
 * Reports whether one host candidate names the same endpoint as a peer.
 * @param candidate Host candidate.
 * @param peer      Trusted peer socket address.
 * @param udp_port  Requested punch source port.
 * @return 1 when the candidate matches the peer address and port.
 */
static int redp2p_candidate_matches_sockaddr(
    const redp2p_candidate_t *candidate,
    const struct sockaddr_storage *peer, unsigned short udp_port)
{
    struct in_addr cand_v4;
    struct in_addr peer_v4;
    struct in6_addr cand_v6;
    struct in6_addr peer_v6;

    if (!candidate || !peer || candidate->port != udp_port) return 0;
    if (inet_pton(AF_INET, candidate->addr, &cand_v4) == 1 &&
        peer->ss_family == AF_INET)
    {
        peer_v4 = ((const struct sockaddr_in *)peer)->sin_addr;
        return cand_v4.s_addr == peer_v4.s_addr;
    }
    if (inet_pton(AF_INET6, candidate->addr, &cand_v6) == 1 &&
        peer->ss_family == AF_INET6)
    {
        peer_v6 = ((const struct sockaddr_in6 *)peer)->sin6_addr;
        return memcmp(&cand_v6, &peer_v6, sizeof(cand_v6)) == 0;
    }
    return 0;
}

/**
 * Merges the server-derived observed candidate into a punch request.
 *
 * The observed endpoint is the trusted transport source address of the
 * request joined with the declared udp_port; it is authoritative over a host
 * candidate that names the same endpoint and otherwise replaces the
 * lowest-priority host candidate in a full list.
 *
 * @param candidates Candidate array, updated in place.
 * @param count      Candidate count in and out.
 * @param peer       Trusted peer socket address, may be NULL.
 * @param udp_port   Punched source port.
 * @return 1 on success, 0 when the observed endpoint cannot be stored.
 */
static int redp2p_punch_req_merge_observed(redp2p_candidate_t *candidates,
    int *count, const struct sockaddr_storage *peer, unsigned short udp_port)
{
    redp2p_candidate_t observed;
    char host[REDP2P_ADDR_MAX + 1];
    int matched;
    int host_index;
    int i;

    if (!candidates || !count) return 0;
    if (!peer || !redp2p_sockaddr_host(peer, host, sizeof(host)))
        return 1;
    matched = -1;
    for (i = 0; i < *count; i++) {
        if (candidates[i].type == REDP2P_CAND_HOST &&
            redp2p_candidate_matches_sockaddr(&candidates[i], peer, udp_port))
        {
            matched = i;
            break;
        }
    }
    memset(&observed, 0, sizeof(observed));
    observed.type = REDP2P_CAND_OBSERVED;
    snprintf(observed.addr, sizeof(observed.addr), "%s", host);
    observed.port = udp_port;
    observed.priority = redp2p_candidate_priority(&observed);
    if (matched >= 0) {
        candidates[matched] = observed;
    } else if (*count < REDP2P_PEER_CANDIDATES_MAX) {
        candidates[(*count)++] = observed;
    } else {
        host_index = -1;
        for (i = *count - 1; i >= 0; i--) {
            if (candidates[i].type == REDP2P_CAND_HOST) {
                host_index = i;
                break;
            }
        }
        if (host_index < 0) return 0;
        candidates[host_index] = observed;
    }
    return redp2p_normalize_candidates(candidates, count);
}

/**
 * Refills one bucket without multiplying an unbounded elapsed interval.
 * @param bucket Bucket to update.
 * @param now Monotonic milliseconds.
 * @param capacity Maximum credit in thousandths of a token.
 * @param rate Credit earned per millisecond.
 * @return None.
 */
static void redp2p_rate_refill(redp2p_rate_bucket_t *bucket, uint64_t now,
    unsigned int capacity, unsigned int rate)
{
    uint64_t elapsed = now >= bucket->updated_ms ? now - bucket->updated_ms : 0;
    unsigned int missing = capacity - bucket->credit;

    if (elapsed >= (missing + rate - 1U) / rate) bucket->credit = capacity;
    else bucket->credit += (unsigned int)elapsed * rate;
    bucket->updated_ms = now;
}

/**
 * Checks both punch buckets and consumes credit only when both permit it.
 * @param ctx Locked index context.
 * @param peer Trusted HTTP transport source.
 * @param target Active publisher index.
 * @return 1 when allowed, 0 when limited, -1 on allocation or address failure.
 */
static int redp2p_punch_rate_allow(redp2p_t *ctx,
    const struct sockaddr_storage *peer, size_t target)
{
    unsigned char address[16] = {0};
    int family;
    size_t i;
    size_t selected = SIZE_MAX;
    size_t oldest = 0;
    uint64_t now = redp2p_now_ms();
    redp2p_rate_bucket_t *source;
    redp2p_rate_bucket_t *destination = &ctx->peers[target].punch_bucket;

    if (!peer) return -1;
    family = peer->ss_family;
    if (family == AF_INET) {
        memcpy(address, &((const struct sockaddr_in *)peer)->sin_addr, 4);
    } else if (family == AF_INET6) {
        static const unsigned char mapped[12] =
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};
        memcpy(address, &((const struct sockaddr_in6 *)peer)->sin6_addr, 16);
        if (memcmp(address, mapped, sizeof(mapped)) == 0) {
            memmove(address, address + 12, 4);
            memset(address + 4, 0, 12);
            family = AF_INET;
        }
    } else return -1;
    if (!ctx->rate_sources) {
        ctx->rate_sources = calloc(REDP2P_RATE_SOURCES_MAX,
            sizeof(*ctx->rate_sources));
        if (!ctx->rate_sources) return -1;
    }
    for (i = 0; i < ctx->n_rate_sources; ) {
        redp2p_rate_source_t *entry = &ctx->rate_sources[i];
        if (now >= entry->bucket.updated_ms &&
            now - entry->bucket.updated_ms >= REDP2P_RATE_SOURCE_IDLE_MS)
        {
            *entry = ctx->rate_sources[--ctx->n_rate_sources];
            continue;
        }
        i++;
    }
    for (i = 0; i < ctx->n_rate_sources; i++) {
        redp2p_rate_source_t *entry = &ctx->rate_sources[i];
        if (entry->family == family &&
            memcmp(entry->address, address, sizeof(address)) == 0)
            selected = i;
        if (entry->bucket.updated_ms < ctx->rate_sources[oldest].bucket.updated_ms)
            oldest = i;
    }
    if (selected == SIZE_MAX) {
        selected = ctx->n_rate_sources < REDP2P_RATE_SOURCES_MAX ?
            ctx->n_rate_sources++ : oldest;
        memcpy(ctx->rate_sources[selected].address, address, sizeof(address));
        ctx->rate_sources[selected].family = family;
        ctx->rate_sources[selected].bucket.credit = REDP2P_RATE_SOURCE_CAP;
        ctx->rate_sources[selected].bucket.updated_ms = now;
    }
    source = &ctx->rate_sources[selected].bucket;
    redp2p_rate_refill(source, now, REDP2P_RATE_SOURCE_CAP, 5U);
    if (source->credit < 1000U) return 0;
    redp2p_rate_refill(destination, now, REDP2P_RATE_TARGET_CAP, 10U);
    if (destination->credit < 1000U) return 0;
    source->credit -= 1000U;
    destination->credit -= 1000U;
    return 1;
}

/**
 * Handles one punch request, storing a bounded pending call.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @param peer Trusted transport source address of the request.
 * @return None.
 */
static void redp2p_index_handle_punch_req(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req, const struct sockaddr_storage *peer)
{
    char self_id[REDP2P_ID_MAX + 1];
    char target_id[REDP2P_ID_MAX + 1];
    char session[REDP2P_CTRL_SESSION_MAX + 1];
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    const char *session_str;
    unsigned short udp_port;
    int n_candidates;
    size_t peer_index;
    int rate_result;

    int self_result;
    int target_result;

    self_result = redp2p_index_require_id(req, "self_id", self_id,
        sizeof(self_id));
    target_result = redp2p_index_require_id(req, "target_id", target_id,
        sizeof(target_id));
    if (self_result == 0 || target_result == 0) {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    if (self_result < 0 || target_result < 0) {
        redp2p_index_respond_error(fd, 400, "invalid_id");
        return;
    }
    peer_index = redp2p_find_peer(ctx, target_id);
    if (peer_index == SIZE_MAX || redp2p_peer_is_stale(ctx, peer_index)) {
        if (peer_index != SIZE_MAX) redp2p_remove_peer(ctx, target_id);
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    if (!json_object_has_value_of_type(req, "session", JSONString)) {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    session_str = json_object_get_string(req, "session");
    if (!json_object_has_value_of_type(req, "udp_port", JSONNumber) ||
        !redp2p_json_require_port(json_object_get_number(req, "udp_port"),
            &udp_port))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    if (!session_str || strlen(session_str) >= sizeof(session) ||
        !redp2p_is_session_token(session_str) ||
        !redp2p_index_parse_request_candidates(req, "candidates", candidates,
            &n_candidates) ||
        !redp2p_punch_req_merge_observed(candidates, &n_candidates, peer,
            udp_port))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    memcpy(session, session_str, strlen(session_str) + 1);
    rate_result = redp2p_punch_rate_allow(ctx, peer, peer_index);
    if (rate_result != 1) {
        redp2p_index_respond_error(fd, rate_result == 0 ? 429 : 500,
            rate_result == 0 ? "rate_limited" : "internal");
        return;
    }
    redp2p_pending_call_evict_stale(ctx);
    if ((size_t)redp2p_pending_call_count_for_target(ctx, target_id) >=
        ctx->max_consumers_per_publisher)
    {
        redp2p_index_respond_error(fd, 429, "pending_limit_publisher");
        return;
    }
    if (ctx->n_pending_calls >= REDP2P_MAX_PENDING_CALLS_GLOBAL) {
        redp2p_index_respond_error(fd, 429, "pending_limit_global");
        return;
    }
    if (!redp2p_pending_call_add(ctx, self_id, target_id, session, candidates,
        n_candidates))
    {
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    {
        JSON_Value *reply;
        JSON_Object *out;

        reply = json_value_init_object();
        if (!reply) {
            redp2p_index_respond_error(fd, 500, "internal");
            return;
        }
        out = json_value_get_object(reply);
        json_object_set_boolean(out, "ok", 1);
        redp2p_index_respond(fd, 200, "OK", reply);
    }
}

/**
 * Handles one punch_poll request, returning and consuming pending calls.
 * @param ctx Locked index context.
 * @param fd  Request socket.
 * @param req Request JSON object.
 * @return None.
 */
static void redp2p_index_handle_punch_poll(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    char proof[65];
    char expected[65];
    char response[REDP2P_BUF];
    size_t response_size;
    redp2p_pending_call_t calls[REDP2P_PUNCH_POLL_MAX];
    JSON_Value *reply;
    JSON_Object *out;
    JSON_Value *array_value;
    JSON_Array *array;
    size_t peer_index;
    uint64_t sequence;
    int n_calls;
    int diff;
    int i;

    memset(proof, 0, sizeof(proof));
    memset(expected, 0, sizeof(expected));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        return;
    }
    if (!redp2p_index_require_sequence(req, &sequence) ||
        !redp2p_json_require_hex(req, "proof", proof, sizeof(proof), 64))
    {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    redp2p_evict_stale(ctx);
    peer_index = redp2p_find_peer(ctx, id);
    if (peer_index == SIZE_MAX) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    if (sequence <= ctx->peers[peer_index].peer.sequence ||
        !redp2p_control_proof(ctx->peers[peer_index].peer.key, "punch_poll", id,
            sequence, 0, 0, NULL, 0, expected))
    {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    diff = 0;
    for (i = 0; i < 64; i++) diff |= proof[i] ^ expected[i];
    if (diff != 0) {
        crypto_wipe(proof, sizeof(proof));
        crypto_wipe(expected, sizeof(expected));
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    ctx->peers[peer_index].peer.sequence = sequence;
    ctx->peers[peer_index].peer.last_seen = redp2p_now_s();
    crypto_wipe(proof, sizeof(proof));
    crypto_wipe(expected, sizeof(expected));
    redp2p_pending_call_evict_stale(ctx);
    redp2p_pending_call_collect(ctx, id, calls, &n_calls);
    reply = json_value_init_object();
    if (!reply) {
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    out = json_value_get_object(reply);
    array_value = json_value_init_array();
    array = json_value_get_array(array_value);
    for (i = 0; i < n_calls; i++) {
        JSON_Value *call_value;
        JSON_Object *call;

        call_value = json_value_init_object();
        call = json_value_get_object(call_value);
        json_object_set_string(call, "self_id", calls[i].caller_id);
        json_object_set_string(call, "session", calls[i].sess_id);
        redp2p_append_candidates(call, "candidates",
            calls[i].candidates, calls[i].n_candidates);
        json_array_append_value(array, call_value);
    }
    json_object_set_value(out, "calls", array_value);
    json_object_set_boolean(out, "ok", 1);
    response_size = json_serialization_size(reply);
    if (response_size == 0 || response_size >= sizeof(response) ||
        json_serialize_to_buffer(reply, response, sizeof(response)) != JSONSuccess)
    {
        json_value_free(reply);
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    json_value_free(reply);
    redp2p_pending_call_remove_first_n(ctx, id, n_calls);
    redp2p_http_write_response(fd, 200, "OK", "application/json", response);
    crypto_wipe(response, sizeof(response));
}

/**
 * Dispatches one complete HTTP request to its JSON index operation handler.
 * @param ctx  Locked index context.
 * @param fd   Request socket.
 * @param path Request path, ignored by the single-endpoint dispatch.
 * @param body Request body.
 * @param peer Trusted transport source address of the request.
 * @return None.
 */
static void redp2p_index_dispatch(redp2p_t *ctx, redp2p_fd_t fd,
    const char *path, const char *body, const struct sockaddr_storage *peer)
{
    JSON_Value *value;
    JSON_Object *req;
    const char *op;

    (void)path;
    value = json_parse_string(body);
    if (!value) {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    if (json_value_get_type(value) != JSONObject) {
        json_value_free(value);
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    req = json_value_get_object(value);
    if (!redp2p_index_json_unique_fields(req)) {
        json_value_free(value);
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    if (!json_object_has_value_of_type(req, "op", JSONString)) {
        json_value_free(value);
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    op = json_object_get_string(req, "op");
    if (strcmp(op, "challenge") == 0)
        redp2p_index_handle_challenge(ctx, fd, req);
    else if (strcmp(op, "register") == 0)
        redp2p_index_handle_register(ctx, fd, req);
    else if (strcmp(op, "heartbeat") == 0)
        redp2p_index_handle_heartbeat(ctx, fd, req);
    else if (strcmp(op, "lookup") == 0)
        redp2p_index_handle_lookup(ctx, fd, req);
    else if (strcmp(op, "list") == 0)
        redp2p_index_handle_list(ctx, fd, req);
    else if (strcmp(op, "deregister") == 0)
        redp2p_index_handle_deregister(ctx, fd, req);
    else if (strcmp(op, "punch_req") == 0)
        redp2p_index_handle_punch_req(ctx, fd, req, peer);
    else if (strcmp(op, "punch_poll") == 0)
        redp2p_index_handle_punch_poll(ctx, fd, req);
    else
        redp2p_index_respond_error(fd, 400, "bad_request");
    json_value_free(value);
}

/**
 * Scans one buffered HTTP request and reports whether it is complete.
 * @param ctx           Locked index context.
 * @param conn          In-flight request connection.
 * @param method        Output HTTP method.
 * @param method_cap    Method buffer capacity.
 * @param path          Output request path.
 * @param path_cap      Path buffer capacity.
 * @param body          Output request body, nul-terminated.
 * @param body_cap      Body buffer capacity.
 * @param http_status_out Optional HTTP status to reply on failure; 0 when the
 *                        request is complete, 1 when more input is needed.
 * @return 1 when the request is complete, 0 when more input is needed or on
 * a protocol violation (http_status_out set to a reply status).
 */
static int redp2p_index_request_parse(redp2p_index_conn_t *conn,
    char *method, int method_cap, char *path, int path_cap, char *body,
    int body_cap, int *http_status_out)
{
    char *header_end;
    char *line;
    const char *cursor;
    long content_length;
    int has_transfer_encoding;

    if (http_status_out) *http_status_out = 0;
    if (conn->buf_len <= 0) return 0;
    if (conn->buf_len >= (int)sizeof(conn->buf) - 1) {
        if (http_status_out) *http_status_out = 431;
        return 1;
    }
    conn->buf[conn->buf_len] = '\0';
    header_end = strstr(conn->buf, "\r\n\r\n");
    if (!header_end) {
        if (conn->buf_len >= REDP2P_HTTP_LINE_MAX * REDP2P_HTTP_HEADERS_MAX) {
            if (http_status_out) *http_status_out = 431;
            return 1;
        }
        return 0;
    }
    line = conn->buf;
    cursor = line;
    if (!redp2p_parse_field(&cursor, method, (size_t)method_cap, ' '))
        goto malformed;
    if (strcmp(method, "POST") != 0) {
        if (http_status_out) *http_status_out = 405;
        return 1;
    }
    if (!redp2p_parse_field(&cursor, path, (size_t)path_cap, ' '))
        goto malformed;
    if (strncmp(cursor, "HTTP/1.1", 8) != 0 &&
        strncmp(cursor, "HTTP/1.0", 8) != 0)
        goto malformed;
    content_length = -1;
    has_transfer_encoding = 0;
    cursor = strstr(line, "\r\n");
    if (!cursor) goto malformed;
    cursor += 2;
    while (cursor < header_end) {
        char *nl;
        char hline[REDP2P_HTTP_LINE_MAX];
        char *colon;
        size_t len;

        nl = strchr(cursor, '\n');
        if (!nl) break;
        len = (size_t)(nl - cursor);
        if (len == 1 && cursor[0] == '\r') break;
        if (len >= sizeof(hline)) goto malformed;
        memcpy(hline, cursor, len);
        hline[len] = '\0';
        if (len > 0 && hline[len - 1] == '\r') hline[len - 1] = '\0';
        colon = strchr(hline, ':');
        if (colon) {
            const char *value;
            char name[32];

            if ((size_t)(colon - hline) >= sizeof(name)) goto malformed;
            memcpy(name, hline, (size_t)(colon - hline));
            name[colon - hline] = '\0';
            value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            if (redp2p_ascii_casecmp(name, "Content-Length") == 0) {
                if (!redp2p_parse_u(value, 0, body_cap - 1, &content_length))
                    goto malformed;
            } else if (redp2p_ascii_casecmp(name, "Transfer-Encoding") == 0) {
                has_transfer_encoding = 1;
            }
        }
        cursor = nl + 1;
    }
    if (has_transfer_encoding) {
        if (http_status_out) *http_status_out = 501;
        return 1;
    }
    if (content_length < 0) content_length = 0;
    if (content_length > body_cap - 1) {
        if (http_status_out) *http_status_out = 413;
        return 1;
    }
    if (conn->buf_len < (int)(header_end - conn->buf) + 4 + (int)content_length)
        return 0;
    if (content_length > 0) {
        memcpy(body, header_end + 4, (size_t)content_length);
    }
    body[content_length] = '\0';
    if (http_status_out) *http_status_out = 0;
    return 1;
malformed:
    if (http_status_out) *http_status_out = 400;
    return 1;
}

/**
 * Opens the index listener and records its owned runtime resources.
 * @param runtime Runtime receiving the listener and context reference.
 * @param ctx Index context.
 * @param host Listener host or NULL for the wildcard address.
 * @param port Listener port.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_index_runtime_initialize(
    redp2p_index_runtime_t *runtime,
    redp2p_t *ctx,
    const char *host,
    unsigned short port)
{
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    char port_text[16];
    int result;

    memset(runtime, 0, sizeof(*runtime));
    runtime->ctx = ctx;
    runtime->listener_fd = REDP2P_FD_INVALID;
    runtime->last_prune = redp2p_now_s();
    if (redp2p_platform_init() != 0) {
        redp2p_set_error(ctx, "index: platform init failed");
        return REDP2P_ENET;
    }
    runtime->platform_initialized = 1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = host ? AF_UNSPEC : AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    result = getaddrinfo(host, port_text, &hints, &addresses);
    if (result != 0 && !host) {
        hints.ai_family = AF_INET;
        result = getaddrinfo(host, port_text, &hints, &addresses);
    }
    if (result != 0) {
        redp2p_set_error(ctx, "index: resolve %s:%u failed (%s)",
            host ? host : "*", (unsigned)port, gai_strerror(result));
        redp2p_platform_cleanup();
        runtime->platform_initialized = 0;
        return REDP2P_ENET;
    }
    for (address = addresses; address; address = address->ai_next) {
        int reuse;

        runtime->listener_fd = socket(address->ai_family,
            address->ai_socktype, address->ai_protocol);
        if (REDP2P_ISERR(runtime->listener_fd)) continue;
        reuse = 1;
        setsockopt(runtime->listener_fd, SOL_SOCKET, SO_REUSEADDR,
            (void *)&reuse, sizeof(reuse));
#ifdef IPV6_V6ONLY
        if (address->ai_family == AF_INET6) {
            int v6only;

            v6only = 0;
            setsockopt(runtime->listener_fd, IPPROTO_IPV6, IPV6_V6ONLY,
                (void *)&v6only, sizeof(v6only));
        }
#endif
        if (bind(runtime->listener_fd, address->ai_addr,
            (socklen_t)address->ai_addrlen) == 0 &&
            listen(runtime->listener_fd, 32) == 0)
            break;
        REDP2P_FD_CLOSE(runtime->listener_fd);
        runtime->listener_fd = REDP2P_FD_INVALID;
    }
    freeaddrinfo(addresses);
    if (REDP2P_ISERR(runtime->listener_fd)) {
        redp2p_set_error(ctx, "index: bind/listen %s:%u failed",
            host ? host : "*", (unsigned)port);
        redp2p_platform_cleanup();
        runtime->platform_initialized = 0;
        return REDP2P_ENET;
    }
    redp2p_set_nonblock(runtime->listener_fd);
    return REDP2P_OK;
}

/**
 * Builds the next readable descriptor set and evicts expired state.
 * @param runtime Initialized index runtime.
 * @return REDP2P_OK when ready, or REDP2P_ENET on failure.
 */
static int redp2p_index_prepare_fdset(redp2p_index_runtime_t *runtime) {
    redp2p_t *ctx;
    int i;

    ctx = runtime->ctx;
    FD_ZERO(&runtime->readable_fds);
    runtime->max_fd = -1;
    if (!redp2p_fdset_add(runtime->listener_fd, &runtime->readable_fds,
        &runtime->max_fd))
    {
        redp2p_set_error(ctx,
            "index: listener cannot be represented by fd_set");
        return REDP2P_ENET;
    }
    redp2p_lock(ctx);
    redp2p_pending_call_evict_stale(ctx);
    for (i = ctx->n_conns - 1; i >= 0; i--) {
        if (!redp2p_fdset_add(ctx->conns[i].fd, &runtime->readable_fds,
            &runtime->max_fd))
        {
            redp2p_set_error(ctx,
                "index: client descriptor cannot be represented by fd_set");
            redp2p_index_conn_remove(ctx, i);
        }
    }
    redp2p_unlock(ctx);
    return REDP2P_OK;
}

/**
 * Accepts at most one ready index client and transfers descriptor ownership.
 * @param runtime Initialized index runtime with a selected listener.
 * @return None.
 */
static void redp2p_index_accept_connection(redp2p_index_runtime_t *runtime) {
    struct sockaddr_storage client_address;
    socklen_t client_address_length;
    redp2p_fd_t client_fd;

    if (!FD_ISSET(runtime->listener_fd, &runtime->readable_fds)) return;
    client_address_length = sizeof(client_address);
    client_fd = accept(runtime->listener_fd,
        (struct sockaddr *)&client_address, &client_address_length);
    if (REDP2P_ISERR(client_fd)) return;
    redp2p_set_nonblock(client_fd);
    redp2p_lock(runtime->ctx);
    if (redp2p_index_conn_add(runtime->ctx, client_fd, &client_address,
        client_address_length) != REDP2P_OK)
        REDP2P_FD_CLOSE(client_fd);
    redp2p_unlock(runtime->ctx);
}

/**
 * Processes readable client connections in reverse table order.
 * @param runtime Initialized index runtime with selected descriptors.
 * @return None.
 */
static void redp2p_index_process_connections(redp2p_index_runtime_t *runtime) {
    redp2p_t *ctx;
    int i;

    ctx = runtime->ctx;
    for (i = ctx->n_conns - 1; i >= 0; i--) {
        char method[16];
        char path[128];
        char body[REDP2P_HTTP_BODY_MAX + 1];
        int http_status;
        int nread;

        if (redp2p_now_ms() - ctx->conns[i].ts >
            REDP2P_HTTP_TIMEOUT_S * 1000u)
        {
            redp2p_lock(ctx);
            redp2p_index_conn_remove(ctx, i);
            redp2p_unlock(ctx);
            continue;
        }
        if (!FD_ISSET(ctx->conns[i].fd, &runtime->readable_fds)) continue;
        nread = redp2p_sock_read(ctx->conns[i].fd,
            ctx->conns[i].buf + ctx->conns[i].buf_len,
            (int)sizeof(ctx->conns[i].buf) - 1 - ctx->conns[i].buf_len);
        if (nread <= 0) {
            redp2p_lock(ctx);
            redp2p_index_conn_remove(ctx, i);
            redp2p_unlock(ctx);
            continue;
        }
        ctx->conns[i].buf_len += nread;
        ctx->conns[i].buf[ctx->conns[i].buf_len] = '\0';
        method[0] = '\0';
        path[0] = '\0';
        body[0] = '\0';
        http_status = 0;
        redp2p_lock(ctx);
        if (redp2p_index_request_parse(&ctx->conns[i], method,
            (int)sizeof(method), path, (int)sizeof(path), body,
            (int)sizeof(body), &http_status))
        {
            if (http_status == 0) {
                redp2p_index_dispatch(ctx, ctx->conns[i].fd, path, body,
                    &ctx->conns[i].peer_addr);
            } else {
                redp2p_http_write_response(ctx->conns[i].fd, http_status,
                    http_status == 413 ? "Payload Too Large" :
                    (http_status == 405 ? "Method Not Allowed" :
                    (http_status == 501 ? "Not Implemented" :
                    (http_status == 431 ? "Request Header Fields Too Large" :
                    "Bad Request"))),
                    "text/plain", "malformed");
            }
            redp2p_index_conn_remove(ctx, i);
        }
        redp2p_unlock(ctx);
    }
}

/**
 * Runs the blocking select loop until stop or a descriptor-set failure.
 * @param runtime Initialized index runtime.
 * @return REDP2P_OK on requested stop, or REDP2P_ENET on fd-set failure.
 */
static int redp2p_index_event_loop(redp2p_index_runtime_t *runtime) {
    struct timeval timeout;
    int ready_count;
    int result;

    for (;;) {
        if (runtime->ctx->stop_requested) {
            break;
        }
        result = redp2p_index_prepare_fdset(runtime);
        if (result != REDP2P_OK) return result;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ready_count = select(runtime->max_fd + 1, &runtime->readable_fds,
            NULL, NULL, &timeout);
        if (ready_count < 0) {
            if (runtime->ctx->stop_requested) break;
            continue;
        }
        if (ready_count == 0) {
            uint64_t now = redp2p_now_s();

            FD_ZERO(&runtime->readable_fds);
            redp2p_index_process_connections(runtime);
            if (now - runtime->last_prune >= runtime->ctx->prune_interval_s) {
                redp2p_lock(runtime->ctx);
                redp2p_evict_stale(runtime->ctx);
                redp2p_unlock(runtime->ctx);
                runtime->last_prune = now;
            }
            continue;
        }
        redp2p_index_accept_connection(runtime);
        redp2p_index_process_connections(runtime);
    }
    return REDP2P_OK;
}

/**
 * Releases all index-owned connections, listener, and platform state.
 * @param runtime Initialized index runtime.
 * @return None.
 */
static void redp2p_index_runtime_cleanup(redp2p_index_runtime_t *runtime) {
    int i;

    redp2p_lock(runtime->ctx);
    for (i = runtime->ctx->n_conns - 1; i >= 0; i--)
        redp2p_index_conn_remove(runtime->ctx, i);
    redp2p_unlock(runtime->ctx);
    if (!REDP2P_ISERR(runtime->listener_fd))
        REDP2P_FD_CLOSE(runtime->listener_fd);
    runtime->listener_fd = REDP2P_FD_INVALID;
    if (runtime->platform_initialized) redp2p_platform_cleanup();
    runtime->platform_initialized = 0;
    atomic_store(&runtime->ctx->stop_requested, 0);
}

/**
 * Serves the blocking request-driven index lifecycle.
 * @param ctx Index context.
 * @param host Listener host or NULL for the wildcard address.
 * @param port Listener port.
 * @return REDP2P_OK on requested stop, or a negative error code on failure.
 */
int redp2p_serve_index(
    redp2p_t *ctx,
    const char *host,
    unsigned short port)
{
    redp2p_index_runtime_t runtime;
    int result;

    if (!ctx) return REDP2P_EINVAL;
    redp2p_set_error(ctx, NULL);
    if (redp2p_is_stop_requested(ctx)) {
        atomic_store(&ctx->stop_requested, 0);
        return REDP2P_OK;
    }
    if (port == 0) {
        redp2p_set_error(ctx, "index: port must be between 1 and 65535");
        return REDP2P_EINVAL;
    }
    result = redp2p_index_runtime_initialize(&runtime, ctx, host, port);
    if (result != REDP2P_OK) {
        atomic_store(&ctx->ready_status, result);
        atomic_store(&ctx->ready_state, -1);
        return result;
    }
    atomic_store(&ctx->ready_status, REDP2P_OK);
    atomic_store(&ctx->ready_state, 1);
    result = redp2p_index_event_loop(&runtime);
    redp2p_index_runtime_cleanup(&runtime);
    return result == REDP2P_OK ? REDP2P_OK : result;
}
