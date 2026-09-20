/**
 * mmap.c - Readonly memory mapping.
 * Summary: Command line interface for the mmap tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_MMAP_BUF_SIZE 8192

/**
 * Print CLI help message.
 * @param none Unused.
 * @return None.
 */
static void print_help(void) {
    printf("Usage:\n");
    printf("  mmap set <file>    Read stdin, replace file with bytes\n");
    printf("  mmap get <file>    Map file, write bytes to stdout\n");
    printf("  mmap -h, --help    Show this help\n");
    printf("  mmap -v, --version Show version\n");
}

/**
 * Print CLI version message.
 * @param none Unused.
 * @return None.
 */
static void print_version(void) {
    printf("mmap build %llu\n", (unsigned long long)kc_mmap_version());
}

/**
 * Executes the set subcommand.
 * @param path Destination file path.
 * @return 0 on success, 1 on error.
 */
static int kc_mmap_cmd_set(const char *path) {
    char buf[KC_MMAP_BUF_SIZE];
    FILE *f;
    size_t n;

    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "mmap: failed to open file for writing\n");
        return 1;
    }

    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (fwrite(buf, 1, n, f) != n) {
            fclose(f);
            fprintf(stderr, "mmap: write error\n");
            return 1;
        }
    }

    if (ferror(stdin)) {
        fclose(f);
        fprintf(stderr, "mmap: read error\n");
        return 1;
    }

    fclose(f);
    return 0;
}

/**
 * Executes the get subcommand.
 * @param path Source file path.
 * @return 0 on success, 1 on error.
 */
static int kc_mmap_cmd_get(const char *path) {
    kc_mmap_t *mf = NULL;
    const void *data;
    size_t size;

    if (kc_mmap_open(&mf, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: failed to open file for mapping\n");
        return 1;
    }

    data = kc_mmap_data(mf);
    size = kc_mmap_size(mf);

    if (size > 0 && data != NULL) {
        if (fwrite(data, 1, size, stdout) != size) {
            kc_mmap_close(mf);
            fprintf(stderr, "mmap: write error\n");
            return 1;
        }
    }

    kc_mmap_close(mf);
    return 0;
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status code.
 */
int main(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
            print_version();
            return 0;
        }
    }

    if (argc != 3) {
        fprintf(stderr, "mmap: invalid arguments\n");
        return 1;
    }

    {
        const char *cmd = argv[1];
        const char *path = argv[2];
        int rc;

        if (strcmp(cmd, "set") == 0) {
            rc = kc_mmap_cmd_set(path);
        } else if (strcmp(cmd, "get") == 0) {
            rc = kc_mmap_cmd_get(path);
        } else {
            fprintf(stderr, "mmap: unknown command '%s'\n", cmd);
            return 1;
        }

        return rc;
    }
}
