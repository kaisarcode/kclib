/* Internal tokenizer regression tests; LGPL-3.0-only. */
#include "tokenizer.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *text;
    int expected[16];
    int count;
} test_case_t;

static const char *vocab[] = {
    "[UNK]", "[CLS]", "[SEP]", "¡", "hola", ",", "caneria", "!",
    "¿", "como", "evitar", "que", "alguien", "entre", "?",
    "cafe", "nino", "hello", "world", "un", "##aff", "##able",
    "中", "文", "long", "##er", "-", "a", "b", "x", "foo", "[MASK]"
};

static int lookup(void *ctx, const char *value) {
    (void)ctx;
    for (size_t i = 0; i < sizeof(vocab) / sizeof(vocab[0]); i++)
        if (strcmp(vocab[i], value) == 0) return (int)i;
    return -1;
}

static const char *gguf_vocab[] = {
    "▁hola", "▁como", "▁cafe", "▁un", "##aff", "##able", "¿", "?"
};

static int gguf_lookup(void *ctx, const char *value) {
    (void)ctx;
    for (size_t i = 0; i < sizeof(gguf_vocab) / sizeof(gguf_vocab[0]); i++)
        if (strcmp(gguf_vocab[i], value) == 0) return (int)(100 + i);
    return -1;
}

int main(void) {
    const test_case_t cases[] = {
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
            lookup, NULL, &count);
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
    { /* Length limit always leaves room for SEP. */
        int actual[4] = {0};
        int count = -1;
        kc_emb_wordpiece_tokenize("hello world foo", actual, 4,
            1, 2, 0, lookup, NULL, &count);
        int failed = count != 4 || actual[0] != 1 ||
            actual[1] != 17 || actual[2] != 18 || actual[3] != 2;
        printf("[%s] tokenizer_truncation\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    { /* GGUF BERT word-start tokens and WordPiece continuations. */
        int tokens[16] = {0}, count = -1;
        const int expected[] = {1, 106, 101, 102, 107, 2};
        kc_emb_wordpiece_tokenize("¿Cómo café?", tokens, 16, 1, 2, 0,
            gguf_lookup, NULL, &count);
        int failed = count != 6;
        for (int i = 0; !failed && i < 6; i++)
            if (tokens[i] != expected[i]) failed = 1;
        printf("[%s] tokenizer_gguf_word_starts\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    { /* GGUF word start plus ## continuation. */
        int tokens[16] = {0}, count = -1;
        kc_emb_wordpiece_tokenize("unaffable", tokens, 16, 1, 2, 0,
            gguf_lookup, NULL, &count);
        int failed = count != 5 || tokens[0] != 1 || tokens[1] != 103 ||
            tokens[2] != 104 || tokens[3] != 105 || tokens[4] != 2;
        printf("[%s] tokenizer_gguf_continuations\n", failed ? "FAIL" : "PASS");
        errors += failed;
    }
    return errors ? 1 : 0;
}
