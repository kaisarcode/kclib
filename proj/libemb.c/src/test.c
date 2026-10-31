/**
 * test.c - libemb public API tests.
 * Summary: Contract tests for fixed-model embeddings.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libemb.h"

/**
 * Tokenize UTF-8 text using a caller-provided vocabulary.
 * @param input Input text.
 * @param tokens Output token IDs.
 * @param capacity Output capacity.
 * @param cls_id Classification token ID.
 * @param sep_id Separator token ID.
 * @param unk_id Unknown-word token ID.
 * @param lookup Vocabulary token lookup.
 * @param context Vocabulary lookup context.
 * @param count Receives the token count.
 * @return None.
 */
void kc_emb_wordpiece_tokenize(
    const char *input,
    int *tokens,
    int capacity,
    int cls_id,
    int sep_id,
    int unk_id,
    int (*lookup)(void *context, const char *token),
    void *context,
    int *count
);

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMB_EXPECTED_DIM 384

static int test_case_total = 0;
static int test_case_current = 0;

#ifndef __EMSCRIPTEN__
static int case_emb_cli(void);
#endif

typedef int (*case_fn)(void);

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param detail Human-readable behavior description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Nonzero when the check passed.
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
 * Verifies two embedding vectors are close within tolerance.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @param tolerance Maximum absolute difference.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_close(
    const char *name,
    const float *a,
    const float *b,
    size_t count,
    float tolerance
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > tolerance) {
            printf("[FAIL] %s: vectors differ at %zu\n", name, i);
            return 1;
        }
    }
    return 0;
}

/**
 * Verifies two embedding vectors contain a measurable difference.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_distinct(
    const char *name,
    const float *a,
    const float *b,
    size_t count
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > 0.000001f) return 0;
    }
    printf("[FAIL] %s\n", name);
    return 1;
}

/**
 * Verifies one embedding vector has the fixed model shape and finite values.
 * @param name Check description.
 * @param vec Vector data.
 * @param count Element count.
 * @return 0 on success, 1 on failure.
 */
static int expect_vector_valid(const char *name, const float *vec, size_t count) {
    size_t i;
    int nonzero = 0;

    if (!vec || count != EMB_EXPECTED_DIM) {
        printf("[FAIL] %s: expected %d floats, got %zu\n",
            name, EMB_EXPECTED_DIM, count);
        return 1;
    }
    for (i = 0; i < count; i++) {
        if (!isfinite(vec[i])) {
            printf("[FAIL] %s: non-finite value at %zu\n", name, i);
            return 1;
        }
        if (vec[i] != 0.0f) nonzero = 1;
    }
    return expect_true(name, nonzero);
}

