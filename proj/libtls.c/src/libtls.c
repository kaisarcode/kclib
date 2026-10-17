/**
 * libtls.c - Transport-independent TLS session.
 * Summary: Runs client and server TLS over caller-supplied byte transport.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtls.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
typedef CRITICAL_SECTION kc_tls_mutex_t;
#else
#include <pthread.h>
typedef pthread_mutex_t kc_tls_mutex_t;
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#ifndef KC_TLS_BUILD_VERSION
#define KC_TLS_BUILD_VERSION 0
#endif

#define KC_TLS_READ_SIZE 65536U

#define KC_TLS_EVENT_PLAINTEXT 1
#define KC_TLS_EVENT_CIPHERTEXT 2

typedef struct kc_tls_buffer {
    unsigned char *data;
    size_t size;
    size_t offset;
    struct kc_tls_buffer *next;
} kc_tls_buffer_t;

typedef struct kc_tls_event {
    int type;
    unsigned char *data;
    size_t size;
    struct kc_tls_event *next;
} kc_tls_event_t;

struct kc_tls {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context random;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt roots;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context private_key;

    kc_tls_mutex_t mutex;
    int mutex_ready;

    int role;
    int native_trust;
    int handshake_ready;
    int close_sent;
    int status;
    char *host;

    kc_tls_buffer_t *input_head;
    kc_tls_buffer_t *input_tail;
    kc_tls_buffer_t *pending_head;
    kc_tls_buffer_t *pending_tail;

    kc_tls_event_t *event_head;
    kc_tls_event_t *event_tail;

    kc_tls_plaintext_fn plaintext;
    kc_tls_ciphertext_fn ciphertext;
    void *userdata;
};

/**
 * Initialize one private mutex.
 * @return Zero on success, nonzero on failure.
 */
