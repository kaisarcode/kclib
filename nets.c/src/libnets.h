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

typedef struct {
    int reserved;
} kc_nets_options_t;

/**
 * Returns default-initialized options.
 * @return Default-initialized options.
 */
kc_nets_options_t kc_nets_options_default(void);

/**
 * Loads environment variables into options.
 * @param opts Options to update.
 * @return None.
 */
void kc_nets_options_load_env(kc_nets_options_t *opts);

/**
 * Frees options resources.
 * @param opts Options to free.
 * @return None.
 */
void kc_nets_options_free(kc_nets_options_t *opts);

/**
 * Initialize a new nets context.
 * @param ctx_out Destination context pointer.
 * @param opts    Configuration options.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL on failure.
 */
int kc_nets_open(kc_nets_t **ctx_out, kc_nets_options_t *opts);

/**
 * Release a nets context.
 * @param ctx Context pointer.
 * @return KC_NETS_OK.
 */
int kc_nets_close(kc_nets_t *ctx);

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
int kc_nets_stop_requested(kc_nets_t *ctx);

/**
 * Parses a host, host:port, bracketed IPv6, or URL-shaped target.
 * URL schemes select transport defaults only.
 * @param text      Input target text.
 * @param host      Output host buffer.
 * @param host_cap  Output host capacity.
 * @param port      Output port pointer.
 * @param proto     Output protocol pointer.
 * @return 0 on success, or 1 on failure.
 */
int kc_nets_parse_target(
const char *text,
char *host,
size_t host_cap,
unsigned short *port,
int *proto
);

/**
 * Sends bytes to one network address and returns the response.
 * @param ctx      Context handle.
 * @param host     Destination host or IP address.
 * @param port     Destination port.
 * @param proto    KC_NETS_TCP, KC_NETS_UDP, or KC_NETS_TLS.
 * @param data     Buffer to send.
 * @param size     Buffer size in bytes.
 * @param out_data Receives malloc'd response bytes (caller frees).
 * @param out_size Receives response size.
 * @return KC_NETS_OK on success, or a negative error code.
 */
int kc_nets_send(
kc_nets_t *ctx,
const char *host,
unsigned short port,
int proto,
const void *data,
size_t size,
char **out_data,
size_t *out_size
);

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
