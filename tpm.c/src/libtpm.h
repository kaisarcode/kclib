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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tpm kc_tpm_t;

typedef struct {
    int ngram_size;
} kc_tpm_options_t;

#define KC_TPM_OK      0
#define KC_TPM_ERROR  -1

/**
 * Create a reusable text profile.
 * A successful call always returns a profile ready for kc_tpm_score().
 * @param out Pointer to receive the profile.
 * @param map_text Representative text used to build the profile.
 * @param options Optional configuration. NULL uses ngram_size = 3.
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_open(
    kc_tpm_t **out,
    const char *map_text,
    const kc_tpm_options_t *options
);

/**
 * Score input text against the profile.
 * @param tpm Profile pointer returned by kc_tpm_open().
 * @param input_text Text to score.
 * @param out_score Destination for the score in [0.0, 1.0].
 * @return KC_TPM_OK on success, KC_TPM_ERROR on failure.
 */
int kc_tpm_score(
    const kc_tpm_t *tpm,
    const char *input_text,
    double *out_score
);

/**
 * Release a text profile.
 * @param tpm Profile pointer, or NULL.
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
