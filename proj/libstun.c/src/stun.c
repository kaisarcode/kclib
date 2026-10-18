/**
 * stun.c - STUN mapping command-line interface.
 * Summary: Discovers UDP mappings and serves Binding requests.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libstun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Prints CLI usage.
 * @param name Program name.
 * @return None.
 */
static void stun_help(const char *name) {
    printf("Usage: %s client <server> [timeout-ms]\n", name);
    printf("       %s server [port]\n", name);
}

/**
 * Runs the STUN command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    kc_stun_mapping_t *mapping = NULL;
    kc_stun_server_t *server = NULL;
    kc_stun_options_t options;
    kc_stun_server_options_t server_options;
    int status;

    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        stun_help(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_stun_version());
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "client")) {
        memset(&options, 0, sizeof(options));
        options.server = argv[2];
        if (argc == 4) options.timeout_ms = strtoull(argv[3], NULL, 10);
        status = kc_stun_discover(&mapping, &options);
        if (status == KC_STUN_OK) {
            printf("%s\t%u\n", kc_stun_mapped_host(mapping),
                kc_stun_mapped_port(mapping));
            kc_stun_close(mapping);
            return 0;
        }
        fprintf(stderr, "stun: %s\n", kc_stun_strerror(status));
        return 1;
    }
    if (argc >= 2 && argc <= 3 && !strcmp(argv[1], "server")) {
        memset(&server_options, 0, sizeof(server_options));
        if (argc == 3) server_options.port = (unsigned short)strtoul(argv[2], NULL, 10);
        status = kc_stun_listen(&server, &server_options);
        if (status != KC_STUN_OK) return 1;
        printf("%u\n", kc_stun_server_port(server));
        fflush(stdout);
        for (;;) {
#ifdef _WIN32
            Sleep(1000);
#else
            sleep(1);
#endif
        }
    }
    stun_help(argv[0]);
    return 1;
}
