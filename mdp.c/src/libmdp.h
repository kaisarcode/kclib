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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mdp kc_mdp_t;

#define KC_MDP_OK          0
#define KC_MDP_ERROR      -1

#define KC_MDP_MODE_NONE   0
#define KC_MDP_MODE_HTML   1
#define KC_MDP_MODE_BODY   2
#define KC_MDP_MODE_META   3

/**
 * Initialize a new mdp context.
 * @param out Pointer to receive the context pointer.
 * @return KC_MDP_OK on success, or KC_MDP_ERROR on failure.
 */
int kc_mdp_open(kc_mdp_t **out);

/**
 * Release a mdp context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_mdp_close(kc_mdp_t *ctx);

/**
 * Select the output mode for a context.
 * @param ctx Context pointer.
 * @param mode Output mode.
 * @return KC_MDP_OK on success, or KC_MDP_ERROR on invalid input.
 */
int kc_mdp_set_mode(kc_mdp_t *ctx, int mode);

/**
 * Convert a mode name to an output mode.
 * @param name Mode name.
 * @return Output mode, or KC_MDP_MODE_NONE for invalid input.
 */
int kc_mdp_mode(const char *name);

/**
 * Execute Markdown processing using the selected context mode.
 * @param ctx Context pointer.
 * @param input Null-terminated Markdown input.
 * @param out Receives a NUL-terminated output buffer owned by the caller, or
 *     NULL on failure. Release it with kc_mdp_free(), never raw free().
 * @param out_len Receives the output byte count excluding the terminator.
 * @return KC_MDP_OK on success, or KC_MDP_ERROR on failure.
 */
int kc_mdp_exec(kc_mdp_t *ctx, const char *input, unsigned char **out,
    size_t *out_len);

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
