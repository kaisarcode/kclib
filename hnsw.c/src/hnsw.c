/**
 * hnsw.c - HNSW Vector Search
 * Summary: CLI for HNSW-based approximate nearest neighbor search.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhnsw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hnsw_read_stdin(char **out_text);
static int hnsw_metric_from_string(const char *name);

/**
 * Reads text from standard input into a dynamically allocated buffer.
 * @param out_text Destination pointer for the allocated text.
 * @return 0 on success, or -1 on failure.
 */
static int hnsw_read_stdin(char **out_text) {
    char *data = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char chunk[4096];
    size_t n;

    if (!out_text) {
        return -1;
    }

    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
        if (length + n + 1 > capacity) {
            size_t next_cap = capacity ? capacity * 2 : 4096;
            while (next_cap < length + n + 1) {
                next_cap *= 2;
            }
            char *next_data = (char *)realloc(data, next_cap);
            if (!next_data) {
                free(data);
                return -1;
            }
            data = next_data;
            capacity = next_cap;
        }
        memcpy(data + length, chunk, n);
        length += n;
    }

    if (ferror(stdin)) {
        free(data);
        return -1;
    }

    if (length == 0) {
        free(data);
        *out_text = NULL;
        return 0;
    }

    data[length] = '\0';
    *out_text = data;
    return 0;
}

#define HNSW_LINE_CAP 16384
#define HNSW_QUERY_CAP 4096
#define HNSW_RESULT_CAP 256

/**
 * Prints the compact command help.
 * @param name Program executable name.
 * @return No return value.
 */
static void hnsw_help(const char *name) {
    printf("Usage:\n");
    printf("  %s --dim <n> --input <dataset> --query <values> [options]\n\n", name);
    printf("Options:\n");
    printf("  --dim, -d <n>        Vector dimension\n");
    printf("  --input, -i <path>   Dataset file with: id v1 v2 ... vN\n");
    printf("  --query, -q <text>   Query vector values separated by spaces or commas\n");
    printf("  --top, -k <n>        Maximum number of results\n");
    printf("  --threshold, -t <n>  Minimum score or maximum distance\n");
    printf("  --metric, -m <name>  cosine | inner | l2\n");
    printf("  --max-conn <n>       Maximum graph connections per level\n");
    printf("  --build-effort <n>    Index quality vs speed (higher = better recall, slower build)\n");
    printf("  --search-effort <n>  Search accuracy vs speed (higher = better recall, slower query)\n");
    printf("  -h, --help           Show help\n");
    printf("  -v, --version        Show version\n\n");
    printf("Examples:\n");
    printf("  %s --dim 3 --input vectors.txt --query \"1 0 0\"\n", name);
    printf("  %s --dim 2 --input points.txt --query \"0.1,0.2\" --metric l2 --top 5\n", name);
}

/**
 * Prints the binary version.
 * @return No return value.
 */
static void hnsw_version(void) {
    printf("hnsw build %llu\n", (unsigned long long)kc_hnsw_version());
}

/**
 * Prints one CLI error followed by help.
 * @param name Program executable name.
 * @param message Error text.
 * @return Process exit status.
 */
static int hnsw_fail_usage(const char *name, const char *message) {
    fprintf(stderr, "hnsw: %s\n\n", message);
    hnsw_help(name);
    return 1;
}

/**
 * Parses one signed integer.
 * @param text Source text.
 * @param out Destination integer pointer.
 * @return 1 on success, or 0 on failure.
 */
