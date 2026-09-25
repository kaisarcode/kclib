/**
 * libredp2p-core.c - REDP2P.
 * Summary: Context lifecycle, shared codecs, cryptography and platform helpers.
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
#include <stdarg.h>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#endif

#define REDP2P_PORT_MIN      1
#define REDP2P_PORT_MAX      65535
#define REDP2P_ETIMEOUT_SEC     120
#define REDP2P_CANDIDATES_MAX       16
#define REDP2P_PENDING_CALL_TTL_S    30
#define REDP2P_PRUNE_INTERVAL_S      60
#define REDP2P_PUNCH_POLL_MS 500
#define REDP2P_CTRL_LINE_MAX     1024
#define REDP2P_CTRL_FIELD_MAX     256

#ifndef REDP2P_BUILD_VERSION
#define REDP2P_BUILD_VERSION 0
#endif

static const unsigned char REDP2P_POW_DOMAIN[] = "REDP2P-POW";
static const unsigned char REDP2P_REGISTER_DOMAIN[] = "REDP2P-REGISTER";

_Static_assert(sizeof(REDP2P_POW_DOMAIN) - 1 == 10,
    "PoW domain must not include a NUL byte");
_Static_assert(sizeof(REDP2P_REGISTER_DOMAIN) - 1 == 15,
    "Register domain must not include a NUL byte");

static const uint32_t redp2p_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define REDP2P_SHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define REDP2P_SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define REDP2P_SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define REDP2P_SHA256_S0(x) (REDP2P_SHA256_ROR(x, 2) ^ REDP2P_SHA256_ROR(x, 13) ^ REDP2P_SHA256_ROR(x, 22))
#define REDP2P_SHA256_S1(x) (REDP2P_SHA256_ROR(x, 6) ^ REDP2P_SHA256_ROR(x, 11) ^ REDP2P_SHA256_ROR(x, 25))
#define REDP2P_SHA256_s0(x) (REDP2P_SHA256_ROR(x, 7) ^ REDP2P_SHA256_ROR(x, 18) ^ ((x) >> 3))
#define REDP2P_SHA256_s1(x) (REDP2P_SHA256_ROR(x, 17) ^ REDP2P_SHA256_ROR(x, 19) ^ ((x) >> 10))

#ifdef REDP2P_TEST_RANDOM
static unsigned char redp2p_test_random_bytes[256];
static size_t redp2p_test_random_len;
static size_t redp2p_test_random_pos;
static int redp2p_test_random_fail;
#endif

/**
 * Returns the textual name for one local candidate type.
 * @param type Candidate type.
 * @return Candidate type name.
 */
static const char *redp2p_candidate_type_name(redp2p_candidate_type_t type);

/**
 * Returns the address family for one candidate address.
 * @param candidate Candidate to inspect.
 * @return AF_INET, AF_INET6, or AF_UNSPEC.
 */
static int redp2p_candidate_family(const redp2p_candidate_t *candidate);

/**
 * Parses one strict unsigned decimal integer.
 * Summary: Rejects NULL, empty, leading/trailing garbage, signs, and overflow.
 * @param text    Input text to parse.
 * @param min     Inclusive lower bound.
 * @param max     Inclusive upper bound.
 * @param out     Output parsed value.
 * @return 1 on valid parse within bounds, 0 otherwise.
 */
