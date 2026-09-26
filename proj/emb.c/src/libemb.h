/**
 * emb.h - Vector Embedding Library Public API
 * Summary: Public interface for fixed-model text embeddings.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_EMB_H
#define KC_EMB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_EMB_OK 0
#define KC_EMB_ERROR -1

/**
 * Return the fixed embedding dimension of the embedded model.
 * @return Model embedding dimension, or zero if initialization fails.
 */
size_t kc_emb_dimension(void);

/**
 * Generate an embedding for one input text using the embedded model.
 * Input is borrowed for the duration of the call. On success, out_data receives
 * a caller-owned float array and out_count receives its element count.
 * Release the array with kc_emb_free().
 * @param input Null-terminated input text.
 * @param out_data Destination for the owned embedding vector.
 * @param out_count Destination for the vector element count.
 * @return KC_EMB_OK on success, KC_EMB_ERROR on invalid input or failure.
 */
int kc_emb_embed(const char *input, float **out_data, size_t *out_count);

/**
 * Release memory allocated by the emb library.
 * @param ptr Owned pointer returned by kc_emb_embed(), or NULL.
 * @return None.
 */
void kc_emb_free(void *ptr);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_emb_version(void);

#ifdef __cplusplus
}
#endif

#endif