static int kc_tls_mutex_open(kc_tls_mutex_t *mutex) {
#ifdef _WIN32
    InitializeCriticalSection(mutex);
    return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

/**
 * Release one private mutex.
 * @return None.
 */
static void kc_tls_mutex_close(kc_tls_mutex_t *mutex) {
#ifdef _WIN32
    DeleteCriticalSection(mutex);
#else
    (void)pthread_mutex_destroy(mutex);
#endif
}

/**
 * Lock one TLS session.
 * @return None.
 */
static void kc_tls_lock(kc_tls_t *tls) {
#ifdef _WIN32
    EnterCriticalSection(&tls->mutex);
#else
    (void)pthread_mutex_lock(&tls->mutex);
#endif
}

/**
 * Unlock one TLS session.
 * @return None.
 */
static void kc_tls_unlock(kc_tls_t *tls) {
#ifdef _WIN32
    LeaveCriticalSection(&tls->mutex);
#else
    (void)pthread_mutex_unlock(&tls->mutex);
#endif
}

/**
 * Duplicate one null-terminated string.
 * @return Owned string, or NULL on allocation failure.
 */
static char *kc_tls_string_copy(const char *value) {
    char *copy;
    size_t size;

    if (value == NULL) return NULL;
    size = strlen(value);
    if (size == SIZE_MAX) return NULL;
    copy = (char *)malloc(size + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, value, size + 1U);
    return copy;
}

/**
 * Release a private byte-buffer queue.
 * @return None.
 */
static void kc_tls_buffer_free(kc_tls_buffer_t *buffer) {
    while (buffer != NULL) {
        kc_tls_buffer_t *next;

        next = buffer->next;
        free(buffer->data);
        free(buffer);
        buffer = next;
    }
}

/**
 * Append bytes to one private byte-buffer queue.
 * @return KC_TLS_OK or KC_TLS_ENOMEM.
 */
static int kc_tls_buffer_append(
    kc_tls_buffer_t **head,
    kc_tls_buffer_t **tail,
    const void *data,
    size_t size
) {
    kc_tls_buffer_t *buffer;

    if (size == 0U) return KC_TLS_OK;
    buffer = (kc_tls_buffer_t *)calloc(1, sizeof(*buffer));
    if (buffer == NULL) return KC_TLS_ENOMEM;
    buffer->data = (unsigned char *)malloc(size);
    if (buffer->data == NULL) {
        free(buffer);
        return KC_TLS_ENOMEM;
    }
    memcpy(buffer->data, data, size);
    buffer->size = size;

    if (*tail != NULL) {
        (*tail)->next = buffer;
    } else {
        *head = buffer;
    }
    *tail = buffer;
    return KC_TLS_OK;
}

/**
 * Consume bytes from the head of one private byte-buffer queue.
 * @return None.
 */
static void kc_tls_buffer_consume(
    kc_tls_buffer_t **head,
    kc_tls_buffer_t **tail,
    size_t size
) {
    while (*head != NULL && size != 0U) {
        kc_tls_buffer_t *buffer;
        size_t available;

        buffer = *head;
        available = buffer->size - buffer->offset;
        if (size < available) {
            buffer->offset += size;
            return;
        }

        size -= available;
        *head = buffer->next;
        if (*head == NULL) *tail = NULL;
        free(buffer->data);
        free(buffer);
    }
}

/**
 * Release a detached event list.
 * @return None.
 */
static void kc_tls_event_free(kc_tls_event_t *event) {
    while (event != NULL) {
        kc_tls_event_t *next;

        next = event->next;
        free(event->data);
        free(event);
        event = next;
    }
}

/**
 * Record one terminal TLS status.
 * @return The recorded status.
 */
static int kc_tls_fail(kc_tls_t *tls, int status) {
    if (tls->status == KC_TLS_OK) tls->status = status;
    return tls->status;
}

/**
 * Queue one callback event.
 * @return KC_TLS_OK or KC_TLS_ENOMEM.
 */
static int kc_tls_event_append(
    kc_tls_t *tls,
    int type,
    const void *data,
    size_t size
) {
    kc_tls_event_t *event;

    if (size == 0U) return KC_TLS_OK;
    event = (kc_tls_event_t *)calloc(1, sizeof(*event));
    if (event == NULL) return kc_tls_fail(tls, KC_TLS_ENOMEM);
    event->data = (unsigned char *)malloc(size);
    if (event->data == NULL) {
        free(event);
        return kc_tls_fail(tls, KC_TLS_ENOMEM);
    }
    memcpy(event->data, data, size);
    event->type = type;
    event->size = size;

    if (tls->event_tail != NULL) {
        tls->event_tail->next = event;
    } else {
        tls->event_head = event;
    }
    tls->event_tail = event;
    return KC_TLS_OK;
}

/**
 * Detach all queued callback events from a TLS session.
 * @return Detached event list.
 */
static kc_tls_event_t *kc_tls_event_take(kc_tls_t *tls) {
    kc_tls_event_t *events;

    events = tls->event_head;
    tls->event_head = NULL;
    tls->event_tail = NULL;
    return events;
}

/**
 * Dispatch one detached event list without touching the TLS session.
 * @return None.
 */
static void kc_tls_event_dispatch(
    kc_tls_event_t *event,
    kc_tls_plaintext_fn plaintext,
    kc_tls_ciphertext_fn ciphertext,
    void *userdata
) {
    while (event != NULL) {
        kc_tls_event_t *next;

        next = event->next;
        if (event->type == KC_TLS_EVENT_PLAINTEXT) {
            plaintext(event->data, event->size, userdata);
        } else {
            ciphertext(event->data, event->size, userdata);
        }
        free(event->data);
        free(event);
        event = next;
    }
}

/**
 * Return whether bytes look like PEM text.
 * @return Nonzero for PEM input.
 */
static int kc_tls_is_pem(const void *data, size_t size) {
    static const char prefix[] = "-----BEGIN ";
    return data != NULL &&
        size >= sizeof(prefix) - 1U &&
        memcmp(data, prefix, sizeof(prefix) - 1U) == 0;
}

/**
 * Make a temporary NUL-terminated PEM copy.
 * @return Owned copy, or NULL on failure.
 */
static unsigned char *kc_tls_pem_copy(const void *data, size_t size) {
    unsigned char *copy;

    if (size == SIZE_MAX) return NULL;
    copy = (unsigned char *)malloc(size + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, data, size);
    copy[size] = '\0';
    return copy;
}

/**
 * Parse one certificate/CA input that may be PEM or DER.
 * @return KC_TLS_OK, KC_TLS_ETLS, or KC_TLS_ENOMEM.
 */
static int kc_tls_parse_certificate(
    mbedtls_x509_crt *certificate,
    const void *data,
    size_t size
) {
    const unsigned char *bytes;
    unsigned char *copy;
    size_t parse_size;
    int result;

    if (data == NULL || size == 0U) return KC_TLS_EINVAL;
    bytes = (const unsigned char *)data;
    copy = NULL;
    parse_size = size;

    if (kc_tls_is_pem(data, size)) {
        copy = kc_tls_pem_copy(data, size);
        if (copy == NULL) return KC_TLS_ENOMEM;
        bytes = copy;
        parse_size = size + 1U;
    }

    result = mbedtls_x509_crt_parse(certificate, bytes, parse_size);
    free(copy);
    return result == 0 ? KC_TLS_OK : KC_TLS_ETLS;
}

/**
 * Parse one private key input that may be PEM or DER.
 * @return KC_TLS_OK, KC_TLS_ETLS, or KC_TLS_ENOMEM.
 */
static int kc_tls_parse_private_key(
    kc_tls_t *tls,
    const void *data,
    size_t size
) {
    const unsigned char *bytes;
    unsigned char *copy;
    size_t parse_size;
    int result;

    if (data == NULL || size == 0U) return KC_TLS_EINVAL;
    bytes = (const unsigned char *)data;
    copy = NULL;
    parse_size = size;

    if (kc_tls_is_pem(data, size)) {
        copy = kc_tls_pem_copy(data, size);
        if (copy == NULL) return KC_TLS_ENOMEM;
        bytes = copy;
        parse_size = size + 1U;
    }

    result = mbedtls_pk_parse_key(
        &tls->private_key,
        bytes,
        parse_size,
        NULL,
        0U,
        mbedtls_ctr_drbg_random,
        &tls->random
    );
    if (copy != NULL) {
        mbedtls_platform_zeroize(copy, parse_size);
        free(copy);
    }
    return result == 0 ? KC_TLS_OK : KC_TLS_ETLS;
}

/**
 * Mbed TLS receive BIO backed by caller-supplied encrypted input.
 * @return Bytes read or MBEDTLS_ERR_SSL_WANT_READ.
 */
static int kc_tls_bio_recv(
    void *context,
    unsigned char *data,
    size_t size
) {
    kc_tls_t *tls;
    size_t copied;

    tls = (kc_tls_t *)context;
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    if (size == 0U) return 0;

    copied = 0U;
    while (copied < size && tls->input_head != NULL) {
        kc_tls_buffer_t *buffer;
        size_t available;
        size_t part;

        buffer = tls->input_head;
        available = buffer->size - buffer->offset;
        part = size - copied;
        if (part > available) part = available;
        memcpy(data + copied, buffer->data + buffer->offset, part);
        copied += part;
        kc_tls_buffer_consume(
            &tls->input_head,
            &tls->input_tail,
            part
        );
    }

    if (copied == 0U) return MBEDTLS_ERR_SSL_WANT_READ;
    return (int)copied;
}

/**
 * Mbed TLS send BIO that queues encrypted output for the consumer.
 * @return Bytes accepted or MBEDTLS_ERR_SSL_ALLOC_FAILED.
 */
static int kc_tls_bio_send(
    void *context,
    const unsigned char *data,
    size_t size
) {
    kc_tls_t *tls;

    tls = (kc_tls_t *)context;
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    if (kc_tls_event_append(
            tls,
            KC_TLS_EVENT_CIPHERTEXT,
            data,
            size
        ) != KC_TLS_OK) {
        return MBEDTLS_ERR_SSL_ALLOC_FAILED;
    }
    return (int)size;
}

#if !defined(_WIN32) && !defined(__APPLE__)

/**
 * Load trusted roots from the native Unix certificate store.
 * @return KC_TLS_OK on success, otherwise KC_TLS_EVERIFY.
 */
static int kc_tls_load_roots(mbedtls_x509_crt *roots) {
    static const char *files[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/etc/ssl/cert.pem"
    };
    static const char *paths[] = {
#ifdef __ANDROID__
        "/apex/com.android.conscrypt/cacerts",
        "/system/etc/security/cacerts",
#endif
        "/etc/ssl/certs",
        "/etc/pki/ca-trust/extracted/pem"
    };
    size_t index;

    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        if (mbedtls_x509_crt_parse_file(roots, files[index]) == 0) {
            return KC_TLS_OK;
        }
    }
    for (index = 0U; index < sizeof(paths) / sizeof(paths[0]); index++) {
        if (mbedtls_x509_crt_parse_path(roots, paths[index]) >= 0 &&
            roots->raw.p != NULL) {
            return KC_TLS_OK;
        }
    }
    return KC_TLS_EVERIFY;
}
#endif

