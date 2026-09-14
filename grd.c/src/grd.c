/**
 * grd.c - Grid | Region | Divide
 * Summary: CLI for computing proportional grid splits.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libgrd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Prints usage help to stdout.
 * @param name Argv[0] program name.
 * @return void
 */
static void kc_grd_help(const char *name) {
    printf("Usage:\n");
    printf("  %s split [options]\n\n", name);
    printf("Options:\n");
    printf("  --width,   -w <n>      Root width in pixels (required)\n");
    printf("  --height,  -H <n>      Root height in pixels (required)\n");
    printf("  --kind,    -k row|col  Split direction (default: row)\n");
    printf("  --weights, -W <...>    Space or comma separated weights (required, min 2)\n");
    printf("  --gap,     -g <n>      Gap between children in pixels (default: 0)\n");
    printf("  --min,     -m <n>      Minimum child size in pixels (default: 1)\n");
    printf("  --help,    -h          Show help\n");
    printf("  -v, --version          Show version\n\n");
    printf("Output:\n");
    printf("  One line per child: index x y w h\n\n");
    printf("Examples:\n");
    printf("  %s split -w 1920 -H 1080 -k row -W \"1 2 1\"\n", name);
    printf("  %s split -w 800 -H 600 -k col -W \"1 1 1 1\" -g 4\n", name);
}

/**
 * Prints the binary version string to stdout.
 * @return void
 */
static void kc_grd_cli_version(void) {
    printf("grd build %llu\n", (unsigned long long)kc_grd_version());
}

/**
 * Prints an error message and usage help to stderr, then returns 1.
 * @param name Argv[0] program name.
 * @param message Error description.
 * @return 1 always.
 */
static int kc_grd_fail_usage(const char *name, const char *message) {
    fprintf(stderr, "grd: %s\n\n", message);
    kc_grd_help(name);
    return 1;
}

/**
 * Parses a decimal integer string into an int.
 * @param text Input string.
 * @param out Output int pointer.
 * @return 1 on success, 0 on failure.
 */
static int kc_grd_parse_int(const char *text, int *out) {
    char *end;
    long value;
    if (!text || !out) return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return 0;
    if (value < -2147483647L - 1L || value > 2147483647L) return 0;
    *out = (int)value;
    return 1;
}

/**
 * Parses a float string into a float.
 * @param text Input string.
 * @param out Output float pointer.
 * @return 1 on success, 0 on failure.
 */
static int kc_grd_parse_float(const char *text, float *out) {
    char *end;
    float value;
    if (!text || !out) return 0;
    errno = 0;
    value = strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0') return 0;
    *out = value;
    return 1;
}

/**
 * Parses space or comma separated positive float weights into an array.
 * @param text Input string.
 * @param weights Output float array.
 * @param cap Maximum number of weights.
 * @return Number of weights parsed, or -1 on failure.
 */
static int kc_grd_parse_weights(const char *text, float *weights, int cap) {
    char buffer[4096];
    char *token;
    char *state;
    int count;
    size_t i;

    if (!text || !weights || cap <= 0) return -1;
    if (strlen(text) >= sizeof(buffer)) return -1;

    memcpy(buffer, text, strlen(text) + 1);
    for (i = 0; buffer[i]; i++) {
        if (buffer[i] == ',') buffer[i] = ' ';
    }

    count = 0;
    token = strtok_r(buffer, " \t\r\n", &state);
    while (token) {
        float w;
        if (count >= cap) return -1;
        if (!kc_grd_parse_float(token, &w) || w <= 0.0f) return -1;
        weights[count++] = w;
        token = strtok_r(NULL, " \t\r\n", &state);
    }

    return count;
}

/**
 * Executes the split command using the C API directly.
 * @param width Root width in pixels.
 * @param height Root height in pixels.
 * @param kind Split direction.
 * @param weights Array of proportional weights.
 * @param weight_count Number of weights.
 * @param gap Gap between children in pixels.
 * @param min_px Minimum child size in pixels.
 * @return 0 on success, 1 on error.
 */
