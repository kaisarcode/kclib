/**
 * libr.h - Summary of the functionality
 * Summary: Public API for the libr library.
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
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void);

#ifdef __cplusplus
}
#endif

#endif
