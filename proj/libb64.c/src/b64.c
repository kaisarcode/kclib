/**
 * b64.c - Base64 command-line interface.
 * Summary: Encodes or decodes Base64 with libb64.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libb64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define B64_MODE_NONE   0
#define B64_MODE_ENCODE 1
#define B64_MODE_DECODE 2

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void b64_cli_usage(const char *name)
{
    printf("Usage: %s -e [text] [options]\n", name);
    printf("       %s -d [base64] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -e, --encode        Encode as Base64\n");
    printf("  -d, --decode        Decode Base64\n");
    printf("  -i, --input <file>  Read input from file\n");
    printf("  -h, --help          Show this help\n");
    printf("  -v, --version       Show version\n");
}

/**
 * Reads all bytes from one stream.
 * @param stream Input stream.
 * @param out Receives allocated data.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int b64_cli_read_stream(
    FILE *stream,
    unsigned char **out,
    size_t *out_size
)
{
    unsigned char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    unsigned char chunk[4096];
    size_t count;

    *out = NULL;
    *out_size = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required = size + count;
        unsigned char *next;

        if (required > capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 4096;

            while (next_capacity < required) next_capacity *= 2;
            next = (unsigned char *)realloc(buffer, next_capacity);
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
        buffer = (unsigned char *)malloc(1);
        if (!buffer) return 1;
    }
    *out = buffer;
    *out_size = size;
    return 0;
}

/**
 * Reads one input file as raw bytes.
 * @param path Input file path.
 * @param out Receives allocated data.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int b64_cli_read_file(
    const char *path,
    unsigned char **out,
    size_t *out_size
)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = b64_cli_read_stream(file, out, out_size);
    fclose(file);
    return status;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int b64_cli_stdin_is_tty(void)
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
 * Runs the b64 CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *text = NULL;
    const char *input_path = NULL;
    unsigned char *owned_input = NULL;
    const unsigned char *input = NULL;
    size_t input_size = 0;
    int mode = B64_MODE_NONE;

#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1)
    {
        fprintf(stderr, "b64: cannot configure binary I/O\n");
        return 1;
    }
#endif

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        b64_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_b64_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encode") == 0) {
            if (mode != B64_MODE_NONE) {
                fprintf(stderr, "b64: choose exactly one of --encode or --decode\n");
                return 1;
            }
            mode = B64_MODE_ENCODE;
        } else if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--decode") == 0)
        {
            if (mode != B64_MODE_NONE) {
                fprintf(stderr, "b64: choose exactly one of --encode or --decode\n");
                return 1;
            }
            mode = B64_MODE_DECODE;
        } else if (strcmp(argv[i], "-i") == 0 ||
            strcmp(argv[i], "--input") == 0)
        {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "b64: --input requires a file\n");
                return 1;
            }
            if (input_path != NULL) {
                fprintf(stderr, "b64: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "b64: help/version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "b64: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (text != NULL) {
            fprintf(stderr, "b64: only one text argument is accepted\n");
            return 1;
        } else {
            text = argv[i];
        }
    }

    if (mode == B64_MODE_NONE) {
        fprintf(stderr, "b64: -e/--encode or -d/--decode is required\n");
        return 1;
    }

    if (text) {
        input = (const unsigned char *)text;
        input_size = strlen(text);
    } else if (!b64_cli_stdin_is_tty()) {
        if (b64_cli_read_stream(stdin, &owned_input, &input_size)) {
            fprintf(stderr, "b64: cannot read stdin\n");
            return 1;
        }
        if (input_size > 0) input = owned_input;
    }
    if (!input && input_path) {
        free(owned_input);
        owned_input = NULL;
        if (b64_cli_read_file(input_path, &owned_input, &input_size)) {
            fprintf(stderr, "b64: cannot read input file\n");
            return 1;
        }
        input = owned_input;
    }
    if (!input) {
        if (!owned_input &&
            b64_cli_read_stream(stdin, &owned_input, &input_size))
        {
            fprintf(stderr, "b64: cannot read stdin\n");
            return 1;
        }
        input = owned_input;
    }

    if (mode == B64_MODE_DECODE) {
        char *encoded = (char *)malloc(input_size + 1);
        void *decoded;
        size_t decoded_size = 0;

        if (!encoded) {
            free(owned_input);
            return 1;
        }
        memcpy(encoded, input, input_size);
        encoded[input_size] = '\0';
        decoded = kc_b64_decode(encoded, &decoded_size);
        free(encoded);
        free(owned_input);
        if (!decoded) {
            fprintf(stderr, "b64: invalid base64 input\n");
            return 1;
        }
        if (decoded_size > 0 &&
            fwrite(decoded, 1, decoded_size, stdout) != decoded_size)
        {
            kc_b64_free(decoded);
            fprintf(stderr, "b64: cannot write output\n");
            return 1;
        }
        kc_b64_free(decoded);
        return 0;
    }

    {
        char *encoded = kc_b64_encode(input, input_size);

        free(owned_input);
        if (!encoded) {
            fprintf(stderr, "b64: encoding failed\n");
            return 1;
        }
        fputs(encoded, stdout);
        kc_b64_free(encoded);
    }
    return 0;
}
