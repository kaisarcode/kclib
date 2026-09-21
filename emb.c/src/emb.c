/**
 * emb.c - Vector Embedding Library CLI
 * Summary: Command line interface for generating vector embeddings.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libemb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#define isatty _isatty
#define STDIN_FILENO 0
#endif

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s <input> [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("emb build %llu\n", (unsigned long long)kc_emb_version());
}

/**
 * Print a vector to stdout in space-separated format.
 * @param vec Vector data.
 * @param dim Vector dimension.
 * @return None.
 */
static void kc_emb_print_vector(const float *vec, size_t dim) {
    for (size_t i = 0; i < dim; i++) {
        printf("%.6f%c", vec[i], (i == dim - 1) ? '\n' : ' ');
    }
    fflush(stdout);
}

/**
 * Read one complete line from a stream.
 * @param stream Input stream.
 * @return Allocated line without trailing newline, or NULL on EOF/error.
 */
static char *kc_read_line(FILE *stream) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;

    while ((ch = fgetc(stream)) != EOF) {
        char *next;

        if (ch == '\n') {
            break;
        }

        if (len + 1 >= cap) {
            cap = cap ? cap * 2 : 256;
            next = (char *)realloc(buf, cap);

            if (!next) {
                free(buf);
                return NULL;
            }

            buf = next;
        }

        buf[len++] = (char)ch;
    }

    if (ch == EOF && len == 0) {
        free(buf);
        return NULL;
    }

    if (len + 1 >= cap) {
        char *next;
        cap = cap ? cap + 1 : 1;
        next = (char *)realloc(buf, cap);

        if (!next) {
            free(buf);
            return NULL;
        }

        buf = next;
    }

    buf[len] = '\0';
    return buf;
}

/**
 * Entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_emb_t *ctx = NULL;
    int status = 0;

    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            kc_print_version();
            return 0;
        }
    }

    if (kc_emb_open(&ctx) != KC_EMB_OK) {
        fprintf(stderr, "emb: initialization failed\n");
        return 1;
    }

    size_t dim = kc_emb_dim(ctx);
    if (dim == 0) {
        fprintf(stderr, "emb: invalid dimension\n");
        kc_emb_close(ctx);
        return 1;
    }

    if (argc >= 2) {
        size_t total = 0;
        for (int i = 1; i < argc; i++) {
            total += strlen(argv[i]);
        }

        total += (argc - 2);
        char *input_text = (char *)malloc(total + 1);
        if (!input_text) {
            fprintf(stderr, "emb: out of memory\n");
            kc_emb_close(ctx);
            return 1;
        }

        char *ptr = input_text;
        for (int i = 1; i < argc; i++) {
            size_t slen = strlen(argv[i]);
            memcpy(ptr, argv[i], slen);
            ptr += slen;
            if (i != argc - 1) {
                *ptr++ = ' ';
            }
        }
        *ptr = '\0';

        float *vec = NULL;
        size_t count = 0;
        if (kc_emb_exec(ctx, input_text, &vec, &count) != KC_EMB_OK) {
            const char *err = kc_emb_get_error(ctx);
            if (err && err[0] != '\0') fprintf(stderr, "emb: %s\n", err);
            else fprintf(stderr, "emb: execution failed\n");
            kc_emb_free(vec);
            free(input_text);
            kc_emb_close(ctx);
            return 1;
        }

        free(input_text);
        kc_emb_print_vector(vec, count);
        kc_emb_free(vec);
    } else {
        if (isatty(STDIN_FILENO)) {
            kc_print_help(argv[0]);
            status = 1;
            goto cleanup;
        }

        for (;;) {
            char *line = kc_read_line(stdin);
            if (!line) {
                break;
            }

            float *vec = NULL;
            size_t count = 0;
            if (kc_emb_exec(ctx, line, &vec, &count) != KC_EMB_OK) {
                const char *err = kc_emb_get_error(ctx);
                if (err && err[0] != '\0') fprintf(stderr, "emb: %s\n", err);
                else fprintf(stderr, "emb: execution failed\n");
                kc_emb_free(vec);
                free(line);
                status = 1;
                continue;
            }

            free(line);
            kc_emb_print_vector(vec, count);
            kc_emb_free(vec);
        }
    }

cleanup:
    kc_emb_close(ctx);
    return status;
}
