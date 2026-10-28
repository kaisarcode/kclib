/**
 * sha256.c - Portable SHA-256 file verification.
 * Summary: Calculate artifact digests without external libraries.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#include "sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint32_t kc_sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
} kc_sha256_t;

/**
 * Rotate a 32-bit word to the right.
 * @param x Source word.
 * @param n Rotation amount.
 * @return Rotated word.
 */
static uint32_t kc_rotr(uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32U - n));
}

/**
 * Initialize the SHA-256 state.
 * @param ctx Hash context.
 * @return None.
 */
static void kc_sha256_init(kc_sha256_t *ctx) {
    static const uint32_t initial[] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bytes = 0;
    ctx->used = 0;
}

/**
 * Compress one complete SHA-256 block.
 * @param ctx Hash context.
 * @param block Input block of 64 bytes.
 * @return None.
 */
static void kc_sha256_compress(kc_sha256_t *ctx, const unsigned char *block) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;
    for (i = 0; i < 16; i++) {
        size_t p = (size_t)i * 4U;
        w[i] = ((uint32_t)block[p] << 24) | ((uint32_t)block[p + 1] << 16) |
            ((uint32_t)block[p + 2] << 8) | (uint32_t)block[p + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t x = w[i - 15];
        uint32_t y = w[i - 2];
        w[i] = (kc_rotr(y, 17) ^ kc_rotr(y, 19) ^ (y >> 10)) +
            w[i - 7] + (kc_rotr(x, 7) ^ kc_rotr(x, 18) ^ (x >> 3)) + w[i - 16];
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (kc_rotr(e, 6) ^ kc_rotr(e, 11) ^ kc_rotr(e, 25)) +
            ((e & f) ^ (~e & g)) + kc_sha256_k[i] + w[i];
        t2 = (kc_rotr(a, 2) ^ kc_rotr(a, 13) ^ kc_rotr(a, 22)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

/**
 * Add file bytes to a SHA-256 computation.
 * @param ctx Hash context.
 * @param input Bytes to hash.
 * @param count Number of bytes.
 * @return None.
 */
static void kc_sha256_update(kc_sha256_t *ctx, const unsigned char *input, size_t count) {
    size_t i;
    ctx->bytes += (uint64_t)count;
    for (i = 0; i < count; i++) {
        ctx->block[ctx->used++] = input[i];
        if (ctx->used == 64U) {
            kc_sha256_compress(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

/**
 * Finalize the digest into hexadecimal form.
 * @param ctx Hash context.
 * @param out Buffer for 64 hexadecimal characters and terminator.
 * @return None.
 */
static void kc_sha256_final(kc_sha256_t *ctx, char out[65]) {
    uint64_t bits = ctx->bytes * 8U;
    size_t i;
    ctx->block[ctx->used++] = 0x80U;
    if (ctx->used > 56U) {
        while (ctx->used < 64U) ctx->block[ctx->used++] = 0;
        kc_sha256_compress(ctx, ctx->block);
        ctx->used = 0;
    }
    while (ctx->used < 56U) ctx->block[ctx->used++] = 0;
    for (i = 0; i < 8U; i++) ctx->block[56U + i] =
        (unsigned char)(bits >> (56U - (unsigned int)i * 8U));
    kc_sha256_compress(ctx, ctx->block);
    for (i = 0; i < 8U; i++) snprintf(out + i * 8U, 9U, "%08x", ctx->state[i]);
    out[64] = '\0';
}

/**
 * Calculate the SHA-256 digest for a local file.
 * @param filename File to hash.
 * @param digest Output hexadecimal SHA-256 string.
 * @return 0 on success, or 1 on failure.
 */
int kc_sha256_file(const char *filename, char digest[65]) {
    FILE *file = fopen(filename, "rb");
    unsigned char buffer[4096];
    size_t count;
    kc_sha256_t ctx;
    if (!file) return 1;
    kc_sha256_init(&ctx);
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        kc_sha256_update(&ctx, buffer, count);
    }
    if (ferror(file)) {
        fclose(file);
        return 1;
    }
    fclose(file);
    kc_sha256_final(&ctx, digest);
    return 0;
}
