/**
 * libtlsio.h - Authenticated TLS byte connection.
 * Summary: Public API for connecting and exchanging bytes over TLS.
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

#define KC_TLSIO_OK 0
#define KC_TLSIO_EOF 1
#define KC_TLSIO_EINVAL -1
#define KC_TLSIO_ENET -2
#define KC_TLSIO_ETLS -3
#define KC_TLSIO_EVERIFY -4
#define KC_TLSIO_ENOMEM -5

typedef struct {
    const char *host;
    unsigned short port;
} kc_tlsio_options_t;

/**
 * Opens one authenticated TLS connection.
 * Port zero selects the conventional TLS port 443. The peer certificate and
 * host name are verified against the default trust store.
 * @param out Destination connection pointer.
 * @param options Connection options.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_connect(
    kc_tlsio_t **out,
    const kc_tlsio_options_t *options
);

/**
 * Writes all supplied bytes to one TLS connection.
 * @param tls Connection handle.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_write(kc_tlsio_t *tls, const void *data, size_t size);

/**
 * Reads the next available TLS plaintext chunk.
 * A successful read returns an owned allocation that must be released with
 * kc_tlsio_free(). Clean peer shutdown returns KC_TLSIO_EOF with no data.
 * @param tls Connection handle.
 * @param out_data Destination for allocated plaintext bytes.
 * @param out_size Destination plaintext byte count.
 * @return KC_TLSIO_OK, KC_TLSIO_EOF, or a negative status code.
 */
int kc_tlsio_read(
    kc_tlsio_t *tls,
    void **out_data,
    size_t *out_size
);

/**
 * Closes and releases one TLS connection.
 * @param tls Connection handle, or NULL.
 * @return void.
 */
void kc_tlsio_close(kc_tlsio_t *tls);

/**
 * Releases one allocation returned by tlsio.
 * @param ptr Allocation pointer, or NULL.
 * @return void.
 */
void kc_tlsio_free(void *ptr);

/**
 * Returns a static message for one public status code.
 * @param code Public status code.
 * @return Static status message.
 */
const char *kc_tlsio_strerror(int code);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tlsio_version(void);

#ifdef __cplusplus
}
#endif

#endif
