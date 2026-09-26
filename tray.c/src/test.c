/** test.c - libtray public API contract. License: GPL-3.0. */
#include "libtray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdatomic.h>
#ifndef KC_TRAY_TEST_CLI
#define KC_TRAY_TEST_CLI ""
#endif
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#else
#include <unistd.h>
#endif

static int current, total, failures;
static void result(const char *name, const char *detail, int ok) {
    current++;
    if (!ok) failures++;
    printf("[%d/%d] [%s] %s: %s\n", current, total,
           ok ? "PASS" : "FAIL", name, detail);
}
static int same(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}
static void case_version(void) {
    result("version", "build version is nonzero", kc_tray_version() != 0);
}
static void case_open_close(void) {
    kc_tray_t *t = (kc_tray_t *)(uintptr_t)1;
    int ok = kc_tray_open(NULL, NULL) == KC_TRAY_ERROR;
    ok &= kc_tray_open(&t, NULL) == KC_TRAY_OK && t != NULL;
    kc_tray_close(t == (kc_tray_t *)(uintptr_t)1 ? NULL : t);
    kc_tray_close(NULL);
    result("open_close", "open is nonblocking and close ends ownership", ok);
}
static void case_options(void) {
    kc_tray_t *t = NULL;
    char tip[] = "Initial tooltip";
    kc_tray_options_t options = {NULL, tip};
    int ok = kc_tray_open(&t, &options) == KC_TRAY_OK;
    if (ok) {
        tip[0] = 'X';
        ok = same(kc_tray_get_tooltip(t), "Initial tooltip") &&
             kc_tray_get_icon(t) == NULL;
        kc_tray_close(t);
    }
    result("options", "initial strings are copied and NULL options are valid", ok);
}
static void case_icon(void) {
    kc_tray_t *t = NULL;
    const char *icon = NULL;
#if defined(_WIN32)
    /* A minimal 1x1 ICO file lets the native setter be tested without a fixture. */
    static const unsigned char ico[70] = {
        0,0,1,0,1,0,1,1,0,0,1,0,32,0,48,0,
        0,0,22,0,0,0,40,0,0,0,1,0,0,0,2,0,
        0,0,1,0,32,0,0,0,0,0,8,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        255,255,0,0,0,0
    };
    char path[MAX_PATH] = {0};
    char dir[MAX_PATH];
    FILE *file = NULL;
    if (GetTempPathA(MAX_PATH, dir) && GetTempFileNameA(dir, "ktr", 0, path)) {
        file = fopen(path, "wb");
        if (file) { fwrite(ico, 1, sizeof(ico), file); fclose(file); icon = path; }
    }
#elif defined(__APPLE__)
    icon = "NSApplicationIcon";
#else
    icon = "emblem-system";
#endif
    int ok = kc_tray_open(&t, NULL) == KC_TRAY_OK;
    if (ok) {
        ok = icon && kc_tray_get_icon(t) == NULL &&
             kc_tray_set_icon(t, icon) == KC_TRAY_OK &&
             same(kc_tray_get_icon(t), icon) &&
             kc_tray_set_icon(t, NULL) == KC_TRAY_OK &&
             kc_tray_get_icon(t) == NULL;
        kc_tray_close(t);
    }
#if defined(_WIN32)
    if (path[0]) DeleteFileA(path);
#endif
    result("icon", "native icon reset and borrowed getter", ok);
}
static void case_tooltip(void) {
    kc_tray_t *t = NULL;
    int ok = kc_tray_open(&t, NULL) == KC_TRAY_OK;
    if (ok) {
        char tip[] = "Updated";
        ok = kc_tray_set_tooltip(t, tip) == KC_TRAY_OK;
        tip[0] = 'X';
        ok &= same(kc_tray_get_tooltip(t), "Updated");
        ok &= kc_tray_set_tooltip(t, NULL) == KC_TRAY_OK &&
              kc_tray_get_tooltip(t) == NULL;
        kc_tray_close(t);
    }
    result("tooltip", "native tooltip update, copy and clear", ok);
}
static atomic_int callback_stage;
static _Atomic(kc_tray_item_t *) expected_first, expected_once, expected_quit;
static _Atomic(kc_tray_t *) closing_tray;
static void record(kc_tray_item_t *item, void *data) {
    (void)data;
    if (item == atomic_load(&expected_first)) atomic_store(&callback_stage, 1);
}
static void remove_self(kc_tray_item_t *item, void *data) {
    (void)data;
    if (item == atomic_load(&expected_once) && atomic_load(&callback_stage) == 1)
        atomic_store(&callback_stage, 2);
    kc_tray_item_remove(item);
}
static void close_from_callback(kc_tray_item_t *item, void *data) {
    kc_tray_t *t;
    (void)data;
    if (item == atomic_load(&expected_quit) && atomic_load(&callback_stage) == 2)
        atomic_store(&callback_stage, 3);
    t = atomic_exchange(&closing_tray, NULL);
    kc_tray_close(t);
#if defined(__APPLE__)
    [NSApp stop:nil];
#endif
}
static void case_items(void) {
    kc_tray_t *t = NULL;
    kc_tray_item_t *a = NULL, *sep = NULL;
    int ok = kc_tray_open(&t, NULL) == KC_TRAY_OK;
    if (ok) {
        ok = kc_tray_add_item(t, &a, "First", record, NULL) == KC_TRAY_OK &&
             a && same(kc_tray_item_get_text(a), "First");
        ok &= kc_tray_add_separator(t, &sep) == KC_TRAY_OK && sep &&
              kc_tray_item_get_text(sep) == NULL;
        if (a) {
            ok &= kc_tray_item_set_text(a, "Second") == KC_TRAY_OK &&
                  same(kc_tray_item_get_text(a), "Second");
            ok &= kc_tray_item_set_text(a, NULL) == KC_TRAY_ERROR;
            kc_tray_item_remove(a);
        }
        kc_tray_item_remove(sep);
        kc_tray_item_remove(NULL);
        kc_tray_close(t);
    }
    result("items", "add, set/get text, separator and remove", ok);
}
static void case_lifecycle(void) {
    kc_tray_t *t = NULL;
    kc_tray_item_t *a = NULL, *b = NULL;
    int ok = kc_tray_open(&t, NULL) == KC_TRAY_OK;
    if (ok) {
        ok = kc_tray_add_item(t, &a, "A", NULL, NULL) == KC_TRAY_OK &&
             kc_tray_add_item(t, &b, "B", NULL, NULL) == KC_TRAY_OK;
        kc_tray_close(t);
    }
    result("lifecycle", "tray close destroys all children", ok);
}
static void case_error(void) {
    kc_tray_t *t = NULL;
    int ok = kc_tray_get_error(NULL) == NULL &&
             kc_tray_open(&t, NULL) == KC_TRAY_OK;
    if (ok) {
        kc_tray_item_t *invalid = (kc_tray_item_t *)(uintptr_t)1;
        ok = kc_tray_get_error(t) == NULL &&
             kc_tray_add_item(t, &invalid, "", NULL, NULL) == KC_TRAY_ERROR &&
             invalid == NULL && kc_tray_get_error(t) != NULL;
        kc_tray_close(t);
    }
    result("error", "context error after rejected input", ok);
}
static void case_cli(void) {
    int ok = 1;
    char command[1024];
    if (KC_TRAY_TEST_CLI[0]) {
#if defined(_WIN32)
        snprintf(command, sizeof(command), "\"%s\" --help > NUL", KC_TRAY_TEST_CLI);
#else
        snprintf(command, sizeof(command), "\"%s\" --help > /dev/null", KC_TRAY_TEST_CLI);
#endif
        ok = system(command) == 0;
#if defined(_WIN32)
        snprintf(command, sizeof(command), "\"%s\" --version > NUL", KC_TRAY_TEST_CLI);
#else
        snprintf(command, sizeof(command), "\"%s\" --version > /dev/null", KC_TRAY_TEST_CLI);
#endif
        ok &= system(command) == 0;
#if defined(_WIN32)
        snprintf(command, sizeof(command), "\"%s\" --invalid > NUL 2>&1", KC_TRAY_TEST_CLI);
#else
        snprintf(command, sizeof(command), "\"%s\" --invalid > /dev/null 2>&1", KC_TRAY_TEST_CLI);
#endif
        ok &= system(command) != 0;
    }
    result("cli", "help, version and invalid argument diagnostics", ok);
}
/* Opt-in desktop integration: select the three items in order. This exercises
 * actual native dispatch, exact identity, removal and close during callbacks.
 * The ordinary suite never simulates a callback by calling it directly.
 */
