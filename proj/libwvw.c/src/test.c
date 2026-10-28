/**
 * test.c - libwvw public contract tests.
 * Summary: Exercises the consumer-facing WebView API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

/* Cocoa uses Darwin BSD typedefs; strict POSIX mode hides them. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwvw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif
#endif

static int current;
static int total;
static int failures;

#ifndef __EMSCRIPTEN__
static const char *kc_wvw_test_self;
static volatile int kc_wvw_exec_test_state;
#endif

#ifndef __EMSCRIPTEN__
static int case_wvw_cli(void);
#endif

/**
 * Print one test result line.
 * @param name Public API behavior under test.
 * @param detail Behavior verified by the case.
 * @param ok Non-zero when the case passed.
 * @return None.
 */
static void result(const char *name, const char *detail, int ok) {
    current++;
    if (!ok) failures++;
    printf("[%d/%d] [%s] %s: %s\n", current, total,
        ok ? "PASS" : "FAIL", name, detail);
}

/**
 * Create one temporary HTML file and return its file URL.
 * @return Owned file URL, or NULL on failure.
 */
static char *test_url(void) {
    char path[2048];
    char url[2200];
    FILE *file;
#ifdef _WIN32
    char dir[MAX_PATH];
    size_t i;

    if (!GetTempPathA((DWORD)sizeof(dir), dir)) return NULL;
    snprintf(path, sizeof(path), "%swvw_test.html", dir);
    for (i = 0; path[i]; i++) {
        if (path[i] == '\\') path[i] = '/';
    }
#else
    snprintf(path, sizeof(path), "/tmp/wvw_test.html");
#endif
    file = fopen(path, "w");
    if (!file) return NULL;
    fputs("<html><body>wvw test</body></html>", file);
    fclose(file);
#ifdef _WIN32
    snprintf(url, sizeof(url), "file:///%s", path);
#else
    snprintf(url, sizeof(url), "file://%s", path);
#endif
    return strdup(url);
}

/**
 * Open one test WebView with fixed initial dimensions.
 * @param out Destination WebView handle.
 * @param url_out Destination owned URL string.
 * @return 1 on success, 0 on failure.
 */
static int open_test(
    kc_wvw_t **out,
    char **url_out,
    const int *hidden,
    const int *unlist
) {
    kc_wvw_options_t options = {0};
    int width = 640;
    int height = 480;
    char *url = test_url();

    if (!url) return 0;
    options.url = url;
    options.title = "wvw test";
    options.width = &width;
    options.height = &height;
    options.hidden = hidden;
    options.unlist = unlist;
    options.allow_file = 1;
    if (kc_wvw_open(out, &options) != KC_WVW_OK) {
        kc_wvw_close(*out);
        *out = NULL;
        free(url);
        return 0;
    }
    *url_out = url;
    return 1;
}

/**
 * Return one fixed bridge response for contract tests.
 * @param wvw WebView handle.
 * @param method Method name.
 * @param params_json Serialized parameters.
 * @param out_result_json Destination response JSON.
 * @param userdata Caller data.
 * @return KC_WVW_OK.
 */
static int bridge_callback(
    kc_wvw_t *wvw,
    const char *method,
    const char *params_json,
    const char **out_result_json,
    void *userdata
) {
    (void)wvw;
    (void)method;
    (void)params_json;
    (void)userdata;
    *out_result_json = "{\"ok\":true}";
    return KC_WVW_OK;
}

/**
 * Test kc_wvw_version.
 * @return None.
 */
static void case_kc_wvw_version(void) {
    result("kc_wvw_version", "returns generated build version",
        kc_wvw_version() != 0);
}

/**
 * Test kc_wvw_open validation and NULL-safe close.
 * @return None.
 */
static void case_kc_wvw_open(void) {
    kc_wvw_t *wvw = NULL;
    kc_wvw_options_t invalid = {0};
    int ok = kc_wvw_open(NULL, &invalid) == KC_WVW_ERROR;

    ok &= kc_wvw_open(&wvw, &invalid) == KC_WVW_ERROR && wvw == NULL;
#ifdef _WIN32
    invalid.url = "about:blank";
    invalid.background = "80ffffff";
    ok &= kc_wvw_open(&wvw, &invalid) == KC_WVW_ERROR;
    kc_wvw_close(wvw);
#endif
    kc_wvw_close(NULL);
    result("kc_wvw_open", "rejects missing URL and close accepts NULL", ok);
}

