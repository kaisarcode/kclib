/**
 * libredp2p.h - REDP2P.
 * Summary: Public API for publishing and consuming direct peer-to-peer port tunnels.
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
    uint16_t port;
    const char *pass;
    const char *stun;
} kc_redp2p_pub_options_t;

typedef struct {
    const char *id;
    const char *index;
    uint16_t port;
    const char *stun;
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
 * Publishes one local port through an index.
 *
 * Success means the publisher is registered and its runtime is active.
 *
 * @param out Destination publisher handle.
 * @param options Publisher identity, index, protocol, and local port.
 * @return KC_REDP2P_OK on success, otherwise a negative status.
 */
int kc_redp2p_pub(kc_redp2p_pub_t **out,
    const kc_redp2p_pub_options_t *options);

/**
 * Exposes one announced publisher through a local port.
 *
 * The publisher protocol is learned from the index. REDP2P only establishes
 * and maintains the tunnel; applications use the local port with any socket
 * library or process they choose.
 *
 * @param out Destination consumer handle.
 * @param options Target publisher, index, and local tunnel port.
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
 */
void kc_redp2p_idx_close(kc_redp2p_idx_t *idx);

/**
 * Stops publication, deregisters when possible, and releases the runtime.
 * NULL is a safe no-op.
 */
void kc_redp2p_pub_close(kc_redp2p_pub_t *pub);

/**
 * Closes the local tunnel and releases the consumer runtime.
 * NULL is a safe no-op.
 */
void kc_redp2p_con_close(kc_redp2p_con_t *con);

/**
 * Releases memory returned by REDP2P. NULL is a safe no-op.
 */
void kc_redp2p_free(void *ptr);

/**
 * Returns a stable static description for one REDP2P status.
 */
const char *kc_redp2p_strerror(int status);

/**
 * Returns the build version generated at compile time.
 */
uint64_t kc_redp2p_version(void);

#ifdef __cplusplus
}
#endif

#endif
