/**
 * test.c - libflow public API contract tests.
 * Summary: Validates each exported flow function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libflow.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Public API function under test.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one integer result.
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
 * Verifies one boolean result.
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
 * Verifies one string result.
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
 * Verifies that output contains a substring anywhere in the buffer.
 * @param name Check name.
 * @param output Output buffer.
 * @param output_size Output size.
 * @param needle Expected substring.
 * @return 0 on success, 1 on failure.
 */
static int expect_output_contains(const char *name,
const char *output, size_t output_size, const char *needle)
{
    size_t needle_len;
    size_t i;

    if (output == NULL || output_size == 0) {
        printf("[FAIL] %s: expected output containing '%s', got empty\n",
            name, needle);
        return 1;
    }
    needle_len = strlen(needle);
    if (output_size < needle_len) {
        printf("[FAIL] %s: expected output containing '%s', got '%.*s'\n",
            name, needle, (int)output_size, output);
        return 1;
    }
    for (i = 0; i + needle_len <= output_size; i++) {
        if (memcmp(output + i, needle, needle_len) == 0) {
            return 0;
        }
    }
    printf("[FAIL] %s: expected output containing '%s', got '%.*s'\n",
        name, needle, (int)output_size, output);
    return 1;
}

/**
 * Creates one temporary directory for generated flow fixtures.
 * @param out Buffer to receive the path.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int make_temp_dir(char *out, size_t cap) {
    const char *base;
    int r;

#ifdef _WIN32
    base = getenv("TEMP");
    if (base == NULL || *base == '\0') base = ".";
    {
        int i;
        for (i = 0; i < 100; i++) {
            r = snprintf(out, cap, "%s/flow-test-%ld-%d", base,
                (long)_getpid(), i);
            if (r < 0 || (size_t)r >= cap) return 1;
            if (_mkdir(out) == 0) return 0;
            if (errno != EEXIST) return 1;
        }
    }
    return 1;
#else
    base = getenv("TMPDIR");
    if (base == NULL || *base == '\0') base = "/tmp";
    r = snprintf(out, cap, "%s/flow-test-XXXXXX", base);
    if (r < 0 || (size_t)r >= cap) return 1;
    if (mkdtemp(out) == NULL) return 1;
    return 0;
#endif
}

/**
 * Writes one fixture file into a directory.
 * @param dir Directory path.
 * @param name File name.
 * @param content File content.
 * @return 0 on success, 1 on failure.
 */
