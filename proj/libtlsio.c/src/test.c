/**
 * test.c - libtlsio public API contract tests.
 * Summary: Exercises verified TLS transport against a local server.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtlsio.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <winsock2.h>
#include <windows.h>
typedef SOCKET test_socket_t;
typedef HANDLE test_thread_t;
#define TEST_BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int test_socket_t;
typedef pthread_t test_thread_t;
#define TEST_BAD_SOCKET (-1)
#endif

#define TEST_HOST "localhost"
#define TEST_LARGE_SIZE 70000U

const unsigned char kc_tlsio_test_ca_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDETCCAfmgAwIBAgIUewod6mSAzSl1m4gck2DjeL8oioAwDQYJKoZIhvcNAQEL\n"
    "BQAwGDEWMBQGA1UEAwwNdGxzaW8gdGVzdCBDQTAeFw0yNjEwMTYxNzEwNDJaFw0z\n"
    "NjEwMTMxNzEwNDJaMBgxFjAUBgNVBAMMDXRsc2lvIHRlc3QgQ0EwggEiMA0GCSqG\n"
    "SIb3DQEBAQUAA4IBDwAwggEKAoIBAQCevsIo51tKhnv6SP33CWYgsuS4Gh/Qj7cC\n"
    "SFEEyZiVcVM+W+fXXyfQqkGsy2YTkJo+HYUf/nxAayreGJKSzwpV0wKiDA/zCmNY\n"
    "+TR26AgNSZHP1hSb6qnFa3BskoSO/TEcUjU9l1T63k9LYbncLeodkAV0AcfZz6yP\n"
    "hh1tjDvBzyrzSrpxWGROBkqJ6YRbZKwC40YcpKSmNneVVOwpx0rlpwOkyUFz9tgs\n"
    "P/8nArsAPVyC+XW1vhkcguuDDj2G5lxlKtCz3WCPH2strkAzhn+bw55MGGlVfwuy\n"
    "HhFqnUxdOF0r62/GJuHVgpyRUT5b5Yv9PeHkyro5lnRbwVgOr9sJAgMBAAGjUzBR\n"
    "MB0GA1UdDgQWBBRU8rzAD65med70ABVn2hw5ztd1RDAfBgNVHSMEGDAWgBRU8rzA\n"
    "D65med70ABVn2hw5ztd1RDAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCISj/erLOSGmGwVEi6Eo4hs+u36+G9JCojrW1u32IyFsnv10ak5woISTIl\n"
    "HKqvEAVoxMuvMh0WFg+OHKC3MFkdTbVKXf2ic0Q40x8dfCrgwriuafrYgouOEKfv\n"
    "1SkDWc2VSXVcpY0miDQpiketdU+rij5wa0MB9uSxb1bLdP7kSA3PYEiMcymhcOps\n"
    "BB9tDDatnDgpV8M+jPSGQlYlI3T8/OLuQbrdnILgYDt8T+UxycTN9hQ2gS61CDyn\n"
    "j7xyeTNvoCGyvl2ERUzazU7VXE/3wIgeQGqN2As8NMLHFDhFNz/plpXkVeKwoMAg\n"
    "VznfJJga5VUVZxuRE78bnFHIDwZp\n"
    "-----END CERTIFICATE-----\n";

static const unsigned char test_server_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDJzCCAg+gAwIBAgIUDOzScgtfy104UjGppKLRd2vF/WcwDQYJKoZIhvcNAQEL\n"
    "BQAwGDEWMBQGA1UEAwwNdGxzaW8gdGVzdCBDQTAeFw0yNjEwMTYxNzEwNDJaFw0z\n"
    "NjEwMTMxNzEwNDJaMBQxEjAQBgNVBAMMCWxvY2FsaG9zdDCCASIwDQYJKoZIhvcN\n"
    "AQEBBQADggEPADCCAQoCggEBAL4tN3DFGFHExUDce5CV5rBohKFA8ZJ7q//EhGyw\n"
    "gvCpqWAb9r5VGq/SG0Ay9NoxU18eNOVJ2Hwui8IzWsBp3ZDCIZ+PKxooM/DyBbO/\n"
    "piHYXnHAuaNy+d7z/Jj3uBnsKRrASAUwGb0xnXlC/KBt1XCKJUag/gkalRDx1vTu\n"
    "x3lg0xFOFSONLhjWoVMS0X1meSwNvindsDtgLNi8mKTruvNhR/S3dRvEQznvooXj\n"
    "2DPP/JILut7DjaxpRtgKTjhW4/kDTzOEX2NQ1cp2nQVeUKOKBVfSwUhoHjr/rGS6\n"
    "NWfWRyBuBAZHPnsTmM0+gR9cP0W0FYWS66attNyQL5JIV+sCAwEAAaNtMGswFAYD\n"
    "VR0RBA0wC4IJbG9jYWxob3N0MBMGA1UdJQQMMAoGCCsGAQUFBwMBMB0GA1UdDgQW\n"
    "BBQ30T2zXs4umPMrR+ZvV+15ZscH+zAfBgNVHSMEGDAWgBRU8rzAD65med70ABVn\n"
    "2hw5ztd1RDANBgkqhkiG9w0BAQsFAAOCAQEAj6GmphsJtOalruJxfj5beyPecZOY\n"
    "0DLLfXyFZEQN32U/rUX/DjHl8DXISWWVEEXqzP5chWnNTWsSECy/7z4s5qUAAUbI\n"
    "Kny4JOBEwO0vDceaFyZ3tp+In7MZjQ4XzsxlIpcU4skm6UJPg+XdTy0hAK2GV7/r\n"
    "/OQC3NChiU6qfBBoaQ4/sXEcl9MjJr03OBKAtvJZzWzSuzCQnoxhBu3ceutoGDFz\n"
    "eMUhE+F3WTQFZr1NX52A4ExJ29SBXe0D9xZfgS0sgGap18wgNedvHwEMorexacgu\n"
    "DVJlxh8DtXdD6K71LqkcCfdx63rPHIt4olAd1NE5qnnX+Xo81PZMNmBUPw==\n"
    "-----END CERTIFICATE-----\n";

static const unsigned char test_server_key_pem[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQC+LTdwxRhRxMVA\n"
    "3HuQleawaIShQPGSe6v/xIRssILwqalgG/a+VRqv0htAMvTaMVNfHjTlSdh8LovC\n"
    "M1rAad2QwiGfjysaKDPw8gWzv6Yh2F5xwLmjcvne8/yY97gZ7CkawEgFMBm9MZ15\n"
    "QvygbdVwiiVGoP4JGpUQ8db07sd5YNMRThUjjS4Y1qFTEtF9ZnksDb4p3bA7YCzY\n"
    "vJik67rzYUf0t3UbxEM576KF49gzz/ySC7rew42saUbYCk44VuP5A08zhF9jUNXK\n"
    "dp0FXlCjigVX0sFIaB46/6xkujVn1kcgbgQGRz57E5jNPoEfXD9FtBWFkuumrbTc\n"
    "kC+SSFfrAgMBAAECggEAGlcemdbKsVn3gi4nn9U1+v1hpV4JN43mkEndQkVJFYvs\n"
    "7ZFh9xHIMk2qaurD4Xa51++nSZIVUySKmfl379+Z3xjBrx0VeLgpyDe//29OKNuU\n"
    "P91+43sjeF8Yf5gL8+yISEVqlEFfHJvDIQ5cpDHM7BtON2lJVjnMJ2kqVIlqr85d\n"
    "XnRnyMhIhRLjR4CpOjsrNkIQllgh28BUwZhm8wIW+sXRqfTkrpemIbtM+8t/jBFN\n"
    "Bw1D+7cnuAWDrKJRBdLFaJ9ZQqZRA7DAfo7G0ZpOSFdGy2B3pVtB8Z3mxQn//doh\n"
    "Lsgvkgki88vTpQBN1UyJm2KeL06A1mzDePhZ4yQiYQKBgQDk5LapQoGxv3GtVNYA\n"
    "Fj5VawlsaPOIfRS5KkYoPfyVaqlCZixQ6Q9uUYbTegyo3E8ArfPj0H+Bal6Yle9Q\n"
    "o8lNzfPZV5UGmhYZJUDQ5efFB4PsDI9uobOX054W4dyMwcYEyLlK3ne2nuFjROFM\n"
    "0CiS53iXh9Fd4lVmlOzzT8B2ywKBgQDUsr0tkJSydvVAVgZPVeBPJVI1wXCLYvNR\n"
    "hVHn79CGCurSNfnq3M5Q2jYW9KAoGe18d1vn9+u0ehyDaguaAcc9ho+v3NI34Mo4\n"
    "07CZjLFr7KOukPLx2I5ZVu3nnFlGVqagDZQ7uQZe/eGJgsgKY1GUhS7r1BmlNJRE\n"
    "eQtSPBJfYQKBgQCQS8sHS0D9sCXW0ouSRHOJX8/yAvu8CLZewNK3ndQ00afgjw7S\n"
    "9wneTxEDEocWkMRRNlhxPsiS6l62LJdQq+BE51gZpQfywIK72WtSBg2lfUZiMcze\n"
    "Ppw8U5Du9fYW2jWeYHfHRhYMD38hGJQ722IosjmotVt8pymVYL876AP6HwKBgGaa\n"
    "KkPWoMROOhRk6vpOVLwZJsUjoSS8AN/sTgxyVMa0cX4h54LyLNw3xyvfqXput275\n"
    "LO6DW8JdvW9vMlWCUhhN1vZgsU+fMi2jgGcxxUxohLTq2ybq7hAOrQizFSWnGw3O\n"
    "PxloMbiVA70Px2uraWkZrf64+/4DlRpKwblTnRFBAoGBAOMAvKDVs0UpMopn/EWx\n"
    "ZJlV+kKIxTjeaKHIDgBSITQcIMDDPKZPruavGRO2WfL+LcE1ZeiR0ea2fArsPhG1\n"
    "Pcb/h6JhpfByqz/oOr3hePB3gXij1HoJ8fIVJTWgi4RGECogBqJz2uYehgjcnw32\n"
    "ZYUcQwI2bx2WUdEC3yPzX68s\n"
    "-----END PRIVATE KEY-----\n";

enum test_mode { TEST_TLS, TEST_PLAIN, TEST_TLS11, TEST_UNCLEAN };
typedef struct {
    test_socket_t listener;
    test_thread_t thread;
    unsigned short port;
    enum test_mode mode;
    int result;
    int version;
    char sni[64];
    unsigned char *received;
    size_t received_size;
} test_server_t;

static int case_total;
static int case_current;

/**
 * Closes one fixture socket.
 * @param value Socket to close.
 * @return void.
 */
