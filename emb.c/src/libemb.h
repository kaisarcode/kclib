/**
 * emb.h - Vector Embedding Library Public API
 * Summary: Public interface for the ggml-based embedding library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_EMB_H
#define KC_EMB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_emb_version(void);

typedef struct kc_emb kc_emb_t;

#define KC_EMB_OK      0
#define KC_EMB_ERROR  -1
#define KC_EMB_ESTOP  -3

/**
 * Embedding options.
 */
typedef struct kc_emb_options {
    int _unused;
} kc_emb_options_t;

/**
 * Initialize a new emb context.
 * Prepares one GGML inference context backed by the embedded model. The
 * worker uses a bounded CPU thread set for each embedding request.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_EMB_OK on success, or KC_EMB_ERROR on failure.
 */
int kc_emb_open(kc_emb_t **out, const kc_emb_options_t *opts);

/**
 * Release a emb context.
 * Shuts down the worker thread and frees all resources.
 * Must not be called while kc_emb_exec() is active on any thread.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_emb_close(kc_emb_t *ctx);

/**
 * Request stop for a specific emb context.
 * @param ctx Context pointer.
 * @return KC_EMB_OK on success, or KC_EMB_ERROR on failure.
 */
int kc_emb_stop(kc_emb_t *ctx);

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_emb_options_t kc_emb_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_emb_options_load_env(kc_emb_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_emb_options_free(kc_emb_options_t *opts);

/**
 * Retrieve the embedding dimension.
 * @param ctx Context pointer.
 * @return Dimension size, or 0 on invalid input.
 */
int kc_emb_dim(kc_emb_t *ctx);

/**
 * Generate an embedding for the given input text.
 * Dispatches to the prepared worker and blocks until the result is ready.
 * Multiple callers on the same context are serialized. The result is written
 * into the caller-supplied buffer.
 * @param ctx Context pointer.
 * @param input Null-terminated input text.
 * @param out Caller-supplied buffer of at least kc_emb_dim(ctx) floats.
 * @return KC_EMB_OK on success, KC_EMB_ERROR on failure.
 */
int kc_emb_exec(kc_emb_t *ctx, const char *input, float *out);

#ifdef __cplusplus
}
#endif

#endif
