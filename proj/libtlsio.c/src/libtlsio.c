/**
 * libtlsio.c - Authenticated TLS byte connection.
 * Summary: Connects to TLS servers and exchanges verified plaintext bytes.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "internal.h"

#include <limits.h>
#include <stdlib.h>

#ifndef KC_TLSIO_BUILD_VERSION
#define KC_TLSIO_BUILD_VERSION 0
#endif

/**
 * Runs the TLS handshake until it reaches a terminal result.
 * @param tls TLS connection.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
static int kc_tlsio_handshake(kc_tlsio_t *tls) {
    int result;

    do {
        result = mbedtls_ssl_handshake(&tls->ssl);
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
        result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result == 0) return KC_TLSIO_OK;
    if (result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ||
        result == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
        return KC_TLSIO_EVERIFY;
    }
    return KC_TLSIO_ETLS;
}

/**
 * Opens one authenticated TLS connection.
 * @param out Destination connection pointer.
 * @param options Connection options.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_connect(
    kc_tlsio_t **out,
    const kc_tlsio_options_t *options
) {
    static const unsigned char personalization[] = "kc_tlsio";
    kc_tlsio_t *tls;
    unsigned short port;
    int rc;

    if (out == NULL) return KC_TLSIO_EINVAL;
    *out = NULL;
    if (options == NULL || options->host == NULL || options->host[0] == '\0') {
        return KC_TLSIO_EINVAL;
    }

    tls = (kc_tlsio_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) return KC_TLSIO_ENOMEM;
    tls->socket = KC_TLSIO_BAD_SOCKET;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_ctr_drbg_init(&tls->random);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_x509_crt_init(&tls->roots);
    tls->tls_ready = 1;

    rc = kc_tlsio_internal_platform_init();
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }
    tls->platform_ready = 1;

    port = options->port == 0U ? 443U : options->port;
    rc = kc_tlsio_internal_socket_connect(
        options->host,
        port,
        &tls->socket
    );
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }

    if (mbedtls_ctr_drbg_seed(
            &tls->random,
            mbedtls_entropy_func,
            &tls->entropy,
            personalization,
            sizeof(personalization) - 1U
        ) != 0) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    if (mbedtls_ssl_config_defaults(
            &tls->config,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT
        ) != 0) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }

    mbedtls_ssl_conf_rng(
        &tls->config,
        mbedtls_ctr_drbg_random,
        &tls->random
    );
    mbedtls_ssl_conf_min_tls_version(
        &tls->config,
        MBEDTLS_SSL_VERSION_TLS1_2
    );
    rc = kc_tlsio_internal_prepare_trust(tls);
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }

    if (mbedtls_ssl_setup(&tls->ssl, &tls->config) != 0 ||
        mbedtls_ssl_set_hostname(&tls->ssl, options->host) != 0) {
        kc_tlsio_close(tls);
        return KC_TLSIO_ETLS;
    }
    mbedtls_ssl_set_bio(
        &tls->ssl,
        &tls->socket,
        kc_tlsio_internal_send,
        kc_tlsio_internal_recv,
        NULL
    );

    rc = kc_tlsio_handshake(tls);
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }
    rc = kc_tlsio_internal_verify_peer(tls, options->host);
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }

    *out = tls;
    return KC_TLSIO_OK;
}

/**
 * Writes all supplied bytes to one TLS connection.
 * @param tls Connection handle.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return KC_TLSIO_OK on success, or a negative status code.
 */
int kc_tlsio_write(kc_tlsio_t *tls, const void *data, size_t size) {
    const unsigned char *bytes;
    size_t sent;

    if (tls == NULL || !tls->tls_ready) return KC_TLSIO_EINVAL;
    if (data == NULL && size != 0U) return KC_TLSIO_EINVAL;
    if (size == 0U) return KC_TLSIO_OK;

    bytes = (const unsigned char *)data;
    sent = 0U;
    while (sent < size) {
        size_t remaining;
        int result;

        remaining = size - sent;
        if (remaining > (size_t)INT_MAX) remaining = (size_t)INT_MAX;
        result = mbedtls_ssl_write(&tls->ssl, bytes + sent, remaining);
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (result <= 0) return KC_TLSIO_ETLS;
        sent += (size_t)result;
    }
    return KC_TLSIO_OK;
}

/**
 * Reads the next available TLS plaintext chunk.
 * @param tls Connection handle.
 * @param out_data Destination for allocated plaintext bytes.
 * @param out_size Destination plaintext byte count.
 * @return KC_TLSIO_OK, KC_TLSIO_EOF, or a negative status code.
 */
int kc_tlsio_read(
    kc_tlsio_t *tls,
    void **out_data,
    size_t *out_size
) {
    unsigned char *buffer;
    int result;

    if (out_data == NULL || out_size == NULL) return KC_TLSIO_EINVAL;
    *out_data = NULL;
    *out_size = 0U;
    if (tls == NULL || !tls->tls_ready) return KC_TLSIO_EINVAL;

    buffer = (unsigned char *)malloc(KC_TLSIO_READ_SIZE);
    if (buffer == NULL) return KC_TLSIO_ENOMEM;
    do {
        result = mbedtls_ssl_read(&tls->ssl, buffer, KC_TLSIO_READ_SIZE);
    } while (result == MBEDTLS_ERR_SSL_WANT_READ ||
        result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result > 0) {
        *out_data = buffer;
        *out_size = (size_t)result;
        return KC_TLSIO_OK;
    }

    free(buffer);
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return KC_TLSIO_EOF;
    return KC_TLSIO_ETLS;
}

/**
 * Closes and releases one TLS connection.
 * @param tls Connection handle, or NULL.
 * @return void.
 */
void kc_tlsio_close(kc_tlsio_t *tls) {
    if (tls == NULL) return;
    if (tls->tls_ready) {
        if (tls->socket != KC_TLSIO_BAD_SOCKET) {
            mbedtls_ssl_close_notify(&tls->ssl);
        }
        mbedtls_ssl_free(&tls->ssl);
        mbedtls_ssl_config_free(&tls->config);
        mbedtls_ctr_drbg_free(&tls->random);
        mbedtls_entropy_free(&tls->entropy);
        mbedtls_x509_crt_free(&tls->roots);
    }
    if (tls->socket != KC_TLSIO_BAD_SOCKET) {
        kc_tlsio_internal_socket_close(tls->socket);
    }
    if (tls->platform_ready) kc_tlsio_internal_platform_cleanup();
    free(tls);
}

/**
 * Releases one allocation returned by tlsio.
 * @param ptr Allocation pointer, or NULL.
 * @return void.
 */
void kc_tlsio_free(void *ptr) {
    free(ptr);
}

/**
 * Returns a static message for one public status code.
 * @param code Public status code.
 * @return Static status message.
 */
const char *kc_tlsio_strerror(int code) {
    switch (code) {
        case KC_TLSIO_OK:
            return "success";
        case KC_TLSIO_EOF:
            return "peer closed the TLS connection";
        case KC_TLSIO_EINVAL:
            return "invalid argument";
        case KC_TLSIO_ENET:
            return "network connection failed";
        case KC_TLSIO_ETLS:
            return "TLS operation failed";
        case KC_TLSIO_EVERIFY:
            return "TLS peer verification failed";
        case KC_TLSIO_ENOMEM:
            return "memory allocation failed";
        default:
            return "unknown tlsio status";
    }
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tlsio_version(void) {
    return (uint64_t)KC_TLSIO_BUILD_VERSION;
}
