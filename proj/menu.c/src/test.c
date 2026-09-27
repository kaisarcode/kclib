/**
 * test.c - libmenu public API and CLI contract tests.
 * Summary: Validates application menu registration and the grouped CLI contract.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef MENU_TEST_CLI
#define MENU_TEST_CLI ""
#endif

static int test_case_total;
static int test_case_current;

typedef int (*case_fn)(void);

static void case_result(int fail, const char *name, const char *detail) {
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

static int expect_true(const char *label, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", label);
        return 1;
    }
    return 0;
}

#ifndef _WIN32
static char test_data_home[512];

static int test_prepare_home(void) {
    char pattern[] = "/tmp/kc-menu-test-XXXXXX";
    char *dir = mkdtemp(pattern);

    if (!dir) return 1;
    if (strlen(dir) + 1U > sizeof(test_data_home)) return 1;
    strcpy(test_data_home, dir);
    return setenv("XDG_DATA_HOME", test_data_home, 1) == 0 ? 0 : 1;
}

static int test_linux_entry_exists(const char *id) {
    char path[1024];
    struct stat st;

    snprintf(path, sizeof(path), "%s/applications/%s.desktop", test_data_home, id);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
#endif

static int case_kc_menu_add_delete(void) {
    const char *name = "kc_menu_add_delete";
    const char *detail = "adds, replaces, and deletes one application menu entry";
    kc_menu_entry_t entry = {
        "kc-menu-contract",
        "KC Menu Contract",
        "Contract test entry",
#ifdef _WIN32
        "cmd.exe /C exit 0",
#else
        "/bin/true --contract",
#endif
        NULL
    };
    int fail = 0;

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true(
            "prepare isolated XDG data home",
            test_prepare_home() == 0
        );
    }
#endif

    fail += expect_true("add rejects NULL", kc_menu_add(NULL) == KC_MENU_ERROR);
    fail += expect_true(
        "delete rejects invalid id",
        kc_menu_delete("../bad") == KC_MENU_ERROR
    );

    if (!fail) {
        fail += expect_true("add succeeds", kc_menu_add(&entry) == KC_MENU_OK);
#ifndef _WIN32
        fail += expect_true(
            "desktop entry exists",
            test_linux_entry_exists(entry.id)
        );
#endif
        entry.description = "Replacement description";
        fail += expect_true(
            "replacement add succeeds",
            kc_menu_add(&entry) == KC_MENU_OK
        );
        fail += expect_true(
            "delete succeeds",
            kc_menu_delete(entry.id) == KC_MENU_OK
        );
        fail += expect_true(
            "missing delete is a no-op",
            kc_menu_delete(entry.id) == KC_MENU_OK
        );
#ifndef _WIN32
        fail += expect_true(
            "desktop entry is gone",
            !test_linux_entry_exists(entry.id)
        );
#endif
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_menu_version(void) {
    int fail = expect_true("version is nonzero", kc_menu_version() != 0U);

    case_result(
        fail,
        "kc_menu_version",
        "returns the generated build version"
    );
    return fail == 0 ? 0 : 1;
}

#ifdef _WIN32
static int test_to_wide(const char *input, wchar_t *output, size_t capacity) {
    return MultiByteToWideChar(
        CP_UTF8,
        0,
        input,
        -1,
        output,
        (int)capacity
    ) > 0 ? 0 : 1;
}

static int test_append_arg(
    wchar_t *command,
    size_t capacity,
    const wchar_t *arg
) {
    size_t used = wcslen(command);
    size_t len = wcslen(arg);

    if (used != 0) {
        if (used + 1 >= capacity) return 1;
        command[used++] = L' ';
    }
    if (used + len + 2 >= capacity) return 1;

    command[used++] = L'"';
    memcpy(command + used, arg, len * sizeof(wchar_t));
    used += len;
    command[used++] = L'"';
    command[used] = L'\0';
    return 0;
}

static void test_read_pipe(HANDLE pipe, char *buffer, size_t size) {
    DWORD count;
    size_t used = 0;

    while (used + 1 < size &&
            ReadFile(
                pipe,
                buffer + used,
                (DWORD)(size - used - 1),
                &count,
                NULL
            ) &&
            count > 0) {
        used += count;
    }
    buffer[used] = '\0';
}
#endif

static int test_cli_run(
    char *const argv[],
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *out_status
) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE out_pipe[2];
    HANDLE err_pipe[2];
    wchar_t exe[MAX_PATH];
    wchar_t command[32768];
    wchar_t wide[4096];
    DWORD exit_code;
    int i;

    if (test_to_wide(MENU_TEST_CLI, exe, MAX_PATH)) return 1;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);

    command[0] = L'\0';
    if (test_append_arg(command, 32768, exe)) return 1;
    for (i = 1; argv[i] != NULL; i++) {
        if (test_to_wide(argv[i], wide, 4096)) return 1;
        if (test_append_arg(command, 32768, wide)) return 1;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(
            exe,
            command,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        )) {
        return 1;
    }

    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    test_read_pipe(out_pipe[0], out, out_size);
    test_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *out_status = (int)exit_code;
    return 0;
#else
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t pos;
    int status;

    if (pipe(out_pipe) != 0) return 1;
    if (pipe(err_pipe) != 0) return 1;

    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        execv(argv[0], argv);
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    pos = 0;
    memset(out, 0, out_size);
    while (pos + 1 < out_size &&
            (count = read(
                out_pipe[0],
                out + pos,
                out_size - pos - 1
            )) > 0) {
        pos += (size_t)count;
    }
    close(out_pipe[0]);

    pos = 0;
    memset(err, 0, err_size);
    while (pos + 1 < err_size &&
            (count = read(
                err_pipe[0],
                err + pos,
                err_size - pos - 1
            )) > 0) {
        pos += (size_t)count;
    }
    close(err_pipe[0]);

    if (waitpid(pid, &status, 0) < 0) return 1;
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
#endif
}

static int case_kc_menu_cli(void) {
    const char *name = "kc_menu_cli";
    const char *detail = "add, delete, help, version, and diagnostics";
    char out[4096];
    char err[4096];
    int status = 0;
    int fail = 0;
    char *help[] = { (char *)MENU_TEST_CLI, "--help", NULL };
    char *version[] = { (char *)MENU_TEST_CLI, "--version", NULL };
    char *missing[] = { (char *)MENU_TEST_CLI, "kc-menu-cli", NULL };
    char *unknown[] = {
        (char *)MENU_TEST_CLI,
        "kc-menu-cli",
        "--nope",
        NULL
    };
    char *add[] = {
        (char *)MENU_TEST_CLI,
        "kc-menu-cli",
        "--name",
        "KC Menu CLI",
        "--description",
        "CLI contract test",
        "--command",
#ifdef _WIN32
        "cmd.exe /C exit 0",
#else
        "/bin/true --cli",
#endif
        NULL
    };
    char *del[] = {
        (char *)MENU_TEST_CLI,
        "--delete",
        "kc-menu-cli",
        NULL
    };

    if (MENU_TEST_CLI[0] == '\0') {
        case_result(1, name, detail);
        return 1;
    }

#ifndef _WIN32
    if (test_data_home[0] == '\0') {
        fail += expect_true(
            "prepare isolated XDG data home",
            test_prepare_home() == 0
        );
    }
#endif

    (void)kc_menu_delete("kc-menu-cli");

    fail += test_cli_run(help, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI help exits 0", status == 0);
    fail += expect_true("CLI help prints usage", strstr(out, "Usage:") != NULL);

    fail += test_cli_run(version, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI version exits 0", status == 0);
    fail += expect_true(
        "CLI version prints build",
        strstr(out, "menu build") != NULL
    );

    fail += test_cli_run(missing, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI missing fields exits 1", status == 1);
    fail += expect_true(
        "CLI missing fields diagnostic",
        strstr(err, "--name and --command are required") != NULL
    );

    fail += test_cli_run(unknown, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI unknown option exits 1", status == 1);
    fail += expect_true(
        "CLI unknown option diagnostic",
        strstr(err, "unknown option") != NULL
    );

    fail += test_cli_run(add, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI add exits 0", status == 0);
#ifndef _WIN32
    fail += expect_true(
        "CLI add creates desktop entry",
        test_linux_entry_exists("kc-menu-cli")
    );
#endif

    fail += test_cli_run(del, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI delete exits 0", status == 0);
#ifndef _WIN32
    fail += expect_true(
        "CLI delete removes desktop entry",
        !test_linux_entry_exists("kc-menu-cli")
    );
#endif

    (void)kc_menu_delete("kc-menu-cli");

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = 3;
    test_case_current = 0;

    run_case(&rc, case_kc_menu_add_delete);
    run_case(&rc, case_kc_menu_version);
    run_case(&rc, case_kc_menu_cli);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(
            stderr,
            "test case: expected one argument, got %d\n",
            argc - 1
        );
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_menu_add_delete") == 0) {
        return case_kc_menu_add_delete();
    }
    if (strcmp(argv[1], "kc_menu_version") == 0) {
        return case_kc_menu_version();
    }
    if (strcmp(argv[1], "kc_menu_cli") == 0) {
        return case_kc_menu_cli();
    }

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