/**
 * Tests the public build version.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_version(void) {
    int fail = expect_true("version nonzero", kc_emb_version() != 0U);

    case_result(fail, "kc_emb_version", "returns the generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the fixed embedding dimension exposed by the model.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_dimension(void) {
    size_t dimension = kc_emb_dimension();
    int fail = 0;

    fail += expect_true("dimension available", dimension != 0U);
    fail += expect_true("dimension matches embedded model",
        dimension == EMB_EXPECTED_DIM);

    case_result(fail, "kc_emb_dimension",
        "returns the embedded model vector dimension");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct fixed-model embedding.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_embed(void) {
    float *vec = NULL;
    float *empty = NULL;
    size_t count = 0;
    size_t empty_count = 0;
    int fail = 0;

    fail += expect_int("embed text returns OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &vec, &count));
    fail += expect_vector_valid("text vector valid", vec, count);
    fail += expect_true("text count matches model dimension",
        count == kc_emb_dimension());

    fail += expect_int("embed empty returns OK", KC_EMB_OK,
        kc_emb_embed("", &empty, &empty_count));
    fail += expect_vector_valid("empty vector valid", empty, empty_count);
    fail += expect_true("empty count matches model dimension",
        empty_count == kc_emb_dimension());

    kc_emb_free(vec);
    kc_emb_free(empty);
    case_result(fail, "kc_emb_embed",
        "embeds text directly with the fixed local model");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct embedding argument and ownership contracts.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_contract(void) {
    float *vec = (float *)0x1;
    size_t count = 123;
    int fail = 0;

    fail += expect_int("NULL input errors", KC_EMB_ERROR,
        kc_emb_embed(NULL, &vec, &count));
    fail += expect_true("NULL input resets vector", vec == NULL);
    fail += expect_true("NULL input resets count", count == 0);

    count = 123;
    fail += expect_int("NULL out_data errors", KC_EMB_ERROR,
        kc_emb_embed("input", NULL, &count));
    fail += expect_true("NULL out_data resets count", count == 0);

    vec = (float *)0x1;
    fail += expect_int("NULL out_count errors", KC_EMB_ERROR,
        kc_emb_embed("input", &vec, NULL));
    fail += expect_true("NULL out_count resets vector", vec == NULL);

    kc_emb_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, "kc_emb_contract",
        "validates arguments, output reset, and ownership");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests deterministic fixed-model output.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_determinism(void) {
    float *a = NULL;
    float *b = NULL;
    float *c = NULL;
    float *spanish_a = NULL;
    float *spanish_b = NULL;
    size_t ca = 0, cb = 0, cc = 0, spanish_ca = 0, spanish_cb = 0;
    int fail = 0;

    fail += expect_int("first embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &a, &ca));
    fail += expect_int("second embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &b, &cb));
    fail += expect_int("different embed OK", KC_EMB_OK,
        kc_emb_embed("incident response runbook", &c, &cc));
    fail += expect_int("spanish computer embed OK", KC_EMB_OK,
        kc_emb_embed("Un servicio técnico diagnostica computadoras lentas.",
            &spanish_a, &spanish_ca));
    fail += expect_int("spanish veterinarian embed OK", KC_EMB_OK,
        kc_emb_embed("Un veterinario atiende animales domésticos enfermos.",
            &spanish_b, &spanish_cb));
    fail += expect_true("all counts fixed",
        ca == EMB_EXPECTED_DIM && cb == EMB_EXPECTED_DIM &&
        cc == EMB_EXPECTED_DIM && spanish_ca == EMB_EXPECTED_DIM &&
        spanish_cb == EMB_EXPECTED_DIM);

    if (a && b && ca == cb) {
        fail += expect_vectors_close("same text deterministic",
            a, b, ca, 0.000001f);
    } else {
        fail += expect_true("determinism vectors available", 0);
    }
    if (a && c && ca == cc) {
        fail += expect_vectors_distinct("different text differs", a, c, ca);
    } else {
        fail += expect_true("distinct vectors available", 0);
    }

    if (spanish_a && spanish_b && spanish_ca == spanish_cb) {
        fail += expect_vectors_distinct("unrelated spanish texts differ",
            spanish_a, spanish_b, spanish_ca);
    } else {
        fail += expect_true("spanish vectors available", 0);
    }

    kc_emb_free(a);
    kc_emb_free(b);
    kc_emb_free(c);
    kc_emb_free(spanish_a);
    kc_emb_free(spanish_b);
    case_result(fail, "kc_emb_determinism",
        "keeps fixed-model output deterministic and input-sensitive");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "emb"
#endif

/**
 * Write one temporary CLI input fixture.
 * @param path Fixture path.
 * @param text Fixture text.
 * @return 0 on success, 1 on failure.
 */
static int emb_cli_write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    int ok;

    if (!file) return 1;
    ok = fwrite(text, 1, size, file) == size;
    fclose(file);
    return ok ? 0 : 1;
}

/**
 * Run the CLI with output discarded.
 * @param args Arguments appended after the executable.
 * @return 0 on success, 1 on failure.
 */
static int emb_cli_run(const char *args) {
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args ? args : "");
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args ? args : "");
#endif
    return system(command) == 0 ? 0 : 1;
}

/**
 * Run the CLI and capture its standard output.
 * @param args Arguments appended after the executable.
 * @param stdin_text Optional standard-input text.
 * @param output Destination output buffer.
 * @param output_size Destination buffer size.
 * @return 0 on success, 1 on failure.
 */
static int emb_cli_capture(
    const char *args,
    const char *stdin_text,
    char *output,
    size_t output_size
) {
    const char *stdin_path = "emb-cli-stdin.txt";
    const char *output_path = "emb-cli-output.txt";
    char command[4096];
    FILE *file;
    size_t size;

    if (!output || output_size == 0) return 1;
    if (stdin_text && emb_cli_write_file(stdin_path, stdin_text)) return 1;
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH, args ? args : "",
        stdin_text ? "< emb-cli-stdin.txt " : "", output_path);
