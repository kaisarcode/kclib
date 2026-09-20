/**
 * b64.c - Base64 encode/decode CLI
 * Summary: Thin CLI adapter for the b64 library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libb64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B64_BUF_SIZE 4096

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s <encode|decode> [text]\n", name);
    printf("       %s [--encode|-e|--decode|-d] [text]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("    encode              Encode text or stdin to base64\n");
    printf("    decode              Decode text or stdin to binary\n");
    printf("\n");
    printf("Options:\n");
    printf("    -e, --encode        Encode text or stdin to base64\n");
    printf("    -d, --decode        Decode text or stdin to binary\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
    printf("\n");
    printf("A text argument takes precedence over stdin.\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("b64 build %llu\n", (unsigned long long)kc_b64_version());
}

/**
 * Reads all of stdin into a malloc'd buffer.
 * @param out Receives the malloc'd buffer.
 * @param out_size Receives the buffer length.
 * @return 0 on success, -1 on allocation failure.
 */
static int read_stdin(char **out, size_t *out_size) {
    char *buf = NULL;
    size_t cap = 0, len = 0;
    char chunk[B64_BUF_SIZE];
    size_t n;

    while ((n = fread(chunk, 1, B64_BUF_SIZE, stdin)) > 0) {
        if (len + n > cap) {
            size_t new_cap = cap ? cap * 2 : B64_BUF_SIZE;
            char *tmp = realloc(buf, new_cap);
            if (tmp == NULL) { free(buf); return -1; }
            buf = tmp;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }

    if (len == cap) {
        char *tmp = realloc(buf, len + 1);
        if (tmp == NULL) { free(buf); return -1; }
        buf = tmp;
    }
    buf[len] = '\0';
    *out = buf;
    *out_size = len;
    return 0;
}

/**
 * Encodes text or stdin to base64 and writes to stdout.
 * @param text Optional text input.
 * @return 0 on success, 1 on failure.
 */
static int cmd_encode(const char *text) {
    char *input = NULL;
    size_t input_size = 0;
    char *encoded;

    if (text != NULL) {
        input = (char *)text;
        input_size = strlen(text);
    } else {
        if (read_stdin(&input, &input_size) != 0) {
            fprintf(stderr, "b64: read error\n");
            free(input);
            return 1;
        }
    }

    encoded = kc_b64_encode(input, input_size);
    if (text == NULL) free(input);

    if (encoded == NULL) {
        fprintf(stderr, "b64: encode error\n");
        return 1;
    }

    fwrite(encoded, 1, strlen(encoded), stdout);
    fputc('\n', stdout);
    kc_b64_free(encoded);
    return 0;
}

/**
 * Decodes text or stdin from base64 and writes binary to stdout.
 * @param text Optional base64 text input.
 * @return 0 on success, 1 on failure.
 */
static int cmd_decode(const char *text) {
    char *stdin_input = NULL;
    const char *input = text;
    size_t input_size = 0;
    void *decoded;
    size_t decoded_size;

    if (text == NULL) {
        if (read_stdin(&stdin_input, &input_size) != 0) {
            fprintf(stderr, "b64: read error\n");
            free(stdin_input);
            return 1;
        }
        input = stdin_input;

        if (input_size > 0 && stdin_input[input_size - 1] == '\n') {
            stdin_input[--input_size] = '\0';
        }
    }

    decoded = kc_b64_decode(input, &decoded_size);
    free(stdin_input);

    if (decoded == NULL) {
        fprintf(stderr, "b64: decode error\n");
        return 1;
    }

    fwrite(decoded, 1, decoded_size, stdout);
    kc_b64_free(decoded);
    return 0;
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    int i = 1;
    const char *cmd = NULL;
    const char *text = NULL;

    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--encode") == 0) {
            if (cmd != NULL) {
                fprintf(stderr, "b64: unexpected argument '%s'\n", argv[i]);
                return 1;
            }
            cmd = "encode";
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decode") == 0) {
            if (cmd != NULL) {
                fprintf(stderr, "b64: unexpected argument '%s'\n", argv[i]);
                return 1;
            }
            cmd = "decode";
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "b64: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            if (cmd == NULL) {
                cmd = argv[i];
            } else if (text == NULL) {
                text = argv[i];
            } else {
                fprintf(stderr, "b64: unexpected argument '%s'\n", argv[i]);
                return 1;
            }
        }
        i++;
    }

    if (cmd == NULL) {
        fprintf(stderr, "b64: missing command\n");
        kc_print_help(argv[0]);
        return 1;
    }

    if (strcmp(cmd, "encode") == 0) return cmd_encode(text);
    if (strcmp(cmd, "decode") == 0) return cmd_decode(text);

    fprintf(stderr, "b64: unknown command '%s'\n", cmd);
    kc_print_help(argv[0]);
    return 1;
}
