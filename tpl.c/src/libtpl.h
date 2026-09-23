/**
 * tpl.h - Template renderer.
 * Summary: Public API for reusable template instances.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_TPL_H
#define KC_TPL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tpl kc_tpl_t;

typedef struct {
    const char *root;
} kc_tpl_options_t;

typedef struct {
    const char *key;
    const char *value;
} kc_tpl_var_t;

#define KC_TPL_OK      0
#define KC_TPL_ERROR  -1

/**
 * Creates a reusable template instance and copies the template source.
 * @param out Pointer to receive the template instance.
 * @param source Borrowed null-terminated template source.
 * @param options Optional template options. NULL uses defaults.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_open(
    kc_tpl_t **out,
    const char *source,
    const kc_tpl_options_t *options
);

/**
 * Renders the stored template with one isolated set of variables.
 * @param tpl Template instance.
 * @param vars Optional variable array.
 * @param var_count Number of entries in vars.
 * @return Owned null-terminated output, or NULL on failure.
 */
char *kc_tpl_render(
    kc_tpl_t *tpl,
    const kc_tpl_var_t *vars,
    size_t var_count
);

/**
 * Returns the latest template error, or "invalid template" when tpl is NULL.
 * @param tpl Template instance.
 * @return Static or template-owned error text.
 */
const char *kc_tpl_error(const kc_tpl_t *tpl);

/**
 * Releases output returned by kc_tpl_render. Passing NULL is valid.
 * @param ptr Output allocation to release.
 * @return None.
 */
void kc_tpl_free(void *ptr);

/**
 * Releases a template instance and its owned source.
 * @param tpl Template instance, or NULL.
 * @return None.
 */
void kc_tpl_close(kc_tpl_t *tpl);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tpl_version(void);

#ifdef __cplusplus
}
#endif

#endif
