/**
 * leven.c - Levenshtein distance command-line interface.
 * Summary: Compare two UTF-8 strings with an optional maximum distance.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libleven.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Print the command-line usage.
 * @param name Invoked executable name.
 * @return None.
 */
static void leven_usage(const char *name)
{
    printf("Usage: %s <left> <right> [options]\n", name);
    printf("\nOptions:\n");
    printf("  -m, --max <N>  Maximum accepted distance\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Parse a decimal unsigned distance without implicit signs or whitespace.
 * @param text Decimal string.
 * @param out Receives the parsed value.
 * @return 0 on success, 1 on invalid or overflowing input.
 */
static int leven_parse_size(const char *text, size_t *out)
{
    unsigned long long value;
    char *end;

    if (text == NULL || text[0] < '0' || text[0] > '9')
        return 1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > (unsigned long long)SIZE_MAX)
        return 1;
    *out = (size_t)value;
    return 0;
}

/**
 * Execute the Levenshtein distance CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on computed distance, 1 on threshold exceedance or input error.
 */
int main(int argc, char **argv)
{
    const char *left = NULL, *right = NULL;
    size_t maximum = 0, distance = 0;
    int has_maximum = 0;
    int status;

    if (argc == 2 && (!strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help"))) {
        leven_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") ||
        !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_leven_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--max")) {
            if (has_maximum || ++i >= argc ||
                leven_parse_size(argv[i], &maximum)) {
                fprintf(stderr, "leven: invalid maximum distance\n");
                return 1;
            }
            has_maximum = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") ||
            !strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            fprintf(stderr, "leven: help/version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "leven: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (left == NULL) {
            left = argv[i];
        } else if (right == NULL) {
            right = argv[i];
        } else {
            fprintf(stderr, "leven: expected exactly two strings\n");
            return 1;
        }
    }
    if (left == NULL || right == NULL) {
        fprintf(stderr, "leven: expected exactly two strings\n");
        return 1;
    }
    status = kc_leven_distance(left, right,
        has_maximum ? &maximum : NULL, &distance);
    if (status == KC_LEVEN_ELIMIT) return 1;
    if (status != KC_LEVEN_OK) {
        fprintf(stderr, "leven: %s\n", kc_leven_strerror(status));
        return 1;
    }
    printf("%zu\n", distance);
    return 0;
}
