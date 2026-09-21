/**
 * libtrust.c - Core implementation for the trust library.
 * Summary: Message cryptography with TOFU peer identity.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#ifdef __EMSCRIPTEN__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#endif

#include "libtrust.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

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
#include <unistd.h>
#endif

#include "monocypher.h"

#ifndef KC_TRUST_BUILD_VERSION
#define KC_TRUST_BUILD_VERSION 0
#endif

#define KC_TRUST_MAX_TRUST 256
#define KC_TRUST_NONCE_SIZE 12
#define KC_TRUST_MAC_SIZE 16
#define KC_TRUST_ENCRYPTED_PK_SIZE (KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE)
#define KC_TRUST_HANDSHAKE_SIZE (KC_TRUST_PK_SIZE + KC_TRUST_ENCRYPTED_PK_SIZE + KC_TRUST_MAC_SIZE)
#define KC_TRUST_TRANSPORT_MESSAGE_MAX 65535
#define KC_TRUST_TRANSPORT_PLAINTEXT_MAX (KC_TRUST_TRANSPORT_MESSAGE_MAX - KC_TRUST_MAC_SIZE)
#define KC_TRUST_LOGICAL_LENGTH_SIZE 8
#define KC_TRUST_LENGTH_RECORD_SIZE (KC_TRUST_LOGICAL_LENGTH_SIZE + KC_TRUST_MAC_SIZE)
#define KC_TRUST_PAYLOAD_BASE_SIZE (KC_TRUST_HANDSHAKE_SIZE + KC_TRUST_LENGTH_RECORD_SIZE)

#ifdef _WIN32

/**
 * Reads random bytes using BCryptGenRandom on Windows.
 * @param buf Destination buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_read_random(unsigned char *buf, size_t len) {
    if (len > 0xFFFFFFFF) return -1;
    NTSTATUS rc = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return rc == 0 ? 0 : -1;
}

#elif defined(__EMSCRIPTEN__)

/**
 * Reads random bytes using getentropy on Emscripten.
 * Processes requests in chunks of at most 256 bytes per call, returning
 * failure if any individual call fails.  Never opens /dev/urandom and
 * never falls back to a deterministic source.
 * @param buf Destination buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_read_random(unsigned char *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        size_t part = len - done;
        if (part > 256) part = 256;
        if (getentropy(buf + done, part) != 0) return -1;
        done += part;
    }
    return 0;
}

#else

/**
 * Reads random bytes from /dev/urandom.
 * @param buf Destination buffer.
 * @param len Number of bytes to read.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_read_random(unsigned char *buf, size_t len) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    size_t done = 0;
    while (done < len) {
        size_t n = fread(buf + done, 1, len - done, f);
        if (n == 0) { fclose(f); return -1; }
        done += n;
    }
    fclose(f);
    return 0;
}

#endif

typedef struct {
    size_t id_len;
    unsigned char id[256];
    unsigned char pk[KC_TRUST_PK_SIZE];
} kc_trust_trust_entry_t;

struct kc_trust {
    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char pk[KC_TRUST_PK_SIZE];
    int has_identity;
    kc_trust_trust_entry_t trust[KC_TRUST_MAX_TRUST];
    int trust_count;
};

#define KC_TRUST_BLAKE2B_BLOCK 128
#define KC_TRUST_HASH_SIZE 64

typedef struct {
    unsigned char k[32];
    uint64_t n;
    int has_key;
} kc_trust_cipher_state_t;

typedef struct {
    unsigned char ck[KC_TRUST_HASH_SIZE];
    unsigned char h[KC_TRUST_HASH_SIZE];
    kc_trust_cipher_state_t cipher;
} kc_trust_symmetric_state_t;

/**
 * Returns the number of transport data records for a logical message.
 * @param message_len Logical message length.
 * @return Number of transport data records.
 */
static size_t kc_trust_transport_record_count(size_t message_len) {
    if (message_len == 0) return 0;
    return 1 + (message_len - 1) / KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
}

/**
 * Returns the exact payload size for a supported logical message.
 * @param message_len Logical message length.
 * @return Exact payload size.
 */
static size_t kc_trust_payload_size(size_t message_len) {
    return KC_TRUST_PAYLOAD_BASE_SIZE + message_len +
        kc_trust_transport_record_count(message_len) * KC_TRUST_MAC_SIZE;
}

