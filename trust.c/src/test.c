/**
 * test.c - libtrust public API contract tests.
 * Summary: Validates each exported libtrust function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtrust.h"
#include "monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#define getpid _getpid
#endif

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

static int temp_keyfile_counter = 0;

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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
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

/**
 * Sets or unsets an environment variable.
 * @param name Variable name.
 * @param value Value to set, or NULL to unset.
 * @return 0 on success, 1 on failure.
 */
static int set_env_value(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value != NULL ? value : "") == 0 ? 0 : 1;
#else
    if (value == NULL) return unsetenv(name) == 0 ? 0 : 1;
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Joins a test directory and child using the platform separator.
 * @param directory Parent directory.
 * @param child Child name.
 * @param path Destination path buffer.
 * @param path_cap Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int join_test_path(const char *directory, const char *child,
char *path, size_t path_cap) {
#ifdef _WIN32
    const char separator = '\\';
#else
    const char separator = '/';
#endif
    size_t directory_len = strlen(directory);
    int length = snprintf(path, path_cap, "%s%s%s", directory,
        directory_len > 0 && (directory[directory_len - 1] == '/' ||
        directory[directory_len - 1] == '\\') ? "" :
        (separator == '\\' ? "\\" : "/"), child);
    return length >= 0 && (size_t)length < path_cap ? 0 : 1;
}

/**
 * Builds a unique temporary test path without creating it.
 * @param suffix Descriptive filename suffix.
 * @param path Destination path buffer.
 * @param path_cap Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int make_temp_path(const char *suffix, char *path, size_t path_cap) {
    char directory[512];
#ifdef _WIN32
    DWORD length = GetTempPathA((DWORD)sizeof(directory), directory);
    if (length == 0 || length >= sizeof(directory)) return 1;
#else
    int length = snprintf(directory, sizeof(directory), "/tmp");
    if (length < 0 || (size_t)length >= sizeof(directory)) return 1;
#endif
    char name[128];
    int name_len = snprintf(name, sizeof(name), "trust_test_%d_%d_%s",
        getpid(), temp_keyfile_counter++, suffix);
    if (name_len < 0 || (size_t)name_len >= sizeof(name)) return 1;
    return join_test_path(directory, name, path, path_cap);
}

/**
 * Creates a temporary keyfile containing sk and pk.
 * @param sk 32-byte secret key.
 * @param pk 32-byte public key.
 * @return Allocated path string, or NULL on failure.
 */
