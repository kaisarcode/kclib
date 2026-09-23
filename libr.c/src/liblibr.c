/**
 * liblibr.c - Minimal example library.
 * Summary: Core implementation for the libr example library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "liblibr.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_LIBR_BUILD_VERSION
#define KC_LIBR_BUILD_VERSION 0
#endif

/**
 * Create a greeting for one name.
 * @param name Name to greet.
 * @return Owned NUL-terminated greeting, or NULL on error.
 */
char *kc_libr_greet(const char *name) {
    static const char prefix[] = "Hello ";
    size_t prefix_len;
    size_t name_len;
    char *out;

    if (!name) {
        return NULL;
    }

    prefix_len = sizeof(prefix) - 1;
    name_len = strlen(name);
    if (name_len > (size_t)-1 - prefix_len - 2) {
        return NULL;
    }

    out = (char *)malloc(prefix_len + name_len + 2);
    if (!out) {
        return NULL;
    }

    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, name, name_len);
    out[prefix_len + name_len] = '!';
    out[prefix_len + name_len + 1] = '\0';
    return out;
}

/**
 * Release memory returned by libr.
 * @param ptr Pointer returned by libr, or NULL.
 * @return None.
 */
void kc_libr_free(void *ptr) {
    free(ptr);
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void) {
    return (uint64_t)KC_LIBR_BUILD_VERSION;
}
