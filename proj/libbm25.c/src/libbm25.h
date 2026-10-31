/**
 * libbm25.h - BM25 text retrieval.
 * Summary: Public API for indexing and ranking text using BM25.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_BM25_H
#define KC_BM25_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_bm25 kc_bm25_t;

#define KC_BM25_OK 0
#define KC_BM25_EINVAL -1
#define KC_BM25_ENOMEM -2
#define KC_BM25_ESTATE -3
#define KC_BM25_EIO -4
#define KC_BM25_EFORMAT -5
#define KC_BM25_EUTF8 -6
#define KC_BM25_EABORT -7

typedef struct {
    const int *fold_accents;
} kc_bm25_options_t;

typedef struct {
    const size_t *limit;
    const double *k1;
    const double *b;
} kc_bm25_search_options_t;

typedef struct {
    size_t ordinal;
    const char *id;
    const char *content;
    double score;
} kc_bm25_result_t;

/**
 * Create an empty, writable BM25 index.
 * An omitted fold_accents option defaults to false. Explicit values must be
 * zero or one. An index owns all successfully added document strings.
 * @param out Receives the index handle, or NULL on failure.
 * @param options Optional analyzer configuration.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_open(kc_bm25_t **out, const kc_bm25_options_t *options);

/**
 * Add one document with an arbitrary, possibly repeated, public ID.
 * Documents receive distinct zero-based ordinals in insertion order.
 * Adding after build invalidates the index until the next build.
 * @param index Writable index.
 * @param id Nonempty document ID.
 * @param content UTF-8 document text; the empty string is valid.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_add(kc_bm25_t *index, const char *id, const char *content);

/**
 * Build or rebuild the searchable inverted index.
 * @param index Writable index.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_build(kc_bm25_t *index);

/**
 * Search documents with the Okapi BM25 lexical ranking function.
 * Defaults are limit 10, k1 1.2 and b 0.75. Results are sorted by
 * descending score and then ascending ordinal. Query terms are deduplicated.
 * A result array is caller-owned; its ID and content strings borrow index
 * storage and remain valid until the index is mutated or closed.
 * @param index Built or loaded index.
 * @param query UTF-8 query string.
 * @param options Optional search parameters.
 * @param out_results Receives results; release with kc_bm25_free().
 * @param out_count Receives the number of results.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_search(
    const kc_bm25_t *index,
    const char *query,
    const kc_bm25_search_options_t *options,
    kc_bm25_result_t **out_results,
    size_t *out_count
);

/**
 * Visit normalized tokens using the same Unicode analyzer as this index.
 * Token strings are borrowed for the callback duration. Returning nonzero
 * aborts traversal with KC_BM25_EABORT.
 * @param index Built or imported index determining accent normalization.
 * @param text UTF-8 source text.
 * @param visit Required token callback.
 * @param userdata Forwarded to the callback.
 * @return KC_BM25_OK, KC_BM25_EABORT, or an error code.
 */
typedef int (*kc_bm25_token_fn)(const char *token, void *userdata);

int kc_bm25_tokens(const kc_bm25_t *index, const char *text,
    kc_bm25_token_fn visit, void *userdata);

/**
 * Visit each distinct normalized vocabulary term and its document frequency.
 * Strings remain borrowed and valid for the callback duration.
 * @param term Distinct normalized word.
 * @param document_count Number of indexed associations containing the term.
 * @param userdata Caller context.
 * @return Zero to continue, nonzero to abort.
 */
typedef int (*kc_bm25_word_fn)(const char *term, size_t document_count,
    void *userdata);

int kc_bm25_vocabulary(const kc_bm25_t *index,
    kc_bm25_word_fn visit, void *userdata);

/**
 * Visit distinct document ordinals containing one normalized vocabulary term.
 * The term must already use the index's normalization; the supplied string
 * is not normalized again. Frequency counts occurrences within each document.
 * @param ordinal Zero-based insertion ordinal.
 * @param frequency Occurrences of this term in the document.
 * @param userdata Caller context.
 * @return Zero to continue, nonzero to abort.
 */
typedef int (*kc_bm25_posting_fn)(size_t ordinal, size_t frequency,
    void *userdata);

int kc_bm25_postings(const kc_bm25_t *index, const char *term,
    kc_bm25_posting_fn visit, void *userdata);

/**
 * Export a built index as a self-contained, portable binary memory value.
 * The byte buffer is caller-owned and released with kc_bm25_free().
 * @param index Built index.
 * @param out Receives allocated serialized bytes.
 * @param out_size Receives the number of bytes.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_export(
    const kc_bm25_t *index,
    void **out,
    size_t *out_size
);

/**
 * Load a read-only index from a bounded, immutable memory value.
 * This call copies the necessary data; the source buffer may be released
 * immediately after a successful import. No document re-tokenization occurs.
 * @param out Receives the read-only index.
 * @param data Serialized index bytes.
 * @param size Exact byte length.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_import(
    kc_bm25_t **out,
    const void *data,
    size_t size
);

/**
 * Save one built index to a portable binary file.
 * The destination is overwritten directly; replacement is not atomic.
 * @param index Built index.
 * @param path Destination path.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_save(const kc_bm25_t *index, const char *path);

/**
 * Load a portable binary index without re-tokenizing its documents.
 * Loaded indexes are read-only.
 * @param out Receives the loaded index, or NULL on failure.
 * @param path Source file path.
 * @return KC_BM25_OK or an error status.
 */
int kc_bm25_load(kc_bm25_t **out, const char *path);

/**
 * Get the number of document records, including repeated public IDs.
 * @param index Index handle, or NULL.
 * @return Document count or zero for NULL.
 */
size_t kc_bm25_count(const kc_bm25_t *index);

/**
 * Get whether the index strips accent combining marks.
 * @param index Index handle, or NULL.
 * @return 1 when accent folding is enabled, otherwise 0.
 */
int kc_bm25_fold_accents(const kc_bm25_t *index);

/**
 * Release a search result array.
 * @param results Array returned by kc_bm25_search(), or NULL.
 * @return None.
 */
void kc_bm25_free(void *results);

/**
 * Close an index and release all owned storage.
 * @param index Index handle, or NULL.
 * @return None.
 */
void kc_bm25_close(kc_bm25_t *index);

/**
 * Describe a library status value.
 * @param status Status code.
 * @return Static status string.
 */
const char *kc_bm25_strerror(int status);

/**
 * Get the generated build timestamp.
 * @return Unix timestamp, or zero if unspecified.
 */
uint64_t kc_bm25_version(void);

#ifdef __cplusplus
}
#endif

#endif
