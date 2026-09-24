/**
 * test.c - Public contract tests for libhttp.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libhttp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <process.h>
#define TEST_GETPID _getpid
#define TEST_CLI_NAME "http.exe"
#else
#include <unistd.h>
#define TEST_GETPID getpid
#define TEST_CLI_NAME "http"
#endif
#endif

static int test_case_total;
static int test_case_current;
static const char *test_program_path;

static int expect_true(const char *label, int condition) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

static int expect_int(const char *label, int expected, int actual) {
    if (expected == actual) return 0;
    fprintf(stderr, "FAIL: %s: expected %d, got %d\n", label, expected, actual);
    return 1;
}

static int expect_bytes(
    const char *label,
    const void *expected,
    size_t expected_size,
    const void *actual,
    size_t actual_size
) {
    if (expected_size == actual_size &&
        (expected_size == 0U || memcmp(expected, actual, expected_size) == 0)) {
        return 0;
    }
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

static int expect_contains(
    const char *label,
    const void *data,
    size_t size,
    const char *needle
) {
    size_t n = strlen(needle);
    size_t i;
    if (n == 0U) return 0;
    for (i = 0; i + n <= size; i++) {
        if (memcmp((const unsigned char *)data + i, needle, n) == 0) return 0;
    }
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

static int expect_absent(
    const char *label,
    const void *data,
    size_t size,
    const char *needle
) {
    size_t n = strlen(needle);
    size_t i;
    if (n == 0U) return 0;
    for (i = 0; i + n <= size; i++) {
        if (memcmp((const unsigned char *)data + i, needle, n) == 0) {
            fprintf(stderr, "FAIL: %s\n", label);
            return 1;
        }
    }
    return 0;
}

static void test_result(int fail, const char *name, const char *detail) {
    test_case_current++;
    printf("[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail);
}

static void test_run(int *rc, int (*fn)(void)) {
    if (fn() != 0) (*rc)++;
}

typedef struct {
    int requests;
    int responses;
    int errors;
    int last_error;
    char method[32];
    char target[128];
    char path[128];
    char query[128];
    int status;
    char reason[128];
    char host[128];
    char trailer_sum[128];
    unsigned char body[256];
    size_t body_size;
} parser_state_t;

static void copy_text(char *dst, size_t cap, const char *src) {
    if (cap == 0U) return;
    if (src == NULL) src = "";
    snprintf(dst, cap, "%s", src);
}

static void on_request(const kc_http_request_t *request, void *userdata) {
    parser_state_t *state = (parser_state_t *)userdata;
    size_t i;
    state->requests++;
    copy_text(state->method, sizeof(state->method), request->method);
    copy_text(state->target, sizeof(state->target), request->target);
    copy_text(state->path, sizeof(state->path), request->path);
    copy_text(state->query, sizeof(state->query), request->query);
    state->host[0] = '\0';
    state->trailer_sum[0] = '\0';
    for (i = 0; i < request->header_count; i++) {
        if (strcmp(request->headers[i].name, "host") == 0) {
            copy_text(state->host, sizeof(state->host), request->headers[i].value);
        }
    }
    for (i = 0; i < request->trailer_count; i++) {
        if (strcmp(request->trailers[i].name, "x-sum") == 0) {
            copy_text(
                state->trailer_sum,
                sizeof(state->trailer_sum),
                request->trailers[i].value
            );
        }
    }
    state->body_size = request->body_size < sizeof(state->body)
        ? request->body_size : sizeof(state->body);
    if (state->body_size != 0U) {
        memcpy(state->body, request->body, state->body_size);
    }
}

static void on_response(const kc_http_response_t *response, void *userdata) {
    parser_state_t *state = (parser_state_t *)userdata;
    state->responses++;
    state->status = response->status;
    copy_text(state->reason, sizeof(state->reason), response->reason);
    state->body_size = response->body_size < sizeof(state->body)
        ? response->body_size : sizeof(state->body);
    if (state->body_size != 0U) {
        memcpy(state->body, response->body, state->body_size);
    }
}

static void on_error(int status, void *userdata) {
    parser_state_t *state = (parser_state_t *)userdata;
    state->errors++;
    state->last_error = status;
}

static int case_kc_http_parser_open(void) {
    kc_http_parser_t *parser = (kc_http_parser_t *)1;
    parser_state_t state;
    int fail = 0;

    memset(&state, 0, sizeof(state));
    fail += expect_int("NULL out", KC_HTTP_EINVAL,
        kc_http_parser_open(NULL, on_request, NULL, on_error, &state));
    fail += expect_int("no message handlers", KC_HTTP_EINVAL,
        kc_http_parser_open(&parser, NULL, NULL, on_error, &state));
    fail += expect_true("failed open clears output", parser == NULL);
    fail += expect_int("valid open", KC_HTTP_OK,
        kc_http_parser_open(&parser, on_request, on_response, on_error, &state));
    fail += expect_true("parser allocated", parser != NULL);
    kc_http_parser_close(parser);

    test_result(fail, "kc_http_parser_open", "creates one parser per HTTP byte stream");
    return fail != 0;
}

static int case_kc_http_parser_write(void) {
    static const char a[] =
        "POST /api?q=1 HTTP/1.1\r\n"
        "Host: Example\r\n"
        "Content-Length: 5\r\n\r\n"
        "he";
    static const char b[] = "llo";
    static const char two[] =
        "GET /a HTTP/1.1\r\n\r\n"
        "GET /b HTTP/1.1\r\n\r\n";
    static const char bad[] =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 1\r\n"
        "Transfer-Encoding: chunked\r\n\r\n"
        "0\r\n\r\n";
    kc_http_parser_t *parser = NULL;
    parser_state_t state;
    int fail = 0;

    memset(&state, 0, sizeof(state));
    fail += expect_int("open", KC_HTTP_OK,
        kc_http_parser_open(&parser, on_request, on_response, on_error, &state));
    fail += expect_int("first fragment", KC_HTTP_OK,
        kc_http_parser_write(parser, a, sizeof(a) - 1U));
    fail += expect_int("no early request", 0, state.requests);
    fail += expect_int("second fragment", KC_HTTP_OK,
        kc_http_parser_write(parser, b, sizeof(b) - 1U));
    fail += expect_int("one request", 1, state.requests);
    fail += expect_true("method", strcmp(state.method, "POST") == 0);
    fail += expect_true("target", strcmp(state.target, "/api?q=1") == 0);
    fail += expect_true("path", strcmp(state.path, "/api") == 0);
    fail += expect_true("query", strcmp(state.query, "q=1") == 0);
    fail += expect_true("lowercase header name", strcmp(state.host, "Example") == 0);
    fail += expect_bytes("body", "hello", 5U, state.body, state.body_size);

    fail += expect_int("multiple complete requests", KC_HTTP_OK,
        kc_http_parser_write(parser, two, sizeof(two) - 1U));
    fail += expect_int("two more callbacks", 3, state.requests);

    {
        static const char chunk_a[] =
            "POST /chunk HTTP/1.1\r\n"
            "Transfer-Encoding: chunked\r\n\r\n"
            "2\r\nhi\r\n3\r\nb";
        static const char chunk_b[] =
            "ye\r\n0\r\nX-Sum: yes\r\n\r\n";

        fail += expect_int("chunked first fragment", KC_HTTP_OK,
            kc_http_parser_write(parser, chunk_a, sizeof(chunk_a) - 1U));
        fail += expect_int("chunked still incomplete", 3, state.requests);
        fail += expect_int("chunked completion", KC_HTTP_OK,
            kc_http_parser_write(parser, chunk_b, sizeof(chunk_b) - 1U));
        fail += expect_int("chunked callback", 4, state.requests);
        fail += expect_bytes("dechunked body", "hibye", 5U, state.body, state.body_size);
        fail += expect_true("parsed trailer", strcmp(state.trailer_sum, "yes") == 0);
    }

    fail += expect_int("malformed framing", KC_HTTP_EPARSE,
        kc_http_parser_write(parser, bad, sizeof(bad) - 1U));
    fail += expect_int("error callback", 1, state.errors);
    fail += expect_int("error status", KC_HTTP_EPARSE, state.last_error);
    kc_http_parser_close(parser);

    fail += expect_int("NULL parser", KC_HTTP_EINVAL,
        kc_http_parser_write(NULL, "x", 1U));

    test_result(fail, "kc_http_parser_write", "buffers fragments and emits complete messages");
    return fail != 0;
}

static int case_kc_http_parser_close(void) {
    kc_http_parser_t *parser = NULL;
    parser_state_t state;
    int fail = 0;

    memset(&state, 0, sizeof(state));
    fail += expect_int("open", KC_HTTP_OK,
        kc_http_parser_open(&parser, on_request, NULL, on_error, &state));
    fail += expect_int("partial write", KC_HTTP_OK,
        kc_http_parser_write(parser, "GET / HTTP/1.1\r\nHost:", strlen("GET / HTTP/1.1\r\nHost:")));
    kc_http_parser_close(parser);
    fail += expect_int("close reports incomplete stream", 1, state.errors);
    fail += expect_int("close parse status", KC_HTTP_EPARSE, state.last_error);
    kc_http_parser_close(NULL);

    test_result(fail, "kc_http_parser_close", "finalizes and releases parser state");
    return fail != 0;
}

static int case_kc_http_request(void) {
    const kc_http_field_t headers[] = {
        { "Host", "example.com" },
        { "X-Test", "one" }
    };
    const kc_http_field_t trailers[] = {
        { "X-Sum", "yes" }
    };
    const unsigned char body[] = { 'A', 0, 'B' };
    kc_http_request_t request;
    void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    fail += expect_int("default request", KC_HTTP_OK,
        kc_http_request(NULL, &data, &size));
    fail += expect_contains("default request line", data, size, "GET / HTTP/1.1\r\n");
    kc_http_free(data);

    memset(&request, 0, sizeof(request));
    request.method = "POST";
    request.target = "/upload";
    request.headers = headers;
    request.header_count = 2U;
    request.body = body;
    request.body_size = sizeof(body);
    fail += expect_int("binary request", KC_HTTP_OK,
        kc_http_request(&request, &data, &size));
    fail += expect_contains("request line", data, size, "POST /upload HTTP/1.1\r\n");
    fail += expect_contains("host header", data, size, "Host: example.com\r\n");
    fail += expect_bytes("binary tail", body, sizeof(body),
        (const unsigned char *)data + size - sizeof(body), sizeof(body));
    kc_http_free(data);

    memset(&request, 0, sizeof(request));
    request.method = "POST";
    request.chunked = 1;
    request.chunk_size = 2U;
    request.body = "abc";
    request.body_size = 3U;
    request.trailers = trailers;
    request.trailer_count = 1U;
    fail += expect_int("chunked request", KC_HTTP_OK,
        kc_http_request(&request, &data, &size));
    fail += expect_contains("chunked header", data, size, "Transfer-Encoding: chunked\r\n");
    fail += expect_contains("trailer", data, size, "0\r\nX-Sum: yes\r\n\r\n");
    kc_http_free(data);

    {
        const char *versions[] = { "2", "3" };
        size_t vi;
        for (vi = 0U; vi < 2U; vi++) {
            kc_http_parser_t *parser = NULL;
            parser_state_t state;

            memset(&request, 0, sizeof(request));
            request.version = versions[vi];
            request.method = "GET";
            request.target = "/proto";
            fail += expect_int("protocol request build", KC_HTTP_OK,
                kc_http_request(&request, &data, &size));

            memset(&state, 0, sizeof(state));
            fail += expect_int("protocol parser open", KC_HTTP_OK,
                kc_http_parser_open(
                    &parser,
                    on_request,
                    on_response,
                    on_error,
                    &state
                ));
            fail += expect_int("protocol request parse", KC_HTTP_OK,
                kc_http_parser_write(parser, data, size));
            fail += expect_int("protocol request callback", 1, state.requests);
            fail += expect_true("protocol target", strcmp(state.target, "/proto") == 0);
            kc_http_parser_close(parser);
            kc_http_free(data);
            data = NULL;
        }
    }

    request.body = NULL;
    request.body_size = 1U;
    fail += expect_int("invalid body", KC_HTTP_EINVAL,
        kc_http_request(&request, &data, &size));

    test_result(fail, "kc_http_request", "builds request wire bytes without persistent builder state");
    return fail != 0;
}

static int case_kc_http_response(void) {
    const kc_http_field_t headers[] = {
        { "Content-Type", "text/plain" }
    };
    kc_http_response_t response;
    void *data = NULL;
    size_t size = 0U;
    kc_http_parser_t *parser = NULL;
    parser_state_t state;
    int fail = 0;

    fail += expect_int("default response", KC_HTTP_OK,
        kc_http_response(NULL, &data, &size));
    fail += expect_contains("default status", data, size, "HTTP/1.1 200 OK\r\n");
    kc_http_free(data);

    memset(&response, 0, sizeof(response));
    response.status = 201;
    response.headers = headers;
    response.header_count = 1U;
    response.body = "created";
    response.body_size = 7U;
    fail += expect_int("response", KC_HTTP_OK,
        kc_http_response(&response, &data, &size));
    fail += expect_contains("status line", data, size, "HTTP/1.1 201 Created\r\n");
    fail += expect_contains("content type", data, size, "Content-Type: text/plain\r\n");

    memset(&state, 0, sizeof(state));
    fail += expect_int("parser open", KC_HTTP_OK,
        kc_http_parser_open(&parser, on_request, on_response, on_error, &state));
    fail += expect_int("parse response", KC_HTTP_OK,
        kc_http_parser_write(parser, data, size));
    fail += expect_int("response callback", 1, state.responses);
    fail += expect_int("status parsed", 201, state.status);
    fail += expect_bytes("response body", "created", 7U, state.body, state.body_size);
    kc_http_parser_close(parser);
    kc_http_free(data);

    {
        const char *versions[] = { "2", "3" };
        size_t vi;
        for (vi = 0U; vi < 2U; vi++) {
            memset(&response, 0, sizeof(response));
            response.version = versions[vi];
            response.status = 204;

            fail += expect_int("protocol response build", KC_HTTP_OK,
                kc_http_response(&response, &data, &size));

            memset(&state, 0, sizeof(state));
            parser = NULL;
            fail += expect_int("protocol response parser open", KC_HTTP_OK,
                kc_http_parser_open(
                    &parser,
                    on_request,
                    on_response,
                    on_error,
                    &state
                ));
            fail += expect_int("protocol response parse", KC_HTTP_OK,
                kc_http_parser_write(parser, data, size));
            fail += expect_int("protocol response callback", 1, state.responses);
            fail += expect_int("protocol response status", 204, state.status);
            kc_http_parser_close(parser);
            kc_http_free(data);
            data = NULL;
        }
    }

    memset(&response, 0, sizeof(response));
    response.status = 99;
    fail += expect_int("bad status", KC_HTTP_EINVAL,
        kc_http_response(&response, &data, &size));

    test_result(fail, "kc_http_response", "builds response wire bytes symmetrically");
    return fail != 0;
}

static int case_kc_http_free(void) {
    int fail = 0;
    void *data = NULL;
    size_t size = 0U;

    kc_http_free(NULL);
    fail += expect_int("library allocation", KC_HTTP_OK,
        kc_http_request(NULL, &data, &size));
    fail += expect_true("allocated wire bytes", data != NULL && size != 0U);
    kc_http_free(data);

    test_result(fail, "kc_http_free", "releases builder output");
    return fail != 0;
}

static int case_kc_http_strerror(void) {
    int fail = 0;
    fail += expect_true("ok", strcmp(kc_http_strerror(KC_HTTP_OK), "ok") == 0);
    fail += expect_true("invalid", strcmp(kc_http_strerror(KC_HTTP_EINVAL), "invalid argument") == 0);
    fail += expect_true("parse", strcmp(kc_http_strerror(KC_HTTP_EPARSE), "HTTP parse error") == 0);
    fail += expect_true("memory", strcmp(kc_http_strerror(KC_HTTP_ENOMEM), "out of memory") == 0);
    fail += expect_true("unknown", strcmp(kc_http_strerror(-999), "unknown error") == 0);
    test_result(fail, "kc_http_strerror", "maps public status values");
    return fail != 0;
}

static int case_kc_http_version(void) {
    int fail = 0;
    (void)kc_http_version();
    test_result(fail, "kc_http_version", "returns the compiled build version");
    return fail != 0;
}


#ifndef __EMSCRIPTEN__
static int test_cli_path(char *out, size_t cap) {
    const char *slash;
    const char *backslash;
    const char *sep;
    size_t dir_len;
    int n;

    if (test_program_path == NULL) return -1;
    slash = strrchr(test_program_path, '/');
    backslash = strrchr(test_program_path, '\\');
    sep = slash;
    if (backslash != NULL && (sep == NULL || backslash > sep)) sep = backslash;

    if (sep == NULL) {
#ifdef _WIN32
        n = snprintf(out, cap, ".\\%s", TEST_CLI_NAME);
#else
        n = snprintf(out, cap, "./%s", TEST_CLI_NAME);
#endif
        return n > 0 && (size_t)n < cap ? 0 : -1;
    }

    dir_len = (size_t)(sep - test_program_path + 1);
    if (dir_len + strlen(TEST_CLI_NAME) + 1U > cap) return -1;
    memcpy(out, test_program_path, dir_len);
    memcpy(out + dir_len, TEST_CLI_NAME, strlen(TEST_CLI_NAME) + 1U);
    return 0;
}

static int test_write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return -1;
    if (size != 0U && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) return -1;
    return 0;
}

static int test_read_file(
    const char *path,
    unsigned char **out,
    size_t *out_size
) {
    FILE *file;
    long end;
    unsigned char *data;
    size_t size;

    *out = NULL;
    *out_size = 0U;
    file = fopen(path, "rb");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    end = ftell(file);
    if (end < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    size = (size_t)end;
    data = (unsigned char *)malloc(size != 0U ? size : 1U);
    if (data == NULL) {
        fclose(file);
        return -1;
    }
    if (size != 0U && fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return -1;
    }
    fclose(file);
    *out = data;
    *out_size = size;
    return 0;
}

static int test_cli_run(
    const char *args,
    const void *input,
    size_t input_size,
    unsigned char **out,
    size_t *out_size,
    unsigned char **err,
    size_t *err_size
) {
    char cli[1024];
    char in_path[128];
    char out_path[128];
    char err_path[128];
    char command[4096];
    long pid = (long)TEST_GETPID();
    int rc;

    *out = NULL;
    *out_size = 0U;
    *err = NULL;
    *err_size = 0U;
    if (test_cli_path(cli, sizeof(cli)) != 0) return -1;

    snprintf(in_path, sizeof(in_path), "http-cli-%ld-in.tmp", pid);
    snprintf(out_path, sizeof(out_path), "http-cli-%ld-out.tmp", pid);
    snprintf(err_path, sizeof(err_path), "http-cli-%ld-err.tmp", pid);

    if (test_write_file(in_path, input, input_size) != 0) return -1;
    snprintf(
        command,
        sizeof(command),
        "\"%s\" %s < \"%s\" > \"%s\" 2> \"%s\"",
        cli,
        args,
        in_path,
        out_path,
        err_path
    );
    rc = system(command);

    if (test_read_file(out_path, out, out_size) != 0 ||
        test_read_file(err_path, err, err_size) != 0) {
        free(*out);
        free(*err);
        *out = NULL;
        *err = NULL;
        *out_size = 0U;
        *err_size = 0U;
        rc = -1;
    }

    remove(in_path);
    remove(out_path);
    remove(err_path);
    return rc;
}

static int case_kc_http_cli(void) {
    static const char request[] =
        "GET /x?q=1 HTTP/1.1\r\n"
        "Host: example.com\r\n\r\n";
    unsigned char *out = NULL;
    unsigned char *err = NULL;
    size_t out_size = 0U;
    size_t err_size = 0U;
    int rc;
    int fail = 0;

    rc = test_cli_run(
        "parse",
        request,
        sizeof(request) - 1U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli parse exit", 0, rc);
    fail += expect_contains("cli method", out, out_size, "request.method=GET\n");
    fail += expect_contains("cli target", out, out_size, "request.target=/x?q=1\n");
    fail += expect_contains("cli path", out, out_size, "request.path=/x\n");
    fail += expect_contains("cli query", out, out_size, "request.query=q=1\n");
    fail += expect_contains("cli header", out, out_size, "header.host=example.com\n");
    fail += expect_absent("cli default omits type", out, out_size, "http.type=");
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "parse --all",
        request,
        sizeof(request) - 1U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli parse all exit", 0, rc);
    fail += expect_contains("cli all type", out, out_size, "http.type=request\n");
    fail += expect_contains("cli all version", out, out_size, "http.version=1.1\n");
    fail += expect_contains("cli all body length", out, out_size, "body.length=0\n");
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "build request --method POST --target /submit --header \"X-Test: yes\"",
        "hi",
        2U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli build request exit", 0, rc);
    fail += expect_contains("cli request line", out, out_size, "POST /submit HTTP/1.1\r\n");
    fail += expect_contains("cli request header", out, out_size, "X-Test: yes\r\n");
    fail += expect_contains("cli request body", out, out_size, "\r\n\r\nhi");
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "build response --status 201 --header \"Content-Type: text/plain\"",
        "hello",
        5U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli build response exit", 0, rc);
    fail += expect_contains("cli response line", out, out_size, "HTTP/1.1 201 Created\r\n");
    fail += expect_contains("cli response header", out, out_size, "Content-Type: text/plain\r\n");
    fail += expect_contains("cli response body", out, out_size, "\r\n\r\nhello");
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "--version",
        "",
        0U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli version exit", 0, rc);
    fail += expect_contains("cli version", out, out_size, "http build ");
    fail += expect_true("cli version stderr empty", err_size == 0U);
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "--help",
        "",
        0U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_int("cli help exit", 0, rc);
    fail += expect_contains("cli help", out, out_size, "Usage:");
    fail += expect_true("cli help stderr empty", err_size == 0U);
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    rc = test_cli_run(
        "unknown",
        "",
        0U,
        &out,
        &out_size,
        &err,
        &err_size
    );
    fail += expect_true("cli invalid fails", rc != 0);
    fail += expect_contains(
        "cli invalid stderr",
        err,
        err_size,
        "http: unknown command"
    );
    free(out);
    free(err);
    out = NULL;
    err = NULL;

    test_result(fail, "kc_http_cli", "covers shipped parse/build/help/error contract");
    return fail != 0;
}
#endif

static int test_named(const char *name) {
    if (strcmp(name, "kc_http_parser_open") == 0) return case_kc_http_parser_open();
    if (strcmp(name, "kc_http_parser_write") == 0) return case_kc_http_parser_write();
    if (strcmp(name, "kc_http_parser_close") == 0) return case_kc_http_parser_close();
    if (strcmp(name, "kc_http_request") == 0) return case_kc_http_request();
    if (strcmp(name, "kc_http_response") == 0) return case_kc_http_response();
    if (strcmp(name, "kc_http_free") == 0) return case_kc_http_free();
    if (strcmp(name, "kc_http_strerror") == 0) return case_kc_http_strerror();
    if (strcmp(name, "kc_http_version") == 0) return case_kc_http_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(name, "kc_http_cli") == 0) return case_kc_http_cli();
#endif
    fprintf(stderr, "unknown case: %s\n", name);
    return 2;
}

int main(int argc, char **argv) {
    int rc = 0;

    test_program_path = argv[0];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") != 0) return test_named(argv[1]);

#ifdef __EMSCRIPTEN__
    test_case_total = 8;
#else
    test_case_total = 9;
#endif
    test_case_current = 0;
    test_run(&rc, case_kc_http_parser_open);
    test_run(&rc, case_kc_http_parser_write);
    test_run(&rc, case_kc_http_parser_close);
    test_run(&rc, case_kc_http_request);
    test_run(&rc, case_kc_http_response);
    test_run(&rc, case_kc_http_free);
    test_run(&rc, case_kc_http_strerror);
    test_run(&rc, case_kc_http_version);
#ifndef __EMSCRIPTEN__
    test_run(&rc, case_kc_http_cli);
#endif
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}
