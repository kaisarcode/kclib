/**
 * libredp2p-idx-webrtc.c - REDP2P.
 * Summary: WebRTC signaling transport for the REDP2P index.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libredp2p-idx.h"

#include "monocypher.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REDP2P_RTC_SDP_MAX 49152U

typedef struct {
    char connection[33];
    char publisher[REDP2P_ID_MAX + 1];
    char *offer;
    char *answer;
    unsigned char capability_hash[32];
    uint64_t created;
} redp2p_rtc_pending_t;

struct redp2p_rtc_state {
    redp2p_rtc_pending_t *pending;
    size_t count;
    size_t cap;
};

/**
 * Sends one successful JSON response.
 * @return None.
 */
static void rtc_ok(redp2p_fd_t fd, JSON_Value *value) {
    JSON_Object *obj = json_value_get_object(value);
    json_object_set_boolean(obj, "ok", 1);
    redp2p_index_respond(fd, 200, "OK", value);
}

/**
 * Encodes binary bytes as lowercase hexadecimal.
 * @return 1 on success, 0 on error.
 */
static int rtc_hex(const unsigned char *in, size_t n, char *out, size_t cap) {
    return redp2p_hex_encode(in, n, out, cap);
}

/**
 * Validates one RTC session-description object.
 * @return 1 when valid, 0 otherwise.
 */
static int rtc_description(JSON_Object *req, const char *field,
    const char *type, const char **sdp)
{
    JSON_Object *desc;
    const char *actual_type;

    if (!json_object_has_value_of_type(req, field, JSONObject)) return 0;
    desc = json_object_get_object(req, field);
    if (!json_object_has_value_of_type(desc, "type", JSONString) ||
        !json_object_has_value_of_type(desc, "sdp", JSONString)) return 0;
    actual_type = json_object_get_string(desc, "type");
    *sdp = json_object_get_string(desc, "sdp");
    return actual_type && *sdp && strcmp(actual_type, type) == 0 &&
        (*sdp)[0] && strlen(*sdp) <= REDP2P_RTC_SDP_MAX;
}

/**
 * Builds one canonical RTC control proof.
 * @return 1 on success, 0 on error.
 */
static int rtc_proof(const char *key, const char *op, const char *id,
    uint64_t seq, const char *connection, const char *digest, char out[65])
{
    char message[256];
    unsigned char hash[32];
    int n;

    n = snprintf(message, sizeof(message), "%s\n%s\n%llu", op, id,
        (unsigned long long)seq);
    if (n < 0 || (size_t)n >= sizeof(message)) return 0;
    if (connection && digest) {
        n += snprintf(message + n, sizeof(message) - (size_t)n, "\n%s\n%s",
            connection, digest);
        if (n < 0 || (size_t)n >= sizeof(message)) return 0;
    }
    redp2p_hmac_sha256_bytes((const unsigned char *)key, strlen(key),
        (const unsigned char *)message, (size_t)n, hash);
    return rtc_hex(hash, sizeof(hash), out, 65);
}

/**
 * Compares two 64-byte hexadecimal proofs in constant time.
 * @return 1 when equal, 0 otherwise.
 */
static int rtc_equal_hex(const char *a, const char *b) {
    return a && b && strlen(a) == 64 && strlen(b) == 64 &&
        redp2p_constant_time_equal((const unsigned char *)a,
            (const unsigned char *)b, 64);
}

/**
 * Removes and wipes one RTC pending request.
 * @return None.
 */
static void rtc_remove(redp2p_t *ctx, size_t index) {
    redp2p_rtc_state_t *state = ctx->rtc;
    if (!state || index >= state->count) return;
    free(state->pending[index].offer);
    free(state->pending[index].answer);
    crypto_wipe(&state->pending[index], sizeof(state->pending[index]));
    if (index + 1U < state->count)
        state->pending[index] = state->pending[state->count - 1U];
    state->count--;
}