int redp2p_parse_u(const char *text, long min, long max, long *out) {
    unsigned long value;
    unsigned long limit;
    size_t i;

    if (!text || !text[0] || !out || min < 0 || max < min) return 0;
    value = 0;
    limit = (unsigned long)max;
    for (i = 0; text[i] != '\0'; i++) {
        unsigned long digit;

        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (unsigned long)(text[i] - '0');
        if (digit > limit) return 0;
        if (value > (limit - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    if (value < (unsigned long)min) return 0;
    *out = (long)value;
    return 1;
}

/**
 * Compares two ASCII strings case-insensitively.
 * Summary: Keeps HTTP header-name matching portable across platforms that
 *          do not declare strcasecmp (macOS with strict C, Windows).
 * @param a Left string.
 * @param b Right string.
 * @return 0 when equal ignoring ASCII case, nonzero otherwise.
 */
int redp2p_ascii_casecmp(const char *a, const char *b) {
    size_t i;

    if (!a || !b) return -1;
    for (i = 0; ; i++) {
        unsigned char ca;
        unsigned char cb;

        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= (unsigned char)'A' && ca <= (unsigned char)'Z')
            ca = (unsigned char)(ca - (unsigned char)'A' + (unsigned char)'a');
        if (cb >= (unsigned char)'A' && cb <= (unsigned char)'Z')
            cb = (unsigned char)(cb - (unsigned char)'A' + (unsigned char)'a');
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == '\0') return 0;
    }
}

/**
 * Parses one strict environment numeric option.
 * Summary: Applies the same validation as CLI parsing.
 * @param text    Input text to parse.
 * @param min     Inclusive lower bound.
 * @param max     Inclusive upper bound.
 * @param out     Output parsed value.
 * @return 1 on valid parse within bounds, 0 otherwise.
 */
static int redp2p_parse_env(const char *text, long min, long max, long *out) {
    if (!text || text[0] == '\0') return 0;
    return redp2p_parse_u(text, min, max, out);
}

/**
 * Parses one strict unsigned decimal size value.
 * @param text Input text to parse.
 * @param out Output parsed value.
 * @return 1 on success, 0 on invalid input or overflow.
 */
static int redp2p_parse_size(const char *text, size_t *out) {
    size_t value;
    size_t i;

    if (!text || !text[0] || !out) return 0;
    value = 0;
    for (i = 0; text[i] != '\0'; i++) {
        size_t digit;

        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (size_t)(text[i] - '0');
        if (value > (SIZE_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    *out = value;
    return 1;
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t redp2p_version(void) {
    return (uint64_t)REDP2P_BUILD_VERSION;
}

/**
 * Sha256 transform.
 * @return Status code.
 */
static void redp2p_sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t W[64], a, b, c, d, e, f, g, h, T1, T2;
    int t;
    for (t = 0; t < 16; t++)
        W[t] = ((uint32_t)block[t*4]) << 24 |
            ((uint32_t)block[t*4+1]) << 16 |
            ((uint32_t)block[t*4+2]) << 8 |
            block[t*4+3];
    for (t = 16; t < 64; t++)
        W[t] = REDP2P_SHA256_s1(W[t-2]) + W[t-7] + REDP2P_SHA256_s0(W[t-15]) + W[t-16];
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (t = 0; t < 64; t++) {
        T1 = h + REDP2P_SHA256_S1(e) + REDP2P_SHA256_CH(e, f, g) + redp2p_sha256_k[t] + W[t];
        T2 = REDP2P_SHA256_S0(a) + REDP2P_SHA256_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    crypto_wipe(W, sizeof(W));
    crypto_wipe(&a, sizeof(a));
    crypto_wipe(&b, sizeof(b));
    crypto_wipe(&c, sizeof(c));
    crypto_wipe(&d, sizeof(d));
    crypto_wipe(&e, sizeof(e));
    crypto_wipe(&f, sizeof(f));
    crypto_wipe(&g, sizeof(g));
    crypto_wipe(&h, sizeof(h));
    crypto_wipe(&T1, sizeof(T1));
    crypto_wipe(&T2, sizeof(T2));
}

/**
 * Sha256 init.
 * @return Status code.
 */
void redp2p_sha256_init(redp2p_sha256_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

/**
 * Sha256 update.
 * @return Status code.
 */
void redp2p_sha256_update(redp2p_sha256_t *ctx, const unsigned char *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->count & 63] = data[i];
        ctx->count++;
        if ((ctx->count & 63) == 0)
            redp2p_sha256_transform(ctx->state, ctx->buf);
    }
}

/**
 * Sha256 final.
 * @return Status code.
 */
void redp2p_sha256_final(redp2p_sha256_t *ctx, unsigned char hash[32]) {
    uint64_t bits = ctx->count * 8;
    int idx = (int)(ctx->count & 63);
    int i;

    ctx->buf[idx++] = 0x80;
    if (idx > 56) {
        while (idx < 64) ctx->buf[idx++] = 0;
        redp2p_sha256_transform(ctx->state, ctx->buf);
        idx = 0;
    }
    while (idx < 56) ctx->buf[idx++] = 0;
    ctx->buf[56] = (unsigned char)(bits >> 56);
    ctx->buf[57] = (unsigned char)(bits >> 48);
    ctx->buf[58] = (unsigned char)(bits >> 40);
    ctx->buf[59] = (unsigned char)(bits >> 32);
    ctx->buf[60] = (unsigned char)(bits >> 24);
    ctx->buf[61] = (unsigned char)(bits >> 16);
    ctx->buf[62] = (unsigned char)(bits >> 8);
    ctx->buf[63] = (unsigned char)(bits);
    redp2p_sha256_transform(ctx->state, ctx->buf);
    for (i = 0; i < 8; i++) {
        hash[i * 4] = (unsigned char)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (unsigned char)ctx->state[i];
    }
}

/**
 * Computes one HMAC-SHA256 digest.
 * @param key     Shared password material.
 * @param msg     Input message bytes.
 * @param msg_len Input message length.
 * @param hash    Output digest buffer.
 * @return None.
 */
void redp2p_hmac_sha256_bytes(const unsigned char *key, size_t key_len,
    const unsigned char *msg, size_t msg_len, unsigned char hash[32])
{
    redp2p_sha256_t ctx;
    unsigned char key_block[64];
    unsigned char ipad[64];
    unsigned char opad[64];
    unsigned char inner[32];
    size_t i;

    memset(key_block, 0, sizeof(key_block));
    if (!key) key_len = 0;
    if (key_len > sizeof(key_block)) {
        redp2p_sha256_init(&ctx);
        redp2p_sha256_update(&ctx, (const unsigned char *)key, key_len);
        redp2p_sha256_final(&ctx, key_block);
    } else if (key_len > 0) {
        memcpy(key_block, key, key_len);
    }
    for (i = 0; i < sizeof(key_block); i++) {
        ipad[i] = (unsigned char)(key_block[i] ^ 0x36);
        opad[i] = (unsigned char)(key_block[i] ^ 0x5c);
    }
    redp2p_sha256_init(&ctx);
    redp2p_sha256_update(&ctx, ipad, sizeof(ipad));
    redp2p_sha256_update(&ctx, msg, msg_len);
    redp2p_sha256_final(&ctx, inner);
    redp2p_sha256_init(&ctx);
    redp2p_sha256_update(&ctx, opad, sizeof(opad));
    redp2p_sha256_update(&ctx, inner, sizeof(inner));
    redp2p_sha256_final(&ctx, hash);
    crypto_wipe(&ctx, sizeof(ctx));
    crypto_wipe(key_block, sizeof(key_block));
    crypto_wipe(ipad, sizeof(ipad));
    crypto_wipe(opad, sizeof(opad));
    crypto_wipe(inner, sizeof(inner));
}

/**
 * Computes one HMAC-SHA256 digest using a string key.
 * @param key Shared password material.
 * @param msg Input message bytes.
 * @param msg_len Input message length.
 * @param hash Output digest buffer.
 * @return None.
 */
static void redp2p_hmac_sha256(const char *key, const unsigned char *msg,
    size_t msg_len, unsigned char hash[32])
{
    redp2p_hmac_sha256_bytes((const unsigned char *)(key ? key : ""),
        key ? strlen(key) : 0, msg, msg_len, hash);
}

/**
 * Computes a password-bound admission proof for a canonical registration.
 * @param password Nonempty index admission password.
 * @param registration_message Canonical registration bytes.
 * @param registration_message_len Canonical byte count, at most 384.
 * @param proof Output buffer for 64 lowercase hexadecimal characters and NUL.
 * @return 1 on success, 0 on invalid input.
 */
int redp2p_admission_proof(const char *password,
const unsigned char *registration_message, size_t registration_message_len,
char proof[65])
{
    static const unsigned char domain[] = "REDP2P-ADMISSION-v1";
    unsigned char input[sizeof(domain) - 1 + 384];
    unsigned char hash[32];
    int result;

    if (!password || !password[0] || strlen(password) > REDP2P_PASS_MAX ||
        !registration_message || registration_message_len == 0 ||
        registration_message_len > 384 || !proof) return 0;
    memcpy(input, domain, sizeof(domain) - 1);
    memcpy(input + sizeof(domain) - 1, registration_message,
        registration_message_len);
    redp2p_hmac_sha256_bytes((const unsigned char *)password, strlen(password),
        input, sizeof(domain) - 1 + registration_message_len, hash);
    result = redp2p_hex_encode(hash, sizeof(hash), proof, 65);
    crypto_wipe(input, sizeof(input));
    crypto_wipe(hash, sizeof(hash));
    return result;
}

/**
 * Builds the canonical message and proof for one publisher control request.
 * @param key Publisher session secret.
 * @param op Control operation name.
 * @param id Publisher identifier.
 * @param sequence Strictly increasing control sequence.
 * @param proto Publisher transport protocol for heartbeat, or zero.
 * @param udp_port Publisher UDP port for heartbeat, or zero.
 * @param candidates Publisher candidates for heartbeat, or NULL.
 * @param n_candidates Candidate count for heartbeat.
 * @param proof Output hexadecimal HMAC proof.
 * @return 1 on success, 0 when the canonical message cannot be represented.
 */
int redp2p_control_proof(const char *key, const char *op,
    const char *id, uint64_t sequence, int proto, unsigned short udp_port,
    const redp2p_candidate_t *candidates, int n_candidates, char proof[65])
{
    unsigned char message[1024];
    unsigned char hash[32];
    size_t used;
    int written;
    int i;

    if (!key || !op || !id || !proof || n_candidates < 0 ||
        n_candidates > REDP2P_PEER_CANDIDATES_MAX)
        return 0;
    written = snprintf((char *)message, sizeof(message), "%s\n%s\n%llu",
        op, id, (unsigned long long)sequence);
    if (written < 0 || (size_t)written >= sizeof(message)) return 0;
    used = (size_t)written;
    if (strcmp(op, "heartbeat") == 0) {
        written = snprintf((char *)message + used, sizeof(message) - used,
            "\n%d\n%u\n%d", proto, (unsigned)udp_port, n_candidates);
        if (written < 0 || (size_t)written >= sizeof(message) - used)
            return 0;
        used += (size_t)written;
        for (i = 0; i < n_candidates; i++) {
            written = snprintf((char *)message + used,
                sizeof(message) - used, "\n%s\n%s\n%u",
                redp2p_candidate_type_name(candidates[i].type),
                candidates[i].addr, (unsigned)candidates[i].port);
            if (written < 0 || (size_t)written >= sizeof(message) - used)
                return 0;
            used += (size_t)written;
        }
    }
    redp2p_hmac_sha256(key, message, used, hash);
    if (!redp2p_hex_encode(hash, sizeof(hash), proof, 65)) return 0;
    crypto_wipe(message, sizeof(message));
    crypto_wipe(hash, sizeof(hash));
    return 1;
}

/**
 * Appends bounded binary bytes to a canonical message.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param data Bytes to append.
 * @param len Byte count.
 * @return 1 on success, 0 on overflow.
 */
int redp2p_append_bytes(unsigned char *out, size_t cap, size_t *used,
    const unsigned char *data, size_t len)
{
    if (!out || !used || (!data && len > 0) || len > cap - *used) return 0;
    if (len > 0) memcpy(out + *used, data, len);
    *used += len;
    return 1;
}

/**
 * Appends one unsigned byte in canonical form.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param value Value to append.
 * @return 1 on success, 0 on overflow.
 */
static int redp2p_append_u8(unsigned char *out, size_t cap, size_t *used,
    uint8_t value)
{
    return redp2p_append_bytes(out, cap, used, &value, 1);
}

/**
 * Appends one uint16 in big-endian form.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param value Value to append.
 * @return 1 on success, 0 on overflow.
 */
static int redp2p_append_u16_be(unsigned char *out, size_t cap, size_t *used,
    uint16_t value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char)(value >> 8);
    bytes[1] = (unsigned char)value;
    return redp2p_append_bytes(out, cap, used, bytes, sizeof(bytes));
}

/**
 * Appends one uint64 in big-endian form.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param value Value to append.
 * @return 1 on success, 0 on overflow.
 */
int redp2p_append_u64_be(unsigned char *out, size_t cap, size_t *used,
    uint64_t value)
{
    unsigned char bytes[8];
    int i;

    for (i = 7; i >= 0; i--) bytes[7 - i] = (unsigned char)(value >> (i * 8));
    return redp2p_append_bytes(out, cap, used, bytes, sizeof(bytes));
}

/**
 * Maps a public protocol value to the registration byte.
 * @param proto Public protocol value.
 * @param out Canonical protocol byte.
 * @return 1 on success, 0 on unsupported protocol.
 */
static int redp2p_register_proto_byte(int proto, uint8_t *out)
{
    if (!out) return 0;
    if (proto == REDP2P_PROTO_UDP) *out = 0x01;
    else if (proto == REDP2P_PROTO_TCP) *out = 0x02;
    else return 0;
    return 1;
}

/**
 * Builds the immutable binary proof-of-work input.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id Publisher identifier.
 * @param solution Candidate solution.
 * @param out Output buffer.
 * @param out_len Output byte count.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_register_pow_input(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    uint64_t solution, unsigned char *out, size_t *out_len)
{
    size_t used;
    size_t id_len;

    if (!nonce || !id || !out || !out_len) return 0;
    id_len = strlen(id);
    if (id_len == 0 || id_len > REDP2P_ID_MAX) return 0;
    used = 0;
    if (!redp2p_append_bytes(out, 128, &used,
        REDP2P_POW_DOMAIN, sizeof(REDP2P_POW_DOMAIN) - 1) ||
        !redp2p_append_bytes(out, 128, &used, nonce, 32) ||
        !redp2p_append_u64_be(out, 128, &used, issued_at) ||
        !redp2p_append_u64_be(out, 128, &used, expires_at) ||
        !redp2p_append_u16_be(out, 128, &used, (uint16_t)id_len) ||
        !redp2p_append_bytes(out, 128, &used,
            (const unsigned char *)id, id_len) ||
        !redp2p_append_u64_be(out, 128, &used, solution)) return 0;
    *out_len = used;
    return 1;
}

/**
 * Computes one registration proof-of-work digest.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id Publisher identifier.
 * @param solution Candidate solution.
 * @param hash Output digest.
 * @return 1 on success, 0 on invalid input.
 */
int redp2p_hash_register_pow(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    uint64_t solution, unsigned char hash[32])
{
    redp2p_sha256_t ctx;
    unsigned char input[128];
    size_t input_len;

    if (!redp2p_register_pow_input(nonce, issued_at, expires_at, id, solution,
        input, &input_len)) return 0;
    redp2p_sha256_init(&ctx);
    redp2p_sha256_update(&ctx, input, input_len);
    redp2p_sha256_final(&ctx, hash);
    crypto_wipe(input, sizeof(input));
    return 1;
}

/**
 * Builds the canonical binary registration proof message.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id Publisher identifier.
 * @param secret Publisher secret text.
 * @param proto Public protocol value.
 * @param udp_port Publisher UDP port.
 * @param candidates Normalized candidates.
 * @param n_candidates Candidate count.
 * @param solution PoW solution.
 * @param out Output buffer.
 * @param out_len Output byte count.
 * @return 1 on success, 0 on invalid input or overflow.
 */
int redp2p_register_message(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    const char *secret, int proto, unsigned short udp_port,
    const redp2p_candidate_t *candidates, int n_candidates,
    uint64_t solution, unsigned char *out, size_t *out_len)
{
    size_t used;
    size_t id_len;
    size_t secret_len;
    uint8_t proto_byte;
    int i;

    if (!nonce || !id || !secret || !out || !out_len || n_candidates < 0 ||
        n_candidates > REDP2P_PEER_CANDIDATES_MAX ||
        !redp2p_register_proto_byte(proto, &proto_byte)) return 0;
    id_len = strlen(id);
    secret_len = strlen(secret);
    if (id_len == 0 || id_len > REDP2P_ID_MAX || secret_len != REDP2P_KEY_SZ)
        return 0;
    used = 0;
    if (!redp2p_append_bytes(out, 384, &used,
        REDP2P_REGISTER_DOMAIN, sizeof(REDP2P_REGISTER_DOMAIN) - 1) ||
        !redp2p_append_bytes(out, 384, &used, nonce, 32) ||
        !redp2p_append_u64_be(out, 384, &used, issued_at) ||
        !redp2p_append_u64_be(out, 384, &used, expires_at) ||
        !redp2p_append_u16_be(out, 384, &used, (uint16_t)id_len) ||
        !redp2p_append_bytes(out, 384, &used,
            (const unsigned char *)id, id_len) ||
        !redp2p_append_u16_be(out, 384, &used, (uint16_t)secret_len) ||
        !redp2p_append_bytes(out, 384, &used,
            (const unsigned char *)secret, secret_len) ||
        !redp2p_append_u8(out, 384, &used, proto_byte) ||
        !redp2p_append_u16_be(out, 384, &used, udp_port) ||
        !redp2p_append_u8(out, 384, &used, (uint8_t)n_candidates)) return 0;
    for (i = 0; i < n_candidates; i++) {
        unsigned char addr[16];
        int family;
        size_t addr_len;

        family = redp2p_candidate_family(&candidates[i]);
        addr_len = family == AF_INET ? 4 : 16;
        if ((family != AF_INET && family != AF_INET6) ||
            inet_pton(family, candidates[i].addr, addr) != 1 ||
            !redp2p_append_u8(out, 384, &used,
                (uint8_t)candidates[i].type) ||
            !redp2p_append_u8(out, 384, &used,
                family == AF_INET ? 0x04 : 0x06) ||
            !redp2p_append_bytes(out, 384, &used, addr, addr_len) ||
            !redp2p_append_u16_be(out, 384, &used, candidates[i].port))
            return 0;
    }
    if (!redp2p_append_u64_be(out, 384, &used, solution)) return 0;
    *out_len = used;
    return 1;
}

/**
 * Tests whether a raw X25519 shared secret is all zero.
 * @param secret Raw shared secret.
 * @return 1 when every byte is zero, 0 otherwise.
 */
static int redp2p_registration_shared_secret_is_zero(
const unsigned char secret[32])
{
    unsigned char value;
    size_t i;

    value = 0;
    for (i = 0; i < 32; i++) value |= secret[i];
    return value == 0;
}

/**
 * Derives a registration encryption key from a shared secret and transcript.
 * @param shared_secret Raw X25519 shared secret.
 * @param nonce Raw challenge nonce.
 * @param index_pkey Index public key.
 * @param publisher_pkey Publisher public key.
 * @param encryption_key Output derived encryption key.
 * @return 1 on success, 0 on invalid input.
 */
static int redp2p_registration_encryption_key(
const unsigned char shared_secret[32], const unsigned char nonce[32],
const unsigned char index_pkey[32], const unsigned char publisher_pkey[32],
unsigned char encryption_key[32])
{
    static const unsigned char domain[] = "REDP2P-REGISTER-SECRET";
    unsigned char transcript[sizeof(domain) - 1 + 32 + 32 + 32];
    size_t used;

    if (!shared_secret || !nonce || !index_pkey || !publisher_pkey ||
        !encryption_key) return 0;
    used = 0;
    memcpy(transcript + used, domain, sizeof(domain) - 1);
    used += sizeof(domain) - 1;
    memcpy(transcript + used, nonce, 32); used += 32;
    memcpy(transcript + used, index_pkey, 32); used += 32;
    memcpy(transcript + used, publisher_pkey, 32); used += 32;
    redp2p_hmac_sha256_bytes(shared_secret, 32, transcript, used,
        encryption_key);
    crypto_wipe(transcript, sizeof(transcript));
    return 1;
}

/**
 * Derives the stateless index X25519 key pair for one registration challenge.
 * @param challenge_key Index registration challenge key.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param index_skey Output index secret key.
 * @param index_pkey Output index public key.
 * @return 1 on success, 0 on invalid input.
 */
int redp2p_registration_index_key(const unsigned char challenge_key[32],
const unsigned char nonce[32], uint64_t issued_at, uint64_t expires_at,
unsigned char index_skey[32], unsigned char index_pkey[32])
{
    static const unsigned char domain[] = "REDP2P-INDEX-KEY";
    unsigned char transcript[sizeof(domain) - 1 + 32 + 8 + 8];
    size_t used;

    if (!challenge_key || !nonce || !index_skey || !index_pkey) return 0;
    used = 0;
    memcpy(transcript + used, domain, sizeof(domain) - 1);
    used += sizeof(domain) - 1;
    memcpy(transcript + used, nonce, 32); used += 32;
    if (!redp2p_append_u64_be(transcript, sizeof(transcript), &used,
        issued_at) || !redp2p_append_u64_be(transcript, sizeof(transcript),
        &used, expires_at))
    {
        crypto_wipe(transcript, sizeof(transcript));
        return 0;
    }
    redp2p_hmac_sha256_bytes(challenge_key, 32, transcript, used, index_skey);
    crypto_x25519_public_key(index_pkey, index_skey);
    crypto_wipe(transcript, sizeof(transcript));
    return 1;
}

/**
 * Encrypts one registration control secret for the index challenge key.
 * @param publisher_skey Fresh publisher X25519 secret key.
 * @param index_pkey Challenge index X25519 public key.
 * @param nonce Raw challenge nonce.
 * @param secret Existing 16-byte publisher control secret.
 * @param publisher_pkey Output publisher X25519 public key.
 * @param encrypted_secret Output ciphertext followed by authentication tag.
 * @return 1 on success, 0 when the shared secret is unusable.
 */
int redp2p_registration_secret_encrypt(const unsigned char publisher_skey[32],
const unsigned char index_pkey[32], const unsigned char nonce[32],
const unsigned char secret[REDP2P_KEY_SZ], unsigned char publisher_pkey[32],
unsigned char encrypted_secret[32])
{
    unsigned char shared_secret[32];
    unsigned char encryption_key[32];
    int result;

    memset(shared_secret, 0, sizeof(shared_secret));
    memset(encryption_key, 0, sizeof(encryption_key));
    result = 0;
    if (!publisher_skey || !index_pkey || !nonce || !secret ||
        !publisher_pkey || !encrypted_secret) goto cleanup;
    crypto_x25519_public_key(publisher_pkey, publisher_skey);
    crypto_x25519(shared_secret, publisher_skey, index_pkey);
    if (redp2p_registration_shared_secret_is_zero(shared_secret) ||
        !redp2p_registration_encryption_key(shared_secret, nonce, index_pkey,
            publisher_pkey, encryption_key)) goto cleanup;
    crypto_aead_lock(encrypted_secret, encrypted_secret + REDP2P_KEY_SZ,
        encryption_key, nonce, NULL, 0, secret, REDP2P_KEY_SZ);
    result = 1;
cleanup:
    crypto_wipe(shared_secret, sizeof(shared_secret));
    crypto_wipe(encryption_key, sizeof(encryption_key));
    return result;
}

/**
 * Decrypts one registration control secret after challenge authentication.
 * @param challenge_key Index registration challenge key.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param publisher_pkey Publisher X25519 public key.
 * @param encrypted_secret Ciphertext followed by authentication tag.
 * @param secret Output recovered publisher control secret.
 * @return 1 on success, 0 on cryptographic failure.
 */
int redp2p_registration_secret_decrypt(const unsigned char challenge_key[32],
const unsigned char nonce[32], uint64_t issued_at, uint64_t expires_at,
const unsigned char publisher_pkey[32], const unsigned char encrypted_secret[32],
unsigned char secret[REDP2P_KEY_SZ])
{
    unsigned char index_skey[32];
    unsigned char index_pkey[32];
    unsigned char shared_secret[32];
    unsigned char encryption_key[32];
    int result;

    memset(index_skey, 0, sizeof(index_skey));
    memset(shared_secret, 0, sizeof(shared_secret));
    memset(encryption_key, 0, sizeof(encryption_key));
    result = 0;
    if (!challenge_key || !nonce || !publisher_pkey || !encrypted_secret ||
        !secret || !redp2p_registration_index_key(challenge_key, nonce,
            issued_at, expires_at, index_skey, index_pkey)) goto cleanup;
    crypto_x25519(shared_secret, index_skey, publisher_pkey);
    if (redp2p_registration_shared_secret_is_zero(shared_secret) ||
        !redp2p_registration_encryption_key(shared_secret, nonce, index_pkey,
            publisher_pkey, encryption_key)) goto cleanup;
    if (crypto_aead_unlock(secret, encrypted_secret + REDP2P_KEY_SZ,
        encryption_key, nonce, NULL, 0, encrypted_secret, REDP2P_KEY_SZ) != 0)
        goto cleanup;
    result = 1;
cleanup:
    crypto_wipe(index_skey, sizeof(index_skey));
    crypto_wipe(shared_secret, sizeof(shared_secret));
    crypto_wipe(encryption_key, sizeof(encryption_key));
    if (!result) crypto_wipe(secret, REDP2P_KEY_SZ);
    return result;
}

/**
 * Encodes a digest as lowercase hex.
 * @param hash     Input digest bytes.
 * @param hash_len Digest length in bytes.
 * @param out      Output hex buffer.
 * @param out_cap  Output buffer capacity.
 * @return 1 on success, 0 on failure.
 */
int redp2p_hex_encode(const unsigned char *hash, size_t hash_len,
    char *out, size_t out_cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (!hash || !out || out_cap < hash_len * 2 + 1) return 0;
    for (i = 0; i < hash_len; i++) {
        out[i * 2] = hex[(hash[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    out[hash_len * 2] = '\0';
    return 1;
}

/**
 * Counts leading zero bits in one digest.
 * @param hash Input digest bytes.
 * @return Count of leading zero bits.
 */
int redp2p_count_leading_zero_bits(const unsigned char hash[32]) {
    int total = 0, i;
    for (i = 0; i < 32; i++) {
        if (hash[i] == 0) {
            total += 8;
        } else {
            unsigned char b = hash[i];
            int j;
            for (j = 0; j < 8; j++) {
                if ((b & 0x80) == 0) total++;
                else break;
                b <<= 1;
            }
            break;
        }
    }
    return total;
}

/**
 * Returns whether one context requested stop.
 * @param ctx Context to inspect.
 * @return 1 when stop was requested, 0 otherwise.
 */
int redp2p_is_stop_requested(redp2p_t *ctx) {
    return ctx && atomic_load(&ctx->stop_requested);
}

/**
 * Lock.
 * @return Status code.
 */
void redp2p_lock(redp2p_t *ctx) {
#ifdef _WIN32
    EnterCriticalSection(&ctx->mutex);
#else
    pthread_mutex_lock(&ctx->mutex);
#endif
}

/**
 * Unlock.
 * @return Status code.
 */
void redp2p_unlock(redp2p_t *ctx) {
#ifdef _WIN32
    LeaveCriticalSection(&ctx->mutex);
#else
    pthread_mutex_unlock(&ctx->mutex);
#endif
}

/**
 * Returns a millisecond timestamp.
 * @return Monotonic-ish timestamp in milliseconds.
 */
uint64_t redp2p_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

/**
 * Returns a second timestamp.
 * Summary: Used for elapsed-time logic to avoid wall-clock jumps.
 * @return Monotonic-ish timestamp in seconds.
 */
uint64_t redp2p_now_s(void) {
    return redp2p_now_ms() / 1000;
}

/**
 * Fills a buffer with secure random bytes.
 * @return 0 on success, -1 on error.
 */
int redp2p_fill_random(unsigned char *buf, size_t len) {
#ifdef REDP2P_TEST_RANDOM
    if (redp2p_test_random_fail) return -1;
    if (redp2p_test_random_len > 0) {
        size_t i;
        for (i = 0; i < len; i++) {
            buf[i] = redp2p_test_random_bytes[redp2p_test_random_pos %
                redp2p_test_random_len];
            redp2p_test_random_pos++;
        }
        return 0;
    }
#endif
#ifdef _WIN32
    return BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    size_t off = 0;
    int fd;
    ssize_t n;

    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    while (off < len) {
        n = read(fd, buf + off, len - off);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        off += (size_t)n;
    }
    close(fd);
    return 0;
#endif
}

#ifdef REDP2P_TEST_RANDOM
/**
 * Configures deterministic random bytes for test builds.
 * @param bytes Byte stream to repeat.
 * @param len   Byte stream length.
 * @return None.
 */
void redp2p_test_random_set(const unsigned char *bytes, size_t len) {
    if (!bytes || len == 0) {
        redp2p_test_random_len = 0;
        redp2p_test_random_pos = 0;
        return;
    }
    if (len > sizeof(redp2p_test_random_bytes))
        len = sizeof(redp2p_test_random_bytes);
    memcpy(redp2p_test_random_bytes, bytes, len);
    redp2p_test_random_len = len;
    redp2p_test_random_pos = 0;
    redp2p_test_random_fail = 0;
}
#endif

#ifdef REDP2P_TEST_RANDOM
/**
 * Configures random-source failure for test builds.
 * @param fail Non-zero forces failure.
 * @return None.
 */
void redp2p_test_random_set_fail(int fail) {
    redp2p_test_random_fail = fail ? 1 : 0;
}
#endif

/**
 * Decodes one hex nibble.
 * @return Nibble value, or -1 on error.
 */
int redp2p_hex_decode_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Decodes a fixed-size hex string.
 * @return 1 on success, 0 on error.
 */
int redp2p_hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    size_t i;

    if (!hex || !out) return 0;
    for (i = 0; i < out_len; i++) {
        int hi = redp2p_hex_decode_nibble(hex[i * 2]);
        int lo = redp2p_hex_decode_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

#ifdef _WIN32
/**
 * Platform init.
 * @return 0 on success, -1 on error.
 */
int redp2p_platform_init(void) {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
}
#endif

#ifdef _WIN32
/**
 * Platform cleanup.
 * @return None.
 */
void redp2p_platform_cleanup(void) { WSACleanup(); }
#endif

#ifndef _WIN32
/**
 * Platform init.
 * @return 0 on success, -1 on error.
 */
int redp2p_platform_init(void) { return 0; }
#endif

#ifndef _WIN32
/**
 * Platform cleanup.
 * @return None.
 */
void redp2p_platform_cleanup(void) {}
#endif

/**
 * Set nonblock.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_nonblock(redp2p_fd_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

/**
 * Returns the UDP or TCP port from a stored socket address.
 * @param addr Stored socket address.
 * @return Host-order port, or 0 for unsupported families.
 */
unsigned short redp2p_sockaddr_port(
    const struct sockaddr_storage *addr)
{
    if (!addr) return 0;
    if (addr->ss_family == AF_INET)
        return ntohs(((const struct sockaddr_in *)addr)->sin_port);
    if (addr->ss_family == AF_INET6)
        return ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
    return 0;
}

/**
 * Compares stored socket endpoints by family, address, and port.
 * @param a First address.
 * @param b Second address.
 * @return 1 when endpoints match, 0 otherwise.
 */
int redp2p_sockaddr_equal(const struct sockaddr_storage *a,
    const struct sockaddr_storage *b)
{
    if (!a || !b || a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *aa = (const struct sockaddr_in *)a;
        const struct sockaddr_in *bb = (const struct sockaddr_in *)b;
        return aa->sin_port == bb->sin_port &&
            aa->sin_addr.s_addr == bb->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *aa = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *bb = (const struct sockaddr_in6 *)b;
        return aa->sin6_port == bb->sin6_port &&
            memcmp(&aa->sin6_addr, &bb->sin6_addr,
                sizeof(aa->sin6_addr)) == 0;
    }
    return 0;
}

/**
 * Applies the index destination policy to one raw candidate address.
 *
 * Host candidates may only name reachable unicast endpoints. Loopback,
 * unspecified, multicast, broadcast, and reserved destinations are refused.
 * IPv6 IPv4-mapped addresses are resolved through the IPv4 policy so that
 * loopback and multicast cannot be smuggled past the v6 checks.
 *
 * @param family Address family, AF_INET or AF_INET6.
 * @param host   Raw network-order address bytes.
 * @param port   Candidate UDP port.
 * @return 1 when the endpoint is acceptable, 0 otherwise.
 */
int redp2p_candidate_dest_allowed(int family, const void *host,
    unsigned short port)
{
    const unsigned char *raw;
    int i;

    if (!host || port == 0) return 0;
    if (family == AF_INET) {
        raw = (const unsigned char *)host;
        if (raw[0] == 0 || raw[0] == 127 || raw[0] >= 224) return 0;
        return 1;
    }
    if (family != AF_INET6) return 0;
    raw = (const unsigned char *)host;
    if (raw[0] == 0xff) return 0;
    for (i = 0; i < 10 && raw[i] == 0; i++) {
    }
    if (i == 10 && raw[10] == 0xff && raw[11] == 0xff)
        return redp2p_candidate_dest_allowed(AF_INET, raw + 12, port);
    for (i = 0; i < 16 && raw[i] == 0; i++) {
    }
    if (i == 16) return 0;
    if (i == 15 && raw[15] == 1) return 0;
    return 1;
}

/**
 * Adds one descriptor to one select set with descriptor-range validation.
 * Summary: Rejects descriptors that cannot be represented by fd_set.
 * @param fd    Descriptor to add.
 * @param set   Select set to update.
 * @param maxfd Current maximum descriptor, updated on success.
 * @return 1 when added, 0 when rejected.
 */
int redp2p_fdset_add(redp2p_fd_t fd, fd_set *set, int *maxfd) {
#ifdef _WIN32
    (void)maxfd;
    if (fd == INVALID_SOCKET) return 0;
    if ((int)set->fd_count >= FD_SETSIZE) return 0;
#else
    if (fd < 0) return 0;
    if (fd >= FD_SETSIZE) return 0;
    if (maxfd && (int)fd > *maxfd) *maxfd = (int)fd;
#endif
    FD_SET(fd, set);
    return 1;
}

/**
 * Sock read.
 * @return 0 on success, -1 on error.
 */
int redp2p_sock_read(redp2p_fd_t fd, char *buf, int len) {
#ifdef _WIN32
    return recv(fd, buf, len, 0);
#else
    return (int)read(fd, buf, (size_t)len);
#endif
}

/**
 * Sock write.
 * @return 0 on success, -1 on error.
 */
int redp2p_sock_write(redp2p_fd_t fd, const char *buf, int len) {
#ifdef _WIN32
    return send(fd, buf, len, 0);
#else
#  ifdef MSG_NOSIGNAL
    return (int)send(fd, buf, (size_t)len, MSG_NOSIGNAL);
#  else
    return (int)send(fd, buf, (size_t)len, 0);
#  endif
#endif
}

/**
 * Write all.
 * @return 0 on success, -1 on error.
 */
int redp2p_write_all(redp2p_fd_t fd, const char *buf, int len) {
    int off;
    int n;

    off = 0;
    while (off < len) {
        n = redp2p_sock_write(fd, buf + off, len - off);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

/**
 * Returns whether the identifier contains only alphanumeric characters.
 * @param id Identifier to validate.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_valid_id(const char *id) {
    size_t i;

    if (!id || !id[0]) return 0;
    for (i = 0; id[i]; i++) {
        if (i >= REDP2P_ID_MAX) return 0;
        if (!((id[i] >= 'a' && id[i] <= 'z') ||
            (id[i] >= 'A' && id[i] <= 'Z') ||
            (id[i] >= '0' && id[i] <= '9')))
            return 0;
    }
    return 1;
}

/**
 * Returns whether a password is a nonempty, non-control byte string.
 * @param pass Password token to validate.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_valid_pass_token(const char *pass) {
    size_t i;

    if (!pass || !pass[0]) return 0;
    for (i = 0; pass[i]; i++) {
        unsigned char byte = (unsigned char)pass[i];

        if (i >= REDP2P_PASS_MAX) return 0;
        if (byte <= 0x20 || byte == 0x7f) return 0;
    }
    return 1;
}

/**
 * Open.
 * @return 0 on success, -1 on error.
 */
int redp2p_open(redp2p_t **out) {
    redp2p_t *ctx;
    if (!out) return REDP2P_ERROR;
    ctx = (redp2p_t *)calloc(1, sizeof(redp2p_t));
    if (!ctx) return REDP2P_ERROR;
    ctx->peers = NULL;
    ctx->n_peers = 0;
    ctx->peers_alloc = 0;
    ctx->n_peers_cap = 0;
    ctx->nonvip_cap = 0;
    ctx->seats_set = 0;
    ctx->pass[0] = '\0';
    ctx->pow_bits = 0;
    ctx->bind_port = 0;
    ctx->explicit_port = 0;
    ctx->proto = REDP2P_PROTO_TCP;
    ctx->vips = NULL;
    ctx->n_vips = 0;
    ctx->vips_cap = 0;
    ctx->conns = NULL;
    ctx->n_conns = 0;
    ctx->conns_cap = 0;
    ctx->pending_calls = NULL;
    ctx->pending_calls_cap = 0;
    ctx->n_pending_calls = 0;
    ctx->max_consumers_per_publisher = REDP2P_MAX_CONSUMERS_PER_PUBLISHER;
    ctx->prune_interval_s = REDP2P_PRUNE_INTERVAL_S;
    ctx->etimeout_sec = REDP2P_ETIMEOUT_SEC;
    ctx->heartbeat_s = REDP2P_HEARTBEAT_S;
    ctx->punch_poll_ms = REDP2P_PUNCH_POLL_MS;
    ctx->pending_ttl_s = REDP2P_PENDING_CALL_TTL_S;
    {
        const char *env = getenv("REDP2P_PRUNE_INTERVAL_S");
        if (env) {
            long v = 0;
            if (redp2p_parse_u(env, 1, 3600, &v))
                ctx->prune_interval_s = (unsigned long)v;
        }
        env = getenv("REDP2P_ETIMEOUT_SEC");
        if (env) {
            long v = 0;
            if (redp2p_parse_u(env, 1, 86400, &v))
                ctx->etimeout_sec = (unsigned long)v;
        }
        env = getenv("REDP2P_HEARTBEAT_S");
        if (env) {
            long v = 0;
            if (redp2p_parse_u(env, 1, 3600, &v))
                ctx->heartbeat_s = (unsigned long)v;
        }
        env = getenv("REDP2P_PUNCH_POLL_MS");
        if (env) {
            long v = 0;
            if (redp2p_parse_u(env, 10, 60000, &v))
                ctx->punch_poll_ms = (unsigned long)v;
        }
        env = getenv("REDP2P_PENDING_CALL_TTL_S");
        if (env) {
            long v = 0;
            if (redp2p_parse_u(env, 1, 86400, &v))
                ctx->pending_ttl_s = (unsigned long)v;
        }
        env = getenv("REDP2P_MAX_CONSUMERS_PER_PUBLISHER");
        {
            size_t window = 0;

            if (redp2p_parse_size(env, &window))
                ctx->max_consumers_per_publisher = window == 0
                    ? REDP2P_MAX_PENDING_CALLS_PER_PUBLISHER : window;
        }
    }
    ctx->stop_requested = 0;
    ctx->ready_state = 0;
    ctx->ready_status = REDP2P_ERROR;
    if (redp2p_fill_random(ctx->challenge_key,
        sizeof(ctx->challenge_key)) != 0)
    {
        crypto_wipe(ctx, sizeof(*ctx));
        free(ctx);
        return REDP2P_ERROR;
    }
#ifdef _WIN32
    InitializeCriticalSection(&ctx->mutex);
#else
    pthread_mutex_init(&ctx->mutex, NULL);
#endif

    *out = ctx;
    return REDP2P_OK;
}

/**
 * Close.
 * @return 0 on success, -1 on error.
 */
int redp2p_close(redp2p_t *ctx) {
    int i;
    if (!ctx) return REDP2P_ERROR;
    if (ctx->conns) {
        for (i = 0; i < ctx->n_conns; i++)
            REDP2P_FD_CLOSE(ctx->conns[i].fd);
        free(ctx->conns);
    }
    if (ctx->vips)
        crypto_wipe(ctx->vips, ctx->vips_cap * sizeof(*ctx->vips));
    free(ctx->vips);
    if (ctx->peers)
        crypto_wipe(ctx->peers, ctx->peers_alloc * sizeof(*ctx->peers));
    free(ctx->peers);
    if (ctx->pending_calls)
        crypto_wipe(ctx->pending_calls,
            ctx->pending_calls_cap * sizeof(ctx->pending_calls[0]));
    free(ctx->pending_calls);
    free(ctx->rate_sources);
#ifdef _WIN32
    DeleteCriticalSection(&ctx->mutex);
#else
    pthread_mutex_destroy(&ctx->mutex);
#endif
    crypto_wipe(ctx->challenge_key, sizeof(ctx->challenge_key));
    crypto_wipe(ctx, sizeof(*ctx));
    free(ctx);
    return REDP2P_OK;
}

/**
 * Requests clean termination for the current blocking operation on one context.
 * @param ctx Context to stop.
 * @return 0 on success, -1 on error.
 */
int redp2p_stop(redp2p_t *ctx) {
    if (!ctx) return REDP2P_EINVAL;
    atomic_store(&ctx->stop_requested, 1);
    return REDP2P_OK;
}

/**
 * Checks whether one context was requested to stop.
 * @return Nonzero if a stop was requested, zero otherwise.
 */
int redp2p_stop_requested(redp2p_t *ctx) {
    return ctx && atomic_load(&ctx->stop_requested);
}

/**
 * Return string for status code.
 * @return Status string.
 */
const char *redp2p_strerror(int code) {
    switch (code) {
        case REDP2P_OK:       return "OK";
        case REDP2P_ERROR:    return "general error";
        case REDP2P_ENET:     return "network error";
        case REDP2P_ENOENT:   return "peer not found";
        case REDP2P_ETIMEOUT: return "timeout";
        case REDP2P_EFULL:    return "peer table full";
        case REDP2P_EINVAL:   return "invalid argument";
        case REDP2P_EPROTO:   return "protocol error";
        case REDP2P_EAUTH:    return "authentication failed";
        case REDP2P_EVERSION: return "unsupported protocol version";
        case REDP2P_EPUNCH:   return "direct connectivity failed";
        case REDP2P_EEXIST:   return "publisher already registered";
        default:              return "unknown error";
    }
}

/**
 * Records one per-context detail error message.
 * @return None.
 */
void redp2p_set_error(redp2p_t *ctx, const char *fmt, ...) {
    va_list ap;
    if (!ctx) return;
    if (!fmt) { ctx->err_buf[0] = '\0'; return; }
    va_start(ap, fmt);
    vsnprintf(ctx->err_buf, sizeof(ctx->err_buf), fmt, ap);
    va_end(ap);
    ctx->err_buf[sizeof(ctx->err_buf) - 1] = '\0';
}

/**
 * Returns the last per-context detail error message.
 * @return Context-owned string valid until the next update or context close.
 */
const char *redp2p_get_error(redp2p_t *ctx) {
    if (!ctx) return "";
    return ctx->err_buf;
}

/**
 * Returns one caller-owned options struct with safe defaults.
 * Summary: Index port and sweep are populated; callers own the returned struct.
 * @return Default options struct.
 */
redp2p_options_t redp2p_options_default(void) {
    redp2p_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.seats = 0;
    opts.pow = 0;
    opts.sweep = REDP2P_SWEEP_DEFAULT;
    return opts;
}

/**
 * Loads REDP2P_* environment values into one options struct with strict rules.
 * Summary: Invalid numeric values are ignored and defaults are retained.
 * Initialize with redp2p_options_default and free with redp2p_options_free.
 * @param opts Options struct to populate.
 * @return None.
 */
void redp2p_options_load_env(redp2p_options_t *opts) {
    const char *val;
    long num;
    size_t seats;

    if (!opts) return;

    val = getenv("REDP2P_SEATS");
    if (redp2p_parse_size(val, &seats) &&
        seats <= SIZE_MAX / sizeof(redp2p_peer_t))
        opts->seats = seats;

    val = getenv("REDP2P_POW");
    if (val && redp2p_parse_env(val, 0, REDP2P_POW_MAX, &num))
        opts->pow = (int)num;

    val = getenv("REDP2P_SWEEP");
    if (val && redp2p_parse_env(val, 0, REDP2P_SWEEP_MAX, &num))
        opts->sweep = (int)num;

    val = getenv("REDP2P_PASS");
    if (val) {
        crypto_wipe(opts->pass, sizeof(opts->pass));
        strncpy(opts->pass, val, REDP2P_PASS_MAX);
        opts->pass[REDP2P_PASS_MAX] = '\0';
    }

    val = getenv("REDP2P_VIP");
    if (val) {
        size_t len = strlen(val);
        if (opts->vip) crypto_wipe(opts->vip, strlen(opts->vip));
        free(opts->vip);
        opts->vip = (char *)malloc(len + 1);
        if (opts->vip) memcpy(opts->vip, val, len + 1);
    }

    val = getenv("REDP2P_STUN");
    if (val) {
        strncpy(opts->stun_url, val, sizeof(opts->stun_url) - 1);
        opts->stun_url[sizeof(opts->stun_url) - 1] = '\0';
    }
}

/**
 * Options free.
 * @return None.
 */
void redp2p_options_free(redp2p_options_t *opts) {
    if (!opts) return;
    crypto_wipe(opts->pass, sizeof(opts->pass));
    if (opts->vip) crypto_wipe(opts->vip, strlen(opts->vip));
    free(opts->vip);
    opts->vip = NULL;
}

/**
 * Stores the shared register password.
 * @param ctx  Open context.
 * @param pass Shared password string.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_pass(redp2p_t *ctx, const char *pass) {
    if (!ctx) return REDP2P_EINVAL;
    if (!pass || (pass[0] && !redp2p_is_valid_pass_token(pass))) {
        redp2p_set_error(ctx, "registration password contains invalid bytes");
        return REDP2P_EINVAL;
    }
    strncpy(ctx->pass, pass, REDP2P_PASS_MAX);
    ctx->pass[REDP2P_PASS_MAX] = '\0';
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Copies one colon-delimited field from a cursor.
 * @param cursor Input cursor updated after the field.
 * @param out    Output field buffer.
 * @param cap    Output field capacity.
 * @param delim  Required delimiter or NUL for final field.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_parse_field(const char **cursor, char *out, size_t cap,
    char delim)
{
    const char *start;
    const char *end;
    size_t len;

    if (!cursor || !*cursor || !out || cap == 0) return 0;
    start = *cursor;
    end = delim ? strchr(start, delim) : start + strlen(start);
    if (!end) return 0;
    len = (size_t)(end - start);
    if (len == 0 || len >= cap) return 0;
    memcpy(out, start, len);
    out[len] = '\0';
    *cursor = delim ? end + 1 : end;
    return 1;
}

/**
 * Reports whether a token is lowercase or uppercase hexadecimal.
 * @param text Input token.
 * @param len  Required token length.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_hex_token(const char *text, size_t len) {
    size_t i;

    if (!text || strlen(text) != len) return 0;
    for (i = 0; i < len; i++) {
        if ((text[i] >= '0' && text[i] <= '9') ||
            (text[i] >= 'a' && text[i] <= 'f') ||
            (text[i] >= 'A' && text[i] <= 'F'))
            continue;
        return 0;
    }
    return 1;
}

/**
 * Reports whether a session token is bounded and alphanumeric.
 * @param text Input token.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_session_token(const char *text) {
    size_t i;

    if (!text || !text[0]) return 0;
    for (i = 0; text[i]; i++) {
        if (i >= REDP2P_CTRL_SESSION_MAX) return 0;
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
            (text[i] >= 'A' && text[i] <= 'Z') ||
            (text[i] >= '0' && text[i] <= '9')))
            return 0;
    }
    return 1;
}

/**
 * Maps one candidate type token to an internal type.
 * @param text Input candidate type token.
 * @param type Output candidate type.
 * @return 1 on success, 0 on unknown type.
 */
static int redp2p_parse_candidate_type(const char *text,
    redp2p_candidate_type_t *type)
{
    if (!text || !type) return 0;
    if (strcmp(text, "host") == 0) *type = REDP2P_CAND_HOST;
    else if (strcmp(text, "observed") == 0) *type = REDP2P_CAND_OBSERVED;
    else return 0;
    return 1;
}

/**
 * Returns the textual name for one local candidate type.
 * @param type Candidate type.
 * @return Candidate type name.
 */
static const char *redp2p_candidate_type_name(redp2p_candidate_type_t type) {
    if (type == REDP2P_CAND_OBSERVED) return "observed";
    return "host";
}

/**
 * Returns the address family for one candidate address.
 * @param candidate Candidate to inspect.
 * @return AF_INET, AF_INET6, or AF_UNSPEC.
 */
static int redp2p_candidate_family(const redp2p_candidate_t *candidate) {
    struct in_addr ipv4;
    struct in6_addr ipv6;

    if (!candidate) return AF_UNSPEC;
    if (inet_pton(AF_INET, candidate->addr, &ipv4) == 1) return AF_INET;
    if (inet_pton(AF_INET6, candidate->addr, &ipv6) == 1) return AF_INET6;
    return AF_UNSPEC;
}

/**
 * Returns the deterministic local priority for one candidate.
 * @param candidate Candidate to rank.
 * @return Lower priority values are attempted first.
 */
unsigned int redp2p_candidate_priority(
    const redp2p_candidate_t *candidate)
{
    int family;
    unsigned int base;

    if (!candidate) return 900u;
    family = redp2p_candidate_family(candidate);
    if (family == AF_INET6) base = 100u;
    else if (family == AF_INET) base = 200u;
    else return 900u;
    if (candidate->type == REDP2P_CAND_HOST) return base;
    if (candidate->type == REDP2P_CAND_OBSERVED) return base + 10u;
    return 900u;
}

/**
 * Reports whether two candidates describe the same network endpoint.
 * @param a First candidate.
 * @param b Second candidate.
 * @return 1 when equivalent, 0 otherwise.
 */
static int redp2p_candidate_same_endpoint(const redp2p_candidate_t *a,
    const redp2p_candidate_t *b)
{
    struct sockaddr_storage aa;
    struct sockaddr_storage bb;

    if (!a || !b || a->type != b->type || a->port != b->port) return 0;
    if (redp2p_candidate_family(a) != redp2p_candidate_family(b)) return 0;
    if (!redp2p_candidate_sockaddr(a, &aa)) return 0;
    if (!redp2p_candidate_sockaddr(b, &bb)) return 0;
    return redp2p_sockaddr_equal(&aa, &bb);
}

/**
 * Compares two candidates by local priority and stable textual fields.
 * @param a First candidate.
 * @param b Second candidate.
 * @return Negative, zero, or positive comparison result.
 */
static int redp2p_candidate_compare(const redp2p_candidate_t *a,
    const redp2p_candidate_t *b)
{
    unsigned char aa[16];
    unsigned char bb[16];
    int family_a;
    int family_b;
    int len;
    int cmp;

    family_a = redp2p_candidate_family(a);
    family_b = redp2p_candidate_family(b);
    if (family_a != family_b) return family_a == AF_INET ? -1 : 1;
    len = family_a == AF_INET ? 4 : 16;
    if (inet_pton(family_a, a->addr, aa) != 1 ||
        inet_pton(family_b, b->addr, bb) != 1) return 0;
    cmp = memcmp(aa, bb, (size_t)len);
    if (cmp != 0) return cmp;
    if (a->port != b->port) return a->port < b->port ? -1 : 1;
    return (int)a->type - (int)b->type;
}

/**
 * Normalizes candidate priority, removes duplicates, and sorts the list.
 * @param candidates Candidate array.
 * @param count      Candidate count in and out.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_normalize_candidates(redp2p_candidate_t *candidates,
    int *count)
{
    int i;
    int n;

    if (!candidates || !count || *count < 0 || *count > REDP2P_CANDIDATES_MAX)
        return 0;
    n = 0;
    for (i = 0; i < *count; i++) {
        int j;
        if (candidates[i].port == 0) return 0;
        if (redp2p_candidate_family(&candidates[i]) == AF_UNSPEC) return 0;
        candidates[i].priority = redp2p_candidate_priority(&candidates[i]);
        if (candidates[i].priority >= 900u) return 0;
        for (j = 0; j < n; j++) {
            if (!redp2p_candidate_same_endpoint(&candidates[j], &candidates[i]))
                continue;
            if (redp2p_candidate_compare(&candidates[i], &candidates[j]) < 0)
                candidates[j] = candidates[i];
            break;
        }
        if (j == n) candidates[n++] = candidates[i];
    }
    for (i = 1; i < n; i++) {
        redp2p_candidate_t item = candidates[i];
        int j = i - 1;
        while (j >= 0 && redp2p_candidate_compare(&item, &candidates[j]) < 0) {
            candidates[j + 1] = candidates[j];
            j--;
        }
        candidates[j + 1] = item;
    }
    *count = n;
    return 1;
}

/**
 * Converts a candidate into a socket address.
 * @param candidate Candidate to convert.
 * @param out       Output socket address.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_candidate_sockaddr(const redp2p_candidate_t *candidate,
    struct sockaddr_storage *out)
{
    struct sockaddr_in *v4;
    struct sockaddr_in6 *v6;

    if (!candidate || !out || candidate->port == 0) return 0;
    memset(out, 0, sizeof(*out));
    v4 = (struct sockaddr_in *)out;
    if (inet_pton(AF_INET, candidate->addr, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(candidate->port);
        return 1;
    }
    v6 = (struct sockaddr_in6 *)out;
    if (inet_pton(AF_INET6, candidate->addr, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(candidate->port);
        return 1;
    }
    return 0;
}

/**
 * Extracts and validates one fixed-width hexadecimal token field.
 * @param obj    Request object.
 * @param field  Field name.
 * @param out    Output token.
 * @param out_cap Output token capacity.
 * @param hex_len Required hex length.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_json_require_hex(JSON_Object *obj, const char *field,
    char *out, size_t out_cap, size_t hex_len)
{
    const char *value;

    if (!json_object_has_value_of_type(obj, field, JSONString)) return 0;
    value = json_object_get_string(obj, field);
    if (!value || out_cap <= hex_len || !redp2p_is_hex_token(value, hex_len))
        return 0;
    memcpy(out, value, hex_len);
    out[hex_len] = '\0';
    return 1;
}

/**
 * Extracts a fixed-width lowercase hexadecimal token field.
 * @param obj Request object.
 * @param field Field name.
 * @param out Output token.
 * @param out_cap Output capacity.
 * @param hex_len Required token length.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_json_require_lower_hex(JSON_Object *obj, const char *field,
    char *out, size_t out_cap, size_t hex_len)
{
    const char *value;
    size_t i;

    if (!json_object_has_value_of_type(obj, field, JSONString)) return 0;
    value = json_object_get_string(obj, field);
    if (!value || out_cap <= hex_len || strlen(value) != hex_len) return 0;
    for (i = 0; i < hex_len; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
            (value[i] >= 'a' && value[i] <= 'f')))
            return 0;
    }
    memcpy(out, value, hex_len);
    out[hex_len] = '\0';
    return 1;
}

/**
 * Extracts one exact nonnegative timestamp JSON number.
 * @param obj Request object.
 * @param field Field name.
 * @param value Output timestamp.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_json_require_u64(JSON_Object *obj, const char *field,
    uint64_t *value)
{
    double number;

    if (!obj || !field || !value || !json_object_has_value_of_type(obj, field,
        JSONNumber)) return 0;
    number = json_object_get_number(obj, field);
    if (!isfinite(number) || number < 0.0 || number > 9007199254740991.0 ||
        number != floor(number)) return 0;
    *value = (uint64_t)number;
    return 1;
}

/**
 * Validates one JSON number as a UDP port before storing it.
 * @param value Parsed JSON numeric value.
 * @param port Output UDP port.
 * @return 1 on a finite integral port in range, 0 otherwise.
 */
int redp2p_json_require_port(double value, unsigned short *port)
{
    if (!port || !isfinite(value)) return 0;
    if (value < REDP2P_PORT_MIN || value > REDP2P_PORT_MAX) return 0;
    if (value != floor(value)) return 0;
    *port = (unsigned short)value;
    return 1;
}

/**
 * Parses one bounded JSON candidate array into candidate records.
 * @param obj       Request object.
 * @param field     Array field name.
 * @param out       Output candidate array.
 * @param out_count Output candidate count.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_parse_candidates(JSON_Object *obj, const char *field,
    redp2p_candidate_t *out, int *out_count)
{
    JSON_Array *array;
    size_t count;
    size_t i;
    redp2p_candidate_t temp[REDP2P_PEER_CANDIDATES_MAX];
    int temp_count = 0;

    *out_count = 0;
    if (!json_object_has_value_of_type(obj, field, JSONArray)) return 1;
    array = json_object_get_array(obj, field);
    count = json_array_get_count(array);
    for (i = 0; i < count; i++) {
        JSON_Object *item;
        const char *type_text;
        const char *addr;
        double value;
        unsigned short port;
        redp2p_candidate_type_t type;
        struct in_addr ipv4;
        struct in6_addr ipv6;
        redp2p_candidate_t cand;
        int j;
        int duplicate;

        item = json_array_get_object(array, i);
        if (!item) return 0;
        type_text = json_object_get_string(item, "type");
        addr = json_object_get_string(item, "addr");
        if (!type_text || !addr || strlen(addr) > REDP2P_ADDR_MAX) return 0;
        if (!redp2p_parse_candidate_type(type_text, &type)) return 0;
        if (type != REDP2P_CAND_HOST && type != REDP2P_CAND_OBSERVED)
            return 0;
        if (inet_pton(AF_INET, addr, &ipv4) != 1 &&
            inet_pton(AF_INET6, addr, &ipv6) != 1)
            return 0;
        if (!json_object_has_value_of_type(item, "port", JSONNumber)) return 0;
        value = json_object_get_number(item, "port");
        if (!redp2p_json_require_port(value, &port)) return 0;

        memset(&cand, 0, sizeof(cand));
        cand.type = type;
        if (inet_pton(AF_INET, addr, &ipv4) == 1) {
            inet_ntop(AF_INET, &ipv4, cand.addr, sizeof(cand.addr));
        } else {
            inet_ntop(AF_INET6, &ipv6, cand.addr, sizeof(cand.addr));
        }
        cand.port = port;
        cand.priority = redp2p_candidate_priority(&cand);

        duplicate = 0;
        for (j = 0; j < temp_count; j++) {
            if (redp2p_candidate_same_endpoint(&temp[j], &cand)) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        if (temp_count >= REDP2P_PEER_CANDIDATES_MAX) return 0;
        temp[temp_count++] = cand;
    }

    for (i = 1; i < (size_t)temp_count; i++) {
        redp2p_candidate_t item = temp[i];
        int j = i - 1;
        while (j >= 0 && redp2p_candidate_compare(&item, &temp[j]) < 0) {
            temp[j + 1] = temp[j];
            j--;
        }
        temp[j + 1] = item;
    }

    if (temp_count > 0) {
        memcpy(out, temp, (size_t)temp_count * sizeof(temp[0]));
    }
    *out_count = temp_count;
    return 1;
}

/**
 * Appends one candidate array to a JSON reply object.
 * @param obj   Reply object.
 * @param field Output array field name.
 * @param cands Candidate array.
 * @param n     Candidate count.
 * @return None.
 */
void redp2p_append_candidates(JSON_Object *obj,
    const char *field, const redp2p_candidate_t *cands, int n)
{
    JSON_Value *array_value;
    JSON_Array *array;
    int i;

    array_value = json_value_init_array();
    array = json_value_get_array(array_value);
    for (i = 0; i < n; i++) {
        JSON_Value *item_value;
        JSON_Object *item;

        item_value = json_value_init_object();
        item = json_value_get_object(item_value);
        json_object_set_string(item, "type",
            redp2p_candidate_type_name(cands[i].type));
        json_object_set_string(item, "addr", cands[i].addr);
        json_object_set_number(item, "port", (double)cands[i].port);
        json_array_append_value(array, item_value);
    }
    json_object_set_value(obj, field, array_value);
}

#ifdef REDP2P_TEST_RANDOM
/**
 * Generates one register challenge nonce through the test-visible path.
 * @param hex Output hex nonce.
 * @return 1 on success, 0 on error.
 */
int redp2p_test_pow_nonce(char hex[17]) {
    unsigned char nonce[8];

    if (redp2p_fill_random(nonce, sizeof(nonce)) != 0) return 0;
    return redp2p_hex_encode(nonce, sizeof(nonce), hex, 17);
}
#endif

#ifdef REDP2P_TEST_RANDOM
/**
 * Generates one UDP session identifier through the test-visible path.
 * @param hex Output hex session identifier.
 * @return 1 on success, 0 on error.
 */
int redp2p_test_udp_session_id(char hex[17]) {
    unsigned char session_id[8];

    if (redp2p_fill_random(session_id, sizeof(session_id)) != 0) return 0;
    return redp2p_hex_encode(session_id, sizeof(session_id), hex, 17);
}
#endif