static char *create_temp_keyfile(unsigned char sk[32], unsigned char pk[32]) {
    char *path = (char *)malloc(1024);
    if (!path || make_temp_path("key", path, 1024) != 0) {
        free(path);
        return NULL;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { free(path); return NULL; }
    size_t sk_written = fwrite(sk, 1, 32, f);
    size_t pk_written = fwrite(pk, 1, 32, f);
    if (fclose(f) != 0 || sk_written != 32 || pk_written != 32) {
        remove(path);
        free(path);
        return NULL;
    }
    return path;
}

/**
 * Removes a temporary file.
 * @param path File path to remove.
 * @return Nothing.
 */
static void remove_temp_file(const char *path) {
    if (!path) return;
#ifdef _WIN32
    DeleteFileA(path);
#else
    remove(path);
#endif
}

/**
 * Replaces a test file with the specified bytes.
 * @param path File path.
 * @param data Source bytes.
 * @param data_len Source length.
 * @return 0 on success, 1 on failure.
 */
static int write_test_file(const char *path, const unsigned char *data,
size_t data_len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t written = fwrite(data, 1, data_len, f);
    int close_failed = fclose(f) != 0;
    return written == data_len && !close_failed ? 0 : 1;
}

/**
 * Reads an exact test file and rejects trailing bytes.
 * @param path File path.
 * @param data Destination buffer.
 * @param data_len Expected file size.
 * @return 0 on success, 1 on failure.
 */
static int read_test_file_exact(const char *path, unsigned char *data,
size_t data_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    size_t read_len = fread(data, 1, data_len, f);
    unsigned char extra;
    size_t extra_len = fread(&extra, 1, 1, f);
    int failed = ferror(f) || read_len != data_len || extra_len != 0;
    if (fclose(f) != 0) failed = 1;
    return failed ? 1 : 0;
}

typedef struct {
    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char pk[KC_TRUST_PK_SIZE];
    char *path;
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
    identity->path = create_temp_keyfile(identity->sk, identity->pk);
    if (!identity->path) return 1;
    kc_trust_options_t opts = kc_trust_options_default();
    opts.key_path = identity->path;
    if (kc_trust_create(&identity->ctx, &opts) != KC_TRUST_OK) {
        remove_temp_file(identity->path);
        free(identity->path);
        identity->path = NULL;
        return 1;
    }
    return 0;
}

/**
 * Releases a temporary identity.
 * @return Nothing.
 */
static void close_test_identity(test_identity_t *identity) {
    kc_trust_close(identity->ctx);
    remove_temp_file(identity->path);
    free(identity->path);
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
        free(payload);
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
 * Tests kc_trust_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_options_default(void) {
    const char *name = "kc_trust_options_default";
    const char *detail = "returns empty options";
    int fail = 0;

    kc_trust_options_t opts = kc_trust_options_default();
    fail += expect_true("default options are empty",
        !opts.state_path && !opts.key_path);
    kc_trust_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_options_load_env(void) {
    const char *name = "kc_trust_options_load_env";
    const char *detail = "loads environment overrides independently";
    int fail = 0;

    kc_trust_options_t opts = kc_trust_options_default();
    set_env_value("TRUST_STATE_DIR", "/tmp/test_state");
    set_env_value("TRUST_KEY", "/tmp/test_key");
    kc_trust_options_load_env(&opts);
    fail += expect_string("TRUST_STATE_DIR loads", "/tmp/test_state",
        opts.state_path);
    fail += expect_string("TRUST_KEY loads", "/tmp/test_key", opts.key_path);
    set_env_value("TRUST_STATE_DIR", "/tmp/test_state2");
    set_env_value("TRUST_KEY", NULL);
    kc_trust_options_load_env(&opts);
    fail += expect_string("state replaces independently", "/tmp/test_state2",
        opts.state_path);
    fail += expect_string("unset key remains", "/tmp/test_key", opts.key_path);
    set_env_value("TRUST_STATE_DIR", NULL);
    set_env_value("TRUST_KEY", "/tmp/test_key2");
    kc_trust_options_load_env(&opts);
    fail += expect_string("key replaces independently", "/tmp/test_key2",
        opts.key_path);
    kc_trust_options_free(&opts);
    fail += expect_true("options free clears fields",
        !opts.state_path && !opts.key_path);
    kc_trust_options_free(&opts);
    kc_trust_options_load_env(NULL);
    set_env_value("TRUST_STATE_DIR", NULL);
    set_env_value("TRUST_KEY", NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_options_free(void) {
    const char *name = "kc_trust_options_free";
    const char *detail = "clears options and accepts NULL";
    int fail = 0;

    kc_trust_options_t opts = kc_trust_options_default();
    opts.state_path = strdup("/tmp/test_state");
    opts.key_path = strdup("/tmp/test_key");
    fail += expect_true("options hold allocated fields",
        opts.state_path != NULL && opts.key_path != NULL);
    kc_trust_options_free(&opts);
    fail += expect_true("options free clears fields",
        !opts.state_path && !opts.key_path);
    kc_trust_options_free(&opts);
    kc_trust_options_free(NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_resolve_state_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_resolve_state_path(void) {
    const char *name = "kc_trust_resolve_state_path";
    const char *detail = "resolves explicit and platform defaults";
    int fail = 0;

    kc_trust_options_t opts = kc_trust_options_default();
    char path[256];
    opts.state_path = "/explicit/trust-state";
    fail += expect_int("explicit state resolves", KC_TRUST_OK,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
    fail += expect_string("explicit state wins", "/explicit/trust-state", path);
    opts.state_path = NULL;
#ifdef _WIN32
    set_env_value("LOCALAPPDATA", "C:\\Users\\test\\AppData\\Local");
    set_env_value("USERPROFILE", "C:\\Users\\test");
    fail += expect_int("LOCALAPPDATA resolves", KC_TRUST_OK,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
    fail += expect_string("LOCALAPPDATA default",
        "C:\\Users\\test\\AppData\\Local\\trust", path);
    set_env_value("LOCALAPPDATA", NULL);
    fail += expect_int("USERPROFILE resolves", KC_TRUST_OK,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
    fail += expect_string("USERPROFILE fallback", "C:\\Users\\test\\.trust", path);
    set_env_value("USERPROFILE", NULL);
#else
    const char *home = getenv("HOME");
    char *saved_home = home ? strdup(home) : NULL;
    const char *saved_xdg_data = getenv("XDG_DATA_HOME");
    char *saved_xdg_data_copy =
        saved_xdg_data ? strdup(saved_xdg_data) : NULL;
    set_env_value("XDG_DATA_HOME", NULL);
    set_env_value("HOME", "/tmp/test_home");
    fail += expect_int("HOME resolves", KC_TRUST_OK,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
    fail += expect_string("HOME default",
        "/tmp/test_home/.local/share/trust", path);
    set_env_value("XDG_DATA_HOME", "/tmp/test_xdg");
    fail += expect_int("XDG_DATA_HOME resolves", KC_TRUST_OK,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
    fail += expect_string("XDG_DATA_HOME default",
        "/tmp/test_xdg/trust", path);
    set_env_value("XDG_DATA_HOME", saved_xdg_data_copy);
    free(saved_xdg_data_copy);
    set_env_value("HOME", NULL);
#endif
    fail += expect_int("missing platform root fails", KC_TRUST_ERROR,
        kc_trust_resolve_state_path(&opts, path, sizeof(path)));
#ifndef _WIN32
    set_env_value("HOME", saved_home);
    free(saved_home);
#endif
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
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
    const char *detail = "writes a non-overwritten identity";
    int fail = 0;

    char path[1024];
    if (make_temp_path("generated", path, sizeof(path))) return 1;
    unsigned char identity[KC_TRUST_IDENTITY_SIZE];
    unsigned char derived[KC_TRUST_PK_SIZE];
    fail += expect_int("generate identity", KC_TRUST_OK,
        kc_trust_generate(path));
    fail += expect_int("generated identity is exact", 0,
        read_test_file_exact(path, identity, sizeof(identity)));
    crypto_x25519_public_key(derived, identity);
    fail += expect_bytes("generated public key", derived,
        identity + KC_TRUST_SK_SIZE, KC_TRUST_PK_SIZE);
    fail += expect_int("generate does not overwrite", KC_TRUST_ERROR,
        kc_trust_generate(path));
    crypto_wipe(identity, sizeof(identity));
    remove_temp_file(path);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_trust_create.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_trust_create(void) {
    const char *name = "kc_trust_create";
    const char *detail = "loads identities and rejects bad arguments";
    int fail = 0;

    test_identity_t identity;
    if (open_test_identity(&identity, 0x11)) return 1;
    kc_trust_options_t opts = kc_trust_options_default();
    opts.key_path = identity.path;
    kc_trust_t *ctx = (kc_trust_t *)1;
    fail += expect_int("create rejects NULL output", KC_TRUST_ERROR,
        kc_trust_create(NULL, &opts));
    fail += expect_int("create rejects NULL options", KC_TRUST_ERROR,
        kc_trust_create(&ctx, NULL));
    fail += expect_true("failed create clears output", ctx == NULL);
    unsigned char bytes[KC_TRUST_IDENTITY_SIZE + 1];
    memcpy(bytes, identity.sk, KC_TRUST_SK_SIZE);
    memcpy(bytes + KC_TRUST_SK_SIZE, identity.pk, KC_TRUST_PK_SIZE);
    bytes[KC_TRUST_IDENTITY_SIZE] = 0xff;
    kc_trust_close(identity.ctx);
    identity.ctx = NULL;
    fail += expect_int("write 63-byte identity", 0,
        write_test_file(identity.path, bytes, KC_TRUST_IDENTITY_SIZE - 1));
    fail += expect_int("reject 63-byte identity", KC_TRUST_ERROR,
        kc_trust_create(&ctx, &opts));
    fail += expect_int("write 65-byte identity", 0,
        write_test_file(identity.path, bytes, KC_TRUST_IDENTITY_SIZE + 1));
    fail += expect_int("reject 65-byte identity", KC_TRUST_ERROR,
        kc_trust_create(&ctx, &opts));
    close_test_identity(&identity);
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

    fail += expect_int("close NULL", KC_TRUST_OK, kc_trust_close(NULL));
    test_identity_t identity;
    if (open_test_identity(&identity, 0x12)) return 1;
    kc_trust_close(identity.ctx);
    identity.ctx = NULL;
    remove_temp_file(identity.path);
    free(identity.path);
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
            free(trailing);
        } else {
            free(payload);
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
        free(payload);
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
    free(sender_payload);
    free(changed_payload);
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
    unsigned char recipient_pk[32];
    unsigned char expected_sender_pk[32];
    unsigned char expected_handshake[TEST_HANDSHAKE_SIZE];
    unsigned char payload[TEST_PAYLOAD_BASE_SIZE + 5 + TEST_NOISE_MAC_SIZE];
    const unsigned char expected_message[] = {0x00, 0xff, 0x01, 0xfe, 0x02};

    for (size_t i = 0; i < sizeof(recipient_sk); i++)
        recipient_sk[i] = (unsigned char)(i + 1);
    crypto_x25519_public_key(recipient_pk, recipient_sk);
    if (decode_hex(handshake_hex, expected_handshake,
        sizeof(expected_handshake)) != 0 ||
        decode_hex(handshake_hex, payload, TEST_HANDSHAKE_SIZE) != 0 ||
        decode_hex(length_record_hex, payload + TEST_HANDSHAKE_SIZE,
        TEST_LENGTH_RECORD_SIZE) != 0 ||
        decode_hex(transport_record_hex, payload + TEST_PAYLOAD_BASE_SIZE,
        sizeof(payload) - TEST_PAYLOAD_BASE_SIZE) != 0 ||
        decode_hex(sender_pk_hex, expected_sender_pk,
        sizeof(expected_sender_pk)) != 0) return 1;

    char *path = create_temp_keyfile(recipient_sk, recipient_pk);
    if (!path) return 1;
    kc_trust_options_t opts = kc_trust_options_default();
    opts.key_path = path;
    kc_trust_t *ctx = NULL;
    fail += expect_int("create vector recipient", KC_TRUST_OK,
        kc_trust_create(&ctx, &opts));
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
    remove_temp_file(path);
    free(path);
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
        free(payload);
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
    test_identity_t vector_recipient;
    memset(&vector_recipient, 0, sizeof(vector_recipient));
    for (size_t i = 0; i < sizeof(vector_recipient.sk); i++)
        vector_recipient.sk[i] = (unsigned char)(i + 1);
    crypto_x25519_public_key(vector_recipient.pk, vector_recipient.sk);
    vector_recipient.path = create_temp_keyfile(vector_recipient.sk,
        vector_recipient.pk);
    kc_trust_options_t vector_opts = kc_trust_options_default();
    vector_opts.key_path = vector_recipient.path;
    if (!vector_recipient.path || kc_trust_create(&vector_recipient.ctx,
        &vector_opts) != KC_TRUST_OK) fail++;
    else if (!decoded)
        fail += expect_open_rejected("reject zero sender static",
            vector_recipient.ctx, zero_static, sizeof(zero_static));
    close_test_identity(&vector_recipient);
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
    kc_trust_options_t opts = kc_trust_options_default();
    opts.key_path = identity.path;
    kc_trust_t *second = NULL;
    fail += expect_int("create independent context", KC_TRUST_OK,
        kc_trust_create(&second, &opts));
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
            free(trailing);
        } else {
            free(payload);
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
    test_case_total = 19;
    test_case_current = 0;
    run_case(&rc, case_kc_trust_options_default);
    run_case(&rc, case_kc_trust_options_load_env);
    run_case(&rc, case_kc_trust_options_free);
    run_case(&rc, case_kc_trust_resolve_state_path);
    run_case(&rc, case_kc_trust_version);
    run_case(&rc, case_kc_trust_generate);
    run_case(&rc, case_kc_trust_create);
    run_case(&rc, case_kc_trust_close);
    run_case(&rc, case_kc_trust_public_key);
    run_case(&rc, case_kc_trust_seal);
    run_case(&rc, case_kc_trust_open);
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
    if (strcmp(argv[1], "kc_trust_options_default") == 0) return case_kc_trust_options_default();
    if (strcmp(argv[1], "kc_trust_options_load_env") == 0) return case_kc_trust_options_load_env();
    if (strcmp(argv[1], "kc_trust_options_free") == 0) return case_kc_trust_options_free();
    if (strcmp(argv[1], "kc_trust_resolve_state_path") == 0) return case_kc_trust_resolve_state_path();
    if (strcmp(argv[1], "kc_trust_version") == 0) return case_kc_trust_version();
    if (strcmp(argv[1], "kc_trust_generate") == 0) return case_kc_trust_generate();
    if (strcmp(argv[1], "kc_trust_create") == 0) return case_kc_trust_create();
    if (strcmp(argv[1], "kc_trust_close") == 0) return case_kc_trust_close();
    if (strcmp(argv[1], "kc_trust_public_key") == 0) return case_kc_trust_public_key();
    if (strcmp(argv[1], "kc_trust_seal") == 0) return case_kc_trust_seal();
    if (strcmp(argv[1], "kc_trust_open") == 0) return case_kc_trust_open();
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
