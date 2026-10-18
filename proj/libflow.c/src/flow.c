/**
 * flow.c - Branch flow command-line interface.
 * Summary: Command-line interface backed by libflow.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libflow.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t flow_cli_stop;

typedef struct {
    _Atomic int done;
    int status;
    void *data;
    size_t data_size;
    char *error;
} flow_cli_result_t;

/**
 * Print command usage.
 * @return None.
 */
static void flow_cli_help(void)
{
    printf("Usage: flow <file.flow> [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -e, --entry <node>       Start from one entry node\n");
    printf("  -i, --input <path>       Input file; use '-' for stdin\n");
    printf("  -s, --set <key> <value>  Set an override; repeatable\n");
    printf("  -u, --unset <key>        Remove an override; repeatable\n");
    printf("  -h, --help               Show this help\n");
    printf("  -v, --version            Show version\n");
}

/**
 * Duplicate one string.
 * @param text Source string.
 * @return Allocated copy, or NULL.
 */
static char *flow_cli_strdup(const char *text)
{
    char *copy;
    size_t size;

    if (!text) return NULL;
    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (!copy) return NULL;
    memcpy(copy, text, size);
    return copy;
}

/**
 * Sleep briefly while waiting for one asynchronous run.
 * @return None.
 */
static void flow_cli_sleep(void)
{
#ifdef _WIN32
    Sleep(20);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 20000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Record a process termination request.
 * @param sig Signal number.
 * @return None.
 */
static void flow_cli_signal(int sig)
{
    (void)sig;
    flow_cli_stop = 1;
}

/**
 * Read one complete input stream.
 * @param stream Source stream.
 * @param out Destination buffer.
 * @param out_size Destination byte count.
 * @return 0 on success, nonzero on failure.
 */
static int flow_cli_read_stream(FILE *stream, void **out, size_t *out_size)
{
    unsigned char *buffer;
    size_t capacity;
    size_t size;

    if (!stream || !out || !out_size) return 1;
    *out = NULL;
    *out_size = 0U;
    buffer = NULL;
    capacity = 0U;
    size = 0U;

    for (;;) {
        size_t chunk;

        if (size == capacity) {
            size_t next_capacity = capacity ? capacity * 2U : 4096U;
            unsigned char *next = (unsigned char *)realloc(
                buffer,
                next_capacity
            );

            if (!next) {
                free(buffer);
                return 1;
            }
            buffer = next;
            capacity = next_capacity;
        }

        chunk = fread(buffer + size, 1U, capacity - size, stream);
        size += chunk;
        if (chunk == 0U) {
            if (ferror(stream)) {
                free(buffer);
                return 1;
            }
            break;
        }
    }

    if (size == 0U) {
        free(buffer);
        return 0;
    }

    *out = buffer;
    *out_size = size;
    return 0;
}

/**
 * Read optional run input from a file or stdin.
 * @param path Input path, '-' for stdin, or NULL.
 * @param out Destination buffer.
 * @param out_size Destination byte count.
 * @return 0 on success, nonzero on failure.
 */
static int flow_cli_read_input(
    const char *path,
    void **out,
    size_t *out_size
)
{
    FILE *file;
    int result;

    *out = NULL;
    *out_size = 0U;
    if (!path) return 0;
    if (strcmp(path, "-") == 0)
        return flow_cli_read_stream(stdin, out, out_size);

    file = fopen(path, "rb");
    if (!file) return 1;
    result = flow_cli_read_stream(file, out, out_size);
    fclose(file);
    return result;
}

/**
 * Receive one terminal flow result.
 * @param status Terminal flow status.
 * @param data Owned successful output.
 * @param data_size Output byte count.
 * @param error Borrowed error text.
 * @param userdata Result state.
 * @return None.
 */
static void flow_cli_finished(
    int status,
    void *data,
    size_t data_size,
    const char *error,
    void *userdata
)
{
    flow_cli_result_t *result = (flow_cli_result_t *)userdata;

    result->status = status;
    result->data = data;
    result->data_size = data_size;
    if (error) result->error = flow_cli_strdup(error);
    atomic_store(&result->done, 1);
}

/**
 * Run one flow file with command-line overrides.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
static int flow_cli_run(int argc, char **argv)
{
    kc_flow_t *flow;
    kc_flow_run_t *run;
    flow_cli_result_t result;
    const char *entry;
    const char *input_path;
    void *input;
    size_t input_size;
    int stop_sent;
    int i;

    flow = NULL;
    run = NULL;
    entry = NULL;
    input_path = NULL;
    input = NULL;
    input_size = 0U;
    stop_sent = 0;
    memset(&result, 0, sizeof(result));

    if (kc_flow_open(&flow, argv[1]) != KC_FLOW_OK) {
        fprintf(stderr, "flow: failed to open flow file: %s\n", argv[1]);
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--entry") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "flow: -e/--entry requires a node\n");
                kc_flow_close(flow);
                return 1;
            }
            entry = argv[i];
        } else if (strcmp(argv[i], "-i") == 0 ||
            strcmp(argv[i], "--input") == 0)
        {
            if (++i >= argc || !argv[i][0] || input_path) {
                fprintf(stderr, "flow: -i/--input requires one path\n");
                kc_flow_close(flow);
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-s") == 0 ||
            strcmp(argv[i], "--set") == 0)
        {
            if (i + 2 >= argc || !argv[i + 1][0] || !argv[i + 2][0]) {
                fprintf(stderr, "flow: -s/--set requires <key> <value>\n");
                kc_flow_close(flow);
                return 1;
            }
            if (kc_flow_set(flow, argv[i + 1], argv[i + 2]) != KC_FLOW_OK) {
                fprintf(stderr, "flow: failed to set override: %s\n", argv[i + 1]);
                kc_flow_close(flow);
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "-u") == 0 ||
            strcmp(argv[i], "--unset") == 0)
        {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "flow: -u/--unset requires a key\n");
                kc_flow_close(flow);
                return 1;
            }
            if (kc_flow_unset(flow, argv[i]) != KC_FLOW_OK) {
                fprintf(stderr, "flow: failed to unset override: %s\n", argv[i]);
                kc_flow_close(flow);
                return 1;
            }
        } else {
            fprintf(stderr, "flow: unknown option: %s\n", argv[i]);
            kc_flow_close(flow);
            return 1;
        }
    }

    if (flow_cli_read_input(input_path, &input, &input_size) != 0) {
        fprintf(stderr, "flow: failed to read input: %s\n", input_path);
        kc_flow_close(flow);
        return 1;
    }

    if (kc_flow_run(
            flow,
            &run,
            entry,
            input,
            input_size,
            flow_cli_finished,
            &result
        ) != KC_FLOW_OK)
    {
        fprintf(stderr, "flow: failed to start flow\n");
        free(input);
        kc_flow_close(flow);
        return 1;
    }
    free(input);

    flow_cli_stop = 0;
    signal(SIGINT, flow_cli_signal);
    signal(SIGTERM, flow_cli_signal);
    while (!atomic_load(&result.done)) {
        if (flow_cli_stop && !stop_sent) {
            if (kc_flow_run_stop(run) != KC_FLOW_OK)
                fprintf(stderr, "flow: failed to request stop\n");
            stop_sent = 1;
        }
        flow_cli_sleep();
    }

    kc_flow_run_close(run);
    kc_flow_close(flow);

    if (result.status == KC_FLOW_OK) {
        if (result.data_size > 0U &&
            fwrite(result.data, 1U, result.data_size, stdout) != result.data_size)
        {
            kc_flow_free(result.data);
            free(result.error);
            return 1;
        }
        kc_flow_free(result.data);
        free(result.error);
        return 0;
    }

    if (result.error && result.error[0])
        fprintf(stderr, "flow: %s\n", result.error);
    else if (result.status == KC_FLOW_ESTOP)
        fprintf(stderr, "flow: stopped\n");
    else
        fprintf(stderr, "flow: execution failed\n");

    kc_flow_free(result.data);
    free(result.error);
    return result.status == KC_FLOW_ESTOP ? 130 : 1;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        flow_cli_help();
        return 0;
    }
    if (argc == 2 &&
        (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0))
    {
        printf("flow build %llu\n", (unsigned long long)kc_flow_version());
        return 0;
    }
    if (argc < 2) {
        flow_cli_help();
        return 1;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "flow: unknown option: %s\n", argv[1]);
        return 1;
    }

    return flow_cli_run(argc, argv);
}

