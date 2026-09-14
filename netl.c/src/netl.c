/**
 * netl.c - Network listener.
 * Summary: Command line interface for registering and running named network listeners.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libnetl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#  define _POSIX_C_SOURCE 200809L
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/types.h>
#else
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_netl_help(const char *name) {
    printf("Usage: %s <name> <addr>[:port] [--udp] <cmd>\n", name);
    printf("       %s <name> [-d|--delete]\n", name);
    printf("       %s <name> [-s|--stop]\n", name);
    printf("       %s <name> [-l|--list]\n", name);
    printf("       %s [-l|--list]\n", name);
    printf("       %s <name>\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  --tcp          Use TCP (default)\n");
    printf("  --udp          Use UDP\n");
    printf("  -d, --delete   Remove a registration\n");
    printf("  -s, --stop     Stop a running listener\n");
    printf("  -l, --list     List registrations\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Prints one registration entry as key followed by address.
 * @param key Registration name.
 * @param addrport Address and port string.
 * @param userdata Unused.
 * @return None.
 */
static void kc_netl_cli_list_cb(
const char *key,
const char *addrport,
void *userdata
) {
    (void)userdata;
    printf("%s\t%s\n", key, addrport);
}

/**
 * Lists all registrations.
 * @param opts Options.
 * @return 0 on success, 1 on failure.
 */
static int kc_netl_cli_list_all(const kc_netl_options_t *opts) {
    kc_netl_t *ctx = NULL;
    int rc;

    if (kc_netl_open(&ctx, opts) != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    rc = kc_netl_list(ctx, NULL, kc_netl_cli_list_cb, NULL);
    kc_netl_close(ctx);
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    return 0;
}

/**
 * Lists one named registration.
 * @param opts Options.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_netl_cli_list_one(const kc_netl_options_t *opts, const char *name) {
    kc_netl_t *ctx = NULL;
    int rc;

    if (kc_netl_open(&ctx, opts) != KC_NETL_OK) {
        fprintf(stderr, "netl: %s: not found\n", name);
        return 1;
    }
    rc = kc_netl_list(ctx, name, kc_netl_cli_list_cb, NULL);
    kc_netl_close(ctx);
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: %s: not found\n", name);
        return 1;
    }
    return 0;
}

/**
 * Removes a named registration.
 * @param opts Options.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_netl_cli_delete(const kc_netl_options_t *opts, const char *name) {
    kc_netl_t *ctx = NULL;
    int rc;

    if (kc_netl_open(&ctx, opts) != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    rc = kc_netl_delete(ctx, name);
    kc_netl_close(ctx);
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    return 0;
}

/**
 * Stops a named registration's recorded listener.
 * @param opts Options.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_netl_cli_stop(const kc_netl_options_t *opts, const char *name) {
    kc_netl_t *ctx = NULL;
    int rc;

    if (kc_netl_open(&ctx, opts) != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    rc = kc_netl_stop(ctx, name);
    kc_netl_close(ctx);
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    return 0;
}

/**
 * Registers or replaces a named listener.
 * @param opts  Options.
 * @param name  Registration name.
 * @param host  Bind host.
 * @param port  Bind port.
 * @param proto KC_NETL_TCP or KC_NETL_UDP.
 * @param cmd   Shell command to dispatch.
 * @return 0 on success, 1 on failure.
 */
static int kc_netl_cli_update(
const kc_netl_options_t *opts,
const char *name,
const char *host,
unsigned short port,
int proto,
const char *cmd
) {
    kc_netl_t *ctx = NULL;
    int rc;

    if (kc_netl_open(&ctx, opts) != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    rc = kc_netl_update(ctx, name, host, port, proto, cmd);
    kc_netl_close(ctx);
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: unknown command or invalid arguments\n");
        return 1;
    }
    return 0;
}

/**
 * Parses address and optional port from host[:port] text.
 * @param text     Input address text.
 * @param host     Output host buffer.
 * @param host_cap Output host capacity.
 * @param port     Output port pointer.
 * @return 0 on success, or 1 on failure.
 */
static int kc_netl_parse_addr(
const char *text,
char *host,
size_t host_cap,
unsigned short *port
) {
    const char *colon;
    char *end;
    unsigned long value;
    size_t n;

    if (!text || !text[0] || !host || host_cap == 0 || !port) return 1;
    colon = strrchr(text, ':');
    *port = 80;
    if (!colon) {
        n = strlen(text);
        if (n == 0 || n >= host_cap) return 1;
        memcpy(host, text, n + 1);
        return 0;
    }
    if (colon == text || colon[1] == '\0') return 1;
    n = (size_t)(colon - text);
    if (n == 0 || n >= host_cap) return 1;
    memcpy(host, text, n);
    host[n] = '\0';
    value = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || value == 0 || value > 65535) return 1;
    *port = (unsigned short)value;
    return 0;
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_netl_options_t opts = kc_netl_options_default();
    kc_netl_t *ctx = NULL;
    const char *name;
    int rc = 1;

    kc_netl_options_load_env(&opts);

#ifdef _WIN32
    if (argc >= 3 && strcmp(argv[1], "--_exec") == 0) {
        if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) goto cleanup;
        kc_netl_exec(ctx, argv[2]);
        kc_netl_close(ctx);
        goto cleanup;
    }