static int kc_grd_cmd_split(
    int width, int height,
    kc_grd_kind_t kind, float *weights, int weight_count,
    int gap, int min_px
) {
    kc_grd_box_t *root = kc_grd_box_new();
    if (root == NULL) {
        fprintf(stderr, "grd: allocation failed\n");
        return 1;
    }
    root->border = 0;
    root->padding = 0;

    kc_grd_split_t *s = kc_grd_split_set(root, kind);
    if (s == NULL) {
        kc_grd_box_free(root);
        fprintf(stderr, "grd: allocation failed\n");
        return 1;
    }

    kc_grd_split_gap(s, gap, min_px);

    int i;
    for (i = 0; i < weight_count; i++) {
        kc_grd_box_t *child = kc_grd_box_new();
        if (child == NULL) {
            kc_grd_box_free(root);
            fprintf(stderr, "grd: allocation failed\n");
            return 1;
        }
        child->border = 0;
        child->padding = 0;
        if (kc_grd_split_add(s, child, weights[i]) != 0) {
            kc_grd_box_free(child);
            kc_grd_box_free(root);
            fprintf(stderr, "grd: split add failed\n");
            return 1;
        }
    }

    kc_grd_box_bounds(root, 0, 0, width, height);
    kc_grd_box_layout(root);

    for (i = 0; i < s->count; i++) {
        kc_grd_box_t *c = kc_grd_split_at(s, i);
        printf("%d %d %d %d %d\n", i, c->x, c->y, c->w, c->h);
    }

    kc_grd_box_free(root);
    return 0;
}

/**
 * Entry point. Parses arguments and dispatches subcommands.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    int i;
    kc_grd_options_t opts = kc_grd_options_default();
    opts.min_px = 1;
    kc_grd_options_load_env(&opts);
    float weights[KC_GRD_WEIGHTS_CAP];
    int weight_count = 0;

    if (argc < 2) {
        kc_grd_help(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        kc_grd_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        kc_grd_cli_version();
        return 0;
    }

    if (strcmp(argv[1], "split") != 0) {
        return kc_grd_fail_usage(argv[0], "Unknown command.");
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            kc_grd_help(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            kc_grd_cli_version();
            return 0;
        }

        if (strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-w") == 0) {
            if (i + 1 >= argc || !kc_grd_parse_int(argv[i + 1], &opts.width) || opts.width <= 0) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Invalid value for --width.");
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-H") == 0) {
            if (i + 1 >= argc || !kc_grd_parse_int(argv[i + 1], &opts.height) || opts.height <= 0) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Invalid value for --height.");
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "--kind") == 0 || strcmp(argv[i], "-k") == 0) {
            if (i + 1 >= argc) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Missing value for --kind.");
            }
            free(opts.kind);
            opts.kind = strdup(argv[i + 1]);
            i++;
            continue;
        }

        if (strcmp(argv[i], "--weights") == 0 || strcmp(argv[i], "-W") == 0) {
            if (i + 1 >= argc) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Missing value for --weights.");
            }
            free(opts.weights);
            opts.weights = strdup(argv[i + 1]);
            i++;
            continue;
        }

        if (strcmp(argv[i], "--gap") == 0 || strcmp(argv[i], "-g") == 0) {
            if (i + 1 >= argc || !kc_grd_parse_int(argv[i + 1], &opts.gap) || opts.gap < 0) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Invalid value for --gap.");
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "--min") == 0 || strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc || !kc_grd_parse_int(argv[i + 1], &opts.min_px) || opts.min_px < 0) {
                kc_grd_options_free(&opts);
                return kc_grd_fail_usage(argv[0], "Invalid value for --min.");
            }
            i++;
            continue;
        }

        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "Unknown argument.");
    }

    if (opts.width <= 0) {
        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "Missing required --width.");
    }
    if (opts.height <= 0) {
        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "Missing required --height.");
    }
    if (!opts.weights) {
        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "Missing required --weights.");
    }

    weight_count = kc_grd_parse_weights(opts.weights, weights, KC_GRD_WEIGHTS_CAP);
    if (weight_count < 2) {
        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "At least two weights are required.");
    }

    kc_grd_kind_t k = KC_GRD_ROW;
    if (opts.kind && strcmp(opts.kind, "col") == 0) {
        k = KC_GRD_COL;
    } else if (opts.kind && strcmp(opts.kind, "row") != 0) {
        kc_grd_options_free(&opts);
        return kc_grd_fail_usage(argv[0], "Invalid value for --kind. Use row or col.");
    }

    int rc = kc_grd_cmd_split(opts.width, opts.height, k, weights, weight_count, opts.gap, opts.min_px);
    kc_grd_options_free(&opts);
    return rc;
}
