/**
 * nets.c - Network sender.
 * Summary: Command line interface for sending stdin to TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libnets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define KC_NETS_STDIN_FD 0
#else
#include <unistd.h>
#define KC_NETS_STDIN_FD STDIN_FILENO
#endif

/**
 * Reads all available bytes from a file descriptor into a malloc'd buffer.
 * CLI-only utility for reading stdin.
 * @param fd        Source file descriptor.
 * @param out_data  Output buffer pointer (caller frees with free()).
 * @param out_size  Output size pointer.
 * @return 0 on success, or 1 on failure.
 */
static int nets_read_fd(int fd, char **out_data, size_t *out_size) {
    char *data = NULL;
    size_t used = 0;
    size_t cap = 0;
    char buf[8192];
#ifdef _WIN32
    int n;
#else
    ssize_t n;
#endif

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!out_data || !out_size) return 1;

    for (;;) {
#ifdef _WIN32
        n = _read(fd, buf, (unsigned)sizeof(buf));
#else
        n = read(fd, buf, sizeof(buf));
#endif
        if (n < 0) { free(data); return 1; }
        if (n == 0) break;
        if (used + (size_t)n > cap) {
            size_t next = cap ? cap * 2 : 8192;
            char *tmp;
            while (next < used + (size_t)n) next *= 2;
            tmp = (char *)realloc(data, next);
            if (!tmp) { free(data); return 1; }
            data = tmp;
            cap = next;
        }
        memcpy(data + used, buf, (size_t)n);
        used += (size_t)n;
    }

    if (!data) {
        data = (char *)malloc(1);
        if (!data) return 1;
    }
    data[used] = '\0';
    *out_data = data;
    *out_size = used;
    return 0;
}

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_nets_help(const char *name) {
    printf("Usage: %s <target> [--tcp|--udp|--tls]\n", name);
    printf("\n");
    printf("Target:\n");
    printf("  host[:port] or http://, https://, tcp://, udp:// URL\n");
    printf("\n");
    printf("Options:\n");
    printf("  --tcp          Use TCP (default)\n");
    printf("  --udp          Use UDP\n");
    printf("  --tls          Use TLS over TCP\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_nets_cli_version(void) {
    printf("nets build %llu\n", (unsigned long long)kc_nets_version());
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    char *input = NULL;
    size_t input_size = 0;
    char host[512];
    unsigned short port = 80;
    int proto = KC_NETS_TCP;
    const char *proto_flag = "tcp";
    const char *target = NULL;
    kc_nets_options_t opts;
    kc_nets_t *ctx = NULL;
    char *resp = NULL;
    size_t resp_size = 0;
    int rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_nets_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_nets_cli_version();
            return 0;
        } else if (strcmp(argv[i], "--tcp") == 0) {
            proto_flag = "tcp";
        } else if (strcmp(argv[i], "--udp") == 0) {
            proto_flag = "udp";
        } else if (strcmp(argv[i], "--tls") == 0) {
            proto_flag = "tls";
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "nets: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (!target) {
            target = argv[i];
        } else {
            fprintf(stderr, "nets: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!target) {
        kc_nets_help(argv[0]);
        return 1;
    }

    if (nets_read_fd(KC_NETS_STDIN_FD, &input, &input_size) != 0) {
        fprintf(stderr, "nets: failed to read stdin\n");
        return 1;
    }

    if (target[0] == '\0') {
        fprintf(stderr, "nets: missing \"target\"\n");
        free(input);
        return 1;
    }

    if (strcmp(proto_flag, "tls") == 0) {
        if (!kc_nets_tls_available()) {
            fprintf(stderr, "nets: TLS not available (compiled without OpenSSL)\n");
            free(input);
            return 1;
        }
        proto = KC_NETS_TLS;
    } else if (strcmp(proto_flag, "udp") == 0) {
        proto = KC_NETS_UDP;
    }

    if (kc_nets_parse_target(target, host, sizeof(host), &port, &proto) != 0) {
        fprintf(stderr, "nets: invalid target\n");
        free(input);
        return 1;
    }

    opts = kc_nets_options_default();
    if (kc_nets_open(&ctx, &opts) != KC_NETS_OK) {
        fprintf(stderr, "nets: out of memory\n");
        free(input);
        return 1;
    }

    rc = kc_nets_send(ctx, host, port, proto, input, input_size, &resp, &resp_size);
    if (rc != KC_NETS_OK) {
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(rc));
        free(resp);
        free(input);
        kc_nets_close(ctx);
        return 1;
    }

    if (resp != NULL && resp_size > 0) {
        fwrite(resp, 1, resp_size, stdout);
    }
    free(resp);
    free(input);
    kc_nets_close(ctx);
    return 0;
}
