/**
 * mmap.c - Persistent binary value.
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
 * Prints CLI usage information.
 * @return None.
 */
static void print_help(void) {
    printf("Usage:\n");
    printf("  mmap <path> -get|--get\n");
    printf("  mmap <path> -set|--set [value]\n");
    printf("  mmap <path> -del|--del\n");
    printf("  mmap -h, --help\n");
    printf("  mmap -v, --version\n");
    printf("\n");
    printf("Input:\n");
    printf("  value  Optional direct value for set; stdin is used when omitted\n");
}

/**
 * Prints the build version.
 * @return None.
 */
static void print_version(void) {
    printf("mmap build %llu\n", (unsigned long long)kc_mmap_version());
}

/**
 * Reads all stdin bytes into an owned buffer.
 * @param out_data Output pointer for allocated bytes.
 * @param out_size Output byte count.
 * @return 0 on success, 1 on failure.
 */
static int read_stdin(void **out_data, size_t *out_size) {
    unsigned char chunk[KC_MMAP_BUF_SIZE];
    unsigned char *data = NULL;
    size_t size = 0U;
    size_t capacity = 0U;
    size_t count;

    if (out_data == NULL || out_size == NULL) {
        return 1;
    }

    *out_data = NULL;
    *out_size = 0U;

    while ((count = fread(chunk, 1U, sizeof(chunk), stdin)) > 0U) {
        size_t required = size + count;

        if (required > capacity) {
            size_t next = capacity == 0U ? KC_MMAP_BUF_SIZE : capacity;
            unsigned char *resized;

            while (next < required) {
                if (next > SIZE_MAX / 2U) {
                    free(data);
                    return 1;
                }
                next *= 2U;
            }

            resized = (unsigned char *)realloc(data, next);
            if (resized == NULL) {
                free(data);
                return 1;
            }

            data = resized;
            capacity = next;
        }

        memcpy(data + size, chunk, count);
        size += count;
    }

    if (ferror(stdin)) {
        free(data);
        return 1;
    }

    *out_data = data;
    *out_size = size;
    return 0;
}

/**
 * Sets and saves one value through the public API.
 * @param path Backing file path.
 * @param value Optional direct string value; NULL reads stdin.
 * @return 0 on success, 1 on failure.
 */
static int command_set(const char *path, const char *value) {
    kc_mmap_t *map = NULL;
    void *stdin_data = NULL;
    const void *data;
    size_t size;
    int rc;

    if (value != NULL) {
        data = value;
        size = strlen(value);
    } else {
        if (read_stdin(&stdin_data, &size) != 0) {
            fprintf(stderr, "mmap: read error\n");
            return 1;
        }
        data = stdin_data;
    }

    rc = kc_mmap_open(&map, path);
    if (rc != KC_MMAP_OK) {
        free(stdin_data);
        fprintf(stderr, "mmap: failed to open value\n");
        return 1;
    }

    rc = kc_mmap_set(map, data, size);
    free(stdin_data);

    if (rc == KC_MMAP_OK) {
        rc = kc_mmap_save(map);
    }

    kc_mmap_close(map);

    if (rc != KC_MMAP_OK) {
        fprintf(stderr, "mmap: failed to save value\n");
        return 1;
    }

    return 0;
}

/**
 * Writes one saved value to stdout through the public API.
 * @param path Backing file path.
 * @return 0 on success, 1 on failure.
 */
static int command_get(const char *path) {
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int rc;

    if (kc_mmap_open(&map, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: failed to open value\n");
        return 1;
    }

    rc = kc_mmap_get(map, &data, &size);
    if (rc == KC_MMAP_NOT_FOUND) {
        kc_mmap_close(map);
        fprintf(stderr, "mmap: value not found\n");
        return 1;
    }
    if (rc != KC_MMAP_OK) {
        kc_mmap_close(map);
        fprintf(stderr, "mmap: failed to get value\n");
        return 1;
    }

    if (size > 0U && fwrite(data, 1U, size, stdout) != size) {
        kc_mmap_close(map);
        fprintf(stderr, "mmap: write error\n");
        return 1;
    }

    kc_mmap_close(map);
    return 0;
}

/**
 * Deletes one value through the public API.
 * @param path Backing file path.
 * @return 0 on success, 1 on failure.
 */
static int command_del(const char *path) {
    kc_mmap_t *map = NULL;
    int rc;

    if (kc_mmap_open(&map, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: failed to open value\n");
        return 1;
    }

    rc = kc_mmap_del(map);
    kc_mmap_close(map);

    if (rc != KC_MMAP_OK) {
        fprintf(stderr, "mmap: failed to delete value\n");
        return 1;
    }

    return 0;
}

/**
 * Runs the mmap command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    const char *path;
    const char *mode;

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

    if (argc < 3 || argc > 4) {
        fprintf(stderr, "mmap: invalid arguments\n");
        return 1;
    }

    path = argv[1];
    mode = argv[2];

    if (strcmp(mode, "-get") == 0 || strcmp(mode, "--get") == 0) {
        if (argc != 3) {
            fprintf(stderr, "mmap: get does not accept a value\n");
            return 1;
        }
        return command_get(path);
    }

    if (strcmp(mode, "-set") == 0 || strcmp(mode, "--set") == 0) {
        return command_set(path, argc == 4 ? argv[3] : NULL);
    }

    if (strcmp(mode, "-del") == 0 || strcmp(mode, "--del") == 0) {
        if (argc != 3) {
            fprintf(stderr, "mmap: del does not accept a value\n");
            return 1;
        }
        return command_del(path);
    }

    fprintf(stderr, "mmap: unknown option '%s'\n", mode);
    return 1;
}
