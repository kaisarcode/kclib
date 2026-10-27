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
 * Receives a UDP datagram and its numeric source address.
 * @param host Source IP address (borrowed for the callback).
 * @param port Source UDP port.
 * @param data Borrowed datagram bytes.
 * @param data_size Datagram byte count.
 * @param userdata User context.
 * @return void.
 */
typedef void (*kc_nets_receive_from_fn)(const char *host,
    unsigned short port,const void *data,size_t data_size,void *userdata);

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
} kc_nets_options_t;

/**
 * Extended UDP configuration; separate from kc_nets_options_t so existing
 * client initializers retain their ABI and initialization contract.
 */
typedef struct {
    kc_nets_options_t endpoint;
    const char *local_host;
    unsigned short local_port;
} kc_nets_udp_options_t;

/**
 * Receives bytes from one outbound network endpoint.
 * @param data Borrowed bytes valid for the callback duration.
 * @param data_size Received byte count.
 * @param userdata Consumer data supplied when opening the endpoint.
 * @return void.
 */
typedef void (*kc_nets_receive_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

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
 * and error are optional.
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
 * Opens a persistent UDP sender with configurable local bind and source-aware
 * receive. The default endpoint is used by kc_nets_send; send_to may target
 * other remote endpoints without creating another socket.
 * @param out New endpoint handle.
 * @param options UDP configuration with endpoint.protocol KC_NETS_UDP.
 * @param receive Source-aware UDP receive callback, separate from options.
 * @param error Optional asynchronous error callback.
 * @param userdata User context.
 * @return KC_NETS_OK or negative status.
 */
int kc_nets_open_udp(kc_nets_t **out,const kc_nets_udp_options_t *options,
    kc_nets_receive_from_fn receive,kc_nets_error_fn error,void *userdata);

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
 * @param host Destination hostname or numeric IP.
 * @param port Destination UDP port.
 * @param data Datagrams bytes (copied).
 * @param data_size Datagram length.
 * @return KC_NETS_OK or a negative status.
 */
int kc_nets_send_to(kc_nets_t *nets,const char *host,unsigned short port,
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
