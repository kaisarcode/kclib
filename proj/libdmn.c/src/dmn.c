/**
 * dmn.c - Local daemon manager CLI.
 * Summary: Manages named resident daemons and exchanges data with them.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libdmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DMN_CLI_BUF 4096

/**
 * Print command usage.
 * @return None.
 */
static void dmn_cli_help(void)
{
    printf("Usage: dmn <id> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  dmn <id> <cmd>  Create or update a daemon\n");
    printf("  dmn -l [id]      List daemons\n");
    printf("  dmn <id> -d      Delete a daemon\n");
    printf("  dmn <id>         Send stdin to a daemon\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list       List daemons\n");
    printf("  -d, --delete     Delete a daemon\n");
    printf("  -h, --help       Show this help\n");
    printf("  -v, --version    Show version\n");
}

/**
 * Print linked libdmn build version.
 * @return None.
 */
static void dmn_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_dmn_version());
}

/**
 * Join positional command arguments.
 * @param out Output command buffer.
 * @param cap Buffer capacity.
 * @param argv Compacted positional argument vector.
 * @param first First command argument index.
 * @param argc Compacted argument count.
 * @return Zero on success, nonzero on overflow.
 */
static int dmn_cli_join_args(
    char *out,
    size_t cap,
    char **argv,
    int first,
    int argc
)
{
    size_t used = 0U;
    int i;

    if (!out || cap == 0U || first >= argc) return 1;
    out[0] = '\0';

    for (i = first; i < argc; i++) {
        size_t size = strlen(argv[i]);

        if (used != 0U) {
            if (used + 1U >= cap) return 1;
            out[used++] = ' ';
        }
        if (size >= cap - used) return 1;
        memcpy(out + used, argv[i], size);
        used += size;
        out[used] = '\0';
    }

    return used == 0U ? 1 : 0;
}

/**
 * Read all stdin bytes.
 * @param out Output allocation.
 * @param out_size Output byte count.
 * @return Zero on success, nonzero on failure.
 */
static int dmn_cli_read_stdin(void **out, size_t *out_size)
{
    unsigned char *data = NULL;
    size_t size = 0U;
    size_t capacity = 0U;
    unsigned char chunk[4096];

    if (!out || !out_size) return 1;
    *out = NULL;
    *out_size = 0U;

    for (;;) {
        size_t amount = fread(chunk, 1, sizeof(chunk), stdin);

        if (amount != 0U) {
            unsigned char *next;
            size_t needed;
            size_t next_capacity;

            if (size > (size_t)-1 - amount) {
                free(data);
                return 1;
            }
            needed = size + amount;
            if (needed > capacity) {
                next_capacity = capacity ? capacity : DMN_CLI_BUF;
                while (next_capacity < needed) {
                    if (next_capacity > (size_t)-1 / 2U) {
                        next_capacity = needed;
                        break;
                    }
                    next_capacity *= 2U;
                }
                next = (unsigned char *)realloc(data, next_capacity);
                if (!next) {
                    free(data);
                    return 1;
                }
                data = next;
                capacity = next_capacity;
            }
            memcpy(data + size, chunk, amount);
            size = needed;
        }

        if (amount < sizeof(chunk)) {
            if (ferror(stdin)) {
                free(data);
                return 1;
            }
            break;
        }
    }

    *out = data;
    *out_size = size;
    return 0;
}

/**
 * Print one daemon registration.
 * @param name Daemon id.
 * @return Zero on success, nonzero on failure.
 */
static int dmn_cli_print(const char *name)
{
    kc_dmn_t *daemon = NULL;
    const char *command;

    if (kc_dmn_open(&daemon, name) != KC_DMN_OK) return 1;
    command = kc_dmn_get_cmd(daemon);
    if (!command) {
        kc_dmn_close(daemon);
        return 1;
    }
    printf("%s\t%s\n", name, command);
    kc_dmn_close(daemon);
    return 0;
}

/**
 * List one daemon or all registered daemons.
 * @param filter Optional daemon id.
 * @return Zero on success, nonzero on failure.
 */
static int dmn_cli_list(const char *filter)
{
    kc_dmn_entry_t *entries = NULL;
    size_t count = 0U;
    size_t i;
    int failed = 0;

    if (kc_dmn_list(&entries, &count) != KC_DMN_OK) return 1;

    for (i = 0U; i < count; i++) {
        if (filter && strcmp(filter, entries[i].name) != 0) continue;
        if (dmn_cli_print(entries[i].name) != 0) failed = 1;
    }

    kc_dmn_free(entries);
    return failed;
}

/**
 * Send one stdin request to an existing daemon.
 * @param name Daemon id.
 * @return Zero on success, nonzero on failure.
 */
static int dmn_cli_send(const char *name)
{
    kc_dmn_t *daemon = NULL;
    void *request = NULL;
    size_t request_size = 0U;
    void *response = NULL;
    size_t response_size = 0U;
    int rc;

    if (kc_dmn_open(&daemon, name) != KC_DMN_OK) return 1;
    if (dmn_cli_read_stdin(&request, &request_size) != 0) {
        kc_dmn_close(daemon);
        return 1;
    }

    rc = kc_dmn_send_data(
        daemon,
        request,
        request_size,
        &response,
        &response_size
    );
    free(request);
    kc_dmn_close(daemon);
    if (rc != KC_DMN_OK) {
        kc_dmn_free(response);
        return 1;
    }

    if (response_size != 0U &&
            fwrite(response, 1, response_size, stdout) != response_size) {
        kc_dmn_free(response);
        return 1;
    }
    kc_dmn_free(response);
    return fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    const char *name;
    char command[DMN_CLI_BUF];
    int list = 0;
    int remove = 0;
    int positional = 0;
    int i;

    if (argc == 2 &&
            (strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0)) {
        dmn_cli_help();
        return 0;
    }
    if (argc == 2 &&
            (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0)) {
        dmn_cli_version();
        return 0;
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 ||
                strcmp(argv[i], "--help") == 0) {
            dmn_cli_help();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 ||
                strcmp(argv[i], "--version") == 0) {
            dmn_cli_version();
            return 0;
        }
        if (strcmp(argv[i], "-l") == 0 ||
                strcmp(argv[i], "--list") == 0) {
            list = 1;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0 ||
                strcmp(argv[i], "--delete") == 0) {
            remove = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "dmn: unknown option: %s\n", argv[i]);
            return 1;
        }

        positional++;
        argv[positional] = argv[i];
    }

    name = positional >= 1 ? argv[1] : NULL;

    if (list) {
        if (remove || positional > 1) {
            fprintf(stderr, "dmn: invalid list arguments\n");
            return 1;
        }
        return dmn_cli_list(name);
    }

    if (!name) {
        fprintf(stderr, "dmn: missing id\n");
        return 1;
    }

    if (remove) {
        if (positional != 1) {
            fprintf(stderr, "dmn: invalid delete arguments\n");
            return 1;
        }
        return kc_dmn_delete(name) == KC_DMN_OK ? 0 : 1;
    }

    if (positional == 1) return dmn_cli_send(name);

    if (dmn_cli_join_args(
            command,
            sizeof(command),
            argv,
            2,
            positional + 1
        ) != 0) {
        fprintf(stderr, "dmn: command is too long\n");
        return 1;
    }

    {
        kc_dmn_options_t options;

        memset(&options, 0, sizeof(options));
        options.cmd = command;
        return kc_dmn_create(name, &options) == KC_DMN_OK ? 0 : 1;
    }
}

