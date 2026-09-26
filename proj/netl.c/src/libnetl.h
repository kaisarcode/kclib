/**
 * libnetl.h - Incoming network listener.
 * Summary: Receives bytes from TCP/UDP peers and optionally responds.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
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

#define KC_NETL_OK        0
#define KC_NETL_EINVAL   -1
#define KC_NETL_ENET     -2
#define KC_NETL_ECLOSED  -3
#define KC_NETL_ENOMEM   -4

#define KC_NETL_TCP       1
#define KC_NETL_UDP       2

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const int *max_pending_connections;
} kc_netl_options_t;

typedef struct {
    kc_netl_peer_t *peer;
    int protocol;
    const char *host;
    unsigned short port;
    const void *data;
    size_t data_size;
} kc_netl_input_t;

/**
 * Receive bytes from one remote peer.
 * The input and its data/host values are borrowed for the callback duration.
 * TCP peer identity remains stable across callbacks until peer close.
 * A UDP peer represents the origin of this datagram and is valid only for the
 * callback duration.
 */
typedef void (*kc_netl_handler_t)(
    const kc_netl_input_t *input,
    void *userdata
);

/**
 * Receive notification that one TCP peer has closed.
 * The peer is borrowed and remains valid only for the callback duration.
 */
typedef void (*kc_netl_close_handler_t)(
    kc_netl_peer_t *peer,
    void *userdata
);

/**
 * Receive an asynchronous listener failure.
 * After this callback no further input is delivered.
 */
typedef void (*kc_netl_error_handler_t)(
    int status,
    void *userdata
);

/**
 * Open an operational TCP or UDP listener.
 * NULL or empty host binds all interfaces. Port zero requests an ephemeral
 * operating-system-selected port. NULL max_pending_connections uses the
 * default. The input handler is required; close/error handlers are
 * optional. Callbacks may respond, close peers, or close the listener.
 *
  * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_open(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_handler_t handler,
    kc_netl_close_handler_t close_handler,
    kc_netl_error_handler_t error_handler,
    void *userdata
);

/**
 * Respond to one peer.
 * The library copies the supplied bytes before returning and absorbs transport
 * buffering, partial writes, retries, and TCP/UDP-specific send mechanics.
  * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_respond(
    kc_netl_peer_t *peer,
    const void *data,
    size_t data_size
);

/**
 * Stop interacting with one peer.
 * For TCP this closes that peer without affecting other peers. For a UDP peer
 * this simply prevents further response through that callback-local peer.
 * NULL and already-closed peers are accepted.
  * @return None.
 */
void kc_netl_peer_close(kc_netl_peer_t *peer);

/**
 * Return the bound port, including an ephemeral port selected by the OS.
  * @return Bound port, or zero for an invalid listener.
 */
unsigned short kc_netl_port(const kc_netl_t *listener);

/**
 * Stop the listener, close all TCP peers, and release it.
 * NULL is accepted. This may also be called from a netl callback.
  * @return None.
 */
void kc_netl_close(kc_netl_t *listener);

/**
 * Return a static message for a public status code.
  * @return Static error string.
 */
const char *kc_netl_strerror(int status);

/**
 * Return the build version generated at compile time.
  * @return Build version.
 */
uint64_t kc_netl_version(void);

#ifdef __cplusplus
}
#endif

#endif
