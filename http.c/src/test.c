/**
 * test.c - libhttp public API contract tests.
 * Summary: Validates each exported libhttp function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libhttp.h"

#include <stdio.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test result.
 * @param fail Non-zero when the case failed.
 * @param name Public function name.
 * @param detail Verified behavior.
 * @return None.
 */
static void test_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs a test function.
 * @param rc Failure accumulator.
 * @param fn Test function.
 * @return None.
 */
static void test_run(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Checks an integer.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Zero on success, one on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected == actual) return 0;
    printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
    return 1;
}

/**
 * Checks a condition.
 * @param name Check description.
 * @param condition Expected condition.
 * @return Zero on success, one on failure.
 */
static int expect_true(const char *name, int condition) {
    if (condition) return 0;
    printf("[FAIL] %s\n", name);
    return 1;
}

/**
 * Checks for text in bytes.
 * @param name Check description.
 * @param data Data bytes.
 * @param size Data size.
 * @param needle Required text.
 * @return Zero on success, one on failure.
 */
static int expect_contains(const char *name, const void *data, size_t size,
    const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    for (i = 0; i + n <= size; i++) {
        if (memcmp((const unsigned char *)data + i, needle, n) == 0) return 0;
    }
    printf("[FAIL] %s: missing '%s'\n", name, needle);
    return 1;
}

/**
 * Checks that bytes omit text.
 * @param data Data bytes.
 * @param size Data size.
 * @param needle Forbidden text.
 * @return Zero when absent, one when present.
 */
static int expect_absent(const void *data, size_t size, const char *needle) {
    size_t n = strlen(needle);
    size_t i;
    for (i = 0; i + n <= size; i++) {
        if (memcmp((const unsigned char *)data + i, needle, n) == 0) return 1;
    }
    return 0;
}

/**
 * Checks a prefix.
 * @param name Check description.
 * @param data Data bytes.
 * @param size Data size.
 * @param prefix Required prefix.
 * @return Zero on success, one on failure.
 */
static int expect_prefix(const char *name, const void *data, size_t size,
    const char *prefix) {
    size_t n = strlen(prefix);
    return expect_true(name, size >= n && memcmp(data, prefix, n) == 0);
}

/**
 * Checks a suffix.
 * @param name Check description.
 * @param data Data bytes.
 * @param size Data size.
 * @param suffix Required suffix.
 * @param suffix_size Required suffix size.
 * @return Zero on success, one on failure.
 */
static int expect_suffix(const char *name, const void *data, size_t size,
    const void *suffix, size_t suffix_size) {
    return expect_true(name, size >= suffix_size &&
        memcmp((const unsigned char *)data + size - suffix_size, suffix, suffix_size) == 0);
}

/**
 * Opens a context for a test.
 * @param fail Failure accumulator.
 * @return Open context, or NULL.
 */
static kc_http_t *test_context(int *fail) {
    kc_http_t *ctx = NULL;
    *fail += expect_int("open context", KC_HTTP_OK, kc_http_open(&ctx));
    *fail += expect_true("context allocated", ctx != NULL);
    return ctx;
}

