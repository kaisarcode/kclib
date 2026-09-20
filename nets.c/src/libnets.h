/**
 * nets.h - Network sender.
 * Summary: Public API for sending byte buffers over TCP or UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_NETS_H
#define KC_NETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_nets kc_nets_t;

#define KC_NETS_OK        0
#define KC_NETS_EINVAL   -1
#define KC_NETS_ENET     -2
#define KC_NETS_ESTOP    -3

#define KC_NETS_TCP       1
#define KC_NETS_UDP       2
#define KC_NETS_TLS       3

/**
 * Initialize a new nets context.
 * @param out Destination context pointer.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_open(kc_nets_t **out);

/**
 * Release a nets context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_nets_close(kc_nets_t *ctx);

/**
 * Request stop for a specific nets context.
 * @param ctx Context handle.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_stop(kc_nets_t *ctx);

/**
 * Check whether a stop has been requested on the context.
 * @param ctx Context pointer.
 * @return 1 if stop was requested, 0 otherwise.
 */
int kc_nets_stop_requested(const kc_nets_t *ctx);

/**
 * Sends bytes to one network address and returns the response.
 * @param ctx      Context handle.
 * @param host     Destination host or IP address.
 * @param port     Destination port.
 * @param proto    KC_NETS_TCP, KC_NETS_UDP, or KC_NETS_TLS.
 * @param data      Borrowed input for the call; caller retains ownership.
 * @param data_size Input buffer size in bytes.
 * @param out_data  Receives owned binary response bytes.
 *                  Release the bytes with kc_nets_free().
 * @param out_size  Receives the authoritative response size in bytes.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
kc_nets_t *ctx,
const char *host,
unsigned short port,
int proto,
const void *data,
size_t data_size,
void **out_data,
size_t *out_size
);

/**
 * Release response memory returned by nets. Accepts NULL.
 * @param ptr Response allocation to release, or NULL.
 * @return None.
 */
void kc_nets_free(void *ptr);

/**
 * Returns a static message for a nets status code.
 * @param code Status code.
 * @return Static message.
 */
const char *kc_nets_strerror(int code);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_nets_version(void);

/**
 * Check if TLS support is compiled in.
 * @return 1 if TLS is available, 0 otherwise.
 */
int kc_nets_tls_available(void);

#ifdef __cplusplus
}
#endif

#endif
