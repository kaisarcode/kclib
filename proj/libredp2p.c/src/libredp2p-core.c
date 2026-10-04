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

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/**
 * Rotates a 32-bit word right by one bounded amount.
 * @param value Input word.
 * @param bits Rotation count.
 * @return Rotated word.
 */
static uint32_t redp2p_rotr32(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32U - bits));
}

/**
 * Performs one SHA-256 compression round over the context block.
 * @param ctx Hash context.
 * @return None.
 */
static void redp2p_sha256_transform(redp2p_sha256_t *ctx)
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
        0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
        0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
        0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
        0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
        0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;

    for (i = 0; i < 16; i++) {
        const unsigned char *p = ctx->buf + (size_t)i * 4;

        w[i] = ((uint32_t)p[0] << 24) |
            ((uint32_t)p[1] << 16) |
            ((uint32_t)p[2] << 8) |
            (uint32_t)p[3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0;
        uint32_t s1;

        s0 = redp2p_rotr32(w[i - 15], 7) ^
            redp2p_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        s1 = redp2p_rotr32(w[i - 2], 17) ^
            redp2p_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1;
        uint32_t ch;
        uint32_t temp1;
        uint32_t s0;
        uint32_t maj;
        uint32_t temp2;

        s1 = redp2p_rotr32(e, 6) ^ redp2p_rotr32(e, 11) ^
            redp2p_rotr32(e, 25);
        ch = (e & f) ^ ((~e) & g);
        temp1 = h + s1 + ch + k[i] + w[i];
        s0 = redp2p_rotr32(a, 2) ^ redp2p_rotr32(a, 13) ^
            redp2p_rotr32(a, 22);
        maj = (a & b) ^ (a & c) ^ (b & c);
        temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/**
 * Sha256 init.
 * @return Status code.
 */
void redp2p_sha256_init(redp2p_sha256_t *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
}

/**
 * Sha256 update.
 * @return Status code.
 */
void redp2p_sha256_update(redp2p_sha256_t *ctx, const unsigned char *data,
    size_t len)
{
    size_t used;

    if (!len) return;
    used = (size_t)(ctx->count & 63U);
    ctx->count += len;
    if (used) {
        size_t take = sizeof(ctx->buf) - used;

        if (take > len) take = len;
        memcpy(ctx->buf + used, data, take);
        used += take;
        data += take;
        len -= take;
        if (used == sizeof(ctx->buf)) redp2p_sha256_transform(ctx);
    }
    while (len >= sizeof(ctx->buf)) {
        memcpy(ctx->buf, data, sizeof(ctx->buf));
        redp2p_sha256_transform(ctx);
        data += sizeof(ctx->buf);
        len -= sizeof(ctx->buf);
    }
    if (len) memcpy(ctx->buf, data, len);
}

/**
 * Sha256 final.
 * @return Status code.
 */
void redp2p_sha256_final(redp2p_sha256_t *ctx, unsigned char hash[32])
{
    uint64_t bits;
    size_t used;
    size_t i;

    bits = ctx->count * 8U;
    used = (size_t)(ctx->count & 63U);
    ctx->buf[used++] = 0x80;
    if (used > 56) {
        memset(ctx->buf + used, 0, sizeof(ctx->buf) - used);
        redp2p_sha256_transform(ctx);
        used = 0;
    }
    memset(ctx->buf + used, 0, 56 - used);
    for (i = 0; i < 8; i++)
        ctx->buf[56 + i] = (unsigned char)(bits >> (56 - i * 8));
    redp2p_sha256_transform(ctx);
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
 * @param key_len Key byte count.
 * @param msg     Input message bytes.
 * @param msg_len Input message length.
 * @param hash    Output digest buffer.
 * @return None.
 */
void redp2p_hmac_sha256_bytes(const unsigned char *key, size_t key_len,
    const unsigned char *msg, size_t msg_len, unsigned char hash[32])
{
    unsigned char key_block[64];
    unsigned char ipad[64];
    unsigned char opad[64];
    unsigned char inner[32];
    redp2p_sha256_t sha;
    size_t i;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        redp2p_sha256_init(&sha);
        redp2p_sha256_update(&sha, key, key_len);
        redp2p_sha256_final(&sha, key_block);
    } else if (key_len > 0) {
        memcpy(key_block, key, key_len);
    }
    for (i = 0; i < sizeof(key_block); i++) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }
    redp2p_sha256_init(&sha);
    redp2p_sha256_update(&sha, ipad, sizeof(ipad));
    redp2p_sha256_update(&sha, msg, msg_len);
    redp2p_sha256_final(&sha, inner);
    redp2p_sha256_init(&sha);
    redp2p_sha256_update(&sha, opad, sizeof(opad));
    redp2p_sha256_update(&sha, inner, sizeof(inner));
    redp2p_sha256_final(&sha, hash);
    crypto_wipe(&sha, sizeof(sha));
    crypto_wipe(key_block, sizeof(key_block));
    crypto_wipe(ipad, sizeof(ipad));
    crypto_wipe(opad, sizeof(opad));
    crypto_wipe(inner, sizeof(inner));
}

/**
 * Encodes one uint64 in fixed-width big-endian form.
 * @param out Output 8-byte buffer.
 * @param value Value to encode.
 * @return None.
 */
static void redp2p_store_u64_be(unsigned char out[8], uint64_t value)
{
    size_t i;

    for (i = 0; i < 8; i++)
        out[i] = (unsigned char)(value >> (56 - i * 8));
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
    if (!out || !used || (!data && len > 0) || *used > cap ||
        len > cap - *used)
        return 0;
    if (len > 0) memcpy(out + *used, data, len);
    *used += len;
    return 1;
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
    unsigned char encoded[8];

    redp2p_store_u64_be(encoded, value);
    return redp2p_append_bytes(out, cap, used, encoded, sizeof(encoded));
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
    static const unsigned char domain[] = "REDP2P-ACCESS";
    unsigned char canonical[sizeof(domain) - 1 + 384];
    unsigned char hash[32];
    size_t used;

    if (!password || !password[0] || !registration_message || !proof ||
        registration_message_len > 384)
        return 0;
    memcpy(canonical, domain, sizeof(domain) - 1);
    memcpy(canonical + sizeof(domain) - 1, registration_message,
        registration_message_len);
    used = sizeof(domain) - 1 + registration_message_len;
    redp2p_hmac_sha256_bytes((const unsigned char *)password,
        strlen(password), canonical, used, hash);
    if (!redp2p_hex_encode(hash, sizeof(hash), proof, 65)) {
        crypto_wipe(canonical, sizeof(canonical));
        crypto_wipe(hash, sizeof(hash));
        return 0;
    }
    crypto_wipe(canonical, sizeof(canonical));
    crypto_wipe(hash, sizeof(hash));
    return 1;
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
int redp2p_control_proof(const char *key, const char *op, const char *id,
    uint64_t sequence, int proto, unsigned short udp_port,
    const redp2p_candidate_t *candidates, int n_candidates, char proof[65])
{
    static const unsigned char domain[] = "REDP2P-CONTROL";
    unsigned char canonical[1024];
    unsigned char hash[32];
    unsigned char number[8];
    size_t used;
    int i;

    if (!key || !op || !id || !proof || sequence == 0 ||
        n_candidates < 0 || n_candidates > REDP2P_PEER_CANDIDATES_MAX ||
        (n_candidates > 0 && !candidates))
        return 0;
    used = 0;
    if (!redp2p_append_bytes(canonical, sizeof(canonical), &used, domain,
        sizeof(domain) - 1) ||
        !redp2p_append_bytes(canonical, sizeof(canonical), &used,
            (const unsigned char *)op, strlen(op) + 1) ||
        !redp2p_append_bytes(canonical, sizeof(canonical), &used,
            (const unsigned char *)id, strlen(id) + 1) ||
        !redp2p_append_u64_be(canonical, sizeof(canonical), &used, sequence))
        return 0;
    if (strcmp(op, "heartbeat") == 0) {
        number[0] = (unsigned char)proto;
        number[1] = (unsigned char)(udp_port >> 8);
        number[2] = (unsigned char)udp_port;
        number[3] = (unsigned char)n_candidates;
        if (!redp2p_append_bytes(canonical, sizeof(canonical), &used, number, 4))
            return 0;
        for (i = 0; i < n_candidates; i++) {
            size_t addr_len = strlen(candidates[i].addr);

            number[0] = (unsigned char)candidates[i].type;
            number[1] = (unsigned char)(candidates[i].port >> 8);
            number[2] = (unsigned char)candidates[i].port;
            number[3] = (unsigned char)(candidates[i].priority >> 24);
            number[4] = (unsigned char)(candidates[i].priority >> 16);
            number[5] = (unsigned char)(candidates[i].priority >> 8);
            number[6] = (unsigned char)candidates[i].priority;
            number[7] = (unsigned char)addr_len;
            if (addr_len > 255 ||
                !redp2p_append_bytes(canonical, sizeof(canonical), &used,
                    number, sizeof(number)) ||
                !redp2p_append_bytes(canonical, sizeof(canonical), &used,
                    (const unsigned char *)candidates[i].addr, addr_len))
                return 0;
        }
    }
    redp2p_hmac_sha256_bytes((const unsigned char *)key, strlen(key),
        canonical, used, hash);
    if (!redp2p_hex_encode(hash, sizeof(hash), proof, 65)) {
        crypto_wipe(canonical, sizeof(canonical));
        crypto_wipe(hash, sizeof(hash));
        return 0;
    }
    crypto_wipe(canonical, sizeof(canonical));
    crypto_wipe(hash, sizeof(hash));
    return 1;
}

/**
 * Counts leading zero bits in one digest.
 * @param hash Input digest bytes.
 * @return Count of leading zero bits.
 */
int redp2p_count_leading_zero_bits(const unsigned char hash[32])
{
    int bits;
    int i;

    bits = 0;
    for (i = 0; i < 32; i++) {
        unsigned char value;
        int j;

        value = hash[i];
        if (value == 0) {
            bits += 8;
            continue;
        }
        for (j = 7; j >= 0; j--) {
            if ((value >> j) & 1U) return bits;
            bits++;
        }
    }
    return bits;
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
    static const unsigned char domain[] = "REDP2P-POW";
    unsigned char message[128];
    size_t used;

    if (!nonce || !id || !id[0] || !hash) return 0;
    used = 0;
    if (!redp2p_append_bytes(message, sizeof(message), &used, domain,
        sizeof(domain) - 1) ||
        !redp2p_append_bytes(message, sizeof(message), &used, nonce, 32) ||
        !redp2p_append_u64_be(message, sizeof(message), &used, issued_at) ||
        !redp2p_append_u64_be(message, sizeof(message), &used, expires_at) ||
        !redp2p_append_bytes(message, sizeof(message), &used,
            (const unsigned char *)id, strlen(id)) ||
        !redp2p_append_u64_be(message, sizeof(message), &used, solution))
        return 0;
    {
        redp2p_sha256_t sha;

        redp2p_sha256_init(&sha);
        redp2p_sha256_update(&sha, message, used);
        redp2p_sha256_final(&sha, hash);
        crypto_wipe(&sha, sizeof(sha));
    }
    crypto_wipe(message, sizeof(message));
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
    static const unsigned char domain[] = "REDP2P-REGISTER";
    unsigned char number[8];
    size_t used;
    size_t id_len;
    size_t secret_len;
    int i;

    if (!nonce || !id || !secret || !out || !out_len ||
        (proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP) ||
        udp_port == 0 || n_candidates < 0 ||
        n_candidates > REDP2P_PEER_CANDIDATES_MAX ||
        (n_candidates > 0 && !candidates))
        return 0;
    id_len = strlen(id);
    secret_len = strlen(secret);
    if (id_len == 0 || id_len > REDP2P_ID_MAX ||
        secret_len != REDP2P_KEY_SZ)
        return 0;
    used = 0;
    if (!redp2p_append_bytes(out, 384, &used, domain, sizeof(domain) - 1) ||
        !redp2p_append_bytes(out, 384, &used, nonce, 32) ||
        !redp2p_append_u64_be(out, 384, &used, issued_at) ||
        !redp2p_append_u64_be(out, 384, &used, expires_at) ||
        !redp2p_append_bytes(out, 384, &used,
            (const unsigned char *)id, id_len + 1) ||
        !redp2p_append_bytes(out, 384, &used,
            (const unsigned char *)secret, secret_len + 1))
        return 0;
    number[0] = (unsigned char)proto;
    number[1] = (unsigned char)(udp_port >> 8);
    number[2] = (unsigned char)udp_port;
    number[3] = (unsigned char)n_candidates;
    if (!redp2p_append_bytes(out, 384, &used, number, 4))
        return 0;
    for (i = 0; i < n_candidates; i++) {
        size_t addr_len = strlen(candidates[i].addr);

        number[0] = (unsigned char)candidates[i].type;
        number[1] = (unsigned char)(candidates[i].port >> 8);
        number[2] = (unsigned char)candidates[i].port;
        number[3] = (unsigned char)(candidates[i].priority >> 24);
        number[4] = (unsigned char)(candidates[i].priority >> 16);
        number[5] = (unsigned char)(candidates[i].priority >> 8);
        number[6] = (unsigned char)candidates[i].priority;
        number[7] = (unsigned char)addr_len;
        if (addr_len > 255 ||
            !redp2p_append_bytes(out, 384, &used, number, sizeof(number)) ||
            !redp2p_append_bytes(out, 384, &used,
                (const unsigned char *)candidates[i].addr, addr_len))
            return 0;
    }
    if (!redp2p_append_u64_be(out, 384, &used, solution)) return 0;
    *out_len = used;
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
int redp2p_registration_index_key(
    const unsigned char challenge_key[32], const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, unsigned char index_skey[32],
    unsigned char index_pkey[32])
{
    static const unsigned char domain[] = "REDP2P-KEY";
    unsigned char message[sizeof(domain) - 1 + 32 + 8 + 8];
    size_t used;

    if (!challenge_key || !nonce || !index_skey || !index_pkey) return 0;
    used = 0;
    if (!redp2p_append_bytes(message, sizeof(message), &used, domain,
        sizeof(domain) - 1) ||
        !redp2p_append_bytes(message, sizeof(message), &used, nonce, 32) ||
        !redp2p_append_u64_be(message, sizeof(message), &used, issued_at) ||
        !redp2p_append_u64_be(message, sizeof(message), &used, expires_at))
        return 0;
    redp2p_hmac_sha256_bytes(challenge_key, 32, message, used, index_skey);
    crypto_x25519_public_key(index_pkey, index_skey);
    crypto_wipe(message, sizeof(message));
    return 1;
}

/**
 * Builds the authenticated registration-secret encryption key.
 * @param shared_secret X25519 shared secret.
 * @param nonce Registration challenge nonce.
 * @param key Output encryption key.
 * @return None.
 */
static void redp2p_registration_secret_key(
    const unsigned char shared_secret[32], const unsigned char nonce[32],
    unsigned char key[32])
{
    static const unsigned char domain[] = "REDP2P-SECRET";
    unsigned char message[sizeof(domain) - 1 + 32];
    redp2p_sha256_t sha;

    memcpy(message, domain, sizeof(domain) - 1);
    memcpy(message + sizeof(domain) - 1, nonce, 32);
    redp2p_sha256_init(&sha);
    redp2p_sha256_update(&sha, shared_secret, 32);
    redp2p_sha256_update(&sha, message, sizeof(message));
    redp2p_sha256_final(&sha, key);
    crypto_wipe(&sha, sizeof(sha));
    crypto_wipe(message, sizeof(message));
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
int redp2p_registration_secret_encrypt(
    const unsigned char publisher_skey[32], const unsigned char index_pkey[32],
    const unsigned char nonce[32], const unsigned char secret[REDP2P_KEY_SZ],
    unsigned char publisher_pkey[32], unsigned char encrypted_secret[32])
{
    unsigned char shared[32];
    unsigned char key[32];
    unsigned char mac[16];

    if (!publisher_skey || !index_pkey || !nonce || !secret ||
        !publisher_pkey || !encrypted_secret)
        return 0;
    crypto_x25519_public_key(publisher_pkey, publisher_skey);
    if (crypto_x25519(shared, publisher_skey, index_pkey) != 0) {
        crypto_wipe(shared, sizeof(shared));
        return 0;
    }
    redp2p_registration_secret_key(shared, nonce, key);
    crypto_lock(mac, encrypted_secret, key, nonce,
        secret, REDP2P_KEY_SZ);
    memcpy(encrypted_secret + REDP2P_KEY_SZ, mac, sizeof(mac));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(key, sizeof(key));
    crypto_wipe(mac, sizeof(mac));
    return 1;
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
int redp2p_registration_secret_decrypt(
    const unsigned char challenge_key[32], const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at,
    const unsigned char publisher_pkey[32],
    const unsigned char encrypted_secret[32],
    unsigned char secret[REDP2P_KEY_SZ])
{
    unsigned char index_skey[32];
    unsigned char index_pkey[32];
    unsigned char shared[32];
    unsigned char key[32];
    int result;

    if (!challenge_key || !nonce || !publisher_pkey || !encrypted_secret ||
        !secret)
        return 0;
    if (!redp2p_registration_index_key(challenge_key, nonce, issued_at,
        expires_at, index_skey, index_pkey))
        return 0;
    if (crypto_x25519(shared, index_skey, publisher_pkey) != 0) {
        crypto_wipe(index_skey, sizeof(index_skey));
        crypto_wipe(index_pkey, sizeof(index_pkey));
        crypto_wipe(shared, sizeof(shared));
        return 0;
    }
    redp2p_registration_secret_key(shared, nonce, key);
    result = crypto_unlock(secret, key, nonce,
        encrypted_secret + REDP2P_KEY_SZ, encrypted_secret,
        REDP2P_KEY_SZ) == 0;
    crypto_wipe(index_skey, sizeof(index_skey));
    crypto_wipe(index_pkey, sizeof(index_pkey));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(key, sizeof(key));
    return result;
}

/**
 * Encodes bytes as lowercase hexadecimal.
 * @param hash Input bytes.
 * @param hash_len Byte count.
 * @param out Output text buffer.
 * @param out_cap Output capacity.
 * @return 1 on success, 0 on invalid input.
 */
int redp2p_hex_encode(const unsigned char *hash, size_t hash_len,
    char *out, size_t out_cap)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (!hash || !out || out_cap < hash_len * 2 + 1) return 0;
    for (i = 0; i < hash_len; i++) {
        out[i * 2] = hex[hash[i] >> 4];
        out[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    out[hash_len * 2] = '\0';
    return 1;
}

/**
 * Decodes one hex nibble.
 * @return Nibble value, or -1 on error.
 */
int redp2p_hex_decode_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Decodes a fixed-size hex string.
 * @return 1 on success, 0 on error.
 */
int redp2p_hex_decode(const char *hex, unsigned char *out, size_t out_len)
{
    size_t i;

    if (!hex || !out || strlen(hex) != out_len * 2) return 0;
    for (i = 0; i < out_len; i++) {
        int hi = redp2p_hex_decode_nibble(hex[i * 2]);
        int lo = redp2p_hex_decode_nibble(hex[i * 2 + 1]);

        if (hi < 0 || lo < 0) return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

/**
 * Parses one strict unsigned decimal integer.
 * Summary: Rejects NULL, empty, leading/trailing garbage, signs, and overflow.
 * @param text Input text to parse.
 * @param min Inclusive lower bound.
 * @param max Inclusive upper bound.
 * @param out Output parsed value.
 * @return 1 on valid parse within bounds, 0 otherwise.
 */
int redp2p_parse_u(const char *text, long min, long max, long *out)
{
    char *end;
    unsigned long value;

    if (!text || !text[0] || text[0] == '+' || text[0] == '-' ||
        min < 0 || max < min || !out)
        return 0;
    errno = 0;
    end = NULL;
    value = strtoul(text, &end, 10);
    if (errno == ERANGE || !end || *end != '\0' ||
        value > (unsigned long)LONG_MAX ||
        (long)value < min || (long)value > max)
        return 0;
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
int redp2p_ascii_casecmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return (int)ca - (int)cb;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/**
 * Returns whether one bounded identifier is protocol-valid.
 * @param id Identifier text.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_valid_id(const char *id)
{
    size_t len;
    size_t i;

    if (!id) return 0;
    len = strlen(id);
    if (len == 0 || len > REDP2P_ID_MAX) return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)id[i];

        if (!(isalnum(c) || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/**
 * Returns whether one password token fits the admission protocol.
 * @param pass Password text.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_valid_pass_token(const char *pass)
{
    size_t len;

    if (!pass) return 0;
    len = strlen(pass);
    return len <= REDP2P_PASS_MAX;
}

/**
 * Reports whether a token is lowercase or uppercase hexadecimal.
 * @param text Input token.
 * @param len Required token length.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_hex_token(const char *text, size_t len)
{
    size_t i;

    if (!text || strlen(text) != len) return 0;
    for (i = 0; i < len; i++) {
        if (redp2p_hex_decode_nibble(text[i]) < 0) return 0;
    }
    return 1;
}

/**
 * Reports whether a session token is bounded and alphanumeric.
 * @param text Input token.
 * @return 1 when valid, 0 otherwise.
 */
int redp2p_is_session_token(const char *text)
{
    size_t len;
    size_t i;

    if (!text) return 0;
    len = strlen(text);
    if (len == 0 || len > REDP2P_CTRL_SESSION_MAX) return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if (!(isalnum(c) || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/**
 * Copies one colon-delimited field from a cursor.
 * @param cursor Input cursor updated after the field.
 * @param out Output field buffer.
 * @param cap Output field capacity.
 * @param delim Required delimiter or NUL for final field.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_parse_field(const char **cursor, char *out, size_t cap, char delim)
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
 * Returns a monotonic millisecond timestamp.
 * @return Monotonic milliseconds.
 */
uint64_t redp2p_now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL +
        (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

/**
 * Returns a monotonic second timestamp.
 * @return Monotonic seconds.
 */
uint64_t redp2p_now_s(void)
{
    return redp2p_now_ms() / 1000ULL;
}

/**
 * Fills a buffer with secure random bytes.
 * @return 0 on success, -1 on error.
 */
int redp2p_fill_random(unsigned char *buf, size_t len)
{
    if (!buf && len > 0) return -1;
    if (len == 0) return 0;
#ifdef _WIN32
    if (BCryptGenRandom(NULL, buf, (ULONG)len,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return -1;
    return 0;
#else
    {
        int fd;
        size_t total;

        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) return -1;
        total = 0;
        while (total < len) {
            ssize_t got = read(fd, buf + total, len - total);

            if (got > 0) {
                total += (size_t)got;
                continue;
            }
            if (got < 0 && errno == EINTR) continue;
            close(fd);
            return -1;
        }
        close(fd);
        return 0;
    }
#endif
}

/**
 * Platform init.
 * @return 0 on success, -1 on error.
 */
int redp2p_platform_init(void)
{
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

/**
 * Platform cleanup.
 * @return None.
 */
void redp2p_platform_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Sets one socket nonblocking.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_nonblock(redp2p_fd_t fd)
{
#ifdef _WIN32
    u_long mode = 1;

    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags;

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;
    return 0;
#endif
}

/**
 * Waits for socket readiness without FD_SETSIZE limits.
 * @return Ready descriptor count, 0 on timeout, or -1 on error.
 */
int redp2p_poll_wait(redp2p_pollfd_t *fds, size_t count, int timeout_ms)
{
#ifdef _WIN32
    return WSAPoll(fds, (ULONG)count, timeout_ms);
#else
    return poll(fds, (nfds_t)count, timeout_ms);
#endif
}

/**
 * Reports whether one poll entry should be processed as readable.
 * @param fd Poll descriptor.
 * @return 1 when readable or closed, 0 otherwise.
 */
int redp2p_poll_readable(const redp2p_pollfd_t *fd)
{
    short terminal;

    if (!fd) return 0;
    terminal = POLLERR | POLLHUP | POLLNVAL;
    return (fd->revents & (REDP2P_POLLIN | terminal)) != 0;
}

/**
 * Returns the UDP or TCP port from a stored socket address.
 * @param addr Stored socket address.
 * @return Host-order port, or 0 for unsupported families.
 */
unsigned short redp2p_sockaddr_port(const struct sockaddr_storage *addr)
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
        const struct sockaddr_in *sa = (const struct sockaddr_in *)a;
        const struct sockaddr_in *sb = (const struct sockaddr_in *)b;

        return sa->sin_port == sb->sin_port &&
            sa->sin_addr.s_addr == sb->sin_addr.s_addr;
    }
    if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6 *)a;
        const struct sockaddr_in6 *sb6 = (const struct sockaddr_in6 *)b;

        return sa6->sin6_port == sb6->sin6_port &&
            memcmp(&sa6->sin6_addr, &sb6->sin6_addr,
                sizeof(sa6->sin6_addr)) == 0;
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
 * @param host Raw network-order address bytes.
 * @param port Candidate UDP port.
 * @return 1 when the endpoint is acceptable, 0 otherwise.
 */
int redp2p_candidate_dest_allowed(int family, const void *host,
    unsigned short port)
{
    if (!host || port == 0) return 0;
    if (family == AF_INET) {
        uint32_t ip;

        memcpy(&ip, host, sizeof(ip));
        ip = ntohl(ip);
        if (ip == 0 || ip == 0xffffffffU ||
            (ip & 0xff000000U) == 0x7f000000U ||
            (ip & 0xf0000000U) == 0xe0000000U ||
            (ip & 0xff000000U) == 0x00000000U)
            return 0;
        return 1;
    }
    if (family == AF_INET6) {
        const unsigned char *bytes = (const unsigned char *)host;
        static const unsigned char zero[16] = {0};
        static const unsigned char loopback[16] =
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

        if (memcmp(bytes, zero, 16) == 0 ||
            memcmp(bytes, loopback, 16) == 0 || bytes[0] == 0xff)
            return 0;
        if (memcmp(bytes, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12) == 0)
            return redp2p_candidate_dest_allowed(AF_INET, bytes + 12, port);
        return 1;
    }
    return 0;
}

/**
 * Sock read.
 * @return 0 on success, -1 on error.
 */
int redp2p_sock_read(redp2p_fd_t fd, char *buf, int len)
{
    int n;

    n = (int)recv(fd, buf, len, 0);
    if (n <= 0) return -1;
    return n;
}

/**
 * Writes one socket chunk without raising SIGPIPE where supported.
 * @param fd Socket descriptor.
 * @param buf Source bytes.
 * @param len Source byte count.
 * @return Bytes written, or -1 on error.
 */
int redp2p_sock_write(redp2p_fd_t fd, const char *buf, int len)
{
#ifdef MSG_NOSIGNAL
    return (int)send(fd, buf, len, MSG_NOSIGNAL);
#else
    return (int)send(fd, buf, len, 0);
#endif
}

/**
 * Write all.
 * @return 0 on success, -1 on error.
 */
int redp2p_write_all(redp2p_fd_t fd, const char *buf, int len)
{
    int offset;

    offset = 0;
    while (offset < len) {
        int n = redp2p_sock_write(fd, buf + offset, len - offset);

        if (n <= 0) return -1;
        offset += n;
    }
    return 0;
}

/**
 * Records one per-context detail error message.
 * @return None.
 */
void redp2p_set_error(redp2p_t *ctx, const char *fmt, ...)
{
    va_list ap;

    if (!ctx) return;
    if (!fmt) {
        ctx->err_buf[0] = '\0';
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->err_buf, sizeof(ctx->err_buf), fmt, ap);
    va_end(ap);
}

/**
 * Lock.
 * @return Status code.
 */
void redp2p_lock(redp2p_t *ctx)
{
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
void redp2p_unlock(redp2p_t *ctx)
{
#ifdef _WIN32
    LeaveCriticalSection(&ctx->mutex);
#else
    pthread_mutex_unlock(&ctx->mutex);
#endif
}

/**
 * Initializes one context with protocol defaults.
 * @param out Output context pointer.
 * @return REDP2P_OK or REDP2P_ERROR.
 */
int redp2p_context_create(redp2p_t **out)
{
    redp2p_t *ctx;

    if (!out) return REDP2P_EINVAL;
    *out = NULL;
    ctx = (redp2p_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return REDP2P_ERROR;
#ifdef _WIN32
    InitializeCriticalSection(&ctx->mutex);
#else
    if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
        free(ctx);
        return REDP2P_ERROR;
    }
#endif
    ctx->pow_bits = 0;
    ctx->sweep = REDP2P_SWEEP_DEFAULT;
    ctx->heartbeat_s = REDP2P_HEARTBEAT_S;
    ctx->punch_poll_ms = 100;
    ctx->pending_ttl_s = 30;
    ctx->prune_interval_s = 5;
    ctx->etimeout_sec = 30;
    ctx->max_consumers_per_publisher = REDP2P_MAX_CONSUMERS_PER_PUBLISHER;
    ctx->proto = REDP2P_PROTO_TCP;
    ctx->direct_mode = 1;
    ctx->wake_write_fd = REDP2P_FD_INVALID;
    atomic_init(&ctx->stop_requested, 0);
    atomic_init(&ctx->ready_state, 0);
    atomic_init(&ctx->ready_status, REDP2P_OK);
    atomic_init(&ctx->channel_state, 0);
    atomic_init(&ctx->channel_status, REDP2P_OK);
    *out = ctx;
    return REDP2P_OK;
}

/**
 * Destroys one context and wipes secret-bearing state.
 * @param ctx Context to destroy.
 * @return REDP2P_OK or REDP2P_EINVAL.
 */
int redp2p_context_destroy(redp2p_t *ctx)
{
    size_t i;

    if (!ctx) return REDP2P_EINVAL;
    for (i = 0; i < REDP2P_TURN_ALLOCATIONS_MAX; i++)
        redp2p_turn_allocation_clear(ctx, &ctx->turn_allocations[i]);
    free(ctx->peers);
    free(ctx->rate_sources);
    free(ctx->vips);
    free(ctx->conns);
    free(ctx->pending_calls);
#ifdef _WIN32
    DeleteCriticalSection(&ctx->mutex);
#else
    pthread_mutex_destroy(&ctx->mutex);
#endif
    crypto_wipe(ctx, sizeof(*ctx));
    free(ctx);
    return REDP2P_OK;
}

/**
 * Requests stop for a running role.
 * @param ctx Context to stop.
 * @return REDP2P_OK or REDP2P_EINVAL.
 */
int redp2p_context_request_stop(redp2p_t *ctx)
{
    unsigned char byte;
    redp2p_fd_t wake_fd;

    if (!ctx) return REDP2P_EINVAL;
    atomic_store(&ctx->stop_requested, 1);
    wake_fd = ctx->wake_write_fd;
    if (!REDP2P_ISERR(wake_fd)) {
        byte = 1;
        (void)send(wake_fd, (const char *)&byte, 1, 0);
    }
    return REDP2P_OK;
}

/**
 * Returns whether one context requested stop.
 * @param ctx Context to inspect.
 * @return 1 when stop was requested, 0 otherwise.
 */
int redp2p_is_stop_requested(redp2p_t *ctx)
{
    return ctx ? atomic_load(&ctx->stop_requested) != 0 : 1;
}

/**
 * Returns the library version.
 * @return Packed semantic version.
 */
uint64_t redp2p_version(void)
{
    return KC_REDP2P_VERSION;
}

/**
 * Returns a stable error description.
 * @param code REDP2P status code.
 * @return Static description string.
 */
const char *redp2p_strerror(int code)
{
    switch (code) {
        case REDP2P_OK: return "success";
        case REDP2P_ERROR: return "general error";
        case REDP2P_ENET: return "network error";
        case REDP2P_ENOENT: return "not found";
        case REDP2P_ETIMEOUT: return "timeout";
        case REDP2P_EFULL: return "capacity reached";
        case REDP2P_EINVAL: return "invalid argument";
        case REDP2P_EPROTO: return "protocol error";
        case REDP2P_EAUTH: return "authentication failed";
        case REDP2P_EVERSION: return "version mismatch";
        case REDP2P_EPUNCH: return "peer punch failed";
        case REDP2P_EEXIST: return "already exists";
        case REDP2P_EUNSUPPORTED: return "unsupported";
        default: return "unknown error";
    }
}

/**
 * Returns context-local detail for the last operation.
 * @param ctx Context to inspect.
 * @return Stable context buffer, or an empty static string.
 */
const char *redp2p_get_error(redp2p_t *ctx)
{
    static const char empty[] = "";

    return ctx ? ctx->err_buf : empty;
}

/**
 * Creates the socket wakeup pair used by a running idx/pub/con loop.
 * @param ctx Runtime context.
 * @param read_fd Destination read socket.
 * @param write_fd Destination write socket.
 * @return REDP2P_OK on success or an error code.
 */
int redp2p_wake_open(redp2p_t *ctx,
    redp2p_fd_t *read_fd, redp2p_fd_t *write_fd)
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    redp2p_fd_t listener;
    redp2p_fd_t reader;
    redp2p_fd_t writer;

    if (!ctx || !read_fd || !write_fd) return REDP2P_EINVAL;
    *read_fd = REDP2P_FD_INVALID;
    *write_fd = REDP2P_FD_INVALID;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (REDP2P_ISERR(listener)) return REDP2P_ENET;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listener, 1) != 0)
    {
        REDP2P_FD_CLOSE(listener);
        return REDP2P_ENET;
    }
    addr_len = sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0) {
        REDP2P_FD_CLOSE(listener);
        return REDP2P_ENET;
    }
    writer = socket(AF_INET, SOCK_STREAM, 0);
    if (REDP2P_ISERR(writer)) {
        REDP2P_FD_CLOSE(listener);
        return REDP2P_ENET;
    }
    if (connect(writer, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        REDP2P_FD_CLOSE(writer);
        REDP2P_FD_CLOSE(listener);
        return REDP2P_ENET;
    }
    reader = accept(listener, NULL, NULL);
    REDP2P_FD_CLOSE(listener);
    if (REDP2P_ISERR(reader) ||
        redp2p_set_nonblock(reader) != 0 ||
        redp2p_set_nonblock(writer) != 0)
    {
        if (!REDP2P_ISERR(reader)) REDP2P_FD_CLOSE(reader);
        REDP2P_FD_CLOSE(writer);
        return REDP2P_ENET;
    }
    ctx->wake_write_fd = writer;
    *read_fd = reader;
    *write_fd = writer;
    return REDP2P_OK;
}

/**
 * Drains pending wake bytes from a nonblocking wake socket.
 * @param read_fd Wake read socket.
 * @return None.
 */
void redp2p_wake_drain(redp2p_fd_t read_fd)
{
    unsigned char buf[64];

    while (recv(read_fd, (char *)buf, sizeof(buf), 0) > 0) {}
}

/**
 * Detaches and closes one wakeup pair.
 * @param ctx Runtime context.
 * @param read_fd Wake read socket.
 * @param write_fd Wake write socket.
 * @return None.
 */
void redp2p_wake_close(redp2p_t *ctx,
    redp2p_fd_t read_fd, redp2p_fd_t write_fd)
{
    if (ctx && ctx->wake_write_fd == write_fd)
        ctx->wake_write_fd = REDP2P_FD_INVALID;
    if (!REDP2P_ISERR(read_fd)) REDP2P_FD_CLOSE(read_fd);
    if (!REDP2P_ISERR(write_fd)) REDP2P_FD_CLOSE(write_fd);
}

/**
 * Extracts and validates one fixed-width hexadecimal token field.
 * @param obj Request object.
 * @param field Field name.
 * @param out Output token.
 * @param out_cap Output token capacity.
 * @param hex_len Required hex length.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_json_require_hex(JSON_Object *obj, const char *field,
    char *out, size_t out_cap, size_t hex_len)
{
    const char *value;

    if (!obj || !field || !out || out_cap <= hex_len) return 0;
    value = json_object_get_string(obj, field);
    if (!value || !redp2p_is_hex_token(value, hex_len)) return 0;
    memcpy(out, value, hex_len + 1);
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

    if (!obj || !field || !out || out_cap <= hex_len) return 0;
    value = json_object_get_string(obj, field);
    if (!value || strlen(value) != hex_len) return 0;
    for (i = 0; i < hex_len; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
            (value[i] >= 'a' && value[i] <= 'f')))
            return 0;
    }
    memcpy(out, value, hex_len + 1);
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

    if (!obj || !field || !value ||
        !json_object_has_value_of_type(obj, field, JSONNumber))
        return 0;
    number = json_object_get_number(obj, field);
    if (!isfinite(number) || number < 0 ||
        number > 9007199254740991.0 || floor(number) != number)
        return 0;
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
    if (!port || !isfinite(value) || value < 1 || value > 65535 ||
        floor(value) != value)
        return 0;
    *port = (unsigned short)value;
    return 1;
}

/**
 * Returns the deterministic local priority for one candidate.
 * @param candidate Candidate to rank.
 * @return Lower priority values are attempted first.
 */
unsigned int redp2p_candidate_priority(const redp2p_candidate_t *candidate)
{
    if (!candidate) return UINT_MAX;
    switch (candidate->type) {
        case REDP2P_CAND_RELAY: return 0;
        case REDP2P_CAND_SRFLX: return 100;
        case REDP2P_CAND_OBSERVED: return 200;
        case REDP2P_CAND_HOST: return 300;
        default: return UINT_MAX;
    }
}

/**
 * Compares candidates by normalized priority and endpoint.
 * @param left Left candidate.
 * @param right Right candidate.
 * @return qsort comparison value.
 */
static int redp2p_candidate_compare(const void *left, const void *right)
{
    const redp2p_candidate_t *a = (const redp2p_candidate_t *)left;
    const redp2p_candidate_t *b = (const redp2p_candidate_t *)right;

    if (a->priority < b->priority) return -1;
    if (a->priority > b->priority) return 1;
    if (a->type < b->type) return -1;
    if (a->type > b->type) return 1;
    {
        int cmp = strcmp(a->addr, b->addr);

        if (cmp != 0) return cmp;
    }
    if (a->port < b->port) return -1;
    if (a->port > b->port) return 1;
    return 0;
}

/**
 * Normalizes candidate priority, removes duplicates, and sorts the list.
 * @param candidates Candidate array.
 * @param count Candidate count in and out.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_normalize_candidates(redp2p_candidate_t *candidates, int *count)
{
    int input_count;
    int output_count;
    int i;

    if (!candidates || !count || *count < 0 ||
        *count > REDP2P_PEER_CANDIDATES_MAX)
        return 0;
    input_count = *count;
    output_count = 0;
    for (i = 0; i < input_count; i++) {
        redp2p_candidate_t candidate;
        int duplicate;
        int j;

        candidate = candidates[i];
        if (candidate.type < REDP2P_CAND_HOST ||
            candidate.type > REDP2P_CAND_SRFLX ||
            !candidate.addr[0] || candidate.port == 0)
            return 0;
        candidate.priority = redp2p_candidate_priority(&candidate);
        duplicate = 0;
        for (j = 0; j < output_count; j++) {
            if (candidates[j].type == candidate.type &&
                candidates[j].port == candidate.port &&
                strcmp(candidates[j].addr, candidate.addr) == 0)
            {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;
        candidates[output_count++] = candidate;
    }
    qsort(candidates, (size_t)output_count, sizeof(*candidates),
        redp2p_candidate_compare);
    *count = output_count;
    return 1;
}

/**
 * Converts a candidate into a socket address.
 * @param candidate Candidate to convert.
 * @param out Output socket address.
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
 * Parses one bounded JSON candidate array into candidate records.
 * @param obj Request object.
 * @param field Array field name.
 * @param out Output candidate array.
 * @param out_count Output candidate count.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_parse_candidates(JSON_Object *obj, const char *field,
    redp2p_candidate_t *out, int *out_count)
{
    JSON_Array *array;
    size_t count;
    size_t i;

    if (!obj || !field || !out || !out_count ||
        !json_object_has_value_of_type(obj, field, JSONArray))
        return 0;
    array = json_object_get_array(obj, field);
    count = json_array_get_count(array);
    if (count > REDP2P_PEER_CANDIDATES_MAX) return 0;
    for (i = 0; i < count; i++) {
        JSON_Object *entry;
        const char *addr;
        double type;
        double port;
        double priority;

        entry = json_array_get_object(array, i);
        if (!entry ||
            !json_object_has_value_of_type(entry, "type", JSONNumber) ||
            !json_object_has_value_of_type(entry, "addr", JSONString) ||
            !json_object_has_value_of_type(entry, "port", JSONNumber) ||
            !json_object_has_value_of_type(entry, "priority", JSONNumber))
            return 0;
        type = json_object_get_number(entry, "type");
        port = json_object_get_number(entry, "port");
        priority = json_object_get_number(entry, "priority");
        addr = json_object_get_string(entry, "addr");
        if (!isfinite(type) || floor(type) != type ||
            type < REDP2P_CAND_HOST || type > REDP2P_CAND_SRFLX ||
            !addr || !addr[0] || strlen(addr) > REDP2P_ADDR_MAX ||
            !redp2p_json_require_port(port, &out[i].port) ||
            !isfinite(priority) || priority < 0 ||
            priority > UINT_MAX || floor(priority) != priority)
            return 0;
        out[i].type = (redp2p_candidate_type_t)(int)type;
        memcpy(out[i].addr, addr, strlen(addr) + 1);
        out[i].priority = (unsigned int)priority;
    }
    *out_count = (int)count;
    return redp2p_normalize_candidates(out, out_count);
}

/**
 * Appends one candidate array to a JSON reply object.
 * @param obj Reply object.
 * @param field Output array field name.
 * @param cands Candidate array.
 * @param n Candidate count.
 * @return None.
 */
void redp2p_append_candidates(JSON_Object *obj, const char *field,
    const redp2p_candidate_t *cands, int n)
{
    JSON_Value *array_value;
    JSON_Array *array;
    int i;

    array_value = json_value_init_array();
    if (!array_value) return;
    array = json_value_get_array(array_value);
    for (i = 0; i < n; i++) {
        JSON_Value *entry_value = json_value_init_object();
        JSON_Object *entry;

        if (!entry_value) continue;
        entry = json_value_get_object(entry_value);
        json_object_set_number(entry, "type", (double)cands[i].type);
        json_object_set_string(entry, "addr", cands[i].addr);
        json_object_set_number(entry, "port", (double)cands[i].port);
        json_object_set_number(entry, "priority", (double)cands[i].priority);
        json_array_append_value(array, entry_value);
    }
    json_object_set_value(obj, field, array_value);
}

/**
 * Sets publisher protocol.
 * @return Status code.
 */
int redp2p_pub_set_protocol(redp2p_t *ctx, int proto)
{
    if (!ctx || (proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP))
        return REDP2P_EINVAL;
    ctx->proto = proto;
    return REDP2P_OK;
}

/**
 * Set local port.
 * @return Status code.
 */
int redp2p_set_local_port(redp2p_t *ctx, unsigned short port)
{
    if (!ctx || port == 0) return REDP2P_EINVAL;
    ctx->bind_port = port;
    ctx->explicit_port = 1;
    return REDP2P_OK;
}

/**
 * Sets the registration admission password.
 * @return Status code.
 */
int redp2p_set_registration_pass(redp2p_t *ctx, const char *pass)
{
    if (!ctx || !pass || !redp2p_is_valid_pass_token(pass))
        return REDP2P_EINVAL;
    memcpy(ctx->pass, pass, strlen(pass) + 1);
    return REDP2P_OK;
}

/**
 * Sets the STUN server URL.
 * @return Status code.
 */
int redp2p_set_stun_server(redp2p_t *ctx, const char *url)
{
    if (!ctx || !url || strlen(url) >= sizeof(ctx->stun_url))
        return REDP2P_EINVAL;
    memcpy(ctx->stun_url, url, strlen(url) + 1);
    return REDP2P_OK;
}

/**
 * Sets the TURN server URL and credentials.
 * @return Status code.
 */
int redp2p_set_turn_server(redp2p_t *ctx, const char *url,
    const char *username, const char *password)
{
    if (!ctx || !url || !username || !password ||
        strlen(url) >= sizeof(ctx->turn_url) ||
        strlen(username) >= sizeof(ctx->turn_user) ||
        strlen(password) >= sizeof(ctx->turn_pass))
        return REDP2P_EINVAL;
    memcpy(ctx->turn_url, url, strlen(url) + 1);
    memcpy(ctx->turn_user, username, strlen(username) + 1);
    memcpy(ctx->turn_pass, password, strlen(password) + 1);
    return REDP2P_OK;
}

/**
 * Resolves one requested local port against context configuration.
 * @param ctx Context.
 * @param requested Requested role port.
 * @param effective Output effective port.
 * @return REDP2P_OK or REDP2P_EINVAL.
 */
int redp2p_resolve_port(redp2p_t *ctx, unsigned short requested,
    unsigned short *effective)
{
    if (!ctx || !effective) return REDP2P_EINVAL;
    if (ctx->explicit_port && requested != 0 && requested != ctx->bind_port)
        return REDP2P_EINVAL;
    *effective = ctx->explicit_port ? ctx->bind_port : requested;
    return REDP2P_OK;
}

/**
 * Sets the publisher stream fault simulation cadence.
 * @param ctx Context.
 * @param drop_every Drop each Nth KCP datagram, zero to disable.
 * @param reorder_every Delay each Nth KCP datagram, zero to disable.
 * @return REDP2P_OK or REDP2P_EINVAL.
 */
#ifdef REDP2P_TESTING
int redp2p_test_set_stream_faults(redp2p_t *ctx,
    int drop_every, int reorder_every)
{
    if (!ctx || drop_every < 0 || reorder_every < 0) return REDP2P_EINVAL;
    ctx->fault_drop_every = drop_every;
    ctx->fault_reorder_every = reorder_every;
    ctx->fault_drop_counter = 0;
    ctx->fault_reorder_counter = 0;
    return REDP2P_OK;
}
#endif

/**
 * Calculates one default public candidate priority.
 * @param type Candidate type.
 * @return Priority value.
 */
static unsigned int redp2p_candidate_default_priority(
    redp2p_candidate_type_t type)
{
    redp2p_candidate_t candidate;

    memset(&candidate, 0, sizeof(candidate));
    candidate.type = type;
    return redp2p_candidate_priority(&candidate);
}

/**
 * Parses one candidate object under the index destination policy.
 * @param entry Candidate JSON object.
 * @param out Output candidate.
 * @return 1 when valid and destination-safe, 0 otherwise.
 */
static int redp2p_index_parse_candidate(JSON_Object *entry,
    redp2p_candidate_t *out)
{
    const char *addr;
    double type_number;
    double priority_number;
    unsigned short port;
    struct in_addr v4;
    struct in6_addr v6;

    if (!entry || !out ||
        !json_object_has_value_of_type(entry, "type", JSONNumber) ||
        !json_object_has_value_of_type(entry, "addr", JSONString) ||
        !json_object_has_value_of_type(entry, "port", JSONNumber))
        return 0;
    type_number = json_object_get_number(entry, "type");
    addr = json_object_get_string(entry, "addr");
    if (!isfinite(type_number) || floor(type_number) != type_number ||
        type_number < REDP2P_CAND_HOST || type_number > REDP2P_CAND_SRFLX ||
        !addr || !addr[0] || strlen(addr) > REDP2P_ADDR_MAX ||
        !redp2p_json_require_port(json_object_get_number(entry, "port"),
            &port))
        return 0;
    if ((int)type_number == REDP2P_CAND_HOST) {
        if (inet_pton(AF_INET, addr, &v4) == 1) {
            if (!redp2p_candidate_dest_allowed(AF_INET, &v4, port)) return 0;
        } else if (inet_pton(AF_INET6, addr, &v6) == 1) {
            if (!redp2p_candidate_dest_allowed(AF_INET6, &v6, port)) return 0;
        } else {
            return 0;
        }
    } else if (inet_pton(AF_INET, addr, &v4) != 1 &&
        inet_pton(AF_INET6, addr, &v6) != 1)
    {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->type = (redp2p_candidate_type_t)(int)type_number;
    memcpy(out->addr, addr, strlen(addr) + 1);
    out->port = port;
    if (json_object_has_value_of_type(entry, "priority", JSONNumber)) {
        priority_number = json_object_get_number(entry, "priority");
        if (!isfinite(priority_number) || priority_number < 0 ||
            priority_number > UINT_MAX || floor(priority_number) !=
            priority_number)
            return 0;
        out->priority = (unsigned int)priority_number;
    } else {
        out->priority = redp2p_candidate_default_priority(out->type);
    }
    return 1;
}

/**
 * Parses one candidate request through the index destination policy.
 * @param obj Request object.
 * @param field Candidate array field.
 * @param out Output candidates.
 * @param out_count Output count.
 * @return 1 on success, 0 otherwise.
 */
static int redp2p_index_parse_request_candidates(JSON_Object *obj,
    const char *field, redp2p_candidate_t *out, int *out_count)
{
    JSON_Array *array;
    size_t count;
    size_t i;

    if (!obj || !field || !out || !out_count ||
        !json_object_has_value_of_type(obj, field, JSONArray))
        return 0;
    array = json_object_get_array(obj, field);
    count = json_array_get_count(array);
    if (count > REDP2P_PEER_CANDIDATES_MAX) return 0;
    for (i = 0; i < count; i++) {
        if (!redp2p_index_parse_candidate(json_array_get_object(array, i),
            &out[i]))
            return 0;
    }
    *out_count = (int)count;
    return redp2p_normalize_candidates(out, out_count);
}

#ifdef REDP2P_TESTING
/**
 * Parses one candidate request through the index destination policy.
 * @return 1 when accepted, 0 when rejected.
 */
int redp2p_test_index_parse_request_candidates(
    const char *json, redp2p_candidate_t *out, int *out_count)
{
    JSON_Value *value;
    JSON_Object *obj;
    int result;

    if (!json || !out || !out_count) return 0;
    value = json_parse_string(json);
    if (!value) return 0;
    obj = json_value_get_object(value);
    result = redp2p_index_parse_request_candidates(obj, "candidates",
        out, out_count);
    json_value_free(value);
    return result;
}
#endif
