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
#define KC_FLOW_ESTOP -2

/**
 * Allocate one flow runtime context.
 * @param out Pointer to receive context pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_open(kc_flow_t **out);

/**
 * Release one flow runtime context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_flow_close(kc_flow_t *ctx);

/**
 * Append one ordered key-value overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @param value Overlay value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_set(kc_flow_t *ctx, const char *key, const char *value);

/**
 * Append one ordered key removal overlay operation.
 * @param ctx Context pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_unset(kc_flow_t *ctx, const char *key);

/**
 * Execute one flow file, optionally from one explicit entry node.
 * @param ctx Context pointer.
 * @param path Flow file path.
 * @param entry Optional entry node reference, or NULL for declared entries.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param out_data Owned output buffer pointer.
 * @param out_size Output buffer size pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_exec(
    kc_flow_t *ctx,
    const char *path,
    const char *entry,
    const void *input,
    size_t input_size,
    void **out_data,
    size_t *out_size
);

/**
 * Release one output buffer produced by the runtime.
 * @param ptr Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *ptr);

/**
 * Request stop for a specific flow context.
 * @param ctx Context pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR on failure.
 */
int kc_flow_stop(kc_flow_t *ctx);

/**
 * Returns the last error message from a flow context.
 * @param ctx Context pointer.
 * @return Borrowed error string, or NULL.
 */
const char *kc_flow_get_error(const kc_flow_t *ctx);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_flow_version(void);

#ifdef __cplusplus
}
#endif

#endif
