/**
 * libredp2p-idx-native.c - REDP2P.
 * Summary: Native index transport registration, lookup and punch signaling.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p-idx.h"

#include "monocypher.h"
#include "parson.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#define REDP2P_RATE_SOURCES_MAX 4096U
#define REDP2P_RATE_SOURCE_IDLE_MS 120000U
#define REDP2P_RATE_SOURCE_CAP 20000U

/**
 * Stores one normalized source IP and its last rate-limit activity.
 */
struct redp2p_rate_source {
    unsigned char address[16];
    int family;
    redp2p_rate_bucket_t bucket;
};

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
 * @param dst Destination buffer.
 * @param dst_cap Destination capacity.
 * @param src Source string.
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
 * @param ctx Open index context.
 * @param caller_id Requesting consumer identifier.
 * @param target_id Target publisher identifier.
 * @param sess_id Session identifier.
 * @param candidates Caller candidate array.
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
 * @param ctx Open index context.
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
 * Copies pending calls for one publisher identifier without consuming them.
 * @param ctx Open index context.
 * @param target_id Target publisher identifier.
 * @param out Output pending call array.
 * @param out_n Output pending call count.
 * @return None.
 */
static void redp2p_pending_call_collect(redp2p_t *ctx,
    const char *target_id, redp2p_pending_call_t *out, int *out_n)
{
    int i;

    *out_n = 0;
    for (i = 0; i < ctx->n_pending_calls; i++) {
        if (strcmp(ctx->pending_calls[i].target_id, target_id) == 0 &&
            *out_n < REDP2P_PUNCH_POLL_MAX)
        {
            out[*out_n] = ctx->pending_calls[i];
            (*out_n)++;
        }
    }
}

/**
 * Consumes the first count pending calls for one publisher identifier.
 * @param ctx Open index context.
 * @param target_id Target publisher identifier.
 * @param count Number of leading matching calls to remove.
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
 * Parses a server request candidate list under the native destination policy.
 * @param obj Request JSON object.
 * @param field Candidate array field name.
 * @param out Output candidate array.
 * @param out_count Output candidate count.
 * @return 1 on success, 0 when malformed or rejected.
 */
static int redp2p_index_parse_request_candidates(JSON_Object *obj,
    const char *field, redp2p_candidate_t *out, int *out_count)
{
    struct in_addr ipv4;
    struct in6_addr ipv6;
    int i;

    if (!redp2p_parse_candidates(obj, field, out, out_count)) return 0;
    for (i = 0; i < *out_count; i++) {
        if (out[i].type != REDP2P_CAND_HOST &&
            out[i].type != REDP2P_CAND_SRFLX &&
            out[i].type != REDP2P_CAND_RELAY) return 0;
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
    return *out_count <= REDP2P_PEER_CANDIDATES_MAX;
}

#ifdef REDP2P_TESTING
/**
 * Parses one synthetic index candidate request through the production policy.
 * @param json Candidate-bearing JSON object.
 * @param out Parsed candidates.
 * @param out_count Parsed candidate count.
 * @return 1 when accepted, 0 when rejected.
 */
int redp2p_test_index_parse_request_candidates(const char *json,
    redp2p_candidate_t *out, int *out_count)
{
    JSON_Value *value;
    JSON_Object *obj;
    int result;

    if (!json || !out || !out_count) return 0;
    value = json_parse_string(json);
    if (!value || json_value_get_type(value) != JSONObject) {
        json_value_free(value);
        return 0;
    }
    obj = json_value_get_object(value);
    result = redp2p_index_parse_request_candidates(obj, "candidates", out,
        out_count);
    json_value_free(value);
    return result;
}
#endif

/**
 * Returns the public transport name stored for one native publisher.
 * @param transport Internal transport value.
 * @return Stable transport name, or NULL when invalid.
 */
static const char *redp2p_index_transport_name(int transport)
{
    if (transport == REDP2P_PROTO_TCP) return "tcp";
    if (transport == REDP2P_PROTO_UDP) return "udp";
    return NULL;
}

/**
 * Handles one native register request.
 * @param ctx Locked index context.
 * @param fd Request socket.
 * @param req Request JSON object.
 * @return None.
 */
void redp2p_idx_native_register(redp2p_t *ctx, redp2p_fd_t fd,
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

            ctx->peers[peer_index].peer.transport = (int)proto;
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
 * Handles one native heartbeat request.
 * @param ctx Locked index context.
 * @param fd Request socket.
 * @param req Request JSON object.
 * @return None.
 */
void redp2p_idx_native_heartbeat(redp2p_t *ctx, redp2p_fd_t fd,
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
            sequence, (int)proto, udp_port, candidates, n_candidates, expected))
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
    ctx->peers[peer_index].peer.transport = (int)proto;
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
 * Handles one native lookup request.
 * @param ctx Locked index context.
 * @param fd Request socket.
 * @param req Request JSON object.
 * @return None.
 */
void redp2p_idx_native_lookup(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    size_t peer_index;
    const char *transport;

    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) return;
    peer_index = redp2p_find_peer(ctx, id);
    if (peer_index == SIZE_MAX || redp2p_peer_is_stale(ctx, peer_index)) {
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    transport = redp2p_index_transport_name(
        ctx->peers[peer_index].peer.transport);
    if (!transport) {
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
        json_object_set_string(out, "id", ctx->peers[peer_index].peer.id);
        json_object_set_string(out, "transport", transport);
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
 * Reports whether one host candidate names the same endpoint as a peer.
 * @param candidate Host candidate.
 * @param peer Trusted peer socket address.
 * @param udp_port Requested punch source port.
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
 * @param candidates Candidate array, updated in place.
 * @param count Candidate count in and out.
 * @param peer Trusted peer socket address, may be NULL.
 * @param udp_port Punched source port.
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
    {
        const char *force_turn = getenv("REDP2P_FORCE_TURN");
        if (force_turn && strcmp(force_turn, "1") == 0)
            return redp2p_normalize_candidates(candidates, count);
    }
    if (!peer || !redp2p_sockaddr_host(peer, host, sizeof(host))) return 1;
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
    redp2p_rate_refill(destination, now, REDP2P_IDX_NATIVE_RATE_TARGET_CAP,
        10U);
    if (destination->credit < 1000U) return 0;
    source->credit -= 1000U;
    destination->credit -= 1000U;
    return 1;
}

/**
 * Handles one native punch request.
 * @param ctx Locked index context.
 * @param fd Request socket.
 * @param req Request JSON object.
 * @param peer Trusted transport source address of the request.
 * @return None.
 */
void redp2p_idx_native_punch_req(redp2p_t *ctx, redp2p_fd_t fd,
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
 * Handles one native punch poll request.
 * @param ctx Locked index context.
 * @param fd Request socket.
 * @param req Request JSON object.
 * @return None.
 */
void redp2p_idx_native_punch_poll(redp2p_t *ctx, redp2p_fd_t fd,
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
 * Prunes native transport pending state.
 * @param ctx Locked index context.
 * @return None.
 */
void redp2p_idx_native_prune(redp2p_t *ctx) {
    if (!ctx) return;
    redp2p_pending_call_evict_stale(ctx);
}
