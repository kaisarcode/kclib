/**
 * librdvz.h - Named peer rendezvous relationships.
 * Summary: Public API for resident peers and direct symmetric links.
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

typedef struct kc_rdvz_peer kc_rdvz_peer_t;

/**
 * Receives the identifier of the other peer in an established relationship.
 * The string is borrowed and remains valid only during the callback.
 * @param other Identifier of the other peer.
 * @param userdata Caller-supplied context.
 * @return None.
 */
typedef void (*kc_rdvz_handler_t)(const char *other, void *userdata);

/**
 * Visits one named peer. The identifier is borrowed for the visit.
 * @param id Peer identifier.
 * @param userdata Caller-supplied context.
 * @return None.
 */
typedef void (*kc_rdvz_visit_t)(const char *id, void *userdata);

typedef struct {
    const char *cmd;
    kc_rdvz_handler_t on_connect;
    void *userdata;
} kc_rdvz_options_t;

#define KC_RDVZ_OK        0
#define KC_RDVZ_ENOENT   -1
#define KC_RDVZ_EINVAL   -2
#define KC_RDVZ_ENOMEM   -3
#define KC_RDVZ_EEXIST  -4
#define KC_RDVZ_EIO     -5

/**
 * Create or update one named resident peer and open a local handle.
 * The optional command persists and runs with the other peer ID for every
 * newly created direct link, regardless of which side initiated it.
 * The optional callback belongs to this handle, not to the resident peer.
 * @param out Receives the local peer handle.
 * @param id Peer identifier.
 * @param options Optional resident command and local connect callback.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_create(kc_rdvz_peer_t **out, const char *id,
    const kc_rdvz_options_t *options);

/**
 * Open a temporary handle to an existing named peer.
 * The callback receives future connections to this peer until close.
 * @param out Receives the local peer handle.
 * @param id Existing peer identifier.
 * @param on_connect Optional local callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_open(kc_rdvz_peer_t **out, const char *id,
    kc_rdvz_handler_t on_connect, void *userdata);

/**
 * Get the identifier associated with a local peer handle.
 * @param peer Local peer handle.
 * @return Borrowed identifier, or NULL.
 */
const char *kc_rdvz_peer_id(const kc_rdvz_peer_t *peer);

/**
 * List every registered peer.
 * @param visit Visitor called for each peer.
 * @param userdata Visitor context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_list(kc_rdvz_visit_t visit, void *userdata);

/**
 * List direct neighbors of one named peer.
 * @param peer Local peer handle.
 * @param visit Visitor called for each directly related peer.
 * @param userdata Visitor context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_peer_list(const kc_rdvz_peer_t *peer,
    kc_rdvz_visit_t visit, void *userdata);

/**
 * Establish one symmetric link between two existing peers.
 * Connecting an existing link fails with KC_RDVZ_EEXIST.
 * The optional callback receives the other peer identifier.
 * @param peer One end of the relationship.
 * @param other Identifier of the other end.
 * @param callback Optional completion callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_connect(kc_rdvz_peer_t *peer, const char *other,
    kc_rdvz_handler_t callback, void *userdata);

/**
 * Remove a direct relationship without removing either peer.
 * Either endpoint can initiate the operation.
 * The optional callback receives the other peer identifier.
 * @param peer One end of the relationship.
 * @param other Identifier of the other end.
 * @param callback Optional completion callback.
 * @param userdata Callback context.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_peer_disconnect(kc_rdvz_peer_t *peer, const char *other,
    kc_rdvz_handler_t callback, void *userdata);

/**
 * Delete a named peer and all its direct relationships.
 * Existing local handles to this identifier become unusable.
 * @param id Identifier of the peer to remove.
 * @return KC_RDVZ_OK or a KC_RDVZ_* error.
 */
int kc_rdvz_disconnect(const char *id);

/**
 * Release a temporary local peer handle without deleting the resident peer.
 * @param peer Local handle, or NULL.
 * @return None.
 */
void kc_rdvz_close(kc_rdvz_peer_t *peer);

/**
 * Describe a library status code.
 * @param status Library status.
 * @return Static textual description.
 */
const char *kc_rdvz_strerror(int status);

/**
 * Get the generated build version.
 * @return Build version.
 */
uint64_t kc_rdvz_version(void);

#ifdef __cplusplus
}
#endif

#endif
