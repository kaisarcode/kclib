/**
 * libnets.h - Asynchronous network transfer.
 * Summary: Public API for sending byte buffers over TCP, UDP, or optional TLS.
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
 * Receives the terminal result of one network transfer.
 * Response bytes are borrowed and remain valid only for the callback duration.
 * @param status   KC_NETS_OK or a negative status code.
 * @param data     Borrowed response bytes, or NULL when no response is present.
 * @param size     Response size in bytes.
 * @param userdata Caller-provided callback data.
 * @return None.
 */
typedef void (*kc_nets_handler_t)(
    int status,
    const void *data,
    size_t size,
    void *userdata
);

/**
 * Start one asynchronous network transfer.
 * The library copies host and data before returning. A successful launch causes
 * exactly one terminal callback.
 * @param out       Receives the transfer handle.
 * @param host      Destination host or IP address.
 * @param port      Destination port.
 * @param protocol  KC_NETS_TCP, KC_NETS_UDP, or KC_NETS_TLS.
 * @param data      Input bytes copied by the library.
 * @param data_size Input size in bytes.
 * @param handler   Terminal result callback.
 * @param userdata  Caller data passed unchanged to handler.
 * @return KC_NETS_OK when launched, or a negative status code.
 */
int kc_nets_send(
    kc_nets_t **out,
    const char *host,
    unsigned short port,
    int protocol,
    const void *data,
    size_t data_size,
    kc_nets_handler_t handler,
    void *userdata
);

/**
 * Request graceful interruption of one transfer.
 * @param nets Transfer handle.
 * @return KC_NETS_OK on success, KC_NETS_EINVAL for NULL.
 */
int kc_nets_stop(kc_nets_t *nets);

/**
 * Stop if necessary and release one transfer.
 * Safe to call from the transfer callback.
 * @param nets Transfer handle, or NULL.
 * @return None.
 */
void kc_nets_close(kc_nets_t *nets);

/**
 * Return a static message for a public status code.
 * @param code Status code.
 * @return Static message.
 */
const char *kc_nets_strerror(int code);

/**
 * Check whether TLS support is compiled in.
 * @return 1 when TLS is available, otherwise 0.
 */
int kc_nets_tls_available(void);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_nets_version(void);

#ifdef __cplusplus
}
#endif

#endif
