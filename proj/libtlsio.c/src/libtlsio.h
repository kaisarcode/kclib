/**
 * libtlsio.h - Transport-independent TLS session.
 * Summary: Public API for client and server TLS over caller-supplied byte transport.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_TLSIO_H
#define KC_TLSIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tlsio kc_tlsio_t;

#define KC_TLSIO_OK        0
#define KC_TLSIO_CLOSED    1
#define KC_TLSIO_EINVAL   -1
#define KC_TLSIO_ETLS     -2
#define KC_TLSIO_EVERIFY  -3
#define KC_TLSIO_ENOMEM   -4

#define KC_TLSIO_CLIENT    1
#define KC_TLSIO_SERVER    2

typedef struct {
    int role;
    const char *host;
    const void *ca;
    size_t ca_size;
    const void *certificate;
    size_t certificate_size;
    const void *private_key;
    size_t private_key_size;
} kc_tlsio_options_t;

/**
 * Receives authenticated plaintext produced by one TLS session.
 *
 * The supplied bytes are borrowed and remain valid only for the callback
 * duration.
 */
typedef void (*kc_tlsio_plaintext_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Receives TLS wire bytes that must be passed to the underlying byte transport.
 *
 * The supplied bytes are borrowed and remain valid only for the callback
 * duration.
 */
typedef void (*kc_tlsio_ciphertext_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Open one client or server TLS session.
 *
 * Client sessions require a non-empty host for SNI and hostname verification.
 * NULL ca selects the native trust store. Server sessions require a certificate
 * chain and private key. Certificate, key, and CA inputs may be PEM or DER.
 *
 * Opening a client session starts the TLS handshake and may invoke ciphertext
 * before returning. Server sessions wait for peer bytes supplied to receive.
 *
 * @return KC_TLSIO_OK on success, otherwise a negative status.
 */
int kc_tlsio_open(
    kc_tlsio_t **out,
    const kc_tlsio_options_t *options,
    kc_tlsio_plaintext_fn plaintext,
    kc_tlsio_ciphertext_fn ciphertext,
    void *userdata
);

/**
 * Send plaintext through one TLS session.
 *
 * The library copies the supplied bytes before returning. Data supplied before
 * the handshake completes is retained and sent automatically once the peer is
 * authenticated.
 *
 * @return KC_TLSIO_OK, KC_TLSIO_CLOSED, or a negative status.
 */
int kc_tlsio_send(
    kc_tlsio_t *tls,
    const void *data,
    size_t data_size
);

/**
 * Supply TLS wire bytes received from the underlying byte transport.
 *
 * One call may emit zero, one, or multiple plaintext/ciphertext callbacks while
 * handshake, record processing, and protocol responses are handled internally.
 *
 * @return KC_TLSIO_OK, KC_TLSIO_CLOSED after authenticated close_notify, or a
 * negative status.
 */
int kc_tlsio_receive(
    kc_tlsio_t *tls,
    const void *data,
    size_t data_size
);

/**
 * Close and release one TLS session.
 *
 * An established session emits its TLS close_notify through ciphertext before
 * being released. NULL is accepted.
 */
void kc_tlsio_close(kc_tlsio_t *tls);

/** Return a static message for one public status code. */
const char *kc_tlsio_strerror(int status);

/** Return the build version generated at compile time. */
uint64_t kc_tlsio_version(void);

#ifdef __cplusplus
}
#endif

#endif
