/**
 * wvw.c - Native WebView window CLI.
 * Summary: Command-line interface backed by libwvw.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwvw.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t kc_cli_stop;

/**
 * Record one process termination signal.
 * @param sig Signal number.
 * @return None.
 */
static void kc_cli_signal(int sig)
{
    (void)sig;
    kc_cli_stop = 1;
}

/**
 * Sleep briefly while the window process remains active.
 * @return None.
 */
static void kc_cli_sleep(void)
{
#ifdef _WIN32
    Sleep(100);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Parse one signed integer.
 * @param text Input text.
 * @param out Destination value.
 * @return 1 on success, otherwise 0.
 */
static int kc_cli_int(const char *text, int *out)
{
    long value;
    char *end;

    if (!text || !text[0] || !out) return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || *end != '\0' || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

/**
 * Parse one valid WebView size dimension.
 * @param text Input text.
 * @param out Destination value.
 * @return 1 on success, otherwise 0.
 */
static int kc_cli_size(const char *text, int *out)
{
    int value;

    if (!kc_cli_int(text, &value)) return 0;
    if (value <= 0 || value > KC_WVW_SIZE_MAX) return 0;
    *out = value;
    return 1;
}

/**
 * Validate one WebView background color.
 * @param text RRGGBB or AARRGGBB color text.
 * @return 1 when valid, otherwise 0.
 */
static int kc_cli_background(const char *text)
{
    size_t length;
    size_t i;

    if (!text) return 0;
    length = strlen(text);
    if (length != 6U && length != 8U) return 0;
    for (i = 0U; i < length; i++) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/**
 * Print command usage.
 * @return None.
 */
static void kc_cli_help(void)
{
    printf("Usage: wvw <url> [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  --title <title>         Window title (default: wvw)\n");
    printf("  --background <color>    RRGGBB or AARRGGBB; 00 alpha is transparent\n");
    printf("  --icon <icon>           Icon file path or platform icon name\n");
    printf("  --width <px>            Content width (default: 1280)\n");
    printf("  --height <px>           Content height (default: 720)\n");
    printf("  --x <px>                Window X position\n");
    printf("  --y <px>                Window Y position\n");
    printf("  --fullscreen            Start fullscreen\n");
    printf("  --borderless            Start without native window borders\n");
    printf("  --always-on-top         Keep the window above normal windows\n");
    printf("  --click-through         Pass pointer input through the window\n");
    printf("  --no-focus              Prevent the window from taking focus\n");
    printf("  --hidden                Start with the native window hidden\n");
    printf("  --unlist                Exclude the window from the window list\n");
    printf("  --exec <path>           Expose executable to NativeBridge.exec\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Print the linked libwvw build version.
 * @return None.
 */
static void kc_cli_version(void)
{
    printf("wvw build %llu\n", (unsigned long long)kc_wvw_version());
}

/**
 * Report one libwvw operation failure.
 * @param wvw WebView handle, or NULL.
 * @param operation Operation description.
 * @return Process failure status.
 */
static int kc_cli_error(const kc_wvw_t *wvw, const char *operation)
{
    const char *error = wvw ? kc_wvw_get_error(wvw) : NULL;

    if (error && error[0])
        fprintf(stderr, "wvw: %s: %s\n", operation, error);
    else
        fprintf(stderr, "wvw: %s\n", operation);
    return 1;
}

/**
 * Execute the command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    kc_wvw_options_t options;
    kc_wvw_t *wvw = NULL;
    int width = 0;
    int height = 0;
    int posx = 0;
    int posy = 0;
    int fullscreen = 0;
    int borderless = 0;
    int always_on_top = 0;
    int click_through = 0;
    int no_focus = 0;
    int hidden = 0;
    int unlist = 0;
    const char **executables;
    size_t executable_count = 0;
    int i;

    if (argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        kc_cli_help();
        return 0;
    }
    if (argc == 2 &&
        (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0))
    {
        kc_cli_version();
        return 0;
    }
    if (argc < 2) {
        kc_cli_help();
        return 1;
    }
    memset(&options, 0, sizeof(options));
    executables = calloc((size_t)argc, sizeof(*executables));
    if (!executables) return 1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--title") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "wvw: --title requires a value\n");
                return 1;
            }
            options.title = argv[i];
        } else if (strcmp(argv[i], "--background") == 0) {
            if (++i >= argc || !kc_cli_background(argv[i])) {
                fprintf(stderr,
                    "wvw: --background requires RRGGBB or AARRGGBB\n");
                return 1;
            }
            options.background = argv[i];
        } else if (strcmp(argv[i], "--icon") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "wvw: --icon requires a value\n");
                return 1;
            }
            options.icon = argv[i];
        } else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc || !kc_cli_size(argv[i], &width)) {
                fprintf(stderr, "wvw: --width requires 1..%d\n",
                    KC_WVW_SIZE_MAX);
                return 1;
            }
            options.width = &width;
        } else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc || !kc_cli_size(argv[i], &height)) {
                fprintf(stderr, "wvw: --height requires 1..%d\n",
                    KC_WVW_SIZE_MAX);
                return 1;
            }
            options.height = &height;
        } else if (strcmp(argv[i], "--x") == 0) {
            if (++i >= argc || !kc_cli_int(argv[i], &posx)) {
                fprintf(stderr, "wvw: --x requires an integer\n");
                return 1;
            }
            options.posx = &posx;
        } else if (strcmp(argv[i], "--y") == 0) {
            if (++i >= argc || !kc_cli_int(argv[i], &posy)) {
                fprintf(stderr, "wvw: --y requires an integer\n");
                return 1;
            }
            options.posy = &posy;
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            fullscreen = 1;
            options.fullscreen = &fullscreen;
        } else if (strcmp(argv[i], "--borderless") == 0) {
            borderless = 1;
            options.borderless = &borderless;
        } else if (strcmp(argv[i], "--always-on-top") == 0) {
            always_on_top = 1;
            options.always_on_top = &always_on_top;
        } else if (strcmp(argv[i], "--click-through") == 0) {
            click_through = 1;
            options.click_through = &click_through;
        } else if (strcmp(argv[i], "--no-focus") == 0) {
            no_focus = 1;
            options.no_focus = &no_focus;
        } else if (strcmp(argv[i], "--hidden") == 0) {
            hidden = 1;
            options.hidden = &hidden;
        } else if (strcmp(argv[i], "--unlist") == 0) {
            unlist = 1;
            options.unlist = &unlist;
        } else if (strcmp(argv[i], "--exec") == 0) {
            if (++i >= argc || !argv[i][0] || argv[i][0] == '-') {
                fprintf(stderr, "wvw: --exec requires a value\n");
                free(executables);
                return 1;
            }
            executables[executable_count++] = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "wvw: unknown option: %s\n", argv[i]);
            free(executables);
            return 1;
        } else if (!options.url) {
            options.url = argv[i];
        } else {
            fprintf(stderr, "wvw: unexpected positional argument: %s\n", argv[i]);
            free(executables);
            return 1;
        }
    }

    if (!options.url) {
        kc_cli_help();
        free(executables);
        return 1;
    }

    if (kc_wvw_open(&wvw, &options) != KC_WVW_OK) {
        free(executables);
        return kc_cli_error(wvw, "failed to open WebView");
    }
    if (kc_wvw_set_executables(wvw, &(kc_wvw_exec_options_t){
        executables, executable_count}) != KC_WVW_OK) {
        free(executables);
        kc_wvw_close(wvw);
        return kc_cli_error(wvw, "failed to expose executables");
    }
    free(executables);

    kc_cli_stop = 0;
    signal(SIGINT, kc_cli_signal);
    signal(SIGTERM, kc_cli_signal);
    while (!kc_cli_stop) kc_cli_sleep();

    kc_wvw_close(wvw);
    return 0;
}
