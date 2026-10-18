/**
 * hnsw.c - HNSW vector search command-line interface.
 * Summary: Builds one in-memory index and searches it with libhnsw.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libhnsw.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void hnsw_cli_usage(const char *name)
{
    printf("Usage: %s <vectors> -q <query> -d <dimension> [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>       Read vectors from file\n");
    printf("  -q, --query <vector>     Query vector as comma-separated floats\n");
    printf("  -d, --dimension <N>      Vector dimension\n");
    printf("  -m, --metric <metric>    cosine, inner, or l2 (default: cosine)\n");
    printf("  -l, --limit <N>          Maximum results (default: 5)\n");
    printf("  -t, --threshold <N>      Metric-specific result threshold\n");
    printf("  --max-connections <N>    Maximum graph connections\n");
    printf("  --build-effort <N>       Graph build effort\n");
    printf("  --search-effort <N>      Search effort\n");
    printf("  -h, --help               Show this help\n");
    printf("  -v, --version            Show version\n");
}

/**
 * Reads all bytes from one stream as a NUL-terminated string.
 * @param stream Input stream.
 * @param out Receives allocated string.
 * @param out_size Receives byte count when non-NULL.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_read_stream(FILE *stream, char **out, size_t *out_size)
{
    char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    char chunk[4096];
    size_t count;

    *out = NULL;
    if (out_size) *out_size = 0;

    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required = size + count + 1;
        char *next;

        if (required > capacity) {
            size_t next_capacity = capacity ? capacity * 2 : 4096;

            while (next_capacity < required) next_capacity *= 2;
            next = (char *)realloc(buffer, next_capacity);
            if (!next) {
                free(buffer);
                return 1;
            }
            buffer = next;
            capacity = next_capacity;
        }
        memcpy(buffer + size, chunk, count);
        size += count;
    }

    if (ferror(stream)) {
        free(buffer);
        return 1;
    }
    if (!buffer) {
        buffer = (char *)malloc(1);
        if (!buffer) return 1;
    }

    buffer[size] = '\0';
    *out = buffer;
    if (out_size) *out_size = size;
    return 0;
}

/**
 * Reads one input file as text.
 * @param path File path.
 * @param out Receives allocated string.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_read_file(const char *path, char **out)
{
    FILE *file;
    int status;

    file = fopen(path, "rb");
    if (!file) return 1;
    status = hnsw_cli_read_stream(file, out, NULL);
    fclose(file);
    return status;
}

/**
 * Reports whether stdin is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int hnsw_cli_stdin_is_tty(void)
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (handle == INVALID_HANDLE_VALUE || handle == NULL) return 0;
    return GetConsoleMode(handle, &mode) != 0;
#else
    return isatty(STDIN_FILENO);
#endif
}

/**
 * Parses one positive size value.
 * @param text Input text.
 * @param out Receives parsed size.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_parse_size(const char *text, size_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (!text[0] || text[0] == '-' || text[0] == '+') return 1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0) return 1;
    if (value > (unsigned long long)(size_t)-1) return 1;
    *out = (size_t)value;
    return 0;
}

/**
 * Parses one positive int value.
 * @param text Input text.
 * @param out Receives parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_parse_int(const char *text, int *out)
{
    size_t value;

    if (hnsw_cli_parse_size(text, &value) || value > 2147483647U) return 1;
    *out = (int)value;
    return 0;
}

/**
 * Parses one finite double value.
 * @param text Input text.
 * @param out Receives parsed value.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_parse_double(const char *text, double *out)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value)) return 1;
    *out = value;
    return 0;
}

/**
 * Parses a comma-separated float vector with an exact dimension.
 * @param text Vector text.
 * @param dimension Required dimension.
 * @param out Receives allocated vector.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_parse_vector(
    const char *text,
    size_t dimension,
    float **out
)
{
    float *values;
    const char *cursor = text;

    values = (float *)malloc(dimension * sizeof(*values));
    if (!values) return 1;

    for (size_t i = 0; i < dimension; i++) {
        char *end = NULL;
        float value;

        errno = 0;
        value = strtof(cursor, &end);
        if (errno != 0 || end == cursor || !isfinite(value)) {
            free(values);
            return 1;
        }
        values[i] = value;

        if (i + 1 < dimension) {
            if (*end != ',') {
                free(values);
                return 1;
            }
            cursor = end + 1;
        } else if (*end != '\0') {
            free(values);
            return 1;
        }
    }

    *out = values;
    return 0;
}

/**
 * Adds tab-separated id/vector rows to one index.
 * @param hnsw Index handle.
 * @param source Mutable dataset text.
 * @param dimension Vector dimension.
 * @return Zero on success, nonzero on failure.
 */
