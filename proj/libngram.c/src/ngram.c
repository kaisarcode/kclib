/**
 * ngram.c - Sliding-window n-gram command-line interface.
 * Summary: Traverses text chunks and optionally filters descending spans.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libngram.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct {
    const char *filter;
    unsigned long filter_index;
    int filter_error;
} ngram_cli_context_t;

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void ngram_cli_usage(const char *name)
{
    printf("Usage: %s [text] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>       Read text from file\n");
    printf("  -f, --filter <cmd>       Run a filter command for each chunk\n");
    printf("      --max <N>            Maximum tokens per chunk (default: 10)\n");
    printf("      --min <N>            Minimum tokens per chunk (default: 1)\n");
    printf("  -s, --separators <chars> Token separator characters\n");
    printf("  -h, --help               Show this help\n");
    printf("  -v, --version            Show version\n");
}

/**
 * Parses one size_t value.
 * @param text Input string.
 * @param out Parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int ngram_cli_parse_size(const char *text, size_t *out)
{
    const unsigned char *cursor;
    unsigned long long value;
    char *end = NULL;

    if (!text || !out) return 1;

    cursor = (const unsigned char *)text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
        *cursor == '\n' || *cursor == '\v' || *cursor == '\f')
    {
        cursor++;
    }
    if (*cursor == '-') return 1;

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value > (unsigned long long)SIZE_MAX)
    {
        return 1;
    }

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
static int ngram_cli_read_stream(FILE *stream, char **out, size_t *out_size)
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
 * Reads one text file.
 * @param path Input file path.
 * @param out Receives allocated string.
 * @return Zero on success, nonzero on failure.
 */
static int ngram_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = ngram_cli_read_stream(file, out, NULL);
    fclose(file);
    return status;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int ngram_cli_stdin_is_tty(void)
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
 * Runs one filter command and reports whether it wrote to stdout.
 * @param command Filter command.
 * @param chunk Current n-gram chunk.
 * @param index Per-process temporary-file sequence.
 * @return One when stdout was produced, zero when empty, negative on failure.
 */
static int ngram_cli_run_filter(
    const char *command,
    const kc_ngram_chunk_t *chunk,
    unsigned long index
)
{
    char path[128];
    char shell[4096];
    char buffer[512];
    FILE *input;
    FILE *pipe_stream;
    size_t count;
    int has_output = 0;
    int close_status;
    int length;

#ifdef _WIN32
    unsigned long process_id = (unsigned long)GetCurrentProcessId();
#else
    unsigned long process_id = (unsigned long)getpid();
#endif

    if (!command || !*command || !chunk) return -1;

    length = snprintf(path, sizeof(path),
        ".ngram-filter-%lu-%lu.tmp", process_id, index);
    if (length < 0 || (size_t)length >= sizeof(path)) return -1;

    input = fopen(path, "wb");
    if (!input) return -1;
    if (chunk->data_size > 0 &&
        fwrite(chunk->data, 1, chunk->data_size, input) != chunk->data_size)
    {
        fclose(input);
        remove(path);
        return -1;
    }
    if (fputc('\n', input) == EOF || fclose(input) != 0) {
        remove(path);
        return -1;
    }

    length = snprintf(shell, sizeof(shell), "%s < \"%s\"", command, path);
    if (length < 0 || (size_t)length >= sizeof(shell)) {
        remove(path);
        return -1;
    }

#ifdef _WIN32
    pipe_stream = _popen(shell, "r");
#else
    pipe_stream = popen(shell, "r");
#endif
    if (!pipe_stream) {
        remove(path);
        return -1;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), pipe_stream)) > 0) {
        has_output = 1;
    }
    if (ferror(pipe_stream)) {
#ifdef _WIN32
        _pclose(pipe_stream);
#else
        pclose(pipe_stream);
#endif
        remove(path);
        return -1;
    }

#ifdef _WIN32
    close_status = _pclose(pipe_stream);
#else
    close_status = pclose(pipe_stream);
#endif
    remove(path);
    if (close_status == -1) return -1;

    return has_output;
}

/**
 * Handles one visited chunk.
 * @param chunk Current chunk.
 * @param userdata CLI callback context.
 * @return Zero to continue, one to close the span, or negative to abort.
 */
