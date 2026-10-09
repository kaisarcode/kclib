/**
 * libredp2p.h - REDP2P.
 * Summary: Public API for publishing and consuming direct peer-to-peer transport.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef REDP2P_H
#define REDP2P_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct redp2p_idx redp2p_idx_t;
typedef struct redp2p_pub redp2p_pub_t;
typedef struct redp2p_con redp2p_con_t;
typedef struct redp2p_client redp2p_client_t;

typedef struct {
    redp2p_client_t *client;
    const void *data;
    size_t size;
} redp2p_pub_input_t;

typedef void (*redp2p_pub_connect_fn)(
    redp2p_client_t *client, void *userdata);

typedef void (*redp2p_pub_receive_fn)(
    const redp2p_pub_input_t *input, void *userdata);
typedef void (*redp2p_con_receive_fn)(
    const void *data, size_t size, void *userdata);

/**
 * Reports that the peer will send no more bytes on one TCP stream direction.
 * The local side may continue sending until it shuts down its own direction.
 */
typedef void (*redp2p_pub_peer_shutdown_fn)(
    redp2p_client_t *client, void *userdata);
typedef void (*redp2p_con_peer_shutdown_fn)(void *userdata);

#define REDP2P_OK          0
#define REDP2P_ERROR      -1
#define REDP2P_ENET       -2
#define REDP2P_ENOENT     -3
#define REDP2P_ETIMEOUT   -4
#define REDP2P_EFULL      -5
#define REDP2P_EINVAL     -6
#define REDP2P_EPROTO     -7
#define REDP2P_EAUTH      -8
#define REDP2P_EPUNCH    -10
#define REDP2P_EEXIST    -11
#define REDP2P_EUNSUPPORTED -12

#define REDP2P_ID_MAX 63
#define REDP2P_PORT_DEFAULT 9876

#define REDP2P_TCP 1
#define REDP2P_UDP 2

#define REDP2P_OPTION_STUN 1
#define REDP2P_OPTION_TURN 2

typedef struct {
    const char *id;
    const char *pass;
} redp2p_vip_t;

typedef struct {
    const char *host;
    uint16_t port;
    const size_t *seats;
    unsigned int pow;
    const char *pass;
    const redp2p_vip_t *vips;
    size_t vip_count;
    size_t max_consumers;
} redp2p_idx_options_t;

typedef struct {
    const char *id;
    const char *index;
    int protocol;
    const char *pass;
    const char *stun;
    redp2p_pub_connect_fn connect;
    redp2p_pub_receive_fn receive;
    void *userdata;
    const char *turn;
    const char *turn_user;
    const char *turn_pass;
    redp2p_pub_peer_shutdown_fn peer_shutdown;
} redp2p_pub_options_t;

typedef struct {
    const char *id;
    const char *index;
    const char *stun;
    redp2p_con_receive_fn receive;
    void *userdata;
    const char *turn;
    const char *turn_user;
    const char *turn_pass;
    redp2p_con_peer_shutdown_fn peer_shutdown;
} redp2p_con_options_t;

typedef struct {
    char id[REDP2P_ID_MAX + 1];
} redp2p_idx_entry_t;

