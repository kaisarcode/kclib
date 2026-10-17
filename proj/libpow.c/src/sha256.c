/**
 * sha256.c - Portable SHA-256 proof-of-work backend.
 * Summary: Implements the self-contained SHA-256 backend.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "backend.h"

#include <stdlib.h>
#include <string.h>

#define KC_POW_SHA256_BLOCK_SIZE 64U
#define KC_POW_SHA256_DIGEST_SIZE 32U

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[KC_POW_SHA256_BLOCK_SIZE];
    size_t block_size;
} kc_pow_sha256_t;

typedef struct {
    const unsigned char *challenge;
    size_t challenge_size;
} kc_pow_sha256_backend_t;

/**
 * Rotates one 32-bit value right.
 * @param value Value to rotate.
 * @param bits Rotation count.
 * @return Rotated value.
 */
static uint32_t kc_pow_rotr32(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

/**
 * Loads one big-endian 32-bit value.
 * @param p Source bytes.
 * @return Decoded value.
 */
static uint32_t kc_pow_load32be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * Stores one big-endian 32-bit value.
 * @param p Destination bytes.
 * @param value Value to encode.
 * @return None.
 */
static void kc_pow_store32be(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

/**
 * Stores one big-endian 64-bit value.
 * @param p Destination bytes.
 * @param value Value to encode.
 * @return None.
 */
static void kc_pow_store64be(unsigned char *p, uint64_t value) {
    int i;
    for (i = 7; i >= 0; --i) {
        p[i] = (unsigned char)value;
        value >>= 8;
    }
}

/**
 * Processes one SHA-256 block.
 * @param ctx Hash context.
 * @param block Input block.
 * @return None.
 */
static void kc_pow_sha256_transform(kc_pow_sha256_t *ctx,
    const unsigned char block[KC_POW_SHA256_BLOCK_SIZE]) {
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,
        0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,
        0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,
        0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,
        0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,
        0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,
        0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,
        0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,
        0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16; ++i) w[i] = kc_pow_load32be(block + (i * 4U));
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = kc_pow_rotr32(w[i - 15], 7) ^
            kc_pow_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = kc_pow_rotr32(w[i - 2], 17) ^
            kc_pow_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; ++i) {
        uint32_t s1 = kc_pow_rotr32(e, 6) ^ kc_pow_rotr32(e, 11) ^
            kc_pow_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = kc_pow_rotr32(a, 2) ^ kc_pow_rotr32(a, 13) ^
            kc_pow_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

/**
 * Initializes a SHA-256 context.
 * @param ctx Context to initialize.
 * @return None.
 */
static void kc_pow_sha256_init(kc_pow_sha256_t *ctx) {
    static const uint32_t initial[8] = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bit_count = 0;
    ctx->block_size = 0;
}

/**
 * Updates a SHA-256 context with input bytes.
 * @param ctx Context to update.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return None.
 */
static void kc_pow_sha256_update(kc_pow_sha256_t *ctx,
    const unsigned char *data, size_t size) {
    while (size > 0) {
        size_t available = KC_POW_SHA256_BLOCK_SIZE - ctx->block_size;
        size_t take = size < available ? size : available;
        memcpy(ctx->block + ctx->block_size, data, take);
        ctx->block_size += take;
        data += take;
        size -= take;
        if (ctx->block_size == KC_POW_SHA256_BLOCK_SIZE) {
            kc_pow_sha256_transform(ctx, ctx->block);
            ctx->bit_count += 512U;
            ctx->block_size = 0;
        }
    }
}

/**
 * Finalizes a SHA-256 context.
 * @param ctx Context to finalize.
 * @param digest Output digest buffer.
 * @return None.
 */
static void kc_pow_sha256_final(kc_pow_sha256_t *ctx,
    unsigned char digest[KC_POW_SHA256_DIGEST_SIZE]) {
    uint64_t bits = ctx->bit_count + ((uint64_t)ctx->block_size * 8U);
    size_t i;
    ctx->block[ctx->block_size++] = 0x80U;
    if (ctx->block_size > 56U) {
        memset(ctx->block + ctx->block_size, 0,
            KC_POW_SHA256_BLOCK_SIZE - ctx->block_size);
        kc_pow_sha256_transform(ctx, ctx->block);
        ctx->block_size = 0;
    }
    memset(ctx->block + ctx->block_size, 0, 56U - ctx->block_size);
    kc_pow_store64be(ctx->block + 56U, bits);
    kc_pow_sha256_transform(ctx, ctx->block);
    for (i = 0; i < 8; ++i) kc_pow_store32be(digest + i * 4U, ctx->state[i]);
}

/**
 * Hashes two concatenated byte sequences with SHA-256.
 * @param first First byte sequence.
 * @param first_size First byte count.
 * @param second Second byte sequence.
 * @param second_size Second byte count.
 * @param digest Output 32-byte digest.
 * @return None.
 */
void kc_pow_sha256_bytes(const unsigned char *first, size_t first_size,
    const unsigned char *second, size_t second_size, unsigned char digest[32]) {
    kc_pow_sha256_t ctx;
    kc_pow_sha256_init(&ctx);
    kc_pow_sha256_update(&ctx, first, first_size);
    kc_pow_sha256_update(&ctx, second, second_size);
    kc_pow_sha256_final(&ctx, digest);
}

/**
 * Reports that the portable SHA-256 backend is available.
 * @return 1.
 */
static int kc_pow_sha256_available(void) {
    return 1;
}

/**
 * Opens one SHA-256 backend operation.
 * @param challenge Exact challenge bytes.
 * @param challenge_size Challenge byte count.
 * @param out_context Output backend context.
 * @return 0 on success, or -1 on invalid input or allocation failure.
 */
static int kc_pow_sha256_open(const unsigned char *challenge,
    size_t challenge_size, void **out_context) {
    kc_pow_sha256_backend_t *context;
    if (!challenge || !out_context) return -1;
    *out_context = NULL;
    context = (kc_pow_sha256_backend_t *)malloc(sizeof(*context));
    if (!context) return -1;
    context->challenge = challenge;
    context->challenge_size = challenge_size;
    *out_context = context;
    return 0;
}

/**
 * Computes one SHA-256 proof value.
 * @param context Backend context.
 * @param nonce Candidate nonce.
 * @param out_value Output digest prefix value.
 * @return 0 on success, or -1 on invalid input.
 */
static int kc_pow_sha256_hash(void *context, uint64_t nonce,
    uint64_t *out_value) {
    kc_pow_sha256_backend_t *backend = (kc_pow_sha256_backend_t *)context;
    unsigned char nonce_bytes[8];
    unsigned char digest[KC_POW_SHA256_DIGEST_SIZE];
    if (!backend || !out_value) return -1;
    kc_pow_store64be(nonce_bytes, nonce);
    kc_pow_sha256_bytes(backend->challenge, backend->challenge_size,
        nonce_bytes, sizeof(nonce_bytes), digest);
    *out_value = ((uint64_t)digest[0] << 56) |
        ((uint64_t)digest[1] << 48) | ((uint64_t)digest[2] << 40) |
        ((uint64_t)digest[3] << 32) | ((uint64_t)digest[4] << 24) |
        ((uint64_t)digest[5] << 16) | ((uint64_t)digest[6] << 8) |
        (uint64_t)digest[7];
    return 0;
}

/**
 * Closes one SHA-256 backend operation.
 * @param context Backend context.
 * @return None.
 */
static void kc_pow_sha256_close(void *context) {
    free(context);
}

const kc_pow_backend_impl_t kc_pow_sha256_backend = {
    KC_POW_BACKEND_SHA256,
    "sha256",
    kc_pow_sha256_available,
    kc_pow_sha256_open,
    kc_pow_sha256_hash,
    kc_pow_sha256_close
};
