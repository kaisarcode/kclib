/**
 * lng.c - Language detection command-line interface.
 * Summary: Detects ranked languages with liblng.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "liblng.h"

#include <errno.h>
#include <math.h>
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
static void lng_cli_usage(const char *name)
{
    printf("Usage: %s [text] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>   Read text from file\n");
    printf("  -t, --threshold <N>  Minimum score in range 0..1 (default: 0.001)\n");
    printf("  -l, --limit <N>      Maximum results (default: 1, max: 32)\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Parses one threshold value.
 * @param text Input string.
 * @param out Parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int lng_cli_parse_threshold(const char *text, double *out)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value) ||
        value < 0.0 || value > 1.0)
    {
        return 1;
    }
    *out = value;
    return 0;
}

/**
 * Parses one positive result limit.
 * @param text Input string.
 * @param out Parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int lng_cli_parse_limit(const char *text, size_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (text[0] == '-' || text[0] == '+') return 1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0) return 1;
    if (value > (unsigned long long)(size_t)-1) return 1;
    *out = (size_t)value;
    return 0;
}

/**
 * Reads all bytes from one stream as a NUL-terminated string.
 * @param stream Input stream.
 * @param out Receives allocated string.
 * @param out_size Receives byte count when non-NULL.
 * @return Zero on success, nonzero on failure.
 */
static int lng_cli_read_stream(FILE *stream, char **out, size_t *out_size)
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
static int lng_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = lng_cli_read_stream(file, out, NULL);
    fclose(file);
    return status;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int lng_cli_stdin_is_tty(void)
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
 * Runs the lng CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_lng_options_t options;
    kc_lng_result_t *results = NULL;
    size_t count = 0;
    const char *text = NULL;
    const char *input_path = NULL;
    char *owned_input = NULL;
    const char *input = NULL;
    double threshold = 0.0;
    size_t limit = 0;
    int has_threshold = 0;
    int has_limit = 0;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        lng_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_lng_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "lng: --input requires a file\n");
                return 1;
            }
            if (input_path != NULL) {
                fprintf(stderr, "lng: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-t") == 0 ||
            strcmp(argv[i], "--threshold") == 0)
        {
            if (++i >= argc || lng_cli_parse_threshold(argv[i], &threshold)) {
                fprintf(stderr, "lng: threshold must be a number in range 0..1\n");
                return 1;
            }
            has_threshold = 1;
        } else if (strcmp(argv[i], "-l") == 0 ||
            strcmp(argv[i], "--limit") == 0)
        {
            if (++i >= argc || lng_cli_parse_limit(argv[i], &limit)) {
                fprintf(stderr, "lng: limit must be a positive integer\n");
                return 1;
            }
            has_limit = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "lng: -h/--help and -v/--version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "lng: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (text != NULL) {
            fprintf(stderr, "lng: only one text argument is accepted; quote spaces\n");
            return 1;
        } else {
            text = argv[i];
        }
    }

    if (text) {
        input = text;
    } else if (!lng_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (lng_cli_read_stream(stdin, &owned_input, &stdin_size)) {
            fprintf(stderr, "lng: cannot read stdin\n");
            return 1;
        }
        if (stdin_size > 0) input = owned_input;
    }
    if (!input && input_path) {
        free(owned_input);
        owned_input = NULL;
        if (lng_cli_read_file(input_path, &owned_input)) {
            fprintf(stderr, "lng: cannot read input file\n");
            return 1;
        }
        input = owned_input;
    }
    if (!input) {
        if (!owned_input && lng_cli_read_stream(stdin, &owned_input, NULL)) {
            fprintf(stderr, "lng: cannot read stdin\n");
            return 1;
        }
        input = owned_input;
    }

    memset(&options, 0, sizeof(options));
    if (has_threshold) options.threshold = &threshold;
    if (has_limit) options.limit = &limit;

    if (kc_lng_detect(input, &options, &results, &count) != KC_LNG_OK) {
        fprintf(stderr, "lng: language detection failed\n");
        free(owned_input);
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        printf("%s\t%.12g\n", results[i].code, results[i].score);
    }

    kc_lng_free(results);
    free(owned_input);
    return 0;
}