/**
 * Starts an index runtime.
 *
 * Success means the index listener is ready. The returned handle owns its
 * runtime until redp2p_idx_close().
 *
 * @param out Destination index handle.
 * @param options Index policy and listener configuration.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_idx(redp2p_idx_t **out,
    const redp2p_idx_options_t *options);

/**
 * Publishes one native TCP or UDP peer capability through an index.
 *
 * REDP2P owns any transport-specific native adapters internally. Applications
 * observe established peer channels through the optional connect function,
 * receive application bytes through the optional receive function, and reply
 * with redp2p_client_respond().
 *
 * Success means the publisher is registered and ready to accept peer channels.
 *
 * @param out Destination publisher handle.
 * Optional STUN/TURN fields affect only internal path establishment; the
 * established application channel and callbacks are unchanged.
 *
 * @param options Publisher identity, index, protocol, relay settings, and
 * callbacks.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_pub(redp2p_pub_t **out,
    const redp2p_pub_options_t *options);

/**
 * Connects to one announced publisher.
 *
 * The publisher transport is learned from the index and remains an internal
 * detail. Success means one peer channel is established and
 * redp2p_con_send() may be used immediately. The optional receive function
 * delivers application bytes arriving from that peer.
 *
 * @param out Destination consumer handle.
 * Optional STUN/TURN fields affect only internal path establishment; the
 * returned application channel is unchanged.
 *
 * @param options Target publisher, index, relay settings, and receive callback.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_con(redp2p_con_t **out,
    const redp2p_con_options_t *options);

/**
 * Updates one supported publisher option without replacing the live handle.
 *
 * The STUN selector reads the stun field. The TURN selector reads the turn,
 * turn_user, and turn_pass fields as one relay configuration. TURN credential
 * rotation preserves an allocation on the same TURN endpoint. Changing the
 * TURN endpoint while an allocation is active returns REDP2P_EUNSUPPORTED.
 * The selected endpoint must be non-NULL. Other option values currently return
 * REDP2P_EUNSUPPORTED.
 *
 * @param pub Live publisher handle.
 * @param option REDP2P_OPTION_* selector.
 * @param options Values for the selected option.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_pub_set(redp2p_pub_t *pub, int option,
    const redp2p_pub_options_t *options);

/**
 * Updates one supported consumer option without replacing the live handle.
 *
 * The STUN selector reads the stun field. The TURN selector reads the turn,
 * turn_user, and turn_pass fields as one relay configuration. TURN credential
 * rotation preserves an allocation on the same TURN endpoint. Changing the
 * TURN endpoint while an allocation is active returns REDP2P_EUNSUPPORTED.
 * The selected endpoint must be non-NULL. Other option values currently return
 * REDP2P_EUNSUPPORTED.
 *
 * @param con Live consumer handle.
 * @param option REDP2P_OPTION_* selector.
 * @param options Values for the selected option.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_con_set(redp2p_con_t *con, int option,
    const redp2p_con_options_t *options);

/**
 * Returns a snapshot of fresh publisher identifiers known by one live index.
 *
 * The returned array is owned by the caller and must be released with
 * redp2p_free(). An empty index returns NULL with count zero.
 *
 * @param idx Live index handle.
 * @param out_entries Destination array pointer.
 * @param out_count Destination entry count.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_idx_list(redp2p_idx_t *idx,
    redp2p_idx_entry_t **out_entries, size_t *out_count);

/**
 * Stops and releases an index runtime. NULL is a safe no-op.
 * @param idx Index runtime.
 * @return None.
 */
void redp2p_idx_close(redp2p_idx_t *idx);

/**
 * Stops publication, deregisters when possible, and releases the runtime.
 * NULL is a safe no-op.
 * @param pub Publisher runtime.
 * @return None.
 */
void redp2p_pub_close(redp2p_pub_t *pub);

/**
 * Closes the peer channel and releases the consumer runtime.
 * NULL is a safe no-op.
 * @param con Consumer runtime.
 * @return None.
 */
void redp2p_con_close(redp2p_con_t *con);

/**
 * Sends application data through an established consumer channel.
 *
 * TCP preserves stream semantics. UDP sends one datagram per call.
 *
 * @param con Consumer capability returned by redp2p_con().
 * @param data Bytes to send.
 * @param size Byte count.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_con_send(redp2p_con_t *con,
    const void *data, size_t size);

/**
 * Ends the local send direction of one TCP consumer stream.
 *
 * Already-sent bytes are preserved. The peer may continue sending data until
 * it independently shuts down its direction. This does not destroy the
 * consumer handle.
 *
 * @param con Consumer capability returned by redp2p_con().
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_con_shutdown(redp2p_con_t *con);

/**
 * Responds to one publisher client.
 *
 * TCP writes bytes to the client stream. UDP sends one datagram per call.
 *
 * @param client Publisher client received with redp2p_pub_input_t.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_client_respond(redp2p_client_t *client,
    const void *data, size_t size);

/**
 * Ends the local send direction of one TCP publisher client.
 *
 * Already-sent bytes are preserved. The peer may continue sending data until
 * it independently shuts down its direction. This does not close the client.
 *
 * @param client Publisher client received with redp2p_pub_input_t.
 * @return REDP2P_OK on success, otherwise a negative status.
 */
int redp2p_client_shutdown(redp2p_client_t *client);

/**
 * Closes one publisher client. NULL is a safe no-op.
 * @param client Publisher client.
 * @return None.
 */
void redp2p_client_close(redp2p_client_t *client);

/**
 * Releases memory returned by REDP2P. NULL is a safe no-op.
 * @param ptr Memory returned by REDP2P.
 * @return None.
 */
void redp2p_free(void *ptr);

/**
 * Returns a stable static description for one REDP2P status.
 * @param status REDP2P status code.
 * @return Stable static status description.
 */
const char *redp2p_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version value.
 */
uint64_t redp2p_version(void);

#ifdef __cplusplus
}
#endif

#endif