#else
    snprintf(command, sizeof(command), "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH, args ? args : "",
        stdin_text ? "< emb-cli-stdin.txt " : "", output_path);
#endif
    if (system(command) != 0) {
        if (stdin_text) remove(stdin_path);
        remove(output_path);
        return 1;
    }
    if (stdin_text) remove(stdin_path);
    file = fopen(output_path, "rb");
    if (!file) return 1;
    size = fread(output, 1, output_size - 1, file);
    fclose(file);
    remove(output_path);
    output[size] = '\0';
    return 0;
}

/**
 * Test the public CLI input and output contract.
 * @return 0 on success, 1 on failure.
 */
static int case_emb_cli(void) {
    const char *file_path = "emb-cli-file.txt";
    char expected[8192];
    char actual[8192];
    int fixture_ok;
    int fail = 0;

    fail += emb_cli_run("--help");
    fail += emb_cli_run("--version");
    fail += emb_cli_run("--dimension");
    fail += emb_cli_run("\"Hello world\"");
    fail += emb_cli_capture("\"Hello world\"", NULL, actual,
        sizeof(actual));
    fail += actual[0] == '\0' || strchr(actual, ',') == NULL ||
        strchr(actual, '\t') != NULL || strncmp(actual, "code,", 5) == 0 ||
        strncmp(actual, "value,", 6) == 0;

    fixture_ok = emb_cli_write_file(file_path, "File input") == 0;
    fail += !fixture_ok || emb_cli_run("-i emb-cli-file.txt");
    fail += !fixture_ok || emb_cli_capture(NULL, "Stdin input", expected,
        sizeof(expected)) || emb_cli_capture("-i emb-cli-file.txt",
        "Stdin input", actual, sizeof(actual)) || strcmp(expected, actual);
    fail += !fixture_ok || emb_cli_capture("\"Argument input\"", NULL,
        expected, sizeof(expected)) || emb_cli_capture(
        "\"Argument input\" -i emb-cli-file.txt", "Stdin input", actual,
        sizeof(actual)) || strcmp(expected, actual);
    remove(file_path);
    fail += emb_cli_run("\"Hello world\" --unknown") == 0;
    case_result(fail, "emb_cli",
        "validates embedding input precedence and CSV output");
    return fail != 0;
}
#endif

/** Tokenizer fixture with expected token IDs. */
typedef struct {
    const char *text;
    int expected[16];
    int count;
} kc_tokenizer_case_t;

static const char *kc_test_vocab[] = {
    "[UNK]", "[CLS]", "[SEP]", "¡", "hola", ",", "caneria", "!",
    "¿", "como", "evitar", "que", "alguien", "entre", "?",
    "cafe", "nino", "hello", "world", "un", "##aff", "##able",
    "中", "文", "long", "##er", "-", "a", "b", "x", "foo", "[MASK]"
};

/**
 * Resolve one token from the WordPiece fixture.
 * @param ctx Unused fixture context.
 * @param value Token to find.
 * @return Token ID, or -1 when absent.
 */
static int kc_test_token_lookup(void *ctx, const char *value) {
    (void)ctx;
    for (size_t i = 0; i < sizeof(kc_test_vocab) / sizeof(kc_test_vocab[0]); i++)
        if (strcmp(kc_test_vocab[i], value) == 0) return (int)i;
    return -1;
}

static const char *kc_test_gguf_vocab[] = {
    "▁hola", "▁como", "▁cafe", "▁un", "##aff", "##able", "¿", "?"
};

/**
 * Resolve one token from the GGUF-style fixture.
 * @param ctx Unused fixture context.
 * @param value Token to find.
 * @return Token ID, or -1 when absent.
 */
static int kc_test_gguf_lookup(void *ctx, const char *value) {
    (void)ctx;
    for (size_t i = 0; i < sizeof(kc_test_gguf_vocab) / sizeof(kc_test_gguf_vocab[0]); i++)
        if (strcmp(kc_test_gguf_vocab[i], value) == 0) return (int)(100 + i);
    return -1;
}

/**
 * Verify Unicode normalization, WordPiece segmentation, and limits.
 * @return 0 if every tokenizer regression test passes, otherwise 1.
 */
