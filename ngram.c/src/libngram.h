/**
 * ngram.h
 * Summary: Public API for descending sliding-window n-gram traversal.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef NGRAM_H
#define NGRAM_H

#include <stddef.h>
#include <stdint.h>

#define KC_NGRAM_OK      0
#define KC_NGRAM_ERROR  -1
#define KC_NGRAM_ESTOP  -3

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_ngram kc_ngram_t;

/**
 * One emitted chunk.
 * @param input Original input buffer backing this span.
 * @param byte_start Inclusive byte start offset in the original input.
 * @param byte_end Exclusive byte end offset in the original input.
 * @param start Inclusive token start index.
 * @param end Inclusive token end index.
 * @param size Number of tokens in the span.
 * @return No return value.
 */
typedef struct {
    const char *input;
    size_t byte_start;
    size_t byte_end;
    int start;
    int end;
    int size;
} kc_ngram_chunk_t;

/**
 * Traversal options.
 * @param max_tokens Maximum window size.
 * @param min_tokens Minimum window size.
 * @param separators Byte set treated as token separators.
 * @return No return value.
 */
typedef struct {
    int max_tokens;
    int min_tokens;
    const char *separators;
} kc_ngram_options_t;

/**
 * Callback invoked for each emitted chunk.
 * The callback is invoked synchronously from the calling thread.
 * @param chunk Current emitted chunk.
 * @param context Caller-provided opaque context.
 * @return 1 to close the span, 0 to keep it open, or -1 to abort traversal.
 */
typedef int (*kc_ngram_visit_fn)(const kc_ngram_chunk_t *chunk, void *context);

/**
 * Initialize a new ngram context.
 * @param out Pointer to receive the context pointer.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_open(kc_ngram_t **out);

/**
 * Release an ngram context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_ngram_close(kc_ngram_t *ctx);

/**
 * Request stop for a specific ngram context.
 * @param ctx Context pointer.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_stop(kc_ngram_t *ctx);

/**
 * Checks whether a stop request has been raised on the context.
 * @param ctx Context pointer.
 * @return 1 when stop was requested, or 0 otherwise.
 */
int kc_ngram_stop_requested(kc_ngram_t *ctx);

/**
 * Fills one options structure with default values.
 * @param options Destination options structure.
 * @return 0 on success, or -1 on invalid input.
 */
int kc_ngram_options_default(kc_ngram_options_t *options);

/**
 * Attach one mutable options struct to the context runtime.
 * @param ctx Context pointer.
 * @param options Runtime options, or NULL to restore internal defaults.
 * @return KC_NGRAM_OK on success, or KC_NGRAM_ERROR on failure.
 */
int kc_ngram_configure(kc_ngram_t *ctx, kc_ngram_options_t *options);

/**
 * Executes descending sliding-window traversal for the given input text.
 * This function is reentrant and uses only per-call traversal state.
 * @param input Input text to tokenize and traverse.
 * @param options Traversal options, or NULL to use defaults.
 * @param visit Callback invoked for each chunk.
 * @param context Caller-provided opaque context.
 * @return Number of emitted chunks, or -1 on failure.
 */
int kc_ngram_execute(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *context
);

/**
 * Loads environment variables from the env config table into options.
 * @return None.
 */
void kc_ngram_options_load_env(kc_ngram_options_t *opts);

/**
 * Frees any resources held by the options structure.
 * @return None.
 */
void kc_ngram_options_free(kc_ngram_options_t *opts);

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_ngram_version(void);

#ifdef __cplusplus
}
#endif

#endif