/**
 * Tests kc_http_open.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_open(void) {
    kc_http_t *ctx = (kc_http_t *)1;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;
    fail += expect_int("NULL destination", KC_HTTP_ERROR, kc_http_open(NULL));
    fail += expect_int("open", KC_HTTP_OK, kc_http_open(&ctx));
    fail += expect_true("allocated", ctx != NULL);
    fail += expect_int("default build", KC_HTTP_OK,
        kc_http_build_request(ctx, NULL, 0, &data, &size));
    fail += expect_prefix("default values", data, size, "GET / HTTP/1.1\r\n");
    kc_http_free(data);
    kc_http_close(ctx);
    test_result(fail, "kc_http_open", "validates output and creates defaults");
    return fail != 0;
}

/**
 * Tests kc_http_close.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_close(void) {
    int fail = 0;
    kc_http_t *ctx = test_context(&fail);
    kc_http_close(NULL);
    kc_http_close(ctx);
    test_result(fail, "kc_http_close", "releases NULL and configured contexts");
    return fail != 0;
}

/**
 * Tests kc_http_set_method.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_method(void) {
    char value[] = "POST";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_method(NULL, "POST"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("set method", KC_HTTP_OK, kc_http_set_method(ctx, value));
        value[0] = 'X';
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_request(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("copied method", data, size, "POST / HTTP/1.1\r\n");
        kc_http_free(data);
        fail += expect_int("NULL default", KC_HTTP_OK, kc_http_set_method(ctx, NULL));
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_method", "copies values and restores defaults");
    return fail != 0;
}

/**
 * Tests kc_http_set_target.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_target(void) {
    char value[] = "/copy?q=1";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_target(NULL, "/"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("set target", KC_HTTP_OK, kc_http_set_target(ctx, value));
        value[1] = 'X';
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_request(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("copied target", data, size, "GET /copy?q=1 HTTP/1.1\r\n");
        kc_http_free(data);
        fail += expect_int("NULL default", KC_HTTP_OK, kc_http_set_target(ctx, NULL));
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_target", "copies request targets");
    return fail != 0;
}

/**
 * Tests kc_http_set_version.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_version(void) {
    char value[] = "1.0";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_version(NULL, "1.0"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("set version", KC_HTTP_OK, kc_http_set_version(ctx, value));
        value[2] = '9';
        fail += expect_int("build HTTP 1.0", KC_HTTP_OK, kc_http_build_request(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("copied version", data, size, "GET / HTTP/1.0\r\n");
        kc_http_free(data);
        fail += expect_int("NULL default", KC_HTTP_OK, kc_http_set_version(ctx, NULL));
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_version", "copies versions and supports HTTP/1.0");
    return fail != 0;
}

/**
 * Tests kc_http_set_status.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_status(void) {
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_status(NULL, 201));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("low status", KC_HTTP_ERROR, kc_http_set_status(ctx, 99));
        fail += expect_int("high status", KC_HTTP_ERROR, kc_http_set_status(ctx, 600));
        fail += expect_int("set status", KC_HTTP_OK, kc_http_set_status(ctx, 201));
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_response(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("status wire", data, size, "HTTP/1.1 201 Created\r\n");
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_status", "rejects invalid status codes");
    return fail != 0;
}

/**
 * Tests kc_http_set_reason.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_reason(void) {
    char value[] = "Local reason";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_reason(NULL, "x"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("set reason", KC_HTTP_OK, kc_http_set_reason(ctx, value));
        value[0] = 'X';
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_response(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("copied reason", data, size, "HTTP/1.1 200 Local reason\r\n");
        kc_http_free(data);
        fail += expect_int("NULL derived", KC_HTTP_OK, kc_http_set_reason(ctx, NULL));
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_reason", "copies response phrases");
    return fail != 0;
}

/**
 * Tests kc_http_set_chunked.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_chunked(void) {
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_chunked(NULL, 1));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("enable", KC_HTTP_OK, kc_http_set_chunked(ctx, 1));
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_response(ctx, "hello", 5, &data, &size));
        fail += expect_contains("transfer encoding", data, size, "Transfer-Encoding: chunked\r\n");
        fail += expect_suffix("terminal framing", data, size, "5\r\nhello\r\n0\r\n\r\n", 15);
        kc_http_free(data);
        fail += expect_int("disable", KC_HTTP_OK, kc_http_set_chunked(ctx, 0));
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_chunked", "controls framing without retained body state");
    return fail != 0;
}

/**
 * Tests kc_http_set_chunk_size.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_set_chunk_size(void) {
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_set_chunk_size(NULL, 2));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("zero size", KC_HTTP_ERROR, kc_http_set_chunk_size(ctx, 0));
        fail += expect_int("set size", KC_HTTP_OK, kc_http_set_chunk_size(ctx, 2));
        fail += expect_int("chunked", KC_HTTP_OK, kc_http_set_chunked(ctx, 1));
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_request(ctx, "hello", 5, &data, &size));
        fail += expect_contains("chunk slices", data, size, "2\r\nhe\r\n2\r\nll\r\n1\r\no\r\n0\r\n\r\n");
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_set_chunk_size", "rejects zero and slices chunks");
    return fail != 0;
}

/**
 * Tests kc_http_add_header.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_add_header(void) {
    char value[] = "one";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_add_header(NULL, "X", "1"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("NULL name", KC_HTTP_ERROR, kc_http_add_header(ctx, NULL, "1"));
        fail += expect_int("bad name", KC_HTTP_ERROR, kc_http_add_header(ctx, "X Bad", "1"));
        fail += expect_int("header", KC_HTTP_OK, kc_http_add_header(ctx, "X-Test", value));
        value[0] = 'X';
        fail += expect_int("repeated", KC_HTTP_OK, kc_http_add_header(ctx, "X-Test", "two"));
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_request(ctx, NULL, 0, &data, &size));
        fail += expect_contains("copied header", data, size, "X-Test: one\r\n");
        fail += expect_contains("repeated header", data, size, "X-Test: two\r\n");
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_add_header", "validates, copies, and preserves fields");
    return fail != 0;
}

/**
 * Tests kc_http_add_trailer.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_add_trailer(void) {
    char value[] = "abc";
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR, kc_http_add_trailer(NULL, "X", "1"));
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("NULL value", KC_HTTP_ERROR, kc_http_add_trailer(ctx, "X", NULL));
        fail += expect_int("trailer", KC_HTTP_OK, kc_http_add_trailer(ctx, "X-Sum", value));
        value[0] = 'X';
        fail += expect_int("chunked", KC_HTTP_OK, kc_http_set_chunked(ctx, 1));
        fail += expect_int("build", KC_HTTP_OK, kc_http_build_response(ctx, "a", 1, &data, &size));
        fail += expect_suffix("copied trailer", data, size, "0\r\nX-Sum: abc\r\n\r\n", 17);
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_add_trailer", "validates and copies chunk trailers");
    return fail != 0;
}

/**
 * Tests kc_http_parse.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_parse(void) {
    const char *chunked = "POST /p?q=1 HTTP/1.1\r\nHost: Example\r\nTransfer-Encoding: chunked\r\n\r\n2;x=y\r\nhi\r\n3\r\nbye\r\n0\r\nX-Sum: yes\r\n\r\n";
    const char *messages = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
    const char *bad[] = {
        "GET / HTTP/1.1\r\nHost: x\r\n",
        "GET / HTTP/1.1\r\nBroken\r\n\r\n",
        "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nab",
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n",
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX\r\n",
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Sum: yes\r\n",
        "POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\na",
        "POST / HTTP/1.1\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
    };
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    size_t i;
    int fail = expect_int("NULL context", KC_HTTP_ERROR,
        kc_http_parse(NULL, "GET / HTTP/1.1\r\n\r\n", 18, 0, &data, &size));
    ctx = test_context(&fail);
    if (ctx) {
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output data", KC_HTTP_ERROR,
            kc_http_parse(ctx, "GET / HTTP/1.1\r\n\r\n", 18, 0, NULL, &size));
        fail += expect_true("NULL output data clears size", size == 0);
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output size", KC_HTTP_ERROR,
            kc_http_parse(ctx, "GET / HTTP/1.1\r\n\r\n", 18, 0, &data, NULL));
        fail += expect_true("NULL output size clears data", data == NULL);
        fail += expect_int("chunk extensions", KC_HTTP_OK,
            kc_http_parse(ctx, chunked, strlen(chunked), 0, &data, &size));
        fail += expect_contains("query", data, size, "request.query=q=1\n");
        fail += expect_contains("lowercase header", data, size, "header.host=Example\n");
        fail += expect_contains("trailer", data, size, "trailer.x-sum=yes\n");
        fail += expect_suffix("dechunked body", data, size, "\n\nhibye", 7);
        kc_http_free(data);
        data = NULL;
        fail += expect_int("single message", KC_HTTP_OK,
            kc_http_parse(ctx, messages, strlen(messages), 0, &data, &size));
        fail += expect_contains("first message", data, size, "request.target=/a\n");
        fail += expect_true("single stops", expect_absent(data, size, "request.target=/b\n") == 0);
        kc_http_free(data);
        data = NULL;
        fail += expect_int("multi message", KC_HTTP_OK,
            kc_http_parse(ctx, messages, strlen(messages), 1, &data, &size));
        fail += expect_contains("second message", data, size, "request.target=/b\n");
        kc_http_free(data);
        data = NULL;
        fail += expect_int("HTTP 0.9", KC_HTTP_OK,
            kc_http_parse(ctx, "GET /legacy\r\n\r\n", 15, 0, &data, &size));
        fail += expect_contains("HTTP 0.9 target", data, size, "request.target=/legacy\n");
        kc_http_free(data);
        data = NULL;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            fail += expect_int("hostile framing", KC_HTTP_ERROR,
                kc_http_parse(ctx, bad[i], strlen(bad[i]), 1, &data, &size));
            fail += expect_true("failure clears results", data == NULL && size == 0);
        }
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_parse", "normalizes framing and rejects hostile messages");
    return fail != 0;
}

/**
 * Tests kc_http_build_request.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_build_request(void) {
    const unsigned char body[] = { 'A', 0, 'B' };
    kc_http_t *ctx;
    void *data = NULL;
    void *parsed = NULL;
    size_t size = 0;
    size_t parsed_size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR,
        kc_http_build_request(NULL, NULL, 0, &data, &size));
    ctx = test_context(&fail);
    if (ctx) {
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output data", KC_HTTP_ERROR,
            kc_http_build_request(ctx, NULL, 0, NULL, &size));
        fail += expect_true("NULL output data clears size", size == 0);
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output size", KC_HTTP_ERROR,
            kc_http_build_request(ctx, NULL, 0, &data, NULL));
        fail += expect_true("NULL output size clears data", data == NULL);
        fail += expect_int("missing body", KC_HTTP_ERROR, kc_http_build_request(ctx, NULL, 1, &data, &size));
        fail += expect_int("binary HTTP 1", KC_HTTP_OK, kc_http_build_request(ctx, body, sizeof(body), &data, &size));
        fail += expect_suffix("binary body", data, size, body, sizeof(body));
        fail += expect_int("parse binary", KC_HTTP_OK, kc_http_parse(ctx, data, size, 0, &parsed, &parsed_size));
        fail += expect_suffix("binary normalized", parsed, parsed_size, body, sizeof(body));
        kc_http_free(parsed);
        kc_http_free(data);
        data = NULL;
        fail += expect_int("HTTP 2 version", KC_HTTP_OK, kc_http_set_version(ctx, "2"));
        fail += expect_int("build HTTP 2", KC_HTTP_OK, kc_http_build_request(ctx, "h2", 2, &data, &size));
        fail += expect_prefix("HTTP 2 preface", data, size, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
        fail += expect_int("parse HTTP 2", KC_HTTP_OK, kc_http_parse(ctx, data, size, 0, &parsed, &parsed_size));
        fail += expect_contains("HTTP 2 normalized", parsed, parsed_size, "http.version=2\n");
        kc_http_free(parsed);
        kc_http_free(data);
        data = NULL;
        fail += expect_int("HTTP 3 version", KC_HTTP_OK, kc_http_set_version(ctx, "3"));
        fail += expect_int("build HTTP 3", KC_HTTP_OK, kc_http_build_request(ctx, "h3", 2, &data, &size));
        fail += expect_int("parse HTTP 3", KC_HTTP_OK, kc_http_parse(ctx, data, size, 0, &parsed, &parsed_size));
        fail += expect_contains("HTTP 3 normalized", parsed, parsed_size, "http.version=3\n");
        kc_http_free(parsed);
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_build_request", "builds owned binary HTTP/1, HTTP/2, and HTTP/3");
    return fail != 0;
}

/**
 * Tests kc_http_build_response.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_build_response(void) {
    kc_http_t *ctx;
    void *data = NULL;
    void *parsed = NULL;
    size_t size = 0;
    size_t parsed_size = 0;
    int fail = expect_int("NULL context", KC_HTTP_ERROR,
        kc_http_build_response(NULL, NULL, 0, &data, &size));
    ctx = test_context(&fail);
    if (ctx) {
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output data", KC_HTTP_ERROR,
            kc_http_build_response(ctx, NULL, 0, NULL, &size));
        fail += expect_true("NULL output data clears size", size == 0);
        data = (void *)1;
        size = 1;
        fail += expect_int("NULL output size", KC_HTTP_ERROR,
            kc_http_build_response(ctx, NULL, 0, &data, NULL));
        fail += expect_true("NULL output size clears data", data == NULL);
        fail += expect_int("status", KC_HTTP_OK, kc_http_set_status(ctx, 204));
        fail += expect_int("HTTP 1", KC_HTTP_OK, kc_http_build_response(ctx, NULL, 0, &data, &size));
        fail += expect_prefix("HTTP 1 response", data, size, "HTTP/1.1 204 No Content\r\n");
        kc_http_free(data);
        data = NULL;
        fail += expect_int("HTTP 2 version", KC_HTTP_OK, kc_http_set_version(ctx, "2"));
        fail += expect_int("build HTTP 2", KC_HTTP_OK, kc_http_build_response(ctx, "ok", 2, &data, &size));
        fail += expect_int("parse HTTP 2", KC_HTTP_OK, kc_http_parse(ctx, data, size, 0, &parsed, &parsed_size));
        fail += expect_contains("HTTP 2 response", parsed, parsed_size, "http.type=response\n");
        kc_http_free(parsed);
        kc_http_free(data);
        data = NULL;
        fail += expect_int("HTTP 3 version", KC_HTTP_OK, kc_http_set_version(ctx, "3"));
        fail += expect_int("build HTTP 3", KC_HTTP_OK, kc_http_build_response(ctx, "ok", 2, &data, &size));
        fail += expect_int("parse HTTP 3", KC_HTTP_OK, kc_http_parse(ctx, data, size, 0, &parsed, &parsed_size));
        fail += expect_contains("HTTP 3 response", parsed, parsed_size, "http.type=response\n");
        kc_http_free(parsed);
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_build_response", "builds owned response messages");
    return fail != 0;
}

/**
 * Tests kc_http_free.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_free(void) {
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;
    kc_http_free(NULL);
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_int("library allocation", KC_HTTP_OK, kc_http_build_request(ctx, "x", 1, &data, &size));
        fail += expect_true("allocated result", data != NULL && size > 0);
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_free", "releases library-owned result buffers");
    return fail != 0;
}

/**
 * Tests kc_http_get_error.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_get_error(void) {
    kc_http_t *ctx;
    void *data = NULL;
    size_t size = 0;
    int fail = expect_true("NULL error", kc_http_get_error(NULL) == NULL);
    ctx = test_context(&fail);
    if (ctx) {
        fail += expect_true("initial error", kc_http_get_error(ctx) == NULL);
        fail += expect_int("failure", KC_HTTP_ERROR, kc_http_build_request(ctx, NULL, 1, &data, &size));
        fail += expect_true("contextual error", kc_http_get_error(ctx) != NULL);
        fail += expect_int("success", KC_HTTP_OK, kc_http_build_request(ctx, NULL, 0, &data, &size));
        fail += expect_true("cleared error", kc_http_get_error(ctx) == NULL);
        kc_http_free(data);
        kc_http_close(ctx);
    }
    test_result(fail, "kc_http_get_error", "reports failures and clears after success");
    return fail != 0;
}

/**
 * Tests kc_http_version.
 * @return Zero on success, one on failure.
 */
