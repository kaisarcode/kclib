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
#include <math.h>
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
    printf("  %s layout [options] < tree.txt\n\n", name);
    printf("Options:\n");
    printf("  --width,   -w <n>      Root width in pixels (required)\n");
    printf("  --height,  -H <n>      Root height in pixels (required)\n");
    printf("  --kind,    -k row|col  Split direction (default: row)\n");
    printf("  --weights, -W <...>    Space or comma separated weights (required, min 2)\n");
    printf("  --gap,     -g <n>      Gap between children in pixels (default: 0)\n");
    printf("  --min,     -m <n>      Minimum child size in pixels (default: 1)\n");
    printf("  --help,    -h          Show help\n");
    printf("  -v, --version          Show version\n\n");
    printf("Layout options:\n");
    printf("  --x,       -x <n>      Root X position (default: 0)\n");
    printf("  --y,       -y <n>      Root Y position (default: 0)\n\n");
    printf("Output:\n");
    printf("  One line per child: index x y w h\n\n");
    printf("Examples:\n");
    printf("  %s split -w 1920 -H 1080 -k row -W \"1 2 1\"\n", name);
    printf("  %s split -w 800 -H 600 -k col -W \"1 1 1 1\" -g 4\n", name);
    printf("  %s layout -x 0 -y 0 -w 1920 -H 1080 < tree.txt\n", name);
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
 * Parses a comma-separated positive finite weight list for layout input.
 * @param text Weight list.
 * @param weights Output array.
 * @param cap Output capacity.
 * @return Parsed count, or -1 on invalid input.
 */
static int kc_grd_layout_weights(const char *text, float *weights, int cap) {
    char buffer[4096];
    char *cursor;
    char *comma;
    int count = 0;

    if (!text || !weights || cap <= 0 || strlen(text) >= sizeof(buffer)) return -1;
    memcpy(buffer, text, strlen(text) + 1);
    cursor = buffer;
    do {
        float weight;
        comma = strchr(cursor, ',');
        if (comma) *comma = '\0';
        if (!*cursor || count >= cap || !kc_grd_parse_float(cursor, &weight) ||
            !isfinite(weight) || weight <= 0.0f) return -1;
        weights[count++] = weight;
        cursor = comma ? comma + 1 : NULL;
    } while (cursor);
    return count;
}

/**
 * Resolves a dot-separated box path from root.
 * @param root Root box.
 * @param path Dot path, with . denoting root.
 * @return Borrowed matching box, or NULL.
 */
static kc_grd_box_t *kc_grd_layout_path(kc_grd_box_t *root, const char *path) {
    const char *cursor;
    kc_grd_box_t *box;

    if (!root || !path) return NULL;
    if (strcmp(path, ".") == 0) return root;
    box = root;
    cursor = path;
    while (*cursor) {
        char *end;
        long index;
        if (*cursor < '0' || *cursor > '9' || !box->split) return NULL;
        if (*cursor == '0' && cursor[1] >= '0' && cursor[1] <= '9') return NULL;
        errno = 0;
        index = strtol(cursor, &end, 10);
        if (errno != 0 || end == cursor || index < 0 || index > 2147483647L ||
            (*end != '\0' && *end != '.')) return NULL;
        box = kc_grd_split_at(box->split, (int)index);
        if (!box) return NULL;
        if (*end == '\0') return box;
        cursor = end + 1;
        if (!*cursor) return NULL;
    }
    return NULL;
}

/**
 * Prints a box and descendants in child-order preorder.
 * @param box Box to print.
 * @param path Stable box path.
 * @return 0 on success, 1 on path overflow.
 */
static int kc_grd_layout_print(const kc_grd_box_t *box, const char *path) {
    int i;
    if (!box || !path) return 1;
    printf("%s %d %d %d %d\n", path, box->x, box->y, box->w, box->h);
    if (!box->split) return 0;
    for (i = 0; i < box->split->count; i++) {
        char child_path[4096];
        int n = strcmp(path, ".") == 0
            ? snprintf(child_path, sizeof(child_path), "%d", i)
            : snprintf(child_path, sizeof(child_path), "%s.%d", path, i);
        if (n < 0 || (size_t)n >= sizeof(child_path) ||
            kc_grd_layout_print(box->split->children[i], child_path) != 0) return 1;
    }
    return 0;
}

/**
 * Builds and prints a hierarchical layout described on standard input.
 * @param argc Argument count after the layout command.
 * @param argv Argument vector after the layout command.
 * @return 0 on success, 1 on invalid input or allocation failure.
 */
