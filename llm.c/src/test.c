/**
 * test.c - libllm public API tests.
 * Summary: Tests each public libllm function through one test binary invocation.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libllm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef KC_LLM_TEST_MODEL
#define KC_LLM_TEST_MODEL ""
#endif

#ifndef KC_LLM_TEST_LORA
#define KC_LLM_TEST_LORA ""
#endif

#ifndef KC_LLM_BUILD_VERSION
#define KC_LLM_BUILD_VERSION 0
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
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

static size_t generate_output_len = 0;

/**
 * expect_int - Compare two integers and report the result.
 * @param name Test name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on match, 1 on mismatch.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * expect_true - Check a condition and report the result.
 * @param name Test name.
 * @param condition Condition to check.
 * @return 0 if true, 1 if false.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * expect_string - Compare two strings and report the result.
 * @param name Test name.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on match, 1 on mismatch.
 */
static int expect_string(const char *name, const char *expected,
    const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name,
            expected, actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * expect_u64 - Compare two unsigned 64-bit integers and report the result.
 * @param name Test name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on match, 1 on mismatch.
 */
static int expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %llu, got %llu\n", name,
            (unsigned long long)expected, (unsigned long long)actual);
        return 1;
    }
    return 0;
}

/**
 * expect_i64 - Compare two signed 64-bit integers and report the result.
 * @param name Test name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on match, 1 on mismatch.
 */
static int expect_i64(const char *name, int64_t expected, int64_t actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %lld, got %lld\n", name,
            (long long)expected, (long long)actual);
        return 1;
    }
    return 0;
}

/**
 * copy_string - Duplicate a string using malloc.
 * @param text Source string.
 * @return Allocated copy, or NULL on failure.
 */
static char *copy_string(const char *text) {
    char *copy;
    size_t length;

    length = strlen(text) + 1;
    copy = (char *)malloc(length);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    return copy;
}

/**
 * is_gguf_file - Check whether a file begins with the GGUF magic bytes.
 * @param path File path.
 * @return 1 if the file is a GGUF, 0 otherwise.
 */
static int is_gguf_file(const char *path) {
    FILE *f;
    unsigned char magic[4];
    size_t n;

    if (!path || !path[0]) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(magic, 1, sizeof(magic), f);
    fclose(f);
    if (n != sizeof(magic)) return 0;
    return magic[0] == 'G' && magic[1] == 'G' && magic[2] == 'U' &&
        magic[3] == 'F';
}

/**
 * set_env_value - Set or unset an environment variable.
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
 * collect_output - Accumulate generated output length.
 * @param data Output bytes.
 * @param len Number of bytes.
 * @param user User pointer (unused).
 * @return 0.
 */
static int collect_output(const char *data, size_t len, void *user) {
    (void)user;
    generate_output_len += len;
    return 0;
}

/**
 * open_context - Open a context using the test model.
 * @param out Pointer to receive the context.
 * @return 0 on success, 1 on failure.
 */
static int open_context(kc_llm_t **out) {
    kc_llm_options_t opts;

    opts = kc_llm_options_default();
    opts.model_path = copy_string(KC_LLM_TEST_MODEL);
    opts.predict = 4;
    if (kc_llm_open(out, &opts) != KC_LLM_OK) {
        free(opts.model_path);
        return 1;
    }
    free(opts.model_path);
    return 0;
}

/**
 * open_model_context - Open a model-only context using the test model.
 * @param out Pointer to receive the context.
 * @return 0 on success, 1 on failure.
 */
static int open_model_context(kc_llm_t **out) {
    kc_llm_options_t opts;

    opts = kc_llm_options_default();
    opts.model_path = copy_string(KC_LLM_TEST_MODEL);
    if (kc_llm_open_model(out, &opts) != KC_LLM_OK) {
        free(opts.model_path);
        return 1;
    }
    free(opts.model_path);
    return 0;
}