static int case_kc_http_version(void) {
    int fail = expect_true("build timestamp", kc_http_version() != 0U);
    test_result(fail, "kc_http_version", "returns a non-zero build timestamp");
    return fail != 0;
}

/**
 * Runs an exact public-function case.
 * @param name Requested function name.
 * @return Case result, or two for an unknown name.
 */
static int test_named(const char *name) {
    if (strcmp(name, "kc_http_open") == 0) return case_kc_http_open();
    if (strcmp(name, "kc_http_close") == 0) return case_kc_http_close();
    if (strcmp(name, "kc_http_set_method") == 0) return case_kc_http_set_method();
    if (strcmp(name, "kc_http_set_target") == 0) return case_kc_http_set_target();
    if (strcmp(name, "kc_http_set_version") == 0) return case_kc_http_set_version();
    if (strcmp(name, "kc_http_set_status") == 0) return case_kc_http_set_status();
    if (strcmp(name, "kc_http_set_reason") == 0) return case_kc_http_set_reason();
    if (strcmp(name, "kc_http_set_chunked") == 0) return case_kc_http_set_chunked();
    if (strcmp(name, "kc_http_set_chunk_size") == 0) return case_kc_http_set_chunk_size();
    if (strcmp(name, "kc_http_add_header") == 0) return case_kc_http_add_header();
    if (strcmp(name, "kc_http_add_trailer") == 0) return case_kc_http_add_trailer();
    if (strcmp(name, "kc_http_parse") == 0) return case_kc_http_parse();
    if (strcmp(name, "kc_http_build_request") == 0) return case_kc_http_build_request();
    if (strcmp(name, "kc_http_build_response") == 0) return case_kc_http_build_response();
    if (strcmp(name, "kc_http_free") == 0) return case_kc_http_free();
    if (strcmp(name, "kc_http_get_error") == 0) return case_kc_http_get_error();
    if (strcmp(name, "kc_http_version") == 0) return case_kc_http_version();
    fprintf(stderr, "unknown case: %s\n", name);
    return 2;
}

/**
 * Runs all cases or one exact case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    int rc = 0;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") != 0) return test_named(argv[1]);
    test_case_total = 17;
    test_case_current = 0;
    test_run(&rc, case_kc_http_open);
    test_run(&rc, case_kc_http_close);
    test_run(&rc, case_kc_http_set_method);
    test_run(&rc, case_kc_http_set_target);
    test_run(&rc, case_kc_http_set_version);
    test_run(&rc, case_kc_http_set_status);
    test_run(&rc, case_kc_http_set_reason);
    test_run(&rc, case_kc_http_set_chunked);
    test_run(&rc, case_kc_http_set_chunk_size);
    test_run(&rc, case_kc_http_add_header);
    test_run(&rc, case_kc_http_add_trailer);
    test_run(&rc, case_kc_http_parse);
    test_run(&rc, case_kc_http_build_request);
    test_run(&rc, case_kc_http_build_response);
    test_run(&rc, case_kc_http_free);
    test_run(&rc, case_kc_http_get_error);
    test_run(&rc, case_kc_http_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}
