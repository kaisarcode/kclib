/**
 * libmin.c - Asset Minifier
 * Summary: Core implementation for the min library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif
#include "libmin.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define KC_MIN_INTERNAL_OK     0
#define KC_MIN_INTERNAL_ERROR -1

/**
 * Appends a single character to a dynamic output buffer.
 * @param out Pointer to the buffer pointer.
 * @param n Pointer to the current used length.
 * @param cap Pointer to the current buffer capacity.
 * @param c Character to append.
 * @return KC_MIN_INTERNAL_OK on success, or KC_MIN_INTERNAL_ERROR on allocation failure.
 */
static int kc_min_push(char **out, size_t *n, size_t *cap, char c) {
    char *grown;
    size_t next_cap;

    if (*n + 1 >= *cap) {
        next_cap = *cap == 0 ? 4096 : *cap * 2;
        grown = (char *)realloc(*out, next_cap + 1);

        if (!grown) {
            return KC_MIN_INTERNAL_ERROR;
        }

        *out = grown;
        *cap = next_cap;
    }

    (*out)[(*n)++] = c;
    (*out)[*n] = '\0';
    return KC_MIN_INTERNAL_OK;
}

/**
 * Removes a trailing space from the output buffer.
 * @param out Output buffer.
 * @param n Pointer to the current used length.
 * @return None.
 */
static void kc_min_trim_space(char *out, size_t *n) {
    if (*n > 0 && out[*n - 1] == ' ') {
        out[--(*n)] = '\0';
    }
}

/**
 * Ensures that empty minification output is represented by an owned string.
 * @param out Output buffer pointer.
 * @return KC_MIN_INTERNAL_OK on success, or KC_MIN_INTERNAL_ERROR on allocation failure.
 */
static int kc_min_finish(char **out) {
    if (*out) {
        return KC_MIN_INTERNAL_OK;
    }

    *out = (char *)malloc(1);
    if (!*out) {
        return KC_MIN_INTERNAL_ERROR;
    }

    (*out)[0] = '\0';
    return KC_MIN_INTERNAL_OK;
}

/**
 * Minifies CSS input conservatively.
 * @param in Input CSS source.
 * @param out Receives the allocated minified buffer.
 * @return KC_MIN_INTERNAL_OK on success, or KC_MIN_INTERNAL_ERROR on failure.
 */
