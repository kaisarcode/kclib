/**
 * stun.c - STUN command-line interface.
 * Summary: Runs a STUN Binding client or server.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libstun.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static volatile sig_atomic_t stun_cli_stop;

/**
 * Prints CLI help.
 * @param name Program name.
 * @return None.
 */
static void stun_cli_help(const char *name)
{
    printf("Usage: %s <client|server> [arguments] [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  %s client <server>    Discover a public UDP endpoint\n", name);
    printf("  %s server             Serve STUN Binding requests\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  --host <host>           Local bind host\n");
    printf("  -p, --port <port>       Local UDP port (client: 0, server: 3478)\n");
    printf("  -t, --timeout <ms>      Client timeout in ms (default: library)\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Parses one nonnegative decimal integer.
 * @param input Decimal text.
 * @param out Parsed value.
 * @return Nonzero on success.
 */
static int stun_cli_u64(const char *input, uint64_t *out)
{
    unsigned long long value;
    char *end = NULL;

    if (!input || !*input || *input == '-' || !out) return 0;
    errno = 0;
    value = strtoull(input, &end, 10);
    if (errno || !end || *end != '\0') return 0;
    *out = (uint64_t)value;
    return 1;
}

/**
 * Parses a local UDP port.
 * @param input Decimal text.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int stun_cli_port(const char *input, unsigned short *out)
{
    uint64_t value;

    if (!stun_cli_u64(input, &value) || value > 65535U) return 0;
    *out = (unsigned short)value;
    return 1;
}

/**
 * Prints a CLI error.
 * @param message Error description.
 * @return Nonzero exit status.
 */
static int stun_cli_error(const char *message)
{
    fprintf(stderr, "stun: %s\n", message);
    return 1;
}

/**
 * Marks a server shutdown request.
 * @param signum Received signal.
 * @return None.
 */
static void stun_cli_signal(int signum)
{
    (void)signum;
    stun_cli_stop = 1;
}

/**
 * Executes the STUN Binding client.
 * @param options Client options.
 * @return Process exit status.
 */
static int stun_cli_client(const kc_stun_options_t *options)
{
    kc_stun_mapping_t *mapping = NULL;
    int rc = kc_stun_discover(&mapping, options);

    if (rc != KC_STUN_OK) {
        fprintf(stderr, "stun: client: %s\n", kc_stun_strerror(rc));
        return 1;
    }
    printf("%s\t%u\n", kc_stun_mapped_host(mapping),
        kc_stun_mapped_port(mapping));
    kc_stun_close(mapping);
    return 0;
}

/**
 * Executes the STUN Binding server.
 * @param options Server options.
 * @return Process exit status.
 */
static int stun_cli_server(const kc_stun_server_options_t *options)
{
    kc_stun_server_t *server = NULL;
    int rc = kc_stun_listen(&server, options);

    if (rc != KC_STUN_OK) {
        fprintf(stderr, "stun: server: %s\n", kc_stun_strerror(rc));
        return 1;
    }
    printf("%u\n", kc_stun_server_port(server));
    fflush(stdout);
    stun_cli_stop = 0;
    (void)signal(SIGINT, stun_cli_signal);
    (void)signal(SIGTERM, stun_cli_signal);
    while (!stun_cli_stop) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    kc_stun_server_close(server);
    return 0;
}

/**
 * Dispatches the public CLI operations.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    kc_stun_options_t client;
    kc_stun_server_options_t server;
    int is_client;
    int is_server;
    int i;

    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        stun_cli_help(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_stun_version());
        return 0;
    }
    if (argc < 2) {
        stun_cli_help(argv[0]);
        return 1;
    }

    is_client = strcmp(argv[1], "client") == 0;
    is_server = strcmp(argv[1], "server") == 0;
    if (!is_client && !is_server) return stun_cli_error("invalid command");

    memset(&client, 0, sizeof(client));
    memset(&server, 0, sizeof(server));
    server.port = 3478;

    for (i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--host") == 0) {
            const char *host;
            if (++i >= argc || !argv[i][0])
                return stun_cli_error("--host requires a host");
            host = argv[i];
            if (is_client) client.host = host;
            else server.host = host;
        } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--port") == 0) {
            unsigned short port;
            if (++i >= argc || !stun_cli_port(argv[i], &port))
                return stun_cli_error("invalid port");
            if (is_client) client.port = port;
            else server.port = port;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--timeout") == 0) {
            if (!is_client) return stun_cli_error("--timeout is client-only");
            if (++i >= argc || !stun_cli_u64(argv[i], &client.timeout_ms))
                return stun_cli_error("invalid timeout");
        } else if (is_client && !client.server && arg[0] != '-') {
            client.server = arg;
        } else {
            return stun_cli_error("unexpected argument");
        }
    }

    if (is_client) {
        if (!client.server) return stun_cli_error("client requires a server");
        return stun_cli_client(&client);
    }
    return stun_cli_server(&server);
}