static void socket_close(test_socket_t value) {
#ifdef _WIN32
    closesocket(value);
#else
    close(value);
#endif
}

static int socket_start(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

static void socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static int bio_send(void *ctx, const unsigned char *data, size_t size) {
    test_socket_t socket_value;
#ifdef _WIN32
    int result;
#else
    ssize_t result;
#endif

    socket_value = *(test_socket_t *)ctx;
#ifdef _WIN32
    result = send(socket_value, (const char *)data, (int)size, 0);
    return result == SOCKET_ERROR ? MBEDTLS_ERR_NET_SEND_FAILED : result;
#else
    result = send(socket_value, data, size, 0);
    return result < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)result;
#endif
}

static int bio_recv(void *ctx, unsigned char *data, size_t size) {
    test_socket_t socket_value;
#ifdef _WIN32
    int result;
#else
    ssize_t result;
#endif

    socket_value = *(test_socket_t *)ctx;
#ifdef _WIN32
    result = recv(socket_value, (char *)data, (int)size, 0);
    return result == SOCKET_ERROR ? MBEDTLS_ERR_NET_RECV_FAILED : result;
#else
    result = recv(socket_value, data, size, 0);
    return result < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : (int)result;
#endif
}

static int observe_sni(void *ctx, mbedtls_ssl_context *ssl,
    const unsigned char *name, size_t name_size) {
    test_server_t *server;
    size_t size;

    (void)ssl;
    server = (test_server_t *)ctx;
    size = name_size < sizeof(server->sni) - 1U ? name_size : sizeof(server->sni) - 1U;
    memcpy(server->sni, name, size);
    server->sni[size] = '\0';
    return 0;
}

