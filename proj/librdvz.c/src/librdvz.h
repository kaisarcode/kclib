/**
 * librdvz.h - Generic peer rendezvous relationships.
 * Summary: Public API for independent peers and their direct links.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_RDVZ_H
#define KC_RDVZ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_rdvz kc_rdvz_t;
typedef struct kc_rdvz_peer kc_rdvz_peer_t;

#define KC_RDVZ_OK 0
#define KC_RDVZ_ENOENT -1
#define KC_RDVZ_EINVAL -2
#define KC_RDVZ_ENOMEM -3
#define KC_RDVZ_EBUSY -4
#define KC_RDVZ_ELIMIT -5

/**
 * Observes one created or removed direct link.
 * Both peers are valid during the notification. Callbacks may inspect the
 * registry but must not mutate or close it; mutations return KC_RDVZ_EBUSY.
 * @param peer Peer initiating connection or disconnection.
 * @param other Peer at the opposite end of the link.
 * @param userdata Application context supplied to open.
 * @return None.
 */
typedef void (*kc_rdvz_link_fn)(const kc_rdvz_peer_t *peer,
    const kc_rdvz_peer_t *other, void *userdata);

/**
 * Visits one peer during a list operation.
 * The registry cannot be mutated or closed during iteration.
 * @param peer Borrowed peer reference, valid until disconnection or close.
 * @param userdata Context supplied to the list operation.
 * @return None.
 */
typedef void (*kc_rdvz_visit_fn)(const kc_rdvz_peer_t *peer, void *userdata);

/**
 * Optional rendezvous notifications.
 * Notifications are synchronous, occur once per created or removed direct
 * link, and do not imply transport, message delivery or peer authentication.
 */
typedef struct {
    kc_rdvz_link_fn on_connect;
    kc_rdvz_link_fn on_disconnect;
    void *userdata;
} kc_rdvz_options_t;

/**
 * Opens an independent in-memory registry.
 * @param out Destination for the owned registry.
 * @param options Optional notification callbacks and application context.
 * @return KC_RDVZ_OK, KC_RDVZ_EINVAL, or KC_RDVZ_ENOMEM.
 */
int kc_rdvz_open(kc_rdvz_t **out, const kc_rdvz_options_t *options);

/**
 * Creates a new peer and optionally links it to an existing peer.
 * Passing NULL as target_id creates an independent peer. Every successful
 * call creates a fresh, registry-unique ID; the supplied ID identifies the
 * target, not the new peer. A link is symmetric and never creates transitive
 * links. On success, on_connect runs once if a link was created.
 * @param rdvz Open registry.
 * @param target_id Existing peer ID, or NULL for an independent peer.
 * @param out Destination for the owned peer handle.
 * @return KC_RDVZ_OK, KC_RDVZ_ENOENT, KC_RDVZ_EINVAL,
 *         KC_RDVZ_ENOMEM, KC_RDVZ_EBUSY, or KC_RDVZ_ELIMIT.
 */
int kc_rdvz_connect(kc_rdvz_t *rdvz, const char *target_id,
    kc_rdvz_peer_t **out);

/**
 * Returns the stable ID of a connected peer.
 * The string remains valid until the peer disconnects or the registry closes.
 * @param peer Live peer.
 * @return Borrowed ID or NULL for an invalid handle.
 */
const char *kc_rdvz_peer_id(const kc_rdvz_peer_t *peer);

/**
 * Visits every live peer in registry insertion order.
 * @param rdvz Open registry.
 * @param visit Required visitor invoked once for each peer.
 * @param userdata Caller context.
 * @return KC_RDVZ_OK or KC_RDVZ_EINVAL.
 */
int kc_rdvz_list(kc_rdvz_t *rdvz, kc_rdvz_visit_fn visit, void *userdata);

/**
 * Visits the direct neighbors of one peer in connection order.
 * Does not traverse indirect links.
 * @param peer Live peer.
 * @param visit Required visitor invoked for each directly linked peer.
 * @param userdata Caller context.
 * @return KC_RDVZ_OK or KC_RDVZ_EINVAL.
 */
int kc_rdvz_peer_list(const kc_rdvz_peer_t *peer, kc_rdvz_visit_fn visit,
    void *userdata);

/**
 * Removes a peer and all its direct links, retaining every other peer.
 * on_disconnect runs once per removed link after that link is detached.
 * The peer handle and its ID become invalid when this function returns.
 * @param peer Live peer to disconnect.
 * @return KC_RDVZ_OK, KC_RDVZ_EINVAL, or KC_RDVZ_EBUSY.
 */
int kc_rdvz_disconnect(kc_rdvz_peer_t *peer);

/**
 * Closes the registry and discards all peers and links.
 * Closing does not emit disconnect notifications.
 * @param rdvz Open registry; NULL is a successful no-op.
 * @return KC_RDVZ_OK or KC_RDVZ_EBUSY when invoked from a callback.
 */
int kc_rdvz_close(kc_rdvz_t *rdvz);

/**
 * Returns a stable textual description for one status code.
 * @param status Public status code.
 * @return Static NUL-terminated description.
 */
const char *kc_rdvz_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_rdvz_version(void);

#ifdef __cplusplus
}
#endif

#endif
