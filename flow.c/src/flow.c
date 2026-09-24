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

#define KC_FLOW_CLI_MAX_OVERLAYS 256

typedef enum {
    KC_FLOW_CLI_SET,
    KC_FLOW_CLI_UNSET
} kc_flow_cli_op_kind;

typedef struct {
    kc_flow_cli_op_kind kind;
    char *key;
    char *value;
} kc_flow_cli_op;

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
    kc_flow_cli_op ops[KC_FLOW_CLI_MAX_OVERLAYS];
    int op_count = 0;
    char *input = NULL;
    size_t input_size = 0;
    void *output = NULL;
    size_t output_size = 0;
    kc_flow_t *ctx = NULL;
    int i;
    int rc = 0;

    if (argc == 1) {
        kc_flow_cli_help(argv[0]);
        return 1;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_flow_cli_help(argv[0]);
            goto cleanup;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_flow_cli_version();
            goto cleanup;
        }
        if (strcmp(argv[i], "--link") == 0) {
            if (++i >= argc) {
                rc = kc_flow_cli_fail("missing value for --link");
                goto cleanup;
            }
            link = argv[i];
        } else if (strcmp(argv[i], "--set") == 0) {
            const char *eq;
            size_t key_size;
            if (++i >= argc) {
                rc = kc_flow_cli_fail("missing value for --set");
                goto cleanup;
            }
            eq = strchr(argv[i], '=');
            if (!eq || eq == argv[i]) {
                rc = kc_flow_cli_fail("invalid --set value");
                goto cleanup;
            }
            key_size = (size_t)(eq - argv[i]);
            if (key_size >= 256) {
                rc = kc_flow_cli_fail("overlay key too long");
                goto cleanup;
            }
            if (op_count >= KC_FLOW_CLI_MAX_OVERLAYS) {
                rc = kc_flow_cli_fail("too many overlays");
                goto cleanup;
            }
            ops[op_count].kind = KC_FLOW_CLI_SET;
            ops[op_count].key = (char *)malloc(key_size + 1);
            ops[op_count].value = strdup(eq + 1);
            if (ops[op_count].key == NULL || ops[op_count].value == NULL) {
                free(ops[op_count].key);
                free(ops[op_count].value);
                rc = kc_flow_cli_fail("allocation failure");
                goto cleanup;
            }
            memcpy(ops[op_count].key, argv[i], key_size);
            ops[op_count].key[key_size] = '\0';
            op_count++;
        } else if (strcmp(argv[i], "--unset") == 0) {
            if (++i >= argc) {
                rc = kc_flow_cli_fail("missing value for --unset");
                goto cleanup;
            }
            if (op_count >= KC_FLOW_CLI_MAX_OVERLAYS) {
                rc = kc_flow_cli_fail("too many overlays");
                goto cleanup;
            }
            ops[op_count].kind = KC_FLOW_CLI_UNSET;
            ops[op_count].key = strdup(argv[i]);
            ops[op_count].value = NULL;
            if (ops[op_count].key == NULL) {
                rc = kc_flow_cli_fail("allocation failure");
                goto cleanup;
            }
            op_count++;
        } else if (argv[i][0] == '-') {
            rc = kc_flow_cli_fail("unknown option");
            goto cleanup;
        } else if (run_path == NULL) {
            run_path = argv[i];
        } else {
            rc = kc_flow_cli_fail("unexpected positional argument");
            goto cleanup;
        }
    }

    if (!run_path) {
        rc = kc_flow_cli_fail("missing flow file");
        goto cleanup;
    }

    if (kc_flow_cli_read_input(&input, &input_size) != KC_FLOW_OK) {
        rc = kc_flow_cli_fail("unable to read stdin");
        goto cleanup;
    }

    if (kc_flow_open(&ctx, run_path) != KC_FLOW_OK) {
        rc = kc_flow_cli_fail("unable to open flow file");
        goto cleanup;
    }

    for (i = 0; i < op_count; i++) {
        if (ops[i].kind == KC_FLOW_CLI_SET) {
            if (kc_flow_set(ctx, ops[i].key, ops[i].value) != KC_FLOW_OK) {
                rc = kc_flow_cli_fail("invalid --set overlay");
                goto cleanup;
            }
        } else {
            if (kc_flow_unset(ctx, ops[i].key) != KC_FLOW_OK) {
                rc = kc_flow_cli_fail("invalid --unset overlay");
                goto cleanup;
            }
        }
    }

    rc = kc_flow_exec(ctx, link, input, input_size, &output, &output_size);

    if (rc != KC_FLOW_OK) {
        const char *err = kc_flow_error(ctx);
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
    for (i = 0; i < op_count; i++) {
        free(ops[i].key);
        free(ops[i].value);
    }
    if (input) free(input);
    if (output) kc_flow_free(output);
    kc_flow_close(ctx);
    return rc;
}
