/**
 * libnets.h - Persistent outbound network transport.
 * Summary: Public API for exchanging raw bytes over TCP or UDP.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_NETS_H
#define KC_NETS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_nets kc_nets_t;

#define KC_NETS_OK 0
#define KC_NETS_EINVAL -1
#define KC_NETS_ENET -2
#define KC_NETS_ECLOSED -3
#define KC_NETS_ENOMEM -4

#define KC_NETS_TCP 1
#define KC_NETS_UDP 2

/**
 * Describes a remote network endpoint.
 * Received UDP source addresses are numeric and borrowed for the callback.
 */
typedef struct {
    const char *host;
    unsigned short port;
} kc_nets_endpoint_t;

/**
 * Configures a persistent outbound connection. For UDP, local_host and
 * local_port optionally select the socket's source interface and port.
 * A zero local_port selects an ephemeral port. TCP ignores these two fields.
 */
typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const char *local_host;
    unsigned short local_port;
} kc_nets_options_t;

/**
 * Receives bytes from a persistent TCP or UDP transport.
 * For UDP, peer identifies the datagram source and is borrowed only for the
 * callback. For TCP, peer is NULL and data is an arbitrary stream chunk.
 * @param data Borrowed input bytes.
 * @param data_size Byte count.
 * @param peer Source endpoint for UDP, NULL for TCP.
 * @param userdata User context.
 * @return void.
 */
typedef void (*kc_nets_receive_fn)(const void *data,size_t data_size,
    const kc_nets_endpoint_t *peer,void *userdata);

/**
 * Receives notification that one TCP peer disconnected.
 * This callback is not used for UDP endpoints.
 * @param userdata Consumer data supplied when opening the endpoint.
 * @return void.
 */
typedef void (*kc_nets_disconnect_fn)(void *userdata);

/**
 * Receives an asynchronous network failure.
 * After this callback no further network input is delivered.
 * @param status Public failure status.
 * @param userdata Consumer data supplied when opening the endpoint.
 * @return void.
 */
typedef void (*kc_nets_error_fn)(int status, void *userdata);

/**
 * Opens one persistent outbound TCP or UDP endpoint.
 *
 * Host must be non-empty and port must be non-zero. Open resolves the host and
 * establishes the endpoint before returning. Receive is required. Disconnect
 * and error are optional. UDP may send to multiple destinations and receives
 * each datagram with its numeric source endpoint.
 *
 * @param out Destination endpoint pointer.
 * @param options Outbound endpoint options.
 * @param receive Incoming byte callback.
 * @param disconnect Optional TCP disconnect callback.
 * @param error Optional asynchronous failure callback.
 * @param userdata Consumer data passed to callbacks.
 * @return KC_NETS_OK on success, otherwise a negative status.
 */
int kc_nets_open(
    kc_nets_t **out,
    const kc_nets_options_t *options,
    kc_nets_receive_fn receive,
    kc_nets_disconnect_fn disconnect,
    kc_nets_error_fn error,
    void *userdata
);

/**
 * Sends bytes through one open endpoint.
 *
 * The library copies the supplied bytes before returning. TCP sends preserve
 * stream order. Each UDP send remains one datagram. This function may be called
 * from callbacks or other threads while the endpoint remains open.
 *
 * @param nets Open endpoint.
 * @param data Bytes to send.
 * @param data_size Byte count.
 * @return KC_NETS_OK on success, otherwise a negative status.
 */
int kc_nets_send(
    kc_nets_t *nets,
    const void *data,
    size_t data_size
);

/**
 * Queues one UDP datagram to an arbitrary destination using the same socket.
 * @param nets Open UDP sender.
 * @param peer Destination UDP endpoint.
 * @param data Datagram bytes (copied).
 * @param data_size Datagram length.
 * @return KC_NETS_OK or a negative status.
 */
int kc_nets_send_to(kc_nets_t *nets,const kc_nets_endpoint_t *peer,
    const void *data,size_t data_size);

/**
 * Reads the UDP socket's locally bound port.
 * @param nets Open endpoint.
 * @return Local port, or zero when unavailable.
 */
unsigned short kc_nets_local_port(const kc_nets_t *nets);

/**
 * Closes and releases one outbound endpoint.
 *
 * NULL is accepted. This function may be called from a callback. In that case
 * release completes after the callback returns. No callback is delivered after
 * an external close call returns.
 *
 * @param nets Endpoint, or NULL.
 * @return void.
 */
void kc_nets_close(kc_nets_t *nets);

/**
 * Returns a static message for one public status code.
 * @param status Public status code.
 * @return Static status message.
 */
const char *kc_nets_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_nets_version(void);

#ifdef __cplusplus
}
#endif

#endif