static int case_emb_tokenizer(void) {
    const kc_tokenizer_case_t cases[] = {
        {"¡Hola, CAÑERÍA!", {1, 3, 4, 5, 6, 7, 2}, 7},
        {"¿Cómo evitar que alguien entre?", {1, 8, 9, 10, 11, 12, 13, 14, 2}, 9},
        {"CAFE\xCC\x81 niño café", {1, 15, 16, 15, 2}, 5},
        {"HELLO\tWorld!", {1, 17, 18, 7, 2}, 5},
        {"unaffable", {1, 19, 20, 21, 2}, 5},
        {"中文", {1, 22, 23, 2}, 4},
        {"hello\xE2\x80\x87world", {1, 17, 18, 2}, 4},
        {"a😀b", {1, 0, 2}, 3},
        {"foo-bar", {1, 30, 26, 0, 2}, 5},
        {"longer", {1, 24, 25, 2}, 4},
        {"longering", {1, 0, 2}, 3},
        {"", {1, 2}, 2},
        {"[MASK] hello", {1, 31, 17, 2}, 4},
    };
    int errors = 0;
    for (size_t t = 0; t < sizeof(cases) / sizeof(cases[0]); t++) {
        int count = -1, actual[512] = {0};
        kc_emb_wordpiece_tokenize(cases[t].text, actual, 512, 1, 2, 0,
            kc_test_token_lookup, NULL, &count);
        int failed = count != cases[t].count;
        if (!failed) {
            for (int j = 0; j < count; j++) {
                if (actual[j] != cases[t].expected[j]) { failed = 1; break; }
            }
        }
        printf("[%s] tokenizer_%02zu: %s\n", failed ? "FAIL" : "PASS", t + 1,
            cases[t].text);
        if (failed) {
            printf("  expected:");
            for (int j = 0; j < cases[t].count; j++) printf(" %d", cases[t].expected[j]);
            printf("\n  received:");
            for (int j = 0; j < count; j++) printf(" %d", actual[j]);
            putchar('\n');
            errors++;
        }
    }
    {
        int actual[4] = {0};
        int count = -1;
        kc_emb_wordpiece_tokenize("hello world foo", actual, 4,
            1, 2, 0, kc_test_token_lookup, NULL, &count);
        int failed = count != 4 || actual[0] != 1 ||
            actual[1] != 17 || actual[2] != 18 || actual[3] != 2;
        printf("[%s] tokenizer_truncation\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    {
        int tokens[16] = {0}, count = -1;
        const int expected[] = {1, 106, 101, 102, 107, 2};
        kc_emb_wordpiece_tokenize("¿Cómo café?", tokens, 16, 1, 2, 0,
            kc_test_gguf_lookup, NULL, &count);
        int failed = count != 6;
        for (int i = 0; !failed && i < 6; i++)
            if (tokens[i] != expected[i]) failed = 1;
        printf("[%s] tokenizer_gguf_word_starts\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    {
        int tokens[16] = {0}, count = -1;
        kc_emb_wordpiece_tokenize("unaffable", tokens, 16, 1, 2, 0,
            kc_test_gguf_lookup, NULL, &count);
        int failed = count != 5 || tokens[0] != 1 || tokens[1] != 103 ||
            tokens[2] != 104 || tokens[3] != 105 || tokens[4] != 2;
        printf("[%s] tokenizer_gguf_continuations\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    case_result(errors, "emb_tokenizer",
        "normalizes Unicode and segments both WordPiece vocabularies");
    return errors ? 1 : 0;
}

/**
 * Runs all reusable and platform-applicable test cases.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    test_case_total = 7;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_emb_version);
    run_case(&rc, case_kc_emb_dimension);
    run_case(&rc, case_kc_emb_embed);
    run_case(&rc, case_kc_emb_contract);
    run_case(&rc, case_kc_emb_determinism);
    run_case(&rc, case_emb_tokenizer);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_emb_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one emb contract test case.
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
    if (strcmp(argv[1], "kc_emb_version") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_version();
    }
    if (strcmp(argv[1], "kc_emb_dimension") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_dimension();
    }
    if (strcmp(argv[1], "kc_emb_embed") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_embed();
    }
    if (strcmp(argv[1], "kc_emb_contract") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_contract();
    }
    if (strcmp(argv[1], "kc_emb_determinism") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_determinism();
    }
    if (strcmp(argv[1], "emb_tokenizer") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_emb_tokenizer();
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "emb_cli") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_emb_cli();
    }
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