#endif

    if (argc < 2) {
        kc_netl_help(argv[0]);
        goto cleanup;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        kc_netl_help(argv[0]);
        rc = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("netl build %llu\n", (unsigned long long)kc_netl_version());
        rc = 0;
        goto cleanup;
    }
    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
        if (argc > 2) {
            fprintf(stderr, "netl: --list takes no arguments\n");
            goto cleanup;
        }
        rc = kc_netl_cli_list_all(&opts);
        goto cleanup;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "netl: unknown option '%s'\n", argv[1]);
        goto cleanup;
    }

    name = argv[1];

    if (argc == 2) {
        if (kc_netl_open(&ctx, &opts) != KC_NETL_OK) goto cleanup;
#ifndef _WIN32
        {
            pid_t pid;
            int nfd;
            pid = fork();
            if (pid < 0) {
                fprintf(stderr, "netl: fork failed\n");
                goto cleanup;
            }
            if (pid == 0) {
                setsid();
                nfd = open("/dev/null", O_RDWR);
                if (nfd >= 0) {
                    dup2(nfd, 0);
                    dup2(nfd, 1);
                    dup2(nfd, 2);
                    if (nfd > 2) close(nfd);
                }
                _exit(kc_netl_exec(ctx, name) == KC_NETL_OK ? 0 : 1);
            }
            (void)pid;
        }
#else
        {
            char exe[MAX_PATH];
            char cmdline[1024];
            STARTUPINFOA si = {0};
            PROCESS_INFORMATION pi = {0};
            if (!GetModuleFileNameA(NULL, exe, sizeof(exe))) {
                fprintf(stderr, "netl: failed to get executable path\n");
                goto cleanup;
            }
            if ((size_t)snprintf(cmdline, sizeof(cmdline),
                    "\"%s\" --_exec \"%s\"", exe, name) >= sizeof(cmdline)) {
                fprintf(stderr, "netl: name too long\n");
                goto cleanup;
            }
            si.cb = sizeof(si);
            if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                    DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                fprintf(stderr, "netl: failed to start listener\n");
                goto cleanup;
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
#endif
        rc = 0;
        goto cleanup;
    }

    if (strcmp(argv[2], "-d") == 0 || strcmp(argv[2], "--delete") == 0) {
        if (argc > 3) {
            fprintf(stderr, "netl: --delete takes no arguments\n");
            goto cleanup;
        }
        rc = kc_netl_cli_delete(&opts, name);
        goto cleanup;
    }
    if (strcmp(argv[2], "-s") == 0 || strcmp(argv[2], "--stop") == 0) {
        if (argc > 3) {
            fprintf(stderr, "netl: --stop takes no arguments\n");
            goto cleanup;
        }
        rc = kc_netl_cli_stop(&opts, name);
        goto cleanup;
    }
    if (strcmp(argv[2], "-l") == 0 || strcmp(argv[2], "--list") == 0) {
        if (argc > 3) {
            fprintf(stderr, "netl: --list takes no arguments\n");
            goto cleanup;
        }
        rc = kc_netl_cli_list_one(&opts, name);
        goto cleanup;
    }

    if (argc < 4) {
        kc_netl_help(argv[0]);
        goto cleanup;
    }

    {
        char host[256];
        unsigned short port = 80;
        int proto = KC_NETL_TCP;
        char cmd[4096];
        int cmd_len = 0;
        int i;

        if (kc_netl_parse_addr(argv[2], host, sizeof(host), &port) != 0) {
            fprintf(stderr, "netl: invalid address '%s'\n", argv[2]);
            goto cleanup;
        }

        for (i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--tcp") == 0) {
                proto = KC_NETL_TCP;
            } else if (strcmp(argv[i], "--udp") == 0) {
                proto = KC_NETL_UDP;
            } else {
                int slen = (int)strlen(argv[i]);
                if (cmd_len > 0) {
                    if (cmd_len >= (int)sizeof(cmd) - 1) {
                        fprintf(stderr, "netl: command too long\n");
                        goto cleanup;
                    }
                    cmd[cmd_len++] = ' ';
                }
                if (cmd_len + slen >= (int)sizeof(cmd)) {
                    fprintf(stderr, "netl: command too long\n");
                    goto cleanup;
                }
                memcpy(cmd + cmd_len, argv[i], (size_t)slen + 1);
                cmd_len += slen;
            }
        }

        if (cmd_len == 0) {
            kc_netl_help(argv[0]);
            goto cleanup;
        }

        rc = kc_netl_cli_update(&opts, name, host, port, proto, cmd);
        goto cleanup;
    }

cleanup:
    if (ctx) kc_netl_close(ctx);
    kc_netl_options_free(&opts);
    return rc;
}
