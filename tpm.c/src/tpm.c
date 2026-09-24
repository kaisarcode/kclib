/**
 * tpm.c - Text profile matcher.
 * Summary: Command line interface for the tpm tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libtpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Read a file into a dynamically allocated buffer.
 * @param path File path.
 * @param out_text Destination pointer for allocated text.
 * @return 0 on success, -1 on failure.
 */
static int kc_tpm_read_file(const char *path, char **out_text) {
    FILE *f;
    char *data;
    size_t length;
    size_t capacity;
    char chunk[4096];
    size_t n;

    if (!path || !out_text) {
        return -1;
    }

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    data = NULL;
    length = 0;
    capacity = 0;

    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (length + n + 1 > capacity) {
            size_t next_cap;
            char *next_data;

            next_cap = capacity ? capacity * 2 : 4096;
            while (next_cap < length + n + 1) {
                next_cap *= 2;
            }

            next_data = (char *)realloc(data, next_cap);
            if (!next_data) {
                free(data);
                fclose(f);
                return -1;
            }

            data = next_data;
            capacity = next_cap;
        }

        memcpy(data + length, chunk, n);
        length += n;
    }

    if (ferror(f)) {
        free(data);
        fclose(f);
        return -1;
    }

    fclose(f);

    if (length == 0) {
        *out_text = NULL;
        return 0;
    }

    data[length] = '\0';
    *out_text = data;
    return 0;
}

/**
 * Read stdin into a dynamically allocated buffer.
 * @param out_text Destination pointer for allocated text.
 * @return 0 on success, -1 on failure.
 */
static int kc_tpm_read_stdin(char **out_text) {
    char *data;
    size_t length;
    size_t capacity;
    char chunk[4096];
    size_t n;

    if (!out_text) {
        return -1;
    }

    data = NULL;
    length = 0;
    capacity = 0;

    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (length + n + 1 > capacity) {
            size_t next_cap;
            char *next_data;

            next_cap = capacity ? capacity * 2 : 4096;
            while (next_cap < length + n + 1) {
                next_cap *= 2;
            }

            next_data = (char *)realloc(data, next_cap);
            if (!next_data) {
                free(data);
                return -1;
            }

            data = next_data;
            capacity = next_cap;
        }

        memcpy(data + length, chunk, n);
        length += n;
    }

    if (ferror(stdin)) {
        free(data);
        return -1;
    }

    if (length == 0) {
        free(data);
        *out_text = NULL;
        return 0;
    }

    data[length] = '\0';
    *out_text = data;
    return 0;
}

/**
 * Print CLI help message.
 * @return None.
 */
static void kc_tpm_help(void) {
    printf("Usage: tpm <map> [-n <size>]\n");
    printf("\n");
    printf("Score stdin text against the n-gram profile built from <map>.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -n <size>      N-gram size (default 3)\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
    printf("\n");
    printf("Examples:\n");
    printf("  echo \"hello world\" | tpm en.map\n");
    printf("  echo \"hola mundo\"  | tpm -n 4 es.map\n");
}

/**
 * Prints the binary version to stdout.
 * @return None.
 */
static void kc_tpm_print_version(void) {
    printf("tpm build %llu\n", (unsigned long long)kc_tpm_version());
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status code.
 */
int main(int argc, char **argv) {
    const char *map_path;
    char *map_text;
    char *stdin_text;
    int ngram_size;
    int ngram_size_set;
    int i;

    map_path = NULL;
    map_text = NULL;
    stdin_text = NULL;
    ngram_size = 0;
    ngram_size_set = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_tpm_help();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_tpm_print_version();
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0) {
            char *end;
            long val;

            if (++i >= argc) {
                fprintf(stderr, "tpm: missing value for -n\n");
                return 1;
            }

            val = strtol(argv[i], &end, 10);
            if (*end != '\0' || val < 1 || val > 8) {
                fprintf(stderr, "tpm: invalid n-gram size\n");
                return 1;
            }

            ngram_size = (int)val;
            ngram_size_set = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "tpm: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (map_path) {
            fprintf(stderr, "tpm: too many arguments\n");
            return 1;
        }
        map_path = argv[i];
    }

    if (!map_path) {
        fprintf(stderr, "tpm: missing map file\n");
        return 1;
    }

    if (kc_tpm_read_file(map_path, &map_text) != 0) {
        fprintf(stderr, "tpm: failed to read map file\n");
        return 1;
    }

    if (kc_tpm_read_stdin(&stdin_text) != 0) {
        fprintf(stderr, "tpm: failed to read stdin\n");
        free(map_text);
        return 1;
    }

    if (!stdin_text || !*stdin_text) {
        puts("0.000000");
        free(map_text);
        free(stdin_text);
        return 0;
    }

    {
        kc_tpm_t *tpm = NULL;
        kc_tpm_options_t options = {0};
        double score = 0.0;

        if (ngram_size_set) {
            options.has_ngram_size = 1;
            options.ngram_size = ngram_size;
        }

        if (kc_tpm_open(&tpm, map_text, &options) != KC_TPM_OK) {
            fprintf(stderr, "tpm: profile creation failed\n");
            free(map_text);
            free(stdin_text);
            return 1;
        }

        if (kc_tpm_score(tpm, stdin_text, &score) != KC_TPM_OK) {
            score = 0.0;
        }
        kc_tpm_close(tpm);

        printf("%.6f\n", score);
        free(map_text);
        free(stdin_text);
        return 0;
    }
}
