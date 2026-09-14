/**
 * wvw.c - Native WebView window wrapper.
 * Summary: Command line interface for opening one native WebView window.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwvw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_wvw_help(const char *name) {
    printf("Usage: %s [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("    --url <url>         Set initial URL\n");
    printf("    --title <title>     Set window title\n");
    printf("    --background <hex>  Set WebView background as RRGGBB or AARRGGBB\n");
    printf("    --width <px>        Set window width\n");
    printf("    --height <px>       Set window height\n");
    printf("    --posx <px>         Set window x position\n");
    printf("    --posy <px>         Set window y position\n");
    printf("    --fullscreen        Start in fullscreen mode\n");
    printf("    --borderless        Start in borderless mode\n");
    printf("    --always-on-top     Keep the window above normal windows\n");
    printf("    --click-through     Ignore mouse input on the host window\n");
    printf("    --no-focus          Do not activate the window for keyboard focus\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show build version\n");
}

/**
 * Replaces one owned option string.
 * @param field Destination option field.
 * @param value New string value.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on allocation failure.
 */
static int kc_wvw_set_string(char **field, const char *value) {
    char *copy;

    if (!field || !value) return KC_WVW_ERROR;
    copy = strdup(value);
    if (!copy) return KC_WVW_ERROR;
    free(*field);
    *field = copy;
    return KC_WVW_OK;
}

/**
 * Parses one decimal integer option.
 * @param text Decimal input text.
 * @param out_value Destination integer.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on invalid input.
 */
static int kc_wvw_parse_int(const char *text, int *out_value) {
    char *end;
    long value;

    if (!text || !out_value) return KC_WVW_ERROR;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return KC_WVW_ERROR;
    if (value < -2147483647L - 1L || value > 2147483647L) return KC_WVW_ERROR;
    *out_value = (int)value;
    return KC_WVW_OK;
}

/**
 * Executes the command line interface through the public C API.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    int status = 0;
    int i;

    opts = kc_wvw_options_default();
    kc_wvw_options_load_env(&opts);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_wvw_help(argv[0]);
            goto cleanup;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("wvw build %llu\n", (unsigned long long)kc_wvw_version());
            goto cleanup;
        }
        if (strcmp(argv[i], "--url") == 0 || strcmp(argv[i], "--title") == 0 ||
            strcmp(argv[i], "--background") == 0) {
            const char *option = argv[i];
            char **field;
            if (++i >= argc) {
                fprintf(stderr, "wvw: missing value for %s\n", option);
                status = 1;
                goto cleanup;
            }
            if (strcmp(option, "--url") == 0) field = &opts.url;
            else if (strcmp(option, "--title") == 0) field = &opts.title;
            else field = &opts.background;
            if (kc_wvw_set_string(field, argv[i]) != KC_WVW_OK) {
                fprintf(stderr, "wvw: allocation failed\n");
                status = 1;
                goto cleanup;
            }
            continue;
        }
        if (strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "--height") == 0 ||
            strcmp(argv[i], "--posx") == 0 || strcmp(argv[i], "--posy") == 0) {
            const char *option = argv[i];
            int *field;
            if (++i >= argc) {
                fprintf(stderr, "wvw: invalid value for %s\n", option);
                status = 1;
                goto cleanup;
            }
            if (strcmp(option, "--width") == 0) field = &opts.width;
            else if (strcmp(option, "--height") == 0) field = &opts.height;
            else if (strcmp(option, "--posx") == 0) field = &opts.posx;
            else field = &opts.posy;
            if (kc_wvw_parse_int(argv[i], field) != KC_WVW_OK) {
                fprintf(stderr, "wvw: invalid value for %s\n", option);
                status = 1;
                goto cleanup;
            }
            continue;
        }
        if (strcmp(argv[i], "--fullscreen") == 0) opts.fullscreen = 1;
        else if (strcmp(argv[i], "--borderless") == 0) opts.borderless = 1;
        else if (strcmp(argv[i], "--always-on-top") == 0) opts.always_on_top = 1;
        else if (strcmp(argv[i], "--click-through") == 0) opts.click_through = 1;
        else if (strcmp(argv[i], "--no-focus") == 0) opts.no_focus = 1;
        else {
            fprintf(stderr, "wvw: unknown option '%s'\n", argv[i]);
            status = 1;
            goto cleanup;
        }
    }

    if (!opts.url || !opts.url[0]) {
        fprintf(stderr, "wvw: missing URL\n");
        status = 1;
        goto cleanup;
    }
    if (kc_wvw_open(&ctx, &opts) != KC_WVW_OK) {
        const char *error = kc_wvw_get_error(ctx);
        fprintf(stderr, "wvw: %s\n", error ? error : "open failed");
        status = 1;
        goto cleanup;
    }
    if (kc_wvw_loop(ctx) != KC_WVW_OK) {
        fprintf(stderr, "wvw: event loop failed\n");
        status = 1;
    }

cleanup:
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    return status;
}