/**
 * Creates one bounded RTC pending request.
 * @return 1 on success, 0 on allocation or capacity failure.
 */
static int rtc_add(redp2p_t *ctx, const char *id, const char *offer,
    const char connection[33], const unsigned char capability_hash[32])
{
    redp2p_rtc_state_t *state = ctx->rtc;
    redp2p_rtc_pending_t *grown;
    size_t len;

    if (!state) {
        state = calloc(1, sizeof(*state));
        if (!state) return 0;
        ctx->rtc = state;
    }
    if (state->count >= REDP2P_MAX_PENDING_CALLS_GLOBAL) return 0;
    if (state->count == state->cap) {
        size_t cap = state->cap ? state->cap * 2U : 16U;
        if (cap > REDP2P_MAX_PENDING_CALLS_GLOBAL) cap = REDP2P_MAX_PENDING_CALLS_GLOBAL;
        grown = realloc(state->pending, cap * sizeof(*grown));
        if (!grown) return 0;
        state->pending = grown;
        state->cap = cap;
    }
    memset(&state->pending[state->count], 0, sizeof(state->pending[0]));
    len = strlen(offer);
    state->pending[state->count].offer = malloc(len + 1U);
    if (!state->pending[state->count].offer) return 0;
    memcpy(state->pending[state->count].offer, offer, len + 1U);
    snprintf(state->pending[state->count].connection,
        sizeof(state->pending[state->count].connection), "%s", connection);
    snprintf(state->pending[state->count].publisher,
        sizeof(state->pending[state->count].publisher), "%s", id);
    memcpy(state->pending[state->count].capability_hash, capability_hash, 32);
    state->pending[state->count].created = redp2p_now_s();
    state->count++;
    return 1;
}

/**
 * Prunes expired RTC pending requests.
 * @return None.
 */
void redp2p_idx_webrtc_prune(redp2p_t *ctx) {
    size_t i = 0;
    if (!ctx->rtc) return;
    while (i < ctx->rtc->count) {
        if (redp2p_now_s() - ctx->rtc->pending[i].created > ctx->pending_ttl_s)
            rtc_remove(ctx, i);
        else i++;
    }
}

/**
 * Removes RTC pending requests owned by one publisher.
 * @return None.
 */
void redp2p_idx_webrtc_remove_publisher(redp2p_t *ctx, const char *id) {
    size_t i = 0;
    if (!ctx->rtc) return;
    while (i < ctx->rtc->count) {
        if (strcmp(ctx->rtc->pending[i].publisher, id) == 0) rtc_remove(ctx, i);
        else i++;
    }
}

/**
 * Releases the private RTC pending state.
 * @return None.
 */
void redp2p_idx_webrtc_destroy(redp2p_t *ctx) {
    if (!ctx || !ctx->rtc) return;
    while (ctx->rtc->count > 0) rtc_remove(ctx, ctx->rtc->count - 1U);
    free(ctx->rtc->pending);
    free(ctx->rtc);
    ctx->rtc = NULL;
}

/**
 * Handles an RTC publisher registration.
 * @return None.
 */
