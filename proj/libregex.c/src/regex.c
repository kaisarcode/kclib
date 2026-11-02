/**
 * regex.c - Portable regex command-line interface.
 * Summary: Matches or searches text using the native regex API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libregex.h"

#include <stdio.h>
#include <string.h>

/**
 * Print CLI usage and available operations.
 * @param name Command name.
 * @return None.
 */
static void regex_usage(const char *name)
{
    printf("Usage: %s match <pattern> <text>\n", name);
    printf("       %s search <pattern> <text>\n", name);
    printf("\nCommands:\n");
    printf("  match   Require the pattern to match the full input\n");
    printf("  search  Find the first substring matching the pattern\n");
    printf("\nOptions:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Execute one regex operation and emit capture ranges.
 * @param argc Process argument count.
 * @param argv Process argument values.
 * @return Zero for a match, one for no match or an error.
 */
int main(int argc, char **argv)
{
    kc_regex_t *regex = NULL;
    kc_regex_capture_t matches[KC_REGEX_CAPTURES + 1U];
    size_t count = 0U, i;
    int status;

    if (argc == 2 && (!strcmp(argv[1], "-h") ||
        !strcmp(argv[1], "--help"))) {
        regex_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") ||
        !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_regex_version());
        return 0;
    }
    if (argc != 4 ||
        (strcmp(argv[1], "match") && strcmp(argv[1], "search"))) {
        regex_usage(argv[0]);
        return 1;
    }
    status = kc_regex_open(&regex, argv[2]);
    if (status == KC_REGEX_OK) {
        if (!strcmp(argv[1], "match"))
            status = kc_regex_match(regex, argv[3],
                matches, KC_REGEX_CAPTURES + 1U, &count);
        else
            status = kc_regex_search(regex, argv[3], 0U,
                matches, KC_REGEX_CAPTURES + 1U, &count);
    }
    if (status == KC_REGEX_OK) {
        for (i = 0U; i < count; ++i) {
            if (matches[i].start == SIZE_MAX)
                printf("%zu\t-\t-\n", i);
            else
                printf("%zu\t%zu\t%zu\n", i,
                    matches[i].start, matches[i].end);
        }
    } else if (status != KC_REGEX_NOMATCH) {
        fprintf(stderr, "regex: %s\n", kc_regex_strerror(status));
    }
    kc_regex_close(regex);
    return status == KC_REGEX_OK ? 0 : 1;
}
