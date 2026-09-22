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

/**
 * Render the Markdown body as an HTML fragment.
 * @param input Null-terminated Markdown input.
 * @return Owned NUL-terminated HTML buffer, or NULL on invalid input or
 *     allocation failure. Release it with kc_mdp_free().
 */
char *kc_mdp_html(const char *input);

/**
 * Return the document body after recognized frontmatter.
 * @param input Null-terminated Markdown input.
 * @return Owned NUL-terminated body buffer, or NULL on invalid input or
 *     allocation failure. Release it with kc_mdp_free().
 */
char *kc_mdp_body(const char *input);

/**
 * Return recognized raw frontmatter content.
 * @param input Null-terminated Markdown input.
 * @return Owned NUL-terminated metadata buffer, or NULL on invalid input or
 *     allocation failure. Release it with kc_mdp_free().
 */
char *kc_mdp_meta(const char *input);

/**
 * Release an allocation returned by mdp. Accepts NULL.
 * @param ptr Allocation to release, or NULL.
 * @return None.
 */
void kc_mdp_free(void *ptr);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mdp_version(void);

#ifdef __cplusplus
}
#endif

#endif
