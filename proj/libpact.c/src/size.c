// size.c - Incremental protected-message size detection for pact.c.
//
// Author:  KaisarCode
// Website: https://kaisarcode.com
// License: https://www.gnu.org/licenses/lgpl-3.0.html

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libpact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "monocypher.h"

#define KC_PACT_SIZE_PATH_SIZE 4096
#define KC_PACT_SIZE_UID_BYTES 16
#define KC_PACT_SIZE_SK_SIZE 32
#define KC_PACT_SIZE_PK_SIZE 32
#define KC_PACT_SIZE_MAC_SIZE 16
#define KC_PACT_SIZE_NONCE_SIZE 12
#define KC_PACT_SIZE_HASH_SIZE 64
#define KC_PACT_SIZE_BLAKE2B_BLOCK 128
#define KC_PACT_SIZE_LOCAL_RECORD_SIZE (4 + KC_PACT_SIZE_UID_BYTES)
#define KC_PACT_SIZE_PEER_RECORD_SIZE (4 + KC_PACT_SIZE_UID_BYTES + KC_PACT_SIZE_SK_SIZE + KC_PACT_SIZE_PK_SIZE)
#define KC_PACT_SIZE_K_HANDSHAKE_SIZE (KC_PACT_SIZE_PK_SIZE + KC_PACT_SIZE_MAC_SIZE)
#define KC_PACT_SIZE_LENGTH_RECORD_SIZE (8 + KC_PACT_SIZE_MAC_SIZE)
#define KC_PACT_SIZE_PAYLOAD_BASE_SIZE (KC_PACT_SIZE_K_HANDSHAKE_SIZE + KC_PACT_SIZE_LENGTH_RECORD_SIZE)
#define KC_PACT_SIZE_TRANSPORT_MESSAGE_MAX 65535
#define KC_PACT_SIZE_TRANSPORT_PLAINTEXT_MAX (KC_PACT_SIZE_TRANSPORT_MESSAGE_MAX - KC_PACT_SIZE_MAC_SIZE)

struct kc_pact {
    char dir[KC_PACT_SIZE_PATH_SIZE];
};

typedef struct {
    unsigned char k[32];
    uint64_t n;
    int has_key;
} kc_pact_size_cipher_t;

typedef struct {
    unsigned char ck[KC_PACT_SIZE_HASH_SIZE];
    unsigned char h[KC_PACT_SIZE_HASH_SIZE];
    kc_pact_size_cipher_t cipher;
} kc_pact_size_noise_t;

static const unsigned char kc_pact_size_local_magic[4] = { 'K', 'T', 'L', '1' };
static const unsigned char kc_pact_size_peer_magic[4] = { 'K', 'T', 'R', '1' };