/**
 * Encodes a 64-bit unsigned integer in big-endian byte order.
 * @param output Eight-byte destination.
 * @param value Value to encode.
 * @return Nothing.
 */
static void kc_trust_store_u64_be(unsigned char output[8], uint64_t value) {
    for (size_t i = 0; i < 8; i++)
        output[7 - i] = (unsigned char)(value >> (8 * i));
}

/**
 * Decodes a big-endian 64-bit unsigned integer.
 * @param input Eight-byte input.
 * @return Decoded value.
 */
static uint64_t kc_trust_load_u64_be(const unsigned char input[8]) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++)
        value = (value << 8) | input[i];
    return value;
}

/**
 * Computes HMAC-BLAKE2b over a message.
 * @param out 64-byte destination for the MAC.
 * @param key HMAC key.
 * @param key_len HMAC key length.
 * @param msg Message input.
 * @param msg_len Message length.
 * @return Nothing.
 */
static void kc_trust_hmac_blake2b(unsigned char out[KC_TRUST_HASH_SIZE],
const unsigned char *key, size_t key_len,
const unsigned char *msg, size_t msg_len) {
    unsigned char k_pad[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char o_key[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char i_key[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char inner[KC_TRUST_HASH_SIZE];
    crypto_blake2b_ctx ctx;

    memset(k_pad, 0, KC_TRUST_BLAKE2B_BLOCK);
    memcpy(k_pad, key, key_len);

    memcpy(o_key, k_pad, KC_TRUST_BLAKE2B_BLOCK);
    memcpy(i_key, k_pad, KC_TRUST_BLAKE2B_BLOCK);
    for (int i = 0; i < KC_TRUST_BLAKE2B_BLOCK; i++) {
        o_key[i] ^= 0x5c;
        i_key[i] ^= 0x36;
    }

    crypto_blake2b_init(&ctx, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&ctx, i_key, KC_TRUST_BLAKE2B_BLOCK);
    crypto_blake2b_update(&ctx, msg, msg_len);
    crypto_blake2b_final(&ctx, inner);

    crypto_blake2b_init(&ctx, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&ctx, o_key, KC_TRUST_BLAKE2B_BLOCK);
    crypto_blake2b_update(&ctx, inner, KC_TRUST_HASH_SIZE);
    crypto_blake2b_final(&ctx, out);

    crypto_wipe(k_pad, KC_TRUST_BLAKE2B_BLOCK);
    crypto_wipe(o_key, KC_TRUST_BLAKE2B_BLOCK);
    crypto_wipe(i_key, KC_TRUST_BLAKE2B_BLOCK);
    crypto_wipe(inner, KC_TRUST_HASH_SIZE);
}

/**
 * Produces two Noise HKDF outputs.
 * @param out1 First 64-byte output.
 * @param out2 Second 64-byte output.
 * @param ck Chaining key.
 * @param ikm Input key material.
 * @param ikm_len Input key material length.
 * @return Nothing.
 */
static void kc_trust_noise_hkdf_2(unsigned char out1[KC_TRUST_HASH_SIZE],
unsigned char out2[KC_TRUST_HASH_SIZE],
const unsigned char ck[KC_TRUST_HASH_SIZE],
const unsigned char *ikm, size_t ikm_len) {
    unsigned char temp[KC_TRUST_HASH_SIZE];
    kc_trust_hmac_blake2b(temp, ck, KC_TRUST_HASH_SIZE, ikm, ikm_len);
    unsigned char c1 = 0x01;
    kc_trust_hmac_blake2b(out1, temp, KC_TRUST_HASH_SIZE, &c1, 1);
    unsigned char prefix[KC_TRUST_HASH_SIZE + 1];
    memcpy(prefix, out1, KC_TRUST_HASH_SIZE);
    prefix[KC_TRUST_HASH_SIZE] = 0x02;
    kc_trust_hmac_blake2b(out2, temp, KC_TRUST_HASH_SIZE,
        prefix, sizeof(prefix));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(prefix, sizeof(prefix));
}

/**
 * Clears a Noise cipher state key and nonce.
 * @param cipher Cipher state to initialize.
 * @return Nothing.
 */
static void kc_trust_cipher_initialize_empty(kc_trust_cipher_state_t *cipher) {
    memset(cipher, 0, sizeof(*cipher));
}

/**
 * Installs a Noise cipher key and resets its nonce.
 * @param cipher Cipher state to initialize.
 * @param key 32-byte cipher key.
 * @return Nothing.
 */
static void kc_trust_cipher_initialize_key(kc_trust_cipher_state_t *cipher,
const unsigned char key[32]) {
    memcpy(cipher->k, key, 32);
    cipher->n = 0;
    cipher->has_key = 1;
}

/**
 * Encrypts with a Noise ChaChaPoly cipher state.
 * @param cipher Cipher state.
 * @param ad Associated data.
 * @param ad_len Associated data length.
 * @param plaintext Plaintext input.
 * @param plaintext_len Plaintext length.
 * @param ciphertext Ciphertext and tag output.
 * @return 0 on success, -1 on nonce exhaustion or invalid state.
 */
static int kc_trust_cipher_encrypt(kc_trust_cipher_state_t *cipher,
const unsigned char *ad, size_t ad_len,
const unsigned char *plaintext, size_t plaintext_len,
unsigned char *ciphertext) {
    if (!cipher->has_key || cipher->n == UINT64_MAX) return -1;
    unsigned char nonce[KC_TRUST_NONCE_SIZE] = {0};
    for (size_t i = 0; i < 8; i++)
        nonce[4 + i] = (unsigned char)(cipher->n >> (8 * i));
    crypto_aead_ctx aead;
    crypto_aead_init_ietf(&aead, cipher->k, nonce);
    crypto_aead_write(&aead, ciphertext, ciphertext + plaintext_len,
        ad, ad_len, plaintext, plaintext_len);
    crypto_wipe(&aead, sizeof(aead));
    cipher->n++;
    return 0;
}

/**
 * Decrypts with a Noise ChaChaPoly cipher state.
 * @param cipher Cipher state.
 * @param ad Associated data.
 * @param ad_len Associated data length.
 * @param ciphertext Ciphertext input.
 * @param ciphertext_len Ciphertext length including the tag.
 * @param plaintext Plaintext output.
 * @return 0 on success, -1 on authentication failure or invalid state.
 */
static int kc_trust_cipher_decrypt(kc_trust_cipher_state_t *cipher,
const unsigned char *ad, size_t ad_len,
const unsigned char *ciphertext, size_t ciphertext_len,
unsigned char *plaintext) {
    if (!cipher->has_key || cipher->n == UINT64_MAX ||
        ciphertext_len < KC_TRUST_MAC_SIZE) return -1;
    size_t plaintext_len = ciphertext_len - KC_TRUST_MAC_SIZE;
    unsigned char nonce[KC_TRUST_NONCE_SIZE] = {0};
    for (size_t i = 0; i < 8; i++)
        nonce[4 + i] = (unsigned char)(cipher->n >> (8 * i));
    crypto_aead_ctx aead;
    crypto_aead_init_ietf(&aead, cipher->k, nonce);
    int rc = crypto_aead_read(&aead, plaintext,
        ciphertext + plaintext_len, ad, ad_len,
        ciphertext, plaintext_len);
    crypto_wipe(&aead, sizeof(aead));
    if (rc != 0) return -1;
    cipher->n++;
    return 0;
}

/**
 * Mixes bytes into a Noise handshake hash.
 * @param state Symmetric state.
 * @param data Input bytes.
 * @param data_len Input length.
 * @return Nothing.
 */
static void kc_trust_noise_mix_hash(kc_trust_symmetric_state_t *state,
const unsigned char *data, size_t data_len) {
    unsigned char next[KC_TRUST_HASH_SIZE];
    crypto_blake2b_ctx hash;
    crypto_blake2b_init(&hash, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&hash, state->h, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&hash, data, data_len);
    crypto_blake2b_final(&hash, next);
    memcpy(state->h, next, KC_TRUST_HASH_SIZE);
    crypto_wipe(next, sizeof(next));
}

/**
 * Mixes DH output into a Noise chaining key and cipher state.
 * @param state Symmetric state.
 * @param input_key_material Input key material.
 * @param input_len Input length.
 * @return Nothing.
 */
static void kc_trust_noise_mix_key(kc_trust_symmetric_state_t *state,
const unsigned char *input_key_material, size_t input_len) {
    unsigned char next_ck[KC_TRUST_HASH_SIZE];
    unsigned char temp_k[KC_TRUST_HASH_SIZE];
    kc_trust_noise_hkdf_2(next_ck, temp_k, state->ck,
        input_key_material, input_len);
    memcpy(state->ck, next_ck, KC_TRUST_HASH_SIZE);
    kc_trust_cipher_initialize_key(&state->cipher, temp_k);
    crypto_wipe(next_ck, sizeof(next_ck));
    crypto_wipe(temp_k, sizeof(temp_k));
}

/**
 * Encrypts plaintext and mixes the resulting ciphertext into the hash.
 * @param state Symmetric state.
 * @param plaintext Plaintext input.
 * @param plaintext_len Plaintext length.
 * @param ciphertext Ciphertext and tag output.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_noise_encrypt_and_hash(kc_trust_symmetric_state_t *state,
const unsigned char *plaintext, size_t plaintext_len,
unsigned char *ciphertext) {
    if (kc_trust_cipher_encrypt(&state->cipher, state->h,
        KC_TRUST_HASH_SIZE, plaintext, plaintext_len, ciphertext) != 0)
        return -1;
    kc_trust_noise_mix_hash(state, ciphertext,
        plaintext_len + KC_TRUST_MAC_SIZE);
    return 0;
}

/**
 * Decrypts ciphertext and then mixes it into the handshake hash.
 * @param state Symmetric state.
 * @param ciphertext Ciphertext input including the tag.
 * @param ciphertext_len Ciphertext length.
 * @param plaintext Plaintext output.
 * @return 0 on success, -1 on authentication failure.
 */
static int kc_trust_noise_decrypt_and_hash(kc_trust_symmetric_state_t *state,
const unsigned char *ciphertext, size_t ciphertext_len,
unsigned char *plaintext) {
    if (kc_trust_cipher_decrypt(&state->cipher, state->h,
        KC_TRUST_HASH_SIZE, ciphertext, ciphertext_len, plaintext) != 0)
        return -1;
    kc_trust_noise_mix_hash(state, ciphertext, ciphertext_len);
    return 0;
}

/**
 * Derives the two Noise transport cipher states.
 * @param state Symmetric state.
 * @param first First transport cipher state.
 * @param second Second transport cipher state.
 * @return Nothing.
 */
static void kc_trust_noise_split(const kc_trust_symmetric_state_t *state,
kc_trust_cipher_state_t *first, kc_trust_cipher_state_t *second) {
    unsigned char first_key[KC_TRUST_HASH_SIZE];
    unsigned char second_key[KC_TRUST_HASH_SIZE];
    kc_trust_noise_hkdf_2(first_key, second_key, state->ck, NULL, 0);
    kc_trust_cipher_initialize_key(first, first_key);
    kc_trust_cipher_initialize_key(second, second_key);
    crypto_wipe(first_key, sizeof(first_key));
    crypto_wipe(second_key, sizeof(second_key));
}

/**
 * Initializes Noise X and processes its responder-static pre-message.
 * @param state Symmetric state output.
 * @param recipient_pk Responder static public key.
 * @return Nothing.
 */
static void kc_trust_noise_initialize(kc_trust_symmetric_state_t *state,
const unsigned char recipient_pk[KC_TRUST_PK_SIZE]) {
    static const unsigned char name[] =
        "Noise_X_25519_ChaChaPoly_BLAKE2b";
    memset(state->h, 0, KC_TRUST_HASH_SIZE);
    memcpy(state->h, name, sizeof(name) - 1);
    memcpy(state->ck, state->h, KC_TRUST_HASH_SIZE);
    kc_trust_cipher_initialize_empty(&state->cipher);
    kc_trust_noise_mix_hash(state, NULL, 0);
    kc_trust_noise_mix_hash(state, recipient_pk, KC_TRUST_PK_SIZE);
}

/**
 * Validates that an X25519 shared secret is not all-zero.
 * @param ss 32-byte shared secret.
 * @return 1 if valid, 0 if all-zero (low-order point).
 */
static int kc_trust_x25519_valid(const unsigned char ss[32]) {
    unsigned char z[32];
    memset(z, 0, 32);
    return crypto_verify32(ss, z) != 0;
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_trust_version(void) {
    return (uint64_t)KC_TRUST_BUILD_VERSION;
}

/**
 * Generates a fresh random keypair for a new identity.
 * Zeroes both output buffers, then fills the secret key with random bytes
 * from the platform entropy backend and derives the public key.
 * Memory-only: no files, no environment, no paths.
 * @param secret_key Destination for the random 32-byte secret key.
 * @param public_key Destination for the derived 32-byte public key.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_generate(unsigned char secret_key[KC_TRUST_SK_SIZE],
unsigned char public_key[KC_TRUST_PK_SIZE]) {
    if (!secret_key || !public_key) return KC_TRUST_ERROR;
    memset(secret_key, 0, KC_TRUST_SK_SIZE);
    memset(public_key, 0, KC_TRUST_PK_SIZE);
    if (kc_trust_read_random(secret_key, KC_TRUST_SK_SIZE) != 0) {
        memset(secret_key, 0, KC_TRUST_SK_SIZE);
        memset(public_key, 0, KC_TRUST_PK_SIZE);
        return KC_TRUST_ERROR;
    }
    crypto_x25519_public_key(public_key, secret_key);
    return KC_TRUST_OK;
}

/**
 * Initialises a new trust context from a caller-provided secret key.
 * Copies the secret key, derives the context public key, and initializes
 * empty in-memory TOFU state.  Memory-only: no files, no environment.
 * @param out Destination context pointer.
 * @param secret_key 32-byte secret key to install in the context.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_create(kc_trust_t **out,
const unsigned char secret_key[KC_TRUST_SK_SIZE]) {
    if (out) *out = NULL;
    if (!out || !secret_key) return KC_TRUST_ERROR;
    kc_trust_t *ctx = (kc_trust_t *)calloc(1, sizeof(kc_trust_t));
    if (!ctx) return KC_TRUST_ERROR;
    memcpy(ctx->sk, secret_key, KC_TRUST_SK_SIZE);
    crypto_x25519_public_key(ctx->pk, ctx->sk);
    ctx->has_identity = 1;
    *out = ctx;
    return KC_TRUST_OK;
}

/**
 * Releases a trust context and wipes all sensitive material.
 * @param ctx Context pointer. NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_close(kc_trust_t *ctx) {
    if (!ctx) return;
    crypto_wipe(ctx->sk, KC_TRUST_SK_SIZE);
    for (int i = 0; i < ctx->trust_count; i++)
        crypto_wipe(ctx->trust[i].pk, KC_TRUST_PK_SIZE);
    crypto_wipe(ctx, sizeof(*ctx));
    free(ctx);
}

/**
 * Returns the context's 32-byte public key.
 * @param ctx Context pointer.
 * @return Pointer to 32 bytes, or NULL on error.
 */
const unsigned char *kc_trust_public_key(const kc_trust_t *ctx) {
    if (!ctx || !ctx->has_identity) return NULL;
    return ctx->pk;
}

/**
 * Protects an outgoing message.
 * @param ctx Context (carries local identity).
 * @param recipient_pk 32-byte recipient public key.
 * @param message Application message.
 * @param message_len Message length.
 * @param payload Destination pointer for the allocated encrypted payload.
 * @param payload_len Destination payload length.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_seal(const kc_trust_t *ctx,
const unsigned char recipient_pk[KC_TRUST_PK_SIZE],
const unsigned char *message, size_t message_len,
unsigned char **payload, size_t *payload_len) {
    if (payload) *payload = NULL;
    if (payload_len) *payload_len = 0;
    if (!payload || !payload_len) return KC_TRUST_ERROR;
    if (!ctx || !ctx->has_identity || !recipient_pk)
        return KC_TRUST_ERROR;
    if (message_len > KC_TRUST_MAX_MESSAGE)
        return KC_TRUST_ERROR;
    if (message_len > 0 && !message) return KC_TRUST_ERROR;

    size_t encoded_len = kc_trust_payload_size(message_len);
    unsigned char *encoded = (unsigned char *)malloc(encoded_len);
    if (!encoded) return KC_TRUST_ERROR;

    unsigned char eph_sk[KC_TRUST_SK_SIZE];
    if (kc_trust_read_random(eph_sk, KC_TRUST_SK_SIZE) != 0) {
        free(encoded);
        return KC_TRUST_ERROR;
    }
    crypto_x25519_public_key(encoded, eph_sk);

    kc_trust_symmetric_state_t state;
    kc_trust_noise_initialize(&state, recipient_pk);
    kc_trust_noise_mix_hash(&state, encoded, KC_TRUST_PK_SIZE);

    unsigned char dh[32];
    crypto_x25519(dh, eph_sk, recipient_pk);
    if (!kc_trust_x25519_valid(dh)) {
        crypto_wipe(dh, sizeof(dh));
        crypto_wipe(eph_sk, sizeof(eph_sk));
        crypto_wipe(&state, sizeof(state));
        free(encoded);
        return KC_TRUST_ERROR;
    }
    kc_trust_noise_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));

    unsigned char *encrypted_static = encoded + KC_TRUST_PK_SIZE;
    if (kc_trust_noise_encrypt_and_hash(&state, ctx->pk,
        KC_TRUST_PK_SIZE, encrypted_static) != 0) {
        crypto_wipe(eph_sk, sizeof(eph_sk));
        crypto_wipe(&state, sizeof(state));
        free(encoded);
        return KC_TRUST_ERROR;
    }

    crypto_x25519(dh, ctx->sk, recipient_pk);
    if (!kc_trust_x25519_valid(dh)) {
        crypto_wipe(dh, sizeof(dh));
        crypto_wipe(eph_sk, sizeof(eph_sk));
        crypto_wipe(&state, sizeof(state));
        free(encoded);
        return KC_TRUST_ERROR;
    }
    kc_trust_noise_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));

    unsigned char *empty_handshake_payload =
        encrypted_static + KC_TRUST_ENCRYPTED_PK_SIZE;
    if (kc_trust_noise_encrypt_and_hash(&state, NULL, 0,
        empty_handshake_payload) != 0) {
        crypto_wipe(eph_sk, sizeof(eph_sk));
        crypto_wipe(&state, sizeof(state));
        free(encoded);
        return KC_TRUST_ERROR;
    }

    kc_trust_cipher_state_t first;
    kc_trust_cipher_state_t second;
    kc_trust_noise_split(&state, &first, &second);
    unsigned char logical_length[KC_TRUST_LOGICAL_LENGTH_SIZE];
    kc_trust_store_u64_be(logical_length, message_len);
    unsigned char *record = encoded + KC_TRUST_HANDSHAKE_SIZE;
    if (kc_trust_cipher_encrypt(&first, NULL, 0, logical_length,
        sizeof(logical_length), record) != 0) {
        crypto_wipe(&first, sizeof(first));
        crypto_wipe(&second, sizeof(second));
        crypto_wipe(logical_length, sizeof(logical_length));
        crypto_wipe(eph_sk, sizeof(eph_sk));
        crypto_wipe(&state, sizeof(state));
        free(encoded);
        return KC_TRUST_ERROR;
    }
    crypto_wipe(logical_length, sizeof(logical_length));
    record += KC_TRUST_LENGTH_RECORD_SIZE;
    size_t offset = 0;
    while (offset < message_len) {
        size_t chunk_len = message_len - offset;
        if (chunk_len > KC_TRUST_TRANSPORT_PLAINTEXT_MAX)
            chunk_len = KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
        if (kc_trust_cipher_encrypt(&first, NULL, 0, message + offset,
            chunk_len, record) != 0) {
            crypto_wipe(&first, sizeof(first));
            crypto_wipe(&second, sizeof(second));
            crypto_wipe(eph_sk, sizeof(eph_sk));
            crypto_wipe(&state, sizeof(state));
            free(encoded);
            return KC_TRUST_ERROR;
        }
        offset += chunk_len;
        record += chunk_len + KC_TRUST_MAC_SIZE;
    }
    crypto_wipe(&first, sizeof(first));
    crypto_wipe(&second, sizeof(second));
    crypto_wipe(eph_sk, sizeof(eph_sk));
    crypto_wipe(&state, sizeof(state));

    *payload = encoded;
    *payload_len = encoded_len;
    return KC_TRUST_OK;
}

/**
 * Explicitly trusts a peer binding.
 * @param ctx Context.
 * @param peer_id Opaque caller-defined peer identifier.
 * @param peer_id_len Identifier length (must be > 0).
 * @param peer_pk 32-byte public key to bind.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_trust(kc_trust_t *ctx,
    const unsigned char *peer_id, size_t peer_id_len,
    const unsigned char peer_pk[KC_TRUST_PK_SIZE]) {
    if (!ctx || !peer_id || peer_id_len == 0 || !peer_pk) return KC_TRUST_ERROR;
    if (peer_id_len > KC_TRUST_MAX_PEER_ID) return KC_TRUST_ERROR;

    for (int i = 0; i < ctx->trust_count; i++) {
        if (ctx->trust[i].id_len == peer_id_len &&
            memcmp(ctx->trust[i].id, peer_id, peer_id_len) == 0) {
            memcpy(ctx->trust[i].pk, peer_pk, KC_TRUST_PK_SIZE);
            return KC_TRUST_OK;
        }
    }

    if (ctx->trust_count >= KC_TRUST_MAX_TRUST) return KC_TRUST_ERROR;

    kc_trust_trust_entry_t *e = &ctx->trust[ctx->trust_count++];
    e->id_len = peer_id_len;
    memcpy(e->id, peer_id, peer_id_len);
    memcpy(e->pk, peer_pk, KC_TRUST_PK_SIZE);
    return KC_TRUST_OK;
}

/**
 * Removes a peer trust binding.
 * @param ctx Context.
 * @param peer_id Opaque caller-defined peer identifier.
 * @param peer_id_len Identifier length (must be > 0).
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR if not found.
 */
int kc_trust_forget(kc_trust_t *ctx,
    const unsigned char *peer_id, size_t peer_id_len) {
    if (!ctx || !peer_id || peer_id_len == 0) return KC_TRUST_ERROR;
    if (peer_id_len > KC_TRUST_MAX_PEER_ID) return KC_TRUST_ERROR;

    for (int i = 0; i < ctx->trust_count; i++) {
        if (ctx->trust[i].id_len == peer_id_len &&
            memcmp(ctx->trust[i].id, peer_id, peer_id_len) == 0) {
            crypto_wipe(ctx->trust[i].pk, KC_TRUST_PK_SIZE);
            ctx->trust_count--;
            if (i < ctx->trust_count)
                ctx->trust[i] = ctx->trust[ctx->trust_count];
            return KC_TRUST_OK;
        }
    }
    return KC_TRUST_ERROR;
}

/**
 * Evaluates trust status for a sender public key against a peer
 * identifier in the trust store.
 * @param ctx Context.
 * @param peer_id Caller-defined peer identifier.
 * @param peer_id_len Peer identifier length.
 * @param sender_pk Sender public key to compare.
 * @return Trust status code.
 */
static int kc_trust_eval_trust(const kc_trust_t *ctx,
    const unsigned char *peer_id, size_t peer_id_len,
    const unsigned char sender_pk[KC_TRUST_PK_SIZE]) {
    for (int i = 0; i < ctx->trust_count; i++) {
        if (ctx->trust[i].id_len == peer_id_len &&
            memcmp(ctx->trust[i].id, peer_id, peer_id_len) == 0) {
            if (crypto_verify32(ctx->trust[i].pk, sender_pk) == 0)
                return KC_TRUST_OK;
            return KC_TRUST_PEER_CHANGED;
        }
    }
    return KC_TRUST_PEER_NEW;
}

/**
 * Authenticates and opens an encrypted payload.
 * @param ctx Context (carries local identity and trust state).
 * @param payload Encrypted payload produced by kc_trust_seal.
 * @param payload_len Payload length.
 * @param peer_id Optional peer identifier for trust evaluation.
 * @param peer_id_len Peer identifier length.
 * @param result Destination result. Caller must free with
 *                kc_trust_result_free().
 * @return KC_TRUST_OK when the result is populated, KC_TRUST_ERROR on failure.
 */
int kc_trust_open(const kc_trust_t *ctx,
const unsigned char *payload, size_t payload_len,
const unsigned char *peer_id, size_t peer_id_len,
kc_trust_result_t **result) {
    if (result) *result = NULL;
    if (!result || !ctx || !payload) return KC_TRUST_ERROR;
    if (peer_id && (peer_id_len == 0 ||
        peer_id_len > KC_TRUST_MAX_PEER_ID)) return KC_TRUST_ERROR;

    if (payload_len < KC_TRUST_PAYLOAD_BASE_SIZE)
        return KC_TRUST_ERROR;
    if (payload_len > KC_TRUST_MAX_PAYLOAD)
        return KC_TRUST_ERROR;

    const unsigned char *eph_pk = payload;
    const unsigned char *encrypted_static = eph_pk + KC_TRUST_PK_SIZE;
    const unsigned char *empty_handshake_payload =
        encrypted_static + KC_TRUST_ENCRYPTED_PK_SIZE;
    const unsigned char *length_record =
        payload + KC_TRUST_HANDSHAKE_SIZE;

    kc_trust_symmetric_state_t state;
    kc_trust_noise_initialize(&state, ctx->pk);
    kc_trust_noise_mix_hash(&state, eph_pk, KC_TRUST_PK_SIZE);

    unsigned char dh[32];
    crypto_x25519(dh, ctx->sk, eph_pk);
    if (!kc_trust_x25519_valid(dh)) {
        crypto_wipe(dh, sizeof(dh));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    kc_trust_noise_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));

    unsigned char sender_pk[KC_TRUST_PK_SIZE];
    if (kc_trust_noise_decrypt_and_hash(&state, encrypted_static,
        KC_TRUST_ENCRYPTED_PK_SIZE, sender_pk) != 0) {
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }

    crypto_x25519(dh, ctx->sk, sender_pk);
    if (!kc_trust_x25519_valid(dh)) {
        crypto_wipe(dh, sizeof(dh));
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    kc_trust_noise_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));

    unsigned char empty_payload[1];
    if (kc_trust_noise_decrypt_and_hash(&state, empty_handshake_payload,
        KC_TRUST_MAC_SIZE, empty_payload) != 0) {
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }

    kc_trust_cipher_state_t first;
    kc_trust_cipher_state_t second;
    kc_trust_noise_split(&state, &first, &second);
    unsigned char logical_length[KC_TRUST_LOGICAL_LENGTH_SIZE];
    if (kc_trust_cipher_decrypt(&first, NULL, 0, length_record,
        KC_TRUST_LENGTH_RECORD_SIZE, logical_length) != 0) {
        crypto_wipe(&first, sizeof(first));
        crypto_wipe(&second, sizeof(second));
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    uint64_t encoded_len = kc_trust_load_u64_be(logical_length);
    crypto_wipe(logical_length, sizeof(logical_length));
    if (encoded_len > KC_TRUST_MAX_MESSAGE) {
        crypto_wipe(&first, sizeof(first));
        crypto_wipe(&second, sizeof(second));
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    size_t message_len = (size_t)encoded_len;
    if (payload_len != kc_trust_payload_size(message_len)) {
        crypto_wipe(&first, sizeof(first));
        crypto_wipe(&second, sizeof(second));
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    unsigned char *message = (unsigned char *)malloc(
        message_len > 0 ? message_len : 1);
    if (!message) {
        crypto_wipe(&first, sizeof(first));
        crypto_wipe(&second, sizeof(second));
        crypto_wipe(sender_pk, sizeof(sender_pk));
        crypto_wipe(&state, sizeof(state));
        return KC_TRUST_ERROR;
    }
    const unsigned char *record = payload + KC_TRUST_PAYLOAD_BASE_SIZE;
    size_t offset = 0;
    while (offset < message_len) {
        size_t chunk_len = message_len - offset;
        if (chunk_len > KC_TRUST_TRANSPORT_PLAINTEXT_MAX)
            chunk_len = KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
        if (kc_trust_cipher_decrypt(&first, NULL, 0, record,
            chunk_len + KC_TRUST_MAC_SIZE, message + offset) != 0) {
            crypto_wipe(message, message_len);
            free(message);
            crypto_wipe(&first, sizeof(first));
            crypto_wipe(&second, sizeof(second));
            crypto_wipe(sender_pk, sizeof(sender_pk));
            crypto_wipe(&state, sizeof(state));
            return KC_TRUST_ERROR;
        }
        offset += chunk_len;
        record += chunk_len + KC_TRUST_MAC_SIZE;
    }
    crypto_wipe(&first, sizeof(first));
    crypto_wipe(&second, sizeof(second));
    crypto_wipe(&state, sizeof(state));

    kc_trust_result_t *r = (kc_trust_result_t *)calloc(1, sizeof(kc_trust_result_t));
    if (!r) {
        crypto_wipe(message, message_len);
        free(message);
        crypto_wipe(sender_pk, sizeof(sender_pk));
        return KC_TRUST_ERROR;
    }

    memcpy(r->peer_pk, sender_pk, KC_TRUST_PK_SIZE);
    r->message = message;
    r->message_len = message_len;

    if (peer_id) {
        r->status = kc_trust_eval_trust(ctx, peer_id, peer_id_len, sender_pk);
    } else {
        r->status = KC_TRUST_OK;
    }
    crypto_wipe(sender_pk, sizeof(sender_pk));

    *result = r;
    return KC_TRUST_OK;
}

/**
 * Releases an allocation returned by kc_trust_seal().
 * @param ptr Payload pointer. NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_free(void *ptr) {
    free(ptr);
}

/**
 * Releases resources owned by a result.
 * @param result Result pointer. NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_result_free(kc_trust_result_t *result) {
    if (!result) return;
    if (result->message && result->message_len > 0)
        crypto_wipe(result->message, result->message_len);
    free(result->message);
    crypto_wipe(result, sizeof(*result));
    free(result);
}