/**
 * case_options_default - Verify default option values.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_options_default(void) {
    const char *name = "kc_llm_options_default";
    const char *detail = "initializes correctly";
    kc_llm_options_t opts;
    int rc;

    opts = kc_llm_options_default();
    rc = 0;
    rc += expect_true("kc_llm_options_default sets model_path NULL",
        opts.model_path == NULL);
    rc += expect_int("kc_llm_options_default enables thinking", 1, opts.think);
    rc += expect_int("kc_llm_options_default sets mtp tokens", 4, opts.mtp_tokens);
    rc += expect_int("kc_llm_options_default keeps draft on CPU", 0, opts.mtp_gpu_layers);
    rc += expect_int("kc_llm_options_default sets flash attention auto", -1, opts.fattn);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_options_load_env - Verify environment variable loading.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_options_load_env(void) {
    const char *name = "kc_llm_options_load_env";
    const char *detail = "loads from environment";
    kc_llm_options_t opts;
    int rc;

    rc = 0;
    opts = kc_llm_options_default();
    set_env_value("KC_LLM_MODEL", NULL);
    kc_llm_options_load_env(&opts);
    rc += expect_true("load_env leaves model_path NULL when unset",
        opts.model_path == NULL);
    set_env_value("KC_LLM_MODEL", "model.gguf");
    kc_llm_options_load_env(&opts);
    rc += expect_string("load_env reads KC_LLM_MODEL", "model.gguf",
        opts.model_path);
    set_env_value("KC_LLM_MODEL", NULL);
    kc_llm_options_load_env(NULL);
    rc += expect_true("load_env accepts NULL", 1);
    kc_llm_options_free(&opts);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_options_free - Verify option cleanup and idempotency.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_options_free(void) {
    const char *name = "kc_llm_options_free";
    const char *detail = "clears resources";
    kc_llm_options_t opts;
    int rc;

    rc = 0;
    opts = kc_llm_options_default();
    opts.model_path = copy_string("owned");
    rc += expect_true("copy option model_path", opts.model_path != NULL);
    kc_llm_options_free(&opts);
    rc += expect_true("options_free clears model_path", opts.model_path == NULL);
    kc_llm_options_free(&opts);
    rc += expect_true("options_free is idempotent", opts.model_path == NULL);
    kc_llm_options_free(NULL);
    rc += expect_true("options_free accepts NULL", 1);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_version - Verify the configured build version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_version(void) {
    const char *name = "kc_llm_version";
    const char *detail = "returns version";
    int rc;
#ifdef KC_LLM_TEST_EXTERNAL_ARTIFACTS
    rc = expect_true("kc_llm_version returns build value",
        kc_llm_version() != 0U);
#else
    rc = expect_u64("kc_llm_version returns build value",
        (uint64_t)KC_LLM_BUILD_VERSION, kc_llm_version());
#endif
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_info_static - Verify static model inspection fields.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_info_static(void) {
    const char *name = "kc_llm_info_static";
    const char *detail = "returns static fields";
    kc_llm_t *ctx;
    kc_llm_info_field_t fields[] = {
        KC_LLM_INFO_ARCHITECTURE,
        KC_LLM_INFO_NAME,
        KC_LLM_INFO_PARAMETERS,
        KC_LLM_INFO_SIZE,
        KC_LLM_INFO_VOCABULARY,
        KC_LLM_INFO_CONTEXT_MAX,
        KC_LLM_INFO_EMBEDDING_SIZE,
        KC_LLM_INFO_LAYERS,
        KC_LLM_INFO_HEADS,
        KC_LLM_INFO_KV_HEADS,
    };
    kc_llm_info_t values[sizeof(fields) / sizeof(fields[0])];
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] info-static (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_model_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("info query static fields", KC_LLM_OK,
        kc_llm_info_query(ctx, fields, sizeof(fields) / sizeof(fields[0]), NULL,
            values));
    if (rc == 0) {
        rc += expect_true("architecture type string",
            values[0].type == KC_LLM_INFO_VALUE_STRING);
        rc += expect_true("architecture present or unavailable",
            values[0].type == KC_LLM_INFO_VALUE_STRING ||
            values[0].type == KC_LLM_INFO_VALUE_NONE);
        rc += expect_true("name present or unavailable",
            values[1].type == KC_LLM_INFO_VALUE_STRING ||
            values[1].type == KC_LLM_INFO_VALUE_NONE);
        rc += expect_true("parameters type u64",
            values[2].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("size type u64",
            values[3].type == KC_LLM_INFO_VALUE_U64 ||
            values[3].type == KC_LLM_INFO_VALUE_NONE);
        rc += expect_true("vocabulary type u64",
            values[4].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("context max type u64",
            values[5].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("embedding size type u64",
            values[6].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("layers type u64",
            values[7].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("heads type u64",
            values[8].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("kv-heads type u64",
            values[9].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_true("parameters non-zero", values[2].value.u64 > 0);
        rc += expect_true("vocabulary non-zero", values[4].value.u64 > 0);
        rc += expect_true("context max non-zero", values[5].value.u64 > 0);
        rc += expect_true("embedding size non-zero", values[6].value.u64 > 0);
        rc += expect_true("layers non-zero", values[7].value.u64 > 0);
        rc += expect_true("heads non-zero", values[8].value.u64 > 0);
    }
    kc_llm_close(ctx);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_info_request_required - Verify input-dependent fields reject
 * missing requests.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_info_request_required(void) {
    const char *name = "kc_llm_info_request_required";
    const char *detail = "input-dependent fields require request";
    kc_llm_t *ctx;
    kc_llm_info_field_t field = KC_LLM_INFO_INPUT_TOKENS;
    kc_llm_info_t value;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] info-request-required (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_model_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("info input-tokens requires request", KC_LLM_ERROR,
        kc_llm_info_query(ctx, &field, 1, NULL, &value));
    kc_llm_close(ctx);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_info_input_tokens - Verify prepared input token accounting.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_info_input_tokens(void) {
    const char *name = "kc_llm_info_input_tokens";
    const char *detail = "input tokens accounting";
    kc_llm_t *ctx;
    kc_llm_info_field_t fields[] = {
        KC_LLM_INFO_INPUT_TOKENS,
        KC_LLM_INFO_INPUT_TOKENS,
    };
    kc_llm_info_request_t req_user = {"user", "hello"};
    kc_llm_info_request_t req_system = {"system", "hello"};
    kc_llm_info_t values[2];
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] info-input-tokens (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_model_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("info input-tokens user", KC_LLM_OK,
        kc_llm_info_query(ctx, fields, 1, &req_user, values));
    if (rc == 0) {
        rc += expect_true("input-tokens type i64",
            values[0].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("input-tokens positive", values[0].value.i64 > 0);
    }
    rc += expect_int("info input-tokens system role", KC_LLM_OK,
        kc_llm_info_query(ctx, fields + 1, 1, &req_system, values + 1));
    if (rc == 0) {
        rc += expect_true("system input-tokens type i64",
            values[1].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("system input-tokens positive", values[1].value.i64 > 0);
    }
    kc_llm_close(ctx);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_info_runtime - Verify runtime context inspection and
 * projections.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_info_runtime(void) {
    const char *name = "kc_llm_info_runtime";
    const char *detail = "runtime fields";
    kc_llm_t *ctx;
    kc_llm_info_field_t fields[] = {
        KC_LLM_INFO_CONTEXT_USED,
        KC_LLM_INFO_CONTEXT_SIZE,
        KC_LLM_INFO_CONTEXT_FREE,
        KC_LLM_INFO_CONTEXT_AFTER,
        KC_LLM_INFO_INPUT_TOKENS,
        KC_LLM_INFO_CONTEXT_MAX,
    };
    kc_llm_info_request_t request = {"user", "hello"};
    kc_llm_info_t values[sizeof(fields) / sizeof(fields[0])];
    int64_t expected_after;
    int64_t expected_free;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] info-runtime (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("runtime info query", KC_LLM_OK,
        kc_llm_info_query(ctx, fields, sizeof(fields) / sizeof(fields[0]),
            &request, values));
    if (rc == 0) {
        expected_free = values[1].value.i64 - values[0].value.i64;
        expected_after = values[0].value.i64 + values[4].value.i64;
        rc += expect_true("context-used type i64",
            values[0].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("context-size type i64",
            values[1].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("context-free type i64",
            values[2].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("context-after type i64",
            values[3].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("input-tokens type i64",
            values[4].type == KC_LLM_INFO_VALUE_I64);
        rc += expect_true("context-max type u64",
            values[5].type == KC_LLM_INFO_VALUE_U64);
        rc += expect_i64("context-free matches derived value", expected_free,
            values[2].value.i64);
        rc += expect_i64("context-after matches derived value", expected_after,
            values[3].value.i64);
        rc += expect_true("context-size positive", values[1].value.i64 > 0);
        rc += expect_true("context-max positive", values[5].value.u64 > 0);
        rc += expect_true("context-size distinct from model max allowed",
            values[1].value.i64 <= (int64_t)values[5].value.u64);
    }
    if (rc == 0) {
        rc += expect_int("generate before context-used recheck", KC_LLM_OK,
            kc_llm_generate(ctx, "hello", collect_output, NULL));
    }
    if (rc == 0) {
        kc_llm_info_field_t field = KC_LLM_INFO_CONTEXT_USED;
        kc_llm_info_t value;
        rc += expect_int("context-used after generation", KC_LLM_OK,
            kc_llm_info_query(ctx, &field, 1, NULL, &value));
        if (rc == 0) {
rc += expect_true("context-used grows after generation",
            value.type == KC_LLM_INFO_VALUE_I64 && value.value.i64 > 0);
        }
    }
    kc_llm_close(ctx);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_open_close - Verify context open and close behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_open_close(void) {
    const char *name = "kc_llm_open_close";
    const char *detail = "open and close context";
    kc_llm_options_t opts;
    kc_llm_t *ctx;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    ctx = NULL;
    opts = kc_llm_options_default();
    rc += expect_int("open rejects NULL out", KC_LLM_ERROR,
        kc_llm_open(NULL, &opts));
    rc += expect_int("open rejects NULL opts", KC_LLM_ERROR,
        kc_llm_open(&ctx, NULL));
    rc += expect_true("open error clears output", ctx == NULL);

    if (!model || !model[0]) {
        printf("[SKIP] open/close with model"
            " (KC_LLM_TEST_MODEL not set)\n");
        case_result(rc != 0, name, detail);
        return rc == 0 ? 0 : 1;
    }

    opts.model_path = copy_string(model);
    opts.predict = 4;
    rc += expect_int("open creates context", KC_LLM_OK,
        kc_llm_open(&ctx, &opts));
    rc += expect_true("open sets output", ctx != NULL);
    free(opts.model_path);
    opts.model_path = NULL;
    kc_llm_close(ctx);
    rc += expect_true("close releases context", 1);
    kc_llm_options_free(&opts);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_generate - Verify text generation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_generate(void) {
    const char *name = "kc_llm_generate";
    const char *detail = "generates output";
    kc_llm_t *ctx;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] generate"
            " (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    generate_output_len = 0;
    rc += expect_int("generate rejects NULL ctx", KC_LLM_ERROR,
        kc_llm_generate(NULL, "hello", collect_output, NULL));
    rc += expect_int("generate rejects NULL prompt", KC_LLM_ERROR,
        kc_llm_generate(ctx, NULL, collect_output, NULL));
    rc += expect_int("generate produces output", KC_LLM_OK,
        kc_llm_generate(ctx, "hello", collect_output, NULL));
    rc += expect_true("generate output is non-empty",
        generate_output_len > 0);
kc_llm_close(ctx);
    rc += expect_true("close context", 1);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_generate_role - Verify role-aware generation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_generate_role(void) {
    const char *name = "kc_llm_generate_role";
    const char *detail = "generates with role";
    kc_llm_t *ctx;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;

    rc = 0;
    if (!model || !model[0]) {
        printf("[SKIP] generate-role"
            " (KC_LLM_TEST_MODEL not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (open_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    generate_output_len = 0;
    rc += expect_int("generate_role rejects NULL ctx", KC_LLM_ERROR,
        kc_llm_generate_role(NULL, "user", "hello", collect_output, NULL));
    rc += expect_int("generate_role rejects NULL content", KC_LLM_ERROR,
        kc_llm_generate_role(ctx, "user", NULL, collect_output, NULL));
    rc += expect_int("generate_role rejects invalid role", KC_LLM_ERROR,
        kc_llm_generate_role(ctx, "operator", "hello", collect_output, NULL));
    rc += expect_int("generate_role accepts explicit user role", KC_LLM_OK,
        kc_llm_generate_role(ctx, "user", "hello", collect_output, NULL));
    rc += expect_true("generate_role user output is non-empty",
        generate_output_len > 0);
    generate_output_len = 0;
    rc += expect_int("generate_role accepts NULL role as default user", KC_LLM_OK,
        kc_llm_generate_role(ctx, NULL, "hello", collect_output, NULL));
    rc += expect_true("generate_role default output is non-empty",
        generate_output_len > 0);
    kc_llm_close(ctx);
    rc += expect_true("close context", 1);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_lora - Verify LoRA loading when a valid GGUF LoRA is present.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_lora(void) {
    const char *name = "kc_llm_lora";
    const char *detail = "LoRA handling";
    kc_llm_options_t opts;
    kc_llm_t *ctx;
    int rc;
    const char *model = KC_LLM_TEST_MODEL;
    const char *lora = KC_LLM_TEST_LORA;

    rc = 0;
    if (!model || !model[0] || !lora || !lora[0]) {
        printf("[SKIP] lora"
            " (KC_LLM_TEST_MODEL or KC_LLM_TEST_LORA not set)\n");
        case_result(0, name, detail);
        return 0;
    }
    if (!is_gguf_file(lora)) {
        printf("[SKIP] lora"
            " (KC_LLM_TEST_LORA is not a GGUF LoRA file)\n");
        case_result(0, name, detail);
        return 0;
    }

    opts = kc_llm_options_default();
    opts.model_path = copy_string(model);
    opts.predict = 4;
    opts.lora_paths = (char **)malloc(sizeof(char *));
    opts.lora_scales = (float *)malloc(sizeof(float));
    if (!opts.lora_paths || !opts.lora_scales) {
        kc_llm_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    opts.lora_paths[0] = copy_string(lora);
    opts.lora_scales[0] = 1.0f;
    opts.n_loras = 1;

    rc += expect_int("open with LoRA", KC_LLM_OK, kc_llm_open(&ctx, &opts));
    if (rc == 0) {
        generate_output_len = 0;
        rc += expect_int("generate with LoRA", KC_LLM_OK,
            kc_llm_generate(ctx, "hello", collect_output, NULL));
        rc += expect_true("generate with LoRA output is non-empty",
            generate_output_len > 0);
        kc_llm_close(ctx);
        rc += expect_true("close context", 1);
    }
    kc_llm_options_free(&opts);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_stop - Verify stop request behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_stop(void) {
    const char *name = "kc_llm_stop";
    const char *detail = "stop request";
    kc_llm_t *ctx;
    int rc;

    rc = 0;
    rc += expect_int("stop rejects NULL", KC_LLM_ERROR, kc_llm_stop(NULL));
    rc += expect_int("stop_requested handles NULL", 0, kc_llm_stop_requested(NULL));
    if (open_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("stop_requested initially false", 0, kc_llm_stop_requested(ctx));
    rc += expect_int("stop context succeeds", KC_LLM_OK, kc_llm_stop(ctx));
    rc += expect_int("stop is idempotent", KC_LLM_OK, kc_llm_stop(ctx));
    rc += expect_int("stop_requested becomes true", 1, kc_llm_stop_requested(ctx));
    kc_llm_close(ctx);
    rc += expect_true("close context", 1);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * case_memory_clear - Verify KV cache reset behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_llm_memory_clear(void) {
    const char *name = "kc_llm_memory_clear";
    const char *detail = "memory clear";
    kc_llm_t *ctx;
    int rc;

    rc = 0;
    rc += expect_int("memory_clear rejects NULL", KC_LLM_ERROR,
        kc_llm_memory_clear(NULL));
    if (open_context(&ctx) != 0) { case_result(1, name, detail); return 1; }
    rc += expect_int("memory_clear succeeds", KC_LLM_OK,
        kc_llm_memory_clear(ctx));
    rc += expect_int("memory_clear is idempotent", KC_LLM_OK,
        kc_llm_memory_clear(ctx));
    kc_llm_close(ctx);
    rc += expect_true("close context", 1);
    case_result(rc != 0, name, detail);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 14;
    test_case_current = 0;

    run_case(&rc, case_kc_llm_options_default);
    run_case(&rc, case_kc_llm_options_load_env);
    run_case(&rc, case_kc_llm_options_free);
    run_case(&rc, case_kc_llm_version);
    run_case(&rc, case_kc_llm_info_static);
    run_case(&rc, case_kc_llm_info_request_required);
    run_case(&rc, case_kc_llm_info_input_tokens);
    run_case(&rc, case_kc_llm_info_runtime);
    run_case(&rc, case_kc_llm_open_close);
    run_case(&rc, case_kc_llm_generate);
    run_case(&rc, case_kc_llm_generate_role);
    run_case(&rc, case_kc_llm_lora);
    run_case(&rc, case_kc_llm_stop);
    run_case(&rc, case_kc_llm_memory_clear);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * main - Test entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_llm_options_default") == 0) return case_kc_llm_options_default();
    if (strcmp(argv[1], "kc_llm_options_load_env") == 0) return case_kc_llm_options_load_env();
    if (strcmp(argv[1], "kc_llm_options_free") == 0) return case_kc_llm_options_free();
    if (strcmp(argv[1], "kc_llm_version") == 0) return case_kc_llm_version();
    if (strcmp(argv[1], "kc_llm_info_static") == 0) return case_kc_llm_info_static();
    if (strcmp(argv[1], "kc_llm_info_request_required") == 0) return case_kc_llm_info_request_required();
    if (strcmp(argv[1], "kc_llm_info_input_tokens") == 0) return case_kc_llm_info_input_tokens();
    if (strcmp(argv[1], "kc_llm_info_runtime") == 0) return case_kc_llm_info_runtime();
    if (strcmp(argv[1], "kc_llm_open_close") == 0) return case_kc_llm_open_close();
    if (strcmp(argv[1], "kc_llm_generate") == 0) return case_kc_llm_generate();
    if (strcmp(argv[1], "kc_llm_generate_role") == 0) return case_kc_llm_generate_role();
    if (strcmp(argv[1], "kc_llm_lora") == 0) return case_kc_llm_lora();
    if (strcmp(argv[1], "kc_llm_stop") == 0) return case_kc_llm_stop();
    if (strcmp(argv[1], "kc_llm_memory_clear") == 0) return case_kc_llm_memory_clear();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