static int tls_handshake(mbedtls_ssl_context *ssl) {
    int result;

    do {
        result = mbedtls_ssl_handshake(ssl);
    } while (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE);
    return result;
}

static int tls_write_all(mbedtls_ssl_context *ssl, const unsigned char *data,
    size_t size) {
    size_t sent;

    sent = 0U;
    while (sent < size) {
        int result;

        result = mbedtls_ssl_write(ssl, data + sent, size - sent);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (result <= 0) return 1;
        sent += (size_t)result;
    }
    return 0;
}

static void run_tls_server(test_server_t *server, test_socket_t peer) {
    static const unsigned char personalization[] = "tlsio-test";
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_ctr_drbg_context random;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
    unsigned char buffer[4096];
    unsigned char *response;
    size_t total;
    int result;

    mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config);
    mbedtls_ctr_drbg_init(&random); mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&certificate); mbedtls_pk_init(&key);
    response = NULL; server->result = 1;
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
            personalization, sizeof(personalization) - 1U) != 0 ||
        mbedtls_x509_crt_parse(&certificate, test_server_pem, sizeof(test_server_pem)) != 0 ||
        mbedtls_pk_parse_key(&key, test_server_key_pem, sizeof(test_server_key_pem),
            NULL, 0U, mbedtls_ctr_drbg_random, &random) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) goto done;
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    mbedtls_ssl_conf_sni(&config, observe_sni, server);
#ifdef MBEDTLS_SSL_VERSION_TLS1_1
    if (server->mode == TEST_TLS11) {
        mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_1);
    }