static int kc_pact_size_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int kc_pact_size_uid_parse(const char *text,
    unsigned char uid[KC_PACT_SIZE_UID_BYTES])
{
    static const int hyphens[] = { 8, 13, 18, 23 };
    size_t i = 0;
    size_t out = 0;
    int h = 0;

    if (!text || strlen(text) != KC_PACT_UID_SIZE || !uid) return -1;
    while (i < KC_PACT_UID_SIZE) {
        int hi;
        int lo;

        if (h < 4 && (int)i == hyphens[h]) {
            if (text[i] != '-') return -1;
            i++;
            h++;
            continue;
        }
        if (i + 1 >= KC_PACT_UID_SIZE || out >= KC_PACT_SIZE_UID_BYTES)
            return -1;
        hi = kc_pact_size_hex(text[i]);
        lo = kc_pact_size_hex(text[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        uid[out++] = (unsigned char)((hi << 4) | lo);
        i += 2;
    }
    return out == KC_PACT_SIZE_UID_BYTES ? 0 : -1;
}

static int kc_pact_size_uid_format(
    const unsigned char uid[KC_PACT_SIZE_UID_BYTES],
    char out[KC_PACT_UID_SIZE + 1])
{
    static const char hex[] = "0123456789abcdef";
    static const int hyphen_after[] = { 4, 6, 8, 10 };
    size_t pos = 0;
    int h = 0;

    if (!uid || !out) return -1;
    for (int i = 0; i < KC_PACT_SIZE_UID_BYTES; i++) {
        out[pos++] = hex[uid[i] >> 4];
        out[pos++] = hex[uid[i] & 15];
        if (h < 4 && i + 1 == hyphen_after[h]) {
            out[pos++] = '-';
            h++;
        }
    }
    out[pos] = '\0';
    return pos == KC_PACT_UID_SIZE ? 0 : -1;
}

static int kc_pact_size_path(char out[KC_PACT_SIZE_PATH_SIZE],
    const char *dir, const char *kind, const char *uid)
{
    size_t n;

    if (!out || !dir || !kind || !uid) return -1;
#ifdef _WIN32
    n = (size_t)snprintf(out, KC_PACT_SIZE_PATH_SIZE, "%s\\%s\\%s",
        dir, kind, uid);
#else
    n = (size_t)snprintf(out, KC_PACT_SIZE_PATH_SIZE, "%s/%s/%s",
        dir, kind, uid);
#endif
    return n < KC_PACT_SIZE_PATH_SIZE ? 0 : -1;
}

static int kc_pact_size_read(const char *path, unsigned char *data,
    size_t size)
{
    FILE *file;
    size_t done;
    int extra;

    if (!path || !data) return -1;
    file = fopen(path, "rb");
    if (!file) return -1;
    done = fread(data, 1, size, file);
    extra = fgetc(file);
    if (fclose(file) != 0) return -1;
    return done == size && extra == EOF ? 0 : -1;
}

static int kc_pact_size_relation(kc_pact_t *trust, const char *uid,
    unsigned char local_uid[KC_PACT_SIZE_UID_BYTES],
    unsigned char remote_uid[KC_PACT_SIZE_UID_BYTES],
    unsigned char local_sk[KC_PACT_SIZE_SK_SIZE],
    unsigned char remote_pk[KC_PACT_SIZE_PK_SIZE])
{
    unsigned char local_record[KC_PACT_SIZE_LOCAL_RECORD_SIZE];
    unsigned char peer_record[KC_PACT_SIZE_PEER_RECORD_SIZE];
    unsigned char parsed_uid[KC_PACT_SIZE_UID_BYTES];
    char remote_text[KC_PACT_UID_SIZE + 1];
    char path[KC_PACT_SIZE_PATH_SIZE];
    int rc = -1;

    if (!trust || !uid ||
        kc_pact_size_uid_parse(uid, parsed_uid) != 0 ||
        kc_pact_size_path(path, trust->dir, "local", uid) != 0 ||
        kc_pact_size_read(path, local_record, sizeof(local_record)) != 0 ||
        memcmp(local_record, kc_pact_size_local_magic, 4) != 0)
        goto done;

    memcpy(remote_uid, local_record + 4, KC_PACT_SIZE_UID_BYTES);
    if (kc_pact_size_uid_format(remote_uid, remote_text) != 0 ||
        kc_pact_size_path(path, trust->dir, "peers", remote_text) != 0 ||
        kc_pact_size_read(path, peer_record, sizeof(peer_record)) != 0 ||
        memcmp(peer_record, kc_pact_size_peer_magic, 4) != 0)
        goto done;

    memcpy(local_uid, peer_record + 4, KC_PACT_SIZE_UID_BYTES);
    if (memcmp(local_uid, parsed_uid, KC_PACT_SIZE_UID_BYTES) != 0)
        goto done;
    memcpy(local_sk, peer_record + 4 + KC_PACT_SIZE_UID_BYTES,
        KC_PACT_SIZE_SK_SIZE);
    memcpy(remote_pk,
        peer_record + 4 + KC_PACT_SIZE_UID_BYTES + KC_PACT_SIZE_SK_SIZE,
        KC_PACT_SIZE_PK_SIZE);
    rc = 0;

done:
    crypto_wipe(local_record, sizeof(local_record));
    crypto_wipe(peer_record, sizeof(peer_record));
    crypto_wipe(parsed_uid, sizeof(parsed_uid));
    crypto_wipe(remote_text, sizeof(remote_text));
    crypto_wipe(path, sizeof(path));
    return rc;
}

static void kc_pact_size_hmac(unsigned char out[KC_PACT_SIZE_HASH_SIZE],
    const unsigned char *key, size_t key_size,
    const unsigned char *data, size_t data_size)
{
    unsigned char kpad[KC_PACT_SIZE_BLAKE2B_BLOCK];
    unsigned char outer[KC_PACT_SIZE_BLAKE2B_BLOCK];
    unsigned char inner_pad[KC_PACT_SIZE_BLAKE2B_BLOCK];
    unsigned char inner[KC_PACT_SIZE_HASH_SIZE];
    crypto_blake2b_ctx hash;

    memset(kpad, 0, sizeof(kpad));
    memcpy(kpad, key, key_size);
    memcpy(outer, kpad, sizeof(outer));
    memcpy(inner_pad, kpad, sizeof(inner_pad));
    for (size_t i = 0; i < sizeof(kpad); i++) {
        outer[i] ^= 0x5c;
        inner_pad[i] ^= 0x36;
    }
    crypto_blake2b_init(&hash, KC_PACT_SIZE_HASH_SIZE);
    crypto_blake2b_update(&hash, inner_pad, sizeof(inner_pad));
    if (data_size) crypto_blake2b_update(&hash, data, data_size);
    crypto_blake2b_final(&hash, inner);
    crypto_blake2b_init(&hash, KC_PACT_SIZE_HASH_SIZE);
    crypto_blake2b_update(&hash, outer, sizeof(outer));
    crypto_blake2b_update(&hash, inner, sizeof(inner));
    crypto_blake2b_final(&hash, out);
    crypto_wipe(kpad, sizeof(kpad));
    crypto_wipe(outer, sizeof(outer));
    crypto_wipe(inner_pad, sizeof(inner_pad));
    crypto_wipe(inner, sizeof(inner));
}

static void kc_pact_size_hkdf2(unsigned char out1[KC_PACT_SIZE_HASH_SIZE],
    unsigned char out2[KC_PACT_SIZE_HASH_SIZE],
    const unsigned char ck[KC_PACT_SIZE_HASH_SIZE],
    const unsigned char *ikm, size_t ikm_size)
{
    unsigned char temp[KC_PACT_SIZE_HASH_SIZE];
    unsigned char input[KC_PACT_SIZE_HASH_SIZE + 1];
    unsigned char one = 1;

    kc_pact_size_hmac(temp, ck, KC_PACT_SIZE_HASH_SIZE, ikm, ikm_size);
    kc_pact_size_hmac(out1, temp, KC_PACT_SIZE_HASH_SIZE, &one, 1);
    memcpy(input, out1, KC_PACT_SIZE_HASH_SIZE);
    input[KC_PACT_SIZE_HASH_SIZE] = 2;
    kc_pact_size_hmac(out2, temp, KC_PACT_SIZE_HASH_SIZE,
        input, sizeof(input));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(input, sizeof(input));
}

static void kc_pact_size_cipher_key(kc_pact_size_cipher_t *cipher,
    const unsigned char key[32])
{
    memcpy(cipher->k, key, 32);
    cipher->n = 0;
    cipher->has_key = 1;
}

static int kc_pact_size_cipher_decrypt(kc_pact_size_cipher_t *cipher,
    const unsigned char *ad, size_t ad_size,
    const unsigned char *data, size_t data_size, unsigned char *out)
{
    unsigned char nonce[KC_PACT_SIZE_NONCE_SIZE] = {0};
    crypto_aead_ctx aead;
    size_t plain_size;
    int rc;

    if (!cipher || !cipher->has_key || cipher->n == UINT64_MAX ||
        !data || data_size < KC_PACT_SIZE_MAC_SIZE || !out)
        return -1;
    plain_size = data_size - KC_PACT_SIZE_MAC_SIZE;
    for (size_t i = 0; i < 8; i++)
        nonce[4 + i] = (unsigned char)(cipher->n >> (8 * i));
    crypto_aead_init_ietf(&aead, cipher->k, nonce);
    rc = crypto_aead_read(&aead, out, data + plain_size,
        ad, ad_size, data, plain_size);
    crypto_wipe(&aead, sizeof(aead));
    if (rc != 0) return -1;
    cipher->n++;
    return 0;
}

static void kc_pact_size_mix_hash(kc_pact_size_noise_t *state,
    const unsigned char *data, size_t size)
{
    unsigned char next[KC_PACT_SIZE_HASH_SIZE];
    crypto_blake2b_ctx hash;

    crypto_blake2b_init(&hash, KC_PACT_SIZE_HASH_SIZE);
    crypto_blake2b_update(&hash, state->h, KC_PACT_SIZE_HASH_SIZE);
    if (size) crypto_blake2b_update(&hash, data, size);
    crypto_blake2b_final(&hash, next);
    memcpy(state->h, next, sizeof(next));
    crypto_wipe(next, sizeof(next));
}

static void kc_pact_size_mix_key(kc_pact_size_noise_t *state,
    const unsigned char *input, size_t size)
{
    unsigned char ck[KC_PACT_SIZE_HASH_SIZE];
    unsigned char key[KC_PACT_SIZE_HASH_SIZE];

    kc_pact_size_hkdf2(ck, key, state->ck, input, size);
    memcpy(state->ck, ck, sizeof(ck));
    kc_pact_size_cipher_key(&state->cipher, key);
    crypto_wipe(ck, sizeof(ck));
    crypto_wipe(key, sizeof(key));
}

static void kc_pact_size_split(const kc_pact_size_noise_t *state,
    kc_pact_size_cipher_t *first, kc_pact_size_cipher_t *second)
{
    unsigned char first_key[KC_PACT_SIZE_HASH_SIZE];
    unsigned char second_key[KC_PACT_SIZE_HASH_SIZE];

    kc_pact_size_hkdf2(first_key, second_key, state->ck, NULL, 0);
    kc_pact_size_cipher_key(first, first_key);
    kc_pact_size_cipher_key(second, second_key);
    crypto_wipe(first_key, sizeof(first_key));
    crypto_wipe(second_key, sizeof(second_key));
}

static void kc_pact_size_noise_init(kc_pact_size_noise_t *state,
    const unsigned char initiator_uid[KC_PACT_SIZE_UID_BYTES],
    const unsigned char responder_uid[KC_PACT_SIZE_UID_BYTES])
{
    static const char name[] = "Noise_K_25519_ChaChaPoly_BLAKE2b";
    unsigned char prologue[2 * KC_PACT_SIZE_UID_BYTES];
    size_t name_size = strlen(name);

    memset(state, 0, sizeof(*state));
    if (name_size <= KC_PACT_SIZE_HASH_SIZE)
        memcpy(state->h, name, name_size);
    else
        crypto_blake2b(state->h, KC_PACT_SIZE_HASH_SIZE,
            (const unsigned char *)name, name_size);
    memcpy(state->ck, state->h, KC_PACT_SIZE_HASH_SIZE);
    memcpy(prologue, initiator_uid, KC_PACT_SIZE_UID_BYTES);
    memcpy(prologue + KC_PACT_SIZE_UID_BYTES, responder_uid,
        KC_PACT_SIZE_UID_BYTES);
    kc_pact_size_mix_hash(state, prologue, sizeof(prologue));
    crypto_wipe(prologue, sizeof(prologue));
}

static int kc_pact_size_x25519(unsigned char out[32],
    const unsigned char sk[32], const unsigned char pk[32])
{
    unsigned char zero[32] = {0};

    crypto_x25519(out, sk, pk);
    if (crypto_verify32(out, zero) == 0) {
        crypto_wipe(out, 32);
        return -1;
    }
    return 0;
}

static uint64_t kc_pact_size_u64(const unsigned char in[8])
{
    uint64_t value = 0;

    for (size_t i = 0; i < 8; i++) value = (value << 8) | in[i];
    return value;
}

static size_t kc_pact_size_record_count(size_t size)
{
    if (size == 0) return 0;
    return 1 + (size - 1) / KC_PACT_SIZE_TRANSPORT_PLAINTEXT_MAX;
}

static size_t kc_pact_size_payload_size(size_t size)
{
    return KC_PACT_SIZE_PAYLOAD_BASE_SIZE + size +
        kc_pact_size_record_count(size) * KC_PACT_SIZE_MAC_SIZE;
}

int kc_pact_size(kc_pact_t *trust, const char *uid,
    const void *data, size_t data_size, size_t *out_size)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned char local_uid[KC_PACT_SIZE_UID_BYTES];
    unsigned char remote_uid[KC_PACT_SIZE_UID_BYTES];
    unsigned char local_sk[KC_PACT_SIZE_SK_SIZE];
    unsigned char remote_pk[KC_PACT_SIZE_PK_SIZE];
    unsigned char local_pk[KC_PACT_SIZE_PK_SIZE];
    unsigned char dh[32];
    unsigned char empty[1];
    unsigned char logical[8];
    kc_pact_size_noise_t state;
    kc_pact_size_cipher_t first;
    kc_pact_size_cipher_t second;
    uint64_t logical_size;
    int rc = KC_PACT_ERROR;

    if (out_size) *out_size = 0;
    if (!trust || !uid || !out_size || (!data && data_size))
        return KC_PACT_ERROR;
    if (data_size < KC_PACT_SIZE_PAYLOAD_BASE_SIZE)
        return KC_PACT_INCOMPLETE;
    if (kc_pact_size_relation(trust, uid, local_uid, remote_uid,
        local_sk, remote_pk) != 0)
        goto done;

    crypto_x25519_public_key(local_pk, local_sk);
    kc_pact_size_noise_init(&state, remote_uid, local_uid);
    kc_pact_size_mix_hash(&state, remote_pk, KC_PACT_SIZE_PK_SIZE);
    kc_pact_size_mix_hash(&state, local_pk, KC_PACT_SIZE_PK_SIZE);
    kc_pact_size_mix_hash(&state, p, KC_PACT_SIZE_PK_SIZE);
    if (kc_pact_size_x25519(dh, local_sk, p) != 0) goto done;
    kc_pact_size_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    if (kc_pact_size_x25519(dh, local_sk, remote_pk) != 0) goto done;
    kc_pact_size_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    p += KC_PACT_SIZE_PK_SIZE;
    if (kc_pact_size_cipher_decrypt(&state.cipher, state.h, KC_PACT_SIZE_HASH_SIZE,
        p, KC_PACT_SIZE_MAC_SIZE, empty) != 0)
        goto done;
    kc_pact_size_mix_hash(&state, p, KC_PACT_SIZE_MAC_SIZE);
    p += KC_PACT_SIZE_MAC_SIZE;
    kc_pact_size_split(&state, &first, &second);
    if (kc_pact_size_cipher_decrypt(&first, NULL, 0, p,
        KC_PACT_SIZE_LENGTH_RECORD_SIZE, logical) != 0)
        goto done;

    logical_size = kc_pact_size_u64(logical);
    if (logical_size > KC_PACT_MAX_MESSAGE) goto done;
    *out_size = kc_pact_size_payload_size((size_t)logical_size);
    rc = KC_PACT_OK;

done:
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    crypto_wipe(local_pk, sizeof(local_pk));
    crypto_wipe(dh, sizeof(dh));
    crypto_wipe(empty, sizeof(empty));
    crypto_wipe(logical, sizeof(logical));
    crypto_wipe(&state, sizeof(state));
    crypto_wipe(&first, sizeof(first));
    crypto_wipe(&second, sizeof(second));
    return rc;
}