static int write_fixture(const char *dir, const char *name, const char *content) {
    char path[512];
    FILE *f;
    size_t len;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 1;
    f = fopen(path, "w");
    if (f == NULL) return 1;
    len = strlen(content);
    if (fwrite(content, 1, len, f) != len) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/**
 * Joins a directory and file name into a path buffer.
 * @param dir Directory path.
 * @param name File name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int join_path(const char *dir, const char *name, char *out, size_t cap) {
    int r;

    r = snprintf(out, cap, "%s/%s", dir, name);
    return (r < 0 || (size_t)r >= cap) ? 1 : 0;
}

/**
 * Writes generated runtime fixtures.
 * @param dir Temporary directory path.
 * @return 0 on success, 1 on failure.
 */
static int write_all_fixtures(const char *dir) {
#ifdef _WIN32
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=more\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=echo default\n"
        "node.server.exec=echo <node.param.msg>\n"
        "node.install.exec=echo install\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo <flow.param.greeting> Left\n"
        "node.right.exec=echo <flow.param.greeting> Right\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#else
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=cat\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=printf \"%s\" \"default\"\n"
        "node.server.exec=printf \"%s\" \"<node.param.msg>\"\n"
        "node.install.exec=printf \"%s\" \"install\"\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=printf \"%s\" \"<flow.param.greeting> Left\"\n"
        "node.right.exec=printf \"%s\" \"<flow.param.greeting> Right\"\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#endif
    if (write_fixture(dir, "empty.flow",
        "flow.link=pass\n"
        "node.pass.exec=\n") != 0)
        return 1;
    if (write_fixture(dir, "size.flow",
        "flow.id=sizefan\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo Left\n"
        "node.right.exec=echo Right\n") != 0)
        return 1;
    return 0;
}

/**
 * Tests kc_flow_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_open(void) {
    const char *name = "kc_flow_open";
    const char *detail = "open validates inputs and sets context pointer";
    kc_flow_t *ctx;
    kc_flow_t *first;
    int fail;

    ctx = NULL;
    first = NULL;
    fail = 0;
    fail += expect_int("open NULL out", KC_FLOW_ERROR, kc_flow_open(NULL));
    fail += expect_int("open valid context", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_true("open sets out", ctx != NULL);
    fail += expect_string("fresh ctx get_error", "", kc_flow_get_error(ctx));
    first = ctx;
    fail += expect_int("open overwrites same pointer", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_true("open replaces context", ctx != NULL && ctx != first);
    kc_flow_close(ctx);
    kc_flow_close(first);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_close(void) {
    const char *name = "kc_flow_close";
    const char *detail = "close releases context";
    kc_flow_t *ctx;
    int fail;

    ctx = NULL;
    fail = 0;
    fail += expect_int("open before close", KC_FLOW_OK, kc_flow_open(&ctx));
    kc_flow_close(NULL);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_stop(void) {
    const char *name = "kc_flow_stop";
    const char *detail = "stop is idempotent and cooperative; exec on a stopped context fails";
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    void *out;
    size_t out_size;
    int fail;

    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("stop NULL ctx", KC_FLOW_ERROR, kc_flow_stop(NULL));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for stop", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_int("stop context", KC_FLOW_OK, kc_flow_stop(ctx));
    fail += expect_int("stop context again", KC_FLOW_OK, kc_flow_stop(ctx));
    fail += expect_int("exec on stopped context", KC_FLOW_ESTOP,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("stopped exec clears output", out == NULL && out_size == 0);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_set(void) {
    const char *name = "kc_flow_set";
    const char *detail = "set validates inputs and applies ordered overlays";
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char replace_path[640];
    void *out;
    size_t out_size;
    int fail;

    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("set NULL ctx", KC_FLOW_ERROR,
        kc_flow_set(NULL, "flow.link", "x"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (write_fixture(tmpdir, "replace.flow",
        "flow.testkey=first\n"
        "flow.link=root\n"
        "node.root.exec=echo key=<flow.testkey>\n") != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    if (join_path(tmpdir, "replace.flow", replace_path, sizeof(replace_path)) != 0) return 1;
    fail += expect_int("open for set", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_int("set NULL key", KC_FLOW_ERROR,
        kc_flow_set(ctx, NULL, "x"));
    fail += expect_int("set NULL value", KC_FLOW_ERROR,
        kc_flow_set(ctx, "flow.link", NULL));
    fail += expect_int("set invalid key", KC_FLOW_ERROR,
        kc_flow_set(ctx, "bad key", "x"));
    fail += expect_int("set replaces existing value", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.testkey", "second"));
    fail += expect_int("exec after set replace", KC_FLOW_OK,
        kc_flow_exec(ctx, replace_path, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("set replace output", (const char *)out,
        out_size, "key=second");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    fail += expect_int("set overlay value", KC_FLOW_OK,
        kc_flow_set(ctx, "node.server.param.msg", "Hello"));
    fail += expect_int("set flow.link=server", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "server"));
    fail += expect_int("exec overlay after set", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("set overlay output", (const char *)out,
        out_size, "Hello");
    kc_flow_free(out);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_unset.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_unset(void) {
    const char *name = "kc_flow_unset";
    const char *detail = "unset removes variables from context";
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    void *out;
    size_t out_size;
    int fail;

    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("unset NULL ctx", KC_FLOW_ERROR,
        kc_flow_unset(NULL, "flow.link"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for unset", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_int("unset NULL key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, NULL));
    fail += expect_int("unset invalid key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, "bad key"));
    fail += expect_int("unset flow.link", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    fail += expect_int("set flow.link=install", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "install"));
    fail += expect_int("exec overlay after unset", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("unset overlay output", (const char *)out,
        out_size, "install");
    kc_flow_free(out);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ordered overlay semantics through the public API.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_overlay_order(void) {
    const char *name = "kc_flow_overlay_order";
    const char *detail = "ordered overlays apply in operation order; unset-then-set differs from set-then-unset";
    kc_flow_t *ctx;
    kc_flow_t *other;
    char tmpdir[320];
    char path[640];
    void *out;
    size_t out_size;
    int fail;

    ctx = NULL;
    other = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for overlay order A", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_int("overlay A unset flow.link", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    fail += expect_int("overlay A set server param", KC_FLOW_OK,
        kc_flow_set(ctx, "node.server.param.msg", "Hello"));
    fail += expect_int("overlay A set flow.link=server", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "server"));
    fail += expect_int("exec overlay order A", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("overlay order A output", (const char *)out,
        out_size, "Hello");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    fail += expect_int("open for overlay order B", KC_FLOW_OK, kc_flow_open(&other));
    fail += expect_int("overlay B set server param", KC_FLOW_OK,
        kc_flow_set(other, "node.server.param.msg", "Hello"));
    fail += expect_int("overlay B set flow.link=server", KC_FLOW_OK,
        kc_flow_set(other, "flow.link", "server"));
    fail += expect_int("overlay B unset flow.link", KC_FLOW_OK,
        kc_flow_unset(other, "flow.link"));
    fail += expect_int("exec overlay order B", KC_FLOW_OK,
        kc_flow_exec(other, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("overlay order B output empty", out == NULL && out_size == 0);
    kc_flow_close(other);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_exec(void) {
    const char *name = "kc_flow_exec";
    const char *detail = "exec executes declared and explicit entries with owned, size-accurate output";
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    void *out;
    size_t out_size;
    int fail;

    ctx = NULL;
    out = (void *)"sentinel";
    out_size = 123;
    fail = 0;
    fail += expect_int("exec NULL ctx", KC_FLOW_ERROR,
        kc_flow_exec(NULL, "x.flow", NULL, NULL, 0, &out, &out_size));
    fail += expect_true("exec NULL ctx clears output", out == NULL && out_size == 0);
    fail += expect_int("open for exec", KC_FLOW_OK, kc_flow_open(&ctx));
    out = (void *)"sentinel";
    out_size = 123;
    fail += expect_int("exec NULL path", KC_FLOW_ERROR,
        kc_flow_exec(ctx, NULL, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("exec NULL path clears output", out == NULL && out_size == 0);
    fail += expect_int("exec NULL out_size", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "x.flow", NULL, NULL, 0, &out, NULL));
    fail += expect_int("exec missing file", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, NULL, 0, &out, &out_size));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "fanout.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec declared fanout", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("fanout left output", (const char *)out,
        out_size, "Hi Left");
    fail += expect_output_contains("fanout right output", (const char *)out,
        out_size, "Hi Right");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec explicit entry", KC_FLOW_OK,
        kc_flow_exec(ctx, path, "default", NULL, 0, &out, &out_size));
    fail += expect_output_contains("explicit entry output", (const char *)out,
        out_size, "default");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    fail += expect_int("exec empty entry", KC_FLOW_ERROR,
        kc_flow_exec(ctx, path, "", NULL, 0, &out, &out_size));
    fail += expect_true("exec empty entry clears output", out == NULL && out_size == 0);
    fail += expect_int("exec input without buffer", KC_FLOW_ERROR,
        kc_flow_exec(ctx, path, NULL, NULL, 5, &out, &out_size));
    fail += expect_true("exec input error clears output", out == NULL && out_size == 0);
    if (join_path(tmpdir, "stdin.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec stdin", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, "Pipe Input", 10, &out, &out_size));
    fail += expect_output_contains("stdin output", (const char *)out,
        out_size, "Pipe Input");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "empty.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec empty output", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("empty output is NULL/0", out == NULL && out_size == 0);
    if (join_path(tmpdir, "size.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec size fanout", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("size fanout exact byte count", out != NULL && out_size == 11);
    fail += expect_true("size fanout payload bytes", out != NULL && out_size == 11 &&
        memcmp(out, "Left\nRight\n", 11) == 0);
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "cycle.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec cycle fails", KC_FLOW_ERROR,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_free(void) {
    const char *name = "kc_flow_free";
    const char *detail = "free releases allocated output";
    char *output;
    int fail = 0;

    output = (char *)malloc(8);
    if (output == NULL) return 1;
    memcpy(output, "owned", 6);
    kc_flow_free(output);
    kc_flow_free(NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_version(void) {
    const char *name = "kc_flow_version";
    const char *detail = "version returns build timestamp";
    int fail;

    fail = expect_true("version is non-zero", kc_flow_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_get_error(void) {
    const char *name = "kc_flow_get_error";
    const char *detail = "error is descriptive after failure and cleared by later success";
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    void *out;
    size_t out_size;
    const char *err;
    int fail;

    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_true("get_error NULL is NULL", kc_flow_get_error(NULL) == NULL);
    fail += expect_int("open for get_error", KC_FLOW_OK, kc_flow_open(&ctx));
    fail += expect_string("get_error fresh ctx", "", kc_flow_get_error(ctx));
    fail += expect_int("exec missing file for get_error", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, NULL, 0, &out, &out_size));
    err = kc_flow_get_error(ctx);
    fail += expect_true("get_error after failure is non-NULL", err != NULL);
    fail += expect_true("get_error after failure is descriptive",
        err != NULL && err[0] != '\0');
    fail += expect_int("set after failure", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.tmp", "x"));
    fail += expect_string("set clears stale error", "", kc_flow_get_error(ctx));
    fail += expect_int("exec missing file again", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, NULL, 0, &out, &out_size));
    err = kc_flow_get_error(ctx);
    fail += expect_true("get_error refreshed after failure",
        err != NULL && err[0] != '\0');
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "empty.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec after failure", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, NULL, 0, &out, &out_size));
    fail += expect_string("exec clears stale error", "", kc_flow_get_error(ctx));
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 10;
    test_case_current = 0;
    run_case(&rc, case_kc_flow_open);
    run_case(&rc, case_kc_flow_close);
    run_case(&rc, case_kc_flow_stop);
    run_case(&rc, case_kc_flow_set);
    run_case(&rc, case_kc_flow_unset);
    run_case(&rc, case_kc_flow_overlay_order);
    run_case(&rc, case_kc_flow_exec);
    run_case(&rc, case_kc_flow_free);
    run_case(&rc, case_kc_flow_version);
    run_case(&rc, case_kc_flow_get_error);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one public API contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_flow_open") == 0) return case_kc_flow_open();
    if (strcmp(argv[1], "kc_flow_close") == 0) return case_kc_flow_close();
    if (strcmp(argv[1], "kc_flow_stop") == 0) return case_kc_flow_stop();
    if (strcmp(argv[1], "kc_flow_set") == 0) return case_kc_flow_set();
    if (strcmp(argv[1], "kc_flow_unset") == 0) return case_kc_flow_unset();
    if (strcmp(argv[1], "kc_flow_overlay_order") == 0) return case_kc_flow_overlay_order();
    if (strcmp(argv[1], "kc_flow_exec") == 0) return case_kc_flow_exec();
    if (strcmp(argv[1], "kc_flow_free") == 0) return case_kc_flow_free();
    if (strcmp(argv[1], "kc_flow_version") == 0) return case_kc_flow_version();
    if (strcmp(argv[1], "kc_flow_get_error") == 0) return case_kc_flow_get_error();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
