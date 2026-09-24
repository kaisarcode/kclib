/**
 * flow.h - Branch-oriented flow runtime API.
 * Summary: Public API for opening flows and running independent branches.
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
 * Open one flow from an existing flow file.
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
 * input before returning. Execution continues in its own context.
 * @param flow Flow pointer.
 * @param out_run Pointer to receive run pointer.
 * @param entry Optional entry node reference, or NULL for declared entries.
 * @param input Optional input buffer.
 * @param input_size Input buffer size.
 * @return KC_FLOW_OK when the run starts, or KC_FLOW_ERROR.
 */
int kc_flow_run(
    kc_flow_t *flow,
    kc_flow_run_t **out_run,
    const char *entry,
    const void *input,
    size_t input_size
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
 * Wait for one run to complete and receive its final output.
 * Successful non-empty output is caller-owned and released with kc_flow_free.
 * Empty success and non-success statuses return NULL with size zero.
 * @param run Run pointer.
 * @param out_data Pointer to receive owned output.
 * @param out_size Pointer to receive output size.
 * @return KC_FLOW_OK, KC_FLOW_ESTOP, or KC_FLOW_ERROR.
 */
int kc_flow_run_wait(
    kc_flow_run_t *run,
    void **out_data,
    size_t *out_size
);

/**
 * Return the contextual error for one run.
 * The string is borrowed until the run is closed.
 * @param run Run pointer.
 * @return Borrowed error string, or NULL for NULL.
 */
const char *kc_flow_run_error(const kc_flow_run_t *run);

/**
 * Release one run. NULL is safe.
 * An unfinished run is cooperatively stopped and joined before release.
 * @param run Run pointer.
 * @return None.
 */
void kc_flow_run_close(kc_flow_run_t *run);

/**
 * Release one output buffer returned by kc_flow_run_wait. NULL is safe.
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