void redp2p_idx_webrtc_register(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1], nonce_hex[65], mac_hex[65], solution_hex[17];
    char secret[REDP2P_KEY_STR_SZ], proof[65], access[65], expected_access[65];
    unsigned char nonce[32], received_mac[32], solution_raw[8], input[384];
    unsigned char hash[32];
    uint64_t issued, expires, solution, now;
    size_t used;
    int result;

    memset(access, 0, sizeof(access));
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id)) ||
        !redp2p_json_require_lower_hex(req, "nonce", nonce_hex, sizeof(nonce_hex), 64) ||
        !redp2p_json_require_lower_hex(req, "mac", mac_hex, sizeof(mac_hex), 64) ||
        !redp2p_json_require_lower_hex(req, "pow_solution", solution_hex, sizeof(solution_hex), 16) ||
        !redp2p_json_require_lower_hex(req, "secret", secret, sizeof(secret), 16) ||
        !redp2p_json_require_lower_hex(req, "proof", proof, sizeof(proof), 64) ||
        !redp2p_json_require_u64(req, "issued_at", &issued) ||
        !redp2p_json_require_u64(req, "expires_at", &expires) ||
        !redp2p_hex_decode(nonce_hex, nonce, 32) ||
        !redp2p_hex_decode(mac_hex, received_mac, 32) ||
        !redp2p_hex_decode(solution_hex, solution_raw, 8)) {
        redp2p_index_respond_error(fd, 400, "bad_request"); return;
    }
    if (json_object_has_value(req, "access_proof") &&
        !redp2p_json_require_lower_hex(req, "access_proof", access, sizeof(access), 64)) {
        redp2p_index_respond_error(fd, 400, "bad_request"); return;
    }
    solution = ((uint64_t)solution_raw[0] << 56) | ((uint64_t)solution_raw[1] << 48) |
        ((uint64_t)solution_raw[2] << 40) | ((uint64_t)solution_raw[3] << 32) |
        ((uint64_t)solution_raw[4] << 24) | ((uint64_t)solution_raw[5] << 16) |
        ((uint64_t)solution_raw[6] << 8) | solution_raw[7];
    now = (uint64_t)time(NULL);
    if (expires <= issued || expires - issued != 60 || issued > now + 5 ||
        now > expires || !redp2p_challenge_mac_input(nonce, issued, expires, input, &used)) {
        redp2p_index_respond_error(fd, 403, "auth_failed"); return;
    }
    redp2p_hmac_sha256_bytes(ctx->challenge_key, 32, input, used, hash);
    if (!redp2p_constant_time_equal(hash, received_mac, 32) ||
        !redp2p_hash_register_pow(nonce, issued, expires, id, solution, hash) ||
        redp2p_count_leading_zero_bits(hash) < ctx->pow_bits) {
        redp2p_index_respond_error(fd, 403, "auth_failed"); return;
    }
    used = 0;
    if (!redp2p_append_bytes(input, sizeof(input), &used,
        (const unsigned char *)"REDP2P-WEB-REGISTER", 19) ||
        !redp2p_append_bytes(input, sizeof(input), &used, nonce, 32) ||
        !redp2p_append_u64_be(input, sizeof(input), &used, issued) ||
        !redp2p_append_u64_be(input, sizeof(input), &used, expires) ||
        !redp2p_append_bytes(input, sizeof(input), &used, (unsigned char[]){0, (unsigned char)strlen(id)}, 2) ||
        !redp2p_append_bytes(input, sizeof(input), &used, (const unsigned char *)id, strlen(id)) ||
        !redp2p_append_bytes(input, sizeof(input), &used, (unsigned char[]){0, 16}, 2) ||
        !redp2p_append_bytes(input, sizeof(input), &used, (const unsigned char *)secret, 16)) {
        redp2p_index_respond_error(fd, 500, "internal"); return;
    }
    if (!redp2p_append_bytes(input, sizeof(input), &used, solution_raw, 8)) {
        redp2p_index_respond_error(fd, 500, "internal"); return;
    }
    redp2p_hmac_sha256_bytes((const unsigned char *)secret, 16, input, used, hash);
    if (!rtc_hex(hash, 32, expected_access, sizeof(expected_access)) || !rtc_equal_hex(proof, expected_access)) {
        redp2p_index_respond_error(fd, 403, "auth_failed"); return;
    }
    if (redp2p_index_password(ctx, id)[0] && (!access[0] ||
        !redp2p_admission_proof(redp2p_index_password(ctx, id), input, used, expected_access) ||
        !rtc_equal_hex(access, expected_access))) {
        redp2p_index_respond_error(fd, 403, "auth_failed"); return;
    }
    redp2p_evict_stale(ctx);
    result = redp2p_add_peer(ctx, id, secret);
    if (result != REDP2P_OK) {
        redp2p_index_respond_error(fd, result == REDP2P_EEXIST ? 409 : 503,
            result == REDP2P_EEXIST ? "already_registered" : "table_full"); return;
    }
    {
        size_t index = redp2p_find_peer(ctx, id);
        ctx->peers[index].peer.transport = REDP2P_TRANSPORT_RTC;
        rtc_ok(fd, json_value_init_object());
    }
}