/**
 * Test immediate navigation across repeated open and close cycles.
 * @return None.
 */
static void case_kc_wvw_navigation(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = 1;
    int cycle;

    for (cycle = 0; cycle < 4 && ok; cycle++) {
        ok = open_test(&wvw, &url, NULL, NULL);
        if (ok) ok = kc_wvw_navigate(wvw, url) == KC_WVW_OK;
        kc_wvw_close(wvw);
        free(url);
        wvw = NULL;
        url = NULL;
    }
    result("kc_wvw_navigation",
        "open returns an operational WebView that can navigate", ok);
}

/**
 * Test bridge setup and native event delivery.
 * @return None.
 */
static void case_kc_wvw_bridge(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    const char *methods[] = { "ping", "minimize", "setTitle" };
    const char *reserved_window[] = { "Window" };
    const char *reserved_exec[] = { "exec" };
    kc_wvw_bridge_options_t bridge = {0};
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        bridge.methods = methods;
        bridge.method_count = 3;
        bridge.callback = bridge_callback;
        ok =
            kc_wvw_add_init_script(
                wvw, "window.__wvw_test = true;") == KC_WVW_OK &&
            kc_wvw_set_bridge_methods(wvw, &bridge) == KC_WVW_OK &&
            kc_wvw_post_bridge_event(
                wvw, "{\"type\":\"test\"}") == KC_WVW_OK;
        bridge.methods = reserved_window;
        bridge.method_count = 1;
        ok = ok && kc_wvw_set_bridge_methods(wvw, &bridge) == KC_WVW_ERROR;
        bridge.methods = reserved_exec;
        ok = ok && kc_wvw_set_bridge_methods(wvw, &bridge) == KC_WVW_ERROR;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_bridge",
        "init script, bridge and native event use the direct public surface",
        ok);
}

#ifndef __EMSCRIPTEN__
/**
 * Report completion from a WebView script which exercised NativeBridge.exec.
 * @return KC_WVW_OK.
 */
static int kc_wvw_exec_test_callback(kc_wvw_t *wvw, const char *method,
    const char *params_json, const char **response, void *userdata) {
    (void)wvw;
    (void)userdata;
    if (!strcmp(method, "execResult")) {
        __atomic_store_n(&kc_wvw_exec_test_state,
            strstr(params_json, "\"passed\":true") ? 1 : -1,
            __ATOMIC_SEQ_CST);
    }
    *response = "{\"ok\":true}";
    return KC_WVW_OK;
}

/**
 * Exercise process output, argument preservation, failure exit status and
 * executable allowlisting through the real JavaScript promise bridge.
 * @return None.
 */
static void case_kc_wvw_exec(void) {
    kc_wvw_t *wvw = NULL;
    char *initial_url = NULL;
    char path[2048], uri[2200];
    const char *methods[] = {"execResult"};
    const char *paths[] = {kc_wvw_test_self};
    kc_wvw_bridge_options_t bridge = {0};
    kc_wvw_exec_options_t executables = {paths, 1};
    FILE *file;
    int ok = 1, count;
#ifdef _WIN32
    char directory[MAX_PATH];
    size_t i;
    if (!GetTempPathA(sizeof(directory), directory)) ok = 0;
    if (ok) snprintf(path, sizeof(path), "%swvw_exec_test.html", directory);
    for (i = 0; ok && path[i]; i++)
        if (path[i] == '\\') path[i] = '/';
    if (ok) snprintf(uri, sizeof(uri), "file:///%s", path);
#else
    snprintf(path, sizeof(path), "/tmp/wvw_exec_test.html");
    snprintf(uri, sizeof(uri), "file://%s", path);
#endif
    if (ok) {
        file = fopen(path, "w");
        if (!file) ok = 0;
        else {
            fputs("<html><body><script>"
                "window.addEventListener('DOMContentLoaded',async function(){"
                "var passed=false;"
                "try{"
#ifdef _WIN32
                "var name='wvw_contract_test.exe';"
#else
                "var name='wvw_contract_test';"
#endif
                "var output=await NativeBridge.exec([name,'--exec-probe','hello world']);"
                "var failed=false;var blocked=false;"
                "try{await NativeBridge.exec([name,'--exec-fail']);}"
                "catch(e){failed=e.code==='EXEC_FAILED'&&e.exitCode===17&&"
                "e.output.indexOf('WVW FAIL')>=0;}"
                "try{await NativeBridge.exec(['not-exposed']);}"
                "catch(e){blocked=e.code==='EXEC_NOT_EXPOSED';}"
                "passed=output.indexOf('WVW PROBE:hello world')>=0&&failed&&blocked;"
                "}catch(e){}"
                "await NativeBridge.execResult({passed:passed});"
                "});"
                "</script></body></html>", file);
            if (fclose(file)) ok = 0;
        }
    }
    if (ok) ok = open_test(&wvw, &initial_url, NULL, NULL);
    if (ok) {
        bridge.methods = methods;
        bridge.method_count = 1;
        bridge.callback = kc_wvw_exec_test_callback;
        kc_wvw_exec_test_state = 0;
        ok = kc_wvw_set_executables(wvw, &executables) == KC_WVW_OK &&
            kc_wvw_set_bridge_methods(wvw, &bridge) == KC_WVW_OK &&
            kc_wvw_navigate(wvw, uri) == KC_WVW_OK;
    }
    for (count = 0; ok && count < 200 &&
        __atomic_load_n(&kc_wvw_exec_test_state, __ATOMIC_SEQ_CST) == 0; count++) {
#ifdef _WIN32
        Sleep(50);
#elif defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, 0);
#else
        struct timespec delay = {0, 50000000L};
        nanosleep(&delay, NULL);
#endif
    }
    if (ok) ok = __atomic_load_n(&kc_wvw_exec_test_state,
        __ATOMIC_SEQ_CST) == 1;
    kc_wvw_close(wvw);
    free(initial_url);
    if (ok) remove(path);
    result("kc_wvw_exec",
        "executes only registered binaries asynchronously with output and errors",
        ok);
}
#endif

