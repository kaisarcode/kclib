/**
 * hashwx.c - HashWX proof-of-work backend.
 * Summary: Adapts the existing HashWX implementation to the backend interface.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "backend.h"

#if KC_POW_ENABLE_HASHWX
#include "hashwx.h"

#include <stdlib.h>
#include <string.h>

#define KC_POW_HASHWX_BATCH 463ULL

typedef struct {
    hashwx_ctx *hashwx;
    const unsigned char *challenge;
    uint64_t group;
    int has_group;
} kc_pow_hashwx_backend_t;

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
 * Allocates a usable HashWX context.
 * @return Allocated context, or NULL when unavailable.
 */
static hashwx_ctx *kc_pow_hashwx_alloc(void) {
    hashwx_ctx *ctx;
#ifdef __EMSCRIPTEN__
    ctx = hashwx_alloc(HASHWX_INTERPRETED);
#else
    ctx = hashwx_alloc(HASHWX_COMPILED);
    if (ctx == HASHWX_NOTSUPP) ctx = hashwx_alloc(HASHWX_INTERPRETED);
#endif
    return ctx == HASHWX_NOTSUPP ? NULL : ctx;
}

/**
 * Reports whether HashWX can allocate a backend context.
 * @return 1 when available, or 0 otherwise.
 */
static int kc_pow_hashwx_available(void) {
    hashwx_ctx *ctx = kc_pow_hashwx_alloc();
    if (!ctx) return 0;
    hashwx_free(ctx);
    return 1;
}

/**
 * Configures HashWX for one nonce group.
 * @param context Backend context.
 * @param nonce Candidate nonce.
 * @return 0 on success, or -1 on failure.
 */
static int kc_pow_hashwx_prepare(kc_pow_hashwx_backend_t *context,
    uint64_t nonce) {
    unsigned char group_bytes[8];
    unsigned char seed[32];
    uint64_t group = nonce / KC_POW_HASHWX_BATCH;
    if (context->has_group && context->group == group) return 0;
    kc_pow_store64be(group_bytes, group);
    kc_pow_sha256_bytes(context->challenge, 32U, group_bytes,
        sizeof(group_bytes), seed);
    hashwx_make(context->hashwx, seed);
    memset(seed, 0, sizeof(seed));
    context->group = group;
    context->has_group = 1;
    return 0;
}

/**
 * Opens one HashWX backend operation.
 * @param challenge Exact challenge bytes.
 * @param challenge_size Challenge byte count.
 * @param out_context Output backend context.
 * @return 0 on success, or -1 on invalid input or allocation failure.
 */
static int kc_pow_hashwx_open(const unsigned char *challenge,
    size_t challenge_size, void **out_context) {
    kc_pow_hashwx_backend_t *context;
    if (!challenge || challenge_size != 32U || !out_context) return -1;
    *out_context = NULL;
    context = (kc_pow_hashwx_backend_t *)calloc(1, sizeof(*context));
    if (!context) return -1;
    context->hashwx = kc_pow_hashwx_alloc();
    if (!context->hashwx) {
        free(context);
        return -1;
    }
    context->challenge = challenge;
    *out_context = context;
    return 0;
}

/**
 * Computes one HashWX proof value.
 * @param context Backend context.
 * @param nonce Candidate nonce.
 * @param out_value Output proof value.
 * @return 0 on success, or -1 on invalid input.
 */
static int kc_pow_hashwx_hash(void *context, uint64_t nonce,
    uint64_t *out_value) {
    kc_pow_hashwx_backend_t *backend = (kc_pow_hashwx_backend_t *)context;
    if (!backend || !out_value || kc_pow_hashwx_prepare(backend, nonce) != 0) {
        return -1;
    }
    *out_value = hashwx_exec(backend->hashwx, nonce);
    return 0;
}

/**
 * Closes one HashWX backend operation.
 * @param context Backend context.
 * @return None.
 */
static void kc_pow_hashwx_close(void *context) {
    kc_pow_hashwx_backend_t *backend = (kc_pow_hashwx_backend_t *)context;
    if (!backend) return;
    hashwx_free(backend->hashwx);
    free(backend);
}
#else
/**
 * Reports that HashWX was not compiled into this build.
 * @return 0.
 */
static int kc_pow_hashwx_available(void) {
    return 0;
}
#endif

const kc_pow_backend_impl_t kc_pow_hashwx_backend = {
    KC_POW_BACKEND_HASHWX,
    "hashwx",
    kc_pow_hashwx_available,
#if KC_POW_ENABLE_HASHWX
    kc_pow_hashwx_open,
    kc_pow_hashwx_hash,
    kc_pow_hashwx_close
#else
    NULL,
    NULL,
    NULL
#endif
};