/**
 * Handles an RTC publisher heartbeat.
 * @return None.
 */
void redp2p_idx_webrtc_heartbeat(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    char proof[65];
    char expected[65];
    uint64_t sequence;
    size_t index;

    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id)) ||
        !redp2p_index_require_sequence(req, &sequence) ||
        !redp2p_json_require_lower_hex(req, "proof", proof,
            sizeof(proof), 64))
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    index = redp2p_find_peer(ctx, id);
    if (index == SIZE_MAX) {
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    if (sequence <= ctx->peers[index].peer.sequence ||
        !rtc_proof(ctx->peers[index].peer.key, "heartbeat", id, sequence,
            NULL, NULL, expected) || !rtc_equal_hex(proof, expected))
    {
        redp2p_index_respond_error(fd, 403, "invalid_proof");
        return;
    }
    ctx->peers[index].peer.sequence = sequence;
    ctx->peers[index].peer.last_seen = redp2p_now_s();
    rtc_ok(fd, json_value_init_object());
}

/**
 * Returns RTC publisher discovery metadata.
 * @return None.
 */
void redp2p_idx_webrtc_lookup(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req)
{
    char id[REDP2P_ID_MAX + 1];
    size_t index;
    JSON_Value *reply;
    JSON_Object *out;

    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id))) return;
    redp2p_evict_stale(ctx);
    index = redp2p_find_peer(ctx, id);
    if (index == SIZE_MAX) {
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    reply = json_value_init_object();
    if (!reply) {
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    out = json_value_get_object(reply);
    json_object_set_string(out, "id", id);
    json_object_set_string(out, "transport", "rtc");
    json_object_set_number(out, "last_seen",
        (double)ctx->peers[index].peer.last_seen);
    rtc_ok(fd, reply);
}

/**
 * Queues one RTC offer for a publisher.
 * @return None.
 */
void redp2p_idx_webrtc_connect(redp2p_t *ctx, redp2p_fd_t fd,
    JSON_Object *req, const struct sockaddr_storage *peer)
{
    char id[REDP2P_ID_MAX + 1];
    char connection[33];
    char capability[65];
    const char *offer;
    unsigned char random[32];
    unsigned char hash[32];
    redp2p_sha256_t digest;
    size_t index;
    size_t count;
    size_t i;
    int rate;
    JSON_Value *reply;
    JSON_Object *out;

    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id)) ||
        !rtc_description(req, "offer", "offer", &offer) || !peer)
    {
        redp2p_index_respond_error(fd, 400, "bad_request");
        return;
    }
    redp2p_evict_stale(ctx);
    redp2p_idx_webrtc_prune(ctx);
    index = redp2p_find_peer(ctx, id);
    if (index == SIZE_MAX) {
        redp2p_index_respond_error(fd, 404, "not_found");
        return;
    }
    if (ctx->peers[index].peer.transport != REDP2P_TRANSPORT_RTC) {
        redp2p_index_respond_error(fd, 409, "unsupported_transport");
        return;
    }
    rate = redp2p_idx_native_rate_allow(ctx, peer, index);
    if (rate != 1) {
        redp2p_index_respond_error(fd, rate == 0 ? 429 : 500,
            rate == 0 ? "rate_limited" : "internal");
        return;
    }
    count = 0;
    if (ctx->rtc) {
        for (i = 0; i < ctx->rtc->count; i++) {
            if (strcmp(ctx->rtc->pending[i].publisher, id) == 0) count++;
        }
    }
    if (count >= ctx->max_consumers_per_publisher || (ctx->rtc &&
        ctx->rtc->count >= REDP2P_MAX_PENDING_CALLS_GLOBAL))
    {
        redp2p_index_respond_error(fd, 503, "pending_full");
        return;
    }
    if (redp2p_fill_random(random, 16) != 0 || !rtc_hex(random, 16,
        connection, sizeof(connection)) || redp2p_fill_random(random, 32) != 0 ||
        !rtc_hex(random, 32, capability, sizeof(capability)))
    {
        redp2p_index_respond_error(fd, 500, "internal");
        return;
    }
    redp2p_sha256_init(&digest);
    redp2p_sha256_update(&digest, (const unsigned char *)capability, 64);
    redp2p_sha256_final(&digest, hash);
    if (!rtc_add(ctx, id, offer, connection, hash)) {
        redp2p_index_respond_error(fd, 503, "pending_full");
        return;
    }
    reply = json_value_init_object();
    out = json_value_get_object(reply);
    json_object_set_string(out, "connection", connection);
    json_object_set_string(out, "capability", capability);
    json_object_set_number(out, "expires_at",
        (double)(redp2p_now_s() + ctx->pending_ttl_s));
    rtc_ok(fd, reply);
}