static int ngram_cli_visit(const kc_ngram_chunk_t *chunk, void *userdata)
{
    ngram_cli_context_t *context = (ngram_cli_context_t *)userdata;
    int decision;

    if (!chunk || !context) return -1;

    if (chunk->data_size > 0 &&
        fwrite(chunk->data, 1, chunk->data_size, stdout) != chunk->data_size)
    {
        return -1;
    }
    if (fputc('\n', stdout) == EOF) return -1;

    if (!context->filter) return 0;

    decision = ngram_cli_run_filter(
        context->filter,
        chunk,
        context->filter_index++
    );
    if (decision < 0) {
        context->filter_error = 1;
        return -1;
    }

    return decision ? 1 : 0;
}

/**
 * Runs the ngram CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_ngram_options_t options;
    ngram_cli_context_t context;
    const char *text = NULL;
    const char *input_path = NULL;
    const char *filter = NULL;
    const char *separators = NULL;
    char *owned_input = NULL;
    const char *input = NULL;
    size_t max_tokens = 0;
    size_t min_tokens = 0;
    int has_max = 0;
    int has_min = 0;
    int status;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        ngram_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_ngram_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "ngram: --input requires a file\n");
                return 1;
            }
            if (input_path) {
                fprintf(stderr, "ngram: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-f") == 0 ||
            strcmp(argv[i], "--filter") == 0)
        {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "ngram: --filter requires a command\n");
                return 1;
            }
            if (filter) {
                fprintf(stderr, "ngram: only one filter command is accepted\n");
                return 1;
            }
            filter = argv[i];
        } else if (strcmp(argv[i], "--max") == 0) {
            if (++i >= argc || ngram_cli_parse_size(argv[i], &max_tokens)) {
                fprintf(stderr, "ngram: --max requires a non-negative integer\n");
                return 1;
            }
            has_max = 1;
        } else if (strcmp(argv[i], "--min") == 0) {
            if (++i >= argc || ngram_cli_parse_size(argv[i], &min_tokens) ||
                min_tokens == 0)
            {
                fprintf(stderr, "ngram: --min requires a positive integer\n");
                return 1;
            }
            has_min = 1;
        } else if (strcmp(argv[i], "-s") == 0 ||
            strcmp(argv[i], "--separators") == 0)
        {
            if (++i >= argc) {
                fprintf(stderr, "ngram: --separators requires a value\n");
                return 1;
            }
            separators = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "ngram: -h/--help and -v/--version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "ngram: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (!text) {
            text = argv[i];
        } else {
            fprintf(stderr, "ngram: only one text positional argument is accepted\n");
            return 1;
        }
    }

    if (has_max && has_min && max_tokens != 0 && max_tokens < min_tokens) {
        fprintf(stderr, "ngram: --max must be zero or greater than or equal to --min\n");
        return 1;
    }

    if (text) {
        input = text;
    } else if (!ngram_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (ngram_cli_read_stream(stdin, &owned_input, &stdin_size)) {
            fprintf(stderr, "ngram: cannot read stdin\n");
            return 1;
        }
        if (stdin_size > 0) input = owned_input;
    }

    if (!input && input_path) {
        free(owned_input);
        owned_input = NULL;
        if (ngram_cli_read_file(input_path, &owned_input)) {
            fprintf(stderr, "ngram: cannot read input file\n");
            return 1;
        }
        input = owned_input;
    }

    if (!input) {
        if (!owned_input && ngram_cli_read_stream(stdin, &owned_input, NULL)) {
            fprintf(stderr, "ngram: cannot read stdin\n");
            return 1;
        }
        input = owned_input;
    }

    memset(&options, 0, sizeof(options));
    if (has_max) options.max_tokens = &max_tokens;
    if (has_min) options.min_tokens = &min_tokens;
    if (separators) options.separators = separators;

    memset(&context, 0, sizeof(context));
    context.filter = filter;

    status = kc_ngram_traverse(input, &options, ngram_cli_visit, &context);
    free(owned_input);

    if (status == KC_NGRAM_OK) return 0;
    if (context.filter_error) {
        fprintf(stderr, "ngram: filter command failed\n");
    } else {
        fprintf(stderr, "ngram: traversal failed\n");
    }
    return 1;
}
