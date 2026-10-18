/**
 * mmap.c - Persistent binary value command-line interface.
 * Summary: Reads, writes, and deletes file-backed values with libmmap.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libmmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void mmap_cli_usage(const char *name)
{
    printf("Usage: %s <command> <path> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  get <path>           Write the stored bytes to stdout\n");
    printf("  set <path>           Replace and persist the stored bytes\n");
    printf("  del <path>           Delete the stored value and backing file\n");
    printf("\n");
    printf("set options:\n");
    printf("  -i, --input <path>   Input file; use '-' for stdin (default: -)\n");
    printf("\n");
    printf("Common options:\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Reads all bytes from one stream.
 * @param stream Open input stream.
 * @param out_data Receives allocated bytes, or NULL for zero bytes.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int mmap_cli_read_stream(FILE *stream, unsigned char **out_data,
    size_t *out_size)
{
    unsigned char *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    unsigned char chunk[8192];
    size_t count;

    *out_data = NULL;
    *out_size = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required = size + count;
        unsigned char *next;

        if (required < size) {
            free(data);
            return 1;
        }
        if (required > capacity) {
            size_t next_capacity = capacity ? capacity : 8192;

            while (next_capacity < required) {
                if (next_capacity > (size_t)-1 / 2) {
                    next_capacity = required;
                    break;
                }
                next_capacity *= 2;
            }
            next = (unsigned char *)realloc(data, next_capacity);
            if (!next) {
                free(data);
                return 1;
            }
            data = next;
            capacity = next_capacity;
        }
        memcpy(data + size, chunk, count);
        size += count;
    }
    if (ferror(stream)) {
        free(data);
        return 1;
    }
    *out_data = data;
    *out_size = size;
    return 0;
}

/**
 * Reads binary input from a file or standard input.
 * @param path Input path, or "-" for standard input.
 * @param out_data Receives allocated bytes, or NULL for zero bytes.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int mmap_cli_read_input(const char *path, unsigned char **out_data,
    size_t *out_size)
{
    FILE *file;
    int result;

    if (strcmp(path, "-") == 0) {
#ifdef _WIN32
        (void)_setmode(_fileno(stdin), _O_BINARY);
#endif
        return mmap_cli_read_stream(stdin, out_data, out_size);
    }

    file = fopen(path, "rb");
    if (!file) return 1;
    result = mmap_cli_read_stream(file, out_data, out_size);
    fclose(file);
    return result;
}

/**
 * Writes the current stored value to standard output.
 * @param path Backing file path.
 * @return Process exit code.
 */
static int mmap_cli_get(const char *path)
{
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0;
    int status;

    if (kc_mmap_open(&map, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: cannot open '%s'\n", path);
        return 1;
    }
    status = kc_mmap_get(map, &data, &size);
    if (status == KC_MMAP_NOT_FOUND) {
        fprintf(stderr, "mmap: value not found: %s\n", path);
        kc_mmap_close(map);
        return 1;
    }
    if (status != KC_MMAP_OK) {
        fprintf(stderr, "mmap: cannot read '%s'\n", path);
        kc_mmap_close(map);
        return 1;
    }
#ifdef _WIN32
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    if (size > 0 && fwrite(data, 1, size, stdout) != size) {
        fprintf(stderr, "mmap: cannot write stdout\n");
        kc_mmap_close(map);
        return 1;
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "mmap: cannot flush stdout\n");
        kc_mmap_close(map);
        return 1;
    }
    kc_mmap_close(map);
    return 0;
}

/**
 * Replaces and persists the stored value.
 * @param path Backing file path.
 * @param input_path Input file path, or "-" for standard input.
 * @return Process exit code.
 */
static int mmap_cli_set(const char *path, const char *input_path)
{
    kc_mmap_t *map = NULL;
    unsigned char *data = NULL;
    size_t size = 0;
    static const unsigned char empty = 0;
    const void *value;

    if (mmap_cli_read_input(input_path, &data, &size)) {
        fprintf(stderr, "mmap: cannot read input '%s'\n", input_path);
        return 1;
    }
    if (kc_mmap_open(&map, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: cannot open '%s'\n", path);
        free(data);
        return 1;
    }
    value = size > 0 ? (const void *)data : (const void *)&empty;
    if (kc_mmap_set(map, value, size) != KC_MMAP_OK ||
        kc_mmap_save(map) != KC_MMAP_OK)
    {
        fprintf(stderr, "mmap: cannot persist '%s'\n", path);
        kc_mmap_close(map);
        free(data);
        return 1;
    }
    kc_mmap_close(map);
    free(data);
    return 0;
}

/**
 * Deletes one stored value and its backing file.
 * @param path Backing file path.
 * @return Process exit code.
 */
static int mmap_cli_del(const char *path)
{
    kc_mmap_t *map = NULL;

    if (kc_mmap_open(&map, path) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: cannot open '%s'\n", path);
        return 1;
    }
    if (kc_mmap_del(map) != KC_MMAP_OK) {
        fprintf(stderr, "mmap: cannot delete '%s'\n", path);
        kc_mmap_close(map);
        return 1;
    }
    kc_mmap_close(map);
    return 0;
}

/**
 * Runs the mmap CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    const char *command;
    const char *path;
    const char *input_path = "-";

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        mmap_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_mmap_version());
        return 0;
    }
    if (argc < 3) {
        mmap_cli_usage(argv[0]);
        return 1;
    }

    command = argv[1];
    path = argv[2];
    if (!path[0] || path[0] == '-') {
        fprintf(stderr, "mmap: invalid path\n");
        return 1;
    }

    if (strcmp(command, "set") == 0) {
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-i") == 0 ||
                strcmp(argv[i], "--input") == 0)
            {
                if (++i >= argc || !argv[i][0]) {
                    fprintf(stderr, "mmap: --input requires a path\n");
                    return 1;
                }
                input_path = argv[i];
            } else {
                fprintf(stderr, "mmap: unknown option '%s'\n", argv[i]);
                return 1;
            }
        }
        return mmap_cli_set(path, input_path);
    }

    if (argc != 3) {
        fprintf(stderr, "mmap: %s does not accept options\n", command);
        return 1;
    }
    if (strcmp(command, "get") == 0) return mmap_cli_get(path);
    if (strcmp(command, "del") == 0) return mmap_cli_del(path);

    fprintf(stderr, "mmap: unknown command '%s'\n", command);
    return 1;
}