static void case_callbacks(void) {
    kc_tray_t *t = NULL;
    kc_tray_item_t *first = NULL, *once = NULL, *quit = NULL;
    int ok = kc_tray_open(&t, NULL) == KC_TRAY_OK;
    atomic_store(&callback_stage, 0);
    atomic_store(&closing_tray, t);
    if (ok) {
        ok &= kc_tray_add_item(t, &first, "1. Select me", record, NULL) == 0;
        ok &= kc_tray_add_item(t, &once, "2. Remove me", remove_self, NULL) == 0;
        ok &= kc_tray_add_item(t, &quit, "3. Close tray", close_from_callback, NULL) == 0;
        atomic_store(&expected_first, first);
        atomic_store(&expected_once, once);
        atomic_store(&expected_quit, quit);
        if (ok) {
            fprintf(stderr, "Select 1, 2, 3 in the tray menu (30 seconds).\n");
#if defined(__APPLE__)
            [NSTimer scheduledTimerWithTimeInterval:30.0 target:NSApp
                       selector:@selector(stop:) userInfo:nil repeats:NO];
            [NSApp run];
#else
            for (int i = 0; i < 300 && atomic_load(&closing_tray); i++) {
#if defined(_WIN32)
                Sleep(100);
#else
                struct timespec ts = {0, 100000000};
                nanosleep(&ts, NULL);
#endif
            }
#endif
            ok = atomic_load(&callback_stage) == 3 &&
                 atomic_load(&closing_tray) == NULL;
        }
        t = atomic_exchange(&closing_tray, NULL);
        if (t) kc_tray_close(t);
    }
    result("callbacks", "exact item, self removal and close from callback", ok);
}
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "interactive") == 0) {
        total = 1; case_callbacks(); return failures ? 1 : 0;
    }
    if (argc != 2 || strcmp(argv[1], "all") != 0) return 2;
    total = 9;
    case_version(); case_open_close(); case_options(); case_icon();
    case_tooltip(); case_items(); case_lifecycle(); case_error(); case_cli();
    printf("\n%d passed, %d failed\n", total - failures, failures);
    return failures ? 1 : 0;
}
