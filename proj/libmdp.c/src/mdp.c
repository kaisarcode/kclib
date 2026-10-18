/**
 * mdp.c - Markdown parser command-line interface.
 * Summary: Renders Markdown HTML, ANSI, body, or metadata with libmdp.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libmdp.h"

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

#define MDP_MODE_HTML 0
#define MDP_MODE_ANSI 1
#define MDP_MODE_BODY 2
#define MDP_MODE_META 3

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void mdp_cli_usage(const char *name)
{
    printf("Usage: %s [text] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>  Read Markdown from file when stdin is absent\n");
    printf("  --ansi              Render Markdown for ANSI terminals\n");
    printf("  --body              Print Markdown body without frontmatter\n");
    printf("  --meta              Print raw frontmatter\n");
    printf("  -h, --help          Show this help\n");
    printf("  -v, --version       Show version\n");
}

/**
 * Reads all bytes from one stream as a NUL-terminated string.
 * @param file Input stream.
 * @param out Receives allocated text.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int mdp_cli_read_stream(FILE *file, char **out, size_t *out_size)
{
    char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    char chunk[4096];
    size_t count;

    *out = NULL;
    if (out_size) *out_size = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) {
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
    if (ferror(file)) {
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
 * Reads all bytes from one file path.
 * @param path Input file path.
 * @param out Receives allocated text.
 * @return Zero on success, nonzero on failure.
 */
static int mdp_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int rc;

    file = fopen(path, "rb");
    if (!file) return 1;
    rc = mdp_cli_read_stream(file, out, NULL);
    fclose(file);
    return rc;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int mdp_cli_stdin_is_tty(void)
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (handle == NULL || handle == INVALID_HANDLE_VALUE) return 0;
    return GetConsoleMode(handle, &mode) != 0;
#else
    return isatty(STDIN_FILENO);
#endif
}

/**
 * Runs the mdp CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *text = NULL;
    const char *input_path = NULL;
    char *owned_input = NULL;
    const char *input;
    const char *output;
    kc_mdp_t *mdp = NULL;
    int mode = MDP_MODE_HTML;

#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        fprintf(stderr, "mdp: cannot configure stdout\n");
        return 1;
    }
#endif

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        mdp_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_mdp_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "mdp: --input requires a file\n");
                return 1;
            }
            if (input_path != NULL) {
                fprintf(stderr, "mdp: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "--ansi") == 0) {
            if (mode != MDP_MODE_HTML) {
                fprintf(stderr, "mdp: choose only one output mode\n");
                return 1;
            }
            mode = MDP_MODE_ANSI;
        } else if (strcmp(argv[i], "--body") == 0) {
            if (mode != MDP_MODE_HTML) {
                fprintf(stderr, "mdp: choose only one output mode\n");
                return 1;
            }
            mode = MDP_MODE_BODY;
        } else if (strcmp(argv[i], "--meta") == 0) {
            if (mode != MDP_MODE_HTML) {
                fprintf(stderr, "mdp: choose only one output mode\n");
                return 1;
            }
            mode = MDP_MODE_META;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "mdp: help/version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mdp: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (text != NULL) {
            fprintf(stderr, "mdp: only one text argument is accepted\n");
            return 1;
        } else {
            text = argv[i];
        }
    }

    if (text) {
        input = text;
    } else if (input_path != NULL && !mdp_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (mdp_cli_read_stream(stdin, &owned_input, &stdin_size)) {
            fprintf(stderr, "mdp: cannot read stdin\n");
            return 1;
        }
        if (stdin_size > 0) {
            input = owned_input;
        } else {
            free(owned_input);
            owned_input = NULL;
            if (mdp_cli_read_file(input_path, &owned_input)) {
                fprintf(stderr, "mdp: cannot read input file\n");
                return 1;
            }
            input = owned_input;
        }
    } else if (input_path != NULL) {
        if (mdp_cli_read_file(input_path, &owned_input)) {
            fprintf(stderr, "mdp: cannot read input file\n");
            return 1;
        }
        input = owned_input;
    } else {
        if (mdp_cli_read_stream(stdin, &owned_input, NULL)) {
            fprintf(stderr, "mdp: cannot read stdin\n");
            return 1;
        }
        input = owned_input;
    }

    if (kc_mdp_open(&mdp, input) != KC_MDP_OK) {
        free(owned_input);
        fprintf(stderr, "mdp: cannot parse markdown\n");
        return 1;
    }
    free(owned_input);

    if (mode == MDP_MODE_ANSI) {
        output = kc_mdp_ansi(mdp);
    } else if (mode == MDP_MODE_BODY) {
        output = kc_mdp_body(mdp);
    } else if (mode == MDP_MODE_META) {
        output = kc_mdp_meta(mdp);
    } else {
        output = kc_mdp_html(mdp);
    }

    if (!output) {
        kc_mdp_close(mdp);
        fprintf(stderr, "mdp: output failed\n");
        return 1;
    }

    fputs(output, stdout);
    kc_mdp_close(mdp);
    return 0;
}