#ifdef _WIN32

/**
 * Convert UTF-8 host text to one Windows wide string.
 * @return Owned wide string, or NULL on failure.
 */
static wchar_t *kc_tls_wide_host(const char *host) {
    wchar_t *wide;
    int size;

    size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, host, -1, NULL, 0);
    if (size <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)size * sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            host,
            -1,
            wide,
            size
        ) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

/**
 * Verify one peer chain with the Windows system trust store.
 * @return KC_TLS_OK or KC_TLS_EVERIFY.
 */
static int kc_tls_windows_verify(
    const mbedtls_x509_crt *peer,
    const char *host
) {
    HCERTSTORE store;
    PCCERT_CONTEXT leaf;
    PCCERT_CHAIN_CONTEXT chain;
    CERT_CHAIN_PARA chain_options;
    CERT_CHAIN_POLICY_PARA policy_options;
    CERT_CHAIN_POLICY_STATUS policy_status;
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_options;
    const mbedtls_x509_crt *item;
    wchar_t *wide_host;
    BOOL ok;
    int rc;

    if (peer == NULL) return KC_TLS_EVERIFY;
    wide_host = kc_tls_wide_host(host);
    if (wide_host == NULL) return KC_TLS_EVERIFY;
    store = CertOpenStore(
        CERT_STORE_PROV_MEMORY,
        X509_ASN_ENCODING,
        0,
        CERT_STORE_CREATE_NEW_FLAG,
        NULL
    );
    if (store == NULL) {
        free(wide_host);
        return KC_TLS_EVERIFY;
    }

    leaf = NULL;
    for (item = peer; item != NULL; item = item->next) {
        PCCERT_CONTEXT certificate;

        certificate = CertCreateCertificateContext(
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            item->raw.p,
            (DWORD)item->raw.len
        );
        if (certificate == NULL) continue;
        if (leaf == NULL) {
            leaf = CertDuplicateCertificateContext(certificate);
        }
        CertAddCertificateContextToStore(
            store,
            certificate,
            CERT_STORE_ADD_USE_EXISTING,
            NULL
        );
        CertFreeCertificateContext(certificate);
    }
    if (leaf == NULL) {
        CertCloseStore(store, 0);
        free(wide_host);
        return KC_TLS_EVERIFY;
    }

    memset(&chain_options, 0, sizeof(chain_options));
    chain_options.cbSize = sizeof(chain_options);
    chain = NULL;
    ok = CertGetCertificateChain(
        NULL,
        leaf,
        NULL,
        store,
        &chain_options,
        0,
        NULL,
        &chain
    );
    rc = KC_TLS_EVERIFY;
    if (ok && chain != NULL) {
        memset(&ssl_options, 0, sizeof(ssl_options));
        ssl_options.cbSize = sizeof(ssl_options);
        ssl_options.dwAuthType = AUTHTYPE_SERVER;
        ssl_options.pwszServerName = wide_host;
        memset(&policy_options, 0, sizeof(policy_options));
        policy_options.cbSize = sizeof(policy_options);
        policy_options.pvExtraPolicyPara = &ssl_options;
        memset(&policy_status, 0, sizeof(policy_status));
        policy_status.cbSize = sizeof(policy_status);
        if (CertVerifyCertificateChainPolicy(
                CERT_CHAIN_POLICY_SSL,
                chain,
                &policy_options,
                &policy_status
            ) && policy_status.dwError == 0U) {
            rc = KC_TLS_OK;
        }
    }

    if (chain != NULL) CertFreeCertificateChain(chain);
    CertFreeCertificateContext(leaf);
    CertCloseStore(store, 0);
    free(wide_host);
    return rc;
}
#elif defined(__APPLE__)

