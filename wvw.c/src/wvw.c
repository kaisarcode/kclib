/**
 * wvw.c - Native WebView window CLI.
 * Summary: Adapts terminal configuration to the public wvw API and blocks
 *          until the native window closes.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwvw.h"
#include "libwvw_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void kc_wvw_env_int(const char *name, int *value) {
    const char *text = getenv(name);
    int parsed;
    if (text && kc_wvw_parse_int(text, &parsed) == KC_WVW_OK) *value = parsed;
}

int main(int argc, char **argv) {
    kc_wvw_t *wvw = NULL;
    kc_wvw_options_t options = {0};
    const char *url = getenv("KC_WVW_URL");
    const char *title = getenv("KC_WVW_TITLE");
    const char *background = getenv("KC_WVW_BACKGROUND");
    int width = 1280;
    int height = 720;
    int posx = 0;
    int posy = 0;
    int fullscreen = 0;
    int borderless = 0;
    int always_on_top = 0;
    int click_through = 0;
    int no_focus = 0;
    int status = 0;
    int i;

    kc_wvw_env_int("KC_WVW_WIDTH", &width);
    kc_wvw_env_int("KC_WVW_HEIGHT", &height);
    kc_wvw_env_int("KC_WVW_POSX", &posx);
    kc_wvw_env_int("KC_WVW_POSY", &posy);
    kc_wvw_env_int("KC_WVW_FULLSCREEN", &fullscreen);
    kc_wvw_env_int("KC_WVW_BORDERLESS", &borderless);
    kc_wvw_env_int("KC_WVW_ALWAYS_ON_TOP", &always_on_top);
    kc_wvw_env_int("KC_WVW_CLICK_THROUGH", &click_through);
    kc_wvw_env_int("KC_WVW_NO_FOCUS", &no_focus);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            kc_wvw_help(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("wvw build %llu\n", (unsigned long long)kc_wvw_version());
            return 0;
        }
        if (!strcmp(argv[i], "--url") || !strcmp(argv[i], "--title") ||
            !strcmp(argv[i], "--background")) {
            const char *flag = argv[i++];
            if (i >= argc) {
                fprintf(stderr, "wvw: missing value for %s\n", flag);
                return 1;
            }
            if (!strcmp(flag, "--url")) url = argv[i];
            else if (!strcmp(flag, "--title")) title = argv[i];
            else background = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--width") || !strcmp(argv[i], "--height") ||
            !strcmp(argv[i], "--posx") || !strcmp(argv[i], "--posy")) {
            const char *flag = argv[i++];
            int *field;
            if (i >= argc) {
                fprintf(stderr, "wvw: invalid value for %s\n", flag);
                return 1;
            }
            if (!strcmp(flag, "--width")) field = &width;
            else if (!strcmp(flag, "--height")) field = &height;
            else if (!strcmp(flag, "--posx")) field = &posx;
            else field = &posy;
            if (kc_wvw_parse_int(argv[i], field) != KC_WVW_OK) {
                fprintf(stderr, "wvw: invalid value for %s\n", flag);
                return 1;
            }
            continue;
        }
        if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--borderless")) borderless = 1;
        else if (!strcmp(argv[i], "--always-on-top")) always_on_top = 1;
        else if (!strcmp(argv[i], "--click-through")) click_through = 1;
        else if (!strcmp(argv[i], "--no-focus")) no_focus = 1;
        else {
            fprintf(stderr, "wvw: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!url || !url[0]) {
        fprintf(stderr, "wvw: missing URL\n");
        return 1;
    }

    options.url = url;
    options.title = title;
    options.background = background;
    options.width = &width;
    options.height = &height;
    options.posx = &posx;
    options.posy = &posy;
    options.fullscreen = &fullscreen;
    options.borderless = &borderless;
    options.always_on_top = &always_on_top;
    options.click_through = &click_through;
    options.no_focus = &no_focus;

    if (kc_wvw_open(&wvw, &options) != KC_WVW_OK) {
        const char *error = kc_wvw_get_error(wvw);
        fprintf(stderr, "wvw: %s\n", error ? error : "open failed");
        status = 1;
    } else if (kc_wvw_cli_wait(wvw) != KC_WVW_OK) {
        fprintf(stderr, "wvw: wait failed\n");
        status = 1;
    }

    kc_wvw_close(wvw);
    return status;
}
