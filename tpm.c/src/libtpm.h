/**
 * tpm.h - Text profile matcher.
 * Summary: Public API for the tpm library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_TPM_H
#define KC_TPM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tpm kc_tpm_t;

#define KC_TPM_OK      0
#define KC_TPM_ERROR  -1

/**
 * Allocate and initialize a new tpm context.
 * Prepares one inference context. Must be paired with kc_tpm_close().
 * @param out Pointer to receive the context pointer.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_open(kc_tpm_t **out);

/**
 * Build an n-gram profile from map text.
 * @param tpm Context pointer.
 * @param map_text Text to build profile from.
 * @param ngram_size N-gram size (must be >= 1).
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_build(kc_tpm_t *tpm, const char *map_text, int ngram_size);

/**
 * Score input text against the built profile.
 * @param tpm Context pointer with built profile.
 * @param input_text Text to score.
 * @return Score in [0.0, 1.0].
 */
double kc_tpm_score(kc_tpm_t *tpm, const char *input_text);

/**
 * Release a tpm context.
 * @param tpm Context pointer.
 * @return None.
 */
void kc_tpm_close(kc_tpm_t *tpm);

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_tpm_version(void);

#ifdef __cplusplus
}
#endif

#endif