static int hnsw_parse_int(const char *text, int *out) {
    char *end;
    long value;

    if (text == NULL || out == NULL) {
        return 0;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    if (value < -2147483647L - 1L || value > 2147483647L) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

/**
 * Parses one floating-point value.
 * @param text Source text.
 * @param out Destination value pointer.
 * @return 1 on success, or 0 on failure.
 */
static int hnsw_parse_float(const char *text, float *out) {
    char *end;
    float value;

    if (text == NULL || out == NULL) {
        return 0;
    }

    errno = 0;
    value = strtof(text, &end);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = value;
    return 1;
}

/**
 * Resolve one CLI metric name.
 * @param name Metric text.
 * @return Metric constant, or zero when invalid.
 */
static int hnsw_metric_from_string(const char *name) {
    if (name == NULL) return 0;
    if (strcmp(name, "cosine") == 0) return KC_HNSW_METRIC_COSINE;
    if (strcmp(name, "inner") == 0 ||
            strcmp(name, "inner_product") == 0) {
        return KC_HNSW_METRIC_INNER_PRODUCT;
    }
    if (strcmp(name, "l2") == 0 || strcmp(name, "euclidean") == 0) {
        return KC_HNSW_METRIC_L2;
    }
    return 0;
}

/**
 * Parses one delimited vector into the provided buffer.
 * @param text Source vector text.
 * @param dimension Expected vector dimension.
 * @param out Destination vector buffer.
 * @return 1 on success, or 0 on failure.
 */
static int hnsw_parse_vector(const char *text, size_t dimension, float *out) {
    char buffer[HNSW_QUERY_CAP];
    char *token;
    char *state;
    size_t count;

    if (text == NULL || out == NULL || dimension == 0) {
        return 0;
    }

    if (strlen(text) >= sizeof(buffer)) {
        return 0;
    }

    memcpy(buffer, text, strlen(text) + 1);

    for (count = 0; buffer[count] != '\0'; count++) {
        if (buffer[count] == ',') {
            buffer[count] = ' ';
        }
    }

    count = 0;
    token = strtok_r(buffer, " \t\r\n", &state);

    while (token != NULL) {
        if (count >= dimension) {
            return 0;
        }

        if (!hnsw_parse_float(token, &out[count])) {
            return 0;
        }

        count++;
        token = strtok_r(NULL, " \t\r\n", &state);
    }

    return count == dimension;
}

/**
 * Prints the results to stdout in id:score format.
 * @param results Array of result structures.
 * @param count Number of results.
 * @return No return value.
 */
static void hnsw_print_results(const kc_hnsw_result_t *results, size_t count) {
    for (size_t i = 0; i < count; i++) {
        printf("%s: %.6f\n", results[i].id, results[i].score);
    }
}

/**
 * Entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    kc_hnsw_t *hnsw = NULL;
    kc_hnsw_result_t *results = NULL;
    float *values = NULL;
    char *stdin_text = NULL;
    const char *dataset_path = NULL;
    const char *query_text = NULL;
    const char *metric_name = NULL;
    int metric = KC_HNSW_METRIC_COSINE;
    int dimension = 0;
    int m = 16;
    int ef_construction = 64;
    int ef_search = 64;
    int limit = 5;
    int i;
    int status = 0;
    double threshold = -1e18;
    int threshold_set = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            hnsw_help(argv[0]);
            goto cleanup;
        }

        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            hnsw_version();
            goto cleanup;
        }

        if (strcmp(argv[i], "--dim") == 0 || strcmp(argv[i], "-d") == 0) {
            int v;
            if (i + 1 >= argc || !hnsw_parse_int(argv[i + 1], &v) || v <= 0) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --dim.");
                goto cleanup;
            }
            dimension = v;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "-i") == 0) {
            if (i + 1 >= argc) {
                status = hnsw_fail_usage(argv[0], "Missing value for --input.");
                goto cleanup;
            }
            dataset_path = argv[i + 1];
            i++;
            continue;
        }

        if (strcmp(argv[i], "--query") == 0 || strcmp(argv[i], "-q") == 0) {
            if (i + 1 >= argc) {
                status = hnsw_fail_usage(argv[0], "Missing value for --query.");
                goto cleanup;
            }
            query_text = argv[i + 1];
            i++;
            continue;
        }

        if (strcmp(argv[i], "--top") == 0 || strcmp(argv[i], "-k") == 0) {
            if (i + 1 >= argc || !hnsw_parse_int(argv[i + 1], &limit)) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --top.");
                goto cleanup;
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "--metric") == 0 || strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                status = hnsw_fail_usage(argv[0], "Missing value for --metric.");
                goto cleanup;
            }
            metric_name = argv[i + 1];
            i++;
            continue;
        }

        if (strcmp(argv[i], "--threshold") == 0 || strcmp(argv[i], "-t") == 0) {
            float t;
            if (i + 1 >= argc || !hnsw_parse_float(argv[i + 1], &t)) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --threshold.");
                goto cleanup;
            }
            threshold = (double)t;
            threshold_set = 1;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--max-conn") == 0) {
            int v;
            if (i + 1 >= argc || !hnsw_parse_int(argv[i + 1], &v) || v <= 0) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --max-conn.");
                goto cleanup;
            }
            m = v;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--build-effort") == 0) {
            int v;
            if (i + 1 >= argc || !hnsw_parse_int(argv[i + 1], &v) || v <= 0) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --build-effort.");
                goto cleanup;
            }
            ef_construction = v;
            i++;
            continue;
        }

        if (strcmp(argv[i], "--search-effort") == 0) {
            int v;
            if (i + 1 >= argc || !hnsw_parse_int(argv[i + 1], &v) || v <= 0) {
                status = hnsw_fail_usage(argv[0], "Invalid value for --search-effort.");
                goto cleanup;
            }
            ef_search = v;
            i++;
            continue;
        }

        status = hnsw_fail_usage(argv[0], "Unknown argument.");
        goto cleanup;
    }

    if (dimension == 0) {
        status = hnsw_fail_usage(argv[0], "Vector dimension must be greater than zero.");
        goto cleanup;
    }

    if (dimension > HNSW_QUERY_CAP) {
        status = hnsw_fail_usage(argv[0], "Vector dimension is too large.");
        goto cleanup;
    }

    if (dataset_path == NULL) {
        status = hnsw_fail_usage(argv[0], "Missing --input dataset file.");
        goto cleanup;
    }

    if (query_text == NULL) {
        if (hnsw_read_stdin(&stdin_text) != 0) {
            status = 1;
            goto cleanup;
        }
        query_text = stdin_text;
    }

    if (query_text == NULL || *query_text == '\0') {
        status = hnsw_fail_usage(argv[0], "Missing --query vector.");
        goto cleanup;
    }

    if (limit < 1) {
        status = hnsw_fail_usage(argv[0], "Top-K value must be greater than zero.");
        goto cleanup;
    }

    if (limit > HNSW_RESULT_CAP) {
        limit = HNSW_RESULT_CAP;
    }

    if (metric_name != NULL) {
        metric = hnsw_metric_from_string(metric_name);
    }
    if (metric == 0) {
        status = hnsw_fail_usage(argv[0], "Unknown metric name.");
        goto cleanup;
    }

    float query[HNSW_QUERY_CAP];
    if (!hnsw_parse_vector(query_text, (size_t)dimension, query)) {
        status = hnsw_fail_usage(argv[0], "Query vector does not match the configured dimension.");
        goto cleanup;
    }

    if (!threshold_set) {
        if (metric == KC_HNSW_METRIC_L2) {
            threshold = 1e18;
        } else {
            threshold = -1e18;
        }
    }

    {
        kc_hnsw_options_t opts = kc_hnsw_options_default();
        opts.dimension = (size_t)dimension;
        opts.metric = metric;
        opts.max_connections = m;
        opts.build_effort = ef_construction;
        opts.search_effort = ef_search;

        int rc = kc_hnsw_open(&hnsw, &opts);
        if (rc != KC_HNSW_OK) {
            fprintf(stderr, "hnsw: open failed: %s\n", kc_hnsw_strerror(rc));
            status = 1;
            goto cleanup;
        }
    }

    {
        FILE *file = fopen(dataset_path, "r");
        if (file == NULL) {
            fprintf(stderr, "hnsw: %s: %s\n", dataset_path, strerror(errno));
            status = 1;
            goto cleanup;
        }

        values = (float *)malloc((size_t)dimension * sizeof(float));
        if (values == NULL) {
            fclose(file);
            fprintf(stderr, "hnsw: out of memory\n");
            status = 1;
            goto cleanup;
        }

        char line[HNSW_LINE_CAP];
        int load_error = 0;

        while (fgets(line, sizeof(line), file) != NULL) {
            char *token;
            char *state;
            char *id;
            size_t index;

            token = strtok_r(line, " \t\r\n", &state);
            if (token == NULL || token[0] == '#') {
                continue;
            }

            id = token;
            index = 0;

            token = strtok_r(NULL, " \t\r\n", &state);
            while (token != NULL && index < (size_t)dimension) {
                if (!hnsw_parse_float(token, &values[index])) {
                    load_error = 1;
                    break;
                }
                index++;
                token = strtok_r(NULL, " \t\r\n", &state);
            }

            if (load_error) break;

            if (index != (size_t)dimension || token != NULL) {
                load_error = 1;
                break;
            }

            int rc = kc_hnsw_add(hnsw, id, values);
            if (rc != KC_HNSW_OK) {
                fprintf(stderr, "hnsw: add failed: %s\n", kc_hnsw_strerror(rc));
                load_error = 1;
                break;
            }
        }

        free(values);
        values = NULL;
        fclose(file);

        if (load_error) {
            fprintf(stderr, "hnsw: invalid argument\n");
            status = 1;
            goto cleanup;
        }
    }

    {
        int rc = kc_hnsw_build(hnsw);
        if (rc != KC_HNSW_OK) {
            fprintf(stderr, "hnsw: index build failed: %s\n", kc_hnsw_strerror(rc));
            status = 1;
            goto cleanup;
        }
    }

    {
        size_t result_count = 0;
        int rc = kc_hnsw_search(hnsw, query, (size_t)limit, threshold,
                                &results, &result_count);
        if (rc != KC_HNSW_OK) {
            fprintf(stderr, "hnsw: search failed: %s\n", kc_hnsw_strerror(rc));
            status = 1;
            goto cleanup;
        }

        hnsw_print_results(results, result_count);
    }

cleanup:
    kc_hnsw_free(results);
    free(stdin_text);
    if (hnsw) kc_hnsw_close(hnsw);
    return status;
}
