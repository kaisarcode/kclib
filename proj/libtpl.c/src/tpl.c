/**
 * tpl.c - Template renderer command-line interface.
 * Summary: Renders one template with variables through libtpl.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libtpl.h"

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
static void tpl_cli_usage(const char *name)
{
    printf("Usage: %s [template] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>      Read template from file\n");
    printf("  -r, --root <path>       Include root (default: .)\n");
    printf("  -v, --var <key> <value> Set a template variable; repeatable\n");
    printf("  -h, --help              Show this help\n");
    printf("  -V, --version           Show version\n");
}

/**
 * Reads all bytes from one stream as a NUL-terminated string.
 * @param stream Input stream.
 * @param out Receives allocated string.
 * @param out_size Receives byte count when non-NULL.
 * @return Zero on success, nonzero on failure.
 */
static int tpl_cli_read_stream(FILE *stream, char **out, size_t *out_size)
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
 * Reads one template source from a file.
 * @param path File path.
 * @param out Receives allocated template source.
 * @return Zero on success, nonzero on failure.
 */
static int tpl_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = tpl_cli_read_stream(file, out, NULL);
    fclose(file);
    return status;
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int tpl_cli_stdin_is_tty(void)
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
 * Copies one literal template argument into owned storage.
 * @param text Literal template text.
 * @param out Receives allocated copy.
 * @return Zero on success, nonzero on allocation failure.
 */
static int tpl_cli_copy_text(const char *text, char **out)
{
    size_t size = strlen(text) + 1;

    *out = (char *)malloc(size);
    if (!*out) return 1;
    memcpy(*out, text, size);
    return 0;
}

/**
 * Runs the tpl CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *template_text = NULL;
    const char *input_path = NULL;
    const char *root = NULL;
    kc_tpl_var_t *vars = NULL;
    size_t var_count = 0;
    char *source = NULL;
    char *output = NULL;
    kc_tpl_t *tpl = NULL;
    kc_tpl_options_t options;
    int result = 1;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        tpl_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-V") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_tpl_version());
        return 0;
    }

    vars = (kc_tpl_var_t *)calloc((size_t)argc, sizeof(*vars));
    if (!vars) return 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tpl: --input requires a file\n");
                goto done;
            }
            if (input_path != NULL) {
                fprintf(stderr, "tpl: only one input file is accepted\n");
                goto done;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-r") == 0 ||
            strcmp(argv[i], "--root") == 0)
        {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "tpl: --root requires a path\n");
                goto done;
            }
            root = argv[i];
        } else if (strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--var") == 0)
        {
            if (i + 2 >= argc || !argv[i + 1][0]) {
                fprintf(stderr, "tpl: --var requires <key> <value>\n");
                goto done;
            }
            vars[var_count].key = argv[i + 1];
            vars[var_count].value = argv[i + 2];
            var_count++;
            i += 2;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-V") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr,
                "tpl: help and version options must be used alone\n");
            goto done;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "tpl: unknown option '%s'\n", argv[i]);
            goto done;
        } else if (template_text != NULL) {
            fprintf(stderr, "tpl: only one template argument is accepted\n");
            goto done;
        } else {
            template_text = argv[i];
        }
    }

    if (template_text) {
        if (tpl_cli_copy_text(template_text, &source)) goto done;
    } else if (!tpl_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (tpl_cli_read_stream(stdin, &source, &stdin_size)) {
            fprintf(stderr, "tpl: cannot read stdin\n");
            goto done;
        }
        if (stdin_size == 0) {
            free(source);
            source = NULL;
        }
    }
    if (!source && input_path) {
        if (tpl_cli_read_file(input_path, &source)) {
            fprintf(stderr, "tpl: cannot read input file\n");
            goto done;
        }
    }
    if (!source) {
        fprintf(stderr, "tpl: missing template input\n");
        goto done;
    }

    memset(&options, 0, sizeof(options));
    options.root = root;
    if (kc_tpl_open(&tpl, source, &options) != KC_TPL_OK) {
        fprintf(stderr, "tpl: %s\n", kc_tpl_error(tpl));
        goto done;
    }

    output = kc_tpl_render(tpl, vars, var_count);
    if (!output) {
        fprintf(stderr, "tpl: %s\n", kc_tpl_error(tpl));
        goto done;
    }
    if (fputs(output, stdout) == EOF) {
        fprintf(stderr, "tpl: cannot write output\n");
        goto done;
    }
    result = 0;

done:
    kc_tpl_free(output);
    kc_tpl_close(tpl);
    free(source);
    free(vars);
    return result;
}
