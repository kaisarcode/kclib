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

#ifndef _WIN32
#  include <sys/stat.h>
#  include <unistd.h>
#endif

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
 * Find one byte sequence inside another.
 * @param data Source bytes.
 * @param data_size Source byte count.
 * @param needle Sequence to locate.
 * @param needle_size Sequence byte count.
 * @return Byte offset, or maximum size value when absent.
 */
static size_t kc_dmn_find_bytes(
    const unsigned char *data,
    size_t data_size,
    const unsigned char *needle,
    size_t needle_size
) {
    size_t i;

    if (!data || !needle || needle_size == 0 || data_size < needle_size) {
        return (size_t)-1;
    }
    for (i = 0; i + needle_size <= data_size; i++) {
        if (memcmp(data + i, needle, needle_size) == 0) return i;
    }
    return (size_t)-1;
}

/**
 * Append bytes to one growing CLI buffer.
 * @param data Buffer pointer.
 * @param size Current byte count.
 * @param capacity Current capacity.
 * @param chunk Source bytes.
 * @param chunk_size Source byte count.
 * @return Zero on success, nonzero on failure.
 */
static int kc_dmn_cli_append(
    unsigned char **data,
    size_t *size,
    size_t *capacity,
    const void *chunk,
    size_t chunk_size
) {
    unsigned char *next;
    size_t needed;
    size_t cap;

    if (chunk_size == 0) return 0;
    if (!data || !size || !capacity || !chunk) return 1;
    if (*size > (size_t)-1 - chunk_size) return 1;

    needed = *size + chunk_size;
    if (needed > *capacity) {
        cap = *capacity ? *capacity : KC_DMN_BUF;
        while (cap < needed) {
            if (cap > (size_t)-1 / 2) {
                cap = needed;
                break;
            }
            cap *= 2;
        }
        next = (unsigned char *)realloc(*data, cap);
        if (!next) return 1;
        *data = next;
        *capacity = cap;
    }

    memcpy(*data + *size, chunk, chunk_size);
    *size = needed;
    return 0;
}

/**
 * Relay one complete EOT-delimited request through the public API.
 * @param daemon Open daemon handle.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
static int kc_dmn_cli_relay(kc_dmn_t *daemon) {
    unsigned char buf[KC_DMN_BUF];
    unsigned char *request = NULL;
    unsigned char *response = NULL;
    const unsigned char *eot;
    size_t request_size = 0;
    size_t request_capacity = 0;
    size_t response_size = 0;
    size_t eot_size = 0;
    int rc = KC_DMN_ERROR;

    eot = (const unsigned char *)kc_dmn_get_eot(daemon, &eot_size);
    if (!eot || eot_size == 0) return KC_DMN_ERROR;

    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), stdin);
        size_t marker;

        if (n == 0) {
            if (ferror(stdin)) goto done;
            break;
        }
        if (kc_dmn_cli_append(
                &request,
                &request_size,
                &request_capacity,
                buf,
                n
            ) != 0)
            goto done;

        marker = kc_dmn_find_bytes(
            request,
            request_size,
            eot,
            eot_size
        );
        if (marker != (size_t)-1) {
            request_size = marker;
            break;
        }
    }

    if (kc_dmn_send_data(
            daemon,
            request,
            request_size,
            (void **)&response,
            &response_size
        ) != KC_DMN_OK)
        goto done;

    if (response_size > 0 &&
            fwrite(response, 1, response_size, stdout) != response_size)
        goto done;
    if (fwrite(eot, 1, eot_size, stdout) != eot_size) goto done;
    if (fflush(stdout) != 0) goto done;

    rc = KC_DMN_OK;

done:
    free(request);
    kc_dmn_free(response);
    return rc;
}

#ifndef _WIN32
/**
 * Resolve the daemon runtime directory for CLI endpoint display.
 * @param out Output path buffer.
 * @param cap Output capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_dmn_cli_runtime_dir(char *out, size_t cap) {
    const char *override = getenv("KC_DMN_DIR");
    const char *xdg;
    char path[KC_DMN_BUF];
    struct stat st;

    if (!out || cap == 0) return 1;
    if (override && override[0]) {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }

    xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0]) {
        return (size_t)snprintf(
            out,
            cap,
            "%s/kaisarcode/dmn",
            xdg
        ) < cap ? 0 : 1;
    }

    if ((size_t)snprintf(
            path,
            sizeof(path),
            "/run/user/%u",
            (unsigned)getuid()
        ) < sizeof(path) &&
            stat(path, &st) == 0 &&
            S_ISDIR(st.st_mode)) {
        return (size_t)snprintf(
            out,
            cap,
            "%s/kaisarcode/dmn",
            path
        ) < cap ? 0 : 1;
    }

    return (size_t)snprintf(
        out,
        cap,
        "/tmp/kaisarcode/dmn-%u",
        (unsigned)getuid()
    ) < cap ? 0 : 1;
}
#endif

/**
 * Compose one daemon endpoint for CLI display.
 * @param name Daemon name.
 * @param out Output path buffer.
 * @param cap Output capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_dmn_cli_endpoint(
    const char *name,
    char *out,
    size_t cap
) {
#ifdef _WIN32
    return (size_t)snprintf(
        out,
        cap,
        "\\\\.\\pipe\\kc-dmn-%s",
        name
    ) < cap ? 0 : 1;
#else
    char dir[KC_DMN_BUF];

    if (kc_dmn_cli_runtime_dir(dir, sizeof(dir)) != 0) return 1;
    return (size_t)snprintf(
        out,
        cap,
        "%s/%s",
        dir,
        name
    ) < cap ? 0 : 1;
#endif
}

/**
 * Print daemon list rows while keeping endpoint details CLI-local.
 * @param filter Optional daemon name.
 * @return Process status.
 */