/**
 * Test show, hide, and visibility query behavior.
 * @return None.
 */
static void case_kc_wvw_visibility(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int hidden = 1;
    int unlist = 1;
    int ok = open_test(&wvw, &url, &hidden, &unlist);

    if (ok) {
        ok =
            kc_wvw_is_visible(wvw) == 0 &&
            kc_wvw_is_listed(wvw) == 0 &&
            kc_wvw_show(wvw) == KC_WVW_OK &&
            kc_wvw_is_visible(wvw) == 1 &&
            kc_wvw_list(wvw) == KC_WVW_OK &&
            kc_wvw_is_listed(wvw) == 1 &&
            kc_wvw_unlist(wvw) == KC_WVW_OK &&
            kc_wvw_is_listed(wvw) == 0 &&
            kc_wvw_hide(wvw) == KC_WVW_OK &&
            kc_wvw_is_visible(wvw) == 0 &&
            kc_wvw_show(wvw) == KC_WVW_OK;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_visibility",
        "hide/show and list/unlist preserve one live window", ok);
}

/**
 * Test minimize, maximize, and restore actions.
 * @return None.
 */
static void case_kc_wvw_actions(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        ok =
            kc_wvw_minimize(wvw) == KC_WVW_OK &&
            kc_wvw_restore(wvw) == KC_WVW_OK &&
            kc_wvw_maximize(wvw) == KC_WVW_OK &&
            kc_wvw_restore(wvw) == KC_WVW_OK;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_actions",
        "minimize, maximize and restore remain direct actions", ok);
}

/**
 * Test title property setters and getters.
 * @return None.
 */
static void case_kc_wvw_title(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    const char *title;
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        ok = kc_wvw_set_title(wvw, "changed") == KC_WVW_OK;
        title = kc_wvw_get_title(wvw);
        ok &= title && !strcmp(title, "changed");
        ok &= kc_wvw_get_icon(wvw) == NULL;
        ok &= kc_wvw_set_icon(wvw, NULL) == KC_WVW_OK;
        ok &= kc_wvw_get_icon(wvw) == NULL;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_title",
        "title and icon expose named window properties", ok);
}

/**
 * Test size property setters and getters.
 * @return None.
 */
static void case_kc_wvw_size(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int width = 0;
    int height = 0;
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        ok =
            kc_wvw_set_size(wvw, 700, 500) == KC_WVW_OK &&
            kc_wvw_get_size(wvw, &width, &height) == KC_WVW_OK &&
            width == 700 &&
            height == 500;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_size",
        "set_size and get_size expose explicit dimensions", ok);
}

/**
 * Test position property setters and getters.
 * @return None.
 */
