/**
 * bm25.c - BM25 command-line interface.
 * Summary: Build and search standalone lexical indexes.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libbm25.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print CLI usage.
 * @return None.
 */
static void bm25_usage(void) {
    puts("Usage: bm25 <command> [arguments]\n"
        "  build <index> [-i file] [--fold-accents]\n"
        "  search <index> <query> [-l limit] [--k1 value] [--b value]\n"
        "  -h, --help       Show this help\n"
        "  --version        Show the build version");
}

/**
 * Parse an unsigned integer command-line option.
 * @param text String to parse.
 * @param out Receives the parsed size.
 * @return Zero when valid, otherwise one.
 */
static int bm25_size(const char *text, size_t *out) {
    char *end;
    unsigned long long value;

    if (text == NULL || !text[0] || text[0] == '-') return 1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > SIZE_MAX) return 1;
    *out = (size_t)value;
    return 0;
}

/**
 * Parse a finite double-precision command-line option.
 * @param text String to parse.
 * @param out Receives the parsed value.
 * @return Zero when valid, otherwise one.
 */
static int bm25_double(const char *text, double *out) {
    char *end;
    double value;

    if (text == NULL || !text[0]) return 1;
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || *end != '\0' || !isfinite(value)) return 1;
    *out = value;
    return 0;
}

/**
 * Read a line from an input stream with no platform-specific getline API.
 * @param file Source stream.
 * @param buffer Reusable buffer allocation.
 * @param capacity Current capacity.
 * @param length Receives line length without a newline.
 * @return 0 for a line, 1 for end-of-stream, or -1 for an I/O or memory error.
 */
static int bm25_line(FILE *file, char **buffer, size_t *capacity,
    size_t *length)
{
    size_t used = 0;
    int c;

    while ((c = fgetc(file)) != EOF) {
        char *grown;

        if (c == '\n') break;
        if (used > SIZE_MAX - 2) return -1;
        if (used + 2 > *capacity) {
            size_t next = *capacity == 0 ? 256 : *capacity * 2;
            if (next < *capacity) next = used + 2;
            grown = (char *)realloc(*buffer, next);
            if (grown == NULL) return -1;
            *buffer = grown;
            *capacity = next;
        }
        (*buffer)[used++] = (char)c;
    }
    if (c == EOF && (ferror(file) || used == 0))
        return ferror(file) ? -1 : 1;
    if (used > 0 && (*buffer)[used - 1] == '\r') used--;
    if (*capacity == 0) {
        *buffer = (char *)malloc(1);
        if (*buffer == NULL) return -1;
        *capacity = 1;
    }
    (*buffer)[used] = '\0';
    *length = used;
    return 0;
}

/**
 * Add line-delimited TSV document associations to a writable index.
 * @param index Index that receives the documents.
 * @param input Input stream.
 * @return KC_BM25_OK or an error status.
 */
static int bm25_documents(kc_bm25_t *index, FILE *input) {
    char *line = NULL;
    size_t capacity = 0;
    size_t length = 0;
    int status = KC_BM25_OK;

    for (;;) {
        int state = bm25_line(input, &line, &capacity, &length);
        char *tab;

        if (state == 1) break;
        if (state < 0) {
            status = KC_BM25_EIO;
            break;
        }
        if (length == 0) continue;
        if (memchr(line, '\0', length) != NULL) {
            status = KC_BM25_EINVAL;
            break;
        }
        tab = strchr(line, '\t');
        if (tab == NULL || tab == line || strchr(tab + 1, '\t') != NULL) {
            status = KC_BM25_EINVAL;
            break;
        }
        *tab = '\0';
        status = kc_bm25_add(index, line, tab + 1);
        if (status != KC_BM25_OK) break;
    }
    free(line);
    return status;
}

/**
 * Parse build options and create a portable index.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
static int bm25_build(int argc, char **argv) {
    const char *path;
    const char *input_path = NULL;
    FILE *input = stdin;
    kc_bm25_t *index = NULL;
    kc_bm25_options_t options;
    int accents = 0;
    int status;

    if (argc < 3) return 1;
    path = argv[2];
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 ||
            strcmp(argv[i], "--input") == 0)
        {
            if (input_path != NULL || ++i >= argc || !argv[i][0])
                return 1;
            input_path = argv[i];
        } else if (strcmp(argv[i], "--fold-accents") == 0) {
            accents = 1;
        } else return 1;
    }
    if (input_path != NULL) {
        input = fopen(input_path, "rb");
        if (input == NULL) {
            fprintf(stderr, "bm25: cannot open input '%s'\n", input_path);
            return 1;
        }
    }
    options.fold_accents = &accents;
    status = kc_bm25_open(&index, &options);
    if (status == KC_BM25_OK)
        status = bm25_documents(index, input);
    if (input != stdin) fclose(input);
    if (status == KC_BM25_OK) status = kc_bm25_build(index);
    if (status == KC_BM25_OK) status = kc_bm25_save(index, path);
    kc_bm25_close(index);
    if (status != KC_BM25_OK) {
        fprintf(stderr, "bm25: %s\n", kc_bm25_strerror(status));
        return 1;
    }
    return 0;
}

/**
 * Parse search options and print the ranked result list.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
static int bm25_search(int argc, char **argv) {
    const char *path, *query;
    kc_bm25_t *index = NULL;
    kc_bm25_result_t *results = NULL;
    kc_bm25_search_options_t options = {0};
    size_t count = 0, limit = 10;
    double k1 = 1.2, b = 0.75;
    int status;

    if (argc < 4) return 1;
    path = argv[2];
    query = argv[3];
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 ||
            strcmp(argv[i], "--limit") == 0)
        {
            if (++i >= argc || bm25_size(argv[i], &limit)) return 1;
        } else if (strcmp(argv[i], "--k1") == 0) {
            if (++i >= argc || bm25_double(argv[i], &k1)) return 1;
        } else if (strcmp(argv[i], "--b") == 0) {
            if (++i >= argc || bm25_double(argv[i], &b)) return 1;
        } else return 1;
    }
    options.limit = &limit;
    options.k1 = &k1;
    options.b = &b;
    status = kc_bm25_load(&index, path);
    if (status == KC_BM25_OK)
        status = kc_bm25_search(index, query, &options, &results, &count);
    if (status == KC_BM25_OK) {
        for (size_t i = 0; i < count; i++) {
            printf("%zu\t%s\t%.17g\t%s\n",
                results[i].ordinal, results[i].id,
                results[i].score, results[i].content);
        }
    } else fprintf(stderr, "bm25: %s\n", kc_bm25_strerror(status));
    kc_bm25_free(results);
    kc_bm25_close(index);
    return status == KC_BM25_OK ? 0 : 1;
}

/**
 * Dispatch the standalone CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Zero on success, otherwise nonzero.
 */
int main(int argc, char **argv) {
    int status;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        bm25_usage();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("%llu\n", (unsigned long long)kc_bm25_version());
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "build") == 0) {
        status = bm25_build(argc, argv);
    } else if (argc > 1 && strcmp(argv[1], "search") == 0) {
        status = bm25_search(argc, argv);
    } else status = 1;

    if (status != 0) {
        fputs("bm25: invalid command or arguments\n", stderr);
        bm25_usage();
    }
    return status;
}
