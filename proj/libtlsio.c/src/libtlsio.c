/**
 * libtlsio.c - Authenticated TLS byte connection.
 * Summary: Connects to TLS servers and exchanges verified plaintext bytes.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtlsio.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#if defined(TLSIO_BUILD_TESTS) && !defined(_WIN32)
extern const unsigned char kc_tlsio_test_ca_pem[];
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
typedef SOCKET kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET INVALID_SOCKET
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int kc_tlsio_socket_t;
#define KC_TLSIO_BAD_SOCKET (-1)
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#ifndef KC_TLSIO_BUILD_VERSION
#define KC_TLSIO_BUILD_VERSION 0
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
static int kc_tlsio_platform_init(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0
        ? KC_TLSIO_OK
        : KC_TLSIO_ENET;
#else
    return KC_TLSIO_OK;
#endif
}

/**
 * Releases native socket support.
 * @return void.
 */
static void kc_tlsio_platform_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Closes one native socket.
 * @param socket Native socket.
 * @return void.
 */
static void kc_tlsio_socket_close(kc_tlsio_socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Connects one TCP socket.
 * @param host Destination host.
 * @param port Destination port.
 * @param out_socket Destination native socket.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_ENET on failure.
 */
static int kc_tlsio_socket_connect(
    const char *host,
    unsigned short port,
    kc_tlsio_socket_t *out_socket
) {
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    char service[16];
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%u", (unsigned int)port);

    addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) {
        return KC_TLSIO_ENET;
    }

    *out_socket = KC_TLSIO_BAD_SOCKET;
    rc = KC_TLSIO_ENET;
    for (address = addresses; address != NULL; address = address->ai_next) {
        kc_tlsio_socket_t socket_handle;

        socket_handle = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );
        if (socket_handle == KC_TLSIO_BAD_SOCKET) continue;
        if (connect(
                socket_handle,
                address->ai_addr,
                address->ai_addrlen
            ) == 0) {
            *out_socket = socket_handle;
            rc = KC_TLSIO_OK;
            break;
        }
        kc_tlsio_socket_close(socket_handle);
    }

    freeaddrinfo(addresses);
    return rc;
}

/**
 * Sends encrypted bytes through the native socket.
 * @param context Native socket pointer.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return Positive byte count or an Mbed TLS transport error.
 */
