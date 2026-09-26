/**
 * test.c - libwvw public contract tests.
 * Summary: Exercises the consumer-facing WebView API and grouped CLI behavior.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwvw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef WVW_TEST_CLI
#define WVW_TEST_CLI ""
#endif

static int current;
static int total;
static int failures;

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
static int open_test(kc_wvw_t **out, char **url_out) {
    kc_wvw_options_t options = {0};
    int width = 640;
    int height = 480;
    char *url = test_url();

    if (!url) return 0;
    options.url = url;
    options.title = "wvw test";
    options.width = &width;
    options.height = &height;
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
    kc_wvw_close(NULL);
    result("kc_wvw_open", "rejects missing URL and close accepts NULL", ok);
}

/**
 * Test immediate navigation after open.
 * @return None.
 */
static void case_kc_wvw_navigation(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = open_test(&wvw, &url);

    if (ok) ok = kc_wvw_navigate(wvw, url) == KC_WVW_OK;
    kc_wvw_close(wvw);
    free(url);
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
    const char *methods[] = { "ping" };
    kc_wvw_bridge_options_t bridge = {0};
    int ok = open_test(&wvw, &url);

    if (ok) {
        bridge.methods = methods;
        bridge.method_count = 1;
        bridge.callback = bridge_callback;
        bridge.allow_file = 1;
        ok =
            kc_wvw_add_init_script(
                wvw, "window.__wvw_test = true;") == KC_WVW_OK &&
            kc_wvw_enable_bridge(wvw, &bridge) == KC_WVW_OK &&
            kc_wvw_post_bridge_event(
                wvw, "{\"type\":\"test\"}") == KC_WVW_OK;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_bridge",
        "init script, bridge and native event use the direct public surface",
        ok);
}

/**
 * Test show, hide, and visibility query behavior.
 * @return None.
 */
static void case_kc_wvw_visibility(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = open_test(&wvw, &url);

    if (ok) {
        ok =
            kc_wvw_hide(wvw) == KC_WVW_OK &&
            kc_wvw_is_visible(wvw) == 0 &&
            kc_wvw_show(wvw) == KC_WVW_OK;
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_visibility",
        "show and hide are actions with an explicit boolean query", ok);
}

/**
 * Test minimize, maximize, and restore actions.
 * @return None.
 */
static void case_kc_wvw_actions(void) {
    kc_wvw_t *wvw = NULL;
    char *url = NULL;
    int ok = open_test(&wvw, &url);

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
    int ok = open_test(&wvw, &url);

    if (ok) {
        ok = kc_wvw_set_title(wvw, "changed") == KC_WVW_OK;
        title = kc_wvw_get_title(wvw);
        ok &= title && !strcmp(title, "changed");
    }
    kc_wvw_close(wvw);
    free(url);
    result("kc_wvw_title",
        "set_title and get_title expose one named property", ok);
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
    int ok = open_test(&wvw, &url);

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
    int ok = open_test(&wvw, &url);

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
    int ok = open_test(&wvw, &url);

    if (ok) {
        int visible = kc_wvw_is_visible(wvw);
        int minimized = kc_wvw_is_minimized(wvw);
        int maximized = kc_wvw_is_maximized(wvw);
        int fullscreen = kc_wvw_is_fullscreen(wvw);

        ok =
            (visible == 0 || visible == 1) &&
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
 * Run one CLI invocation and classify its process status.
 * @param args CLI argument string.
 * @param expect_success Non-zero when success is expected.
 * @return 1 when the observed status matches the expectation, otherwise 0.
 */
static int run_cli(const char *args, int expect_success) {
    char command[2048];
    int rc;

    if (!WVW_TEST_CLI[0]) return 1;
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"%s\" %s > NUL 2>&1",
        WVW_TEST_CLI, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        WVW_TEST_CLI, args);
#endif
    rc = system(command);
    return expect_success ? rc == 0 : rc != 0;
}

/**
 * Test grouped CLI behavior.
 * @return None.
 */
static void case_kc_wvw_cli(void) {
    int ok = 1;

    ok &= run_cli("--help", 1);
    ok &= run_cli("-h", 1);
    ok &= run_cli("--version", 1);
    ok &= run_cli("-v", 1);
    ok &= run_cli("", 0);
    ok &= run_cli("--invalid", 0);
    ok &= run_cli("--url", 0);
    ok &= run_cli("--width", 0);
    ok &= run_cli("--width nope --url about:blank", 0);
    ok &= run_cli("--height 12x --url about:blank", 0);
    result("kc_wvw_cli",
        "help, version, missing URL, unknown options and invalid values", ok);
}

/**
 * Run all public contract cases.
 * @return None.
 */
static void run_all(void) {
    total = 11;
    case_kc_wvw_version();
    case_kc_wvw_open();
    case_kc_wvw_navigation();
    case_kc_wvw_bridge();
    case_kc_wvw_visibility();
    case_kc_wvw_actions();
    case_kc_wvw_title();
    case_kc_wvw_size();
    case_kc_wvw_position();
    case_kc_wvw_booleans();
    case_kc_wvw_cli();
}

/**
 * Execute the selected test group.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc == 1 || !strcmp(argv[1], "all")) {
        run_all();
        return failures ? 1 : 0;
    }
    fprintf(stderr, "unknown test group: %s\n", argv[1]);
    return 1;
}