#endif
    if (mbedtls_ssl_conf_own_cert(&config, &certificate, &key) != 0 ||
        mbedtls_ssl_setup(&ssl, &config) != 0) goto done;
    mbedtls_ssl_set_bio(&ssl, &peer, bio_send, bio_recv, NULL);
    if (tls_handshake(&ssl) != 0) goto done;
    server->version = mbedtls_ssl_get_version_number(&ssl);
    if (server->mode == TEST_UNCLEAN) { server->result = 0; goto done; }
    total = 0U;
    while (total < TEST_LARGE_SIZE + 2U) {
        result = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (result <= 0 || total + (size_t)result > TEST_LARGE_SIZE + 2U) goto done;
        memcpy(server->received + total, buffer, (size_t)result);
        total += (size_t)result;
    }
    server->received_size = total;
    response = (unsigned char *)malloc(TEST_LARGE_SIZE);
    if (response == NULL) goto done;
    for (total = 0U; total < TEST_LARGE_SIZE; total++) response[total] = (unsigned char)((total * 37U + 11U) & 255U);
    if (tls_write_all(&ssl, response, TEST_LARGE_SIZE) != 0) goto done;
    mbedtls_ssl_close_notify(&ssl);
    server->result = 0;
done:
    free(response); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config);
    mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&certificate); mbedtls_pk_free(&key);
}

#ifdef _WIN32
static unsigned __stdcall server_thread(void *ctx)
#else
static void *server_thread(void *ctx)
#endif
{
    test_server_t *server;
    test_socket_t peer;

    server = (test_server_t *)ctx;
    peer = accept(server->listener, NULL, NULL);
    if (peer == TEST_BAD_SOCKET) server->result = 1;
    else if (server->mode == TEST_PLAIN) { server->result = 0; socket_close(peer); }
    else { run_tls_server(server, peer); socket_close(peer); }
    socket_close(server->listener);
#ifdef _WIN32
    return 0U;
#else
    return NULL;
#endif
}

static int server_start(test_server_t *server, enum test_mode mode) {
    struct sockaddr_in address;
#ifdef _WIN32
    int length;
#else
    socklen_t length;
#endif
    int enabled;

    memset(server, 0, sizeof(*server));
    server->mode = mode; server->listener = socket(AF_INET, SOCK_STREAM, 0);
    server->received = (unsigned char *)malloc(TEST_LARGE_SIZE + 2U);
    if (server->listener == TEST_BAD_SOCKET || server->received == NULL) return 1;
    enabled = 1;
    setsockopt(server->listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server->listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(server->listener, 1) != 0) return 1;
    length = sizeof(address);
    if (getsockname(server->listener, (struct sockaddr *)&address, &length) != 0) return 1;
    server->port = ntohs(address.sin_port);
#ifdef _WIN32
    server->thread = (HANDLE)_beginthreadex(NULL, 0U, server_thread, server, 0U, NULL);
    return server->thread == NULL ? 1 : 0;
#else
    return pthread_create(&server->thread, NULL, server_thread, server) == 0 ? 0 : 1;
#endif
}

