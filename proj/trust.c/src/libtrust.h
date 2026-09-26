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
 * Initializes the local trust store.
 *
 * The library resolves its per-user data directory automatically. If the
 * store does not exist, it is created. KC_TRUST_DIR is an advanced
 * process-level override for tests and controlled deployments.
 *
 * @param out Destination context pointer.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_init(kc_trust_t **out);

/**
 * Creates a one-use invitation for a scoped trust relationship.
 *
 * The returned UID identifies the invited endpoint for the inviter.
 * The code contains both endpoint UIDs and cryptographic invitation data.
 * The application may transport it by any out-of-band mechanism.
 *
 * @param trust Trust context.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @param out_code Destination for the allocated invitation code.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_invite(kc_trust_t *trust, char **out_uid, char **out_code);

/**
 * Joins a trust relationship from an invitation code.
 *
 * The returned UID identifies the inviter in the joining application.
 * It is distinct from the UID assigned to the joining endpoint.
 * The confirmation must be delivered back by the application.
 *
 * @param trust Trust context.
 * @param code Invitation code obtained out of band.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @param out_confirmation Destination allocated confirmation string.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_join(kc_trust_t *trust, const char *code,
    char **out_uid, char **out_confirmation);

/**
 * Confirms a response to one pending invitation.
 *
 * A valid confirmation establishes the trust relationship and destroys
 * the one-use invitation secret. The returned UID is the invited endpoint
 * UID originally produced by kc_trust_invite().
 *
 * @param trust Trust context.
 * @param confirmation Opaque confirmation from the joining endpoint.
 * @param out_uid Destination for the allocated canonical UUID string.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_confirm(kc_trust_t *trust, const char *confirmation,
    char **out_uid);

/**
 * Encrypts and authenticates one message for an established remote UID.
 *
 * trust.c performs no transport, ordering, replay, retry, timeout, or
 * request-lifecycle handling.
 *
 * @param trust Trust context.
 * @param uid Established remote UID.
 * @param message Plaintext bytes.
 * @param message_size Plaintext byte count.
 * @param out_data Destination for the allocated protected blob.
 * @param out_size Destination protected blob byte count.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_seal(kc_trust_t *trust, const char *uid,
    const void *message, size_t message_size,
    void **out_data, size_t *out_size);

/**
 * Authenticates and decrypts one message addressed to a local UID.
 *
 * This operation intentionally has no replay or temporal policy.
 *
 * @param trust Trust context.
 * @param uid Local destination UID from the application envelope.
 * @param data Protected blob from kc_trust_seal().
 * @param data_size Protected blob byte count.
 * @param out_message Destination for the allocated plaintext.
 * @param out_message_size Destination plaintext byte count.
 * @return KC_TRUST_OK on success, or KC_TRUST_ERROR on failure.
 */
int kc_trust_unseal(kc_trust_t *trust, const char *uid,
    const void *data, size_t data_size,
    void **out_message, size_t *out_message_size);

/**
 * Revokes an established remote UID or pending invitation.
 * @param trust Trust context.
 * @param uid Remote UID returned by invite, confirm, or join.
 * @return KC_TRUST_OK when removed, or KC_TRUST_ERROR otherwise.
 */
int kc_trust_revoke(kc_trust_t *trust, const char *uid);

/**
 * Releases a trust context.
 * @param trust Context pointer. NULL is a safe no-op.
 * @return No return value.
 */
void kc_trust_close(kc_trust_t *trust);

/**
 * Releases an allocation returned by trust.c.
 * @param ptr Allocation pointer. NULL is a safe no-op.
 * @return No return value.
 */
void kc_trust_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
