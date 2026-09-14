/**
 * libtrust.h - Message cryptography with TOFU peer identity.
 * Summary: Public API for the trust library.
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

#define KC_TRUST_OK           0
#define KC_TRUST_ERROR       -1
#define KC_TRUST_PEER_NEW     1
#define KC_TRUST_PEER_CHANGED 2

#define KC_TRUST_PK_SIZE      32
#define KC_TRUST_SK_SIZE      32
#define KC_TRUST_IDENTITY_SIZE (KC_TRUST_SK_SIZE + KC_TRUST_PK_SIZE)
#define KC_TRUST_MAX_MESSAGE  (64 * 1024 * 1024)
#define KC_TRUST_MAX_PEER_ID  256
#define KC_TRUST_MAX_PAYLOAD  67125384

typedef struct {
    char *state_path;
    char *key_path;
} kc_trust_options_t;

typedef struct {
    int status;
    unsigned char peer_pk[KC_TRUST_PK_SIZE];
    unsigned char *message;
    size_t message_len;
} kc_trust_result_t;

/**
 * Return default options for the library.
 * @return Default options struct (caller-owned).
 */
kc_trust_options_t kc_trust_options_default(void);

/**
 * Load configuration overrides from environment variables.
 * Recognised variables: TRUST_STATE_DIR and TRUST_KEY.
 * @param opts Options to override.  NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_options_load_env(kc_trust_options_t *opts);

/**
 * Release resources owned by options struct.
 * @param opts Options to free.  NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_options_free(kc_trust_options_t *opts);

/**
 * Resolve the configured state directory or the platform default.
 * Uses LOCALAPPDATA/trust on Windows, with USERPROFILE/.trust as fallback, and
 * HOME/.trust on POSIX. The destination is not created.
 * @param opts Options containing an optional caller-owned state_path.
 * @param path Destination path buffer.
 * @param path_cap Destination buffer capacity.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_resolve_state_path(const kc_trust_options_t *opts,
char *path, size_t path_cap);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_trust_version(void);

/**
 * Generate a new random identity keypair and write it to disk.
 * Fails if the file already exists.
 * @param key_path Destination file path. NULL uses TRUST_KEY or an identity
 *                 file under the platform default state dir.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_generate(const char *key_path);

/**
 * Initialise a new trust context.
 * Loads the identity key from the path given in opts (or the default).
 * @param ctx_out Destination context pointer.
 * @param opts    Configuration options.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_create(kc_trust_t **ctx_out, kc_trust_options_t *opts);

/**
 * Release a trust context and wipe all sensitive material.
 * @param ctx Context pointer.  NULL is a safe no-op.
 * @return KC_TRUST_OK.
 */
int kc_trust_close(kc_trust_t *ctx);

/**
 * Return the context's 32-byte public key.
 * The returned pointer is owned by the context, remains valid until
 * kc_trust_close(), and must not be freed by the caller.
 * @param ctx Context pointer.
 * @return Pointer to 32 bytes, or NULL on error.
 */
const unsigned char *kc_trust_public_key(kc_trust_t *ctx);

/**
 * Protect an outgoing message.
 * @param ctx Context carrying the local identity.
 * @param recipient_pk 32-byte recipient public key.
 * @param message Application message bytes.
 * @param message_len Message length.
 * @param payload Destination pointer for the allocated encrypted payload.
 * @param payload_len Destination payload length.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_seal(kc_trust_t *ctx,
    const unsigned char recipient_pk[KC_TRUST_PK_SIZE],
    const unsigned char *message, size_t message_len,
    unsigned char **payload, size_t *payload_len);

/**
 * Authenticate and open an encrypted payload.
 * If peer_id is non-NULL, the trust store is evaluated against the
 * sender public key using the provided caller-defined identifier.
 * @param ctx Context carrying local identity and trust state.
 * @param payload Encrypted payload produced by kc_trust_seal.
 * @param payload_len Payload length.
 * @param peer_id Optional caller-defined peer identifier for trust evaluation.
 *                NULL skips trust evaluation and returns KC_TRUST_OK status.
 * @param peer_id_len Length of peer_id.  Must be > 0 when peer_id is
 *                    non-NULL and at most KC_TRUST_MAX_PEER_ID.
 * @param result Destination structured result, freed by kc_trust_result_free.
 * @return KC_TRUST_OK when the result is populated, KC_TRUST_ERROR on failure.
 */
int kc_trust_open(kc_trust_t *ctx,
    const unsigned char *payload, size_t payload_len,
    const unsigned char *peer_id, size_t peer_id_len,
    kc_trust_result_t **result);

/**
 * Release resources owned by a result.
 * @param result Result pointer.  NULL is a safe no-op.
 * @return Nothing.
 */
void kc_trust_result_free(kc_trust_result_t *result);

/**
 * Explicitly trust a peer binding.
 * Creates or replaces the binding for the given caller-defined
 * peer identifier in this context's in-memory trust state.  This function
 * performs no filesystem persistence.
 * @param ctx       Context.
 * @param peer_id   Opaque caller-defined peer identifier.
 * @param peer_id_len Identifier length (1 through KC_TRUST_MAX_PEER_ID).
 * @param peer_pk   32-byte public key to bind.
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR on failure.
 */
int kc_trust_trust(kc_trust_t *ctx,
    const unsigned char *peer_id, size_t peer_id_len,
    const unsigned char peer_pk[KC_TRUST_PK_SIZE]);

/**
 * Remove a peer trust binding.
 * Removes only this context's in-memory binding and performs no filesystem
 * persistence.
 * @param ctx       Context.
 * @param peer_id   Opaque caller-defined peer identifier.
 * @param peer_id_len Identifier length (1 through KC_TRUST_MAX_PEER_ID).
 * @return KC_TRUST_OK on success, KC_TRUST_ERROR if not found.
 */
int kc_trust_forget(kc_trust_t *ctx,
    const unsigned char *peer_id, size_t peer_id_len);

#ifdef __cplusplus
}
#endif

#endif