static void server_join(test_server_t *server) {
#ifdef _WIN32
    WaitForSingleObject(server->thread, INFINITE); CloseHandle(server->thread);
#else
    pthread_join(server->thread, NULL);
#endif
}

static int expect(const char *label, int value) {
    if (!value) printf("[FAIL] %s\n", label);
    return value ? 0 : 1;
}

static int connect_local(kc_tlsio_t **tls, unsigned short port, const char *host) {
    kc_tlsio_options_t options;

    options.host = host; options.port = port;
    return kc_tlsio_connect(tls, &options);
}

static void print_case(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", case_current, case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static int case_validation(void) {
    kc_tlsio_options_t options;
    kc_tlsio_t *tls;
    void *data;
    size_t size;
    int fail;

    memset(&options, 0, sizeof(options)); tls = (kc_tlsio_t *)1;
    data = (void *)1; size = 1U; fail = 0;
    fail += expect("connect NULL output", kc_tlsio_connect(NULL, &options) == KC_TLSIO_EINVAL);
    fail += expect("connect NULL options clears output", kc_tlsio_connect(&tls, NULL) == KC_TLSIO_EINVAL && tls == NULL);
    options.host = "";
    fail += expect("connect empty host", kc_tlsio_connect(&tls, &options) == KC_TLSIO_EINVAL && tls == NULL);
    fail += expect("write NULL handle", kc_tlsio_write(NULL, "x", 1U) == KC_TLSIO_EINVAL);
    fail += expect("write NULL data", kc_tlsio_write(NULL, NULL, 1U) == KC_TLSIO_EINVAL);
    fail += expect("read NULL outputs", kc_tlsio_read(NULL, NULL, &size) == KC_TLSIO_EINVAL);
    fail += expect("read clears outputs", kc_tlsio_read(NULL, &data, &size) == KC_TLSIO_EINVAL && data == NULL && size == 0U);
    kc_tlsio_close(NULL); kc_tlsio_free(NULL);
    print_case(fail, "validation", "covers invalid public arguments and output clearing");
    return fail != 0;
}

static int case_exchange(void) {
    test_server_t server;
    kc_tlsio_t *tls;
    unsigned char *payload;
    unsigned char *response;
    size_t size;
    int fail;

    fail = 0; tls = NULL; payload = (unsigned char *)malloc(TEST_LARGE_SIZE);
    response = (unsigned char *)malloc(TEST_LARGE_SIZE);
    if (payload == NULL || response == NULL || server_start(&server, TEST_TLS) != 0) return 1;
    for (size = 0U; size < TEST_LARGE_SIZE; size++) payload[size] = (unsigned char)((size * 19U + 3U) & 255U);
    {
        int result;

        result = connect_local(&tls, server.port, TEST_HOST);
        if (result != KC_TLSIO_OK) printf("[FAIL] TLS connect status %d\n", result);
        fail += expect("authenticated TLS connection", result == KC_TLSIO_OK);
    }
    fail += expect("valid zero write", kc_tlsio_write(tls, NULL, 0U) == KC_TLSIO_OK);
    fail += expect("first sequential write", kc_tlsio_write(tls, "A", 1U) == KC_TLSIO_OK);
    fail += expect("second sequential write", kc_tlsio_write(tls, "B", 1U) == KC_TLSIO_OK);
    fail += expect("large binary write", kc_tlsio_write(tls, payload, TEST_LARGE_SIZE) == KC_TLSIO_OK);
    size = 0U;
    while (tls != NULL && size < TEST_LARGE_SIZE) {
        void *data;
        size_t part;
        int result;

        data = NULL; part = 0U; result = kc_tlsio_read(tls, &data, &part);
        if (result != KC_TLSIO_OK || part > TEST_LARGE_SIZE - size) { fail++; kc_tlsio_free(data); break; }
        memcpy(response + size, data, part); size += part; kc_tlsio_free(data);
    }
    fail += expect("large response exact size", size == TEST_LARGE_SIZE);
    for (size = 0U; size < TEST_LARGE_SIZE; size++) if (response[size] != (unsigned char)((size * 37U + 11U) & 255U)) { fail += expect("large response bytes", 0); break; }
    { void *data = (void *)1; size_t part = 1U; fail += expect("close notify EOF", kc_tlsio_read(tls, &data, &part) == KC_TLSIO_EOF && data == NULL && part == 0U); }
    kc_tlsio_close(tls); server_join(&server);
    fail += expect("server exact ordered bytes", server.received_size == TEST_LARGE_SIZE + 2U && server.received[0] == 'A' && server.received[1] == 'B' && memcmp(server.received + 2U, payload, TEST_LARGE_SIZE) == 0);
    fail += expect("SNI observed", strcmp(server.sni, TEST_HOST) == 0);
    fail += expect("TLS 1.2 negotiated", server.version >= MBEDTLS_SSL_VERSION_TLS1_2);
    fail += expect("server completed", server.result == 0);
    free(server.received); free(payload); free(response);
    print_case(fail, "tls_exchange", "covers SNI, verified binary I/O, records, and close_notify");
    return fail != 0;
}

