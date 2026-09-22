/**
 * liblibr.c - Summary of the functionality
 * Summary: Core implementation for the libr library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "liblibr.h"

#include <stdint.h>

#ifndef KC_LIBR_BUILD_VERSION
#define KC_LIBR_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void) {
    return (uint64_t)KC_LIBR_BUILD_VERSION;
}