static int kc_min_css(const char *in, char **out) {
    size_t i = 0;
    size_t n = 0;
    size_t cap = 0;
    int quote = 0;
    int esc = 0;
    int space = 0;

    *out = NULL;

    while (in[i]) {
        if (!quote && in[i] == '/' && in[i + 1] == '*') {
            i += 2;
            while (in[i] && !(in[i] == '*' && in[i + 1] == '/')) {
                i++;
            }
            if (in[i]) {
                i += 2;
            }
            continue;
        }

        if (quote) {
            if (kc_min_push(out, &n, &cap, in[i]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            esc = (!esc && in[i] == '\\');
            if (!esc && in[i] == quote) {
                quote = 0;
            }
            i++;
            continue;
        }

        if (in[i] == '"' || in[i] == '\'') {
            kc_min_trim_space(*out, &n);
            quote = in[i];
            esc = 0;
            if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            continue;
        }

        if (isspace((unsigned char)in[i])) {
            space = 1;
            i++;
            continue;
        }

        if (strchr("{}:;,>~()", in[i])) {
            kc_min_trim_space(*out, &n);
            if (in[i] == '}' && n > 0 && (*out)[n - 1] == ';') {
                (*out)[--n] = '\0';
            }
            if (in[i] == '(' && n > 0 && ((*out)[n - 1] == '+' || (*out)[n - 1] == '-')) {
                if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                    return KC_MIN_INTERNAL_ERROR;
                }
            }
            if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            space = 0;
            continue;
        }

        if (
                space &&
                n > 0 &&
                (*out)[n - 1] != '{' &&
                (*out)[n - 1] != ':' &&
                (*out)[n - 1] != ',' &&
                (*out)[n - 1] != ';') {
            if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
        }

        space = 0;

        if (
                in[i] == '0' &&
                (isalpha((unsigned char)in[i + 1]) || in[i + 1] == '%') &&
                (i == 0 || (!isdigit((unsigned char)in[i - 1]) && in[i - 1] != '.'))) {
            if (kc_min_push(out, &n, &cap, '0') != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            i++;
            while (isalpha((unsigned char)in[i]) || in[i] == '%') {
                i++;
            }
            continue;
        }

        if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
            return KC_MIN_INTERNAL_ERROR;
        }
    }

    kc_min_trim_space(*out, &n);
    return kc_min_finish(out);
}

/**
 * Minifies JavaScript input conservatively.
 * @param in Input JS source.
 * @param out Receives the allocated minified buffer.
 * @return KC_MIN_INTERNAL_OK on success, or KC_MIN_INTERNAL_ERROR on failure.
 */
static int kc_min_js(const char *in, char **out) {
    size_t i = 0;
    size_t n = 0;
    size_t cap = 0;
    int quote = 0;
    int esc = 0;
    int space = 0;
    int regex = 0;
    int regex_class = 0;
    char prev_sig = 0;

    *out = NULL;

    while (in[i]) {
        if (regex) {
            if (kc_min_push(out, &n, &cap, in[i]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            if (!esc && in[i] == '[') {
                regex_class = 1;
            } else if (!esc && in[i] == ']' && regex_class) {
                regex_class = 0;
            } else if (!esc && in[i] == '/' && !regex_class) {
                i++;
                while (isalpha((unsigned char)in[i])) {
                    if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                        return KC_MIN_INTERNAL_ERROR;
                    }
                }
                regex = 0;
                prev_sig = '/';
                continue;
            }
            esc = (!esc && in[i] == '\\');
            i++;
            continue;
        }

        if (!quote && in[i] == '/' && in[i + 1] == '*') {
            i += 2;
            while (in[i] && !(in[i] == '*' && in[i + 1] == '/')) {
                i++;
            }
            if (in[i]) {
                i += 2;
            }
            continue;
        }

        if (!quote && in[i] == '/' && in[i + 1] == '/' && (n == 0 || (*out)[n - 1] != ':')) {
            i += 2;
            while (in[i] && in[i] != '\n') {
                i++;
            }
            space = 0;
            continue;
        }

        if (!quote && in[i] == '/' && in[i + 1] != '/' && in[i + 1] != '*') {
            if (!prev_sig || strchr("([{:;,=!?&|+-*%^~<>", prev_sig)) {
                if (
                        space &&
                        n > 0 &&
                        (*out)[n - 1] != '\n' &&
                        (*out)[n - 1] != ' ' &&
                        (*out)[n - 1] != ';') {
                    if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                        return KC_MIN_INTERNAL_ERROR;
                    }
                }
                space = 0;
                regex = 1;
                regex_class = 0;
                esc = 0;
                if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                    return KC_MIN_INTERNAL_ERROR;
                }
                continue;
            }
        }

        if (quote) {
            if (kc_min_push(out, &n, &cap, in[i]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            esc = (!esc && in[i] == '\\');
            if (!esc && in[i] == quote) {
                quote = 0;
                prev_sig = '"';
            }
            i++;
            continue;
        }

        if (in[i] == '"' || in[i] == '\'' || in[i] == '`') {
            if (
                    space &&
                    n > 0 &&
                    (*out)[n - 1] != '\n' &&
                    (*out)[n - 1] != ' ' &&
                    (*out)[n - 1] != ';') {
                if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                    return KC_MIN_INTERNAL_ERROR;
                }
            }
            quote = in[i];
            esc = 0;
            if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            space = 0;
            continue;
        }

        if (isspace((unsigned char)in[i])) {
            space = 1;
            i++;
            continue;
        }

        if (strchr("{}();,[]", in[i])) {
            kc_min_trim_space(*out, &n);
            if (kc_min_push(out, &n, &cap, in[i]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            prev_sig = in[i];
            i++;
            space = 0;
            continue;
        }

        if (
                space &&
                n > 0 &&
                (*out)[n - 1] != ' ' &&
                (*out)[n - 1] != '\n' &&
                (*out)[n - 1] != '(' &&
                (*out)[n - 1] != '{' &&
                (*out)[n - 1] != '[' &&
                (*out)[n - 1] != ';') {
            if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
        }

        if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
            return KC_MIN_INTERNAL_ERROR;
        }
        if (n > 0 && !isspace((unsigned char)(*out)[n - 1])) {
            prev_sig = (*out)[n - 1];
        }
        space = 0;
    }

    kc_min_trim_space(*out, &n);
    return kc_min_finish(out);
}

/**
 * Minifies HTML input conservatively.
 * @param in Input HTML source.
 * @param out Receives the allocated minified buffer.
 * @return KC_MIN_INTERNAL_OK on success, or KC_MIN_INTERNAL_ERROR on failure.
 */
static int kc_min_html(const char *in, char **out) {
    size_t i = 0;
    size_t n = 0;
    size_t cap = 0;
    int keep = 0;
    int quote = 0;
    int esc = 0;
    int space = 0;

    *out = NULL;

    while (in[i]) {
        if (!keep && !strncmp(in + i, "<!--", 4)) {
            i += 4;
            while (in[i] && strncmp(in + i, "-->", 3)) {
                i++;
            }
            if (in[i]) {
                i += 3;
            }
            continue;
        }

        if (
                !keep &&
                in[i] == '<' &&
                (!strncmp(in + i, "<pre", 4) || !strncmp(in + i, "<textarea", 9))) {
            keep = 1;
        }

        if (keep) {
            if (
                    in[i] == '<' &&
                    (!strncmp(in + i, "</pre", 5) || !strncmp(in + i, "</textarea", 10))) {
                keep = 0;
            }
            if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
            continue;
        }

        if (isspace((unsigned char)in[i])) {
            space = 1;
            i++;
            continue;
        }

        if (in[i] == '<') {
            if (space && n > 0 && (*out)[n - 1] != ' ') {
                if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                    return KC_MIN_INTERNAL_ERROR;
                }
            }

            while (in[i]) {
                if (kc_min_push(out, &n, &cap, in[i]) != KC_MIN_INTERNAL_OK) {
                    return KC_MIN_INTERNAL_ERROR;
                }
                if (quote) {
                    esc = (!esc && in[i] == '\\');
                    if (!esc && in[i] == quote) {
                        quote = 0;
                    }
                } else if (in[i] == '"' || in[i] == '\'') {
                    quote = in[i];
                    esc = 0;
                } else if (in[i] == '>') {
                    i++;
                    break;
                }
                i++;
            }

            space = 0;
            continue;
        }

        if (space && n > 0 && (*out)[n - 1] != ' ') {
            if (kc_min_push(out, &n, &cap, ' ') != KC_MIN_INTERNAL_OK) {
                return KC_MIN_INTERNAL_ERROR;
            }
        }

        if (kc_min_push(out, &n, &cap, in[i++]) != KC_MIN_INTERNAL_OK) {
            return KC_MIN_INTERNAL_ERROR;
        }
        space = 0;
    }

    kc_min_trim_space(*out, &n);
    return kc_min_finish(out);
}

/**
 * Minify a null-terminated source string using the requested mode.
 * @param mode One of KC_MIN_MODE_CSS, KC_MIN_MODE_JS, or KC_MIN_MODE_HTML.
 * @param input Borrowed null-terminated input source.
 * @return Owned null-terminated minified string, or NULL on invalid input or allocation failure.
 */
char *kc_min_minify(int mode, const char *input) {
    char *output = NULL;
    int status;

    if (!input) {
        return NULL;
    }

    if (mode == KC_MIN_MODE_CSS) {
        status = kc_min_css(input, &output);
    } else if (mode == KC_MIN_MODE_JS) {
        status = kc_min_js(input, &output);
    } else if (mode == KC_MIN_MODE_HTML) {
        status = kc_min_html(input, &output);
    } else {
        return NULL;
    }

    if (status != KC_MIN_INTERNAL_OK) {
        free(output);
        return NULL;
    }

    return output;
}

/**
 * Release memory allocated by the min library.
 * @param ptr Owned pointer returned by the API, or NULL.
 * @return None.
 */
void kc_min_free(void *ptr) {
    free(ptr);
}

#ifndef KC_MIN_BUILD_VERSION
#define KC_MIN_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_min_version(void) {
    return (uint64_t)KC_MIN_BUILD_VERSION;
}