static int kc_dmn_cli_list(const char *filter) {
    kc_dmn_entry_t *entries = NULL;
    size_t count = 0;
    size_t i;

    if (kc_dmn_list(&entries, &count) != KC_DMN_OK) return 1;

    for (i = 0; i < count; i++) {
        char endpoint[KC_DMN_BUF];

        if (filter && strcmp(filter, entries[i].name) != 0) continue;
        if (kc_dmn_cli_endpoint(
                entries[i].name,
                endpoint,
                sizeof(endpoint)
            ) != 0) {
            kc_dmn_free(entries);
            return 1;
        }
        printf("%s\t%s\n", entries[i].name, endpoint);
    }

    kc_dmn_free(entries);
    return 0;
}

/**
 * Executes the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    int i = 1;

    if (i >= argc) {
        kc_dmn_help();
        return 1;
    }

    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        kc_dmn_help();
        return 0;
    }

    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
        kc_dmn_print_version();
        return 0;
    }

#ifdef _WIN32
    if (strcmp(argv[i], "--_serve") == 0) {
        char cmd[KC_DMN_BUF];

        if (i + 2 >= argc) return 1;
        kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 2, argc);
        return kc_dmn_cli_serve_win32(argv[i + 1], cmd) == 0 ? 0 : 1;
    }
#endif

    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
        const char *filter = (i + 2 == argc) ? argv[i + 1] : NULL;

        if (i + 2 < argc) return 1;
        return kc_dmn_cli_list(filter);
    }

    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
        if (i + 2 != argc) return 1;
        return kc_dmn_delete(argv[i + 1]) == KC_DMN_OK ? 0 : 1;
    }

    if (argv[i][0] == '-') {
        kc_dmn_help();
        return 1;
    }

    if (i + 1 < argc) {
        if (strcmp(argv[i + 1], "-d") == 0 ||
                strcmp(argv[i + 1], "--delete") == 0) {
            if (i + 2 != argc) return 1;
            return kc_dmn_delete(argv[i]) == KC_DMN_OK ? 0 : 1;
        }

        if (strcmp(argv[i + 1], "-l") == 0 ||
                strcmp(argv[i + 1], "--list") == 0) {
            if (i + 2 != argc) return 1;
            return kc_dmn_cli_list(argv[i]);
        }

        if (strcmp(argv[i + 1], "-s") == 0 ||
                strcmp(argv[i + 1], "--signal") == 0) {
            kc_dmn_t *daemon = NULL;
            int signal;
            int rc;

            if (i + 3 != argc) return 1;
            signal = atoi(argv[i + 2]);
            if (kc_dmn_open(&daemon, argv[i]) != KC_DMN_OK) return 1;
            rc = kc_dmn_send_signal(daemon, signal);
            kc_dmn_close(daemon);
            return rc == KC_DMN_OK ? 0 : 1;
        }

        {
            kc_dmn_options_t options = {0};
            char cmd[KC_DMN_BUF];

            kc_dmn_join_args(cmd, sizeof(cmd), argv, i + 1, argc);
            options.cmd = cmd;
            return kc_dmn_create(argv[i], &options) == KC_DMN_OK ? 0 : 1;
        }
    }

    {
        kc_dmn_t *daemon = NULL;
        int rc;

        if (kc_dmn_open(&daemon, argv[i]) != KC_DMN_OK) return 1;
        rc = kc_dmn_cli_relay(daemon);
        kc_dmn_close(daemon);
        return rc == KC_DMN_OK ? 0 : 1;
    }
}
