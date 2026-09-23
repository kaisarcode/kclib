/**
 * libr.h - Minimal example library.
 * Summary: Public API for the libr example library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_LIBR_H
#define KC_LIBR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a greeting for one name.
 * @param name Name to greet.
 * @return Owned NUL-terminated greeting, or NULL on error.
 */
char *kc_libr_greet(const char *name);

/**
 * Release memory returned by libr.
 * @param ptr Pointer returned by libr, or NULL.
 * @return None.
 */
void kc_libr_free(void *ptr);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void);

#ifdef __cplusplus
}
#endif

#endif
