/**
 * demo.c - Demo command-line interface.
 * Summary: Creates greetings with libdemo.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libdemo.h"

#include <stdio.h>
#include <string.h>

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void demo_cli_usage(const char *name)
{
    printf("Usage: %s <name> [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Runs the demo CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *name = NULL;
    char *greeting;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        demo_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_demo_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr,
                "demo: -h/--help and -v/--version must be used alone\n");
            return 1;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "demo: unknown option: %s\n", argv[i]);
            return 1;
        }
        if (name != NULL) {
            fprintf(stderr, "demo: only one name is accepted; quote spaces\n");
            return 1;
        }
        name = argv[i];
    }

    if (name == NULL) {
        fprintf(stderr, "demo: missing name\n");
        return 1;
    }

    greeting = kc_demo_greet(name);
    if (greeting == NULL) {
        fprintf(stderr, "demo: greeting failed\n");
        return 1;
    }

    printf("%s\n", greeting);
    kc_demo_free(greeting);
    return 0;
}
