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
    return 0;
}

/**
 * Tests kc_flow_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_open(void) {
    const char *name = "kc_flow_open";
    const char *detail = "open validates inputs and allocates context";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    fail = 0;
    fail += expect_int("open NULL out", KC_FLOW_ERROR, kc_flow_open(NULL, &opts));
    fail += expect_int("open NULL opts", KC_FLOW_ERROR, kc_flow_open(&ctx, NULL));
    fail += expect_int("open valid context", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_default(void) {
    const char *name = "kc_flow_options_default";
    const char *detail = "default options initialize correctly";
    kc_flow_options_t opts;

    opts = kc_flow_options_default();
    int fail = expect_int("default options are zero initialized", 0, (int)opts.reserved);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_load_env(void) {
    const char *name = "kc_flow_options_load_env";
    const char *detail = "options load from environment";
    kc_flow_options_t opts;
    int fail;

    opts = kc_flow_options_default();
    fail = 0;
    kc_flow_options_load_env(&opts);
    fail += expect_int("load_env preserves options", 0, (int)opts.reserved);
    kc_flow_options_load_env(NULL);
    kc_flow_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_options_free(void) {
    const char *name = "kc_flow_options_free";
    const char *detail = "options free releases resources";
    kc_flow_options_t opts;
    int fail;

    opts = kc_flow_options_default();
    kc_flow_options_free(&opts);
    kc_flow_options_free(NULL);
    fail = expect_int("options free preserves reserved field", 0, (int)opts.reserved);
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
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    fail = 0;
    fail += expect_int("open before close", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
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
    const char *detail = "stop sets flag on context";
    kc_flow_options_t opts;
    kc_flow_t *first;
    kc_flow_t *second;
    int fail;

    opts = kc_flow_options_default();
    first = NULL;
    second = NULL;
    fail = 0;
    fail += expect_int("stop NULL ctx", KC_FLOW_ERROR, kc_flow_stop(NULL));
    fail += expect_int("open first context", KC_FLOW_OK, kc_flow_open(&first, &opts));
    fail += expect_int("open second context", KC_FLOW_OK, kc_flow_open(&second, &opts));
    fail += expect_int("stop first context", KC_FLOW_OK, kc_flow_stop(first));
    fail += expect_int("stop first context again", KC_FLOW_OK, kc_flow_stop(first));
    fail += expect_int("stop second context", KC_FLOW_OK, kc_flow_stop(second));
    kc_flow_close(first);
    kc_flow_close(second);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_stop_requested.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_stop_requested(void) {
    const char *name = "kc_flow_stop_requested";
    const char *detail = "stop_requested reports flag state";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    fail = 0;
    fail += expect_int("stop_requested NULL ctx", 0, kc_flow_stop_requested(NULL));
    fail += expect_int("open for stop_requested", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_int("stop_requested not set", 0, kc_flow_stop_requested(ctx));
    fail += expect_int("stop sets flag", KC_FLOW_OK, kc_flow_stop(ctx));
    fail += expect_int("stop_requested set", 1, kc_flow_stop_requested(ctx));
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
    const char *detail = "set assigns variables to context";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("set NULL ctx", KC_FLOW_ERROR,
        kc_flow_set(NULL, "flow.link", "x"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for set", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_int("set NULL key", KC_FLOW_ERROR,
        kc_flow_set(ctx, NULL, "x"));
    fail += expect_int("set NULL value", KC_FLOW_ERROR,
        kc_flow_set(ctx, "flow.link", NULL));
    fail += expect_int("set invalid key", KC_FLOW_ERROR,
        kc_flow_set(ctx, "bad key", "x"));
    fail += expect_int("set overlay value", KC_FLOW_OK,
        kc_flow_set(ctx, "node.server.param.msg", "Hello"));
    fail += expect_int("set flow.link=server", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "server"));
    fail += expect_int("exec overlay after set", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    fail += expect_output_contains("set overlay output", out, out_size, "Hello");
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
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("unset NULL ctx", KC_FLOW_ERROR,
        kc_flow_unset(NULL, "flow.link"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for unset", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_int("unset NULL key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, NULL));
    fail += expect_int("unset invalid key", KC_FLOW_ERROR,
        kc_flow_unset(ctx, "bad key"));
    fail += expect_int("unset flow.link", KC_FLOW_OK,
        kc_flow_unset(ctx, "flow.link"));
    fail += expect_int("set flow.link=install", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.link", "install"));
    fail += expect_int("exec overlay after unset", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    fail += expect_output_contains("unset overlay output", out, out_size, "install");
    kc_flow_free(out);
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_set overlay replaces an existing key (regression).
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_overlay_replace(void) {
    const char *name = "kc_flow_set";
    const char *detail = "set overlay replaces an existing key";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_fixture(tmpdir, "replace.flow",
        "flow.testkey=first\n"
        "flow.link=root\n"
        "node.root.exec=echo key=<flow.testkey>\n") != 0) return 1;
    if (join_path(tmpdir, "replace.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for overlay replace", KC_FLOW_OK,
        kc_flow_open(&ctx, &opts));
    fail += expect_int("set overlay replaces flow.testkey", KC_FLOW_OK,
        kc_flow_set(ctx, "flow.testkey", "second"));
    fail += expect_int("exec after replace overlay", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    fail += expect_output_contains("overlay replace output", out, out_size, "key=second");
    kc_flow_free(out);
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
    const char *detail = "exec processes input through pipeline";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_int("exec NULL ctx", KC_FLOW_ERROR,
        kc_flow_exec(NULL, "x.flow", NULL, 0, &out, &out_size));
    fail += expect_int("open for exec", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_int("exec NULL path", KC_FLOW_ERROR,
        kc_flow_exec(ctx, NULL, NULL, 0, &out, &out_size));
    fail += expect_int("exec missing file", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, 0, &out, &out_size));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "fanout.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec fanout", KC_FLOW_OK,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    fail += expect_output_contains("fanout left output", out, out_size, "Hi Left");
    fail += expect_output_contains("fanout right output", out, out_size, "Hi Right");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "stdin.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec stdin", KC_FLOW_OK,
        kc_flow_exec(ctx, path, "Pipe Input", 10, &out, &out_size));
    fail += expect_output_contains("stdin output", out, out_size, "Pipe Input");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    if (join_path(tmpdir, "cycle.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("exec cycle fails", KC_FLOW_ERROR,
        kc_flow_exec(ctx, path, NULL, 0, &out, &out_size));
    kc_flow_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_flow_exec_entry.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_exec_entry(void) {
    const char *name = "kc_flow_exec_entry";
    const char *detail = "exec entry point runs pipeline step";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char tmpdir[320];
    char path[640];
    char *out;
    size_t out_size;
    int fail;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for exec_entry", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_int("exec_entry NULL ctx", KC_FLOW_ERROR,
        kc_flow_exec_entry(NULL, path, "default", NULL, 0, &out, &out_size));
    fail += expect_int("exec_entry empty entry", KC_FLOW_ERROR,
        kc_flow_exec_entry(ctx, path, "", NULL, 0, &out, &out_size));
    fail += expect_int("exec_entry default", KC_FLOW_OK,
        kc_flow_exec_entry(ctx, path, "default", NULL, 0, &out, &out_size));
    fail += expect_output_contains("entry default output", out, out_size, "default");
    kc_flow_free(out);
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
 * Tests kc_flow_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_strerror(void) {
    const char *name = "kc_flow_strerror";
    const char *detail = "strerror returns error details";
    kc_flow_options_t opts;
    kc_flow_t *ctx;
    char *out;
    size_t out_size;
    const char *err;
    int fail = 0;

    opts = kc_flow_options_default();
    ctx = NULL;
    out = NULL;
    out_size = 0;
    fail = 0;
    fail += expect_string("strerror NULL ctx", "unknown error", kc_flow_strerror(NULL));
    fail += expect_int("open for strerror", KC_FLOW_OK, kc_flow_open(&ctx, &opts));
    fail += expect_string("strerror fresh ctx", "unknown error", kc_flow_strerror(ctx));
    fail += expect_int("exec missing file for strerror", KC_FLOW_ERROR,
        kc_flow_exec(ctx, "/nonexistent/path.flow", NULL, 0, &out, &out_size));
    err = kc_flow_strerror(ctx);
    fail += expect_true("strerror after failure is non-NULL", err != NULL);
    fail += expect_true("strerror after failure is descriptive",
        err != NULL && strcmp(err, "unknown error") != 0);
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
    test_case_total = 15;
    test_case_current = 0;
    run_case(&rc, case_kc_flow_open);
    run_case(&rc, case_kc_flow_options_default);
    run_case(&rc, case_kc_flow_options_load_env);
    run_case(&rc, case_kc_flow_options_free);
    run_case(&rc, case_kc_flow_close);
    run_case(&rc, case_kc_flow_stop);
    run_case(&rc, case_kc_flow_stop_requested);
    run_case(&rc, case_kc_flow_set);
    run_case(&rc, case_kc_flow_unset);
    run_case(&rc, case_kc_flow_overlay_replace);
    run_case(&rc, case_kc_flow_exec);
    run_case(&rc, case_kc_flow_exec_entry);
    run_case(&rc, case_kc_flow_free);
    run_case(&rc, case_kc_flow_version);
    run_case(&rc, case_kc_flow_strerror);
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
    if (strcmp(argv[1], "kc_flow_options_default") == 0) return case_kc_flow_options_default();
    if (strcmp(argv[1], "kc_flow_options_load_env") == 0) return case_kc_flow_options_load_env();
    if (strcmp(argv[1], "kc_flow_options_free") == 0) return case_kc_flow_options_free();
    if (strcmp(argv[1], "kc_flow_close") == 0) return case_kc_flow_close();
    if (strcmp(argv[1], "kc_flow_stop") == 0) return case_kc_flow_stop();
    if (strcmp(argv[1], "kc_flow_stop_requested") == 0) return case_kc_flow_stop_requested();
    if (strcmp(argv[1], "kc_flow_set") == 0) return case_kc_flow_set();
    if (strcmp(argv[1], "kc_flow_unset") == 0) return case_kc_flow_unset();
    if (strcmp(argv[1], "kc_flow_overlay_replace") == 0) return case_kc_flow_overlay_replace();
    if (strcmp(argv[1], "kc_flow_exec") == 0) return case_kc_flow_exec();
    if (strcmp(argv[1], "kc_flow_exec_entry") == 0) return case_kc_flow_exec_entry();
    if (strcmp(argv[1], "kc_flow_free") == 0) return case_kc_flow_free();
    if (strcmp(argv[1], "kc_flow_version") == 0) return case_kc_flow_version();
    if (strcmp(argv[1], "kc_flow_strerror") == 0) return case_kc_flow_strerror();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
