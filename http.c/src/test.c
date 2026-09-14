/**
 * test.c - libhttp public API contract tests.
 * Summary: Validates each exported libhttp function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhttp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
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

/**
 * Sets or clears one environment variable.
 * @param name Variable name.
 * @param value Variable value, or NULL to clear.
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
 * Verifies an integer result.
 * @param name Check name.
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
 * Verifies a true condition.
 * @param name Check name.
 * @param condition Condition expected to be true.
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
 * Verifies a string result.
 * @param name Check name.
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
 * Verifies that a byte buffer starts with an exact prefix.
 * @param name Check name.
 * @param data Buffer.
 * @param len Buffer length.
 * @param prefix Expected prefix.
 * @return 0 on success, 1 on failure.
 */
static int expect_prefix(const char *name, const unsigned char *data, size_t len,
    const char *prefix) {
    size_t n = strlen(prefix);
    if (len >= n && memcmp(data, prefix, n) == 0) return 0;
    printf("[FAIL] %s: starts with '%s'\n", name, prefix);
    return 1;
}

/**
 * Verifies that a byte buffer contains an exact byte sequence.
 * @param name Check name.
 * @param data Buffer.
 * @param len Buffer length.
 * @param needle Expected sequence.
 * @return 0 on success, 1 on failure.
 */
static int expect_contains(const char *name, const unsigned char *data, size_t len,
    const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    if (n == 0) return 0;
    for (i = 0; i + n <= len; i++) {
        if (memcmp(data + i, needle, n) == 0) return 0;
    }
    printf("[FAIL] %s: contains '%s'\n", name, needle);
    return 1;
}

/**
 * Verifies that a byte buffer does not contain an exact byte sequence.
 * @param name Check name.
 * @param data Buffer.
 * @param len Buffer length.
 * @param needle Absent sequence.
 * @return 0 on success, 1 on failure.
 */
static int expect_not_contains(const char *name, const unsigned char *data, size_t len,
    const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    if (n == 0) return 0;
    for (i = 0; i + n <= len; i++) {
        if (memcmp(data + i, needle, n) == 0) {
            printf("[FAIL] %s: absent '%s'\n", name, needle);
            return 1;
        }
    }
    return 0;
}

