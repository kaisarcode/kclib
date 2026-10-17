/**
 * libpow.c - Portable proof-of-work challenges.
 * Summary: Creates and verifies backend-tagged proof-of-work entities.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "backend.h"

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

#ifndef KC_POW_BUILD_VERSION
#define KC_POW_BUILD_VERSION 0
#endif

#define KC_POW_HEADER_SIZE 8U
#define KC_POW_CHALLENGE_SIZE 48U
#define KC_POW_SOLUTION_SIZE 16U
#define KC_POW_KIND_CHALLENGE 1U
#define KC_POW_KIND_SOLUTION 2U
#define KC_POW_VERSION_LEGACY 1U
#define KC_POW_VERSION_CURRENT 2U

extern const kc_pow_backend_impl_t kc_pow_sha256_backend;
extern const kc_pow_backend_impl_t kc_pow_hashwx_backend;

/**
 * Loads one big-endian 16-bit value.
 * @param p Source bytes.
 * @return Decoded value.
 */
static uint16_t kc_pow_load16be(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/**
 * Stores one big-endian 16-bit value.
 * @param p Destination bytes.
 * @param value Value to encode.
 * @return None.
 */
static void kc_pow_store16be(unsigned char *p, uint16_t value) {
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

/**
 * Loads one big-endian 64-bit value.
 * @param p Source bytes.
 * @return Decoded value.
 */
static uint64_t kc_pow_load64be(const unsigned char *p) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
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
 * Resolves AUTO to the portable default backend.
 * @param backend Requested backend identifier.
 * @return Resolved backend identifier.
 */
static kc_pow_backend_t kc_pow_backend_resolve(kc_pow_backend_t backend) {
    return backend == KC_POW_BACKEND_AUTO ? KC_POW_BACKEND_SHA256 : backend;
}

/**
 * Returns the private implementation for one backend identifier.
 * @param backend Backend identifier.
 * @return Backend implementation, or NULL for an unknown identifier.
 */
const kc_pow_backend_impl_t *kc_pow_backend_impl(kc_pow_backend_t backend) {
    backend = kc_pow_backend_resolve(backend);
    if (backend == KC_POW_BACKEND_SHA256) return &kc_pow_sha256_backend;
    if (backend == KC_POW_BACKEND_HASHWX) return &kc_pow_hashwx_backend;
    return NULL;
}

/**
 * Returns whether one backend is available in this build.
 * @param backend Backend identifier.
 * @return 1 when available, or 0 otherwise.
 */
int kc_pow_backend_available(kc_pow_backend_t backend) {
    const kc_pow_backend_impl_t *implementation = kc_pow_backend_impl(backend);
    return implementation && implementation->available() ? 1 : 0;
}

/**
 * Returns the stable name for one backend identifier.
 * @param backend Backend identifier.
 * @return Backend name, or NULL for an unknown identifier.
 */
const char *kc_pow_backend_name(kc_pow_backend_t backend) {
    if (backend == KC_POW_BACKEND_AUTO) return "auto";
    if (backend == KC_POW_BACKEND_SHA256) return "sha256";
    if (backend == KC_POW_BACKEND_HASHWX) return "hashwx";
    return NULL;
}

/**
 * Converts one backend name to its stable identifier.
 * @param name Backend name.
 * @return Backend identifier, or INVALID for an unknown name.
 */
kc_pow_backend_t kc_pow_backend_from_name(const char *name) {
    if (!name) return KC_POW_BACKEND_INVALID;
    if (strcmp(name, "auto") == 0) return KC_POW_BACKEND_AUTO;
    if (strcmp(name, "sha256") == 0) return KC_POW_BACKEND_SHA256;
    if (strcmp(name, "hashwx") == 0) return KC_POW_BACKEND_HASHWX;
    return KC_POW_BACKEND_INVALID;
}

#ifdef _WIN32
/**
 * Fills a buffer with cryptographically random bytes on Windows.
 * @param buf Output buffer.
 * @param size Output byte count.
 * @return 0 on success, or -1 on failure.
 */
static int kc_pow_random(unsigned char *buf, size_t size) {
    if (!buf || size > 0xFFFFFFFFU) return -1;
    return BCryptGenRandom(NULL, buf, (ULONG)size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
}
#elif defined(__EMSCRIPTEN__)
/**
 * Fills a buffer with cryptographically random bytes on Emscripten.
 * @param buf Output buffer.
 * @param size Output byte count.
 * @return 0 on success, or -1 on failure.
 */
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
/**
 * Fills a buffer with cryptographically random bytes on Unix.
 * @param buf Output buffer.
 * @param size Output byte count.
 * @return 0 on success, or -1 on failure.
 */
static int kc_pow_random(unsigned char *buf, size_t size) {
    FILE *file;
    size_t done = 0;
    if (!buf) return -1;
    file = fopen("/dev/urandom", "rb");
    if (!file) return -1;
    while (done < size) {
        size_t count = fread(buf + done, 1, size - done, file);
        if (count == 0) {
            fclose(file);
            return -1;
        }
        done += count;
    }
    return fclose(file) == 0 ? 0 : -1;
}
#endif

/**
 * Writes one current entity header.
 * @param out Output buffer with at least eight bytes.
 * @param kind Entity kind.
 * @param backend Backend identifier.
 * @param size Total entity size.
 * @return 0 on success, or -1 on invalid input.
 */
static int kc_pow_header(unsigned char *out, unsigned kind,
    kc_pow_backend_t backend, unsigned size) {
    if (!out || size > 0xFFFFU || backend == KC_POW_BACKEND_AUTO) return -1;
    out[0] = 'K'; out[1] = 'P'; out[2] = 'W'; out[3] = KC_POW_VERSION_CURRENT;
    out[4] = (unsigned char)kind;
    out[5] = (unsigned char)backend;
    kc_pow_store16be(out + 6, (uint16_t)size);
    return 0;
}

/**
 * Decodes one entity backend from a complete header.
 * @param data Entity header bytes.
 * @param out_backend Destination backend identifier.
 * @return 0 on success, or -1 on invalid input.
 */
static int kc_pow_header_backend(const unsigned char *data,
    kc_pow_backend_t *out_backend) {
    if (data[3] == KC_POW_VERSION_LEGACY && data[5] == 0) {
        *out_backend = KC_POW_BACKEND_HASHWX;
        return 0;
    }
    if (data[3] != KC_POW_VERSION_CURRENT) return -1;
    if (data[5] == KC_POW_BACKEND_SHA256 || data[5] == KC_POW_BACKEND_HASHWX) {
        *out_backend = (kc_pow_backend_t)data[5];
        return 0;
    }
    return -1;
}

/**
 * Determines the total size of one framed entity.
 * @param data Entity bytes.
 * @param data_size Available byte count.
 * @param out_size Destination expected entity size.
 * @return KC_POW_OK, KC_POW_INCOMPLETE, or KC_POW_ERROR.
 */
int kc_pow_size(const void *data, size_t data_size, size_t *out_size) {
    const unsigned char *bytes = (const unsigned char *)data;
    kc_pow_backend_t backend;
    size_t expected;
    if (!out_size) return KC_POW_ERROR;
    *out_size = 0;
    if (data_size < KC_POW_HEADER_SIZE) return KC_POW_INCOMPLETE;
    if (!bytes || bytes[0] != 'K' || bytes[1] != 'P' || bytes[2] != 'W' ||
        kc_pow_header_backend(bytes, &backend) != 0) return KC_POW_ERROR;
    if (bytes[4] == KC_POW_KIND_CHALLENGE) expected = KC_POW_CHALLENGE_SIZE;
    else if (bytes[4] == KC_POW_KIND_SOLUTION) expected = KC_POW_SOLUTION_SIZE;
    else return KC_POW_ERROR;
    if ((size_t)kc_pow_load16be(bytes + 6) != expected) return KC_POW_ERROR;
    *out_size = expected;
    return KC_POW_OK;
}

/**
 * Parses one exact entity and returns its encoded backend.
 * @param data Entity data.
 * @param data_size Entity byte count.
 * @param kind Expected entity kind.
 * @param expected Expected total entity size.
 * @param out_backend Destination backend identifier.
 * @return 0 on success, or -1 on malformed input.
 */
static int kc_pow_parse_exact(const void *data, size_t data_size,
    unsigned kind, size_t expected, kc_pow_backend_t *out_backend) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t total = 0;
    if (!out_backend || kc_pow_size(data, data_size, &total) != KC_POW_OK ||
        !bytes || total != expected || data_size != expected ||
        bytes[4] != kind) return -1;
    return kc_pow_header_backend(bytes, out_backend);
}

/**
 * Returns the backend encoded in one challenge entity.
 * @param challenge Challenge entity.
 * @param challenge_size Challenge byte count.
 * @param out_backend Destination backend identifier.
 * @return KC_POW_OK on success, or KC_POW_ERROR on malformed input.
 */
int kc_pow_get_backend(const void *challenge, size_t challenge_size,
    kc_pow_backend_t *out_backend) {
    return out_backend && kc_pow_parse_exact(challenge, challenge_size,
        KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE, out_backend) == 0 ?
        KC_POW_OK : KC_POW_ERROR;
}

/**
 * Returns the library build version.
 * @return Build version number.
 */
uint64_t kc_pow_version(void) {
    return (uint64_t)KC_POW_BUILD_VERSION;
}

/**
 * Creates one challenge for an explicitly selected backend.
 * @param backend Requested backend identifier.
 * @param effort Approximate expected nonce attempts.
 * @param out_challenge Destination allocated challenge pointer.
 * @param out_size Destination challenge size.
 * @return KC_POW_OK on success, or KC_POW_ERROR on failure.
 */
int kc_pow_challenge_with_backend(kc_pow_backend_t backend, uint64_t effort,
    void **out_challenge, size_t *out_size) {
    unsigned char *challenge;
    uint64_t target;
    backend = kc_pow_backend_resolve(backend);
    if (!out_challenge || !out_size || effort == 0 ||
        !kc_pow_backend_available(backend)) return KC_POW_ERROR;
    *out_challenge = NULL;
    *out_size = 0;
    challenge = (unsigned char *)malloc(KC_POW_CHALLENGE_SIZE);
    if (!challenge || kc_pow_header(challenge, KC_POW_KIND_CHALLENGE, backend,
        KC_POW_CHALLENGE_SIZE) != 0) {
        free(challenge);
        return KC_POW_ERROR;
    }
    target = UINT64_MAX / effort;
    kc_pow_store64be(challenge + KC_POW_HEADER_SIZE, target);
    if (kc_pow_random(challenge + 16U, 32U) != 0) {
        free(challenge);
        return KC_POW_ERROR;
    }
    *out_challenge = challenge;
    *out_size = KC_POW_CHALLENGE_SIZE;
    return KC_POW_OK;
}

/**
 * Creates one challenge using the portable default backend.
 * @param effort Approximate expected nonce attempts.
 * @param out_challenge Destination allocated challenge pointer.
 * @param out_size Destination challenge size.
 * @return KC_POW_OK on success, or KC_POW_ERROR on failure.
 */
int kc_pow_challenge(uint64_t effort, void **out_challenge, size_t *out_size) {
    return kc_pow_challenge_with_backend(KC_POW_BACKEND_SHA256, effort,
        out_challenge, out_size);
}

/**
 * Runs one backend operation over a candidate nonce sequence.
 * @param implementation Selected backend implementation.
 * @param challenge Exact challenge bytes.
 * @param target Exclusive upper proof target.
 * @param out_nonce Destination matching nonce.
 * @return 0 on success, or -1 when no nonce can be found.
 */
static int kc_pow_find_nonce(const kc_pow_backend_impl_t *implementation,
    const unsigned char *challenge, uint64_t target, uint64_t *out_nonce) {
    void *context = NULL;
    uint64_t nonce = 0;
    uint64_t value;
    if (!implementation || !challenge || !out_nonce || target == 0 ||
        implementation->open(challenge, 32U, &context) != 0) return -1;
    for (;;) {
        if (implementation->hash(context, nonce, &value) != 0) break;
        if (value < target) {
            *out_nonce = nonce;
            implementation->close(context);
            return 0;
        }
        if (nonce == UINT64_MAX) break;
        nonce++;
    }
    implementation->close(context);
    return -1;
}

/**
 * Computes one backend proof value.
 * @param implementation Selected backend implementation.
 * @param challenge Exact challenge bytes.
 * @param nonce Candidate nonce.
 * @param out_value Destination proof value.
 * @return 0 on success, or -1 on failure.
 */
static int kc_pow_value(const kc_pow_backend_impl_t *implementation,
    const unsigned char *challenge, uint64_t nonce, uint64_t *out_value) {
    void *context = NULL;
    int result;
    if (!implementation || !challenge || !out_value ||
        implementation->open(challenge, 32U, &context) != 0) return -1;
    result = implementation->hash(context, nonce, out_value);
    implementation->close(context);
    return result;
}

/**
 * Solves one proof-of-work challenge.
 * @param challenge Challenge entity.
 * @param challenge_size Challenge byte count.
 * @param out_solution Destination allocated solution pointer.
 * @param out_size Destination solution size.
 * @return KC_POW_OK on success, or KC_POW_ERROR on failure.
 */
int kc_pow_solve(const void *challenge, size_t challenge_size,
    void **out_solution, size_t *out_size) {
    const unsigned char *bytes = (const unsigned char *)challenge;
    const kc_pow_backend_impl_t *implementation;
    kc_pow_backend_t backend;
    unsigned char *solution;
    uint64_t target;
    uint64_t nonce;
    if (!out_solution || !out_size) return KC_POW_ERROR;
    *out_solution = NULL;
    *out_size = 0;
    if (kc_pow_parse_exact(challenge, challenge_size,
        KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE, &backend) != 0) {
        return KC_POW_ERROR;
    }
    implementation = kc_pow_backend_impl(backend);
    target = kc_pow_load64be(bytes + KC_POW_HEADER_SIZE);
    if (!implementation || !implementation->available() ||
        kc_pow_find_nonce(implementation, bytes + 16U, target, &nonce) != 0) {
        return KC_POW_ERROR;
    }
    solution = (unsigned char *)malloc(KC_POW_SOLUTION_SIZE);
    if (!solution || kc_pow_header(solution, KC_POW_KIND_SOLUTION, backend,
        KC_POW_SOLUTION_SIZE) != 0) {
        free(solution);
        return KC_POW_ERROR;
    }
    kc_pow_store64be(solution + KC_POW_HEADER_SIZE, nonce);
    *out_solution = solution;
    *out_size = KC_POW_SOLUTION_SIZE;
    return KC_POW_OK;
}

/**
 * Verifies one proof-of-work solution.
 * @param challenge Challenge entity.
 * @param challenge_size Challenge byte count.
 * @param solution Solution entity.
 * @param solution_size Solution byte count.
 * @return KC_POW_OK, KC_POW_INVALID, or KC_POW_ERROR.
 */
int kc_pow_verify(const void *challenge, size_t challenge_size,
    const void *solution, size_t solution_size) {
    const unsigned char *challenge_bytes = (const unsigned char *)challenge;
    const unsigned char *solution_bytes = (const unsigned char *)solution;
    const kc_pow_backend_impl_t *implementation;
    kc_pow_backend_t challenge_backend;
    kc_pow_backend_t solution_backend;
    uint64_t target;
    uint64_t nonce;
    uint64_t value;
    if (kc_pow_parse_exact(challenge, challenge_size,
        KC_POW_KIND_CHALLENGE, KC_POW_CHALLENGE_SIZE, &challenge_backend) != 0 ||
        kc_pow_parse_exact(solution, solution_size, KC_POW_KIND_SOLUTION,
        KC_POW_SOLUTION_SIZE, &solution_backend) != 0 ||
        challenge_backend != solution_backend) return KC_POW_ERROR;
    implementation = kc_pow_backend_impl(challenge_backend);
    target = kc_pow_load64be(challenge_bytes + KC_POW_HEADER_SIZE);
    nonce = kc_pow_load64be(solution_bytes + KC_POW_HEADER_SIZE);
    if (!implementation || !implementation->available() || target == 0 ||
        kc_pow_value(implementation, challenge_bytes + 16U, nonce, &value) != 0) {
        return KC_POW_ERROR;
    }
    return value < target ? KC_POW_OK : KC_POW_INVALID;
}

/**
 * Releases an allocation returned by libpow.
 * @param ptr Allocation pointer, or NULL.
 * @return None.
 */
void kc_pow_free(void *ptr) {
    free(ptr);
}
