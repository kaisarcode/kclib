/**
 * libflow.h - Branch-oriented flow runtime API.
 * Summary: Public API for reusable flow templates and cancellable executions.
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
typedef struct kc_flow_run kc_flow_run_t;

#define KC_FLOW_OK 0
#define KC_FLOW_ERROR -1
#define KC_FLOW_ESTOP -2

/**
 * Receive the terminal result of one flow run.
 * A successful non-empty data buffer is caller-owned and released with
 * kc_flow_free(). Empty success and non-success statuses provide NULL data with
 * size zero. Error is borrowed for the callback duration and is NULL on success.
 * @param status KC_FLOW_OK, KC_FLOW_ESTOP, or KC_FLOW_ERROR.
 * @param data Owned successful output, or NULL.
 * @param data_size Output size.
 * @param error Borrowed contextual error for non-success, or NULL.
 * @param userdata Caller-provided callback data.
 * @return None.
 */
typedef void (*kc_flow_handler_t)(
    int status,
    void *data,
    size_t data_size,
    const char *error,
    void *userdata
);

/**
 * Open one reusable flow template from an existing flow file.
 * The path is copied. Ordered set/unset overrides remain local to this flow
 * and do not modify the source file.
 * @param out Pointer to receive flow pointer.
 * @param path Existing flow file path.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_open(kc_flow_t **out, const char *path);

/**
 * Append one ordered key-value override.
 * @param flow Flow pointer.
 * @param key Flow document key.
 * @param value Override value.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_set(kc_flow_t *flow, const char *key, const char *value);

/**
 * Append one ordered key removal override.
 * @param flow Flow pointer.
 * @param key Flow document key.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_unset(kc_flow_t *flow, const char *key);

/**
 * Start one independent run of the opened flow.
 * The run snapshots the opened path, ordered overrides, optional entry, and
 * input before returning. A successful launch produces exactly one terminal
 * callback. The handler and userdata must remain valid until that callback
 * returns.
 * @param flow Flow pointer.
 * @param out_run Pointer to receive run pointer.
 * @param entry Optional entry node reference, or NULL for declared entries.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @param handler Required terminal result handler.
 * @param userdata Caller data passed unchanged to handler.
 * @return KC_FLOW_OK when the run starts, or KC_FLOW_ERROR.
 */
int kc_flow_run(
    kc_flow_t *flow,
    kc_flow_run_t **out_run,
    const char *entry,
    const void *input,
    size_t input_size,
    kc_flow_handler_t handler,
    void *userdata
);

/**
 * Cooperatively stop one run after its current step completes.
 * The current command is not killed. The run stops before starting another
 * step and completes with KC_FLOW_ESTOP.
 * @param run Run pointer.
 * @return KC_FLOW_OK on success, or KC_FLOW_ERROR.
 */
int kc_flow_run_stop(kc_flow_run_t *run);

/**
 * Release one run. NULL is safe.
 * An unfinished run is cooperatively stopped and joined before release. Call
 * this only outside the terminal callback.
 * @param run Run pointer.
 * @return None.
 */
void kc_flow_run_close(kc_flow_run_t *run);

/**
 * Release one successful output buffer received by a run handler. NULL is safe.
 * @param ptr Owned output buffer.
 * @return None.
 */
void kc_flow_free(void *ptr);

/**
 * Release one opened flow. NULL is safe.
 * Existing runs are independent snapshots and remain valid.
 * @param flow Flow pointer.
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