static void case_kc_wvw_position(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int x = 0;
    int y = 0;
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        ok =
            kc_wvw_set_position(wvw, 120, 140) == KC_WVW_OK &&
            kc_wvw_get_position(wvw, &x, &y) == KC_WVW_OK &&
            x == 120 &&
            y == 140;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_position",
        "set_position and get_position expose explicit coordinates", ok);
}

/**
 * Test boolean property queries.
 * @return None.
 */
static void case_kc_wvw_booleans(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = open_test(&wvw, &url, NULL, NULL);

    if (ok) {
        int visible = kc_wvw_is_visible(wvw);
        int listed = kc_wvw_is_listed(wvw);
        int minimized = kc_wvw_is_minimized(wvw);
        int maximized = kc_wvw_is_maximized(wvw);
        int fullscreen = kc_wvw_is_fullscreen(wvw);

        ok =
            (visible == 0 || visible == 1) &&
            (listed == 0 || listed == 1) &&
            (minimized == 0 || minimized == 1) &&
            (maximized == 0 || maximized == 1) &&
            (fullscreen == 0 || fullscreen == 1);
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_booleans",
        "boolean window properties use is_* queries", ok);
}

/**
 * Run all public contract cases.
 * @return None.
 */
static void run_all(void) {
#ifdef __EMSCRIPTEN__
    total = 10;
#else
    total = 12;
#endif
    case_kc_wvw_version();
    case_kc_wvw_open();
    case_kc_wvw_navigation();
    case_kc_wvw_bridge();
#ifndef __EMSCRIPTEN__
    case_kc_wvw_exec();
#endif
    case_kc_wvw_visibility();
    case_kc_wvw_actions();
    case_kc_wvw_title();
    case_kc_wvw_size();
    case_kc_wvw_position();
    case_kc_wvw_booleans();
#ifndef __EMSCRIPTEN__
    (void)case_wvw_cli();
#endif
    printf("%d passed, %d failed\n", total - failures, failures);
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "wvw"
#endif

/**
 * Run the CLI with output discarded.
 * @param args Arguments appended after the executable.
 * @return Child exit code, or -1 on execution failure.
 */
static int run_wvw_cli(const char *args) {
#ifdef _WIN32
    HANDLE null_handle;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command_line[4096];
    DWORD exit_code;

    if ((size_t)snprintf(command_line, sizeof(command_line), "\"%s\" %s",
        KC_CLI_PATH, args ? args : "") >= sizeof(command_line)) return -1;
    null_handle = CreateFileA("NUL", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (null_handle == INVALID_HANDLE_VALUE) return -1;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = null_handle;
    startup.hStdError = null_handle;
    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL,
        &startup, &process))
    {
        CloseHandle(null_handle);
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) exit_code = (DWORD)-1;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(null_handle);
    return (int)exit_code;
#else
    char command[8192];
    int rc;

    if ((size_t)snprintf(command, sizeof(command),
        "\"%s\" %s > /dev/null 2>&1", KC_CLI_PATH,
        args ? args : "") >= sizeof(command)) return -1;
    rc = system(command);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
#endif
}

/**
 * Validate the command-line argument contract without opening a window.
 * @return 0 on success, 1 on failure.
 */
static int case_wvw_cli(void) {
    int fail = 0;

    fail += run_wvw_cli("--help") != 0;
    fail += run_wvw_cli("--version") != 0;
    fail += run_wvw_cli("") == 0;
    fail += run_wvw_cli("--unknown") == 0;
    fail += run_wvw_cli("about:blank --unknown") == 0;
    fail += run_wvw_cli("about:blank --title") == 0;
    fail += run_wvw_cli("about:blank --background transparent") == 0;
    fail += run_wvw_cli("about:blank --width 0") == 0;
    fail += run_wvw_cli("about:blank --height 16385") == 0;
    fail += run_wvw_cli("about:blank --x bad") == 0;
    fail += run_wvw_cli("about:blank --y bad") == 0;
    fail += run_wvw_cli("about:blank --exec") == 0;
    result("wvw_cli", "validates command options without opening a window", !fail);
    return fail != 0;
}
#endif

/**
 * Execute the selected test group.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--exec-probe")) {
        printf("WVW PROBE:%s\n", argc > 2 ? argv[2] : "");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "--exec-fail")) {
        fputs("WVW FAIL\n", stderr);
        return 17;
    }
    kc_wvw_test_self = argv[0];
    if (argc == 1 || !strcmp(argv[1], "all")) {
        run_all();
        return failures ? 1 : 0;
    }
    fprintf(stderr, "unknown test group: %s\n", argv[1]);
    return 1;
}
