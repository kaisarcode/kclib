/**
 * test.c - libdmn public API tests.
 * Summary: Contract tests for the normalized dmn API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libdmn.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#define getpid _getpid
#define mkdir_one(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#endif

#ifndef DMN_TEST_CLI
#define DMN_TEST_CLI ""
#endif

static int test_case_total;
static int test_case_current;

typedef int (*case_fn)(void);

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail);
}

static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

static int expect_string(
    const char *name,
    const char *expected,
    const char *actual
) {
    if (!actual || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n",
            name,
            expected,
            actual ? actual : "NULL");
        return 1;
    }
    return 0;
}

static void short_sleep(void) {
#ifdef _WIN32
    Sleep(250);
#else
    struct timespec ts = {0, 250000000L};
    nanosleep(&ts, NULL);
#endif
}

static int make_runtime_dir(
    char *out,
    size_t cap,
    const char *name
) {
    size_t base_len;
    unsigned long suffix = 1;
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(tmp), tmp);
    if (n == 0 || n >= (DWORD)sizeof(tmp)) return 1;
    if ((size_t)snprintf(
            out,
            cap,
            "%skc-dmn-test-%ld-%s",
            tmp,
            (long)getpid(),
            name
        ) >= cap)
        return 1;
#else
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    if ((size_t)snprintf(
            out,
            cap,
            "%s/kc-dmn-test-%ld-%s",
            base,
            (long)getpid(),
            name
        ) >= cap)
        return 1;
#endif

    base_len = strlen(out);
    if (mkdir_one(out) == 0) return 0;
    if (errno != EEXIST) return 1;

    for (;;) {
        if ((size_t)snprintf(
                out + base_len,
                cap - base_len,
                "-%lu",
                suffix
            ) >= cap - base_len)
            return 1;
        if (mkdir_one(out) == 0) return 0;
        if (errno != EEXIST) return 1;
        suffix++;
    }
}

static void remove_runtime_dir(const char *dir) {
#ifdef _WIN32
    (void)RemoveDirectoryA(dir);
#else
    (void)rmdir(dir);
#endif
}

static const char *response_command(void) {
#ifdef _WIN32
    return "echo response";
#else
    return "while IFS= read -r l; do printf '%s\\004' \"$l\"; done";
#endif
}

static int create_daemon(
    const char *name,
    const char *dir,
    const char *cmd
) {
    kc_dmn_options_t options = {0};
    options.cmd = cmd;
    options.dir = dir;
    return kc_dmn_create(name, &options);
}

static int open_daemon(
    kc_dmn_t **out,
    const char *name,
    const char *dir
) {
    int rc = kc_dmn_open(out, name);
    if (rc != KC_DMN_OK) return rc;
    rc = kc_dmn_set_dir(*out, dir);
    if (rc != KC_DMN_OK) {
        kc_dmn_close(*out);
        *out = NULL;
    }
    return rc;
}

#ifdef _WIN32

static void test_join_args(
    char *out,
    size_t cap,
    char **argv,
    int first,
    int argc
) {
    size_t used = 0;
    int i;

    if (!cap) return;
    out[0] = '\0';
    for (i = first; i < argc; i++) {
        int written = snprintf(
            out + used,
            cap - used,
            "%s%s",
            used ? " " : "",
            argv[i]
        );
        if (written < 0 || (size_t)written >= cap - used) {
            out[0] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

static int test_serve_win32(
    const char *pipename,
    const char *cmd
) {
    SECURITY_ATTRIBUTES sa;
    char cmdstr[8192];
    char buf[8192];

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if ((size_t)snprintf(
            cmdstr,
            sizeof(cmdstr),
            "cmd.exe /c %s",
            cmd
        ) >= sizeof(cmdstr))
        return 1;

    while (1) {
        HANDLE h = CreateNamedPipeA(
            pipename,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            sizeof(buf),
            sizeof(buf),
            0,
            NULL
        );
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

        if (!CreateProcessA(
                NULL,
                cmdstr,
                NULL,
                NULL,
                TRUE,
                CREATE_NO_WINDOW,
                NULL,
                NULL,
                &si,
                &pi
            )) {
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
                DWORD rd = avail < (DWORD)sizeof(buf)
                    ? avail
                    : (DWORD)sizeof(buf);
                if (ReadFile(h, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0;
                    DWORD written;
                    while (off < br &&
                            WriteFile(
                                in_wr,
                                buf + off,
                                br - off,
                                &written,
                                NULL
                            ) &&
                            written > 0)
                        off += written;
                }
            }

            if (PeekNamedPipe(out_rd, NULL, 0, NULL, &avail, NULL) &&
                    avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf)
                    ? avail
                    : (DWORD)sizeof(buf);
                if (ReadFile(out_rd, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0;
                    DWORD written;
                    while (off < br &&
                            WriteFile(
                                h,
                                buf + off,
                                br - off,
                                &written,
                                NULL
                            ) &&
                            written > 0)
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

static int case_kc_dmn_create(void) {
    char dir[512];
    kc_dmn_options_t options = {0};
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "create") != 0) return 1;
    options.cmd = response_command();
    options.dir = dir;

    fail += expect_int(
        "invalid name rejected",
        KC_DMN_ERROR,
        kc_dmn_create("../bad", &options)
    );
    fail += expect_int(
        "missing options rejected",
        KC_DMN_ERROR,
        kc_dmn_create("created", NULL)
    );
    fail += expect_int(
        "create succeeds",
        KC_DMN_OK,
        kc_dmn_create("created", &options)
    );
    short_sleep();
    (void)kc_dmn_delete("created", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_create", "creates or replaces a named daemon");
    return fail != 0;
}

static int case_kc_dmn_open(void) {
    kc_dmn_t *daemon = (kc_dmn_t *)(uintptr_t)1;
    int fail = 0;

    fail += expect_int(
        "NULL output rejected",
        KC_DMN_ERROR,
        kc_dmn_open(NULL, "name")
    );
    fail += expect_int(
        "invalid name rejected",
        KC_DMN_ERROR,
        kc_dmn_open(&daemon, "../bad")
    );
    fail += expect_true("failed output is NULL", daemon == NULL);
    fail += expect_int(
        "name handle opens",
        KC_DMN_OK,
        kc_dmn_open(&daemon, "name")
    );
    fail += expect_true("handle returned", daemon != NULL);
    kc_dmn_close(daemon);
    case_result(fail, "kc_dmn_open", "opens a handle bound to one daemon name");
    return fail != 0;
}

static int case_kc_dmn_list(void) {
    char dir[512];
    kc_dmn_entry_t *entries = NULL;
    size_t count = 0;
    int fail = 0;
    size_t i;
    int found = 0;

    if (make_runtime_dir(dir, sizeof(dir), "list") != 0) return 1;
    fail += expect_int(
        "create list daemon",
        KC_DMN_OK,
        create_daemon("listed", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "list succeeds",
        KC_DMN_OK,
        kc_dmn_list(dir, &entries, &count)
    );
    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].name, "listed") == 0) {
            found = entries[i].endpoint && entries[i].endpoint[0];
            break;
        }
    }
    fail += expect_true("list includes daemon and endpoint", found);
    kc_dmn_free(entries);
    (void)kc_dmn_delete("listed", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_list", "returns owned daemon entries");
    return fail != 0;
}

static int case_kc_dmn_delete(void) {
    char dir[512];
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "delete") != 0) return 1;
    fail += expect_int(
        "missing delete is no-op",
        KC_DMN_OK,
        kc_dmn_delete("missing", dir)
    );
    fail += expect_int(
        "create delete daemon",
        KC_DMN_OK,
        create_daemon("deleted", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "delete succeeds",
        KC_DMN_OK,
        kc_dmn_delete("deleted", dir)
    );
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_delete", "deletes a named daemon");
    return fail != 0;
}

static int case_kc_dmn_set_cmd(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "set-cmd") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("setcmd", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "setcmd", dir)
    );
    if (daemon) {
        fail += expect_int(
            "set command succeeds",
            KC_DMN_OK,
            kc_dmn_set_cmd(daemon, response_command())
        );
    }
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("setcmd", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_set_cmd", "updates the daemon command");
    return fail != 0;
}

static int case_kc_dmn_get_cmd(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "get-cmd") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("getcmd", dir, response_command())
    );
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "getcmd", dir)
    );
    if (daemon)
        fail += expect_string(
            "get command",
            response_command(),
            kc_dmn_get_cmd(daemon)
        );
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("getcmd", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_get_cmd", "returns the configured command");
    return fail != 0;
}

static int case_kc_dmn_set_dir(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "set-dir") != 0) return 1;
    fail += expect_int("open handle", KC_DMN_OK, kc_dmn_open(&daemon, "dir"));
    if (daemon)
        fail += expect_int(
            "set directory",
            KC_DMN_OK,
            kc_dmn_set_dir(daemon, dir)
        );
    kc_dmn_close(daemon);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_set_dir", "changes the handle runtime directory");
    return fail != 0;
}

static int case_kc_dmn_get_dir(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "get-dir") != 0) return 1;
    fail += expect_int("open handle", KC_DMN_OK, kc_dmn_open(&daemon, "dir"));
    if (daemon) {
        fail += expect_int(
            "set directory",
            KC_DMN_OK,
            kc_dmn_set_dir(daemon, dir)
        );
        fail += expect_string("get directory", dir, kc_dmn_get_dir(daemon));
    }
    kc_dmn_close(daemon);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_get_dir", "returns the handle runtime directory");
    return fail != 0;
}

static int case_kc_dmn_set_eot(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    const char marker[] = "==END==";
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "set-eot") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("seteot", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "seteot", dir)
    );
    if (daemon) {
        fail += expect_int(
            "set custom eot",
            KC_DMN_OK,
            kc_dmn_set_eot(daemon, marker, sizeof(marker) - 1)
        );
        fail += expect_int(
            "reset default eot",
            KC_DMN_OK,
            kc_dmn_set_eot(daemon, NULL, 0)
        );
    }
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("seteot", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_set_eot", "updates and resets the cycle marker");
    return fail != 0;
}

static int case_kc_dmn_get_eot(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    const unsigned char *eot;
    size_t eot_size = 0;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "get-eot") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("geteot", dir, response_command())
    );
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "geteot", dir)
    );
    if (daemon) {
        eot = (const unsigned char *)kc_dmn_get_eot(daemon, &eot_size);
        fail += expect_true(
            "default eot is byte four",
            eot && eot_size == 1 && eot[0] == 4
        );
    }
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("geteot", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_get_eot", "returns the configured cycle marker");
    return fail != 0;
}

typedef struct {
    int calls;
    size_t bytes;
} data_state_t;

static void on_data(const void *data, size_t size, void *userdata) {
    data_state_t *state = (data_state_t *)userdata;
    if (!state || (!data && size > 0)) return;
    state->calls++;
    state->bytes += size;
}

static int case_kc_dmn_on(void) {
    kc_dmn_t *daemon = NULL;
    data_state_t state = {0};
    int fail = 0;

    fail += expect_int("open handle", KC_DMN_OK, kc_dmn_open(&daemon, "event"));
    if (daemon) {
        fail += expect_int(
            "unknown event rejected",
            KC_DMN_ERROR,
            kc_dmn_on(daemon, "unknown", on_data, &state)
        );
        fail += expect_int(
            "data handler accepted",
            KC_DMN_OK,
            kc_dmn_on(daemon, "data", on_data, &state)
        );
    }
    kc_dmn_close(daemon);
    case_result(fail, "kc_dmn_on", "registers the data event handler");
    return fail != 0;
}

static int case_kc_dmn_send_data(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    data_state_t state = {0};
    void *response = NULL;
    size_t response_size = 0;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "send-data") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("senddata", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "senddata", dir)
    );
    if (daemon) {
        fail += expect_int(
            "register data handler",
            KC_DMN_OK,
            kc_dmn_on(daemon, "data", on_data, &state)
        );
        fail += expect_int(
            "send data",
            KC_DMN_OK,
            kc_dmn_send_data(
                daemon,
                "hello\n",
                6,
                &response,
                &response_size
            )
        );
        fail += expect_true("response returned", response_size > 0);
        fail += expect_true("data handler called", state.calls > 0);
    }
    kc_dmn_free(response);
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("senddata", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_send_data", "returns a complete response and emits chunks");
    return fail != 0;
}

static int case_kc_dmn_send_signal(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "signal") != 0) return 1;
    fail += expect_int(
        "open missing handle",
        KC_DMN_OK,
        open_daemon(&daemon, "missing", dir)
    );
    if (daemon)
        fail += expect_int(
            "missing daemon reported",
            KC_DMN_NOT_FOUND,
            kc_dmn_send_signal(daemon, 10)
        );
    kc_dmn_close(daemon);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_send_signal", "sends a platform daemon signal");
    return fail != 0;
}

static int case_kc_dmn_stream(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    kc_dmn_stream_t *stream = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "stream") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("stream", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "stream", dir)
    );
    if (daemon)
        fail += expect_int(
            "open stream",
            KC_DMN_OK,
            kc_dmn_stream(daemon, &stream)
        );
    fail += expect_true("stream returned", stream != NULL);
    kc_dmn_stream_close(stream);
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("stream", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_stream", "opens a raw daemon stream");
    return fail != 0;
}

static int case_kc_dmn_stream_write(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    kc_dmn_stream_t *stream = NULL;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "stream-write") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("streamwrite", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "streamwrite", dir)
    );
    if (daemon)
        fail += expect_int(
            "open stream",
            KC_DMN_OK,
            kc_dmn_stream(daemon, &stream)
        );
    if (stream)
        fail += expect_int(
            "write raw bytes",
            KC_DMN_OK,
            kc_dmn_stream_write(stream, "raw\n\004", 5)
        );
    kc_dmn_stream_close(stream);
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("streamwrite", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_stream_write", "writes raw stream bytes");
    return fail != 0;
}

static int case_kc_dmn_stream_read(void) {
    char dir[512];
    kc_dmn_t *daemon = NULL;
    kc_dmn_stream_t *stream = NULL;
    unsigned char data[128];
    size_t size = 0;
    int fail = 0;

    if (make_runtime_dir(dir, sizeof(dir), "stream-read") != 0) return 1;
    fail += expect_int(
        "create daemon",
        KC_DMN_OK,
        create_daemon("streamread", dir, response_command())
    );
    short_sleep();
    fail += expect_int(
        "open daemon",
        KC_DMN_OK,
        open_daemon(&daemon, "streamread", dir)
    );
    if (daemon)
        fail += expect_int(
            "open stream",
            KC_DMN_OK,
            kc_dmn_stream(daemon, &stream)
        );
    if (stream) {
        fail += expect_int(
            "write request",
            KC_DMN_OK,
            kc_dmn_stream_write(stream, "raw\n\004", 5)
        );
        fail += expect_int(
            "read response",
            KC_DMN_OK,
            kc_dmn_stream_read(stream, data, sizeof(data), &size)
        );
        fail += expect_true("raw response has bytes", size > 0);
    }
    kc_dmn_stream_close(stream);
    kc_dmn_close(daemon);
    (void)kc_dmn_delete("streamread", dir);
    remove_runtime_dir(dir);
    case_result(fail, "kc_dmn_stream_read", "reads raw stream bytes");
    return fail != 0;
}

static int case_kc_dmn_stream_close(void) {
    kc_dmn_stream_close(NULL);
    case_result(0, "kc_dmn_stream_close", "accepts NULL");
    return 0;
}

static int case_kc_dmn_free(void) {
    kc_dmn_entry_t *entries = NULL;
    size_t count = 0;
    int fail = expect_int(
        "empty default list succeeds",
        KC_DMN_OK,
        kc_dmn_list(NULL, &entries, &count)
    );
    kc_dmn_free(entries);
    kc_dmn_free(NULL);
    case_result(fail, "kc_dmn_free", "releases API-owned allocations");
    return fail != 0;
}

static int case_kc_dmn_close(void) {
    kc_dmn_t *daemon = NULL;
    int fail = expect_int(
        "open handle",
        KC_DMN_OK,
        kc_dmn_open(&daemon, "close")
    );
    kc_dmn_close(daemon);
    kc_dmn_close(NULL);
    case_result(fail, "kc_dmn_close", "releases daemon handles");
    return fail != 0;
}

static int case_kc_dmn_version(void) {
    int fail = expect_true("version non-zero", kc_dmn_version() != 0U);
    case_result(fail, "kc_dmn_version", "returns the generated build version");
    return fail != 0;
}

static int case_kc_dmn_cli(void) {
    char command[4096];
    int fail = 0;
    int rc;

    if (DMN_TEST_CLI[0] == '\0') {
        case_result(1, "kc_dmn_cli", "covers stable CLI parsing");
        return 1;
    }

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --help > NUL 2>&1", DMN_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --help > /dev/null 2>&1", DMN_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI help succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --version > NUL 2>&1", DMN_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --version > /dev/null 2>&1", DMN_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI version succeeds", rc == 0);

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" --unknown > NUL 2>&1", DMN_TEST_CLI);
#else
    snprintf(command, sizeof(command), "\"%s\" --unknown > /dev/null 2>&1", DMN_TEST_CLI);
#endif
    rc = system(command);
    fail += expect_true("CLI unknown option fails", rc != 0);

    case_result(fail, "kc_dmn_cli", "covers stable CLI parsing");
    return fail != 0;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = 21;
    test_case_current = 0;
    run_case(&rc, case_kc_dmn_create);
    run_case(&rc, case_kc_dmn_open);
    run_case(&rc, case_kc_dmn_list);
    run_case(&rc, case_kc_dmn_delete);
    run_case(&rc, case_kc_dmn_set_cmd);
    run_case(&rc, case_kc_dmn_get_cmd);
    run_case(&rc, case_kc_dmn_set_dir);
    run_case(&rc, case_kc_dmn_get_dir);
    run_case(&rc, case_kc_dmn_set_eot);
    run_case(&rc, case_kc_dmn_get_eot);
    run_case(&rc, case_kc_dmn_on);
    run_case(&rc, case_kc_dmn_send_data);
    run_case(&rc, case_kc_dmn_send_signal);
    run_case(&rc, case_kc_dmn_stream);
    run_case(&rc, case_kc_dmn_stream_write);
    run_case(&rc, case_kc_dmn_stream_read);
    run_case(&rc, case_kc_dmn_stream_close);
    run_case(&rc, case_kc_dmn_free);
    run_case(&rc, case_kc_dmn_close);
    run_case(&rc, case_kc_dmn_version);
    run_case(&rc, case_kc_dmn_cli);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (argc >= 4 && strcmp(argv[1], "--_serve") == 0) {
        char cmd[8192];
        test_join_args(cmd, sizeof(cmd), argv, 3, argc);
        if (!cmd[0]) return 1;
        return test_serve_win32(argv[2], cmd);
    }
#endif

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_dmn_create") == 0) return case_kc_dmn_create();
    if (strcmp(argv[1], "kc_dmn_open") == 0) return case_kc_dmn_open();
    if (strcmp(argv[1], "kc_dmn_list") == 0) return case_kc_dmn_list();
    if (strcmp(argv[1], "kc_dmn_delete") == 0) return case_kc_dmn_delete();
    if (strcmp(argv[1], "kc_dmn_set_cmd") == 0) return case_kc_dmn_set_cmd();
    if (strcmp(argv[1], "kc_dmn_get_cmd") == 0) return case_kc_dmn_get_cmd();
    if (strcmp(argv[1], "kc_dmn_set_dir") == 0) return case_kc_dmn_set_dir();
    if (strcmp(argv[1], "kc_dmn_get_dir") == 0) return case_kc_dmn_get_dir();
    if (strcmp(argv[1], "kc_dmn_set_eot") == 0) return case_kc_dmn_set_eot();
    if (strcmp(argv[1], "kc_dmn_get_eot") == 0) return case_kc_dmn_get_eot();
    if (strcmp(argv[1], "kc_dmn_on") == 0) return case_kc_dmn_on();
    if (strcmp(argv[1], "kc_dmn_send_data") == 0) return case_kc_dmn_send_data();
    if (strcmp(argv[1], "kc_dmn_send_signal") == 0) return case_kc_dmn_send_signal();
    if (strcmp(argv[1], "kc_dmn_stream") == 0) return case_kc_dmn_stream();
    if (strcmp(argv[1], "kc_dmn_stream_write") == 0) return case_kc_dmn_stream_write();
    if (strcmp(argv[1], "kc_dmn_stream_read") == 0) return case_kc_dmn_stream_read();
    if (strcmp(argv[1], "kc_dmn_stream_close") == 0) return case_kc_dmn_stream_close();
    if (strcmp(argv[1], "kc_dmn_free") == 0) return case_kc_dmn_free();
    if (strcmp(argv[1], "kc_dmn_close") == 0) return case_kc_dmn_close();
    if (strcmp(argv[1], "kc_dmn_version") == 0) return case_kc_dmn_version();
    if (strcmp(argv[1], "kc_dmn_cli") == 0) return case_kc_dmn_cli();

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
