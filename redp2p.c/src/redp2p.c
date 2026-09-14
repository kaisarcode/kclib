/**
 * redp2p.c - REDP2P.
 * Summary: REDP2P tunnel CLI - idx, pub, con.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

/**
 * The context owned by the currently running blocking operation, or NULL.
 * Summary: Async-signal-safe access for the signal handler. redp2p_stop is a
 * single atomic store and is safe to call from a signal handler.
 */
static redp2p_t *g_active_ctx = NULL;

/**
 * Signal handler for SIGINT/SIGTERM.
 * Summary: Requests clean stop of the active blocking REDP2P operation.
 * @param sig Signal number (unused).
 * @return None.
 */
static void sigint_handler(int sig) {
    (void)sig;
    if (g_active_ctx != NULL) redp2p_stop(g_active_ctx);
}

/**
 * Prints one publisher id to standard output.
 * @param id       Publisher id.
 * @param userdata Pointer to a failure flag (int).
 * @return None.
 */
static void print_publisher_id(const char *id, void *userdata) {
    int *failed = (int *)userdata;

    if (id == NULL || fprintf(stdout, "%s\n", id) < 0) *failed = 1;
}

/**
 * Parses one bounded ASCII decimal integer.
 * @param text Input decimal text.
 * @param min  Inclusive lower bound.
 * @param max  Inclusive upper bound.
 * @param out  Output parsed value.
 * @return 0 on success, 1 on invalid input.
 */
static int parse_decimal(const char *text, long min, long max, long *out) {
    unsigned long value;
    unsigned long limit;
    size_t i;

    if (!text || !text[0] || !out || min < 0 || max < min) return 1;
    value = 0;
    limit = (unsigned long)max;
    for (i = 0; text[i] != '\0'; i++) {
        unsigned long digit;

        if (text[i] < '0' || text[i] > '9') return 1;
        digit = (unsigned long)(text[i] - '0');
        if (digit > limit) return 1;
        if (value > (limit - digit) / 10) return 1;
        value = value * 10 + digit;
    }
    if (value < (unsigned long)min) return 1;
    *out = (long)value;
    return 0;
}

/**
 * Parses one ASCII decimal size value.
 * @param text Input decimal text.
 * @param out Output parsed value.
 * @return 0 on success, 1 on invalid input or overflow.
 */
