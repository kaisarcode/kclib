/**
 * tpm.c - Text profile matcher command-line interface.
 * Summary: Scores input text against a reusable libtpm text profile.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libtpm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void tpm_cli_usage(const char *name)
{
    printf("Usage: %s <map> [text] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>   Read input text from file\n");
    printf("  -n, --ngram <N>      N-gram size (default: 3)\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Parses one n-gram size.
 * @param text Input string.
 * @param out Parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int tpm_cli_parse_ngram(const char *text, int *out)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1) {
        return 1;
    }
    *out = (int)value;
    return 0;
}

/**
 * Reads all bytes from one stream as a NUL-terminated string.
 * @param stream Input stream.
 * @param out Receives allocated string.
 * @param out_size Receives byte count when non-NULL.
 * @return Zero on success, nonzero on failure.
 */
static int tpm_cli_read_stream(FILE *stream, char **out, size_t *out_size)
{
    char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    char chunk[4096];
    size_t count;

    *out = NULL;
    if (out_size) *out_size = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required = size + count + 1;
        char *next;

        if (required > capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 4096;

            while (next_capacity < required) next_capacity *= 2;
            next = (char *)realloc(buffer, next_capacity);
            if (!next) {
                free(buffer);
                return 1;
            }
            buffer = next;
            capacity = next_capacity;
        }
        memcpy(buffer + size, chunk, count);
        size += count;
    }
    if (ferror(stream)) {
        free(buffer);
        return 1;
    }
    if (!buffer) {
        buffer = (char *)malloc(1);
        if (!buffer) return 1;
    }
    buffer[size] = '\0';
    *out = buffer;
    if (out_size) *out_size = size;
    return 0;
}

/**
 * Reads one input file.
 * @param path Input file path.
 * @param out Receives allocated string.
 * @return Zero on success, nonzero on failure.
 */
static int tpm_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = tpm_cli_read_stream(file, out, NULL);
    fclose(file);
    return status;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int tpm_cli_stdin_is_tty(void)
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (handle == INVALID_HANDLE_VALUE || handle == NULL) return 0;
    return GetConsoleMode(handle, &mode) != 0;
#else
    return isatty(STDIN_FILENO);
#endif
}

/**
 * Runs the tpm CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_tpm_options_t options;
    kc_tpm_t *profile = NULL;
    const char *map_text = NULL;
    const char *text = NULL;
    const char *input_path = NULL;
    char *owned_input = NULL;
    const char *input = NULL;
    double score = 0.0;
    int ngram_size = 0;
    int has_ngram = 0;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        tpm_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_tpm_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tpm: --input requires a file\n");
                return 1;
            }
            if (input_path != NULL) {
                fprintf(stderr, "tpm: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-n") == 0 ||
            strcmp(argv[i], "--ngram") == 0)
        {
            if (++i >= argc || tpm_cli_parse_ngram(argv[i], &ngram_size)) {
                fprintf(stderr, "tpm: ngram must be a positive integer\n");
                return 1;
            }
            has_ngram = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "tpm: -h/--help and -v/--version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "tpm: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (!map_text) {
            map_text = argv[i];
        } else if (!text) {
            text = argv[i];
        } else {
            fprintf(stderr, "tpm: only map and text positional arguments are accepted\n");
            return 1;
        }
    }

    if (!map_text) {
        fprintf(stderr, "tpm: missing map text\n");
        return 1;
    }

    if (text) {
        input = text;
    } else if (!tpm_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (tpm_cli_read_stream(stdin, &owned_input, &stdin_size)) {
            fprintf(stderr, "tpm: cannot read stdin\n");
            return 1;
        }
        if (stdin_size > 0) input = owned_input;
    }
    if (!input && input_path) {
        free(owned_input);
        owned_input = NULL;
        if (tpm_cli_read_file(input_path, &owned_input)) {
            fprintf(stderr, "tpm: cannot read input file\n");
            return 1;
        }
        input = owned_input;
    }
    if (!input) {
        if (!owned_input && tpm_cli_read_stream(stdin, &owned_input, NULL)) {
            fprintf(stderr, "tpm: cannot read stdin\n");
            return 1;
        }
        input = owned_input;
    }

    memset(&options, 0, sizeof(options));
    if (has_ngram) options.ngram_size = &ngram_size;

    if (kc_tpm_open(&profile, map_text, &options) != KC_TPM_OK) {
        fprintf(stderr, "tpm: cannot create text profile\n");
        free(owned_input);
        return 1;
    }
    if (kc_tpm_score(profile, input, &score) != KC_TPM_OK) {
        fprintf(stderr, "tpm: text scoring failed\n");
        kc_tpm_close(profile);
        free(owned_input);
        return 1;
    }

    printf("%.12g\n", score);

    kc_tpm_close(profile);
    free(owned_input);
    return 0;
}
