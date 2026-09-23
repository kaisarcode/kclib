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

static void print_help(void) {
    printf("Usage:\n");
    printf("  mmap -set|--set <file> Read stdin, set the value, and save it\n");
    printf("  mmap -get|--get <file> Read the saved value to stdout\n");
    printf("  mmap -del|--del <file> Delete the backing file\n");
    printf("  mmap -h, --help        Show this help\n");
    printf("  mmap -v, --version     Show version\n");
}

static void print_version(void) {
    printf("mmap build %llu\n", (unsigned long long)kc_mmap_version());
}

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

static int command_set(const char *path) {
    kc_mmap_t *map = NULL;
    void *data = NULL;
    size_t size = 0U;
    int rc;

    if (read_stdin(&data, &size) != 0) {
        fprintf(stderr, "mmap: read error\n");
        return 1;
    }

    rc = kc_mmap_open(&map, path);
    if (rc != KC_MMAP_OK) {
        free(data);
        fprintf(stderr, "mmap: failed to open value\n");
        return 1;
    }

    rc = kc_mmap_set(map, data, size);
    free(data);
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

int main(int argc, char **argv) {
    const char *mode;
    const char *path;

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

    mode = argv[1];
    path = argv[2];

    if (strcmp(mode, "-set") == 0 || strcmp(mode, "--set") == 0) {
        return command_set(path);
    }
    if (strcmp(mode, "-get") == 0 || strcmp(mode, "--get") == 0) {
        return command_get(path);
    }
    if (strcmp(mode, "-del") == 0 || strcmp(mode, "--del") == 0) {
        return command_del(path);
    }

    fprintf(stderr, "mmap: unknown option '%s'\n", mode);
    return 1;
}