/**
 * Create one Apple certificate object from DER bytes.
 * @return Owned Apple certificate, or NULL on failure.
 */
static SecCertificateRef kc_tls_apple_certificate(
    const mbedtls_x509_crt *certificate
) {
    CFDataRef data;
    SecCertificateRef result;

    data = CFDataCreate(
        kCFAllocatorDefault,
        certificate->raw.p,
        (CFIndex)certificate->raw.len
    );
    if (data == NULL) return NULL;
    result = SecCertificateCreateWithData(kCFAllocatorDefault, data);
    CFRelease(data);
    return result;
}

/**
 * Verify one peer chain with the Apple system trust store.
 * @return KC_TLS_OK or KC_TLS_EVERIFY.
 */
static int kc_tls_apple_verify(
    const mbedtls_x509_crt *peer,
    const char *host
) {
    CFMutableArrayRef certificates;
    CFStringRef server;
    SecPolicyRef policy;
    SecTrustRef trust;
    const mbedtls_x509_crt *item;
    int rc;

    if (peer == NULL) return KC_TLS_EVERIFY;
    certificates = CFArrayCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeArrayCallBacks
    );
    if (certificates == NULL) return KC_TLS_EVERIFY;
    for (item = peer; item != NULL; item = item->next) {
        SecCertificateRef certificate;

        certificate = kc_tls_apple_certificate(item);
        if (certificate == NULL) continue;
        CFArrayAppendValue(certificates, certificate);
        CFRelease(certificate);
    }
    if (CFArrayGetCount(certificates) == 0) {
        CFRelease(certificates);
        return KC_TLS_EVERIFY;
    }

    server = CFStringCreateWithCString(
        kCFAllocatorDefault,
        host,
        kCFStringEncodingUTF8
    );
    if (server == NULL) {
        CFRelease(certificates);
        return KC_TLS_EVERIFY;
    }
    policy = SecPolicyCreateSSL(1, server);
    CFRelease(server);
    if (policy == NULL) {
        CFRelease(certificates);
        return KC_TLS_EVERIFY;
    }

    trust = NULL;
    rc = KC_TLS_EVERIFY;
    if (SecTrustCreateWithCertificates(
            certificates,
            policy,
            &trust
        ) == errSecSuccess &&
        trust != NULL &&
        SecTrustEvaluateWithError(trust, NULL)) {
        rc = KC_TLS_OK;
    }

    if (trust != NULL) CFRelease(trust);
    CFRelease(policy);
    CFRelease(certificates);
    return rc;
}
#endif

