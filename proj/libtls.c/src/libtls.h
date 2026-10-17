/**
 * libtls.h - Transport-independent TLS session.
 * Summary: Public API for caller-supplied TLS byte transport.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_TLS_H
#define KC_TLS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tls kc_tls_t;

#define KC_TLS_OK 0
#define KC_TLS_CLOSED 1
#define KC_TLS_EINVAL -1
#define KC_TLS_ETLS -2
#define KC_TLS_EVERIFY -3
#define KC_TLS_ENOMEM -4

#define KC_TLS_CLIENT 1
#define KC_TLS_SERVER 2

typedef struct {
    int role;
    const char *host;
    const void *ca;
    size_t ca_size;
    const void *certificate;
    size_t certificate_size;
    const void *private_key;
    size_t private_key_size;
} kc_tls_options_t;

/**
 * Receives authenticated plaintext produced by one TLS session.
 * @param data Borrowed plaintext bytes valid for the callback duration.
 * @param data_size Plaintext byte count.
 * @param userdata Consumer data supplied when opening the session.
 * @return void.
 */
typedef void (*kc_tls_plaintext_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Receives TLS wire bytes produced by one TLS session.
 * @param data Borrowed TLS wire bytes valid for the callback duration.
 * @param data_size TLS wire byte count.
 * @param userdata Consumer data supplied when opening the session.
 * @return void.
 */
typedef void (*kc_tls_ciphertext_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Opens one client or server TLS session.
 *
 * Client sessions require a non-empty host for SNI and hostname verification.
 * NULL ca selects the native trust store. Server sessions require a certificate
 * chain and private key. Certificate, key, and CA inputs may be PEM or DER.
 *
 * Opening a client session starts the TLS handshake and may invoke ciphertext
 * before returning. Server sessions wait for peer bytes supplied to receive.
 *
 * @param out Destination session pointer.
 * @param options TLS session options.
 * @param plaintext Authenticated plaintext callback.
 * @param ciphertext TLS wire output callback.
 * @param userdata Consumer data passed to callbacks.
 * @return KC_TLS_OK on success, otherwise a negative status.
 */
int kc_tls_open(
    kc_tls_t **out,
    const kc_tls_options_t *options,
    kc_tls_plaintext_fn plaintext,
    kc_tls_ciphertext_fn ciphertext,
    void *userdata
);

/**
 * Sends plaintext through one TLS session.
 *
 * The library copies the supplied bytes before returning. Data supplied before
 * the handshake completes is retained and sent after peer authentication.
 *
 * @param tls TLS session.
 * @param data Plaintext bytes.
 * @param data_size Plaintext byte count.
 * @return KC_TLS_OK, KC_TLS_CLOSED, or a negative status.
 */
int kc_tls_send(
    kc_tls_t *tls,
    const void *data,
    size_t data_size
);

/**
 * Supplies TLS wire bytes received from the underlying byte transport.
 *
 * One call may emit zero, one, or multiple plaintext or ciphertext callbacks.
 * Handshake, record processing, and protocol responses remain internal.
 *
 * @param tls TLS session.
 * @param data TLS wire bytes.
 * @param data_size TLS wire byte count.
 * @return KC_TLS_OK, KC_TLS_CLOSED, or a negative status.
 */
int kc_tls_receive(
    kc_tls_t *tls,
    const void *data,
    size_t data_size
);

/**
 * Closes and releases one TLS session.
 *
 * An established session emits close_notify through ciphertext before release.
 * NULL is accepted.
 *
 * @param tls TLS session, or NULL.
 * @return void.
 */
void kc_tls_close(kc_tls_t *tls);

/**
 * Returns a static message for one public status code.
 * @param status Public status code.
 * @return Static status message.
 */
const char *kc_tls_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_tls_version(void);

#ifdef __cplusplus
}
#endif

#endif
