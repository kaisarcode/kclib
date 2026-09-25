/**
 * libtrust.h - Portable identity trust and message cryptography.
 * Summary: Public API for scoped trust relationships backed by Noise.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_TRUST_H
#define KC_TRUST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_trust kc_trust_t;

#define KC_TRUST_OK 0
#define KC_TRUST_ERROR -1

#define KC_TRUST_UID_SIZE 36
#define KC_TRUST_MAX_MESSAGE (64 * 1024 * 1024)

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_trust_version(void);

/**
 * Initialize the local trust store.
 *
 * The library resolves its per-user data directory automatically. If the
 * store does not exist it is created. KC_TRUST_DIR is an advanced
 * process-level override intended for tests and controlled deployments.
 *
 * @param out Destination context pointer.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_init(kc_trust_t **out);

/**
 * Create a one-use invitation for a new scoped trust relationship.
 *
 * Both returned strings are allocated by the library and released with
 * kc_trust_free(). The UID is the application-visible identifier for the
 * new scope. The code contains the cryptographic invitation and may be
 * transported by any out-of-band mechanism chosen by the application.
 *
 * @param trust Trust context.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @param out_code Destination for the allocated invitation code.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_invite(kc_trust_t *trust, char **out_uid, char **out_code);

/**
 * Join a trust relationship from an invitation code.
 *
 * The returned UID identifies the inviter in the local application. The
 * confirmation is an opaque portable string that must be delivered back to
 * the inviter by the application. Both strings are released with
 * kc_trust_free().
 *
 * @param trust Trust context.
 * @param code Invitation code obtained out of band.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @param out_confirmation Destination for the allocated confirmation string.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_join(kc_trust_t *trust, const char *code,
    char **out_uid, char **out_confirmation);

/**
 * Confirm a response to one pending invitation.
 *
 * A valid confirmation atomically replaces the pending invitation with the
 * established trust relationship and destroys the one-use invitation secret.
 * The returned UID is the application-visible scope identifier originally
 * returned by kc_trust_invite().
 *
 * @param trust Trust context.
 * @param confirmation Opaque confirmation received from the joining side.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_confirm(kc_trust_t *trust, const char *confirmation,
    char **out_uid);

/**
 * Encrypt and authenticate one message for an established UID.
 *
 * trust.c performs no transport, ordering, replay, retry, timeout, or request
 * lifecycle handling. The returned blob may be transported by any mechanism.
 *
 * @param trust Trust context.
 * @param uid Established peer UID.
 * @param message Plaintext bytes.
 * @param message_size Plaintext size.
 * @param out_data Destination for the allocated protected blob.
 * @param out_size Destination protected blob size.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_seal(kc_trust_t *trust, const char *uid,
    const void *message, size_t message_size,
    void **out_data, size_t *out_size);

/**
 * Authenticate and decrypt one message from an established UID.
 *
 * This operation intentionally has no replay or temporal policy. Replaying the
 * same valid blob is the responsibility of the composing transport/protocol.
 *
 * @param trust Trust context.
 * @param uid Established peer UID supplied by the surrounding application.
 * @param data Protected blob from kc_trust_seal().
 * @param data_size Protected blob size.
 * @param out_message Destination for the allocated plaintext.
 * @param out_message_size Destination plaintext size.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on authentication or input failure.
 */
int kc_trust_unseal(kc_trust_t *trust, const char *uid,
    const void *data, size_t data_size,
    void **out_message, size_t *out_message_size);

/**
 * Revoke an established or pending UID from the local trust store.
 * @param trust Trust context.
 * @param uid UID to revoke.
 * @return KC_TRUST_OK when something was removed, KC_TRUST_ERROR otherwise.
 */
int kc_trust_revoke(kc_trust_t *trust, const char *uid);

/**
 * Release a trust context.
 * @param trust Context pointer. NULL is a safe no-op.
 */
void kc_trust_close(kc_trust_t *trust);

/**
 * Release an allocation returned by trust.c.
 * @param ptr Allocation pointer. NULL is a safe no-op.
 */
void kc_trust_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
