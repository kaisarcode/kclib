/**
 * flow.h - Branch-oriented flow runtime API.
 * Summary: Public API for loading and executing flat flow documents.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_FLOW_H
#define KC_FLOW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_flow kc_flow_t;

#define KC_FLOW_OK 0
#define KC_FLOW_ERROR -1

/**
 * Open one flow runtime from an existing flow file.
 * The path is copied by the runtime. Ordered set/unset overrides remain local
 * to this runtime and do not modify the file.
 * @param out Pointer to receive runtime pointer.
 * @param path Existing flow file path.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_open(kc_flow_t **out, const char *path);

/**
 * Append one ordered key-value override.
 * @param flow Runtime pointer.
 * @param key Flow document key.
 * @param value Override value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set(kc_flow_t *flow, const char *key, const char *value);

/**
 * Append one ordered key removal override.
 * @param flow Runtime pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_unset(kc_flow_t *flow, const char *key);

/**
 * Execute the opened flow, optionally from one explicit entry node.
 * @param flow Runtime pointer.
 * @param entry Optional entry node reference, or NULL for declared entries.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param out_data Owned output buffer pointer.
 * @param out_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec(
    kc_flow_t *flow,
    const char *entry,
    const void *input,
    size_t input_size,
    void **out_data,
    size_t *out_size
);

/**
 * Return the last contextual error.
 * The string is borrowed from the runtime and remains valid until the next
 * operation that changes the error or until kc_flow_close().
 * @param flow Runtime pointer.
 * @return Borrowed error string, or NULL for NULL.
 */
const char *kc_flow_error(const kc_flow_t *flow);

/**
 * Release one output buffer produced by the runtime.
 * NULL is safe.
 * @param ptr Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *ptr);

/**
 * Release one flow runtime. NULL is safe.
 * @param flow Runtime pointer.
 * @return None.
 */
void kc_flow_close(kc_flow_t *flow);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_flow_version(void);

#ifdef __cplusplus
}
#endif

#endif
