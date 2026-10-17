/**
 * libpow.h - Portable proof-of-work challenges.
 * Summary: Transport-agnostic challenge, solve, and verify API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_POW_H
#define KC_POW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_POW_OK 0
#define KC_POW_INCOMPLETE 1
#define KC_POW_INVALID 2
#define KC_POW_ERROR -1

typedef enum {
    KC_POW_BACKEND_INVALID = -1,
    KC_POW_BACKEND_AUTO = 0,
    KC_POW_BACKEND_SHA256 = 1,
    KC_POW_BACKEND_HASHWX = 2
} kc_pow_backend_t;

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_pow_version(void);

/**
 * Creates one self-contained proof-of-work challenge.
 *
 * Effort is the approximate expected number of nonce attempts required to
 * solve the challenge. An effort of 1 is valid and requires effectively no
 * search. The returned challenge is an opaque binary entity that may be
 * stored or transported by the caller by any mechanism.
 *
 * @param effort Approximate expected nonce attempts, greater than zero.
 * @param out_challenge Destination for the allocated challenge entity.
 * @param out_size Destination challenge byte count.
 * @return KC_POW_OK on success, or KC_POW_ERROR on failure.
 */
int kc_pow_challenge(uint64_t effort, void **out_challenge, size_t *out_size);

/**
 * Creates a challenge for one explicitly selected proof-of-work backend.
 *
 * KC_POW_BACKEND_AUTO resolves to KC_POW_BACKEND_SHA256. A request for an
 * unavailable backend fails and does not use another backend.
 *
 * @param backend Selected backend identifier.
 * @param effort Approximate expected nonce attempts, greater than zero.
 * @param out_challenge Destination for the allocated challenge entity.
 * @param out_size Destination challenge byte count.
 * @return KC_POW_OK on success, or KC_POW_ERROR on invalid or unavailable
 *         input.
 */
int kc_pow_challenge_with_backend(kc_pow_backend_t backend, uint64_t effort,
    void **out_challenge, size_t *out_size);

/**
 * Returns the backend encoded by one challenge entity.
 * @param challenge Challenge entity from kc_pow_challenge().
 * @param challenge_size Challenge byte count.
 * @param out_backend Destination backend identifier.
 * @return KC_POW_OK on success, or KC_POW_ERROR on malformed input.
 */
int kc_pow_get_backend(const void *challenge, size_t challenge_size,
    kc_pow_backend_t *out_backend);

/**
 * Determines whether one backend is available in this build.
 * @param backend Backend identifier, including KC_POW_BACKEND_AUTO.
 * @return 1 when available, or 0 otherwise.
 */
int kc_pow_backend_available(kc_pow_backend_t backend);

/**
 * Returns the stable public name of one backend identifier.
 * @param backend Backend identifier.
 * @return "auto", "sha256", "hashwx", or NULL for an unknown identifier.
 */
const char *kc_pow_backend_name(kc_pow_backend_t backend);

/**
 * Converts one stable public backend name to its identifier.
 * @param name Backend name: "auto", "sha256", or "hashwx".
 * @return Matching identifier, or KC_POW_BACKEND_INVALID for an unknown name.
 */
kc_pow_backend_t kc_pow_backend_from_name(const char *name);

/**
 * Solves one proof-of-work challenge.
 *
 * This operation performs no I/O and has no transport semantics. The returned
 * solution is an opaque binary entity tied to the supplied challenge.
 *
 * @param challenge Challenge entity from kc_pow_challenge().
 * @param challenge_size Challenge byte count.
 * @param out_solution Destination for the allocated solution entity.
 * @param out_size Destination solution byte count.
 * @return KC_POW_OK on success, or KC_POW_ERROR on malformed input or failure.
 */
int kc_pow_solve(const void *challenge, size_t challenge_size,
    void **out_solution, size_t *out_size);

/**
 * Verifies one solution against one challenge.
 *
 * @param challenge Challenge entity.
 * @param challenge_size Challenge byte count.
 * @param solution Solution entity.
 * @param solution_size Solution byte count.
 * @return KC_POW_OK when valid, KC_POW_INVALID when the proof does not satisfy
 *         the challenge, or KC_POW_ERROR for malformed input.
 */
int kc_pow_verify(const void *challenge, size_t challenge_size,
    const void *solution, size_t solution_size);

/**
 * Determines the total size of one challenge or solution entity from its
 * leading bytes.
 *
 * This is transport-agnostic. Callers receiving arbitrary chunks may
 * accumulate bytes and call this function until KC_POW_OK is returned. The
 * returned size may be greater than data_size; the remaining bytes can then be
 * collected before passing the complete entity to solve or verify.
 *
 * @param data Available leading bytes of an entity.
 * @param data_size Number of bytes currently available.
 * @param out_size Destination total entity byte count.
 * @return KC_POW_OK when the size is known, KC_POW_INCOMPLETE when more leading
 *         bytes are required, or KC_POW_ERROR for malformed input.
 */
int kc_pow_size(const void *data, size_t data_size, size_t *out_size);

/**
 * Releases an allocation returned by pow.c.
 * @param ptr Allocation pointer. NULL is a safe no-op.
 * @return None.
 */
void kc_pow_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
