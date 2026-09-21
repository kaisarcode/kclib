/**
 * test.c - libtrust public API contract tests.
 * Summary: Validates each exported libtrust function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libtrust.h"
#include "monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Function under test.
 * @param detail Behavior verified.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

#define TEST_NOISE_MAC_SIZE 16
#define TEST_HANDSHAKE_SIZE 96
#define TEST_TRANSPORT_MESSAGE_MAX 65535
#define TEST_TRANSPORT_PLAINTEXT_MAX 65519
#define TEST_LENGTH_RECORD_SIZE 24
#define TEST_PAYLOAD_BASE_SIZE 120

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero means pass.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies a byte sequence.
 * @param name Check description.
 * @param expected Expected bytes.
 * @param actual Actual bytes.
 * @param len Number of bytes to compare.
 * @return 0 on success, 1 on failure.
 */
static int expect_bytes(const char *name, const unsigned char *expected, const unsigned char *actual, size_t len) {
    if (memcmp(expected, actual, len) != 0) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Decodes an even-length lowercase hexadecimal string.
 * @param hex Hexadecimal input.
 * @param output Decoded byte output.
 * @param output_len Expected output length.
 * @return 0 on success, 1 on malformed input.
 */
static int decode_hex(const char *hex, unsigned char *output, size_t output_len) {
    for (size_t i = 0; i < output_len; i++) {
        unsigned int value;
        if (sscanf(hex + i * 2, "%2x", &value) != 1) return 1;
        output[i] = (unsigned char)value;
    }
    return hex[output_len * 2] == '\0' ? 0 : 1;
}

/**
 * Returns the expected chunked payload size for a logical message.
 * @param plaintext_len Logical plaintext length.
 * @return Expected payload size.
 */
static size_t expected_payload_size(size_t plaintext_len) {
    size_t records = plaintext_len == 0 ? 0 :
        1 + (plaintext_len - 1) / TEST_TRANSPORT_PLAINTEXT_MAX;
    return TEST_PAYLOAD_BASE_SIZE + plaintext_len +
        records * TEST_NOISE_MAC_SIZE;
}

typedef struct {
    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char pk[KC_TRUST_PK_SIZE];
    kc_trust_t *ctx;
} test_identity_t;

/**
 * Opens a deterministic temporary identity.
 * @return 0 on success, 1 on failure.
 */
static int open_test_identity(test_identity_t *identity, unsigned char marker) {
    memset(identity, 0, sizeof(*identity));
    identity->sk[0] = marker;
    identity->sk[1] = marker;
    crypto_x25519_public_key(identity->pk, identity->sk);
    if (kc_trust_create(&identity->ctx, identity->sk) != KC_TRUST_OK) return 1;
    return 0;
}

/**
 * Releases a temporary identity.
 * @return Nothing.
 */
static void close_test_identity(test_identity_t *identity) {
    kc_trust_close(identity->ctx);
    memset(identity, 0, sizeof(*identity));
}

/**
 * Verifies one complete seal/open round trip.
 * @return 0 on success, 1 on failure.
 */
static int expect_round_trip(test_identity_t *sender,
test_identity_t *recipient, const unsigned char *message, size_t message_len) {
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    int rc = expect_int("seal round trip", KC_TRUST_OK,
        kc_trust_seal(sender->ctx, recipient->pk, message, message_len,
        &payload, &payload_len));
    rc += expect_true("round trip payload size",
        payload && payload_len == expected_payload_size(message_len));
    if (payload) {
        kc_trust_result_t *result = NULL;
        rc += expect_int("open round trip", KC_TRUST_OK,
            kc_trust_open(recipient->ctx, payload, payload_len,
            NULL, 0, &result));
        if (result) {
            rc += expect_true("round trip message",
                result->message_len == message_len &&
                (!message_len || memcmp(result->message, message,
                message_len) == 0));
            rc += expect_bytes("round trip sender", sender->pk,
                result->peer_pk, KC_TRUST_PK_SIZE);
            kc_trust_result_free(result);
        }
        kc_trust_free(payload);
    }
    return rc;
}

/**
 * Verifies that opening a payload fails without a result.
 * @return 0 on success, 1 on failure.
 */
static int expect_open_rejected(const char *name, kc_trust_t *ctx,
const unsigned char *payload, size_t payload_len) {
    kc_trust_result_t *result = NULL;
    int rc = expect_int(name, KC_TRUST_ERROR,
        kc_trust_open(ctx, payload, payload_len, NULL, 0, &result));
    return rc + expect_true("rejected payload exposes no result",
        result == NULL);
}

/**
 * Returns trust status after opening one payload.
 * @return Status, or KC_TRUST_ERROR.
 */
static int open_trust_status(kc_trust_t *ctx, const unsigned char *payload,
size_t payload_len, const unsigned char *peer_id, size_t peer_id_len) {
    kc_trust_result_t *result = NULL;
    if (kc_trust_open(ctx, payload, payload_len, peer_id, peer_id_len,
        &result) != KC_TRUST_OK || !result) return KC_TRUST_ERROR;
    int status = result->status;
    kc_trust_result_free(result);
    return status;
}

/**
 * Tests kc_trust_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_version(void) {
    const char *name = "kc_trust_version";
    const char *detail = "returns build timestamp";
    int fail = expect_true("version is nonzero", kc_trust_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_generate.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_generate(void) {
    const char *name = "kc_trust_generate";
    const char *detail = "generates a memory keypair and rejects NULL";
    int fail = 0;

    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char pk[KC_TRUST_PK_SIZE];
    unsigned char derived[KC_TRUST_PK_SIZE];
    fail += expect_int("generate keypair", KC_TRUST_OK,
        kc_trust_generate(sk, pk));
    crypto_x25519_public_key(derived, sk);
    fail += expect_bytes("generated public key", derived, pk, KC_TRUST_PK_SIZE);
    fail += expect_int("generate rejects NULL secret key", KC_TRUST_ERROR,
        kc_trust_generate(NULL, pk));
    fail += expect_int("generate rejects NULL public key", KC_TRUST_ERROR,
        kc_trust_generate(sk, NULL));
    crypto_wipe(sk, sizeof(sk));
    crypto_wipe(pk, sizeof(pk));
    crypto_wipe(derived, sizeof(derived));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_create.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_create(void) {
    const char *name = "kc_trust_create";
    const char *detail = "creates contexts from secret keys and rejects bad args";
    int fail = 0;

    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char derived[KC_TRUST_PK_SIZE];
    memset(sk, 0x44, sizeof(sk));
    crypto_x25519_public_key(derived, sk);
    fail += expect_int("create rejects NULL output", KC_TRUST_ERROR,
        kc_trust_create(NULL, sk));
    kc_trust_t *ctx = (kc_trust_t *)1;
    fail += expect_int("create rejects NULL secret key", KC_TRUST_ERROR,
        kc_trust_create(&ctx, NULL));
    fail += expect_true("failed create clears output", ctx == NULL);
    fail += expect_int("create valid context", KC_TRUST_OK,
        kc_trust_create(&ctx, sk));
    if (ctx) {
        fail += expect_bytes("created public key matches derivation", derived,
            kc_trust_public_key(ctx), KC_TRUST_PK_SIZE);
        kc_trust_close(ctx);
    }
    crypto_wipe(sk, sizeof(sk));
    crypto_wipe(derived, sizeof(derived));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_close(void) {
    const char *name = "kc_trust_close";
    const char *detail = "releases contexts and accepts NULL";
    int fail = 0;

    kc_trust_close(NULL);
    fail += expect_true("close accepts NULL", 1);
    test_identity_t identity;
    if (open_test_identity(&identity, 0x12)) return 1;
    kc_trust_close(identity.ctx);
    identity.ctx = NULL;
    fail += expect_true("close opened context", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_public_key.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_public_key(void) {
    const char *name = "kc_trust_public_key";
    const char *detail = "exposes the local 32-byte public key";
    int fail = 0;

    fail += expect_true("public key rejects NULL",
        kc_trust_public_key(NULL) == NULL);
    test_identity_t identity;
    if (open_test_identity(&identity, 0x13)) return 1;
    fail += expect_true("public key is present",
        kc_trust_public_key(identity.ctx) != NULL);
    fail += expect_bytes("public key matches identity", identity.pk,
        kc_trust_public_key(identity.ctx), KC_TRUST_PK_SIZE);
    close_test_identity(&identity);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_seal.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_seal(void) {
    const char *name = "kc_trust_seal";
    const char *detail = "seals fixed-size payloads and rejects bad arguments";
    int fail = 0;

    test_identity_t sender;
    test_identity_t recipient;
    if (open_test_identity(&sender, 0x21) ||
        open_test_identity(&recipient, 0x22)) return 1;
    const unsigned char text[] = "hello world";
    unsigned char binary[256];
    for (size_t i = 0; i < sizeof(binary); i++) binary[i] = (unsigned char)i;
    size_t boundary_len = TEST_TRANSPORT_PLAINTEXT_MAX + 1;
    unsigned char *boundary = (unsigned char *)malloc(boundary_len);
    fail += expect_true("allocate boundary payload", boundary != NULL);
    fail += expect_round_trip(&sender, &recipient, text, sizeof(text) - 1);
    fail += expect_round_trip(&sender, &recipient, NULL, 0);
    fail += expect_round_trip(&sender, &recipient, binary, sizeof(binary));
    if (boundary) {
        memset(boundary, 0x6d, boundary_len);
        fail += expect_round_trip(&sender, &recipient, boundary, boundary_len);
        free(boundary);
    }
    unsigned char zero_pk[KC_TRUST_PK_SIZE] = {0};
    unsigned char *payload = (unsigned char *)1;
    size_t payload_len = 1;
    fail += expect_int("reject zero recipient", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, zero_pk, text, sizeof(text) - 1,
        &payload, &payload_len));
    fail += expect_int("seal rejects NULL context", KC_TRUST_ERROR,
        kc_trust_seal(NULL, recipient.pk, (const unsigned char *)"x", 1,
        &payload, &payload_len));
    fail += expect_true("failed seal clears outputs", !payload && payload_len == 0);
    fail += expect_int("seal rejects NULL key", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, NULL, (const unsigned char *)"x", 1,
        &payload, &payload_len));
    fail += expect_int("seal rejects NULL message", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, recipient.pk, NULL, 1, &payload,
        &payload_len));
    fail += expect_int("seal rejects NULL output", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, recipient.pk, (const unsigned char *)"x", 1,
        NULL, &payload_len));
    fail += expect_int("seal rejects NULL length", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, recipient.pk, (const unsigned char *)"x", 1,
        &payload, NULL));
    close_test_identity(&sender);
    close_test_identity(&recipient);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_open(void) {
    const char *name = "kc_trust_open";
    const char *detail = "opens payloads, rejects trailing bytes and bad args";
    int fail = 0;

    test_identity_t sender;
    test_identity_t recipient;
    if (open_test_identity(&sender, 0x23) ||
        open_test_identity(&recipient, 0x24)) return 1;
    const unsigned char text[] = "hello world";
    fail += expect_round_trip(&sender, &recipient, text, sizeof(text) - 1);
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    if (kc_trust_seal(sender.ctx, recipient.pk, text, sizeof(text) - 1,
        &payload, &payload_len) == KC_TRUST_OK) {
        unsigned char *trailing = (unsigned char *)realloc(payload,
            payload_len + 1);
        fail += expect_true("extend valid payload", trailing != NULL);
        if (trailing) {
            trailing[payload_len] = 0;
            fail += expect_open_rejected("reject trailing payload",
                recipient.ctx, trailing, payload_len + 1);
            kc_trust_free(trailing);
        } else {
            kc_trust_free(payload);
        }
    } else {
        fail++;
    }
    unsigned char dummy_payload[TEST_PAYLOAD_BASE_SIZE] = {0};
    unsigned char long_id[KC_TRUST_MAX_PEER_ID + 1];
    memset(long_id, 'x', sizeof(long_id));
    kc_trust_result_t *result = (kc_trust_result_t *)1;
    fail += expect_int("open rejects NULL context", KC_TRUST_ERROR,
        kc_trust_open(NULL, dummy_payload, sizeof(dummy_payload), NULL, 0,
        &result));
    fail += expect_true("failed open clears result", result == NULL);
    fail += expect_int("open rejects NULL payload", KC_TRUST_ERROR,
        kc_trust_open(recipient.ctx, NULL, sizeof(dummy_payload), NULL, 0,
        &result));
    fail += expect_int("open rejects NULL result", KC_TRUST_ERROR,
        kc_trust_open(recipient.ctx, dummy_payload, sizeof(dummy_payload),
        NULL, 0, NULL));
    fail += expect_int("open rejects empty peer id", KC_TRUST_ERROR,
        kc_trust_open(recipient.ctx, dummy_payload, sizeof(dummy_payload),
        (const unsigned char *)"x", 0, &result));
    fail += expect_int("open rejects long peer id", KC_TRUST_ERROR,
        kc_trust_open(recipient.ctx, dummy_payload, sizeof(dummy_payload),
        long_id, sizeof(long_id), &result));
    close_test_identity(&sender);
    close_test_identity(&recipient);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_free(void) {
    const char *name = "kc_trust_free";
    const char *detail = "releases seal payloads and accepts NULL";
    int fail = 0;

    kc_trust_free(NULL);
    fail += expect_true("free accepts NULL", 1);
    test_identity_t sender;
    test_identity_t recipient;
    if (open_test_identity(&sender, 0x14) ||
        open_test_identity(&recipient, 0x15)) return 1;
    const unsigned char message[] = "owned payload";
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    if (kc_trust_seal(sender.ctx, recipient.pk, message, sizeof(message) - 1,
        &payload, &payload_len) == KC_TRUST_OK && payload) {
        kc_trust_free(payload);
        fail += expect_true("free releases seal payload", 1);
    } else {
        fail++;
    }
    unsigned char *generic = (unsigned char *)malloc(16);
    fail += expect_true("allocate generic heap buffer", generic != NULL);
    if (generic) {
        kc_trust_free(generic);
        fail += expect_true("free releases generic buffer", 1);
    }
    close_test_identity(&sender);
    close_test_identity(&recipient);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_result_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_result_free(void) {
    const char *name = "kc_trust_result_free";
    const char *detail = "releases results and accepts NULL";
    int fail = 0;

    kc_trust_result_free(NULL);
    fail += expect_true("result free accepts NULL", 1);
    test_identity_t sender;
    test_identity_t recipient;
    if (open_test_identity(&sender, 0x25) ||
        open_test_identity(&recipient, 0x26)) return 1;
    const unsigned char message[] = "owned result";
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    if (kc_trust_seal(sender.ctx, recipient.pk, message, sizeof(message) - 1,
        &payload, &payload_len) == KC_TRUST_OK && payload) {
        kc_trust_result_t *result = NULL;
        if (kc_trust_open(recipient.ctx, payload, payload_len, NULL, 0,
            &result) == KC_TRUST_OK) {
            kc_trust_result_free(result);
            fail += expect_true("result free releases owned message", 1);
        } else {
            fail++;
        }
        kc_trust_free(payload);
    } else {
        fail++;
    }
    close_test_identity(&sender);
    close_test_identity(&recipient);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_trust.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_trust(void) {
    const char *name = "kc_trust_trust";
    const char *detail = "binds peer keys and rejects bad arguments";
    int fail = 0;

    test_identity_t receiver;
    test_identity_t sender;
    if (open_test_identity(&receiver, 0x27) ||
        open_test_identity(&sender, 0x28)) return 1;
    const unsigned char peer[] = "alice";
    unsigned char long_id[KC_TRUST_MAX_PEER_ID + 1];
    memset(long_id, 'x', sizeof(long_id));
    fail += expect_int("trust matching sender", KC_TRUST_OK,
        kc_trust_trust(receiver.ctx, peer, 5, sender.pk));
    fail += expect_int("trust replaces binding", KC_TRUST_OK,
        kc_trust_trust(receiver.ctx, peer, 5, receiver.pk));
    fail += expect_int("trust rejects NULL context", KC_TRUST_ERROR,
        kc_trust_trust(NULL, peer, 5, sender.pk));
    fail += expect_int("trust rejects NULL peer id", KC_TRUST_ERROR,
        kc_trust_trust(receiver.ctx, NULL, 1, sender.pk));
    fail += expect_int("trust rejects long peer id", KC_TRUST_ERROR,
        kc_trust_trust(receiver.ctx, long_id, sizeof(long_id), sender.pk));
    fail += expect_int("trust rejects NULL key", KC_TRUST_ERROR,
        kc_trust_trust(receiver.ctx, peer, 5, NULL));
    close_test_identity(&receiver);
    close_test_identity(&sender);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_forget.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_forget(void) {
    const char *name = "kc_trust_forget";
    const char *detail = "removes bindings and rejects bad arguments";
    int fail = 0;

    test_identity_t identity;
    if (open_test_identity(&identity, 0x29)) return 1;
    const unsigned char peer[] = "alice";
    unsigned char long_id[KC_TRUST_MAX_PEER_ID + 1];
    memset(long_id, 'x', sizeof(long_id));
    fail += expect_int("trust before forget", KC_TRUST_OK,
        kc_trust_trust(identity.ctx, peer, 5, identity.pk));
    fail += expect_int("forget binding", KC_TRUST_OK,
        kc_trust_forget(identity.ctx, peer, 5));
    fail += expect_int("forget missing binding", KC_TRUST_ERROR,
        kc_trust_forget(identity.ctx, peer, 5));
    fail += expect_int("forget rejects NULL context", KC_TRUST_ERROR,
        kc_trust_forget(NULL, peer, 5));
    fail += expect_int("forget rejects NULL peer id", KC_TRUST_ERROR,
        kc_trust_forget(identity.ctx, NULL, 1));
    fail += expect_int("forget rejects long peer id", KC_TRUST_ERROR,
        kc_trust_forget(identity.ctx, long_id, sizeof(long_id)));
    close_test_identity(&identity);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the in-memory TOFU trust state machine and immutability.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_tofu_state_machine(void) {
    const char *name = "kc_trust_tofu_state_machine";
    const char *detail = "known, new, changed, and forget flows";
    int fail = 0;

    test_identity_t receiver;
    test_identity_t sender;
    test_identity_t changed;
    if (open_test_identity(&receiver, 0x41) ||
        open_test_identity(&sender, 0x42) ||
        open_test_identity(&changed, 0x43)) return 1;
    const unsigned char peer[] = "alice";
    const unsigned char message[] = "trust";
    unsigned char *sender_payload = NULL;
    unsigned char *changed_payload = NULL;
    size_t sender_len = 0;
    size_t changed_len = 0;
    fail += expect_int("seal trusted sender", KC_TRUST_OK,
        kc_trust_seal(sender.ctx, receiver.pk, message, sizeof(message) - 1,
        &sender_payload, &sender_len));
    fail += expect_int("seal changed sender", KC_TRUST_OK,
        kc_trust_seal(changed.ctx, receiver.pk, message, sizeof(message) - 1,
        &changed_payload, &changed_len));
    if (sender_payload && changed_payload) {
        fail += expect_int("unknown peer is new", KC_TRUST_PEER_NEW,
            open_trust_status(receiver.ctx, sender_payload, sender_len, peer, 5));
        fail += expect_int("incoming does not establish trust", KC_TRUST_PEER_NEW,
            open_trust_status(receiver.ctx, sender_payload, sender_len, peer, 5));
        fail += expect_int("trust matching sender", KC_TRUST_OK,
            kc_trust_trust(receiver.ctx, peer, 5, sender.pk));
        fail += expect_int("matching peer is known", KC_TRUST_OK,
            open_trust_status(receiver.ctx, sender_payload, sender_len, peer, 5));
        fail += expect_int("different sender changed", KC_TRUST_PEER_CHANGED,
            open_trust_status(receiver.ctx, changed_payload, changed_len,
                peer, 5));
        fail += expect_int("changed open does not mutate trust", KC_TRUST_OK,
            open_trust_status(receiver.ctx, sender_payload, sender_len, peer, 5));
        fail += expect_int("unnamed open bypasses trust", KC_TRUST_OK,
            open_trust_status(receiver.ctx, changed_payload, changed_len,
                NULL, 0));
        fail += expect_int("forget binding", KC_TRUST_OK,
            kc_trust_forget(receiver.ctx, peer, 5));
        fail += expect_int("forgotten peer is new", KC_TRUST_PEER_NEW,
            open_trust_status(receiver.ctx, sender_payload, sender_len, peer, 5));
        fail += expect_int("forget missing binding", KC_TRUST_ERROR,
            kc_trust_forget(receiver.ctx, peer, 5));
    }
    kc_trust_free(sender_payload);
    kc_trust_free(changed_payload);
    close_test_identity(&receiver);
    close_test_identity(&sender);
    close_test_identity(&changed);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Opens an independently generated Noise X conformance vector.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_noise_conformance(void) {
    const char *name = "kc_trust_noise_conformance";
    const char *detail = "matches an independent protocol vector";
    int fail = 0;

    static const char handshake_hex[] =
        "64b101b1d0be5a8704bd078f9895001fc03e8e9f9522f188dd128d9846d48466"
        "966214f55a51127cd18882703f24ede20d9f9d1220ac18745656284e58e64ef74"
        "712863db63620259ef2a733d1120698b1bca909a1710d16bdecc0b8ae227c3b";
    static const char length_record_hex[] =
        "deca7636858fda9c4d0529743c4a275ec913a445c936c99b";
    static const char transport_record_hex[] =
        "ab62b0721573ee2c4da5ff36d519b1cd8f70264a26";
    static const char sender_pk_hex[] =
        "5869aff450549732cbaaed5e5df9b30a6da31cb0e5742bad5ad4a1a768f1a67b";
    unsigned char recipient_sk[32];
    unsigned char expected_sender_pk[32];
    unsigned char expected_handshake[TEST_HANDSHAKE_SIZE];
    unsigned char payload[TEST_PAYLOAD_BASE_SIZE + 5 + TEST_NOISE_MAC_SIZE];
    const unsigned char expected_message[] = {0x00, 0xff, 0x01, 0xfe, 0x02};

    for (size_t i = 0; i < sizeof(recipient_sk); i++)
        recipient_sk[i] = (unsigned char)(i + 1);
    if (decode_hex(handshake_hex, expected_handshake,
        sizeof(expected_handshake)) != 0 ||
        decode_hex(handshake_hex, payload, TEST_HANDSHAKE_SIZE) != 0 ||
        decode_hex(length_record_hex, payload + TEST_HANDSHAKE_SIZE,
        TEST_LENGTH_RECORD_SIZE) != 0 ||
        decode_hex(transport_record_hex, payload + TEST_PAYLOAD_BASE_SIZE,
        sizeof(payload) - TEST_PAYLOAD_BASE_SIZE) != 0 ||
        decode_hex(sender_pk_hex, expected_sender_pk,
        sizeof(expected_sender_pk)) != 0) return 1;

    kc_trust_t *ctx = NULL;
    fail += expect_int("create vector recipient", KC_TRUST_OK,
        kc_trust_create(&ctx, recipient_sk));
    fail += expect_int("fixed Noise X handshake size", 96,
        TEST_HANDSHAKE_SIZE);
    fail += expect_int("fixed payload base size", 120,
        TEST_PAYLOAD_BASE_SIZE);
    fail += expect_int("Noise transport message maximum", 65535,
        TEST_TRANSPORT_MESSAGE_MAX);
    fail += expect_int("Noise transport plaintext maximum", 65519,
        TEST_TRANSPORT_PLAINTEXT_MAX);
    fail += expect_bytes("independent empty-payload handshake",
        expected_handshake, payload, TEST_HANDSHAKE_SIZE);
    if (ctx) {
        kc_trust_result_t *result = NULL;
        fail += expect_int("open independent Noise X vector", KC_TRUST_OK,
            kc_trust_open(ctx, payload, sizeof(payload), NULL, 0, &result));
        if (result) {
            fail += expect_bytes("vector authenticates sender static",
                expected_sender_pk, result->peer_pk, KC_TRUST_PK_SIZE);
            fail += expect_int("vector message length", 5,
                (int)result->message_len);
            fail += expect_bytes("vector message", expected_message,
                result->message, sizeof(expected_message));
            kc_trust_result_free(result);
        }
        kc_trust_close(ctx);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests authentication, malformed inputs, tampering, and low-order points.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_crypto_rejection(void) {
    const char *name = "kc_trust_crypto_rejection";
    const char *detail = "rejects tampering, truncation, and low-order points";
    int fail = 0;

    test_identity_t sender;
    test_identity_t recipient;
    test_identity_t wrong;
    if (open_test_identity(&sender, 0x31) ||
        open_test_identity(&recipient, 0x32) ||
        open_test_identity(&wrong, 0x33)) return 1;
    const unsigned char message[] = "rejection";
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    fail += expect_int("create valid rejection fixture", KC_TRUST_OK,
        kc_trust_seal(sender.ctx, recipient.pk, message, sizeof(message) - 1,
        &payload, &payload_len));
    if (payload) {
        fail += expect_open_rejected("reject wrong recipient", wrong.ctx,
            payload, payload_len);
        size_t truncations[] = {0, 1, 50, TEST_PAYLOAD_BASE_SIZE - 1,
            payload_len - 1};
        for (size_t i = 0; i < sizeof(truncations) / sizeof(truncations[0]); i++)
            fail += expect_open_rejected("reject truncated payload",
                recipient.ctx, payload, truncations[i]);
        size_t offsets[] = {0, KC_TRUST_PK_SIZE,
            TEST_HANDSHAKE_SIZE - 1, TEST_PAYLOAD_BASE_SIZE, payload_len - 1};
        const char *names[] = {"reject modified ephemeral",
            "reject modified sender", "reject modified handshake tag",
            "reject modified transport", "reject modified transport tag"};
        unsigned char *mutated = (unsigned char *)malloc(payload_len);
        fail += expect_true("allocate mutation payload", mutated != NULL);
        if (mutated) {
            for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
                memcpy(mutated, payload, payload_len);
                mutated[offsets[i]] ^= 0xff;
                fail += expect_open_rejected(names[i], recipient.ctx,
                    mutated, payload_len);
            }
            memcpy(mutated, payload, payload_len);
            memset(mutated, 0, KC_TRUST_PK_SIZE);
            fail += expect_open_rejected("reject zero ephemeral", recipient.ctx,
                mutated, payload_len);
            free(mutated);
        }
        kc_trust_free(payload);
    }
    unsigned char garbage[200];
    memset(garbage, 0xab, sizeof(garbage));
    fail += expect_open_rejected("reject garbage", recipient.ctx,
        garbage, sizeof(garbage));
    unsigned char zero_pk[KC_TRUST_PK_SIZE] = {0};
    unsigned char *ignored = NULL;
    size_t ignored_len = 0;
    fail += expect_int("reject zero recipient", KC_TRUST_ERROR,
        kc_trust_seal(sender.ctx, zero_pk, message, sizeof(message) - 1,
        &ignored, &ignored_len));
    static const char zero_static_hex[] =
        "64b101b1d0be5a8704bd078f9895001fc03e8e9f9522f188dd128d9846d48466"
        "6ce0bbb010a05854e1a226f2e62dd5ee8603c81a2c5d833d90c8289e93017e88"
        "cdf5f7ebf1e965482efffc332085d822800000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000";
    unsigned char zero_static[TEST_PAYLOAD_BASE_SIZE];
    fail += expect_int("zero static vector length",
        TEST_PAYLOAD_BASE_SIZE * 2, (int)strlen(zero_static_hex));
    int decoded = decode_hex(zero_static_hex, zero_static, sizeof(zero_static));
    fail += expect_int("decode zero static vector", 0, decoded);
    unsigned char vector_sk[KC_TRUST_SK_SIZE];
    kc_trust_t *vector_ctx = NULL;
    for (size_t i = 0; i < sizeof(vector_sk); i++)
        vector_sk[i] = (unsigned char)(i + 1);
    if (kc_trust_create(&vector_ctx, vector_sk) != KC_TRUST_OK) fail++;
    else if (!decoded)
        fail += expect_open_rejected("reject zero sender static",
            vector_ctx, zero_static, sizeof(zero_static));
    if (vector_ctx) kc_trust_close(vector_ctx);
    crypto_wipe(vector_sk, sizeof(vector_sk));
    close_test_identity(&sender);
    close_test_identity(&recipient);
    close_test_identity(&wrong);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests independent context ownership and trust isolation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_multictx(void) {
    const char *name = "kc_trust_multictx";
    const char *detail = "contexts are independent and trust is isolated";
    int fail = 0;

    test_identity_t identity;
    if (open_test_identity(&identity, 0x51)) return 1;
    kc_trust_t *second = NULL;
    fail += expect_int("create independent context", KC_TRUST_OK,
        kc_trust_create(&second, identity.sk));
    if (second) {
        fail += expect_true("contexts are distinct", identity.ctx != second);
        fail += expect_int("first context trusts", KC_TRUST_OK,
            kc_trust_trust(identity.ctx, (const unsigned char *)"x", 1,
            identity.pk));
        fail += expect_int("second context remains isolated", KC_TRUST_ERROR,
            kc_trust_forget(second, (const unsigned char *)"x", 1));
        kc_trust_close(second);
    }
    close_test_identity(&identity);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the 64 MiB message limit and maximum payload rejection.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_message_limit(void) {
    const char *name = "kc_trust_message_limit";
    const char *detail = "enforces the 64 MiB boundary";
    int fail = 0;

    test_identity_t identity;
    if (open_test_identity(&identity, 0x61)) return 1;
    unsigned char *message = (unsigned char *)calloc(KC_TRUST_MAX_MESSAGE, 1);
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    fail += expect_int("logical limit is 64 MiB", 64 * 1024 * 1024,
        KC_TRUST_MAX_MESSAGE);
    fail += expect_true("allocate maximum message", message != NULL);
    if (message) {
        fail += expect_int("seal maximum message", KC_TRUST_OK,
            kc_trust_seal(identity.ctx, identity.pk, message,
            KC_TRUST_MAX_MESSAGE, &payload, &payload_len));
        free(message);
    }
    fail += expect_true("maximum payload formula",
        payload && payload_len == expected_payload_size(KC_TRUST_MAX_MESSAGE));
    if (payload) {
        kc_trust_result_t *result = NULL;
        fail += expect_int("open maximum message", KC_TRUST_OK,
            kc_trust_open(identity.ctx, payload, payload_len, NULL, 0, &result));
        fail += expect_true("maximum message restored",
            result && result->message_len == KC_TRUST_MAX_MESSAGE);
        kc_trust_result_free(result);
        fail += expect_open_rejected("reject truncated maximum payload",
            identity.ctx, payload, payload_len - 1);
        unsigned char *trailing = (unsigned char *)realloc(payload,
            payload_len + 1);
        fail += expect_true("extend maximum payload", trailing != NULL);
        if (trailing) {
            trailing[payload_len] = 0;
            fail += expect_open_rejected("reject oversized maximum payload",
                identity.ctx, trailing, payload_len + 1);
            kc_trust_free(trailing);
        } else {
            kc_trust_free(payload);
        }
    }
    unsigned char *ignored = NULL;
    size_t ignored_len = 0;
    fail += expect_int("reject above logical limit", KC_TRUST_ERROR,
        kc_trust_seal(identity.ctx, identity.pk, (const unsigned char *)"x",
        KC_TRUST_MAX_MESSAGE + 1, &ignored, &ignored_len));
    close_test_identity(&identity);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 16;
    test_case_current = 0;
    run_case(&rc, case_kc_trust_version);
    run_case(&rc, case_kc_trust_generate);
    run_case(&rc, case_kc_trust_create);
    run_case(&rc, case_kc_trust_close);
    run_case(&rc, case_kc_trust_public_key);
    run_case(&rc, case_kc_trust_seal);
    run_case(&rc, case_kc_trust_open);
    run_case(&rc, case_kc_trust_free);
    run_case(&rc, case_kc_trust_result_free);
    run_case(&rc, case_kc_trust_trust);
    run_case(&rc, case_kc_trust_forget);
    run_case(&rc, case_kc_trust_tofu_state_machine);
    run_case(&rc, case_kc_trust_noise_conformance);
    run_case(&rc, case_kc_trust_crypto_rejection);
    run_case(&rc, case_kc_trust_multictx);
    run_case(&rc, case_kc_trust_message_limit);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test entry point. Dispatches to one test case by name.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Test exit code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_trust_version") == 0) return case_kc_trust_version();
    if (strcmp(argv[1], "kc_trust_generate") == 0) return case_kc_trust_generate();
    if (strcmp(argv[1], "kc_trust_create") == 0) return case_kc_trust_create();
    if (strcmp(argv[1], "kc_trust_close") == 0) return case_kc_trust_close();
    if (strcmp(argv[1], "kc_trust_public_key") == 0) return case_kc_trust_public_key();
    if (strcmp(argv[1], "kc_trust_seal") == 0) return case_kc_trust_seal();
    if (strcmp(argv[1], "kc_trust_open") == 0) return case_kc_trust_open();
    if (strcmp(argv[1], "kc_trust_free") == 0) return case_kc_trust_free();
    if (strcmp(argv[1], "kc_trust_result_free") == 0) return case_kc_trust_result_free();
    if (strcmp(argv[1], "kc_trust_trust") == 0) return case_kc_trust_trust();
    if (strcmp(argv[1], "kc_trust_forget") == 0) return case_kc_trust_forget();
    if (strcmp(argv[1], "kc_trust_tofu_state_machine") == 0) return case_kc_trust_tofu_state_machine();
    if (strcmp(argv[1], "kc_trust_noise_conformance") == 0) return case_kc_trust_noise_conformance();
    if (strcmp(argv[1], "kc_trust_crypto_rejection") == 0) return case_kc_trust_crypto_rejection();
    if (strcmp(argv[1], "kc_trust_multictx") == 0) return case_kc_trust_multictx();
    if (strcmp(argv[1], "kc_trust_message_limit") == 0) return case_kc_trust_message_limit();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
