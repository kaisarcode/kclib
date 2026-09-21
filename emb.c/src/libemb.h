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

#include <stddef.h>
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

#define KC_EMB_OK 0
#define KC_EMB_ERROR -1

/**
 * Initialize a new emb context.
 * @param out Pointer to receive the context pointer.
 * @return KC_EMB_OK on success, or KC_EMB_ERROR on failure.
 */
int kc_emb_open(kc_emb_t **out);

/**
 * Release an emb context.
 * Shuts down the worker thread and frees all resources.
 * Must not be called while kc_emb_exec() is active on any thread.
 * @param ctx Context pointer.
 * @return void
 */
void kc_emb_close(kc_emb_t *ctx);

/**
 * Retrieve the embedding dimension.
 * @param ctx Context pointer.
 * @return Dimension size, or 0 on invalid input.
 */
size_t kc_emb_dim(const kc_emb_t *ctx);

/**
 * Generate an embedding for the given input text.
 * Dispatches to the prepared worker and blocks until the result is ready.
 * Multiple callers on the same context are serialized.
 *
 * Ownership and lifetime:
 * - input is borrowed for the duration of the call only; the library does
 *   not retain it after return.
 * - out_data is caller-owned on success; the library allocates a buffer
 *   containing the embedding floats which the caller must free via
 *   kc_emb_free(). Caller must not free with free().
 * - out_count is set to the embedding dimension on success and equals
 *   kc_emb_dim(ctx).
 *
 * @param ctx Context pointer.
 * @param input Null-terminated input text, borrowed for call.
 * @param out_data Output pointer to receive caller-owned float
 * buffer; free with kc_emb_free().
 * @param out_count Output count, equals dim (kc_emb_dim(ctx)) on success.
 * @return KC_EMB_OK on success, KC_EMB_ERROR on failure.
 */
int kc_emb_exec(kc_emb_t *ctx, const char *input, float **out_data, size_t *out_count);

/**
 * Free memory allocated by the library.
 * @param ptr Pointer previously returned by kc_emb_exec() via out_data.
 * @return void
 */
void kc_emb_free(void *ptr);

/**
 * Retrieve the last error message for a context.
 *
 * Ownership and lifetime:
 * - Returned string is borrowed and owned by ctx.
 * - Caller must not free or modify the returned pointer.
 * - Valid until the next operation that mutates the error state or until
 *   kc_emb_close() closes the context.
 * - A fresh context returns an empty string ("").
 * - If ctx is NULL, returns NULL.
 *
 * @param ctx Context pointer, may be NULL.
 * @return Borrowed error string owned by ctx, empty string for fresh
 * context, or NULL if ctx is NULL.
 */
const char *kc_emb_get_error(const kc_emb_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
