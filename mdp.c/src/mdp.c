/**
 * mdp.c - Markdown Parser
 * Summary: Command line interface for the mdp tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Reads all data from stdin into a buffer.
 * @param out Pointer to receive the allocated buffer.
 * @return 0 on success, or -1 on failure.
 */
static int read_stdin(char **out) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    int ch;

    if (!out) return -1;
    *out = NULL;

    while ((ch = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            size_t next = cap ? cap * 2 : 4096;
            char *p = (char *)realloc(buf, next + 1);
            if (!p) { free(buf); return -1; }
            buf = p;
            cap = next;
        }
        buf[len++] = (char)ch;
    }
    if (ferror(stdin)) { free(buf); return -1; }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return -1;
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --html              Render Markdown body as HTML (default)\n");
    printf("    --body              Output body without frontmatter\n");
    printf("    --meta              Output raw frontmatter block\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_print_version(void) {
    printf("mdp build %llu\n", (unsigned long long)kc_mdp_version());
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    int mode = KC_MDP_MODE_HTML;
    char *src = NULL;
    kc_mdp_t *ctx = NULL;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_print_version();
            return 0;
        }

        {
            int next_mode = kc_mdp_mode(argv[i]);
            if (next_mode == KC_MDP_MODE_NONE) {
                fprintf(stderr, "mdp: unknown option '%s'\n", argv[i]);
                return 1;
            }
            mode = next_mode;
        }
    }

    if (read_stdin(&src) != 0) {
        fprintf(stderr, "mdp: failed to read input\n");
        return 1;
    }

    if (kc_mdp_open(&ctx) != KC_MDP_OK) {
        fprintf(stderr, "mdp: out of memory\n");
        kc_mdp_close(ctx);
        free(src);
        return 1;
    }
    kc_mdp_set_mode(ctx, mode);

    if (kc_mdp_exec(ctx, src, &out, &out_len) != KC_MDP_OK) {
        fprintf(stderr, "mdp: execution failed\n");
        kc_mdp_free(out);
        kc_mdp_close(ctx);
        free(src);
        return 1;
    }

    if (out != NULL) {
        fputs((const char *)out, stdout);
    }

    kc_mdp_free(out);
    kc_mdp_close(ctx);
    free(src);
    return 0;
}