/**
 * Handles a publisher or consumer RTC poll.
 * @return None.
 */
void redp2p_idx_webrtc_poll(redp2p_t *ctx, redp2p_fd_t fd, JSON_Object *req) {
    char id[REDP2P_ID_MAX + 1], proof[65], expected[65], connection[33], capability[65]; uint64_t seq; size_t i, index; JSON_Value *reply; JSON_Object *out;
    redp2p_idx_webrtc_prune(ctx);
    if (json_object_has_value(req, "id")) {
        JSON_Value *array_value; JSON_Array *array;
        if (!redp2p_index_require_id_response(req, fd, id, sizeof(id)) || !redp2p_index_require_sequence(req, &seq) || !redp2p_json_require_lower_hex(req, "proof", proof, sizeof(proof), 64)) { redp2p_index_respond_error(fd, 400, "bad_request"); return; }
        index = redp2p_find_peer(ctx, id); if (index == SIZE_MAX) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
        if (ctx->peers[index].peer.transport != REDP2P_TRANSPORT_RTC) { redp2p_index_respond_error(fd, 409, "unsupported_transport"); return; }
        if (seq <= ctx->peers[index].peer.sequence || !rtc_proof(ctx->peers[index].peer.key, "poll", id, seq, NULL, NULL, expected) || !rtc_equal_hex(proof, expected)) { redp2p_index_respond_error(fd, 403, "invalid_proof"); return; }
        ctx->peers[index].peer.sequence = seq; ctx->peers[index].peer.last_seen = redp2p_now_s(); reply = json_value_init_object(); out = json_value_get_object(reply); array_value = json_value_init_array(); array = json_value_get_array(array_value);
        if (ctx->rtc) for (i = 0; i < ctx->rtc->count; i++) if (strcmp(ctx->rtc->pending[i].publisher, id) == 0 && !ctx->rtc->pending[i].answer) { JSON_Value *row = json_value_init_object(), *offer = json_value_init_object(); JSON_Object *row_obj = json_value_get_object(row), *offer_obj = json_value_get_object(offer); json_object_set_string(row_obj, "connection", ctx->rtc->pending[i].connection); json_object_set_string(offer_obj, "type", "offer"); json_object_set_string(offer_obj, "sdp", ctx->rtc->pending[i].offer); json_object_set_value(row_obj, "offer", offer); json_array_append_value(array, row); }
        json_object_set_value(out, "connections", array_value); rtc_ok(fd, reply); return;
    }
    if (!redp2p_json_require_lower_hex(req, "connection", connection, sizeof(connection), 32) || !redp2p_json_require_lower_hex(req, "capability", capability, sizeof(capability), 64)) { redp2p_index_respond_error(fd, 400, "bad_request"); return; }
    if (!ctx->rtc) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
    redp2p_sha256_t digest; unsigned char hash[32]; redp2p_sha256_init(&digest); redp2p_sha256_update(&digest, (const unsigned char *)capability, 64); redp2p_sha256_final(&digest, hash);
    for (i = 0; i < ctx->rtc->count && strcmp(ctx->rtc->pending[i].connection, connection); i++);
    if (i == ctx->rtc->count) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
    if (!redp2p_constant_time_equal(hash, ctx->rtc->pending[i].capability_hash, 32)) { redp2p_index_respond_error(fd, 403, "auth_failed"); return; }
    reply = json_value_init_object(); out = json_value_get_object(reply); if (ctx->rtc->pending[i].answer) { JSON_Value *answer = json_value_init_object(); JSON_Object *answer_obj = json_value_get_object(answer); json_object_set_string(answer_obj, "type", "answer"); json_object_set_string(answer_obj, "sdp", ctx->rtc->pending[i].answer); json_object_set_value(out, "answer", answer); rtc_ok(fd, reply); rtc_remove(ctx, i); } else { json_object_set_null(out, "answer"); rtc_ok(fd, reply); }
}

