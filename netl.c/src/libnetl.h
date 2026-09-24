/**
 * libnetl.h - Incoming network listener.
 * Summary: Public API for multiplexed TCP connections and UDP datagrams.
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
typedef struct kc_netl_connection kc_netl_connection_t;

#define KC_NETL_OK        0
#define KC_NETL_EINVAL   -1
#define KC_NETL_ENET     -2
#define KC_NETL_EAGAIN   -3
#define KC_NETL_ECLOSED  -4
#define KC_NETL_ENOMEM   -5

#define KC_NETL_TCP       1
#define KC_NETL_UDP       2

#define KC_NETL_EVENT_CONNECTION  1
#define KC_NETL_EVENT_DATA        2
#define KC_NETL_EVENT_CLOSE       3
#define KC_NETL_EVENT_DATAGRAM    4
#define KC_NETL_EVENT_WRITABLE    5

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    int backlog;
} kc_netl_options_t;

typedef struct {
    int type;
    kc_netl_connection_t *connection;
    const void *data;
    size_t data_size;
    const char *host;
    unsigned short port;
} kc_netl_event_t;

/**
 * Open one incoming TCP or UDP listener.
 * NULL or empty host binds all interfaces. Port zero requests an ephemeral
 * operating-system-selected port. A zero backlog selects the default.
 * @param out Receives the listener handle.
 * @param options Listener options.
 * @return KC_NETL_OK on success, or a negative status code.
 */
int kc_netl_open(
    kc_netl_t **out,
    const kc_netl_options_t *options
);

/**
 * Wait for one listener event.
 * WRITABLE is emitted only after a TCP send was partial or returned EAGAIN.
 * Event data, host text, and connection handles are borrowed. DATA and
 * DATAGRAM bytes remain valid only until the next kc_netl_poll call.
 * A connection delivered with CLOSE remains valid only until the next poll.
 * @param listener Listener handle.
 * @param event Receives one event.
 * @param timeout_ms Timeout in milliseconds. Zero returns immediately;
 *                    negative waits forever.
 * @return KC_NETL_OK for an event, KC_NETL_EAGAIN on timeout, or an error.
 */
int kc_netl_poll(
    kc_netl_t *listener,
    kc_netl_event_t *event,
    int timeout_ms
);

/**
 * Attempt a non-blocking send on one TCP connection.
 * A successful call may send fewer bytes than requested.
 * @param connection TCP connection.
 * @param data Bytes to send.
 * @param data_size Number of bytes requested.
 * @param out_sent Receives the number of bytes sent.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, KC_NETL_ECLOSED, or an error.
 */
int kc_netl_send(
    kc_netl_connection_t *connection,
    const void *data,
    size_t data_size,
    size_t *out_sent
);

/**
 * Attempt a non-blocking UDP send to one peer.
 * A UDP datagram is either sent in full or not sent.
 * @param listener UDP listener.
 * @param host Destination host or numeric address.
 * @param port Destination port.
 * @param data Datagram bytes.
 * @param data_size Datagram size.
 * @return KC_NETL_OK, KC_NETL_EAGAIN, or an error.
 */
int kc_netl_sendto(
    kc_netl_t *listener,
    const char *host,
    unsigned short port,
    const void *data,
    size_t data_size
);

/**
 * Close one TCP connection without closing its listener.
 * NULL and already-closed connections are accepted. The borrowed connection
 * handle remains valid until the next kc_netl_poll call or listener close.
 * @param connection Connection handle.
 * @return None.
 */
void kc_netl_connection_close(kc_netl_connection_t *connection);

/**
 * Return the bound port, including an ephemeral port selected by the OS.
 * @param listener Listener handle.
 * @return Bound port, or zero for an invalid listener.
 */
unsigned short kc_netl_port(const kc_netl_t *listener);

/**
 * Close all connections and release one listener.
 * NULL is accepted.
 * @param listener Listener handle.
 * @return None.
 */
void kc_netl_close(kc_netl_t *listener);

/**
 * Return a static message for a public status code.
 * @param status Status code.
 * @return Static error string.
 */
const char *kc_netl_strerror(int status);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_netl_version(void);

#ifdef __cplusplus
}
#endif

#endif
