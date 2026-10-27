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

typedef struct kc_netl_interface kc_netl_t;
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
 * Open an address-oriented TCP/UDP interface without selecting a role.
 * @param out Interface handle.
 * @param options Configured address and transport.
 * @param receive Incoming data callback.
 * @param disconnect Optional TCP disconnect callback.
 * @param error Optional asynchronous error callback.
 * @param userdata Callback context.
 * @return Status code.
 */
int kc_netl_open(kc_netl_t **out,const kc_netl_options_t *options,
    kc_netl_receive_fn receive,kc_netl_disconnect_fn disconnect,
    kc_netl_error_fn error,void *userdata);

/**
 * Optional destination for an individual UDP datagram.
 */
typedef struct {
    const char *host;
    unsigned short port;
} kc_netl_endpoint_t;

/**
 * Send using a persistent interface. NULL destination uses the configured
 * address. An explicit UDP destination leaves the local socket unchanged.
 * @param interface Interface handle.
 * @param data Bytes to send.
 * @param size Byte count.
 * @param destination Optional remote endpoint, or NULL.
 * @return Status code.
 */
int kc_netl_send(kc_netl_t *interface,const void *data,size_t size,
    const kc_netl_endpoint_t *destination);

/**
 * Send to the peer supplied by the receive callback.
 * For UDP the peer is valid only during the callback.
 * @param peer Incoming peer.
 * @param data Data bytes.
 * @param size Byte count.
 * @return Status code.
 */
int kc_netl_peer_send(kc_netl_peer_t *peer,const void *data,size_t size);

/**
 * Close an interface and release its sockets.
 * @param interface Interface handle, or NULL.
 */
void kc_netl_close(kc_netl_t *interface);



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
