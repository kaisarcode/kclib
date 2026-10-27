/**
 * libnetl.h - Persistent bidirectional TCP/UDP socket interfaces.
 * Summary: Opens, maintains and operates bidirectional network endpoints.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_NETL_H
#define KC_NETL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_netl kc_netl_t;
typedef struct kc_netl_peer kc_netl_peer_t;

#define KC_NETL_OK 0
#define KC_NETL_EINVAL -1
#define KC_NETL_ENET -2
#define KC_NETL_ECLOSED -3
#define KC_NETL_ENOMEM -4

#define KC_NETL_TCP 1
#define KC_NETL_UDP 2

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const int *max_pending_connections;
} kc_netl_options_t;

/**
 * Receives bytes from one remote peer.
 * The peer and received bytes are borrowed for the
 * callback duration.
 * TCP peer identity remains stable across receives until disconnect or peer
 * close. A UDP peer represents one datagram origin and is valid only for the
 * callback duration.
 * @param peer Borrowed peer description.
 * @param data Borrowed received bytes.
 * @param data_size Byte count.
 * @param userdata Consumer data supplied when opening the listener.
 * @return void.
 */
typedef void (*kc_netl_receive_fn)(
    kc_netl_peer_t *peer,
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Receives notification that one TCP peer has closed.
 * The peer is borrowed and remains valid only for the callback duration. This
 * is the final callback in which that peer may be observed.
 * @param peer Closing TCP peer.
 * @param userdata Consumer data supplied when opening the listener.
 * @return void.
 */
typedef void (*kc_netl_disconnect_fn)(
    kc_netl_peer_t *peer,
    void *userdata
);

/**
 * Receives an asynchronous listener failure.
 * After this notification no further input is delivered.
 * @param status Public failure status.
 * @param userdata Consumer data supplied when opening the listener.
 * @return void.
 */
typedef void (*kc_netl_error_fn)(
    int status,
    void *userdata
);

/**
 * Owns one persistent bidirectional interface independently of TCP roles.
 * The configured address is attempted as a local bind; if it is not local,
 * it is retained as the outbound destination.
 */
typedef struct kc_netl_interface kc_netl_interface_t;

int kc_netl_interface_open(kc_netl_interface_t **out,
    const kc_netl_options_t *options,kc_netl_receive_fn receive,
    kc_netl_disconnect_fn disconnect,kc_netl_error_fn error,void *userdata);

int kc_netl_interface_send(kc_netl_interface_t *interface,
    const void *data,size_t size);

/**
 * Queue one UDP datagram to an explicit address using the interface's
 * existing socket and local port. The configured destination is unchanged.
 * @param interface Persistent UDP interface.
 * @param host Destination address.
 * @param port Destination port.
 * @param data Payload.
 * @param size Payload size.
 * @return Status code.
 */
int kc_netl_interface_send_to(kc_netl_interface_t *interface,
    const char *host,unsigned short port,const void *data,size_t size);

void kc_netl_interface_close(kc_netl_interface_t *interface);

/**
 * Opens an operational TCP or UDP listener.
 * NULL or empty host binds all interfaces. Port zero requests an ephemeral
 * operating-system-selected port. NULL max_pending_connections uses the
 * default. Receive is required; disconnect and error are optional. Receive may
 * respond, close peers, or close the listener.
 * @param out Destination listener pointer.
 * @param options Listener options.
 * @param receive Incoming byte callback.
 * @param disconnect Optional TCP disconnect callback.
 * @param error Optional asynchronous failure callback.
 * @param userdata Consumer data passed to callbacks.
 * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_open(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_receive_fn receive,
    kc_netl_disconnect_fn disconnect,
    kc_netl_error_fn error,
    void *userdata
);

/**
 * Opens one persistent outgoing network endpoint. The returned peer belongs to
 * the endpoint and remains usable until closed or disconnected. Both TCP and
 * UDP receive through the same peer,data callback as listener sockets.
 */
typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const char *local_host;
    unsigned short local_port;
} kc_netl_connect_options_t;

int kc_netl_connect(
    kc_netl_t **out,
    kc_netl_peer_t **peer,
    const kc_netl_connect_options_t *options,
    kc_netl_receive_fn receive,
    kc_netl_disconnect_fn disconnect,
    kc_netl_error_fn error,
    void *userdata
);

/**
 * Sends bytes to a peer.
 * The library copies the supplied bytes before returning and absorbs transport
 * buffering, partial writes, and retries. TCP responses preserve call order.
 * Send may be called repeatedly, from receive, after receive returns, or
 * from another thread while the TCP peer remains connected. Each UDP response
 * is one datagram to the receive-local peer.
 * @param peer Destination peer.
 * @param data Bytes to copy and send.
 * @param data_size Byte count.
 * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_send(
    kc_netl_peer_t *peer,
    const void *data,
    size_t data_size
);

/**
 * Returns the protocol of a peer received from a callback.
 * @param peer Borrowed peer.
 * @return KC_NETL_TCP, KC_NETL_UDP, or zero.
 */
int kc_netl_peer_protocol(const kc_netl_peer_t *peer);

/**
 * Returns the numeric remote host of a peer.
 * The pointer remains valid only while the peer remains valid.
 * @param peer Borrowed peer.
 * @return Numeric address or NULL.
 */
const char *kc_netl_peer_host(const kc_netl_peer_t *peer);

/**
 * Returns the remote port of a peer.
 * @param peer Borrowed peer.
 * @return Port, or zero.
 */
unsigned short kc_netl_peer_port(const kc_netl_peer_t *peer);

/**
 * Stops interacting with one peer.
 * For TCP, queued responses are sent in order before the peer is closed. For a
 * UDP peer this only prevents further response through that receive-local peer.
 * NULL and already-closed peers are accepted.
 * @param peer Peer to close, or NULL.
 * @return void.
 */
void kc_netl_peer_close(kc_netl_peer_t *peer);

/**
 * Returns the bound port, including an ephemeral port selected by the OS.
 * @param listener Listener, or NULL.
 * @return Bound port, or zero for an invalid listener.
 */
unsigned short kc_netl_port(const kc_netl_t *listener);

/**
 * Stops the listener, closes active TCP peers, and releases it.
 * NULL is accepted. This may also be called while netl is delivering a receive,
 * disconnect, or error callback.
 * @param listener Listener, or NULL.
 * @return void.
 */
void kc_netl_close(kc_netl_t *listener);

/**
 * Returns a static message for a public status code.
 * @param status Public status code.
 * @return Static error string.
 */
const char *kc_netl_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_netl_version(void);

#ifdef __cplusplus
}
#endif

#endif
