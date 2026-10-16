/**
 * trust.c - Native tlsio certificate trust.
 * Summary: Connects Mbed TLS peer certificates to platform trust stores.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
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
        if (leaf == NULL) leaf = CertDuplicateCertificateContext(certificate);
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
int kc_tlsio_internal_prepare_trust(kc_tlsio_t *tls) {
#if defined(_WIN32) || defined(__APPLE__)
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    return KC_TLSIO_OK;
#else
    int rc;

    rc = kc_tlsio_load_roots(&tls->roots);
    if (rc != KC_TLSIO_OK) return rc;
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
int kc_tlsio_internal_verify_peer(kc_tlsio_t *tls, const char *host) {
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
