/**
 * libpow.c - Portable proof-of-work challenges.
 * Summary: HashWX-backed transport-agnostic proof-of-work entities.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libpow.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <stdio.h>
#include <unistd.h>
#endif

#include "hashwx.h"

#ifndef KC_POW_BUILD_VERSION
#define KC_POW_BUILD_VERSION 0
#endif

#define KC_POW_HEADER_SIZE 8U
#define KC_POW_CHALLENGE_SIZE 48U
#define KC_POW_SOLUTION_SIZE 16U
#define KC_POW_KIND_CHALLENGE 1U
#define KC_POW_KIND_SOLUTION 2U
#define KC_POW_VERSION 1U
#define KC_POW_HASHWX_BATCH 463ULL

#define KC_POW_SHA256_BLOCK_SIZE 64U
#define KC_POW_SHA256_DIGEST_SIZE 32U

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[KC_POW_SHA256_BLOCK_SIZE];
    size_t block_size;
} kc_pow_sha256_t;

static uint32_t kc_pow_rotr32(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

static uint32_t kc_pow_load32be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void kc_pow_store32be(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static uint16_t kc_pow_load16be(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void kc_pow_store16be(unsigned char *p, uint16_t value) {
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static uint64_t kc_pow_load64be(const unsigned char *p) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

static void kc_pow_store64be(unsigned char *p, uint64_t value) {
    int i;
    for (i = 7; i >= 0; --i) {
        p[i] = (unsigned char)value;
        value >>= 8;
    }
}

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
        uint32_t s1 = kc_pow_rotr32(e, 6) ^ kc_pow_rotr32(e, 11) ^ kc_pow_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = kc_pow_rotr32(a, 2) ^ kc_pow_rotr32(a, 13) ^ kc_pow_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void kc_pow_sha256_init(kc_pow_sha256_t *ctx) {
    static const uint32_t initial[8] = {
        0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U
    };
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bit_count = 0;
    ctx->block_size = 0;
}

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

static void kc_pow_sha256(const unsigned char *a, size_t a_size,
    const unsigned char *b, size_t b_size,
    unsigned char digest[KC_POW_SHA256_DIGEST_SIZE]) {
    kc_pow_sha256_t ctx;
    kc_pow_sha256_init(&ctx);
    kc_pow_sha256_update(&ctx, a, a_size);
    kc_pow_sha256_update(&ctx, b, b_size);
    kc_pow_sha256_final(&ctx, digest);
}

#ifdef _WIN32
static int kc_pow_random(unsigned char *buf, size_t size) {
    if (!buf || size > 0xFFFFFFFFU) return -1;
    return BCryptGenRandom(NULL, buf, (ULONG)size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
}
#elif defined(__EMSCRIPTEN__)
static int kc_pow_random(unsigned char *buf, size_t size) {
    size_t done = 0;
    if (!buf) return -1;
    while (done < size) {
        size_t part = size - done;
        if (part > 256U) part = 256U;
        if (getentropy(buf + done, part) != 0) return -1;
        done += part;
    }
    return 0;
}
#else
static int kc_pow_random(unsigned char *buf, size_t size) {
    FILE *f;
    size_t done = 0;
    if (!buf) return -1;
    f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    while (done < size) {
        size_t n = fread(buf + done, 1, size - done, f);
        if (n == 0) {
            fclose(f);
            return -1;
        }
        done += n;
    }
    return fclose(f) == 0 ? 0 : -1;
}
#endif

static int kc_pow_header(unsigned char *out, unsigned kind, unsigned size) {
    if (!out || size > 0xFFFFU) return -1;
    out[0] = 'K'; out[1] = 'P'; out[2] = 'W'; out[3] = KC_POW_VERSION;
    out[4] = (unsigned char)kind;
    out[5] = 0;
    kc_pow_store16be(out + 6, (uint16_t)size);
    return 0;
}

int kc_pow_size(const void *data, size_t data_size, size_t *out_size) {
    const unsigned char *p = (const unsigned char *)data;
    size_t expected;
    uint16_t declared;

    if (!out_size) return KC_POW_ERROR;
    *out_size = 0;
    if (data_size < KC_POW_HEADER_SIZE) return KC_POW_INCOMPLETE;
    if (!p) return KC_POW_ERROR;
    if (p[0] != 'K' || p[1] != 'P' || p[2] != 'W' ||
        p[3] != KC_POW_VERSION || p[5] != 0) return KC_POW_ERROR;

    if (p[4] == KC_POW_KIND_CHALLENGE) expected = KC_POW_CHALLENGE_SIZE;
    else if (p[4] == KC_POW_KIND_SOLUTION) expected = KC_POW_SOLUTION_SIZE;
    else return KC_POW_ERROR;

    declared = kc_pow_load16be(p + 6);
    if ((size_t)declared != expected) return KC_POW_ERROR;
    *out_size = expected;
    return KC_POW_OK;
}

static int kc_pow_parse_exact(const void *data, size_t data_size,
    unsigned kind, size_t expected) {
    const unsigned char *p = (const unsigned char *)data;
    size_t total = 0;
    if (kc_pow_size(data, data_size, &total) != KC_POW_OK) return -1;
    if (!p || total != expected || data_size != expected || p[4] != kind) return -1;
    return 0;
}

static hashwx_ctx *kc_pow_hashwx_alloc(void) {
    hashwx_ctx *ctx;
#ifdef __EMSCRIPTEN__
    ctx = hashwx_alloc(HASHWX_INTERPRETED);
#else
    ctx = hashwx_alloc(HASHWX_COMPILED);
    if (ctx == HASHWX_NOTSUPP) ctx = hashwx_alloc(HASHWX_INTERPRETED);
#endif
    if (ctx == HASHWX_NOTSUPP) return NULL;
    return ctx;
}

static void kc_pow_hashwx_seed(const unsigned char challenge[32], uint64_t nonce,
    unsigned char seed[32]) {
    unsigned char group[8];
    kc_pow_store64be(group, nonce / KC_POW_HASHWX_BATCH);
    kc_pow_sha256(challenge, 32U, group, sizeof(group), seed);
}

static int kc_pow_hash_value(hashwx_ctx *ctx,
    const unsigned char challenge[32], uint64_t nonce, uint64_t *out_hash) {
    unsigned char seed[32];
    if (!ctx || !challenge || !out_hash) return -1;
    kc_pow_hashwx_seed(challenge, nonce, seed);
    hashwx_make(ctx, seed);
    *out_hash = hashwx_exec(ctx, nonce);
    memset(seed, 0, sizeof(seed));
    return 0;
}

uint64_t kc_pow_version(void) {
    return (uint64_t)KC_POW_BUILD_VERSION;
}

int kc_pow_challenge(uint64_t effort, void **out_challenge, size_t *out_size) {
    unsigned char *blob;
    uint64_t target;

    if (!out_challenge || !out_size || effort == 0) return KC_POW_ERROR;
    *out_challenge = NULL;
    *out_size = 0;

    blob = (unsigned char *)malloc(KC_POW_CHALLENGE_SIZE);
    if (!blob) return KC_POW_ERROR;
    if (kc_pow_header(blob, KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE) != 0) {
        free(blob);
        return KC_POW_ERROR;
    }

    target = UINT64_MAX / effort;
    kc_pow_store64be(blob + KC_POW_HEADER_SIZE, target);
    if (kc_pow_random(blob + 16U, 32U) != 0) {
        free(blob);
        return KC_POW_ERROR;
    }

    *out_challenge = blob;
    *out_size = KC_POW_CHALLENGE_SIZE;
    return KC_POW_OK;
}

int kc_pow_solve(const void *challenge, size_t challenge_size,
    void **out_solution, size_t *out_size) {
    const unsigned char *c = (const unsigned char *)challenge;
    hashwx_ctx *ctx;
    unsigned char *solution;
    uint64_t target;
    uint64_t nonce = 0;
    uint64_t hash;

    if (!out_solution || !out_size) return KC_POW_ERROR;
    *out_solution = NULL;
    *out_size = 0;
    if (kc_pow_parse_exact(challenge, challenge_size,
        KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE) != 0) return KC_POW_ERROR;

    target = kc_pow_load64be(c + KC_POW_HEADER_SIZE);
    if (target == 0) return KC_POW_ERROR;
    ctx = kc_pow_hashwx_alloc();
    if (!ctx) return KC_POW_ERROR;

    for (;;) {
        if (kc_pow_hash_value(ctx, c + 16U, nonce, &hash) != 0) {
            hashwx_free(ctx);
            return KC_POW_ERROR;
        }
        if (hash < target) break;
        if (nonce == UINT64_MAX) {
            hashwx_free(ctx);
            return KC_POW_ERROR;
        }
        nonce++;
    }
    hashwx_free(ctx);

    solution = (unsigned char *)malloc(KC_POW_SOLUTION_SIZE);
    if (!solution) return KC_POW_ERROR;
    if (kc_pow_header(solution, KC_POW_KIND_SOLUTION, KC_POW_SOLUTION_SIZE) != 0) {
        free(solution);
        return KC_POW_ERROR;
    }
    kc_pow_store64be(solution + KC_POW_HEADER_SIZE, nonce);
    *out_solution = solution;
    *out_size = KC_POW_SOLUTION_SIZE;
    return KC_POW_OK;
}

int kc_pow_verify(const void *challenge, size_t challenge_size,
    const void *solution, size_t solution_size) {
    const unsigned char *c = (const unsigned char *)challenge;
    const unsigned char *s = (const unsigned char *)solution;
    hashwx_ctx *ctx;
    uint64_t target;
    uint64_t nonce;
    uint64_t hash;

    if (kc_pow_parse_exact(challenge, challenge_size,
        KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE) != 0) return KC_POW_ERROR;
    if (kc_pow_parse_exact(solution, solution_size,
        KC_POW_KIND_SOLUTION, KC_POW_SOLUTION_SIZE) != 0) return KC_POW_ERROR;

    target = kc_pow_load64be(c + KC_POW_HEADER_SIZE);
    nonce = kc_pow_load64be(s + KC_POW_HEADER_SIZE);
    if (target == 0) return KC_POW_ERROR;

    ctx = kc_pow_hashwx_alloc();
    if (!ctx) return KC_POW_ERROR;
    if (kc_pow_hash_value(ctx, c + 16U, nonce, &hash) != 0) {
        hashwx_free(ctx);
        return KC_POW_ERROR;
    }
    hashwx_free(ctx);
    return hash < target ? KC_POW_OK : KC_POW_INVALID;
}

void kc_pow_free(void *ptr) {
    free(ptr);
}
