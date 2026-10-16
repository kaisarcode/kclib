/**
 * internal.h - Private tlsio implementation contract.
 * Summary: Shares backend state and private operations across tlsio sources.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_TLSIO_INTERNAL_H
#define KC_TLSIO_INTERNAL_H

#include "libtlsio.h"

#include <stddef.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
typedef SOCKET kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET INVALID_SOCKET
#define KC_TLSIO_PRIVATE
#else
typedef int kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET (-1)
#define KC_TLSIO_PRIVATE __attribute__((visibility("hidden")))
#endif

#define KC_TLSIO_READ_SIZE 65536U

struct kc_tlsio {
    kc_tlsio_socket_t socket;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context random;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt roots;
    int platform_ready;
    int tls_ready;
};

/**
 * Initializes native socket support.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_platform_init(void);

/**
 * Releases native socket support.
 * @return void.
 */
KC_TLSIO_PRIVATE void kc_tlsio_internal_platform_cleanup(void);

/**
 * Closes one native socket.
 * @param socket Native socket.
 * @return void.
 */
KC_TLSIO_PRIVATE void kc_tlsio_internal_socket_close(
    kc_tlsio_socket_t socket
);

/**
 * Connects one TCP socket.
 * @param host Destination host.
 * @param port Destination port.
 * @param out_socket Destination native socket.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_socket_connect(
    const char *host,
    unsigned short port,
    kc_tlsio_socket_t *out_socket
);

/**
 * Sends encrypted bytes through the native socket.
 * @param context Native socket pointer.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return Positive byte count or an Mbed TLS transport error.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_send(
    void *context,
    const unsigned char *data,
    size_t size
);

/**
 * Receives encrypted bytes from the native socket.
 * @param context Native socket pointer.
 * @param data Destination buffer.
 * @param size Buffer size.
 * @return Non-negative byte count or an Mbed TLS transport error.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_recv(
    void *context,
    unsigned char *data,
    size_t size
);

/**
 * Configures certificate trust before the TLS handshake.
 * @param tls TLS connection.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_prepare_trust(kc_tlsio_t *tls);

/**
 * Verifies the peer after the TLS handshake.
 * @param tls TLS connection.
 * @param host Expected peer host.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
KC_TLSIO_PRIVATE int kc_tlsio_internal_verify_peer(
    kc_tlsio_t *tls,
    const char *host
);

#endif