/**
 * Tests kc_http_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_version(void) {
    const char *name = "kc_http_version";
    const char *detail = "returns non-zero build timestamp";
    int fail = expect_true("version set", kc_http_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_options_default(void) {
    const char *name = "kc_http_options_default";
    const char *detail = "initializes correctly";
    kc_http_options_t opts;
    int fail = 0;

    opts = kc_http_options_default();
    fail += expect_true("method NULL", opts.method == NULL);
    fail += expect_true("target NULL", opts.target == NULL);
    fail += expect_true("version NULL", opts.version == NULL);
    fail += expect_int("chunked 0", 0, opts.chunked);
    fail += expect_int("chunk size", 8192, (int)opts.chunk_size);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_options_load_env(void) {
    const char *name = "kc_http_options_load_env";
    const char *detail = "loads from environment";
    kc_http_options_t opts;
    int fail = 0;

    fail += expect_int("set env method", 0, set_env_value("KC_HTTP_METHOD", "POST"));
    fail += expect_int("set env target", 0, set_env_value("KC_HTTP_TARGET", "/env"));
    fail += expect_int("set env version", 0, set_env_value("KC_HTTP_VERSION", "1.0"));
    fail += expect_int("set env chunked", 0, set_env_value("KC_HTTP_CHUNKED", "1"));

    opts = kc_http_options_default();
    kc_http_options_load_env(&opts);

    fail += expect_string("loaded method", "POST", opts.method);
    fail += expect_string("loaded target", "/env", opts.target);
    fail += expect_string("loaded version", "1.0", opts.version);
    fail += expect_int("loaded chunked", 1, opts.chunked);

    kc_http_options_free(&opts);

    set_env_value("KC_HTTP_METHOD", NULL);
    set_env_value("KC_HTTP_TARGET", NULL);
    set_env_value("KC_HTTP_VERSION", NULL);
    set_env_value("KC_HTTP_CHUNKED", NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_options_free(void) {
    const char *name = "kc_http_options_free";
    const char *detail = "clears resources";
    kc_http_options_t opts;
    int fail = 0;

    opts = kc_http_options_default();
    kc_http_options_free(&opts);
    kc_http_options_free(NULL);
    fail += expect_int("free does not crash", 0, 0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_open(void) {
    const char *name = "kc_http_open";
    const char *detail = "allocates context";
    kc_http_options_t opts;
    kc_http_t *ctx = NULL;
    int fail = 0;

    opts = kc_http_options_default();
    fail += expect_int("open NULL out", KC_HTTP_ERROR, kc_http_open(NULL, &opts));
    fail += expect_int("open NULL opts", KC_HTTP_ERROR, kc_http_open(&ctx, NULL));
    fail += expect_int("open valid", KC_HTTP_OK, kc_http_open(&ctx, &opts));
    fail += expect_true("open sets context", ctx != NULL);
    kc_http_options_free(&opts);
    kc_http_close(ctx);
    fail += expect_true("open context closes", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_close(void) {
    const char *name = "kc_http_close";
    const char *detail = "releases context";
    kc_http_options_t opts;
    kc_http_t *ctx;
    int fail = 0;

    kc_http_close(NULL);
    fail += expect_true("close NULL doesn't crash", 1);
    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_close(ctx);
    fail += expect_true("close context doesn't crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_stop(void) {
    const char *name = "kc_http_stop";
    const char *detail = "sets flag";
    kc_http_options_t opts;
    kc_http_t *ctx;
    int fail = 0;

    fail += expect_int("stop NULL", KC_HTTP_ERROR, kc_http_stop(NULL));
    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    fail += expect_int("stop context", KC_HTTP_OK, kc_http_stop(ctx));
    fail += expect_int("stop idempotent", KC_HTTP_OK, kc_http_stop(ctx));
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_op.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_op(void) {
    const char *name = "kc_http_set_op";
    const char *detail = "selects the executed operation";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_input(ctx, "hello", 5);
    fail += expect_int("exec without op", KC_HTTP_ERROR, kc_http_exec(ctx));
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    fail += expect_int("exec after set_op", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output request line", out, out_len, "GET / HTTP/1.1\r\n");
    fail += expect_contains("output body", out, out_len, "hello");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_all.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_all(void) {
    const char *name = "kc_http_set_all";
    const char *detail = "parses all messages until EOF";
    const char *req = "GET /a HTTP/1.1\r\nHost: a.com\r\n\r\n"
        "GET /b HTTP/1.1\r\nHost: b.com\r\n\r\n";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_PARSE);
    kc_http_set_input(ctx, req, strlen(req));
    fail += expect_int("exec single", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("single first", out, out_len, "request.target=/a");
    fail += expect_not_contains("single stops", out, out_len, "request.target=/b");
    kc_http_set_all(ctx, 1);
    fail += expect_int("exec all", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("all first", out, out_len, "request.target=/a");
    fail += expect_contains("all second", out, out_len, "request.target=/b");
    fail += expect_contains("all separator", out, out_len, "\n\nhttp.type=request");
    kc_http_set_input(ctx, "GET /a HTTP/1.1\r\nHost: a.com\r\n",
        strlen("GET /a HTTP/1.1\r\nHost: a.com\r\n"));
    fail += expect_int("exec truncated fails", KC_HTTP_ERROR, kc_http_exec(ctx));
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_input.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_input(void) {
    const char *name = "kc_http_set_input";
    const char *detail = "supplies the exec input";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    kc_http_set_input(ctx, "hello", 5);
    fail += expect_int("exec with input", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output length", out, out_len, "Content-Length: 5\r\n");
    fail += expect_contains("output body", out, out_len, "hello");
    kc_http_set_input(ctx, NULL, 0);
    fail += expect_int("exec empty input", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output zero length", out, out_len, "Content-Length: 0\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_get_output.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_get_output(void) {
    const char *name = "kc_http_get_output";
    const char *detail = "returns the last exec result";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = (unsigned char *)"x";
    size_t out_len = 1;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    fail += expect_int("get_output before exec", KC_HTTP_OK, kc_http_get_output(ctx, &out, &out_len));
    fail += expect_true("no output before exec", out == NULL && out_len == 0);
    kc_http_set_op(ctx, KC_HTTP_OP_PARSE);
    kc_http_set_input(ctx, "GET / HTTP/1.1\r\n\r\n", 19);
    fail += expect_int("exec parse", KC_HTTP_OK, kc_http_exec(ctx));
    fail += expect_int("get_output after exec", KC_HTTP_OK, kc_http_get_output(ctx, &out, &out_len));
    fail += expect_true("output present", out != NULL && out_len > 0);
    fail += expect_contains("output metadata", out, out_len, "request.target=/");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_method.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_method(void) {
    const char *name = "kc_http_set_method";
    const char *detail = "sets the request method";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    kc_http_set_method(NULL, "TRACE");
    kc_http_set_method(ctx, "TRACE");
    fail += expect_int("exec build request", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output method", out, out_len, "TRACE / HTTP/1.1\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_target.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_target(void) {
    const char *name = "kc_http_set_target";
    const char *detail = "sets the request target";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    kc_http_set_target(NULL, "/api?a=1");
    kc_http_set_target(ctx, "/api?a=1");
    fail += expect_int("exec build request", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output target", out, out_len, "GET /api?a=1 HTTP/1.1\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_version(void) {
    const char *name = "kc_http_set_version";
    const char *detail = "sets the wire version";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    kc_http_set_version(NULL, "1.0");
    kc_http_set_version(ctx, "1.0");
    fail += expect_int("exec build request", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output version", out, out_len, "GET / HTTP/1.0\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_status.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_status(void) {
    const char *name = "kc_http_set_status";
    const char *detail = "sets the response status";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_status(NULL, 201);
    kc_http_set_status(ctx, 201);
    fail += expect_int("exec build response", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output status", out, out_len, "HTTP/1.1 201 Created\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_reason.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_reason(void) {
    const char *name = "kc_http_set_reason";
    const char *detail = "sets the response reason phrase";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_reason(NULL, "Custom");
    kc_http_set_reason(ctx, "Custom");
    fail += expect_int("exec build response", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_prefix("output reason", out, out_len, "HTTP/1.1 200 Custom\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_chunked.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_chunked(void) {
    const char *name = "kc_http_set_chunked";
    const char *detail = "enables chunked transfer encoding";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_chunked(NULL, 1);
    kc_http_set_chunked(ctx, 1);
    kc_http_set_input(ctx, "hello", 5);
    fail += expect_int("exec build response", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output chunked header", out, out_len, "Transfer-Encoding: chunked\r\n");
    fail += expect_contains("output chunk framing", out, out_len, "5\r\nhello\r\n0\r\n\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_set_chunk_size.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_set_chunk_size(void) {
    const char *name = "kc_http_set_chunk_size";
    const char *detail = "bounds chunked body slices";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_chunked(ctx, 1);
    kc_http_set_chunk_size(NULL, 1);
    kc_http_set_chunk_size(ctx, 2);
    kc_http_set_input(ctx, "hello", 5);
    fail += expect_int("exec build response", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output sliced chunks", out, out_len, "2\r\nhe\r\n2\r\nll\r\n1\r\no\r\n0\r\n\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_add_header.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_add_header(void) {
    const char *name = "kc_http_add_header";
    const char *detail = "adds build headers";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    fail += expect_int("add NULL header", KC_HTTP_ERROR, kc_http_add_header(NULL, "h"));
    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    fail += expect_int("add header", KC_HTTP_OK, kc_http_add_header(ctx, "X-Test: 1"));
    fail += expect_int("add second header", KC_HTTP_OK, kc_http_add_header(ctx, "Host: example.com"));
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_REQUEST);
    fail += expect_int("exec build request", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output header", out, out_len, "X-Test: 1\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_add_trailer.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_add_trailer(void) {
    const char *name = "kc_http_add_trailer";
    const char *detail = "adds chunked build trailers";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    fail += expect_int("add NULL trailer", KC_HTTP_ERROR, kc_http_add_trailer(NULL, "t"));
    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    fail += expect_int("add trailer", KC_HTTP_OK, kc_http_add_trailer(ctx, "X-Checksum: abc"));
    kc_http_set_op(ctx, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_chunked(ctx, 1);
    kc_http_set_input(ctx, "hi", 2);
    fail += expect_int("exec build response", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output trailer", out, out_len, "0\r\nX-Checksum: abc\r\n\r\n");
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_http_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_exec(void) {
    const char *name = "kc_http_exec";
    const char *detail = "parses and builds messages";
    const char *req = "POST /api HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello";
    kc_http_options_t opts;
    kc_http_t *ctx;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);
    kc_http_set_op(ctx, KC_HTTP_OP_PARSE);
    kc_http_set_input(ctx, req, strlen(req));
    fail += expect_int("exec parse", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("output type", out, out_len, "http.type=request\n");
    fail += expect_contains("output method", out, out_len, "request.method=POST\n");
    fail += expect_contains("output target", out, out_len, "request.target=/api\n");
    fail += expect_contains("output header", out, out_len, "header.content-length=5\n");
    fail += expect_contains("output body", out, out_len, "body.length=5\n\nhello");
    kc_http_set_input(ctx, "GET / HTTP/1.1\r\nHost: a.com\r\nContent-Length: 5\r\n\r\nhello",
        strlen("GET / HTTP/1.1\r\nHost: a.com\r\nContent-Length: 5\r\n\r\nhello"));
    fail += expect_int("exec boundary", KC_HTTP_OK, kc_http_exec(ctx));
    kc_http_get_output(ctx, &out, &out_len);
    fail += expect_contains("boundary host", out, out_len, "header.host=a.com\n");
    fail += expect_not_contains("boundary no replay", out, out_len, "header.host=a.GET");
    kc_http_set_all(ctx, 1);
    kc_http_set_input(ctx, "GET\n", 4);
    fail += expect_int("exec malformed", KC_HTTP_ERROR, kc_http_exec(ctx));
    kc_http_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests that two contexts coexist and stop is isolated.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_http_multictx(void) {
    const char *name = "kc_http_multictx";
    const char *detail = "two contexts coexist independently";
    kc_http_options_t opts;
    kc_http_t *a;
    kc_http_t *b;
    unsigned char *out_a = NULL;
    unsigned char *out_b = NULL;
    size_t len_a = 0;
    size_t len_b = 0;
    int fail = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&a, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    if (kc_http_open(&b, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        kc_http_close(a);
        case_result(1, name, detail);
        return 1;
    }
    kc_http_options_free(&opts);

    kc_http_set_op(a, KC_HTTP_OP_BUILD_RESPONSE);
    kc_http_set_status(a, 201);
    kc_http_set_input(a, "A", 1);
    kc_http_set_op(b, KC_HTTP_OP_BUILD_REQUEST);
    kc_http_set_method(b, "POST");
    kc_http_set_input(b, "B", 1);

    fail += expect_int("exec a", KC_HTTP_OK, kc_http_exec(a));
    fail += expect_int("exec b", KC_HTTP_OK, kc_http_exec(b));
    kc_http_get_output(a, &out_a, &len_a);
    kc_http_get_output(b, &out_b, &len_b);
    fail += expect_prefix("output a", out_a, len_a, "HTTP/1.1 201 Created\r\n");
    fail += expect_prefix("output b", out_b, len_b, "POST / HTTP/1.1\r\n");
    fail += expect_contains("output a body", out_a, len_a, "Content-Length: 1\r\n\r\nA");
    fail += expect_contains("output b body", out_b, len_b, "Content-Length: 1\r\n\r\nB");

    fail += expect_int("stop a", KC_HTTP_OK, kc_http_stop(a));
    fail += expect_int("stop b", KC_HTTP_OK, kc_http_stop(b));
    kc_http_close(a);
    kc_http_close(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 22;
    test_case_current = 0;
    run_case(&rc, case_kc_http_version);
    run_case(&rc, case_kc_http_options_default);
    run_case(&rc, case_kc_http_options_load_env);
    run_case(&rc, case_kc_http_options_free);
    run_case(&rc, case_kc_http_open);
    run_case(&rc, case_kc_http_close);
    run_case(&rc, case_kc_http_stop);
    run_case(&rc, case_kc_http_set_op);
    run_case(&rc, case_kc_http_set_all);
    run_case(&rc, case_kc_http_set_input);
    run_case(&rc, case_kc_http_get_output);
    run_case(&rc, case_kc_http_set_method);
    run_case(&rc, case_kc_http_set_target);
    run_case(&rc, case_kc_http_set_version);
    run_case(&rc, case_kc_http_set_status);
    run_case(&rc, case_kc_http_set_reason);
    run_case(&rc, case_kc_http_set_chunked);
    run_case(&rc, case_kc_http_set_chunk_size);
    run_case(&rc, case_kc_http_add_header);
    run_case(&rc, case_kc_http_add_trailer);
    run_case(&rc, case_kc_http_exec);
    run_case(&rc, case_kc_http_multictx);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_http_version") == 0) return case_kc_http_version();
    if (strcmp(argv[1], "kc_http_options_default") == 0) return case_kc_http_options_default();
    if (strcmp(argv[1], "kc_http_options_load_env") == 0) return case_kc_http_options_load_env();
    if (strcmp(argv[1], "kc_http_options_free") == 0) return case_kc_http_options_free();
    if (strcmp(argv[1], "kc_http_open") == 0) return case_kc_http_open();
    if (strcmp(argv[1], "kc_http_close") == 0) return case_kc_http_close();
    if (strcmp(argv[1], "kc_http_stop") == 0) return case_kc_http_stop();
    if (strcmp(argv[1], "kc_http_set_op") == 0) return case_kc_http_set_op();
    if (strcmp(argv[1], "kc_http_set_all") == 0) return case_kc_http_set_all();
    if (strcmp(argv[1], "kc_http_set_input") == 0) return case_kc_http_set_input();
    if (strcmp(argv[1], "kc_http_get_output") == 0) return case_kc_http_get_output();
    if (strcmp(argv[1], "kc_http_set_method") == 0) return case_kc_http_set_method();
    if (strcmp(argv[1], "kc_http_set_target") == 0) return case_kc_http_set_target();
    if (strcmp(argv[1], "kc_http_set_version") == 0) return case_kc_http_set_version();
    if (strcmp(argv[1], "kc_http_set_status") == 0) return case_kc_http_set_status();
    if (strcmp(argv[1], "kc_http_set_reason") == 0) return case_kc_http_set_reason();
    if (strcmp(argv[1], "kc_http_set_chunked") == 0) return case_kc_http_set_chunked();
    if (strcmp(argv[1], "kc_http_set_chunk_size") == 0) return case_kc_http_set_chunk_size();
    if (strcmp(argv[1], "kc_http_add_header") == 0) return case_kc_http_add_header();
    if (strcmp(argv[1], "kc_http_add_trailer") == 0) return case_kc_http_add_trailer();
    if (strcmp(argv[1], "kc_http_exec") == 0) return case_kc_http_exec();
    if (strcmp(argv[1], "kc_http_multictx") == 0) return case_kc_http_multictx();
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
