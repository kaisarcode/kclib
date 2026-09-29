/**
 * demo.h - Minimal example library.
 * Summary: Public API for the demo example library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_DEMO_H
#define KC_DEMO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a greeting for one name.
 * @param name Name to greet.
 * @return Owned NUL-terminated greeting, or NULL on error.
 */
char *kc_demo_greet(const char *name);

/**
 * Release memory returned by demo.
 * @param ptr Pointer returned by demo, or NULL.
 * @return None.
 */
void kc_demo_free(void *ptr);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_demo_version(void);

#ifdef __cplusplus
}
#endif

#endif
