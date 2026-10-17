/**
 * backend.h - Private proof-of-work backend interface.
 * Summary: Defines the common backend operations used by libpow.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_POW_BACKEND_H
#define KC_POW_BACKEND_H

#include "libpow.h"

#include <stddef.h>
#include <stdint.h>

typedef struct kc_pow_backend_impl kc_pow_backend_impl_t;

/**
 * Holds one initialized private backend operation.
 * @return None.
 */
struct kc_pow_backend_impl {
    kc_pow_backend_t id;
    const char *name;
    int (*available)(void);
    int (*open)(const unsigned char *challenge, size_t challenge_size,
        void **out_context);
    int (*hash)(void *context, uint64_t nonce, uint64_t *out_value);
    void (*close)(void *context);
};

const kc_pow_backend_impl_t *kc_pow_backend_impl(kc_pow_backend_t backend);
void kc_pow_sha256_bytes(const unsigned char *first, size_t first_size,
    const unsigned char *second, size_t second_size, unsigned char digest[32]);

#endif