static int kc_tlsio_send(
    void *context,
    const unsigned char *data,
    size_t size
) {
    kc_tlsio_socket_t socket_handle;
#ifdef _WIN32
    int count;
#else
    int flags;
    ssize_t count;
#endif

    socket_handle = *(kc_tlsio_socket_t *)context;
#ifdef _WIN32
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    count = send(socket_handle, (const char *)data, (int)size, 0);
    if (count == SOCKET_ERROR) return MBEDTLS_ERR_NET_SEND_FAILED;
#else
    flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    count = send(socket_handle, data, size, flags);
    if (count < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
#endif
    return (int)count;
}

/**
 * Receives encrypted bytes from the native socket.
 * @param context Native socket pointer.
 * @param data Destination buffer.
 * @param size Buffer size.
 * @return Non-negative byte count or an Mbed TLS transport error.
 */
static int kc_tlsio_recv(
    void *context,
    unsigned char *data,
    size_t size
) {
    kc_tlsio_socket_t socket_handle;
#ifdef _WIN32
    int count;
#else
    ssize_t count;
#endif

    socket_handle = *(kc_tlsio_socket_t *)context;
#ifdef _WIN32
    if (size > (size_t)INT_MAX) size = (size_t)INT_MAX;
    count = recv(socket_handle, (char *)data, (int)size, 0);
    if (count == SOCKET_ERROR) return MBEDTLS_ERR_NET_RECV_FAILED;
#else
    count = recv(socket_handle, data, size, 0);
    if (count < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
#endif
    return (int)count;
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(TLSIO_BUILD_TESTS)

/**
 * Loads trusted roots from the native Unix certificate store.
 * @param roots Destination certificate chain.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
static int kc_tlsio_load_roots(mbedtls_x509_crt *roots) {
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
            return KC_TLSIO_OK;
        }
    }
    for (index = 0U; index < sizeof(paths) / sizeof(paths[0]); index++) {
        if (mbedtls_x509_crt_parse_path(roots, paths[index]) >= 0 &&
            roots->raw.p != NULL) {
            return KC_TLSIO_OK;
        }
    }
    return KC_TLSIO_EVERIFY;
}
#endif

#ifdef _WIN32

/**
 * Converts UTF-8 host text to a Windows wide string.
 * @param host UTF-8 host name.
 * @return Owned wide string, or NULL on failure.
 */
static wchar_t *kc_tlsio_wide_host(const char *host) {
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
 * Verifies one peer chain with the Windows system trust store.
 * @param peer Peer certificate chain.
 * @param host Expected peer host.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
static int kc_tlsio_windows_verify(
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

    if (peer == NULL) return KC_TLSIO_EVERIFY;
    wide_host = kc_tlsio_wide_host(host);
    if (wide_host == NULL) return KC_TLSIO_EVERIFY;
    store = CertOpenStore(
        CERT_STORE_PROV_MEMORY,
        X509_ASN_ENCODING,
        0,
        CERT_STORE_CREATE_NEW_FLAG,
        NULL
    );
    if (store == NULL) {
        free(wide_host);
        return KC_TLSIO_EVERIFY;
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
        return KC_TLSIO_EVERIFY;
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
    rc = KC_TLSIO_EVERIFY;
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
            rc = KC_TLSIO_OK;
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
 * Creates one Apple certificate object from DER bytes.
 * @param certificate Mbed TLS certificate.
 * @return Owned Apple certificate, or NULL on failure.
 */
static SecCertificateRef kc_tlsio_apple_certificate(
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
 * Verifies one peer chain with the Apple system trust store.
 * @param peer Peer certificate chain.
 * @param host Expected peer host.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
static int kc_tlsio_apple_verify(
    const mbedtls_x509_crt *peer,
    const char *host
) {
    CFMutableArrayRef certificates;
    CFStringRef server;
    SecPolicyRef policy;
    SecTrustRef trust;
    const mbedtls_x509_crt *item;
    int rc;

    if (peer == NULL) return KC_TLSIO_EVERIFY;
    certificates = CFArrayCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeArrayCallBacks
    );
    if (certificates == NULL) return KC_TLSIO_EVERIFY;
    for (item = peer; item != NULL; item = item->next) {
        SecCertificateRef certificate;

        certificate = kc_tlsio_apple_certificate(item);
        if (certificate == NULL) continue;
        CFArrayAppendValue(certificates, certificate);
        CFRelease(certificate);
    }
    if (CFArrayGetCount(certificates) == 0) {
        CFRelease(certificates);
        return KC_TLSIO_EVERIFY;
    }

    server = CFStringCreateWithCString(
        kCFAllocatorDefault,
        host,
        kCFStringEncodingUTF8
    );
    if (server == NULL) {
        CFRelease(certificates);
        return KC_TLSIO_EVERIFY;
    }
    policy = SecPolicyCreateSSL(1, server);
    CFRelease(server);
    if (policy == NULL) {
        CFRelease(certificates);
        return KC_TLSIO_EVERIFY;
    }

    trust = NULL;
    rc = KC_TLSIO_EVERIFY;
    if (SecTrustCreateWithCertificates(
            certificates,
            policy,
            &trust
        ) == errSecSuccess &&
        trust != NULL &&
        SecTrustEvaluateWithError(trust, NULL)) {
        rc = KC_TLSIO_OK;
    }
    if (trust != NULL) CFRelease(trust);
    CFRelease(policy);
    CFRelease(certificates);
    return rc;
}
#endif

/**
 * Configures certificate trust before the TLS handshake.
 * @param tls TLS connection.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
static int kc_tlsio_prepare_trust(kc_tlsio_t *tls) {
#if defined(_WIN32) || defined(__APPLE__)
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    return KC_TLSIO_OK;
#else
#ifdef TLSIO_BUILD_TESTS
    if (mbedtls_x509_crt_parse(
            &tls->roots,
            kc_tlsio_test_ca_pem,
            strlen((const char *)kc_tlsio_test_ca_pem) + 1U
        ) != 0) {
        return KC_TLSIO_EVERIFY;
    }
#else
    int rc;

    rc = kc_tlsio_load_roots(&tls->roots);
    if (rc != KC_TLSIO_OK) return rc;
#endif
    mbedtls_ssl_conf_ca_chain(&tls->config, &tls->roots, NULL);
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    return KC_TLSIO_OK;
#endif
}

/**
 * Verifies the peer after the TLS handshake.
 * @param tls TLS connection.
 * @param host Expected peer host.
 * @return KC_TLSIO_OK on success, or KC_TLSIO_EVERIFY on failure.
 */
static int kc_tlsio_verify_peer(kc_tlsio_t *tls, const char *host) {
#if defined(_WIN32)
    return kc_tlsio_windows_verify(
        mbedtls_ssl_get_peer_cert(&tls->ssl),
        host
    );
#elif defined(__APPLE__)
    return kc_tlsio_apple_verify(
        mbedtls_ssl_get_peer_cert(&tls->ssl),
        host
    );
#else
    (void)host;
    return mbedtls_ssl_get_verify_result(&tls->ssl) == 0U
        ? KC_TLSIO_OK
        : KC_TLSIO_EVERIFY;
#endif
}

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
    if (psa_crypto_init() != PSA_SUCCESS) return KC_TLSIO_ETLS;

    tls = (kc_tlsio_t *)calloc(1, sizeof(*tls));
    if (tls == NULL) return KC_TLSIO_ENOMEM;
    tls->socket = KC_TLSIO_BAD_SOCKET;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    mbedtls_ctr_drbg_init(&tls->random);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_x509_crt_init(&tls->roots);
    tls->tls_ready = 1;

    rc = kc_tlsio_platform_init();
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }
    tls->platform_ready = 1;

    port = options->port == 0U ? 443U : options->port;
    rc = kc_tlsio_socket_connect(options->host, port, &tls->socket);
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
    rc = kc_tlsio_prepare_trust(tls);
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
        kc_tlsio_send,
        kc_tlsio_recv,
        NULL
    );

    rc = kc_tlsio_handshake(tls);
    if (rc != KC_TLSIO_OK) {
        kc_tlsio_close(tls);
        return rc;
    }
    rc = kc_tlsio_verify_peer(tls, options->host);
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
        kc_tlsio_socket_close(tls->socket);
    }
    if (tls->platform_ready) kc_tlsio_platform_cleanup();
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