/**
 * Configure client peer trust.
 * @return KC_TLS_OK or a negative status.
 */
static int kc_tls_prepare_client_trust(
    kc_tls_t *tls,
    const kc_tls_options_t *options
) {
    int rc;

    if (options->ca != NULL || options->ca_size != 0U) {
        if (options->ca == NULL || options->ca_size == 0U) {
            return KC_TLS_EINVAL;
        }
        rc = kc_tls_parse_certificate(
            &tls->roots,
            options->ca,
            options->ca_size
        );
        if (rc != KC_TLS_OK) return rc;
        mbedtls_ssl_conf_ca_chain(&tls->config, &tls->roots, NULL);
        mbedtls_ssl_conf_authmode(
            &tls->config,
            MBEDTLS_SSL_VERIFY_REQUIRED
        );
        return KC_TLS_OK;
    }

#if defined(_WIN32) || defined(__APPLE__)
    tls->native_trust = 1;
    mbedtls_ssl_conf_authmode(
        &tls->config,
        MBEDTLS_SSL_VERIFY_OPTIONAL
    );
    return KC_TLS_OK;
#else
    rc = kc_tls_load_roots(&tls->roots);
    if (rc != KC_TLS_OK) return rc;
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->roots, NULL);
    mbedtls_ssl_conf_authmode(
        &tls->config,
        MBEDTLS_SSL_VERIFY_REQUIRED
    );
    return KC_TLS_OK;
#endif
}

/**
 * Verify the client session peer after handshake.
 * @return KC_TLS_OK or KC_TLS_EVERIFY.
 */
static int kc_tls_verify_peer(kc_tls_t *tls) {
#if defined(_WIN32)
    if (tls->native_trust) {
        return kc_tls_windows_verify(
            mbedtls_ssl_get_peer_cert(&tls->ssl),
            tls->host
        );
    }
#elif defined(__APPLE__)
    if (tls->native_trust) {
        return kc_tls_apple_verify(
            mbedtls_ssl_get_peer_cert(&tls->ssl),
            tls->host
        );
    }
#endif

    return mbedtls_ssl_get_verify_result(&tls->ssl) == 0U
        ? KC_TLS_OK
        : KC_TLS_EVERIFY;
}

/**
 * Map one Mbed TLS operation error to the public status model.
 * @return Public negative status.
 */
static int kc_tls_map_tls_error(int result) {
    if (result == MBEDTLS_ERR_SSL_ALLOC_FAILED) return KC_TLS_ENOMEM;
    if (result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ||
        result == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
        return KC_TLS_EVERIFY;
    }
    return KC_TLS_ETLS;
}

/**
 * Flush queued application plaintext through an established session.
 * @return KC_TLS_OK or a terminal status.
 */
