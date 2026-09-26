/**
 * mdp.h - Markdown Parser
 * Summary: Public API for the mdp library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MDP_H
#define KC_MDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mdp kc_mdp_t;

#define KC_MDP_OK      0
#define KC_MDP_ERROR  -1

/**
 * Create a reusable Markdown document.
 * Frontmatter is split from the body once during this call.
 * @param out Pointer to receive the document.
 * @param input Null-terminated Markdown input.
 * @return KC_MDP_OK on success, KC_MDP_ERROR on failure.
 */
int kc_mdp_open(kc_mdp_t **out, const char *input);

/**
 * Render and cache the document body as an HTML fragment.
 * The returned pointer belongs to the document and remains valid until
 * kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return Cached NUL-terminated HTML fragment, or NULL on failure.
 */
const char *kc_mdp_html(kc_mdp_t *mdp);

/**
 * Return the document body after recognized frontmatter.
 * The returned pointer belongs to the document and remains valid until
 * kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated body text, or NULL for an invalid document.
 */
const char *kc_mdp_body(const kc_mdp_t *mdp);

/**
 * Return recognized raw frontmatter content.
 * The returned pointer belongs to the document and remains valid until
 * kc_mdp_close().
 * @param mdp Document returned by kc_mdp_open().
 * @return NUL-terminated metadata text, or NULL for an invalid document.
 */
const char *kc_mdp_meta(const kc_mdp_t *mdp);

/**
 * Release a Markdown document. Accepts NULL.
 * @param mdp Document returned by kc_mdp_open(), or NULL.
 * @return None.
 */
void kc_mdp_close(kc_mdp_t *mdp);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mdp_version(void);

#ifdef __cplusplus
}
#endif

#endif
