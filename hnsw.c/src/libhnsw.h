/**
 * libhnsw.h - HNSW Vector Search
 * Summary: Public API for one in-memory approximate nearest-neighbor index.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_HNSW_H
#define KC_HNSW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_hnsw kc_hnsw_t;

#define KC_HNSW_OK 0
#define KC_HNSW_EINVAL -1
#define KC_HNSW_ENOMEM -2
#define KC_HNSW_ESTATE -3

#define KC_HNSW_METRIC_COSINE 1
#define KC_HNSW_METRIC_INNER_PRODUCT 2
#define KC_HNSW_METRIC_L2 3

typedef struct {
    const char *id;
    double score;
} kc_hnsw_result_t;

typedef struct {
    size_t dimension;
    int metric;
    int max_connections;
    int build_effort;
    int search_effort;
} kc_hnsw_options_t;

/**
 * Return default index options.
 * dimension defaults to zero and must be set by the caller.
 * Other fields use library defaults and may be left unchanged.
 * @return Default-initialized options.
 */
kc_hnsw_options_t kc_hnsw_options_default(void);

/**
 * Create one in-memory index.
 * dimension is required. Zero metric, max_connections, build_effort, and
 * search_effort values select the library defaults, so zero-initialized
 * partial options are valid.
 * @param out Receives the new index.
 * @param options Index configuration.
 * @return KC_HNSW_OK on success, or a negative status code.
 */
int kc_hnsw_open(
    kc_hnsw_t **out,
    const kc_hnsw_options_t *options
);

/**
 * Add one vector. The index copies both id and values.
 * Adding after build invalidates the graph until kc_hnsw_build() is called
 * again.
 * @param hnsw Index handle.
 * @param id Non-empty vector identifier.
 * @param values Vector values matching the configured dimension.
 * @return KC_HNSW_OK on success, or a negative status code.
 */
int kc_hnsw_add(
    kc_hnsw_t *hnsw,
    const char *id,
    const float *values
);

/**
 * Build or rebuild the approximate-neighbor graph.
 * @param hnsw Index handle.
 * @return KC_HNSW_OK on success, or a negative status code.
 */
int kc_hnsw_build(kc_hnsw_t *hnsw);

/**
 * Search a built index.
 * For cosine and inner product, threshold is a minimum accepted score.
 * For L2, threshold is a maximum accepted squared distance.
 * Results are caller-owned and must be released with kc_hnsw_free().
 * Result ids borrow index storage.
 * @param hnsw Index handle.
 * @param query Query vector matching the configured dimension.
 * @param limit Maximum number of results.
 * @param threshold Metric-specific acceptance threshold.
 * @param out_results Receives the allocated result array.
 * @param out_count Receives the result count.
 * @return KC_HNSW_OK on success, or a negative status code.
 */
int kc_hnsw_search(
    const kc_hnsw_t *hnsw,
    const float *query,
    size_t limit,
    double threshold,
    kc_hnsw_result_t **out_results,
    size_t *out_count
);

/**
 * Release memory returned by this library.
 * @param ptr Allocation to release, or NULL.
 * @return None.
 */
void kc_hnsw_free(void *ptr);

/**
 * Return the configured vector dimension.
 * @param hnsw Index handle.
 * @return Dimension, or zero for NULL.
 */
size_t kc_hnsw_dimension(const kc_hnsw_t *hnsw);

/**
 * Return the configured metric.
 * @param hnsw Index handle.
 * @return Metric constant, or zero for NULL.
 */
int kc_hnsw_metric(const kc_hnsw_t *hnsw);

/**
 * Return the number of inserted vectors.
 * @param hnsw Index handle.
 * @return Vector count, or zero for NULL.
 */
size_t kc_hnsw_count(const kc_hnsw_t *hnsw);

/**
 * Release one index. NULL is safe.
 * @param hnsw Index handle.
 * @return None.
 */
void kc_hnsw_close(kc_hnsw_t *hnsw);

/**
 * Return a static message for a public status code.
 * @param status Status code.
 * @return Static message.
 */
const char *kc_hnsw_strerror(int status);

/**
 * Return the generated build version.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_hnsw_version(void);

#ifdef __cplusplus
}
#endif

#endif