static int kc_tls_flush_pending(kc_tls_t *tls) {
    while (tls->pending_head != NULL) {
        kc_tls_buffer_t *buffer;
        size_t remaining;
        int result;

        buffer = tls->pending_head;
        remaining = buffer->size - buffer->offset;
        if (remaining > MBEDTLS_SSL_OUT_CONTENT_LEN) {
            remaining = MBEDTLS_SSL_OUT_CONTENT_LEN;
        }
        result = mbedtls_ssl_write(
            &tls->ssl,
            buffer->data + buffer->offset,
            remaining
        );
        if (tls->status != KC_TLS_OK) return tls->status;
        if (result > 0) {
            kc_tls_buffer_consume(
                &tls->pending_head,
                &tls->pending_tail,
                (size_t)result
            );
            continue;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return KC_TLS_OK;
        }
        return kc_tls_fail(tls, kc_tls_map_tls_error(result));
    }
    return KC_TLS_OK;
}

/**
 * Send a reciprocal close_notify after peer shutdown.
 * @return None.
 */
static void kc_tls_reply_close_notify(kc_tls_t *tls) {
    int result;

    if (tls->close_sent || !tls->handshake_ready) return;
    result = mbedtls_ssl_close_notify(&tls->ssl);
    if (tls->status == KC_TLS_OK &&
        result != 0 &&
        result != MBEDTLS_ERR_SSL_WANT_READ &&
        result != MBEDTLS_ERR_SSL_WANT_WRITE) {
        (void)kc_tls_fail(tls, kc_tls_map_tls_error(result));
    }
    tls->close_sent = 1;
}

/**
 * Advance handshake, queued writes, and plaintext reads as far as possible.
 * @return Current public status.
 */
static int kc_tls_pump(kc_tls_t *tls) {
    unsigned char buffer[KC_TLS_READ_SIZE];

    for (;;) {
        int result;

        if (tls->status != KC_TLS_OK) return tls->status;

        if (!tls->handshake_ready) {
            result = mbedtls_ssl_handshake(&tls->ssl);
            if (tls->status != KC_TLS_OK) return tls->status;
            if (result == 0) {
                if (tls->role == KC_TLS_CLIENT) {
                    int verify;

                    verify = kc_tls_verify_peer(tls);
                    if (verify != KC_TLS_OK) {
                        return kc_tls_fail(tls, verify);
                    }
                }
                tls->handshake_ready = 1;
                continue;
            }
            if (result == MBEDTLS_ERR_SSL_WANT_READ ||
                result == MBEDTLS_ERR_SSL_WANT_WRITE) {
                return KC_TLS_OK;
            }
            return kc_tls_fail(tls, kc_tls_map_tls_error(result));
        }

        result = kc_tls_flush_pending(tls);
        if (result != KC_TLS_OK) return result;
        if (tls->pending_head != NULL) return KC_TLS_OK;

        result = mbedtls_ssl_read(&tls->ssl, buffer, sizeof(buffer));
        if (tls->status != KC_TLS_OK) return tls->status;
        if (result > 0) {
            if (kc_tls_event_append(
                    tls,
                    KC_TLS_EVENT_PLAINTEXT,
                    buffer,
                    (size_t)result
                ) != KC_TLS_OK) {
                return tls->status;
            }
            continue;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ ||
            result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return KC_TLS_OK;
        }
        if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            kc_tls_reply_close_notify(tls);
            if (tls->status < 0) return tls->status;
            tls->status = KC_TLS_CLOSED;
            return KC_TLS_CLOSED;
        }
        if (result == 0) return KC_TLS_OK;
        return kc_tls_fail(tls, kc_tls_map_tls_error(result));
    }
}

/**
 * Release initialized TLS internals. The caller must own the context.
 * @return None.
 */
static void kc_tls_release(kc_tls_t *tls) {
    if (tls == NULL) return;
    kc_tls_buffer_free(tls->input_head);
    kc_tls_buffer_free(tls->pending_head);
    kc_tls_event_free(tls->event_head);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    mbedtls_ctr_drbg_free(&tls->random);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_x509_crt_free(&tls->roots);
    mbedtls_x509_crt_free(&tls->certificate);
    mbedtls_pk_free(&tls->private_key);
    free(tls->host);
    if (tls->mutex_ready) kc_tls_mutex_close(&tls->mutex);
    free(tls);
}

/**
 * Open one client or server TLS session.
 * @return KC_TLS_OK on success, otherwise a negative status.
 */