static int case_failures(void) {
    test_server_t server;
    kc_tlsio_t *tls;
    int fail;

    fail = 0; tls = (kc_tlsio_t *)1;
    fail += expect("unused loopback port", connect_local(&tls, 1U, TEST_HOST) == KC_TLSIO_ENET && tls == NULL);
    if (server_start(&server, TEST_PLAIN) == 0) { tls = (kc_tlsio_t *)1; fail += expect("plain TCP handshake failure", connect_local(&tls, server.port, TEST_HOST) == KC_TLSIO_ETLS && tls == NULL); server_join(&server); free(server.received); } else fail++;
    if (server_start(&server, TEST_TLS) == 0) { tls = (kc_tlsio_t *)1; fail += expect("hostname verification failure", connect_local(&tls, server.port, "127.0.0.1") == KC_TLSIO_EVERIFY && tls == NULL); server_join(&server); free(server.received); } else fail++;
#ifdef MBEDTLS_SSL_VERSION_TLS1_1
    if (server_start(&server, TEST_TLS11) == 0) { tls = (kc_tlsio_t *)1; fail += expect("TLS 1.1 peer rejected", connect_local(&tls, server.port, TEST_HOST) == KC_TLSIO_ETLS && tls == NULL); server_join(&server); free(server.received); } else fail++;
#endif
    print_case(fail, "failures", "rejects network, plaintext, hostname, and pre-TLS-1.2 peers");
    return fail != 0;
}

static int case_metadata(void) {
    static const int codes[] = {KC_TLSIO_OK, KC_TLSIO_EOF, KC_TLSIO_EINVAL, KC_TLSIO_ENET, KC_TLSIO_ETLS, KC_TLSIO_EVERIFY, KC_TLSIO_ENOMEM};
    size_t index;
    int fail;

    fail = 0;
    for (index = 0U; index < sizeof(codes) / sizeof(codes[0]); index++) fail += expect("status string", kc_tlsio_strerror(codes[index])[0] != '\0');
    fail += expect("unknown status", strcmp(kc_tlsio_strerror(9999), "unknown tlsio status") == 0);
    fail += expect("nonzero version", kc_tlsio_version() != 0U);
    print_case(fail, "metadata", "covers all public statuses and generated version");
    return fail != 0;
}

int main(int argc, char **argv) {
    int failed;

    if (argc != 2 || strcmp(argv[1], "all") != 0) return 2;
    if (socket_start() != 0) return 1;
    case_total = 4; case_current = 0; failed = 0;
    case_current++; failed += case_validation();
    case_current++; failed += case_exchange();
    case_current++; failed += case_failures();
    case_current++; failed += case_metadata();
    socket_stop();
    printf("\n%d passed, %d failed\n", case_total - failed, failed);
    return failed == 0 ? 0 : 1;
}
