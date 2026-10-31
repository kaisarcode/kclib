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
