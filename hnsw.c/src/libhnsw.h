/**
 * libhnsw.h - HNSW Vector Search
 * Summary: HNSW-based approximate nearest neighbor search library.
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
#define KC_HNSW_ESTOP -4

#define KC_HNSW_METRIC_COSINE 1
#define KC_HNSW_METRIC_INNER_PRODUCT 2
#define KC_HNSW_METRIC_L2 3

/**
 * One ranked search result. Result IDs borrow context storage.
 * @param id Vector identifier.
 * @param score Similarity score or squared distance.
 * @return None.
 */
typedef struct {
    const char *id;
    double score;
} kc_hnsw_result_t;

/**
 * Configuration for one vector index instance. Options are plain caller-owned values.
 * @param dimension Fixed vector dimension for all entries.
 * @param metric Configured similarity metric.
 * @param m Maximum graph connections per level.
 * @param ef_construction Construction search budget.
 * @param ef_search Query search budget.
 * @return None.
 */
typedef struct {
    size_t dimension;
    int metric;
    int m;
    int ef_construction;
    int ef_search;
} kc_hnsw_options_t;

/**
 * Creates default vector index options.
 * @return Default-initialized options.
 */
kc_hnsw_options_t kc_hnsw_options_default(void);

/**
 * Creates one vector index instance.
 * @param out Receives the new index on success.
 * @param options Index configuration.
 * @return Status code.
 */
int kc_hnsw_open(kc_hnsw_t **out, const kc_hnsw_options_t *options);

/**
 * Releases one vector index instance.
 * @param ctx Index pointer.
 * @return None.
 */
void kc_hnsw_close(kc_hnsw_t *ctx);

/**
 * Reserves capacity for a target number of vectors.
 * @param ctx Index pointer.
 * @param capacity Target vector capacity.
 * @return Status code.
 */
int kc_hnsw_reserve(kc_hnsw_t *ctx, size_t capacity);

/**
 * Inserts one vector. The index copies both the ID and vector.
 * @param ctx Index pointer.
 * @param id Vector identifier.
 * @param values Vector values with the configured dimension.
 * @return Status code.
 */
int kc_hnsw_add(kc_hnsw_t *ctx, const char *id, const float *values);

/**
 * Constructs the HNSW graph. May return KC_HNSW_ESTOP.
 * @param ctx Index pointer.
 * @return Status code.
 */
int kc_hnsw_build(kc_hnsw_t *ctx);

/**
 * Searches an index. The query is borrowed. Results are caller-owned and must
 * be released with kc_hnsw_free(). Thresholds are minimum similarity for
 * cosine/inner and maximum squared distance for L2. May return KC_HNSW_ESTOP.
 * @param ctx Index pointer.
 * @param query Borrowed query vector.
 * @param limit Maximum number of results.
 * @param threshold Acceptance threshold.
 * @param out_results Receives the allocated result array.
 * @param out_count Receives the result count.
 * @return Status code.
 */
int kc_hnsw_search(const kc_hnsw_t *ctx, const float *query, size_t limit,
    double threshold, kc_hnsw_result_t **out_results,
    size_t *out_count);

/**
 * Releases memory returned by this library.
 * @param ptr Allocation to release.
 * @return None.
 */
void kc_hnsw_free(void *ptr);

/**
 * Requests clean termination for one vector index context.
 * @param ctx Index pointer.
 * @return Status code.
 */
int kc_hnsw_stop(kc_hnsw_t *ctx);

/**
 * Returns the configured vector dimension.
 * @param ctx Index pointer.
 * @return Dimension value, or 0 on invalid input.
 */
size_t kc_hnsw_dimension(const kc_hnsw_t *ctx);

/**
 * Returns the configured similarity metric.
 * @param ctx Index pointer.
 * @return Metric constant, or 0 on invalid input.
 */
int kc_hnsw_metric(const kc_hnsw_t *ctx);

/**
 * Returns the number of inserted vectors.
 * @param ctx Index pointer.
 * @return Vector count, or 0 on invalid input.
 */
size_t kc_hnsw_count(const kc_hnsw_t *ctx);

/**
 * Resolves one metric name into a metric constant.
 * @param name Metric text name.
 * @return Metric constant, or 0 on invalid input.
 */
int kc_hnsw_metric_from_string(const char *name);

/**
 * Resolves one metric constant into a metric name.
 * @param metric Metric constant.
 * @return Static metric name, or NULL on invalid input.
 */
const char *kc_hnsw_metric_to_string(int metric);

/**
 * Resolves one status code into text.
 * @param rc Status code.
 * @return Static string.
 */
const char *kc_hnsw_strerror(int rc);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_hnsw_version(void);

#ifdef __cplusplus
}
#endif

#endif