static int kc_grd_cmd_layout(int argc, char **argv) {
    char line[4096];
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int line_no = 0;
    int i;
    kc_grd_box_t *root;

    for (i = 0; i < argc; i++) {
        int *value = NULL;
        const char *flag = argv[i];
        if (strcmp(flag, "--help") == 0 || strcmp(flag, "-h") == 0) {
            kc_grd_help("grd");
            return 0;
        }
        if (strcmp(flag, "--version") == 0 || strcmp(flag, "-v") == 0) {
            kc_grd_cli_version();
            return 0;
        }
        if (strcmp(flag, "--x") == 0 || strcmp(flag, "-x") == 0) value = &x;
        else if (strcmp(flag, "--y") == 0 || strcmp(flag, "-y") == 0) value = &y;
        else if (strcmp(flag, "--width") == 0 || strcmp(flag, "-w") == 0) value = &width;
        else if (strcmp(flag, "--height") == 0 || strcmp(flag, "-H") == 0) value = &height;
        else {
            fprintf(stderr, "grd: unknown layout argument: %s\n", flag);
            return 1;
        }
        if (i + 1 >= argc || !kc_grd_parse_int(argv[++i], value) ||
            ((value == &width || value == &height) && *value <= 0)) {
            fprintf(stderr, "grd: invalid value for %s\n", flag);
            return 1;
        }
    }
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "grd: layout requires --width and --height\n");
        return 1;
    }

    root = kc_grd_box_new();
    if (!root) {
        fprintf(stderr, "grd: allocation failed\n");
        return 1;
    }
    root->border = 0;
    root->padding = 0;
    while (fgets(line, sizeof(line), stdin)) {
        char *fields[5];
        char *token;
        char *state;
        kc_grd_box_t *box;
        kc_grd_split_t *split;
        float weights[KC_GRD_WEIGHTS_CAP];
        int count;
        int field_count = 0;
        int gap;
        int min_px;

        line_no++;
        if (!strchr(line, '\n') && !feof(stdin)) {
            fprintf(stderr, "grd: line %d is too long\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        token = strtok_r(line, " \t", &state);
        while (token && field_count < 5) {
            fields[field_count++] = token;
            token = strtok_r(NULL, " \t", &state);
        }
        if (token || field_count < 3) {
            fprintf(stderr, "grd: malformed layout line %d\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        box = kc_grd_layout_path(root, fields[0]);
        if (!box) {
            fprintf(stderr, "grd: invalid or nonexistent path on line %d\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        if (box->split) {
            fprintf(stderr, "grd: path already has a split on line %d\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        if (strcmp(fields[1], "row") == 0) split = kc_grd_split_set(box, KC_GRD_ROW);
        else if (strcmp(fields[1], "col") == 0) split = kc_grd_split_set(box, KC_GRD_COL);
        else split = NULL;
        if (!split) {
            fprintf(stderr, "grd: invalid split kind or allocation failure on line %d\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        count = kc_grd_layout_weights(fields[2], weights, KC_GRD_WEIGHTS_CAP);
        if (count < 2) {
            fprintf(stderr, "grd: layout line %d requires at least two valid weights\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        gap = split->gap;
        min_px = split->min_px;
        if ((field_count >= 4 && (!kc_grd_parse_int(fields[3], &gap) || gap < 0)) ||
            (field_count == 5 && (!kc_grd_parse_int(fields[4], &min_px) || min_px < 0))) {
            fprintf(stderr, "grd: invalid gap or min on line %d\n", line_no);
            kc_grd_box_free(root);
            return 1;
        }
        kc_grd_split_gap(split, gap, min_px);
        for (i = 0; i < count; i++) {
            kc_grd_box_t *child = kc_grd_box_new();
            if (!child || kc_grd_split_add(split, child, weights[i]) != 0) {
                kc_grd_box_free(child);
                fprintf(stderr, "grd: allocation failed on line %d\n", line_no);
                kc_grd_box_free(root);
                return 1;
            }
            child->border = 0;
            child->padding = 0;
        }
    }
    if (ferror(stdin)) {
        fprintf(stderr, "grd: failed reading layout input\n");
        kc_grd_box_free(root);
        return 1;
    }
    kc_grd_box_bounds(root, x, y, width, height);
    kc_grd_box_layout(root);
    if (kc_grd_layout_print(root, ".") != 0) {
        fprintf(stderr, "grd: layout path is too long to print\n");
        kc_grd_box_free(root);
        return 1;
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

    if (strcmp(argv[1], "layout") == 0) {
        return kc_grd_cmd_layout(argc - 2, argv + 2);
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