static int parse_size(const char *text, size_t *out) {
    size_t value;
    size_t i;

    if (!text || !text[0] || !out) return 1;
    value = 0;
    for (i = 0; text[i] != '\0'; i++) {
        size_t digit;

        if (text[i] < '0' || text[i] > '9') return 1;
        digit = (size_t)(text[i] - '0');
        if (value > (SIZE_MAX - digit) / 10) return 1;
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

/**
 * Parses host and optional port from a string.
 * Summary: Supports host, host:port, IPv4:port, [IPv6], and [IPv6]:port.
 * @param text     Input address string.
 * @param host     Output host buffer.
 * @param host_sz  Output host buffer capacity.
 * @param port     Output port.
 * @return 0 on success, 1 on failure.
 */
static int parse_addr(const char *text, char *host, size_t host_sz,
    unsigned short *port)
{
    const char *colon;
    const char *end_bracket;
    long val;
    size_t n;

    if (!text || !text[0] || !host || host_sz == 0 || !port) return 1;
    *port = REDP2P_PORT_DEFAULT;
    if (text[0] == '[') {
        end_bracket = strchr(text, ']');
        if (!end_bracket) return 1;
        n = (size_t)(end_bracket - text - 1);
        if (n == 0 || n >= host_sz) return 1;
        memcpy(host, text + 1, n);
        host[n] = '\0';
        if (end_bracket[1] == '\0') return 0;
        if (end_bracket[1] != ':' || end_bracket[2] == '\0') return 1;
        if (parse_decimal(end_bracket + 2, 1, 65535, &val) != 0) return 1;
        *port = (unsigned short)val;
        return 0;
    }
    colon = strrchr(text, ':');
    if (colon && strchr(text, ':') != colon) {
        n = strlen(text);
        if (n == 0 || n >= host_sz) return 1;
        memcpy(host, text, n + 1);
        return 0;
    }
    if (!colon) {
        n = strlen(text);
        if (n == 0 || n >= host_sz) return 1;
        memcpy(host, text, n + 1);
        return 0;
    }
    if (colon == text || colon[1] == '\0') return 1;
    n = (size_t)(colon - text);
    if (n == 0 || n >= host_sz) return 1;
    memcpy(host, text, n);
    host[n] = '\0';
    if (parse_decimal(colon + 1, 1, 65535, &val) != 0) return 1;
    *port = (unsigned short)val;
    return 0;
}

/**
 * Parses hostname@index:port spec string.
 * Summary: Splits on '@', parses the index part as addr:port.
 * @param spec     Input spec string (hostname@addr:port).
 * @param hostname Output hostname buffer.
 * @param hn_sz    Output hostname buffer capacity.
 * @param idx_addr Output index address buffer.
 * @param ia_sz    Output index address buffer capacity.
 * @param idx_port Output index port.
 * @return 0 on success, 1 on failure.
 */
static int parse_hostspec(const char *spec, char *hostname, size_t hn_sz,
    char *idx_addr, size_t ia_sz, unsigned short *idx_port)
{
    const char *at;
    size_t n;

    if (!spec || !spec[0]) return 1;
    at = strrchr(spec, '@');
    if (!at || at == spec) return 1;

    n = (size_t)(at - spec);
    if (n == 0 || n >= hn_sz) return 1;
    memcpy(hostname, spec, n);
    hostname[n] = '\0';

    *idx_port = REDP2P_PORT_DEFAULT;
    return parse_addr(at + 1, idx_addr, ia_sz, idx_port);
}

/**
 * Parses one strict TCP or UDP port from CLI text.
 * Summary: Rejects NULL, empty, signs, trailing garbage, overflow, and 0.
 * @param text Input text to parse.
 * @param out  Output parsed port.
 * @return 0 on success, 1 on failure.
 */
static int parse_port(const char *text, unsigned short *out) {
    long val;

    if (!out || parse_decimal(text, 1, 65535, &val) != 0) return 1;
    *out = (unsigned short)val;
    return 0;
}

/**
 * Parses one strict signed integer with explicit bounds from CLI text.
 * Summary: Rejects NULL, empty, signs, trailing garbage, and overflow.
 * @param text Input text to parse.
 * @param min  Inclusive lower bound.
 * @param max  Inclusive upper bound.
 * @param out  Output parsed value.
 * @return 0 on success, 1 on failure.
 */
static int parse_int(const char *text, long min, long max, long *out) {
    return parse_decimal(text, min, max, out);
}

/**
 * Loads only index-owned environment configuration.
 * @param opts Index options to populate.
 * @param seats_set Set when REDP2P_SEATS contains a valid value.
 * @return 0 on success, 1 on allocation failure.
 */
static int load_index_options(redp2p_options_t *opts, int *seats_set) {
    const char *value;
    long number;
    size_t seats;
    size_t len;

    value = getenv("REDP2P_SEATS");
    if (value) {
        if (parse_size(value, &seats) != 0 ||
            seats > SIZE_MAX / sizeof(redp2p_peer_t))
            return 1;
        opts->seats = seats;
        *seats_set = 1;
    }
    value = getenv("REDP2P_POW");
    if (parse_decimal(value, 0, 32, &number) == 0)
        opts->pow = (int)number;
    value = getenv("REDP2P_PASS");
    if (value) {
        strncpy(opts->pass, value, REDP2P_PASS_MAX);
        opts->pass[REDP2P_PASS_MAX] = '\0';
    }
    value = getenv("REDP2P_VIP");
    if (!value) return 0;
    len = strlen(value);
    opts->vip = (char *)malloc(len + 1);
    if (!opts->vip) return 1;
    memcpy(opts->vip, value, len + 1);
    return 0;
}

/**
 * Prints usage information.
 * Summary: Shows available commands and options.
 * @param name Program executable name.
 * @return None.
 */
static void print_help(const char *name) {
    printf("Usage: %s <command> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  idx <port> [--seats <N>] [--pow <N>] [--max-consumers-per-publisher <N>] Start index server\n");
    printf("  idx <port> -l, --list                  List local index publishers\n");
    printf("  idx <port> -p, --prune                 Prune expired index records\n");
    printf("  idx <port> -d, --down                   Stop a background index\n");
    printf("  pub <host>@<index[:port]> --tcp <port> [--sweep <n>] [--stun <url>]\n");
    printf("  pub <host>@<index[:port]> --udp <port> [--sweep <n>] [--stun <url>]\n");
    printf("  pub <host>@<index[:port]> -d, --down    Stop a background publisher\n");
    printf("  con <host>@<index[:port]> --tcp <port> [--sweep <n>] [--stun <url>]\n");
    printf("  con <host>@<index[:port]> --udp <port> [--sweep <n>] [--stun <url>]\n");
    printf("  con <host>@<index[:port]> -d, --down    Stop a background consumer\n");
    printf("\n");
    printf("  --state-dir <dir>                     Override CLI PID and publisher data base\n");
    printf("\n");
    printf("Environment:\n");
    printf("  REDP2P_STATE_DIR          PID directory base (default: $XDG_STATE_HOME/redp2p; fallback: $HOME/.local/state/redp2p)\n");
    printf("  REDP2P_SEATS              Publisher seats; VIPs count; unset means no limit\n");
    printf("  REDP2P_POW                PoW bits for index registration (0..32)\n");
    printf("  REDP2P_PASS               Optional shared password for REGISTER/pub protection\n");
    printf("  REDP2P_VIP                Reserved seat passwords as '<id> <pass> ...'\n");
    printf("  REDP2P_SWEEP              UDP port sweep range used during punch fallback\n");
    printf("  REDP2P_STUN               Optional STUN URL (stun:host:port)\n");
    printf("  REDP2P_PRUNE_INTERVAL_S   Index prune interval seconds (1..3600, default 60)\n");
    printf("  REDP2P_ETIMEOUT_SEC       Index eviction TTL seconds (1..86400, default 120)\n");
    printf("  REDP2P_HEARTBEAT_S        Publisher heartbeat interval seconds (1..3600, default 15)\n");
    printf("  REDP2P_PUNCH_POLL_MS      Publisher punch poll interval ms (10..60000, default 500)\n");
    printf("  REDP2P_PENDING_CALL_TTL_S Index pending call TTL seconds (1..86400, default 30)\n");
    printf("  REDP2P_MAX_CONSUMERS_PER_PUBLISHER  Per-publisher pending-call bound (default 32; 0 restores default)\n");
    printf("  IDs may use only A-Z a-z 0-9\n");
    printf("  Passwords may use A-Z a-z 0-9 . _ - + = , : @ %% /\n");
}

/**
 * Sanitizes an address string for use as a PID filename.
 * Replaces '.', ':', '@' with '_'. Writes into caller-owned buffer.
 * @param addr Input address string.
 * @param out  Output buffer.
 * @param out_sz Output buffer capacity.
 * @return 0 on success, 1 if output too small.
 */
static int cli_sanitize_addr(const char *addr, char *out, size_t out_sz) {
    size_t i;
    size_t n;

    if (!addr || !out || out_sz == 0) return 1;
    n = strlen(addr);
    if (n >= out_sz) return 1;
    for (i = 0; i < n; i++) {
        char c = addr[i];
        if (c == '.' || c == ':' || c == '@')
            out[i] = '_';
        else
            out[i] = c;
    }
    out[n] = '\0';
    return 0;
}

/**
 * Resolves the process state directory path used for PID files.
 * Summary: Follows the XDG Base Directory convention. Priority:
 * cli_flag > REDP2P_STATE_DIR env > $XDG_STATE_HOME/redp2p >
 * $HOME/.local/state/redp2p (POSIX) or $USERPROFILE/.local/state/redp2p
 * (Windows).
 * @param cli_flag CLI --state-dir value, or NULL.
 * @param buf Output buffer.
 * @param buf_sz Output buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int cli_state_dir(const char *cli_flag, char *buf, size_t buf_sz) {
    const char *base;
    int written;

    if (cli_flag && cli_flag[0] != '\0') {
        written = snprintf(buf, buf_sz, "%s", cli_flag);
        return written < 0 || (size_t)written >= buf_sz;
    }
    base = getenv("REDP2P_STATE_DIR");
    if (base && base[0] != '\0') {
        written = snprintf(buf, buf_sz, "%s", base);
        return written < 0 || (size_t)written >= buf_sz;
    }
#ifndef _WIN32
    base = getenv("XDG_STATE_HOME");
    if (base && base[0] != '\0') {
        written = snprintf(buf, buf_sz, "%s/redp2p", base);
        return written < 0 || (size_t)written >= buf_sz;
    }
#endif
    base = getenv("HOME");
#ifdef _WIN32
    if (!base || !base[0]) base = getenv("USERPROFILE");
#endif
    if (!base || !base[0]) return 1;
    written = snprintf(buf, buf_sz, "%s/.local/state/redp2p", base);
    return written < 0 || (size_t)written >= buf_sz;
}

/**
 * Computes the PID file path for a background process.
 * @param state_dir State directory base.
 * @param cmd Command name (idx, pub, con).
 * @param addr Address string (port for idx, host@index:port for pub/con).
 * @param buf Output buffer.
 * @param buf_sz Output buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int cli_pid_path(const char *state_dir, const char *cmd,
    const char *addr, char *buf, size_t buf_sz)
{
    char safe[256];
    int written;

    if (strcmp(cmd, "idx") == 0) {
        written = snprintf(buf, buf_sz, "%s/pids/%s_%s.pid", state_dir, cmd, addr);
    } else {
        if (cli_sanitize_addr(addr, safe, sizeof(safe)) != 0) return 1;
        written = snprintf(buf, buf_sz, "%s/pids/%s_%s.pid", state_dir, cmd, safe);
    }
    return written < 0 || (size_t)written >= buf_sz;
}

/**
 * Creates a directory and its missing parents.
 * @param path Directory path.
 * @return 0 on success, -1 on failure.
 */
static int cli_mkdir_p(const char *path) {
    char tmp[1024];
    char *p;
    size_t n;

    n = strlen(path);
    if (n >= sizeof(tmp)) return -1;
    memcpy(tmp, path, n + 1);
    for (p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST) return -1;
#else
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
#endif
        *p = '/';
    }
#ifdef _WIN32
    if (_mkdir(tmp) != 0 && errno != EEXIST) return -1;
#else
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
#endif
    return 0;
}

/**
 * Writes the current process PID to a file.
 * @param pid_path Full path to PID file.
 * @return 0 on success, 1 on failure.
 */
static int cli_pid_write(const char *pid_path) {
    FILE *f;
    char parent[1280];
    char *separator;
    int failed;

    if (!pid_path || strlen(pid_path) >= sizeof(parent)) return 1;
    strcpy(parent, pid_path);
    separator = strrchr(parent, '/');
    if (!separator) return 1;
    *separator = '\0';
    if (cli_mkdir_p(parent) != 0) return 1;
    f = fopen(pid_path, "w");
    if (f == NULL) return 1;
    failed = fprintf(f, "%d\n", (int)getpid()) < 0;
    if (fclose(f) != 0) failed = 1;
    return failed;
}

/**
 * Removes a PID file.
 * @param pid_path Full path to PID file.
 * @return None.
 */
static void cli_pid_remove(const char *pid_path) {
    if (pid_path && pid_path[0] != '\0')
        remove(pid_path);
}

/**
 * Stops a background process by PID file.
 * @param pid_path Full path to PID file.
 * @return 0 on success, 1 on error.
 */
static int cli_pid_stop(const char *pid_path) {
    FILE *f;
    long pid;

    f = fopen(pid_path, "r");
    if (f == NULL) {
        fprintf(stderr, "redp2p: no background instance found\n");
        return 1;
    }
    if (fscanf(f, "%ld", &pid) != 1 || pid <= 0) {
        fclose(f);
        remove(pid_path);
        fprintf(stderr, "redp2p: invalid PID file\n");
        return 1;
    }
    fclose(f);

#ifdef _WIN32
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (h == NULL) {
            remove(pid_path);
            fprintf(stderr, "redp2p: process not running\n");
            return 1;
        }
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
#else
    {
        int killed;
        if (kill((pid_t)pid, 0) != 0) {
            remove(pid_path);
            fprintf(stderr, "redp2p: process not running\n");
            return 1;
        }
        killed = kill((pid_t)pid, SIGTERM);
        if (killed != 0) {
            fprintf(stderr, "redp2p: failed to signal process\n");
            return 1;
        }
    }
#endif
    remove(pid_path);
    fprintf(stderr, "redp2p: stopped\n");
    return 0;
}

/**
 * Program entry point.
 * Summary: Dispatches subcommands (idx, pub, con).
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    const char *state_dir_flag = NULL;

    if (argc < 2) { print_help(argv[0]); return 1; }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) { print_help(argv[0]); return 0; }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("redp2p build %llu\n",
            (unsigned long long)redp2p_version());
        return 0;
    }

    if (strcmp(argv[1], "idx") == 0) {
        redp2p_options_t opts;
        unsigned short port = 0;
        size_t seats;
        int seats_set;
        int seats_option_set;
        int pow_bits;
        int pow_option_set;
        int list_mode;
        int prune_mode;
        int down_mode;
        int consumers_set;
        int consumers_option_set;
        size_t consumers;

        list_mode = 0;
        prune_mode = 0;
        down_mode = 0;
        seats_set = 0;
        seats_option_set = 0;
        pow_option_set = 0;
        consumers_set = 0;
        consumers_option_set = 0;
        seats = 0;
        pow_bits = 0;
        consumers = REDP2P_MAX_CONSUMERS_PER_PUBLISHER;

        opts = redp2p_options_default();
        seats = opts.seats;
        pow_bits = opts.pow;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--state-dir") == 0) {
                if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                    strlen(argv[i + 1]) > REDP2P_STATE_DIR_MAX)
                {
                    fprintf(stderr, "redp2p: --state-dir requires a nonempty path of at most %d bytes\n", REDP2P_STATE_DIR_MAX);
                    redp2p_options_free(&opts);
                    return 1;
                }
                state_dir_flag = argv[++i];
            } else if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
                list_mode = 1;
            } else if (strcmp(argv[i], "--prune") == 0 || strcmp(argv[i], "-p") == 0) {
                prune_mode = 1;
            } else if (strcmp(argv[i], "--down") == 0 || strcmp(argv[i], "-d") == 0) {
                down_mode = 1;
            } else if (strcmp(argv[i], "--seats") == 0) {
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --seats requires an argument\n"); redp2p_options_free(&opts); return 1; }
                if (parse_size(argv[++i], &seats) != 0) {
                    fprintf(stderr, "redp2p: invalid --seats '%s'\n", argv[i]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                seats_set = 1;
                seats_option_set = 1;
            } else if (strcmp(argv[i], "--pow") == 0) {
                long v;
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --pow requires an argument\n"); redp2p_options_free(&opts); return 1; }
                if (parse_int(argv[++i], 0, 32, &v) != 0) {
                    fprintf(stderr, "redp2p: invalid --pow '%s'\n", argv[i]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                pow_bits = (int)v;
                pow_option_set = 1;
            } else if (strcmp(argv[i], "--max-consumers-per-publisher") == 0) {
                size_t sz;
                if (i + 1 >= argc) {
                    fprintf(stderr,
                        "redp2p: --max-consumers-per-publisher requires an argument\n");
                    redp2p_options_free(&opts);
                    return 1;
                }
                if (parse_size(argv[++i], &sz) != 0) {
                    fprintf(stderr,
                        "redp2p: invalid --max-consumers-per-publisher '%s'\n",
                        argv[i]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                consumers = sz;
                consumers_set = 1;
                consumers_option_set = 1;
            } else if (port == 0 && parse_port(argv[i], &port) == 0) {
            } else {
                fprintf(stderr, "redp2p: unknown option '%s'\n", argv[i]);
                redp2p_options_free(&opts);
                return 1;
            }
        }

        if (port == 0) {
            fprintf(stderr, "redp2p: usage: %s idx <port> [--list|-l] [--prune|-p] [--seats N] [--pow N] [--max-consumers-per-publisher N]\n", argv[0]);
            redp2p_options_free(&opts);
            return 1;
        }

        if (load_index_options(&opts, &seats_set) != 0) {
            fprintf(stderr, "redp2p: failed to load index options\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (list_mode && (seats_option_set || pow_option_set || consumers_option_set)) {
            fprintf(stderr,
                "redp2p: --list cannot be combined with --seats or --pow\n");
            redp2p_options_free(&opts);
            return 1;
        }
        if (prune_mode && (list_mode || seats_option_set || pow_option_set || consumers_option_set)) {
            fprintf(stderr,
                "redp2p: --prune cannot be combined with --list, --seats, or --pow\n");
            redp2p_options_free(&opts);
            return 1;
        }
        if (down_mode && (list_mode || prune_mode || seats_option_set || pow_option_set || consumers_option_set)) {
            fprintf(stderr,
                "redp2p: --down cannot be combined with --list, --prune, --seats, or --pow\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (down_mode) {
            char sdir[1024];
            char pidfile[1280];
            int rc;

            redp2p_options_free(&opts);
            if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
                fprintf(stderr, "redp2p: cannot resolve state directory\n");
                return 1;
            }
            {
                char port_str[8];
                snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
                if (cli_pid_path(sdir, "idx", port_str, pidfile, sizeof(pidfile)) != 0) {
                    fprintf(stderr, "redp2p: path too long\n");
                    return 1;
                }
            }
            rc = cli_pid_stop(pidfile);
            return rc;
        }

        if (list_mode) {
            redp2p_t *ctx = NULL;
            const char *detail = NULL;
            int failed = 0;
            int list_rc;

            detail = NULL;
            if (redp2p_open(&ctx) != REDP2P_OK) {
                fprintf(stderr, "redp2p: allocation failed\n");
                redp2p_options_free(&opts);
                return 1;
            }
            list_rc = redp2p_list_publishers(ctx, "127.0.0.1", port,
                print_publisher_id, &failed);
            if (list_rc != REDP2P_OK)
                detail = redp2p_get_error(ctx);
            redp2p_close(ctx);
            redp2p_options_free(&opts);
            if (list_rc != REDP2P_OK) {
                fprintf(stderr, "redp2p: list failed: %s\n",
                    detail != NULL ? detail : redp2p_strerror(list_rc));
                return 1;
            }
            if (failed != 0 || fflush(stdout) != 0) {
                fprintf(stderr, "redp2p: failed to write publisher list\n");
                return 1;
            }
            return 0;
        }
        if (prune_mode) {
            fprintf(stderr, "redp2p: index pruning runs automatically every 60 seconds\n");
            redp2p_options_free(&opts);
            return 0;
        }
        {
            char sdir[1024];
            char pidfile[1280];
            redp2p_t *ctx = NULL;

            sdir[0] = '\0';
            pidfile[0] = '\0';
            if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
                fprintf(stderr, "redp2p: cannot resolve state directory\n");
                redp2p_options_free(&opts);
                return 1;
            }
            {
                char port_str[8];
                snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
                if (cli_pid_path(sdir, "idx", port_str, pidfile, sizeof(pidfile)) != 0) {
                    fprintf(stderr, "redp2p: path too long\n");
                    redp2p_options_free(&opts);
                    return 1;
                }
            }

            if (redp2p_open(&ctx) != REDP2P_OK) {
                fprintf(stderr, "redp2p: failed to create context\n");
                redp2p_options_free(&opts);
                return 1;
            }
            if (seats_set) redp2p_set_seats(ctx, seats);
            if (consumers_set) redp2p_set_max_consumers_per_publisher(ctx, consumers);
            if (pow_option_set) redp2p_set_pow(ctx, pow_bits);
            if (opts.pass[0] != '\0') redp2p_set_pass(ctx, opts.pass);
            if (opts.vip != NULL) {
                char verr[256];
                if (redp2p_set_vip(ctx, opts.vip, verr, sizeof(verr)) != REDP2P_OK) {
                    fprintf(stderr, "redp2p: %s\n", verr);
                    redp2p_close(ctx);
                    redp2p_options_free(&opts);
                    return 1;
                }
            }
            if (state_dir_flag) redp2p_set_state_dir(ctx, state_dir_flag);
            redp2p_options_free(&opts);
            if (cli_pid_write(pidfile) != 0) {
                fprintf(stderr, "redp2p: failed to write PID file\n");
                redp2p_close(ctx);
                return 1;
            }
            g_active_ctx = ctx;
            fprintf(stderr, "redp2p: index server listening on port %u\n",
                (unsigned)port);
            signal(SIGINT, sigint_handler);
            signal(SIGTERM, sigint_handler);
            {
                int result = redp2p_serve_index(ctx, NULL, port);
                int interrupted = redp2p_stop_requested(ctx);
                const char *detail;
                int rc;
                signal(SIGINT, SIG_DFL);
                signal(SIGTERM, SIG_DFL);
                g_active_ctx = NULL;
                cli_pid_remove(pidfile);
                if (interrupted) {
                    if (result != REDP2P_OK) {
                        fprintf(stderr, "redp2p: index exited: %s\n",
                            redp2p_get_error(ctx) != NULL
                                ? redp2p_get_error(ctx)
                                : redp2p_strerror(result));
                    }
                    redp2p_close(ctx);
                    return 0;
                }
                detail = redp2p_get_error(ctx);
                fprintf(stderr, "redp2p: index exited: %s\n",
                    detail != NULL ? detail : redp2p_strerror(result));
                rc = redp2p_close(ctx);
                if (rc != REDP2P_OK) return 1;
                return result == REDP2P_OK ? 0 : 1;
            }
        }

    } else if (strcmp(argv[1], "pub") == 0) {
        redp2p_options_t opts;
        char host[REDP2P_ID_MAX + 1];
        char idx_host[256];
        unsigned short idx_port;
        unsigned short service_port = 0;
        int proto = 0;
        int down_mode = 0;
        int sweep_option_set = 0;
        int stun_option_set = 0;
        char sdir[1024];
        char pidfile[1280];
        redp2p_t *ctx = NULL;

        opts = redp2p_options_default();
        redp2p_options_load_env(&opts);

        if (argc < 3) { fprintf(stderr, "redp2p: usage: %s pub <host>@<index[:port]>\n", argv[0]); redp2p_options_free(&opts); return 1; }
        if (parse_hostspec(argv[2], host, sizeof(host), idx_host, sizeof(idx_host), &idx_port) != 0) {
            fprintf(stderr, "redp2p: invalid spec '%s' (expected host@index:port)\n", argv[2]); redp2p_options_free(&opts); return 1;
        }
        if (!redp2p_is_valid_id(host)) {
            fprintf(stderr, "redp2p: invalid host id '%s'\n", host);
            redp2p_options_free(&opts);
            return 1;
        }

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--state-dir") == 0) {
                if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                    strlen(argv[i + 1]) > REDP2P_STATE_DIR_MAX)
                {
                    fprintf(stderr, "redp2p: --state-dir requires a nonempty path of at most %d bytes\n", REDP2P_STATE_DIR_MAX);
                    redp2p_options_free(&opts);
                    return 1;
                }
                state_dir_flag = argv[++i];
            } else if (strcmp(argv[i], "--down") == 0 || strcmp(argv[i], "-d") == 0) {
                down_mode = 1;
            } else if (strcmp(argv[i], "--tcp") == 0) {
                if (proto != 0) { fprintf(stderr, "redp2p: choose only one of --tcp or --udp\n"); redp2p_options_free(&opts); return 1; }
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --tcp requires a port\n"); redp2p_options_free(&opts); return 1; }
                if (parse_port(argv[i + 1], &service_port) != 0) {
                    fprintf(stderr, "redp2p: invalid --tcp port '%s'\n", argv[i + 1]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                i++;
                proto = REDP2P_PROTO_TCP;
            } else if (strcmp(argv[i], "--udp") == 0) {
                if (proto != 0) { fprintf(stderr, "redp2p: choose only one of --tcp or --udp\n"); redp2p_options_free(&opts); return 1; }
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --udp requires a port\n"); redp2p_options_free(&opts); return 1; }
                if (parse_port(argv[i + 1], &service_port) != 0) {
                    fprintf(stderr, "redp2p: invalid --udp port '%s'\n", argv[i + 1]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                i++;
                proto = REDP2P_PROTO_UDP;
            } else if (strcmp(argv[i], "--sweep") == 0) {
                long v;
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --sweep requires a number\n"); redp2p_options_free(&opts); return 1; }
                if (parse_int(argv[++i], 0, 1024, &v) != 0) {
                    fprintf(stderr, "redp2p: invalid --sweep '%s'\n", argv[i]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                opts.sweep = (int)v;
                sweep_option_set = 1;
            } else if (strcmp(argv[i], "--stun") == 0) {
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --stun requires a URL\n"); redp2p_options_free(&opts); return 1; }
                strncpy(opts.stun_url, argv[++i], sizeof(opts.stun_url) - 1);
                opts.stun_url[sizeof(opts.stun_url) - 1] = '\0';
                stun_option_set = 1;
            } else { fprintf(stderr, "redp2p: unknown option '%s'\n", argv[i]); redp2p_options_free(&opts); return 1; }
        }

        if (down_mode && (proto != 0 || service_port != 0 || (sweep_option_set && opts.sweep != 0) ||
            (stun_option_set && opts.stun_url[0] != '\0'))) {
            fprintf(stderr, "redp2p: --down cannot be combined with --tcp, --udp, --sweep, or --stun\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (down_mode) {
            int rc;

            redp2p_options_free(&opts);
            if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
                fprintf(stderr, "redp2p: cannot resolve state directory\n");
                return 1;
            }
            if (cli_pid_path(sdir, "pub", argv[2], pidfile, sizeof(pidfile)) != 0) {
                fprintf(stderr, "redp2p: path too long\n");
                return 1;
            }
            rc = cli_pid_stop(pidfile);
            return rc;
        }

        if (proto == 0 || service_port == 0) { fprintf(stderr, "redp2p: pub requires --tcp <port> or --udp <port>\n"); redp2p_options_free(&opts); return 1; }

        sdir[0] = '\0';
        pidfile[0] = '\0';
        if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
            fprintf(stderr, "redp2p: cannot resolve state directory\n");
            redp2p_options_free(&opts);
            return 1;
        }
        if (cli_pid_path(sdir, "pub", argv[2], pidfile, sizeof(pidfile)) != 0) {
            fprintf(stderr, "redp2p: path too long\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (redp2p_open(&ctx) != REDP2P_OK) {
            fprintf(stderr, "redp2p: failed to create context\n");
            redp2p_options_free(&opts);
            return 1;
        }
        redp2p_set_protocol(ctx, proto);
        redp2p_set_port(ctx, service_port);
        if (opts.pass[0] != '\0') redp2p_set_pass(ctx, opts.pass);
        redp2p_set_sweep(ctx, opts.sweep);
        if (opts.stun_url[0] != '\0') redp2p_set_stun_url(ctx, opts.stun_url);
        if (state_dir_flag) redp2p_set_state_dir(ctx, state_dir_flag);
        redp2p_options_free(&opts);
        if (cli_pid_write(pidfile) != 0) {
            fprintf(stderr, "redp2p: failed to write PID file\n");
            redp2p_close(ctx);
            return 1;
        }
        g_active_ctx = ctx;
        fprintf(stderr, "redp2p: waiting for connections...\n");
        signal(SIGINT, sigint_handler);
        signal(SIGTERM, sigint_handler);
        {
            unsigned short bind_port = redp2p_get_bind_port(ctx);
            int result = redp2p_wait(ctx, idx_host, idx_port, host, bind_port);
            int interrupted = redp2p_stop_requested(ctx);
            int rc;
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            g_active_ctx = NULL;
            cli_pid_remove(pidfile);
            if (interrupted) {
                redp2p_close(ctx);
                return 0;
            }
            if (result != REDP2P_OK)
                fprintf(stderr, "redp2p: pub exited: %s\n",
                    redp2p_get_error(ctx) != NULL ? redp2p_get_error(ctx)
                        : redp2p_strerror(result));
            rc = redp2p_close(ctx);
            if (rc != REDP2P_OK) return 1;
            return result == REDP2P_OK ? 0 : 1;
        }

    } else if (strcmp(argv[1], "con") == 0) {
        redp2p_options_t opts;
        char host[REDP2P_ID_MAX + 1];
        char self_id[REDP2P_ID_MAX + 1];
        char idx_host[256];
        unsigned short idx_port;
        unsigned short listen_port = 0;
        int proto = 0;
        int down_mode = 0;
        int sweep_option_set = 0;
        int stun_option_set = 0;
        char sdir[1024];
        char pidfile[1280];
        redp2p_t *ctx = NULL;

        opts = redp2p_options_default();
        redp2p_options_load_env(&opts);

        if (argc < 3) { fprintf(stderr, "redp2p: usage: %s con <host>@<index[:port]>\n", argv[0]); redp2p_options_free(&opts); return 1; }
        if (parse_hostspec(argv[2], host, sizeof(host), idx_host, sizeof(idx_host), &idx_port) != 0) {
            fprintf(stderr, "redp2p: invalid spec '%s' (expected host@index:port)\n", argv[2]); redp2p_options_free(&opts); return 1;
        }
        if (!redp2p_is_valid_id(host)) {
            fprintf(stderr, "redp2p: invalid host id '%s'\n", host);
            redp2p_options_free(&opts);
            return 1;
        }

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--state-dir") == 0) {
                if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                    strlen(argv[i + 1]) > REDP2P_STATE_DIR_MAX)
                {
                    fprintf(stderr, "redp2p: --state-dir requires a nonempty path of at most %d bytes\n", REDP2P_STATE_DIR_MAX);
                    redp2p_options_free(&opts);
                    return 1;
                }
                state_dir_flag = argv[++i];
            } else if (strcmp(argv[i], "--down") == 0 || strcmp(argv[i], "-d") == 0) {
                down_mode = 1;
            } else if (strcmp(argv[i], "--tcp") == 0) {
                if (proto != 0) { fprintf(stderr, "redp2p: choose only one of --tcp or --udp\n"); redp2p_options_free(&opts); return 1; }
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --tcp requires a port\n"); redp2p_options_free(&opts); return 1; }
                if (parse_port(argv[i + 1], &listen_port) != 0) {
                    fprintf(stderr, "redp2p: invalid --tcp port '%s'\n", argv[i + 1]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                i++;
                proto = REDP2P_PROTO_TCP;
            } else if (strcmp(argv[i], "--udp") == 0) {
                if (proto != 0) { fprintf(stderr, "redp2p: choose only one of --tcp or --udp\n"); redp2p_options_free(&opts); return 1; }
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --udp requires a port\n"); redp2p_options_free(&opts); return 1; }
                if (parse_port(argv[i + 1], &listen_port) != 0) {
                    fprintf(stderr, "redp2p: invalid --udp port '%s'\n", argv[i + 1]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                i++;
                proto = REDP2P_PROTO_UDP;
            } else if (strcmp(argv[i], "--sweep") == 0) {
                long v;
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --sweep requires a number\n"); redp2p_options_free(&opts); return 1; }
                if (parse_int(argv[++i], 0, 1024, &v) != 0) {
                    fprintf(stderr, "redp2p: invalid --sweep '%s'\n", argv[i]);
                    redp2p_options_free(&opts);
                    return 1;
                }
                opts.sweep = (int)v;
                sweep_option_set = 1;
            } else if (strcmp(argv[i], "--stun") == 0) {
                if (i + 1 >= argc) { fprintf(stderr, "redp2p: --stun requires a URL\n"); redp2p_options_free(&opts); return 1; }
                strncpy(opts.stun_url, argv[++i], sizeof(opts.stun_url) - 1);
                opts.stun_url[sizeof(opts.stun_url) - 1] = '\0';
                stun_option_set = 1;
            } else { fprintf(stderr, "redp2p: unknown option '%s'\n", argv[i]); redp2p_options_free(&opts); return 1; }
        }

        if (down_mode && (proto != 0 || listen_port != 0 || (sweep_option_set && opts.sweep != 0) ||
            (stun_option_set && opts.stun_url[0] != '\0'))) {
            fprintf(stderr, "redp2p: --down cannot be combined with --tcp, --udp, --sweep, or --stun\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (down_mode) {
            int rc;

            redp2p_options_free(&opts);
            if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
                fprintf(stderr, "redp2p: cannot resolve state directory\n");
                return 1;
            }
            if (cli_pid_path(sdir, "con", argv[2], pidfile, sizeof(pidfile)) != 0) {
                fprintf(stderr, "redp2p: path too long\n");
                return 1;
            }
            rc = cli_pid_stop(pidfile);
            return rc;
        }

        if (proto == 0 || listen_port == 0) { fprintf(stderr, "redp2p: con requires --tcp <port> or --udp <port>\n"); redp2p_options_free(&opts); return 1; }

        sdir[0] = '\0';
        pidfile[0] = '\0';
        if (cli_state_dir(state_dir_flag, sdir, sizeof(sdir)) != 0) {
            fprintf(stderr, "redp2p: cannot resolve state directory\n");
            redp2p_options_free(&opts);
            return 1;
        }
        if (cli_pid_path(sdir, "con", argv[2], pidfile, sizeof(pidfile)) != 0) {
            fprintf(stderr, "redp2p: path too long\n");
            redp2p_options_free(&opts);
            return 1;
        }

        if (redp2p_open(&ctx) != REDP2P_OK) {
            fprintf(stderr, "redp2p: failed to create context\n");
            redp2p_options_free(&opts);
            return 1;
        }
        redp2p_set_protocol(ctx, proto);
        redp2p_set_port(ctx, listen_port);
        redp2p_set_sweep(ctx, opts.sweep);
        if (opts.stun_url[0] != '\0') redp2p_set_stun_url(ctx, opts.stun_url);
        if (state_dir_flag) redp2p_set_state_dir(ctx, state_dir_flag);
        redp2p_options_free(&opts);
        snprintf(self_id, sizeof(self_id), "c%ld", (long)getpid());
        if (cli_pid_write(pidfile) != 0) {
            fprintf(stderr, "redp2p: failed to write PID file\n");
            redp2p_close(ctx);
            return 1;
        }
        g_active_ctx = ctx;
        fprintf(stderr, "redp2p: connecting to %s...\n", argv[2]);
        signal(SIGINT, sigint_handler);
        signal(SIGTERM, sigint_handler);
        {
            unsigned short bind_port = redp2p_get_bind_port(ctx);
            int result = redp2p_connect(ctx, idx_host, idx_port, self_id,
                host, bind_port);
            int interrupted = redp2p_stop_requested(ctx);
            int rc;
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            g_active_ctx = NULL;
            cli_pid_remove(pidfile);
            if (interrupted) {
                redp2p_close(ctx);
                return 0;
            }
            if (result != REDP2P_OK)
                fprintf(stderr, "redp2p: connect failed: %s\n",
                    redp2p_get_error(ctx) != NULL ? redp2p_get_error(ctx)
                        : redp2p_strerror(result));
            rc = redp2p_close(ctx);
            if (rc != REDP2P_OK) return 1;
            return result == REDP2P_OK ? 0 : 1;
        }

    } else {
        fprintf(stderr, "redp2p: unknown command '%s'\n", argv[1]);
        fprintf(stderr, "redp2p: try '%s --help'\n", argv[0]);
        return 1;
    }
}
