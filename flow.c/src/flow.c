/**
 * flow.c - Flow command line interface.
 * Summary: Runs flat branch-oriented flow documents from the shell.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

/**
 * Read standard input into memory.
 * @param output Output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_cli_read_stdin(char **output, size_t *output_size) {
    char *data = NULL;
    size_t size = 0;
    size_t cap = 0;
    char chunk[4096];
    size_t n;

    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (size + n + 1 > cap) {
            char *next;
            cap = cap ? cap * 2 : 4096;
            while (cap < size + n + 1) {
                cap *= 2;
            }
            next = (char *)realloc(data, cap);
            if (!next) {
                free(data);
                return KC_FLOW_ERROR;
            }
            data = next;
        }
        memcpy(data + size, chunk, n);
        size += n;
    }
    if (ferror(stdin)) {
        free(data);
        return KC_FLOW_ERROR;
    }
    if (!data) {
        data = (char *)malloc(1);
        if (!data) {
            return KC_FLOW_ERROR;
        }
    }
    data[size] = '\0';
    *output = data;
    *output_size = size;
    return KC_FLOW_OK;
}

/**
 * Read standard input when the process receives piped data.
 * @param output Output buffer pointer.
 * @param output_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
static int kc_flow_cli_read_input(char **output, size_t *output_size) {
#ifndef _WIN32
    if (isatty(fileno(stdin))) {
#else
    if (_isatty(_fileno(stdin))) {
#endif
        char *empty;

        empty = (char *)malloc(1U);
        if (empty == NULL) {
            return KC_FLOW_ERROR;
        }

        empty[0] = '\0';
        *output = empty;
        *output_size = 0U;
        return KC_FLOW_OK;
    }

    return kc_flow_cli_read_stdin(output, output_size);
}

/**
 * Print command help.
 * @param name Program name.
 * @return None.
 */
static void kc_flow_cli_help(const char *name) {
    printf("Usage: %s file.flow [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --link <name>      Execute one explicit entry node\n");
    printf("    --set key=value    Append one overlay record\n");
    printf("    --unset <key>      Remove prior records for one key\n");

    printf("    -h, --help         Show this help\n");
    printf("    -v, --version      Show version\n");
}

/**
 * Print command version.
 * @return None.
 */
static void kc_flow_cli_version(void) {
    printf("flow build %llu\n", (unsigned long long)kc_flow_version());
}

/**
 * Print one command failure.
 * @param message Error message.
 * @return Process failure status.
 */
static int kc_flow_cli_fail(const char *message) {
    fprintf(stderr, "flow: %s\n", message);
    return 1;
}

/**
 * Program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    const char *run_path = NULL;
    const char *link = NULL;
    char *set_keys[256];
    char *set_vals[256];
    int set_count = 0;
    char *unset_keys[256];
    int unset_count = 0;
    char *input = NULL;
    size_t input_size = 0;
    char *output = NULL;
    size_t output_size = 0;
    kc_flow_options_t opts;
    kc_flow_t *ctx = NULL;
    int i;
    int rc;

    if (argc == 1) {
        kc_flow_cli_help(argv[0]);
        return 1;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_flow_cli_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_flow_cli_version();
            return 0;
        }
        if (strcmp(argv[i], "--link") == 0) {
            if (++i >= argc) {
                return kc_flow_cli_fail("missing value for --link");
            }
            link = argv[i];
        } else if (strcmp(argv[i], "--set") == 0) {
            const char *eq;
            size_t key_size;
            if (++i >= argc) {
                return kc_flow_cli_fail("missing value for --set");
            }
            eq = strchr(argv[i], '=');
            if (!eq || eq == argv[i]) {
                return kc_flow_cli_fail("invalid --set value");
            }
            key_size = (size_t)(eq - argv[i]);
            if (key_size >= 256) {
                return kc_flow_cli_fail("overlay key too long");
            }
            set_keys[set_count] = (char *)malloc(key_size + 1);
            set_vals[set_count] = strdup(eq + 1);
            if (set_keys[set_count] == NULL || set_vals[set_count] == NULL) {
                free(set_keys[set_count]);
                free(set_vals[set_count]);
                return kc_flow_cli_fail("allocation failure");
            }
            memcpy(set_keys[set_count], argv[i], key_size);
            set_keys[set_count][key_size] = '\0';
            set_count++;
        } else if (strcmp(argv[i], "--unset") == 0) {
            if (++i >= argc) {
                return kc_flow_cli_fail("missing value for --unset");
            }
            unset_keys[unset_count] = strdup(argv[i]);
            if (unset_keys[unset_count] == NULL) {
                return kc_flow_cli_fail("allocation failure");
            }
            unset_count++;
        } else if (argv[i][0] == '-') {
            return kc_flow_cli_fail("unknown option");
        } else if (run_path == NULL) {
            run_path = argv[i];
        } else {
            return kc_flow_cli_fail("unexpected positional argument");
        }
    }

    if (!run_path) {
        return kc_flow_cli_fail("missing flow file");
    }

    if (kc_flow_cli_read_input(&input, &input_size) != KC_FLOW_OK) {
        for (i = 0; i < set_count; i++) { free(set_keys[i]); free(set_vals[i]); }
        for (i = 0; i < unset_count; i++) free(unset_keys[i]);
        return kc_flow_cli_fail("unable to read stdin");
    }

    opts = kc_flow_options_default();
    if (kc_flow_open(&ctx, &opts) != KC_FLOW_OK) {
        free(input);
        for (i = 0; i < set_count; i++) { free(set_keys[i]); free(set_vals[i]); }
        for (i = 0; i < unset_count; i++) free(unset_keys[i]);
        return kc_flow_cli_fail("allocation failure");
    }

    for (i = 0; i < set_count; i++) {
        if (kc_flow_set(ctx, set_keys[i], set_vals[i]) != KC_FLOW_OK) {
            rc = kc_flow_cli_fail("invalid --set overlay");
            goto cleanup;
        }
    }

    for (i = 0; i < unset_count; i++) {
        if (kc_flow_unset(ctx, unset_keys[i]) != KC_FLOW_OK) {
            rc = kc_flow_cli_fail("invalid --unset overlay");
            goto cleanup;
        }
    }

    if (link) {
        rc = kc_flow_exec_entry(ctx, run_path, link, input, input_size, &output, &output_size);
    } else {
        rc = kc_flow_exec(ctx, run_path, input, input_size, &output, &output_size);
    }

    free(input);
    for (i = 0; i < set_count; i++) { free(set_keys[i]); free(set_vals[i]); }
    for (i = 0; i < unset_count; i++) free(unset_keys[i]);

    if (rc != KC_FLOW_OK) {
        const char *err = kc_flow_strerror(ctx);
        fprintf(stderr, "flow: %s\n", err ? err : "execution failed");
        rc = 1;
        goto cleanup;
    }

    if (output_size > 0 && output != NULL) {
        if (fwrite(output, 1, output_size, stdout) != output_size) {
            rc = 1;
            goto cleanup;
        }
    }

    rc = 0;

cleanup:
    if (output) kc_flow_free(output);
    kc_flow_close(ctx);
    return rc;
}