/**
 * Stores one publisher RTC answer.
 * @return None.
 */
void redp2p_idx_webrtc_answer(redp2p_t *ctx, redp2p_fd_t fd, JSON_Object *req) {
    char id[REDP2P_ID_MAX + 1], proof[65], expected[65], connection[33], digest_hex[65]; const char *answer; uint64_t seq; size_t index, i; redp2p_sha256_t digest; unsigned char hash[32];
    if (!redp2p_index_require_id_response(req, fd, id, sizeof(id)) || !redp2p_index_require_sequence(req, &seq) || !redp2p_json_require_lower_hex(req, "proof", proof, sizeof(proof), 64) || !redp2p_json_require_lower_hex(req, "connection", connection, sizeof(connection), 32) || !rtc_description(req, "answer", "answer", &answer)) { redp2p_index_respond_error(fd, 400, "bad_request"); return; }
    index = redp2p_find_peer(ctx, id); if (index == SIZE_MAX) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
    if (ctx->peers[index].peer.transport != REDP2P_TRANSPORT_RTC) { redp2p_index_respond_error(fd, 409, "unsupported_transport"); return; }
    redp2p_sha256_init(&digest); redp2p_sha256_update(&digest, (const unsigned char *)"answer\n", 7); redp2p_sha256_update(&digest, (const unsigned char *)answer, strlen(answer)); redp2p_sha256_final(&digest, hash); if (!rtc_hex(hash, 32, digest_hex, sizeof(digest_hex)) || seq <= ctx->peers[index].peer.sequence || !rtc_proof(ctx->peers[index].peer.key, "answer", id, seq, connection, digest_hex, expected) || !rtc_equal_hex(proof, expected)) { redp2p_index_respond_error(fd, 403, "invalid_proof"); return; }
    if (!ctx->rtc) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
    for (i = 0; i < ctx->rtc->count && strcmp(ctx->rtc->pending[i].connection, connection); i++);
    if (i == ctx->rtc->count || strcmp(ctx->rtc->pending[i].publisher, id) || ctx->rtc->pending[i].answer) { redp2p_index_respond_error(fd, 404, "not_found"); return; }
    ctx->rtc->pending[i].answer = malloc(strlen(answer) + 1U); if (!ctx->rtc->pending[i].answer) { redp2p_index_respond_error(fd, 500, "internal"); return; } strcpy(ctx->rtc->pending[i].answer, answer); ctx->peers[index].peer.sequence = seq; ctx->peers[index].peer.last_seen = redp2p_now_s(); rtc_ok(fd, json_value_init_object());
}