static int hnsw_cli_add_rows(
    kc_hnsw_t *hnsw,
    char *source,
    size_t dimension
)
{
    char *cursor = source;

    while (*cursor) {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        char *separator;
        float *values = NULL;
        int status;

        if (newline) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }

        if (line[0] && line[strlen(line) - 1] == '\r') {
            line[strlen(line) - 1] = '\0';
        }
        if (!line[0]) continue;

        separator = strchr(line, '\t');
        if (!separator || separator == line || !separator[1]) return 1;
        if (strchr(separator + 1, '\t')) return 1;
        *separator = '\0';

        if (hnsw_cli_parse_vector(separator + 1, dimension, &values)) return 1;
        status = kc_hnsw_add(hnsw, line, values);
        free(values);
        if (status != KC_HNSW_OK) return 1;
    }

    return kc_hnsw_count(hnsw) == 0;
}

/**
 * Runs the hnsw CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_hnsw_options_t options;
    kc_hnsw_t *hnsw = NULL;
    kc_hnsw_result_t *results = NULL;
    const char *dataset_arg = NULL;
    const char *input_path = NULL;
    const char *query_text = NULL;
    char *owned_source = NULL;
    char *source = NULL;
    float *query = NULL;
    size_t dimension = 0;
    size_t limit = 5;
    size_t result_count = 0;
    int metric = KC_HNSW_METRIC_COSINE;
    int max_connections = 0;
    int build_effort = 0;
    int search_effort = 0;
    int has_metric = 0;
    int has_max_connections = 0;
    int has_build_effort = 0;
    int has_search_effort = 0;
    double threshold = 0.0;
    int has_threshold = 0;
    int result = 1;
    int status;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        hnsw_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_hnsw_version());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "hnsw: --input requires a file\n");
                goto done;
            }
            if (input_path) {
                fprintf(stderr, "hnsw: only one input file is accepted\n");
                goto done;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-q") == 0 ||
            strcmp(argv[i], "--query") == 0)
        {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "hnsw: --query requires a vector\n");
                goto done;
            }
            query_text = argv[i];
        } else if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--dimension") == 0)
        {
            if (++i >= argc || hnsw_cli_parse_size(argv[i], &dimension)) {
                fprintf(stderr, "hnsw: dimension must be a positive integer\n");
                goto done;
            }
        } else if (strcmp(argv[i], "-m") == 0 ||
            strcmp(argv[i], "--metric") == 0)
        {
            if (++i >= argc) {
                fprintf(stderr, "hnsw: --metric requires a value\n");
                goto done;
            }
            if (strcmp(argv[i], "cosine") == 0) {
                metric = KC_HNSW_METRIC_COSINE;
            } else if (strcmp(argv[i], "inner") == 0 ||
                strcmp(argv[i], "inner_product") == 0)
            {
                metric = KC_HNSW_METRIC_INNER_PRODUCT;
            } else if (strcmp(argv[i], "l2") == 0) {
                metric = KC_HNSW_METRIC_L2;
            } else {
                fprintf(stderr, "hnsw: metric must be cosine, inner, or l2\n");
                goto done;
            }
            has_metric = 1;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--limit") == 0) {
            if (++i >= argc || hnsw_cli_parse_size(argv[i], &limit)) {
                fprintf(stderr, "hnsw: limit must be a positive integer\n");
                goto done;
            }
        } else if (strcmp(argv[i], "-t") == 0 ||
            strcmp(argv[i], "--threshold") == 0)
        {
            if (++i >= argc || hnsw_cli_parse_double(argv[i], &threshold)) {
                fprintf(stderr, "hnsw: threshold must be a finite number\n");
                goto done;
            }
            has_threshold = 1;
        } else if (strcmp(argv[i], "--max-connections") == 0) {
            if (++i >= argc || hnsw_cli_parse_int(argv[i], &max_connections)) {
                fprintf(stderr, "hnsw: max-connections must be positive\n");
                goto done;
            }
            has_max_connections = 1;
        } else if (strcmp(argv[i], "--build-effort") == 0) {
            if (++i >= argc || hnsw_cli_parse_int(argv[i], &build_effort)) {
                fprintf(stderr, "hnsw: build-effort must be positive\n");
                goto done;
            }
            has_build_effort = 1;
        } else if (strcmp(argv[i], "--search-effort") == 0) {
            if (++i >= argc || hnsw_cli_parse_int(argv[i], &search_effort)) {
                fprintf(stderr, "hnsw: search-effort must be positive\n");
                goto done;
            }
            has_search_effort = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "hnsw: help/version must be used alone\n");
            goto done;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "hnsw: unknown option '%s'\n", argv[i]);
            goto done;
        } else if (dataset_arg) {
            fprintf(stderr, "hnsw: only one vectors argument is accepted\n");
            goto done;
        } else {
            dataset_arg = argv[i];
        }
    }

    if (dimension == 0) {
        fprintf(stderr, "hnsw: --dimension is required\n");
        goto done;
    }
    if (!query_text) {
        fprintf(stderr, "hnsw: --query is required\n");
        goto done;
    }

    if (dataset_arg) {
        size_t size = strlen(dataset_arg) + 1;

        owned_source = (char *)malloc(size);
        if (!owned_source) goto done;
        memcpy(owned_source, dataset_arg, size);
        source = owned_source;
    } else if (!hnsw_cli_stdin_is_tty()) {
        size_t stdin_size = 0;

        if (hnsw_cli_read_stream(stdin, &owned_source, &stdin_size)) {
            fprintf(stderr, "hnsw: cannot read stdin\n");
            goto done;
        }
        if (stdin_size > 0) source = owned_source;
    }

    if (!source && input_path) {
        free(owned_source);
        owned_source = NULL;
        if (hnsw_cli_read_file(input_path, &owned_source)) {
            fprintf(stderr, "hnsw: cannot read input file\n");
            goto done;
        }
        source = owned_source;
    }
    if (!source) {
        fprintf(stderr, "hnsw: missing vectors input\n");
        goto done;
    }

    if (hnsw_cli_parse_vector(query_text, dimension, &query)) {
        fprintf(stderr, "hnsw: query does not match dimension\n");
        goto done;
    }

    memset(&options, 0, sizeof(options));
    options.dimension = dimension;
    if (has_metric) options.metric = &metric;
    if (has_max_connections) options.max_connections = &max_connections;
    if (has_build_effort) options.build_effort = &build_effort;
    if (has_search_effort) options.search_effort = &search_effort;

    status = kc_hnsw_open(&hnsw, &options);
    if (status != KC_HNSW_OK) {
        fprintf(stderr, "hnsw: %s\n", kc_hnsw_strerror(status));
        goto done;
    }

    if (hnsw_cli_add_rows(hnsw, source, dimension)) {
        fprintf(stderr, "hnsw: invalid vectors input\n");
        goto done;
    }

    status = kc_hnsw_build(hnsw);
    if (status != KC_HNSW_OK) {
        fprintf(stderr, "hnsw: %s\n", kc_hnsw_strerror(status));
        goto done;
    }

    if (!has_threshold) {
        threshold = metric == KC_HNSW_METRIC_L2 ? INFINITY : -INFINITY;
    }

    status = kc_hnsw_search(
        hnsw,
        query,
        limit,
        threshold,
        &results,
        &result_count
    );
    if (status != KC_HNSW_OK) {
        fprintf(stderr, "hnsw: %s\n", kc_hnsw_strerror(status));
        goto done;
    }

    for (size_t i = 0; i < result_count; i++) {
        printf("%s,%.12g\n", results[i].id, results[i].score);
    }
    result = 0;

done:
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);
    free(query);
    free(owned_source);
    return result;
}
