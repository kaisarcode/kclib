/**
 * dmn.c - IPC Daemon Manager
 * Summary: Command line interface for the dmn tool.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libdmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#include <windows.h>
#endif

#define KC_DMN_BUF 4096

/**
 * Joins argv[from..to) into buf with space separators.
 * @param buf Output buffer.
 * @param cap Buffer capacity.
 * @param argv Argument vector.
 * @param from Start index.
 * @param to End index.
 * @return Bytes written excluding null terminator.
 */
static size_t kc_dmn_join_args(char *buf, size_t cap, char **argv, int from, int to) {
    size_t pos = 0;
    int j;

    buf[0] = '\0';

    for (j = from; j < to; j++) {
        int n;

        if (pos > 0 && pos < cap - 1) {
            buf[pos++] = ' ';
        }

        n = snprintf(buf + pos, cap - pos, "%s", argv[j]);
        if (n < 0 || (size_t)n >= cap - pos) {
            break;
        }

        pos += (size_t)n;
    }

    return pos;
}

/**
 * Prints command usage to standard output.
 * @return None.
 */
static void kc_dmn_help(void) {
    printf("Usage: dmn <name> [command...]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  dmn <name> <cmd>     Start or update a daemon\n");
    printf("  dmn -d <name>        Delete a daemon\n");
    printf("  dmn <name> -d        Delete a daemon\n");
    printf("  dmn -l [name]        List daemons\n");
    printf("  dmn <name> -l        List one daemon\n");
    printf("  dmn <name> -s <sig>  Send signal to a daemon\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list           List daemons\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
}

/**
 * Prints version to standard output.
 * @return None.
 */
static void kc_dmn_print_version(void) {
    printf("dmn build %llu\n", (unsigned long long)kc_dmn_version());
}

/**
 * List callback for CLI - prints key and socket path.
 * @param key Daemon key name.
 * @param sock Socket path.
 * @param userdata Unused.
 * @return None.
 */
static void kc_dmn_cli_list_cb(const char *key, const char *sock, void *userdata) {
    (void)userdata;
    printf("%s\t%s\n", key, sock);
}

#ifdef _WIN32
/**
 * Private Windows --_serve implementation.
 * @param pipename Named Pipe path.
 * @param cmd Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_dmn_cli_serve_win32(
    const char *pipename, const char *cmd
) {
    SECURITY_ATTRIBUTES sa;
    char cmdstr[KC_DMN_BUF];
    char buf[KC_DMN_BUF];
    char evname[64];
    HANDLE hSignalEvent;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if ((size_t)snprintf(cmdstr, sizeof(cmdstr),
            "cmd.exe /c %s", cmd) >= sizeof(cmdstr))
        return 1;
    snprintf(evname, sizeof(evname), "Global\\DmnSignal_%lu",
        (unsigned long)GetCurrentProcessId());
    hSignalEvent = CreateEventA(NULL, FALSE, FALSE, evname);
    (void)hSignalEvent;

    while (1) {
        HANDLE h = CreateNamedPipeA(pipename, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, KC_DMN_BUF, KC_DMN_BUF, 0, NULL);
        HANDLE in_rd, in_wr, out_rd, out_wr;
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD br, avail, exit_code;

        if (h == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }
        if (!ConnectNamedPipe(h, NULL) &&
                GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            continue;
        }
        if (!CreatePipe(&in_rd, &in_wr, &sa, 0) ||
                !CreatePipe(&out_rd, &out_wr, &sa, 0)) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_rd;
        si.hStdOutput = out_wr;
        si.hStdError = out_wr;
        memset(&pi, 0, sizeof(pi));
        if (!CreateProcessA(NULL, cmdstr, NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(in_rd);
            CloseHandle(in_wr);
            CloseHandle(out_rd);
            CloseHandle(out_wr);
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        CloseHandle(in_rd);
        CloseHandle(out_wr);
        CloseHandle(pi.hThread);
        while (1) {
            if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) &&
                    avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf) ?
                    avail : (DWORD)sizeof(buf);
                if (ReadFile(h, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0, written;
                    while (off < br && WriteFile(in_wr, buf + off,
                            br - off, &written, NULL) && written > 0)
                        off += written;
                }
            }
            if (PeekNamedPipe(out_rd, NULL, 0, NULL, &avail, NULL) &&
                    avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf) ?
                    avail : (DWORD)sizeof(buf);
                if (ReadFile(out_rd, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0, written;
                    while (off < br && WriteFile(h, buf + off,
                            br - off, &written, NULL) && written > 0)
                        off += written;
                }
            }
            if (!GetExitCodeProcess(pi.hProcess, &exit_code) ||
                    exit_code != STILL_ACTIVE)
                break;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) &&
                    GetLastError() == ERROR_BROKEN_PIPE)
                break;
            Sleep(5);
        }
        CloseHandle(in_wr);
        CloseHandle(out_rd);
        CloseHandle(pi.hProcess);
        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
}
#endif

/**
 * Relays standard input and output through a daemon connection.
 * @param ctx Dmn context.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
static int kc_dmn_cli_relay(kc_dmn_t *ctx, const char *key) {
    kc_dmn_conn_t *conn = NULL;
    unsigned char buf[KC_DMN_BUF];
    int stdin_open = 1;
    int rc = KC_DMN_OK;

    if (kc_dmn_connect(ctx, key, &conn) != KC_DMN_OK)
        return KC_DMN_ERROR;

    while (stdin_open) {
        size_t n = fread(buf, 1, sizeof(buf), stdin);
        void *data = NULL;
        size_t data_size = 0;
        int input_eot;
        int response_eot = 0;

        if (n == 0) {
            if (ferror(stdin))
                rc = KC_DMN_ERROR;
            break;
        }

        if (kc_dmn_send(conn, buf, n) != KC_DMN_OK) {
            rc = KC_DMN_ERROR;
            break;
        }
        input_eot = memchr(buf, 4, n) != NULL;
        if (input_eot)
            stdin_open = 0;

        do {
            data = NULL;
            data_size = 0;
            rc = kc_dmn_recv(conn, sizeof(buf), &data, &data_size);
            if (rc == KC_DMN_EOF) {
                kc_dmn_free(data);
                rc = KC_DMN_OK;
                break;
            }
            if (rc != KC_DMN_OK) {
                kc_dmn_free(data);
                break;
            }
            response_eot = memchr(data, 4, data_size) != NULL;
            if (fwrite(data, 1, data_size, stdout) != data_size) {
                kc_dmn_free(data);
                rc = KC_DMN_ERROR;
                break;
            }
            kc_dmn_free(data);
            if (fflush(stdout) != 0) {
                rc = KC_DMN_ERROR;
                break;
            }
        } while (input_eot && !response_eot);

        if (rc != KC_DMN_OK || response_eot)
            break;
        if (!stdin_open)
            break;
    }

    kc_dmn_disconnect(conn);
    return rc;
}

/**
 * Executes the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_dmn_options_t *opts = kc_dmn_options_default();
    const char *dir;
    int i = 1;

    if (!opts) {
        fprintf(stderr, "dmn: cannot allocate options\n");
        return 1;
    }

    dir = getenv("KC_DMN_DIR");
    if (dir && kc_dmn_options_set(opts, "dir", dir) != KC_DMN_OK) {
        fprintf(stderr, "dmn: cannot set runtime directory\n");
        kc_dmn_options_free(opts);
        return 1;
    }

    if (i >= argc) {
        kc_dmn_help();
        kc_dmn_options_free(opts);
        return 1;
    }

    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        kc_dmn_help();
        kc_dmn_options_free(opts);
        return 0;
    }

    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
        kc_dmn_print_version();
        kc_dmn_options_free(opts);
        return 0;
    }

#ifdef _WIN32
    if (strcmp(argv[i], "--_serve") == 0) {
        char cmd[KC_DMN_BUF];

        if (i + 2 >= argc) {
            kc_dmn_options_free(opts);
            return 1;
        }

        kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 2, argc);
        {
            int rc = kc_dmn_cli_serve_win32(argv[i + 1], cmd) == 0 ? 0 : 1;
            kc_dmn_options_free(opts);
            return rc;
        }
    }
#endif

    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
        const char *key = (i + 2 == argc) ? argv[i + 1] : NULL;
        kc_dmn_t *ctx = NULL;
        int rc;

        if (i + 2 < argc) {
            kc_dmn_options_free(opts);
            return 1;
        }

        if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_options_free(opts);
            return 1;
        }

        rc = kc_dmn_list(ctx, key, kc_dmn_cli_list_cb, NULL);
        kc_dmn_close(ctx);
        kc_dmn_options_free(opts);
        return rc == KC_DMN_OK ? 0 : 1;
    }

    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
        kc_dmn_t *ctx = NULL;
        int rc;

        if (i + 2 != argc) {
            kc_dmn_options_free(opts);
            return 1;
        }

        if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_options_free(opts);
            return 1;
        }

        rc = kc_dmn_delete(ctx, argv[i + 1]);
        kc_dmn_close(ctx);
        kc_dmn_options_free(opts);
        return rc == KC_DMN_OK ? 0 : 1;
    }

    if (argv[i][0] == '-') {
        kc_dmn_help();
            kc_dmn_options_free(opts);
        return 1;
    }

    if (i + 1 < argc) {
        if (strcmp(argv[i + 1], "-d") == 0 || strcmp(argv[i + 1], "--delete") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;

            if (i + 2 != argc) {
                kc_dmn_options_free(opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(opts);
                return 1;
            }

            rc = kc_dmn_delete(ctx, argv[i]);
            kc_dmn_close(ctx);
            kc_dmn_options_free(opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        if (strcmp(argv[i + 1], "-l") == 0 || strcmp(argv[i + 1], "--list") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;

            if (i + 2 != argc) {
                kc_dmn_options_free(opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(opts);
                return 1;
            }

            rc = kc_dmn_list(ctx, argv[i], kc_dmn_cli_list_cb, NULL);
            kc_dmn_close(ctx);
            kc_dmn_options_free(opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        if (strcmp(argv[i + 1], "-s") == 0 || strcmp(argv[i + 1], "--signal") == 0) {
            kc_dmn_t *ctx = NULL;
            int rc;
            int signo = atoi(argv[i + 2]);

            if (i + 3 != argc) {
                kc_dmn_options_free(opts);
                return 1;
            }

            if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(opts);
                return 1;
            }

            rc = kc_dmn_signal(ctx, argv[i], signo);
            kc_dmn_close(ctx);
            kc_dmn_options_free(opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        {
            kc_dmn_t *ctx = NULL;
            char cmd[KC_DMN_BUF];
            int rc;

            kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 1, argc);

            if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
                fprintf(stderr, "dmn: cannot resolve runtime directory\n");
                kc_dmn_options_free(opts);
                return 1;
            }

            rc = kc_dmn_update(ctx, argv[i], cmd);
            kc_dmn_close(ctx);
            kc_dmn_options_free(opts);
            return rc == KC_DMN_OK ? 0 : 1;
        }
    }

    {
        kc_dmn_t *ctx = NULL;
        int rc;

        if (kc_dmn_open(&ctx, opts) != KC_DMN_OK) {
            fprintf(stderr, "dmn: cannot resolve runtime directory\n");
            kc_dmn_close(ctx);
            kc_dmn_options_free(opts);
            return 1;
        }

        rc = kc_dmn_cli_relay(ctx, argv[i]);
        kc_dmn_close(ctx);
        kc_dmn_options_free(opts);
        return rc == KC_DMN_OK ? 0 : 1;
    }

    kc_dmn_help();
    kc_dmn_options_free(opts);
    return 1;
}
