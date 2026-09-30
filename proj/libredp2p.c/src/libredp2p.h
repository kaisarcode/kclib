/**
 * libredp2p.h - REDP2P.
 * Summary: Public API for publishing and consuming direct peer-to-peer transport.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_REDP2P_H
#define KC_REDP2P_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_redp2p_idx kc_redp2p_idx_t;
typedef struct kc_redp2p_pub kc_redp2p_pub_t;
typedef struct kc_redp2p_con kc_redp2p_con_t;
typedef struct kc_redp2p_client kc_redp2p_client_t;

typedef struct {
    kc_redp2p_client_t *client;
    const void *data;
    size_t size;
} kc_redp2p_pub_input_t;

typedef void (*kc_redp2p_pub_connect_fn)(
    kc_redp2p_client_t *client, void *userdata);

typedef void (*kc_redp2p_pub_receive_fn)(
    const kc_redp2p_pub_input_t *input, void *userdata);
typedef void (*kc_redp2p_con_receive_fn)(
    const void *data, size_t size, void *userdata);

#define KC_REDP2P_OK          0
#define KC_REDP2P_ERROR      -1
#define KC_REDP2P_ENET       -2
#define KC_REDP2P_ENOENT     -3
#define KC_REDP2P_ETIMEOUT   -4
#define KC_REDP2P_EFULL      -5
#define KC_REDP2P_EINVAL     -6
#define KC_REDP2P_EPROTO     -7
#define KC_REDP2P_EAUTH      -8
#define KC_REDP2P_EVERSION   -9
#define KC_REDP2P_EPUNCH    -10
#define KC_REDP2P_EEXIST    -11
#define KC_REDP2P_EUNSUPPORTED -12

#define KC_REDP2P_ID_MAX 63
#define KC_REDP2P_PORT_DEFAULT 9876

#define KC_REDP2P_TCP 1
#define KC_REDP2P_UDP 2

typedef struct {
    const char *id;
    const char *pass;
} kc_redp2p_vip_t;

typedef struct {
    const char *host;
    uint16_t port;
    const size_t *seats;
    unsigned int pow;
    const char *pass;
    const kc_redp2p_vip_t *vips;
    size_t vip_count;
    size_t max_consumers;
} kc_redp2p_idx_options_t;

typedef struct {
    const char *id;
    const char *index;
    int protocol;
    const char *pass;
    const char *stun;
    kc_redp2p_pub_connect_fn connect;
    kc_redp2p_pub_receive_fn receive;
    void *userdata;
    const char *turn;
    const char *turn_user;
    const char *turn_pass;
} kc_redp2p_pub_options_t;

typedef struct {
    const char *id;
    const char *index;
    const char *stun;
    kc_redp2p_con_receive_fn receive;
    void *userdata;
    const char *turn;
    const char *turn_user;
    const char *turn_pass;
} kc_redp2p_con_options_t;

typedef struct {
    char id[KC_REDP2P_ID_MAX + 1];
} kc_redp2p_idx_entry_t;

/**
 * Starts an index runtime.
 *
 * Success means the index listener is ready. The returned handle owns its
 * runtime until kc_redp2p_idx_close().
 *
 * @param out Destination index handle.
 * @param options Index policy and listener configuration.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_idx(kc_redp2p_idx_t **out,
    const kc_redp2p_idx_options_t *options);

/**
 * Publishes one native TCP or UDP peer capability through an index.
 *
 * REDP2P owns any transport-specific native adapters internally. Applications
 * observe established peer channels through the optional connect function,
 * receive application bytes through the optional receive function, and reply
 * with kc_redp2p_client_respond().
 *
 * Success means the publisher is registered and ready to accept peer channels.
 *
 * @param out Destination publisher handle.
 * Optional STUN/TURN fields affect only internal path establishment; the
 * established application channel and callbacks are unchanged.
 *
 * @param options Publisher identity, index, protocol, relay settings, and
 * callbacks.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options);

/**
 * Connects to one announced publisher.
 *
 * The publisher transport is learned from the index and remains an internal
 * detail. Success means one peer channel is established and
 * kc_redp2p_con_send() may be used immediately. The optional receive function
 * delivers application bytes arriving from that peer.
 *
 * @param out Destination consumer handle.
 * Optional STUN/TURN fields affect only internal path establishment; the
 * returned application channel is unchanged.
 *
 * @param options Target publisher, index, relay settings, and receive callback.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_con(kc_redp2p_con_t **out,
    const kc_redp2p_con_options_t *options);

/**
 * Returns a snapshot of fresh publisher identifiers known by one live index.
 *
 * The returned array is owned by the caller and must be released with
 * kc_redp2p_free(). An empty index returns NULL with count zero.
 *
 * @param idx Live index handle.
 * @param out_entries Destination array pointer.
 * @param out_count Destination entry count.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_idx_list(kc_redp2p_idx_t *idx,
    kc_redp2p_idx_entry_t **out_entries, size_t *out_count);

/**
 * Stops and releases an index runtime. NULL is a safe no-op.
 * @param idx Index runtime.
 * @return None.
 */
void kc_redp2p_idx_close(kc_redp2p_idx_t *idx);

/**
 * Stops publication, deregisters when possible, and releases the runtime.
 * NULL is a safe no-op.
 * @param pub Publisher runtime.
 * @return None.
 */
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub);

/**
 * Closes the peer channel and releases the consumer runtime.
 * NULL is a safe no-op.
 * @param con Consumer runtime.
 * @return None.
 */
void kc_redp2p_con_close(kc_redp2p_con_t *con);

/**
 * Sends application data through an established consumer channel.
 *
 * TCP preserves stream semantics. UDP sends one datagram per call.
 *
 * @param con Consumer capability returned by kc_redp2p_con().
 * @param data Bytes to send.
 * @param size Byte count.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_con_send(kc_redp2p_con_t *con,
    const void *data, size_t size);

/**
 * Responds to one publisher client.
 *
 * TCP writes bytes to the client stream. UDP sends one datagram per call.
 *
 * @param client Publisher client received with kc_redp2p_pub_input_t.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_client_respond(kc_redp2p_client_t *client,
    const void *data, size_t size);

/**
 * Closes one publisher client. NULL is a safe no-op.
 * @param client Publisher client.
 * @return None.
 */
void kc_redp2p_client_close(kc_redp2p_client_t *client);

/**
 * Releases memory returned by REDP2P. NULL is a safe no-op.
 * @param ptr Memory returned by REDP2P.
 * @return None.
 */
void kc_redp2p_free(void *ptr);

/**
 * Returns a stable static description for one REDP2P status.
 * @param status REDP2P status code.
 * @return Stable static status description.
 */
const char *kc_redp2p_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version value.
 */
uint64_t kc_redp2p_version(void);

#ifdef __cplusplus
}
#endif

#endif
