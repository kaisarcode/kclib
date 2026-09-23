/**
 * demo.c - Minimal example CLI.
 * Summary: Command line interface for the demo example library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libdemo.h"

#include <stdio.h>
#include <string.h>

/**
 * Print command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_demo_help(const char *name) {
    printf("Usage: %s [-n <name>]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -n, --name <name>  Name to greet (default: World)\n");
    printf("  -h, --help         Show this help\n");
    printf("  -v, --version      Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_demo_print_version(void) {
    printf("demo build %llu\n", (unsigned long long)kc_demo_version());
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    const char *name = "World";
    char *greeting;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_demo_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_demo_print_version();
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "demo: missing value for %s\n", argv[i - 1]);
                return 1;
            }
            name = argv[i];
            continue;
        }

        fprintf(stderr, "demo: unknown option '%s'\n", argv[i]);
        return 1;
    }

    greeting = kc_demo_greet(name);
    if (!greeting) {
        fprintf(stderr, "demo: greeting failed\n");
        return 1;
    }

    puts(greeting);
    kc_demo_free(greeting);
    return 0;
}