int kc_tls_open(
    kc_tls_t **out,
    const kc_tls_options_t *options,
    kc_tls_plaintext_fn plaintext,
    kc_tls_ciphertext_fn ciphertext,
    void *userdata
) {
    static const unsigned char personalization[] = "kc_tls";
    kc_tls_t *tls;
    kc_tls_event_t *events;
    int endpoint;
    int rc;

    if (out == NULL) return KC_TLS_EINVAL;
    *out = NULL;
    if (options == NULL || plaintext == NULL || ciphertext == NULL) {
        return KC_TLS_EINVAL;
    }
    if (options->role != KC_TLS_CLIENT &&
        options->role != KC_TLS_SERVER) {
        return KC_TLS_EINVAL;
    }
    if (options->role == KC_TLS_CLIENT &&
        (options->host == NULL || options->host[0] == '\0')) {
        return KC_TLS_EINVAL;
    }
    if (options->role == KC_TLS_SERVER &&
        (options->certificate == NULL ||
        options->certificate_size == 0U ||
        options->private_key == NULL ||
        options->private_key_size == 0U)) {
        return KC_TLS_EINVAL;
    }
    if (psa_crypto_init() != PSA_SUCCESS) return KC_TLS_ETLS;

    tls = (kc_tls_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) return KC_TLS_ENOMEM;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_ctr_drbg_init(&tls->random);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_x509_crt_init(&tls->roots);
    mbedtls_x509_crt_init(&tls->certificate);
    mbedtls_pk_init(&tls->private_key);

    if (kc_tls_mutex_open(&tls->mutex) != 0) {
        kc_tls_release(tls);
        return KC_TLS_ETLS;
    }
    tls->mutex_ready = 1;
    tls->role = options->role;
    tls->plaintext = plaintext;
    tls->ciphertext = ciphertext;
    tls->userdata = userdata;

    if (options->role == KC_TLS_CLIENT) {
        tls->host = kc_tls_string_copy(options->host);
        if (tls->host == NULL) {
            kc_tls_release(tls);
            return KC_TLS_ENOMEM;
        }
    }

    if (mbedtls_ctr_drbg_seed(
            &tls->random,
            mbedtls_entropy_func,
            &tls->entropy,
            personalization,
            sizeof(personalization) - 1U
        ) != 0) {
        kc_tls_release(tls);
        return KC_TLS_ETLS;
    }

    endpoint = options->role == KC_TLS_CLIENT
        ? MBEDTLS_SSL_IS_CLIENT
        : MBEDTLS_SSL_IS_SERVER;
    if (mbedtls_ssl_config_defaults(
            &tls->config,
            endpoint,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT
        ) != 0) {
        kc_tls_release(tls);
        return KC_TLS_ETLS;
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

    if (options->role == KC_TLS_CLIENT) {
        rc = kc_tls_prepare_client_trust(tls, options);
        if (rc != KC_TLS_OK) {
            kc_tls_release(tls);
            return rc;
        }
    } else {
        rc = kc_tls_parse_certificate(
            &tls->certificate,
            options->certificate,
            options->certificate_size
        );
        if (rc == KC_TLS_OK) {
            rc = kc_tls_parse_private_key(
                tls,
                options->private_key,
                options->private_key_size
            );
        }
        if (rc != KC_TLS_OK) {
            kc_tls_release(tls);
            return rc;
        }
        mbedtls_ssl_conf_authmode(
            &tls->config,
            MBEDTLS_SSL_VERIFY_NONE
        );
        if (mbedtls_ssl_conf_own_cert(
                &tls->config,
                &tls->certificate,
                &tls->private_key
            ) != 0) {
            kc_tls_release(tls);
            return KC_TLS_ETLS;
        }
    }

    if (mbedtls_ssl_setup(&tls->ssl, &tls->config) != 0) {
        kc_tls_release(tls);
        return KC_TLS_ETLS;
    }
    if (options->role == KC_TLS_CLIENT &&
        mbedtls_ssl_set_hostname(&tls->ssl, tls->host) != 0) {
        kc_tls_release(tls);
        return KC_TLS_ETLS;
    }

    mbedtls_ssl_set_bio(
        &tls->ssl,
        tls,
        kc_tls_bio_send,
        kc_tls_bio_recv,
        NULL
    );

    events = NULL;
    if (options->role == KC_TLS_CLIENT) {
        kc_tls_lock(tls);
        rc = kc_tls_pump(tls);
        events = kc_tls_event_take(tls);
        kc_tls_unlock(tls);
        if (rc < 0) {
            kc_tls_event_free(events);
            kc_tls_release(tls);
            return rc;
        }
    }

    kc_tls_event_dispatch(
        events,
        plaintext,
        ciphertext,
        userdata
    );
    *out = tls;
    return KC_TLS_OK;
}

/**
 * Send plaintext through one TLS session.
 * @return KC_TLS_OK, KC_TLS_CLOSED, or a negative status.
 */
int kc_tls_send(
    kc_tls_t *tls,
    const void *data,
    size_t data_size
) {
    kc_tls_event_t *events;
    kc_tls_plaintext_fn plaintext;
    kc_tls_ciphertext_fn ciphertext;
    void *userdata;
    int rc;

    if (tls == NULL) return KC_TLS_EINVAL;
    if (data == NULL && data_size != 0U) return KC_TLS_EINVAL;

    kc_tls_lock(tls);
    if (tls->status != KC_TLS_OK) {
        rc = tls->status;
    } else {
        rc = kc_tls_buffer_append(
            &tls->pending_head,
            &tls->pending_tail,
            data,
            data_size
        );
        if (rc == KC_TLS_OK) rc = kc_tls_pump(tls);
    }
    events = kc_tls_event_take(tls);
    plaintext = tls->plaintext;
    ciphertext = tls->ciphertext;
    userdata = tls->userdata;
    kc_tls_unlock(tls);

    kc_tls_event_dispatch(events, plaintext, ciphertext, userdata);
    return rc;
}

/**
 * Supply TLS wire bytes received from the underlying transport.
 * @return KC_TLS_OK, KC_TLS_CLOSED, or a negative status.
 */
int kc_tls_receive(
    kc_tls_t *tls,
    const void *data,
    size_t data_size
) {
    kc_tls_event_t *events;
    kc_tls_plaintext_fn plaintext;
    kc_tls_ciphertext_fn ciphertext;
    void *userdata;
    int rc;

    if (tls == NULL) return KC_TLS_EINVAL;
    if (data == NULL && data_size != 0U) return KC_TLS_EINVAL;

    kc_tls_lock(tls);
    if (tls->status != KC_TLS_OK) {
        rc = tls->status;
    } else {
        rc = kc_tls_buffer_append(
            &tls->input_head,
            &tls->input_tail,
            data,
            data_size
        );
        if (rc == KC_TLS_OK) rc = kc_tls_pump(tls);
    }
    events = kc_tls_event_take(tls);
    plaintext = tls->plaintext;
    ciphertext = tls->ciphertext;
    userdata = tls->userdata;
    kc_tls_unlock(tls);

    kc_tls_event_dispatch(events, plaintext, ciphertext, userdata);
    return rc;
}

/**
 * Close and release one TLS session.
 * @return None.
 */
void kc_tls_close(kc_tls_t *tls) {
    kc_tls_event_t *events;
    kc_tls_plaintext_fn plaintext;
    kc_tls_ciphertext_fn ciphertext;
    void *userdata;

    if (tls == NULL) return;

    kc_tls_lock(tls);
    if (tls->handshake_ready &&
        !tls->close_sent &&
        tls->status >= KC_TLS_OK) {
        int result;

        result = mbedtls_ssl_close_notify(&tls->ssl);
        if (tls->status == KC_TLS_OK &&
            result != 0 &&
            result != MBEDTLS_ERR_SSL_WANT_READ &&
            result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            (void)kc_tls_fail(tls, kc_tls_map_tls_error(result));
        }
        tls->close_sent = 1;
    }

    events = kc_tls_event_take(tls);
    plaintext = tls->plaintext;
    ciphertext = tls->ciphertext;
    userdata = tls->userdata;
    kc_tls_unlock(tls);

    kc_tls_release(tls);
    kc_tls_event_dispatch(events, plaintext, ciphertext, userdata);
}

/**
 * Return a static message for one public status code.
 * @return Static status message.
 */
const char *kc_tls_strerror(int status) {
    switch (status) {
        case KC_TLS_OK:
            return "success";
        case KC_TLS_CLOSED:
            return "TLS peer closed the session";
        case KC_TLS_EINVAL:
            return "invalid argument";
        case KC_TLS_ETLS:
            return "TLS operation failed";
        case KC_TLS_EVERIFY:
            return "TLS peer verification failed";
        case KC_TLS_ENOMEM:
            return "memory allocation failed";
        default:
            return "unknown tls status";
    }
}

/**
 * Return the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_tls_version(void) {
    return (uint64_t)KC_TLS_BUILD_VERSION;
}
