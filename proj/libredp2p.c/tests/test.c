/**
 * redp2p.c - libredp2p public API contract tests.
 * Summary: Exercises the libredp2p contract through 17 grouped functional
 * and integration cases covering the public capability API, protocol,
 * index lifecycle, registration, and peer transport.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p.h"
#include "libredp2p-core.h"
#include "monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

#define TEST_HOST "127.0.0.1"
#define TEST_TCP_CLIENTS 4
#define TEST_TCP_LARGE_SIZE (80U * 1024U + 333U)
#define TEST_HTTP_LINE_MAX 256
#define TEST_HTTP_HEADERS_MAX 32
#define TEST_HTTP_CONNECTIONS 128

#ifdef _WIN32
typedef HANDLE test_thread_t;
typedef SOCKET test_socket_t;
typedef int test_socklen_t;
#define TEST_SOCKET_INVALID INVALID_SOCKET
#else
typedef pthread_t test_thread_t;
typedef int test_socket_t;
typedef socklen_t test_socklen_t;
#define TEST_SOCKET_INVALID -1
#endif

typedef struct {
    redp2p_t *ctx;
    unsigned short port;
    int result;
    test_thread_t thread;
} test_index_t;

typedef struct {
    redp2p_t *ctx;
    const char *host;
    unsigned short index_port;
    const char *id;
    unsigned short bind_port;
    int protocol;
    const char *pass;
    const char *state_dir;
    _Atomic int result;
    test_thread_t thread;
} test_publisher_t;

typedef struct {
    redp2p_t *ctx;
    const char *host;
    unsigned short index_port;
    const char *self_id;
    const char *target_id;
    unsigned short bind_port;
    int protocol;
    _Atomic int result;
    test_thread_t thread;
} test_consumer_t;

typedef struct {
    test_socket_t fd;
    unsigned short port;
    _Atomic int stop;
    test_thread_t thread;
} test_udp_echo_t;

typedef struct {
    test_socket_t fd;
    test_socket_t clients[TEST_TCP_CLIENTS];
    unsigned short port;
    _Atomic int stop;
    test_thread_t thread;
} test_tcp_echo_t;

typedef struct {
    test_socket_t fd;
    unsigned short port;
    test_thread_t thread;
} test_control_stub_t;

typedef struct {
    test_socket_t fd;
    unsigned short port;
    _Atomic int stop;
    _Atomic int register_count;
    _Atomic int heartbeat_count;
    _Atomic int dropped_heartbeat;
    _Atomic uint64_t first_heartbeat_sequence;
    _Atomic uint64_t second_heartbeat_sequence;
    char register_body[4096];
    test_thread_t thread;
} test_publisher_control_stub_t;

typedef struct {
    char ids[8][REDP2P_ID_MAX + 1];
    size_t count;
} test_publishers_t;

static char test_home_path[512];
static char test_port_reservation[512];
static const char *test_case_name;

typedef struct {
    _Atomic int received;
    unsigned char data[256];
    size_t size;
} test_direct_receive_t;

static test_direct_receive_t test_direct_receive;
static _Atomic int test_direct_connected;

/**
 * Records one established publisher channel.
 * @param client Established publisher client.
 * @param userdata Unused.
 * @return None.
 */
static void test_direct_pub_connect(kc_redp2p_client_t *client, void *userdata)
{
    (void)userdata;
    if (!client) return;
    atomic_store(&test_direct_connected, 1);
    (void)kc_redp2p_client_respond(client, "ready", 5);
}

/**
 * Echoes direct publisher data back through the stable client capability.
 * @param input Publisher input.
 * @param userdata Unused.
 * @return None.
 */
static void test_direct_pub_receive(const kc_redp2p_pub_input_t *input,
    void *userdata)
{
    (void)userdata;
    if (!input || !input->client) return;
    (void)kc_redp2p_client_respond(input->client, input->data, input->size);
}

/**
 * Captures one direct consumer receive event.
 * @param data Received bytes.
 * @param size Byte count.
 * @param userdata Unused.
 * @return None.
 */
static void test_direct_con_receive(const void *data, size_t size,
    void *userdata)
{
    (void)userdata;
    if (!data || size > sizeof(test_direct_receive.data)) return;
    memcpy(test_direct_receive.data, data, size);
    test_direct_receive.size = size;
    atomic_store(&test_direct_receive.received, 1);
}

static const unsigned char REDP2P_CHALLENGE_DOMAIN[] = "REDP2P-CHALLENGE";
static const unsigned char REDP2P_POW_DOMAIN[] = "REDP2P-POW";
static const unsigned char REDP2P_REGISTER_DOMAIN[] = "REDP2P-REGISTER";
_Static_assert(sizeof(REDP2P_CHALLENGE_DOMAIN) - 1 == 16,
    "Challenge domain must not include a NUL byte");
_Static_assert(sizeof(REDP2P_POW_DOMAIN) - 1 == 10,
    "PoW domain must not include a NUL byte");
_Static_assert(sizeof(REDP2P_REGISTER_DOMAIN) - 1 == 15,
    "Register domain must not include a NUL byte");

static int test_socket_close(test_socket_t fd);
static int test_http_request(unsigned short port, const char *body,
    size_t body_len, int expected_status, const char *expected_fragment);
static int test_http_response(unsigned short port, const char *body,
    size_t body_len, char *response, size_t response_cap);
static int test_http_raw_status(unsigned short port, const char *request,
    size_t request_len);
static test_socket_t test_tcp_connect(unsigned short port);
static int test_socket_timeout(test_socket_t fd, unsigned int ms);
static int test_socket_send_all(test_socket_t fd, const unsigned char *data,
    size_t len);
static unsigned short test_port_base(void);
static int test_wait_port(unsigned short port, int open);
#ifdef _WIN32
static DWORD WINAPI test_index_main(void *arg);
static int test_thread_start(test_thread_t *thread, DWORD (WINAPI *run)(void *),
    void *arg);
#else
static void *test_index_main(void *arg);
static int test_thread_start(test_thread_t *thread, void *(*run)(void *),
    void *arg);
#endif
static int test_index_start_configured(test_index_t *index,
    unsigned short port, size_t seats, const char *vip, const char *pass);
static int test_index_stop(test_index_t *index);

typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char block[64];
} test_sha256_t;

static const uint32_t test_sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/**
 * Rotates one SHA-256 word right.
 * @param value Input word.
 * @param bits Rotation count.
 * @return Rotated word.
 */
static uint32_t test_ror(uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

/**
 * Transforms one SHA-256 block.
 * @param context Hash context.
 * @return None.
 */
static void test_sha256_block(test_sha256_t *context) {
    uint32_t words[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++) words[i] = ((uint32_t)context->block[i * 4] << 24) |
        ((uint32_t)context->block[i * 4 + 1] << 16) |
        ((uint32_t)context->block[i * 4 + 2] << 8) | context->block[i * 4 + 3];
    for (i = 16; i < 64; i++) words[i] = (test_ror(words[i - 2], 17) ^
        test_ror(words[i - 2], 19) ^ (words[i - 2] >> 10)) + words[i - 7] +
        (test_ror(words[i - 15], 7) ^ test_ror(words[i - 15], 18) ^
        (words[i - 15] >> 3)) + words[i - 16];
    a = context->state[0]; b = context->state[1]; c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5]; g = context->state[6]; h = context->state[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (test_ror(e, 6) ^ test_ror(e, 11) ^ test_ror(e, 25)) +
            ((e & f) ^ (~e & g)) + test_sha256_k[i] + words[i];
        t2 = (test_ror(a, 2) ^ test_ror(a, 13) ^ test_ror(a, 22)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
}

/**
 * Computes SHA-256 over bounded test bytes.
 * @param data Input bytes.
 * @param len Input length.
 * @param hash Output digest.
 * @return None.
 */
static void test_sha256(const unsigned char *data, size_t len,
    unsigned char hash[32])
{
    test_sha256_t context;
    uint64_t bits;
    size_t i;

    context.state[0]=0x6a09e667; context.state[1]=0xbb67ae85; context.state[2]=0x3c6ef372; context.state[3]=0xa54ff53a;
    context.state[4]=0x510e527f; context.state[5]=0x9b05688c; context.state[6]=0x1f83d9ab; context.state[7]=0x5be0cd19;
    context.count = 0;
    for (i = 0; i < len; i++) { context.block[context.count++ & 63] = data[i]; if ((context.count & 63) == 0) test_sha256_block(&context); }
    bits = context.count * 8; i = (size_t)(context.count & 63); context.block[i++] = 0x80;
    if (i > 56) { while (i < 64) context.block[i++] = 0; test_sha256_block(&context); i = 0; }
    while (i < 56) context.block[i++] = 0;
    for (i = 0; i < 8; i++) context.block[56 + i] = (unsigned char)(bits >> (56 - i * 8));
    test_sha256_block(&context);
    for (i = 0; i < 8; i++) { hash[i * 4] = (unsigned char)(context.state[i] >> 24); hash[i * 4 + 1] = (unsigned char)(context.state[i] >> 16); hash[i * 4 + 2] = (unsigned char)(context.state[i] >> 8); hash[i * 4 + 3] = (unsigned char)context.state[i]; }
}

/**
 * Computes HMAC-SHA-256 for bounded test messages.
 * @param key Key bytes.
 * @param key_len Key length.
 * @param message Message bytes.
 * @param message_len Message length.
 * @param hash Output digest.
 * @return None.
 */
static void test_hmac_sha256(const unsigned char *key, size_t key_len,
    const unsigned char *message, size_t message_len, unsigned char hash[32])
{
    unsigned char block[64], inner[32], buffer[512];
    size_t i;

    memset(block, 0, sizeof(block));
    if (key_len > sizeof(block)) test_sha256(key, key_len, block);
    else memcpy(block, key, key_len);
    for (i = 0; i < sizeof(block); i++) buffer[i] = block[i] ^ 0x36;
    memcpy(buffer + sizeof(block), message, message_len);
    test_sha256(buffer, sizeof(block) + message_len, inner);
    for (i = 0; i < sizeof(block); i++) buffer[i] = block[i] ^ 0x5c;
    memcpy(buffer + sizeof(block), inner, sizeof(inner));
    test_sha256(buffer, sizeof(block) + sizeof(inner), hash);
}

/**
 * Extracts one JSON string field from a bounded response.
 * @param response HTTP response text.
 * @param name Field name.
 * @param out Output string.
 * @param cap Output capacity.
 * @return 1 on success, 0 on malformed input.
 */
static int test_json_string(const char *response, const char *name, char *out,
    size_t cap)
{
    char pattern[64];
    const char *start;
    const char *end;
    size_t len;

    if (snprintf(pattern, sizeof(pattern), "\"%s\":\"", name) < 0) return 0;
    start = strstr(response, pattern);
    if (!start) return 0;
    start += strlen(pattern); end = strchr(start, '\"');
    if (!end || (len = (size_t)(end - start)) >= cap) return 0;
    memcpy(out, start, len); out[len] = '\0'; return 1;
}

/**
 * Extracts one JSON unsigned integer field from a bounded response.
 * @param response HTTP response text.
 * @param name Field name.
 * @param out Output value.
 * @return 1 on success, 0 on malformed input.
 */
static int test_json_u64(const char *response, const char *name, uint64_t *out)
{
    char pattern[64];
    char *end;
    const char *start;
    unsigned long long value;

    if (snprintf(pattern, sizeof(pattern), "\"%s\":", name) < 0) return 0;
    start = strstr(response, pattern);
    if (!start) return 0;
    value = strtoull(start + strlen(pattern), &end, 10);
    if (end == start + strlen(pattern)) return 0;
    *out = (uint64_t)value; return 1;
}

/**
 * Decodes exact hexadecimal bytes.
 * @param text Hex text.
 * @param out Output bytes.
 * @param len Required byte length.
 * @return 1 on success, 0 on malformed input.
 */
static int test_hex_decode(const char *text, unsigned char *out, size_t len)
{
    size_t i;

    if (!text || strlen(text) != len * 2) return 0;
    for (i = 0; i < len; i++) {
        unsigned int value;
        if (sscanf(text + i * 2, "%2x", &value) != 1) return 0;
        out[i] = (unsigned char)value;
    }
    return 1;
}

/**
 * Encodes bytes as lowercase hexadecimal text.
 * @param input Input bytes.
 * @param len Input length.
 * @param out Output text.
 * @return None.
 */
static void test_hex_encode(const unsigned char *input, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < len; i++) { out[i * 2] = hex[input[i] >> 4]; out[i * 2 + 1] = hex[input[i] & 15]; }
    out[len * 2] = '\0';
}

static int expect_string(const char *name, const char *expected,
    const char *actual);
static int expect_int(const char *name, int expected, int actual);
static int expect_true(const char *name, int condition);
static void case_result(int fail, const char *name, const char *detail);
static char test_register_index_pkey_hex[65];

/**
 * Protects a test control secret with a challenge index public key.
 * @param index_pkey_hex Challenge index public key hex.
 * @param nonce_hex Challenge nonce hex.
 * @param secret Original 16-byte control secret.
 * @param publisher_pkey_hex Output publisher public key hex.
 * @param encrypted_secret_hex Output ciphertext and tag hex.
 * @return 0 on success, 1 on failure.
 */
static int test_protect_registration_secret(const char *index_pkey_hex,
    const char *nonce_hex, const void *secret, char publisher_pkey_hex[65],
    char encrypted_secret_hex[65])
{
    unsigned char index_pkey[32], nonce[32], publisher_skey[32];
    unsigned char publisher_pkey[32], shared[32], key[32], wire[32];
    size_t used;
    int i;

    memset(index_pkey, 0, sizeof(index_pkey));
    memset(nonce, 0, sizeof(nonce));
    memset(publisher_skey, 0, sizeof(publisher_skey));
    memset(shared, 0, sizeof(shared));
    memset(key, 0, sizeof(key));
    memset(wire, 0, sizeof(wire));
    if (!index_pkey_hex || !nonce_hex || !secret || !test_hex_decode(
        index_pkey_hex, index_pkey,
        sizeof(index_pkey)) || !test_hex_decode(nonce_hex, nonce,
        sizeof(nonce))) return 1;
    for (i = 0; i < 32; i++) publisher_skey[i] = (unsigned char)(i + 1);
    crypto_x25519_public_key(publisher_pkey, publisher_skey);
    crypto_x25519(shared, publisher_skey, index_pkey);
    for (i = 0; i < 32; i++) if (shared[i] != 0) break;
    if (i == 32) return 1;
    used = 0;
    {
        unsigned char transcript[sizeof("REDP2P-REGISTER-SECRET") - 1 + 96];

        memcpy(transcript + used, "REDP2P-REGISTER-SECRET",
            sizeof("REDP2P-REGISTER-SECRET") - 1);
        used += sizeof("REDP2P-REGISTER-SECRET") - 1;
        memcpy(transcript + used, nonce, 32); used += 32;
        memcpy(transcript + used, index_pkey, 32); used += 32;
        memcpy(transcript + used, publisher_pkey, 32); used += 32;
        test_hmac_sha256(shared, sizeof(shared), transcript, used, key);
        crypto_wipe(transcript, sizeof(transcript));
    }
    crypto_aead_lock(wire, wire + REDP2P_KEY_SZ, key, nonce, NULL, 0,
        secret, REDP2P_KEY_SZ);
    test_hex_encode(publisher_pkey, sizeof(publisher_pkey), publisher_pkey_hex);
    test_hex_encode(wire, sizeof(wire), encrypted_secret_hex);
    crypto_wipe(index_pkey, sizeof(index_pkey));
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(publisher_skey, sizeof(publisher_skey));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(key, sizeof(key));
    crypto_wipe(wire, sizeof(wire));
    return 0;
}

/**
 * Registers one fixed host candidate through a real authenticated challenge.
 * @param port Index port.
 * @param id Publisher id.
 * @param secret Publisher secret.
 * @param udp_port Publisher port.
 * @return 0 on success, 1 on failure.
 */
static int test_register_publisher(unsigned short port, const char *id,
    const char *secret, unsigned short udp_port)
{
    char response[4096], nonce_hex[65], mac_hex[65], pkey_hex[65], proof_hex[65],
        publisher_pkey_hex[65], encrypted_secret_hex[65], body[2048];
    unsigned char nonce[32], solution[8], hash[32], message[384];
    uint64_t issued_at, expires_at;
    size_t used, id_len;
    int n;

    n = snprintf(body, sizeof(body), "{\"op\":\"challenge\",\"id\":\"%s\"}", id);
    if (n < 0 || (size_t)n >= sizeof(body) || test_http_response(port, body,
        (size_t)n, response, sizeof(response)) != 0 ||
        !test_json_string(response, "nonce", nonce_hex, sizeof(nonce_hex)) ||
        !test_json_string(response, "mac", mac_hex, sizeof(mac_hex)) ||
        !test_json_string(response, "pkey", pkey_hex, sizeof(pkey_hex)) ||
        !test_json_u64(response, "issued_at", &issued_at) ||
        !test_json_u64(response, "expires_at", &expires_at) ||
        !test_hex_decode(nonce_hex, nonce, sizeof(nonce))) return 1;
    memset(solution, 0, sizeof(solution)); id_len = strlen(id); used = 0;
    memcpy(message + used, REDP2P_REGISTER_DOMAIN, sizeof(REDP2P_REGISTER_DOMAIN) - 1); used += sizeof(REDP2P_REGISTER_DOMAIN) - 1;
    memcpy(message + used, nonce, 32); used += 32;
    for (int shift = 56; shift >= 0; shift -= 8) message[used++] = (unsigned char)(issued_at >> shift);
    for (int shift = 56; shift >= 0; shift -= 8) message[used++] = (unsigned char)(expires_at >> shift);
    message[used++] = (unsigned char)(id_len >> 8); message[used++] = (unsigned char)id_len;
    memcpy(message + used, id, id_len); used += id_len;
    message[used++] = 0; message[used++] = REDP2P_KEY_SZ;
    memcpy(message + used, secret, REDP2P_KEY_SZ); used += REDP2P_KEY_SZ;
    message[used++] = 2; message[used++] = (unsigned char)(udp_port >> 8); message[used++] = (unsigned char)udp_port;
    message[used++] = 1; message[used++] = 1; message[used++] = 4;
    message[used++] = 192; message[used++] = 0; message[used++] = 2; message[used++] = 1;
    message[used++] = (unsigned char)(udp_port >> 8); message[used++] = (unsigned char)udp_port;
    memcpy(message + used, solution, sizeof(solution)); used += sizeof(solution);
    test_hmac_sha256((const unsigned char *)secret, REDP2P_KEY_SZ, message, used, hash);
    test_hex_encode(hash, sizeof(hash), proof_hex);
    if (test_protect_registration_secret(pkey_hex, nonce_hex, secret,
        publisher_pkey_hex, encrypted_secret_hex) != 0) return 1;
    n = snprintf(body, sizeof(body), "{\"op\":\"register\",\"id\":\"%s\",\"nonce\":\"%s\",\"issued_at\":%llu,\"expires_at\":%llu,\"mac\":\"%s\",\"pkey\":\"%s\",\"secret\":\"%s\",\"proto\":1,\"udp_port\":%u,\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":%u}],\"pow_solution\":\"0000000000000000\",\"proof\":\"%s\"}", id, nonce_hex, (unsigned long long)issued_at, (unsigned long long)expires_at, mac_hex, publisher_pkey_hex, encrypted_secret_hex, (unsigned)udp_port, (unsigned)udp_port, proof_hex);
    if (n >= 0 && (size_t)n < sizeof(body) && (strstr(body, secret) ||
        !strstr(body, "\"pkey\":\"") || strlen(encrypted_secret_hex) != 64))
        return 1;
    return n < 0 || (size_t)n >= sizeof(body) ? 1 : test_http_request(port, body,
        (size_t)n, 200, "\"ok\":true");
}

/**
 * Fetches one fresh challenge and computes the reference registration proof.
 * The reference state is one host candidate at 192.0.2.1:udp_port with
 * protocol 1 (TCP) and an all-zero pow solution.
 * @param port Index port.
 * @param id Publisher id.
 * @param secret Publisher secret.
 * @param udp_port Publisher port.
 * @param nonce_hex Output challenge nonce hex.
 * @param mac_hex Output challenge mac hex.
 * @param proof_hex Output registration proof hex.
 * @param issued_at Output challenge issued_at.
 * @param expires_at Output challenge expires_at.
 * @return 0 on success, 1 on failure.
 */
static int test_register_prepare(unsigned short port, const char *id,
    const char *secret, unsigned short udp_port, char *nonce_hex,
    char *mac_hex, char *proof_hex, uint64_t *issued_at, uint64_t *expires_at)
{
    char body[2048], response[4096];
    unsigned char nonce[32], solution[8], hash[32], message[384];
    size_t used, id_len;
    int n;

    n = snprintf(body, sizeof(body), "{\"op\":\"challenge\",\"id\":\"%s\"}",
        id);
    if (n < 0 || (size_t)n >= sizeof(body) ||
        test_http_response(port, body, (size_t)n, response, sizeof(response)) !=
        0 || !test_json_string(response, "nonce", nonce_hex, 65) ||
        !test_json_string(response, "mac", mac_hex, 65) ||
        !test_json_string(response, "pkey", test_register_index_pkey_hex,
            sizeof(test_register_index_pkey_hex)) ||
        !test_json_u64(response, "issued_at", issued_at) ||
        !test_json_u64(response, "expires_at", expires_at) ||
        !test_hex_decode(nonce_hex, nonce, sizeof(nonce))) return 1;
    memset(solution, 0, sizeof(solution)); id_len = strlen(id); used = 0;
    memcpy(message + used, REDP2P_REGISTER_DOMAIN,
        sizeof(REDP2P_REGISTER_DOMAIN) - 1);
    used += sizeof(REDP2P_REGISTER_DOMAIN) - 1;
    memcpy(message + used, nonce, sizeof(nonce)); used += sizeof(nonce);
    for (int shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(*issued_at >> shift);
    for (int shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(*expires_at >> shift);
    message[used++] = (unsigned char)(id_len >> 8);
    message[used++] = (unsigned char)id_len;
    memcpy(message + used, id, id_len); used += id_len;
    message[used++] = 0; message[used++] = REDP2P_KEY_SZ;
    memcpy(message + used, secret, REDP2P_KEY_SZ); used += REDP2P_KEY_SZ;
    message[used++] = 2; message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    message[used++] = 1; message[used++] = 1; message[used++] = 4;
    message[used++] = 192; message[used++] = 0;
    message[used++] = 2; message[used++] = 1;
    message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    memcpy(message + used, solution, sizeof(solution)); used += sizeof(solution);
    test_hmac_sha256((const unsigned char *)secret, REDP2P_KEY_SZ, message,
        used, hash);
    test_hex_encode(hash, sizeof(hash), proof_hex);
    return 0;
}

/**
 * Computes the admission proof for a test registration with one host candidate.
 * @return 0 on success, 1 on malformed input.
 */
static int test_register_admission_proof(const char *id, const char *secret,
    unsigned short udp_port, const char *nonce_hex, uint64_t issued_at,
    uint64_t expires_at, const char *password, char access_proof[65])
{
    unsigned char nonce[32], message[384], input[sizeof("REDP2P-ADMISSION-v1") - 1 + 384], hash[32];
    size_t used = 0, id_len = strlen(id);
    int shift;

    if (!test_hex_decode(nonce_hex, nonce, sizeof(nonce))) return 1;
    memcpy(message + used, REDP2P_REGISTER_DOMAIN,
        sizeof(REDP2P_REGISTER_DOMAIN) - 1);
    used += sizeof(REDP2P_REGISTER_DOMAIN) - 1;
    memcpy(message + used, nonce, sizeof(nonce)); used += sizeof(nonce);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(issued_at >> shift);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(expires_at >> shift);
    message[used++] = (unsigned char)(id_len >> 8);
    message[used++] = (unsigned char)id_len;
    memcpy(message + used, id, id_len); used += id_len;
    message[used++] = 0; message[used++] = REDP2P_KEY_SZ;
    memcpy(message + used, secret, REDP2P_KEY_SZ); used += REDP2P_KEY_SZ;
    message[used++] = 2; message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    message[used++] = 1; message[used++] = 1; message[used++] = 4;
    message[used++] = 192; message[used++] = 0; message[used++] = 2;
    message[used++] = 1; message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    memset(message + used, 0, 8); used += 8;
    memcpy(input, "REDP2P-ADMISSION-v1", sizeof("REDP2P-ADMISSION-v1") - 1);
    memcpy(input + sizeof("REDP2P-ADMISSION-v1") - 1, message, used);
    test_hmac_sha256((const unsigned char *)password, strlen(password), input,
        sizeof("REDP2P-ADMISSION-v1") - 1 + used, hash);
    test_hex_encode(hash, sizeof(hash), access_proof);
    return 0;
}

/**
 * Sends one registration request and checks the HTTP response.
 * @param port Index port.
 * @param id Publisher id.
 * @param nonce_hex Challenge nonce hex.
 * @param issued_at Challenge issued_at.
 * @param expires_at Challenge expires_at.
 * @param mac_hex Challenge mac hex.
 * @param secret Publisher secret hex.
 * @param proto Protocol value.
 * @param udp_port Publisher port.
 * @param candidates JSON candidate array text.
 * @param pow_solution Hex solution.
 * @param proof_hex Registration proof hex.
 * @param access_proof Optional admission proof hex.
 * @param expected_status Expected HTTP status.
 * @param expected_fragment Expected response body fragment.
 * @return 0 on success, 1 on failure.
 */
static int test_register_send(unsigned short port, const char *id,
    const char *nonce_hex, uint64_t issued_at, uint64_t expires_at,
    const char *mac_hex, const char *secret, int proto, unsigned int udp_port,
    const char *candidates, const char *pow_solution_hex,
    const char *proof_hex, const char *access_proof, int expected_status,
    const char *expected_fragment)
{
    char publisher_pkey_hex[65], encrypted_secret_hex[65], body[2048];
    int n;

    if (test_protect_registration_secret(test_register_index_pkey_hex,
        nonce_hex, secret, publisher_pkey_hex, encrypted_secret_hex) != 0)
        return 1;
    n = snprintf(body, sizeof(body),
        "{\"op\":\"register\",\"id\":\"%s\",\"nonce\":\"%s\",\"issued_at\":%llu,"
        "\"expires_at\":%llu,\"mac\":\"%s\",\"pkey\":\"%s\",\"secret\":\"%s\",\"proto\":%d,"
        "\"udp_port\":%u,\"candidates\":%s,\"pow_solution\":\"%s\","
        "\"proof\":\"%s\"%s%s%s}", id, nonce_hex, (unsigned long long)issued_at,
        (unsigned long long)expires_at, mac_hex, publisher_pkey_hex,
        encrypted_secret_hex, proto, udp_port,
        candidates, pow_solution_hex, proof_hex,
        access_proof ? ",\"access_proof\":\"" : "",
        access_proof ? access_proof : "", access_proof ? "\"" : "");
    if (n < 0 || (size_t)n >= sizeof(body)) return 1;
    return test_http_request(port, body, (size_t)n, expected_status,
        expected_fragment);
}

/**
 * Sends an encrypted registration whose proof uses one fixed valid candidate.
 * @param port Index port.
 * @param id Publisher id.
 * @param nonce_hex Challenge nonce hex.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param mac_hex Challenge MAC hex.
 * @param secret Exact 16-byte control secret.
 * @param udp_port Publisher UDP port.
 * @param candidates JSON candidates placed on the request wire.
 * @param solution Exact PoW solution bytes.
 * @param expected_status Expected HTTP status.
 * @param expected_fragment Expected response body fragment.
 * @return 0 on success, 1 on failure.
 */
static int test_register_send_secret_bytes(unsigned short port, const char *id,
    const char *nonce_hex, uint64_t issued_at, uint64_t expires_at,
    const char *mac_hex, const unsigned char secret[REDP2P_KEY_SZ],
    unsigned short udp_port, const char *candidates,
    const unsigned char solution[8], int expected_status,
    const char *expected_fragment)
{
    char publisher_pkey_hex[65], encrypted_secret_hex[65], proof_hex[65];
    char solution_hex[17], body[2048];
    unsigned char nonce[32], message[384], proof[32];
    size_t used;
    size_t id_len;
    int n;
    int shift;

    if (!id || !nonce_hex || !mac_hex || !secret || !candidates || !solution ||
        !test_hex_decode(nonce_hex, nonce, sizeof(nonce)) ||
        test_protect_registration_secret(test_register_index_pkey_hex, nonce_hex,
            secret, publisher_pkey_hex, encrypted_secret_hex) != 0) return 1;
    id_len = strlen(id);
    used = 0;
    memcpy(message + used, REDP2P_REGISTER_DOMAIN,
        sizeof(REDP2P_REGISTER_DOMAIN) - 1);
    used += sizeof(REDP2P_REGISTER_DOMAIN) - 1;
    memcpy(message + used, nonce, sizeof(nonce)); used += sizeof(nonce);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(issued_at >> shift);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(expires_at >> shift);
    message[used++] = (unsigned char)(id_len >> 8);
    message[used++] = (unsigned char)id_len;
    memcpy(message + used, id, id_len); used += id_len;
    message[used++] = 0; message[used++] = REDP2P_KEY_SZ;
    memcpy(message + used, secret, REDP2P_KEY_SZ); used += REDP2P_KEY_SZ;
    message[used++] = 2; message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    message[used++] = 1; message[used++] = 1; message[used++] = 4;
    message[used++] = 192; message[used++] = 0;
    message[used++] = 2; message[used++] = 1;
    message[used++] = (unsigned char)(udp_port >> 8);
    message[used++] = (unsigned char)udp_port;
    memcpy(message + used, solution, 8); used += 8;
    test_hmac_sha256(secret, REDP2P_KEY_SZ, message, used, proof);
    test_hex_encode(proof, sizeof(proof), proof_hex);
    test_hex_encode(solution, 8, solution_hex);
    n = snprintf(body, sizeof(body),
        "{\"op\":\"register\",\"id\":\"%s\",\"nonce\":\"%s\",\"issued_at\":%llu,"
        "\"expires_at\":%llu,\"mac\":\"%s\",\"pkey\":\"%s\",\"secret\":\"%s\",\"proto\":1,"
        "\"udp_port\":%u,\"candidates\":%s,\"pow_solution\":\"%s\",\"proof\":\"%s\"}",
        id, nonce_hex, (unsigned long long)issued_at,
        (unsigned long long)expires_at, mac_hex, publisher_pkey_hex,
        encrypted_secret_hex, (unsigned)udp_port, candidates, solution_hex,
        proof_hex);
    return n < 0 || (size_t)n >= sizeof(body) ? 1 : test_http_request(port,
        body, (size_t)n, expected_status, expected_fragment);
}

/**
 * Counts leading zero bits in a digest.
 * @param digest Digest bytes.
 * @param len Digest length.
 * @return Leading zero bit count.
 */
static unsigned int test_leading_zero_bits(const unsigned char *digest,
    size_t len)
{
    unsigned int total = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned int byte;

        if (digest[i] == 0) {
            total += 8;
            continue;
        }
        byte = digest[i];
        while ((byte & 0x80) == 0) {
            total++;
            byte <<= 1;
        }
        break;
    }
    return total;
}

/**
 * Searches for a pow solution whose SHA-256 digest fails a bit target.
 * @param nonce Challenge nonce.
 * @param issued_at Challenge issued_at.
 * @param expires_at Challenge expires_at.
 * @param id Publisher id.
 * @param bits Required leading zero bits.
 * @param solution Output failing solution bytes.
 * @return 0 on success, 1 when no failing solution was found.
 */
static int test_pow_failing_solution(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    unsigned int bits, unsigned char solution[8])
{
    unsigned char input[128], digest[32];
    size_t used, id_len;
    int i;

    memset(solution, 0, 8);
    id_len = strlen(id);
    for (i = 0; i < 1000000; i++) {
        int b;

        used = 0;
        memcpy(input + used, REDP2P_POW_DOMAIN,
            sizeof(REDP2P_POW_DOMAIN) - 1);
        used += sizeof(REDP2P_POW_DOMAIN) - 1;
        memcpy(input + used, nonce, 32); used += 32;
        for (int shift = 56; shift >= 0; shift -= 8)
            input[used++] = (unsigned char)(issued_at >> shift);
        for (int shift = 56; shift >= 0; shift -= 8)
            input[used++] = (unsigned char)(expires_at >> shift);
        input[used++] = (unsigned char)(id_len >> 8);
        input[used++] = (unsigned char)id_len;
        memcpy(input + used, id, id_len); used += id_len;
        memcpy(input + used, solution, 8); used += 8;
        test_sha256(input, used, digest);
        if (test_leading_zero_bits(digest, sizeof(digest)) < bits) return 0;
        for (b = 7; b >= 0; b--) {
            if (++solution[b] != 0) break;
        }
        if (b < 0) return 1;
    }
    return 1;
}

/**
 * Runs representative authenticated-registration tampering and challenge
 * validity. A valid proof is computed for the reference registration state,
 * then one distinct authenticated category is mutated per scenario. The
 * deterministic C/PHP vector covers the byte-exact canonical encoding.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register_mutations(void) {
    const char *name = "kc_redp2p_register_mutations";
    const char *detail =
        "authenticated registration rejects tampered and stale challenges";
    const char *candidates_base =
        "[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":41100}]";
    const char *candidates_address =
        "[{\"type\":\"host\",\"addr\":\"192.0.2.2\",\"port\":41100}]";
    test_index_t index;
    char nonce_hex[65], mac_hex[65], proof_hex[65], id_token[32],
        lookup_body[64];
    const char *secret = "0123456789abcdef";
    const char *pow_zero = "0000000000000000";
    uint64_t issued_at = 0, expires_at = 0;
    unsigned short port, udp_port = 41100;
    int fail;
    int i;

    fail = 0;
    port = (unsigned short)(test_port_base() + 5U);
    fail += expect_int("open mutation index", REDP2P_OK,
        test_index_start_configured(&index, port, 8, NULL, NULL));
    if (fail != 0) return 1;
    for (i = 0; i < 4; i++) {
        const char *request_secret = secret;
        const char *request_candidates = candidates_base;
        const char *request_solution = pow_zero;
        int proto = 1;
        unsigned int request_udp_port = udp_port;

        snprintf(id_token, sizeof(id_token), "authtamper%d", i);
        fail += expect_int("prepare tamper challenge", 0,
            test_register_prepare(port, id_token, secret, udp_port, nonce_hex,
                mac_hex, proof_hex, &issued_at, &expires_at));
        switch (i) {
        case 0: request_secret = "ffffffffffffffff"; break;
        case 1: proto = 2; break;
        case 2: request_candidates = candidates_address; break;
        case 3: request_solution = "ffffffffffffffff"; break;
        }
        fail += expect_int("authenticated registration rejects tampered state",
            0, test_register_send(port, id_token, nonce_hex, issued_at,
            expires_at, mac_hex, request_secret, proto, request_udp_port,
            request_candidates, request_solution, proof_hex, NULL, 403,
            "\"error\":\"auth_failed\""));
        snprintf(lookup_body, sizeof(lookup_body),
            "{\"op\":\"lookup\",\"id\":\"%s\"}", id_token);
        fail += expect_int("tampered registration creates no publisher", 0,
            test_http_request(port, lookup_body, strlen(lookup_body), 404,
            "\"error\":\"not_found\""));
    }
    for (i = 0; i < 3; i++) {
        const char *case_id;
        uint64_t request_issued;
        uint64_t request_expires;

        switch (i) {
        case 0: case_id = "authexpired"; break;
        case 1: case_id = "authlife"; break;
        default: case_id = "authfuture"; break;
        }
        fail += expect_int("prepare challenge validity case", 0,
            test_register_prepare(port, case_id, secret, udp_port, nonce_hex,
                mac_hex, proof_hex, &issued_at, &expires_at));
        if (i == 0) {
            request_issued = issued_at;
            request_expires = issued_at - 1;
        } else if (i == 1) {
            request_issued = issued_at;
            request_expires = issued_at + 61;
        } else {
            request_issued = (uint64_t)time(NULL) + 10;
            request_expires = (uint64_t)time(NULL) + 70;
        }
        fail += expect_int("invalid challenge is rejected", 0,
            test_register_send(port, case_id, nonce_hex, request_issued,
                request_expires, mac_hex, secret, 1, udp_port,
                candidates_base, pow_zero, proof_hex, NULL, 403,
                "\"error\":\"auth_failed\""));
        snprintf(lookup_body, sizeof(lookup_body),
            "{\"op\":\"lookup\",\"id\":\"%s\"}", case_id);
        fail += expect_int("invalid challenge creates no publisher", 0,
            test_http_request(port, lookup_body, strlen(lookup_body), 404,
            "\"error\":\"not_found\""));
    }
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies C index admission passwords, case sensitivity, and error ordering.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register_admission(void) {
    const char *name = "kc_redp2p_register_admission";
    const char *detail = "global and VIP admission proofs are exact and opaque";
    const char *secret = "0123456789abcdef";
    const char *candidates =
        "[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":41110}]";
    test_index_t index;
    char nonce[65], mac[65], proof[65], access[65];
    uint64_t issued, expires;
    unsigned short port = (unsigned short)(test_port_base() + 7U);
    int fail = 0;

    fail += expect_int("open admission index", REDP2P_OK,
        test_index_start_configured(&index, port, 2, "vip VipPass!", "GlobalPass!"));
    if (fail != 0) return 1;
    fail += expect_int("prepare missing admission", 0,
        test_register_prepare(port, "missing", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    fail += expect_int("missing admission proof", 0,
        test_register_send(port, "missing", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, NULL, 403,
            "\"error\":\"auth_failed\""));
    fail += expect_int("missing admission creates no state", 0,
        test_http_request(port, "{\"op\":\"lookup\",\"id\":\"missing\"}",
            sizeof("{\"op\":\"lookup\",\"id\":\"missing\"}") - 1,
            404, "\"error\":\"not_found\""));
    fail += expect_int("prepare malformed admission", 0,
        test_register_prepare(port, "malformed", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    memset(access, 'A', 64); access[64] = '\0';
    fail += expect_int("uppercase admission proof", 0,
        test_register_send(port, "malformed", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, access, 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("prepare exact global", 0,
        test_register_prepare(port, "global", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    fail += expect_int("compute exact global proof", 0,
        test_register_admission_proof("global", secret, 41110, nonce, issued,
            expires, "GlobalPass!", access));
    fail += expect_int("exact global admission", 0,
        test_register_send(port, "global", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, access, 200,
            "\"ok\":true"));
    fail += expect_int("prepare case-mismatched global", 0,
        test_register_prepare(port, "caseglobal", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    fail += expect_int("compute case-mismatched proof", 0,
        test_register_admission_proof("caseglobal", secret, 41110, nonce, issued,
            expires, "Globalpass!", access));
    fail += expect_int("case-mismatched global admission", 0,
        test_register_send(port, "caseglobal", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, access, 403,
            "\"error\":\"auth_failed\""));
    fail += expect_int("prepare VIP global fallback", 0,
        test_register_prepare(port, "vip", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    fail += expect_int("compute global proof for VIP", 0,
        test_register_admission_proof("vip", secret, 41110, nonce, issued,
            expires, "GlobalPass!", access));
    fail += expect_int("VIP does not fall back to global", 0,
        test_register_send(port, "vip", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, access, 403,
            "\"error\":\"auth_failed\""));
    fail += expect_int("prepare exact VIP", 0,
        test_register_prepare(port, "vip", secret, 41110, nonce, mac, proof,
            &issued, &expires));
    fail += expect_int("compute exact VIP proof", 0,
        test_register_admission_proof("vip", secret, 41110, nonce, issued,
            expires, "VipPass!", access));
    fail += expect_int("exact VIP admission", 0,
        test_register_send(port, "vip", nonce, issued, expires, mac, secret,
            1, 41110, candidates, "0000000000000000", proof, access, 200,
            "\"ok\":true"));
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies authenticated encrypted secrets retain the control-secret grammar.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register_secret_validation(void) {
    const char *name = "kc_redp2p_register_secret_validation";
    const char *detail = "decrypted malformed control secrets fail authentication";
    const char *ids[] = {"nonhexsecret", "nulsecret", "malformedsecret"};
    unsigned char secrets[3][REDP2P_KEY_SZ] = {
        "0123456789abcde!",
        {'0', '1', '2', '3', '4', '5', '6', '7', '\0', '9', 'a', 'b', 'c', 'd', 'e', 'f'},
        "0123456789abcde "
    };
    test_index_t index;
    char nonce[65], mac[65], proof[65];
    unsigned char solution[8];
    uint64_t issued;
    uint64_t expires;
    unsigned short port;
    int fail;
    int i;

    fail = 0;
    port = (unsigned short)(test_port_base() + 17U);
    memset(solution, 0, sizeof(solution));
    fail += expect_int("open secret validation index", REDP2P_OK,
        test_index_start_configured(&index, port, 0, NULL, NULL));
    if (fail != 0) return 1;
    for (i = 0; i < 3; i++) {
        fail += expect_int("prepare encrypted invalid secret", 0,
            test_register_prepare(port, ids[i], "0123456789abcdef", 41117,
                nonce, mac, proof, &issued, &expires));
        fail += expect_int(ids[i], 0,
            test_register_send_secret_bytes(port, ids[i], nonce, issued, expires,
                mac, secrets[i], 41117,
                "[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":41117}]",
                solution, 403, "\"error\":\"auth_failed\""));
    }
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies candidate validation runs before proof-of-work on a hard index.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register_order(void) {
    const char *name = "kc_redp2p_register_order";
    const char *detail = "register rejects malformed candidates before PoW auth";
    test_index_t index;
    char nonce_hex[65], mac_hex[65], body[2048], response[4096];
    unsigned char nonce[32], solution[8];
    uint64_t issued_at, expires_at;
    unsigned short port;
    int fail, n;

    fail = 0;
    port = (unsigned short)(test_port_base() + 6U);
    memset(&index, 0, sizeof(index));
    index.port = port;
    index.result = 999;
    fail += expect_int("open ordering index", REDP2P_OK, redp2p_context_create(&index.ctx));
    fail += expect_int("configure ordering index pow", REDP2P_OK,
        redp2p_idx_set_pow(index.ctx, 8));
    if (test_thread_start(&index.thread, test_index_main, &index) != 0) return 1;
    if (!test_wait_port(port, 1)) return 1;
    n = snprintf(body, sizeof(body), "{\"op\":\"challenge\",\"id\":\"orderpub\"}");
    if (n < 0 || (size_t)n >= sizeof(body) ||
        test_http_response(port, body, (size_t)n, response, sizeof(response)) !=
        0 || !test_json_string(response, "nonce", nonce_hex, sizeof(nonce_hex)) ||
        !test_json_string(response, "mac", mac_hex, sizeof(mac_hex)) ||
        !test_json_string(response, "pkey", test_register_index_pkey_hex,
            sizeof(test_register_index_pkey_hex)) ||
        !test_json_u64(response, "issued_at", &issued_at) ||
        !test_json_u64(response, "expires_at", &expires_at) ||
        !test_hex_decode(nonce_hex, nonce, sizeof(nonce))) {
        fail += expect_true("ordering challenge parse failed", 0);
        test_index_stop(&index);
        case_result(fail, name, detail);
        return 1;
    }
    fail += expect_int("find failing pow solution", 0,
        test_pow_failing_solution(nonce, issued_at, expires_at, "orderpub", 8,
            solution));
    fail += expect_int("malformed candidates rejected before PoW", 0,
        test_register_send_secret_bytes(port, "orderpub", nonce_hex, issued_at,
            expires_at, mac_hex, (const unsigned char *)"0123456789abcdef",
            41000, "[{\"type\":\"host\",\"addr\":\"not-an-address\","
            "\"port\":41000}]", solution, 400,
            "\"error\":\"bad_request\""));
    n = snprintf(body, sizeof(body), "{\"op\":\"lookup\",\"id\":\"orderpub\"}");
    fail += expect_int("rejected ordering register creates no publisher", 0,
        test_http_request(port, body, (size_t)n, 404,
        "\"error\":\"not_found\""));
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the fixed cross-language registration wire vector.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register_vector(void) {
    const char *name = "kc_redp2p_register_vector";
    const char *detail = "registration bytes match the shared C/PHP vector";
    const char *id = "vector";
    const char *secret = "0123456789abcdef";
    unsigned char nonce[32], key[32], solution[8], challenge[64];
    unsigned char pow_input[128], message[384];
    unsigned char hash[32], index_skey[32], index_pkey[32], publisher_skey[32];
    unsigned char publisher_pkey[32], shared[32], encryption_key[32], wire[32];
    char actual[65];
    size_t challenge_len, pow_len, message_len, id_len;
    uint64_t issued_at, expires_at;
    int fail;
    size_t i;

    issued_at = 1700000000ULL;
    expires_at = 1700000060ULL;
    id_len = strlen(id);
    for (i = 0; i < sizeof(nonce); i++) {
        nonce[i] = (unsigned char)i;
        key[i] = (unsigned char)i;
    }
    if (!test_hex_decode("0102030405060708", solution, sizeof(solution))) return 1;
    challenge_len = 0;
    memcpy(challenge + challenge_len, REDP2P_CHALLENGE_DOMAIN,
        sizeof(REDP2P_CHALLENGE_DOMAIN) - 1);
    challenge_len += sizeof(REDP2P_CHALLENGE_DOMAIN) - 1;
    memcpy(challenge + challenge_len, nonce, sizeof(nonce));
    challenge_len += sizeof(nonce);
    for (i = 0; i < 8; i++) challenge[challenge_len++] =
        (unsigned char)(issued_at >> (56 - i * 8));
    for (i = 0; i < 8; i++) challenge[challenge_len++] =
        (unsigned char)(expires_at >> (56 - i * 8));
    test_hmac_sha256(key, sizeof(key), challenge, challenge_len, hash);
    test_hex_encode(hash, sizeof(hash), actual);
    fail = expect_string("challenge MAC vector",
        "b4ae0428385fc80d7daeefc69d0ce98aafd9c2c36c65b1e9b998ac80c989a0f3",
        actual);
    test_hmac_sha256(key, sizeof(key), (const unsigned char *)
        "REDP2P-INDEX-KEY\x00\x01\x02\x03\x04\x05\x06\x07"
        "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13"
        "\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
        "\x00\x00\x00\x00\x65\x53\xf1\x00\x00\x00\x00\x00\x65\x53\xf1\x3c",
        64, index_skey);
    test_hex_encode(index_skey, sizeof(index_skey), actual);
    fail += expect_string("index secret key vector",
        "cbcb985720a1c789c78b2086e7d28418b398e7524daac8a4d24feb437cdb1635", actual);
    crypto_x25519_public_key(index_pkey, index_skey);
    test_hex_encode(index_pkey, sizeof(index_pkey), actual);
    fail += expect_string("index public key vector",
        "cb2a1d87390c90d74588ab1b0959af4232404cd6f8f74e731028ab63dc263b66", actual);
    for (i = 0; i < sizeof(publisher_skey); i++) publisher_skey[i] = (unsigned char)(i + 1);
    crypto_x25519_public_key(publisher_pkey, publisher_skey);
    test_hex_encode(publisher_pkey, sizeof(publisher_pkey), actual);
    fail += expect_string("publisher public key vector",
        "07a37cbc142093c8b755dc1b10e86cb426374ad16aa853ed0bdfc0b2b86d1c7c", actual);
    crypto_x25519(shared, publisher_skey, index_pkey);
    test_hex_encode(shared, sizeof(shared), actual);
    fail += expect_string("registration shared secret vector",
        "1a294ae3b7cdb9307edcd551841d879b3529c6d09eab920c72d350a11be33e38", actual);
    memcpy(pow_input, "REDP2P-REGISTER-SECRET", 22);
    memcpy(pow_input + 22, nonce, 32);
    memcpy(pow_input + 54, index_pkey, 32);
    memcpy(pow_input + 86, publisher_pkey, 32);
    test_hmac_sha256(shared, sizeof(shared), pow_input, 118, encryption_key);
    test_hex_encode(encryption_key, sizeof(encryption_key), actual);
    fail += expect_string("registration encryption key vector",
        "9d0f990cb51ee320f14245b526b35cf80848d58b6cc6ff5d02fec6a4b7df5975", actual);
    crypto_aead_lock(wire, wire + REDP2P_KEY_SZ, encryption_key, nonce, NULL,
        0, (const unsigned char *)secret, REDP2P_KEY_SZ);
    test_hex_encode(wire, sizeof(wire), actual);
    fail += expect_string("encrypted registration secret vector",
        "edccec6e4b9f610612682014714771710cb5970b35db8e45e5d3a184eda862b5", actual);
    pow_len = 0;
    memcpy(pow_input + pow_len, REDP2P_POW_DOMAIN, sizeof(REDP2P_POW_DOMAIN) - 1);
    pow_len += sizeof(REDP2P_POW_DOMAIN) - 1;
    memcpy(pow_input + pow_len, nonce, sizeof(nonce)); pow_len += sizeof(nonce);
    for (i = 0; i < 8; i++) pow_input[pow_len++] =
        (unsigned char)(issued_at >> (56 - i * 8));
    for (i = 0; i < 8; i++) pow_input[pow_len++] =
        (unsigned char)(expires_at >> (56 - i * 8));
    pow_input[pow_len++] = 0; pow_input[pow_len++] = (unsigned char)id_len;
    memcpy(pow_input + pow_len, id, id_len); pow_len += id_len;
    memcpy(pow_input + pow_len, solution, sizeof(solution)); pow_len += sizeof(solution);
    test_sha256(pow_input, pow_len, hash);
    test_hex_encode(hash, sizeof(hash), actual);
    fail += expect_string("registration PoW vector",
        "357b34780abc35e2529d74352d41660474cbe71502979cba2d9063a9b27bd2a3",
        actual);
    message_len = 0;
    memcpy(message + message_len, REDP2P_REGISTER_DOMAIN,
        sizeof(REDP2P_REGISTER_DOMAIN) - 1);
    message_len += sizeof(REDP2P_REGISTER_DOMAIN) - 1;
    memcpy(message + message_len, nonce, sizeof(nonce)); message_len += sizeof(nonce);
    for (i = 0; i < 8; i++) message[message_len++] =
        (unsigned char)(issued_at >> (56 - i * 8));
    for (i = 0; i < 8; i++) message[message_len++] =
        (unsigned char)(expires_at >> (56 - i * 8));
    message[message_len++] = 0; message[message_len++] = (unsigned char)id_len;
    memcpy(message + message_len, id, id_len); message_len += id_len;
    message[message_len++] = 0; message[message_len++] = REDP2P_KEY_SZ;
    memcpy(message + message_len, secret, REDP2P_KEY_SZ); message_len += REDP2P_KEY_SZ;
    message[message_len++] = 2; message[message_len++] = 0xa0;
    message[message_len++] = 0x31; message[message_len++] = 2;
    message[message_len++] = 1; message[message_len++] = 4;
    message[message_len++] = 127; message[message_len++] = 0;
    message[message_len++] = 0; message[message_len++] = 1;
    message[message_len++] = 0xa0; message[message_len++] = 0x31;
    message[message_len++] = 2; message[message_len++] = 6;
    message[message_len++] = 0x20; message[message_len++] = 0x01;
    message[message_len++] = 0x0d; message[message_len++] = 0xb8;
    memset(message + message_len, 0, 11); message_len += 11;
    message[message_len++] = 1; message[message_len++] = 0x11;
    message[message_len++] = 0x5c;
    memcpy(message + message_len, solution, sizeof(solution));
    message_len += sizeof(solution);
    test_hmac_sha256((const unsigned char *)secret, REDP2P_KEY_SZ, message,
        message_len, hash);
    test_hex_encode(hash, sizeof(hash), actual);
    fail += expect_string("registration proof vector",
        "702ade4feeb6297ec53e7eebc262bf8a61feb20731fdcf7f6001df7d616de906",
        actual);
    {
        unsigned char admission[sizeof("REDP2P-ADMISSION-v1") - 1 + 384];
        size_t admission_len;

        admission_len = strlen("REDP2P-ADMISSION-v1");
        memcpy(admission, "REDP2P-ADMISSION-v1", admission_len);
        memcpy(admission + admission_len, message, message_len);
        admission_len += message_len;
        test_hmac_sha256((const unsigned char *)"admission-pass",
            strlen("admission-pass"), admission, admission_len, hash);
        test_hex_encode(hash, sizeof(hash), actual);
        fail += expect_string("admission proof vector",
            "252d6e3219e49f38e73fb587e66400a051f2c6c5ec593f8dfab583a5c7225b1d",
            actual);
        test_hmac_sha256((const unsigned char *)"Admission-pass",
            strlen("Admission-pass"), admission, admission_len, hash);
        test_hex_encode(hash, sizeof(hash), actual);
        fail += expect_string("case-sensitive admission proof vector",
            "caa4863ffe53fe7aa240e40147e65481f423aeaed141ef1389c8ea58ce009b58",
            actual);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Reports which socket protocols one test case uses at a port offset.
 * @param offset Port offset from the allocated base.
 * @param tcp Set to 1 when the TCP port is required.
 * @param udp Set to 1 when the UDP port is required.
 * @return None.
 */
static void test_port_requirement(unsigned int offset, int *tcp, int *udp) {
    unsigned int anchor;

    *tcp = 0;
    *udp = 0;
    if (strcmp(test_case_name, "redp2p_idx_run") == 0) {
        *tcp = offset >= 1U && offset <= 4U;
    } else if (strcmp(test_case_name, "redp2p_pub_run") == 0) {
        *tcp = offset == 20U || offset == 22U;
    } else if (strcmp(test_case_name, "redp2p_con_run") == 0) {
        *tcp = offset >= 40U && offset <= 45U;
        if (offset == 47U) *tcp = 1;
    } else if (strcmp(test_case_name, "redp2p_udp_tunnel") == 0) {
        anchor = 300U;
        *tcp = offset == anchor;
        *udp = offset == anchor + 1U || offset == anchor + 2U;
    } else if (strcmp(test_case_name, "redp2p_tcp_stream") == 0)
    {
        anchor = 400U;
        *tcp = offset >= anchor && offset <= anchor + 2U;
    } else if (strcmp(test_case_name, "kc_redp2p_api") == 0) {
        *tcp = offset >= 410U && offset <= 412U;
    } else if (strcmp(test_case_name, "redp2p_test_deregister_persisted_publisher") == 0) {
        *tcp = offset >= 60U && offset <= 62U;
    } else if (strcmp(test_case_name, "redp2p_idx_query_publishers") == 0) {
        *tcp = offset >= 80U && offset <= 84U;
    } else if (strcmp(test_case_name, "redp2p_heartbeat") == 0) {
        *tcp = offset >= 100U && offset <= 102U;
    } else if (strcmp(test_case_name, "redp2p_lost_response") == 0) {
        *tcp = offset == 120U;
        *udp = offset == 121U;
    }
}

/**
 * Issues one HTTP JSON request and captures its complete response.
 * @param source Bound transport source IP, or NULL for the default.
 * @param port Index port.
 * @param body JSON request body.
 * @param body_len Request body byte count.
 * @param response Output response buffer.
 * @param response_cap Output response capacity.
 * @return 0 on success, 1 on transport or framing failure.
 */
static int test_http_response(unsigned short port, const char *body,
    size_t body_len, char *response, size_t response_cap)
{
    test_socket_t fd;
    char head[512];
    size_t total;
    int n;

    if (!response || response_cap < 2) return 1;
    fd = test_tcp_connect(port);
    if (fd == TEST_SOCKET_INVALID) return 1;
    test_socket_timeout(fd, 2000U);
    n = snprintf(head, sizeof(head),
        "POST /redp2p/ HTTP/1.1\r\nHost: %s\r\nContent-Length: %u\r\n"
        "Content-Type: application/json\r\nConnection: close\r\n\r\n",
        TEST_HOST, (unsigned)body_len);
    if (n < 0 || (size_t)n >= sizeof(head) ||
        test_socket_send_all(fd, (const unsigned char *)head, (size_t)n) != 0 ||
        test_socket_send_all(fd, (const unsigned char *)body, body_len) != 0)
    {
        test_socket_close(fd);
        return 1;
    }
    total = 0;
    while (total < response_cap - 1) {
        int got = (int)recv(fd, response + total, response_cap - 1 - total, 0);

        if (got <= 0) break;
        total += (size_t)got;
    }
    response[total] = '\0';
    test_socket_close(fd);
    return total > 0 && strncmp(response, "HTTP/", 5) == 0 ? 0 : 1;
}

/**
 * Reports whether one required loopback port can be bound.
 * @param base First port in the candidate block.
 * @param offset Port offset from the candidate base.
 * @param type Socket type.
 * @return 1 when the required port can be bound, 0 otherwise.
 */
static int test_port_available(unsigned short base, unsigned int offset,
    int type)
{
    struct sockaddr_in addr;
    test_socket_t fd;
    int result;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    addr.sin_port = htons((unsigned short)(base + offset));
    fd = socket(AF_INET, type, 0);
    if (fd == TEST_SOCKET_INVALID) return 0;
    result = bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return result;
}

/**
 * Reports whether every port required by the active case is available.
 * @param base Candidate port base.
 * @return 1 when all required ports are available, 0 otherwise.
 */
static int test_port_base_available(unsigned short base) {
    unsigned int offset;

    for (offset = 0; offset <= 412U; offset++) {
        int tcp;
        int udp;

        test_port_requirement(offset, &tcp, &udp);
        if (tcp && !test_port_available(base, offset, SOCK_STREAM)) return 0;
        if (udp && !test_port_available(base, offset, SOCK_DGRAM)) return 0;
    }
    return 1;
}

/**
 * Reserves one candidate port block against other test processes.
 * @param base Candidate port base.
 * @return 1 when reserved, 0 when already reserved or unavailable.
 */
static int test_port_base_reserve(unsigned short base) {
    int length;

#ifdef _WIN32
    char root[384];
    DWORD root_len;

    root_len = GetTempPathA(sizeof(root), root);
    if (root_len == 0 || root_len >= sizeof(root)) return 0;
    length = snprintf(test_port_reservation, sizeof(test_port_reservation),
        "%sredp2p-test-ports-%u.lock", root, (unsigned)base);
    if (length < 0 || (size_t)length >= sizeof(test_port_reservation)) return 0;
    if (!CreateDirectoryA(test_port_reservation, NULL)) {
        test_port_reservation[0] = '\0';
        return 0;
    }
#else
    const char *tmp;

    tmp = getenv("TMPDIR");
    if (!tmp || !tmp[0]) tmp = "/tmp";
    length = snprintf(test_port_reservation, sizeof(test_port_reservation),
        "%s/redp2p-test-ports-%u.lock", tmp, (unsigned)base);
    if (length < 0 || (size_t)length >= sizeof(test_port_reservation)) return 0;
    if (mkdir(test_port_reservation, 0700) != 0) {
        test_port_reservation[0] = '\0';
        return 0;
    }
#endif
    return 1;
}

/**
 * Releases the current cross-process port block reservation.
 * @return None.
 */
static void test_port_base_release(void) {
    if (!test_port_reservation[0]) return;
#ifdef _WIN32
    RemoveDirectoryA(test_port_reservation);
#else
    rmdir(test_port_reservation);
#endif
    test_port_reservation[0] = '\0';
}

/**
 * Returns a checked loopback port base for the active test case.
 * @return Port base, or 0 when no candidate is available.
 */
static unsigned short test_port_base(void) {
    unsigned long seed;
    static unsigned short selected;
    unsigned int attempt;

    if (selected != 0) return selected;
#ifdef _WIN32
    seed = (unsigned long)_getpid();
#else
    seed = (unsigned long)getpid();
#endif
    for (attempt = 0; attempt < 64U; attempt++) {
        unsigned short candidate;

        candidate = (unsigned short)(25000U + (seed + attempt) % 20000U);
        if (!test_port_base_reserve(candidate)) continue;
        if (test_port_base_available(candidate)) {
            selected = candidate;
            return selected;
        }
        test_port_base_release();
    }
    fprintf(stderr, "no available loopback ports for %s\n", test_case_name);
    return 0;
}

/**
 * Sleeps for a bounded number of milliseconds.
 * @param ms Milliseconds to sleep.
 * @return None.
 */
static void test_sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
#endif
}

/**
 * Returns a monotonic wall-clock value in milliseconds.
 * @return Monotonic milliseconds since an arbitrary epoch.
 */
static uint64_t test_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)(ts.tv_nsec / 1000000L);
#endif
}

/**
 * Sets or clears a process environment variable.
 * @param name Environment variable name.
 * @param value Environment variable value or NULL.
 * @return 0 on success, 1 on failure.
 */
static int test_setenv(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value != NULL ? value : "") == 0 ? 0 : 1;
#else
    if (value == NULL) return unsetenv(name) == 0 ? 0 : 1;
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Removes one test directory tree.
 * @param path Directory path.
 * @return 0 on success, 1 on failure.
 */
static int test_remove_tree(const char *path) {
#ifdef _WIN32
    WIN32_FIND_DATAA entry;
    char child[512];
    char pattern[512];
    HANDLE search;
    int rc;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) < 0 ||
        strlen(pattern) >= sizeof(pattern))
        return 1;
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE)
        return RemoveDirectoryA(path) ? 0 : 1;
    rc = 0;
    do {
        if (strcmp(entry.cFileName, ".") == 0 ||
            strcmp(entry.cFileName, "..") == 0)
            continue;
        if (snprintf(child, sizeof(child), "%s\\%s", path,
            entry.cFileName) < 0 || strlen(child) >= sizeof(child))
        {
            rc = 1;
            continue;
        }
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (test_remove_tree(child) != 0) rc = 1;
        } else if (!DeleteFileA(child)) {
            rc = 1;
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
    if (!RemoveDirectoryA(path)) rc = 1;
    return rc;
#else
    struct dirent *entry;
    struct stat st;
    char child[512];
    DIR *dir;
    int rc;

    dir = opendir(path);
    if (!dir) return rmdir(path) == 0 ? 0 : 1;
    rc = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (snprintf(child, sizeof(child), "%s/%s", path,
            entry->d_name) < 0 || strlen(child) >= sizeof(child))
        {
            rc = 1;
            continue;
        }
        if (lstat(child, &st) != 0) {
            rc = 1;
        } else if (S_ISDIR(st.st_mode)) {
            if (test_remove_tree(child) != 0) rc = 1;
        } else if (unlink(child) != 0) {
            rc = 1;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0) rc = 1;
    return rc;
#endif
}

/**
 * Configures a temporary process-local HOME directory for key files.
 * @return 0 on success, 1 on failure.
 */
static int test_home(void) {
#ifdef _WIN32
    char base[MAX_PATH];
    char path[MAX_PATH];
    DWORD base_len;

    base_len = GetTempPathA(sizeof(base), base);
    if (base_len == 0 || base_len >= sizeof(base)) return 1;
    if (GetTempFileNameA(base, "rpt", 0, path) == 0) return 1;
    if (!DeleteFileA(path) || !CreateDirectoryA(path, NULL)) return 1;
    if (strlen(path) >= sizeof(test_home_path)) {
        RemoveDirectoryA(path);
        return 1;
    }
    strcpy(test_home_path, path);
#else
    const char *base;

    base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    if (snprintf(test_home_path, sizeof(test_home_path),
        "%s/redp2p-test-XXXXXX", base) < 0 ||
        strlen(test_home_path) >= sizeof(test_home_path))
        return 1;
    if (!mkdtemp(test_home_path)) return 1;
#endif
    if (test_setenv("HOME", test_home_path) != 0) {
        test_remove_tree(test_home_path);
        test_home_path[0] = '\0';
        return 1;
    }
    test_setenv("XDG_DATA_HOME", NULL);
    test_setenv("XDG_STATE_HOME", NULL);
    return 0;
}

/**
 * Removes the temporary process-local HOME directory.
 * @return 0 on success, 1 on failure.
 */
static int test_home_cleanup(void) {
    int rc;

    if (!test_home_path[0]) return 0;
    rc = test_remove_tree(test_home_path);
    test_home_path[0] = '\0';
    return rc;
}

/**
 * Builds the test HOME publisher session-secret directory path.
 * @param path Output path.
 * @param cap Output capacity.
 * @return 0 on success, 1 on overflow.
 */
static int test_key_dir(char *path, size_t cap) {
    int n;

    n = snprintf(path, cap, "%s/.local/share/redp2p/keys", test_home_path);
    return n < 0 || (size_t)n >= cap ? 1 : 0;
}

/**
 * Lists regular publisher session-secret filenames in the test HOME.
 * @param names Output filename array.
 * @param capacity Maximum filename count.
 * @param count Output filename count.
 * @return 0 on success, 1 on failure or overflow.
 */
static int test_key_list(char names[][128], int capacity, int *count) {
    char dir[640];

    if (test_key_dir(dir, sizeof(dir)) != 0 || !count) return 1;
    *count = 0;
#ifdef _WIN32
    {
        WIN32_FIND_DATAA entry;
        char pattern[672];
        HANDLE search;

        if (snprintf(pattern, sizeof(pattern), "%s/*", dir) < 0 ||
            strlen(pattern) >= sizeof(pattern))
            return 1;
        search = FindFirstFileA(pattern, &entry);
        if (search == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : 1;
        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (*count >= capacity || strlen(entry.cFileName) >= 128) {
                FindClose(search);
                return 1;
            }
            strcpy(names[*count], entry.cFileName);
            (*count)++;
        } while (FindNextFileA(search, &entry));
        FindClose(search);
    }
#else
    {
        struct dirent *entry;
        DIR *directory;

        directory = opendir(dir);
        if (!directory) return errno == ENOENT ? 0 : 1;
        while ((entry = readdir(directory)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (*count >= capacity || strlen(entry->d_name) >= 128) {
                closedir(directory);
                return 1;
            }
            strcpy(names[*count], entry->d_name);
            (*count)++;
        }
        closedir(directory);
    }
#endif
    return 0;
}

/**
 * Builds one path below the publisher session-secret directory.
 * @param name Filename.
 * @param path Output path.
 * @param cap Output capacity.
 * @return 0 on success, 1 on overflow.
 */
static int test_key_path(const char *name, char *path, size_t cap) {
    char dir[640];
    int n;

    if (test_key_dir(dir, sizeof(dir)) != 0) return 1;
    n = snprintf(path, cap, "%s/%s", dir, name);
    return n < 0 || (size_t)n >= cap ? 1 : 0;
}

/**
 * Removes all files from the keys directory.
 * @return 0 on success, 1 on failure.
 */
static int test_key_cleanup(void) {
    char dir[640];
    if (test_key_dir(dir, sizeof(dir)) != 0) return 0;
#ifdef _WIN32
    {
        WIN32_FIND_DATAA entry;
        char pattern[672];
        char path[768];
        HANDLE search;

        if (snprintf(pattern, sizeof(pattern), "%s/*", dir) < 0 ||
            strlen(pattern) >= sizeof(pattern))
            return 1;
        search = FindFirstFileA(pattern, &entry);
        if (search == INVALID_HANDLE_VALUE) return 0;
        do {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (snprintf(path, sizeof(path), "%s/%s", dir, entry.cFileName) < 0 ||
                strlen(path) >= sizeof(path))
                continue;
            DeleteFileA(path);
        } while (FindNextFileA(search, &entry));
        FindClose(search);
    }
#else
    {
        struct dirent *entry;
        DIR *directory;
        char path[768];

        directory = opendir(dir);
        if (!directory) return 0;
        while ((entry = readdir(directory)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) < 0 ||
                (size_t)snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name) >= sizeof(path))
                continue;
            unlink(path);
        }
        closedir(directory);
    }
#endif
    return 0;
}

/**
 * Reads one complete small test file.
 * @param path File path.
 * @param data Output bytes.
 * @param cap Output capacity.
 * @param len Output byte count.
 * @return 0 on success, 1 on failure or overflow.
 */
static int test_file_read(const char *path, char *data, size_t cap,
    size_t *len)
{
    FILE *file;
    size_t total;
    int byte;

    file = fopen(path, "rb");
    if (!file) return 1;
    total = fread(data, 1, cap, file);
    byte = fgetc(file);
    if (ferror(file) || byte != EOF || fclose(file) != 0) return 1;
    if (len) *len = total;
    return 0;
}

/**
 * Replaces one small test file with exact bytes.
 * @param path File path.
 * @param data Input bytes.
 * @param len Input byte count.
 * @return 0 on success, 1 on failure.
 */
static int test_file_write(const char *path, const char *data, size_t len) {
    FILE *file;
    size_t written;

    file = fopen(path, "wb");
    if (!file) return 1;
    written = fwrite(data, 1, len, file);
    if (written != len || fflush(file) != 0 || fclose(file) != 0) return 1;
    return 0;
}

/**
 * Reports whether one filesystem path exists.
 * @param path Path to inspect.
 * @return 1 when present, 0 otherwise.
 */
static int test_path_exists(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat status;

    return lstat(path, &status) == 0;
#endif
}

/**
 * Starts platform socket state.
 * @return 0 on success, 1 on failure.
 */
static int test_socket_start(void) {
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

/**
 * Stops platform socket state.
 * @return 0 on success.
 */
static int test_socket_stop(void) {
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

/**
 * Closes a socket.
 * @param fd Socket descriptor.
 * @return 0 on success, non-zero on failure.
 */
static int test_socket_close(test_socket_t fd) {
#ifdef _WIN32
    return closesocket(fd);
#else
    return close(fd);
#endif
}

/**
 * Shuts down both directions of one socket.
 * @param fd Socket descriptor.
 * @return 0 on success, non-zero on failure.
 */
static int test_socket_shutdown(test_socket_t fd) {
#ifdef _WIN32
    return shutdown(fd, SD_BOTH);
#else
    return shutdown(fd, SHUT_RDWR);
#endif
}

/**
 * Sets receive timeout on one socket.
 * @param fd Socket descriptor.
 * @param ms Timeout in milliseconds.
 * @return 0 on success.
 */
static int test_socket_timeout(test_socket_t fd, unsigned int ms) {
#ifdef _WIN32
    DWORD tv;

    tv = (DWORD)ms;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;

    tv.tv_sec = (time_t)(ms / 1000U);
    tv.tv_usec = (suseconds_t)(ms % 1000U) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#endif
    return 0;
}

/**
 * Reports whether the latest socket operation was interrupted.
 * @return 1 when interrupted, 0 otherwise.
 */
static int test_socket_interrupted(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

/**
 * Sends an exact byte sequence before the socket timeout expires.
 * @param fd Socket descriptor.
 * @param data Bytes to send.
 * @param len Byte count.
 * @return 0 on success, 1 on failure.
 */
static int test_socket_send_all(test_socket_t fd, const unsigned char *data,
size_t len)
{
    size_t sent;

    sent = 0;
    while (sent < len) {
        int n;

        n = (int)send(fd, (const char *)data + sent, (int)(len - sent), 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && test_socket_interrupted()) continue;
        return 1;
    }
    return 0;
}

/**
 * Receives an exact byte sequence before the socket timeout expires.
 * @param fd Socket descriptor.
 * @param data Destination buffer.
 * @param len Byte count.
 * @return 0 on success, 1 on failure.
 */
static int test_socket_receive_exact(test_socket_t fd, unsigned char *data,
size_t len)
{
    size_t received;

    received = 0;
    while (received < len) {
        int n;

        n = (int)recv(fd, (char *)data + received, (int)(len - received), 0);
        if (n > 0) {
            received += (size_t)n;
            continue;
        }
        if (n < 0 && test_socket_interrupted()) continue;
        return 1;
    }
    return 0;
}

/**
 * Tests whether a loopback TCP port accepts connections.
 * @param port TCP port.
 * @return 1 when open, 0 when closed.
 */
static int test_port_open(unsigned short port) {
    test_socket_t fd;
    struct sockaddr_in addr;
    int rc;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == TEST_SOCKET_INVALID) return 0;
    test_socket_timeout(fd, 250U);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    rc = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    test_socket_close(fd);
    return rc == 0 ? 1 : 0;
}

/**
 * Waits for a TCP port to reach an expected state.
 * @param port TCP port.
 * @param open Expected state.
 * @return 1 when observed, 0 on timeout.
 */
static int test_wait_port(unsigned short port, int open) {
    int i;

    for (i = 0; i < 100; i++) {
        if (test_port_open(port) == open) return 1;
        test_sleep_ms(20U);
    }
    return 0;
}

/**
 * Waits until one publisher is registered or has returned.
 * @param publisher Publisher state.
 * @return 0 when startup reaches an observable state, 1 on timeout.
 */
static int test_wait_publisher_ready(test_publisher_t *publisher) {
    char body[REDP2P_ID_MAX + 64];
    char expected[64];
    int body_len;
    int expected_len;
    int observed;
    unsigned int elapsed;

    body_len = snprintf(body, sizeof(body), "{\"op\":\"lookup\",\"id\":\"%s\"}",
        publisher->id);
    expected_len = snprintf(expected, sizeof(expected), "\"udp_port\":%u",
        (unsigned)publisher->bind_port);
    if (body_len < 0 || (size_t)body_len >= sizeof(body) ||
        expected_len < 0 || (size_t)expected_len >= sizeof(expected))
        return 1;
    observed = 0;
    for (elapsed = 0; elapsed < 2000U; elapsed += 20U) {
        if (atomic_load(&publisher->result) != 999) return 0;
        if (test_http_request(publisher->index_port, body, (size_t)body_len,
            200, expected) == 0) {
            observed++;
            if (observed == 2) return 0;
        } else {
            observed = 0;
        }
        test_sleep_ms(20U);
    }
    fprintf(stderr, "publisher %s startup timed out: result=%d error=%s\n",
        publisher->id, atomic_load(&publisher->result),
        redp2p_get_error(publisher->ctx));
    return 1;
}

/**
 * Waits until one TCP consumer listens or has returned.
 * @param consumer Consumer state.
 * @return 0 when listening, 1 on timeout or terminal failure.
 */
static int test_wait_consumer_ready(test_consumer_t *consumer) {
    unsigned int elapsed;

    for (elapsed = 0; elapsed < 2000U; elapsed += 20U) {
        if (test_port_open(consumer->bind_port)) return 0;
        if (atomic_load(&consumer->result) != 999) break;
        test_sleep_ms(20U);
    }
    fprintf(stderr, "consumer %s startup failed: result=%d error=%s\n",
        consumer->self_id, atomic_load(&consumer->result),
        redp2p_get_error(consumer->ctx));
    return 1;
}

/**
 * Checks one integer value.
 * @param name Check name.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Checks one true condition.
 * @param name Check name.
 * @param condition Condition value.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Checks one string value.
 * @param name Check name.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

static int test_case_total = 1;
static int test_case_current = 0;
static int test_grouped = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    if (test_grouped) return;
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Runs one loopback UDP echo backend until stopped.
 * @param arg Echo backend state.
 * @return None.
 */
static void test_udp_echo_run(void *arg) {
    test_udp_echo_t *echo;
    unsigned char buf[2048];

    echo = (test_udp_echo_t *)arg;
    while (!atomic_load(&echo->stop)) {
        struct sockaddr_storage from;
        test_socklen_t from_len;
        int n;

        from_len = sizeof(from);
        n = (int)recvfrom(echo->fd, (char *)buf, sizeof(buf), 0,
            (struct sockaddr *)&from, &from_len);
        if (n < 0) continue;
        sendto(echo->fd, (const char *)buf, (size_t)n, 0,
            (const struct sockaddr *)&from, from_len);
    }
}

/**
 * Runs one loopback TCP echo backend until stopped.
 * @param arg Echo backend state.
 * @return None.
 */
static void test_tcp_echo_run(void *arg) {
    test_tcp_echo_t *echo;
    unsigned int i;

    echo = (test_tcp_echo_t *)arg;
    while (!atomic_load(&echo->stop)) {
        fd_set readable;
        struct timeval timeout;
        int max_fd;
        int selected;

        FD_ZERO(&readable);
        FD_SET(echo->fd, &readable);
#ifdef _WIN32
        max_fd = 0;
#else
        max_fd = (int)echo->fd;
#endif
        for (i = 0; i < TEST_TCP_CLIENTS; i++) {
            if (echo->clients[i] == TEST_SOCKET_INVALID) continue;
            FD_SET(echo->clients[i], &readable);
#ifndef _WIN32
            if (echo->clients[i] > max_fd) max_fd = echo->clients[i];
#endif
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        selected = select(max_fd + 1, &readable, NULL, NULL, &timeout);
        if (selected <= 0) continue;
        if (FD_ISSET(echo->fd, &readable)) {
            test_socket_t client;

            client = accept(echo->fd, NULL, NULL);
            if (client != TEST_SOCKET_INVALID) {
                for (i = 0; i < TEST_TCP_CLIENTS; i++) {
                    if (echo->clients[i] == TEST_SOCKET_INVALID) break;
                }
                if (i == TEST_TCP_CLIENTS) {
                    test_socket_close(client);
                } else {
                    test_socket_timeout(client, 3000U);
                    echo->clients[i] = client;
                }
            }
        }
        for (i = 0; i < TEST_TCP_CLIENTS; i++) {
            unsigned char buf[1024];
            int n;

            if (echo->clients[i] == TEST_SOCKET_INVALID ||
                !FD_ISSET(echo->clients[i], &readable))
                continue;
            n = (int)recv(echo->clients[i], (char *)buf, sizeof(buf), 0);
            if (n > 0 && test_socket_send_all(echo->clients[i], buf,
                (size_t)n) == 0)
                continue;
            test_socket_close(echo->clients[i]);
            echo->clients[i] = TEST_SOCKET_INVALID;
        }
    }
    for (i = 0; i < TEST_TCP_CLIENTS; i++) {
        if (echo->clients[i] == TEST_SOCKET_INVALID) continue;
        test_socket_close(echo->clients[i]);
        echo->clients[i] = TEST_SOCKET_INVALID;
    }
}

/**
 * Reads one bounded HTTP request head up to the blank line.
 * @param client   Client socket.
 * @param head     Output head buffer.
 * @param cap      Head buffer capacity.
 * @param head_len Output head byte count.
 * @return 0 on success, 1 on failure.
 */
static int test_stub_read_head(test_socket_t client, char *head, int cap,
    int *head_len)
{
    int len;
    int n;

    len = 0;
    while (len < cap - 1) {
        char byte;

        n = (int)recv(client, &byte, 1, 0);
        if (n <= 0) return 1;
        head[len++] = byte;
        if (len >= 4 &&
            head[len - 4] == '\r' && head[len - 3] == '\n' &&
            head[len - 2] == '\r' && head[len - 1] == '\n')
            break;
    }
    head[len] = '\0';
    *head_len = len;
    return 0;
}

/**
 * Reads one Content-Length value from an HTTP request head.
 * @param head     Request head text.
 * @param head_len Request head byte count.
 * @return Content-Length value, or 0 when absent or invalid.
 */
static long test_stub_content_length(const char *head, int head_len) {
    static const char marker[] = "Content-Length:";
    long i;

    for (i = 0; i + (long)sizeof(marker) - 1 <= head_len; i++) {
        long j;
        int matched;

        matched = 1;
        for (j = 0; marker[j] != '\0'; j++) {
            if (head[i + j] != marker[j]) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            long pos;
            long value;

            pos = i + (long)sizeof(marker) - 1;
            while (pos < head_len && head[pos] == ' ') pos++;
            value = 0;
            while (pos < head_len && head[pos] >= '0' && head[pos] <= '9') {
                value = value * 10 + (head[pos] - '0');
                pos++;
            }
            return value;
        }
    }
    return 0;
}

/**
 * Discards one HTTP request body of known length.
 * @param client Client socket.
 * @param len    Remaining body bytes.
 * @return 0 on success, 1 on failure.
 */
static int test_stub_drain_body(test_socket_t client, long len) {
    char buf[256];

    while (len > 0) {
        int want;
        int n;

        want = (int)(len < (long)sizeof(buf) ? len : (long)sizeof(buf));
        n = (int)recv(client, buf, (int)want, 0);
        if (n <= 0) return 1;
        len -= n;
    }
    return 0;
}

/**
 * Serves one closed-before-response and one non-HTTP index response.
 * @param arg Control stub state.
 * @return None.
 */
static void test_control_stub_run(void *arg) {
    static const unsigned char bad_status_line[] =
        "NOT-HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    test_control_stub_t *stub;
    int request;

    stub = (test_control_stub_t *)arg;
    for (request = 0; request < 2; request++) {
        test_socket_t client;
        char head[4096];
        int head_len;

        client = accept(stub->fd, NULL, NULL);
        if (client == TEST_SOCKET_INVALID) return;
        test_socket_timeout(client, 2000U);
        if (test_stub_read_head(client, head, sizeof(head), &head_len) != 0 ||
            test_stub_drain_body(client,
                test_stub_content_length(head, head_len)) != 0)
        {
            test_socket_close(client);
            return;
        }
        if (request == 1)
            test_socket_send_all(client, bad_status_line,
                sizeof(bad_status_line) - 1);
        test_socket_close(client);
    }
}

/**
 * Reads one bounded HTTP request body into a terminated buffer.
 * @param client Client socket.
 * @param len Request body byte count.
 * @param body Output body buffer.
 * @param cap Output body capacity.
 * @return 0 on success, 1 on failure.
 */
static int test_stub_read_body(test_socket_t client, long len, char *body,
    size_t cap)
{
    size_t used;

    if (len < 0 || (size_t)len >= cap) return 1;
    used = 0;
    while (used < (size_t)len) {
        int n;

        n = (int)recv(client, body + used, (int)((size_t)len - used), 0);
        if (n <= 0) return 1;
        used += (size_t)n;
    }
    body[used] = '\0';
    return 0;
}

/**
 * Writes one minimal successful JSON HTTP response.
 * @param client Client socket.
 * @param body JSON response body.
 * @return 0 on success, 1 on failure.
 */
static int test_stub_respond_json(test_socket_t client, const char *body)
{
    char head[256];
    int n;

    n = snprintf(head, sizeof(head),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n", (unsigned)strlen(body));
    if (n < 0 || (size_t)n >= sizeof(head) ||
        test_socket_send_all(client, (const unsigned char *)head, (size_t)n) != 0 ||
        test_socket_send_all(client, (const unsigned char *)body,
            strlen(body)) != 0)
        return 1;
    return 0;
}

/**
 * Extracts one non-negative JSON sequence value from a control request.
 * @param body JSON request body.
 * @param sequence Output sequence.
 * @return 0 on success, 1 on failure.
 */
static int test_stub_sequence(const char *body, uint64_t *sequence)
{
    const char *cursor;
    uint64_t value;

    if (!body || !sequence) return 1;
    cursor = strstr(body, "\"seq\":");
    if (!cursor) return 1;
    cursor += strlen("\"seq\":");
    value = 0;
    if (*cursor < '0' || *cursor > '9') return 1;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10U + (uint64_t)(*cursor - '0');
        cursor++;
    }
    *sequence = value;
    return 0;
}

/**
 * Emulates publisher control replies while dropping one accepted heartbeat.
 * @param arg Publisher-control stub state.
 * @return None.
 */
static void test_publisher_control_stub_run(void *arg)
{
    test_publisher_control_stub_t *stub;

    stub = (test_publisher_control_stub_t *)arg;
    while (!atomic_load(&stub->stop)) {
        test_socket_t client;
        char body[4096];
        char head[4096];
        int head_len;
        long body_len;

        client = accept(stub->fd, NULL, NULL);
        if (client == TEST_SOCKET_INVALID) continue;
        test_socket_timeout(client, 2000U);
        body_len = -1;
        if (test_stub_read_head(client, head, sizeof(head), &head_len) == 0)
            body_len = test_stub_content_length(head, head_len);
        if (body_len < 0 || test_stub_read_body(client, body_len, body,
            sizeof(body)) != 0)
        {
            test_socket_close(client);
            continue;
        }
        if (strstr(body, "\"op\":\"challenge\""))
            test_stub_respond_json(client,
                "{\"nonce\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"issued_at\":1,\"expires_at\":61,\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"pkey\":\"0900000000000000000000000000000000000000000000000000000000000000\",\"bits\":0}");
        else if (strstr(body, "\"op\":\"register\"")) {
            memcpy(stub->register_body, body, strlen(body) + 1);
            atomic_fetch_add(&stub->register_count, 1);
            test_stub_respond_json(client, "{\"ok\":true}");
        } else if (strstr(body, "\"op\":\"heartbeat\"")) {
            uint64_t sequence;
            int count;

            sequence = 0;
            count = atomic_fetch_add(&stub->heartbeat_count, 1);
            if (test_stub_sequence(body, &sequence) == 0) {
                if (count == 0)
                    atomic_store(&stub->first_heartbeat_sequence, sequence);
                else if (count == 1)
                    atomic_store(&stub->second_heartbeat_sequence, sequence);
            }
            if (count == 0)
                atomic_store(&stub->dropped_heartbeat, 1);
            else
                test_stub_respond_json(client, "{\"ok\":true}");
        } else if (strstr(body, "\"op\":\"punch_poll\""))
            test_stub_respond_json(client, "{\"calls\":[]}");
        else
            test_stub_respond_json(client, "{\"ok\":true}");
        test_socket_close(client);
    }
}

#ifdef _WIN32
/**
 * Runs an index server thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_index_main(void *arg) {
    test_index_t *index;

    index = (test_index_t *)arg;
    index->result = redp2p_idx_run(index->ctx, NULL, index->port);
    return 0;
}

/**
 * Runs a publisher thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_publisher_main(void *arg) {
    test_publisher_t *publisher;

    publisher = (test_publisher_t *)arg;
    redp2p_pub_set_protocol(publisher->ctx, publisher->protocol);
    redp2p_set_local_port(publisher->ctx, publisher->bind_port);
    if (publisher->pass != NULL) redp2p_set_registration_pass(publisher->ctx, publisher->pass);
    if (publisher->state_dir != NULL)
        redp2p_test_set_state_dir(publisher->ctx, publisher->state_dir);
    atomic_store(&publisher->result,
        redp2p_pub_run(publisher->ctx, publisher->host, publisher->index_port,
            publisher->id, publisher->bind_port));
    return 0;
}

/**
 * Runs a consumer thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_consumer_main(void *arg) {
    test_consumer_t *consumer;

    consumer = (test_consumer_t *)arg;
    redp2p_pub_set_protocol(consumer->ctx, consumer->protocol);
    redp2p_set_local_port(consumer->ctx, consumer->bind_port);
    atomic_store(&consumer->result,
        redp2p_con_run(consumer->ctx, consumer->host, consumer->index_port,
            consumer->self_id, consumer->target_id, consumer->bind_port));
    return 0;
}

/**
 * Runs a UDP echo backend thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_udp_echo_main(void *arg) {
    test_udp_echo_run(arg);
    return 0;
}

/**
 * Runs a TCP echo backend thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_tcp_echo_main(void *arg) {
    test_tcp_echo_run(arg);
    return 0;
}

/**
 * Runs an incomplete-response control stub thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_control_stub_main(void *arg) {
    test_control_stub_run(arg);
    return 0;
}

/**
 * Runs a publisher-control stub thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static DWORD WINAPI test_publisher_control_stub_main(void *arg) {
    test_publisher_control_stub_run(arg);
    return 0;
}
#else
/**
 * Runs an index server thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static void *test_index_main(void *arg) {
    test_index_t *index;

    index = (test_index_t *)arg;
    index->result = redp2p_idx_run(index->ctx, NULL, index->port);
    return NULL;
}

/**
 * Runs a publisher thread.
 * @param arg Thread argument.
 * @return Thread status.
 */
static void *test_publisher_main(void *arg) {
    test_publisher_t *publisher;

    publisher = (test_publisher_t *)arg;
    redp2p_pub_set_protocol(publisher->ctx, publisher->protocol);
    redp2p_set_local_port(publisher->ctx, publisher->bind_port);
    if (publisher->pass != NULL) redp2p_set_registration_pass(publisher->ctx, publisher->pass);
    if (publisher->state_dir != NULL)
        redp2p_test_set_state_dir(publisher->ctx, publisher->state_dir);
    atomic_store(&publisher->result,
        redp2p_pub_run(publisher->ctx, publisher->host, publisher->index_port,
            publisher->id, publisher->bind_port));
    return NULL;
}

/**
 * Runs a consumer thread.
 * @param arg Thread argument.
 * @return NULL.
 */
static void *test_consumer_main(void *arg) {
    test_consumer_t *consumer;

    consumer = (test_consumer_t *)arg;
    redp2p_pub_set_protocol(consumer->ctx, consumer->protocol);
    redp2p_set_local_port(consumer->ctx, consumer->bind_port);
    atomic_store(&consumer->result,
        redp2p_con_run(consumer->ctx, consumer->host, consumer->index_port,
            consumer->self_id, consumer->target_id, consumer->bind_port));
    return NULL;
}

/**
 * Runs a UDP echo backend thread.
 * @param arg Thread argument.
 * @return NULL.
 */
static void *test_udp_echo_main(void *arg) {
    test_udp_echo_run(arg);
    return NULL;
}

/**
 * Runs a TCP echo backend thread.
 * @param arg Thread argument.
 * @return NULL.
 */
static void *test_tcp_echo_main(void *arg) {
    test_tcp_echo_run(arg);
    return NULL;
}

/**
 * Runs an incomplete-response control stub thread.
 * @param arg Thread argument.
 * @return NULL.
 */
static void *test_control_stub_main(void *arg) {
    test_control_stub_run(arg);
    return NULL;
}

/**
 * Runs a publisher-control stub thread.
 * @param arg Thread argument.
 * @return NULL.
 */
static void *test_publisher_control_stub_main(void *arg) {
    test_publisher_control_stub_run(arg);
    return NULL;
}
#endif

/**
 * Starts one thread.
 * @param thread Output thread handle.
 * @param fn Thread entry point.
 * @param arg Thread argument.
 * @return 0 on success, 1 on failure.
 */
static int test_thread_start(test_thread_t *thread,
#ifdef _WIN32
DWORD (WINAPI *fn)(void *),
#else
void *(*fn)(void *),
#endif
void *arg)
{
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, fn, arg, 0, NULL);
    return *thread != NULL ? 0 : 1;
#else
    return pthread_create(thread, NULL, fn, arg) == 0 ? 0 : 1;
#endif
}

/**
 * Joins one thread.
 * @param thread Thread handle.
 * @return 0 on success, 1 on failure.
 */
static int test_thread_join(test_thread_t thread) {
#ifdef _WIN32
    if (WaitForSingleObject(thread, 15000U) != WAIT_OBJECT_0) return 1;
    CloseHandle(thread);
    return 0;
#else
    return pthread_join(thread, NULL) == 0 ? 0 : 1;
#endif
}

/**
 * Starts an index server on a port.
 * @param index Index state.
 * @param port TCP port.
 * @return 0 on success, 1 on failure.
 */
static int test_index_start(test_index_t *index, unsigned short port) {
    memset(index, 0, sizeof(*index));
    index->port = port;
    index->result = 999;
    if (redp2p_context_create(&index->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&index->thread, test_index_main, index) != 0) return 1;
    return test_wait_port(port, 1) ? 0 : 1;
}

/**
 * Starts one configured index server on a port.
 * @param index Index state.
 * @param port TCP port.
 * @param seats Publisher capacity.
 * @param vip Optional VIP seat map.
 * @param pass Optional global registration password.
 * @return 0 on success, 1 on failure.
 */
static int test_index_start_configured(test_index_t *index,
    unsigned short port, size_t seats, const char *vip, const char *pass)
{
    char err[128];

    memset(index, 0, sizeof(*index));
    index->port = port;
    index->result = 999;
    if (redp2p_context_create(&index->ctx) != REDP2P_OK) return 1;
    if (redp2p_idx_set_capacity(index->ctx, seats) != REDP2P_OK) return 1;
    if (vip != NULL && redp2p_idx_set_vips(index->ctx, vip, err,
        sizeof(err)) != REDP2P_OK)
        return 1;
    if (pass != NULL && redp2p_set_registration_pass(index->ctx, pass) != REDP2P_OK) return 1;
    if (test_thread_start(&index->thread, test_index_main, index) != 0) return 1;
    return test_wait_port(port, 1) ? 0 : 1;
}

/**
 * Stops an index server.
 * @param index Index state.
 * @return 0 on success.
 */
static int test_index_stop(test_index_t *index) {
    if (index->ctx != NULL) redp2p_context_request_stop(index->ctx);
    test_port_open(index->port);
    test_thread_join(index->thread);
    if (index->ctx != NULL) redp2p_context_destroy(index->ctx);
    index->ctx = NULL;
    return 0;
}

/**
 * Starts a publisher context.
 * @param publisher Publisher state.
 * @param id Publisher id.
 * @param index_port Index control port.
 * @param bind_port Backend port value.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_start(test_publisher_t *publisher, const char *id,
unsigned short index_port, unsigned short bind_port)
{
    memset(publisher, 0, sizeof(*publisher));
    publisher->host = TEST_HOST;
    publisher->index_port = index_port;
    publisher->id = id;
    publisher->bind_port = bind_port;
    publisher->protocol = REDP2P_PROTO_TCP;
    atomic_init(&publisher->result, 999);
    if (redp2p_context_create(&publisher->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&publisher->thread, test_publisher_main,
        publisher) != 0) return 1;
    return test_wait_publisher_ready(publisher);
}

/**
 * Starts one password-configured TCP publisher context.
 * @param publisher Publisher state.
 * @param id Publisher id.
 * @param index_port Index control port.
 * @param bind_port Backend port value.
 * @param pass Registration password.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_start_pass(test_publisher_t *publisher,
    const char *id, unsigned short index_port, unsigned short bind_port,
    const char *pass)
{
    memset(publisher, 0, sizeof(*publisher));
    publisher->host = TEST_HOST;
    publisher->index_port = index_port;
    publisher->id = id;
    publisher->bind_port = bind_port;
    publisher->protocol = REDP2P_PROTO_TCP;
    publisher->pass = pass;
    atomic_init(&publisher->result, 999);
    if (redp2p_context_create(&publisher->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&publisher->thread, test_publisher_main,
        publisher) != 0)
        return 1;
    return test_wait_publisher_ready(publisher);
}

/**
 * Starts one state-dir-configured TCP publisher context.
 * @param publisher Publisher state.
 * @param id Publisher id.
 * @param index_port Index control port.
 * @param bind_port Backend port value.
 * @param pass Registration password.
 * @param state_dir Base directory for process files.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_start_state(test_publisher_t *publisher,
    const char *id, unsigned short index_port, unsigned short bind_port,
    const char *pass, const char *state_dir)
{
    memset(publisher, 0, sizeof(*publisher));
    publisher->host = TEST_HOST;
    publisher->index_port = index_port;
    publisher->id = id;
    publisher->bind_port = bind_port;
    publisher->protocol = REDP2P_PROTO_TCP;
    publisher->pass = pass;
    publisher->state_dir = state_dir;
    atomic_init(&publisher->result, 999);
    if (redp2p_context_create(&publisher->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&publisher->thread, test_publisher_main,
        publisher) != 0)
        return 1;
    return test_wait_publisher_ready(publisher);
}

/**
 * Stops a publisher context.
 * @param publisher Publisher state.
 * @return 0 on success.
 */
static int test_publisher_stop(test_publisher_t *publisher) {
    if (publisher->ctx != NULL) redp2p_context_request_stop(publisher->ctx);
    test_thread_join(publisher->thread);
    if (publisher->ctx != NULL) redp2p_context_destroy(publisher->ctx);
    publisher->ctx = NULL;
    return 0;
}

/**
 * Joins one publisher that must have exited without a local stop request.
 * @param publisher Publisher state.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_finish(test_publisher_t *publisher) {
    int result;

    result = test_thread_join(publisher->thread);
    if (publisher->ctx != NULL) redp2p_context_destroy(publisher->ctx);
    publisher->ctx = NULL;
    return result;
}

/**
 * Waits for one publisher operation to return.
 * @param publisher Publisher state.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return 1 when returned, 0 on timeout.
 */
static int test_publisher_wait_result(test_publisher_t *publisher,
    unsigned int timeout_ms)
{
    unsigned int elapsed;

    for (elapsed = 0; elapsed < timeout_ms; elapsed += 50U) {
        if (atomic_load(&publisher->result) != 999) return 1;
        test_sleep_ms(50U);
    }
    return atomic_load(&publisher->result) != 999;
}

/**
 * Starts a UDP publisher.
 * @return 0 on success, 1 on failure.
 */
static int test_udp_publisher_start(test_publisher_t *publisher,
    const char *id, unsigned short index_port, unsigned short bind_port)
{
    memset(publisher, 0, sizeof(*publisher));
    publisher->host = TEST_HOST;
    publisher->index_port = index_port;
    publisher->id = id;
    publisher->bind_port = bind_port;
    publisher->protocol = REDP2P_PROTO_UDP;
    atomic_init(&publisher->result, 999);
    if (redp2p_context_create(&publisher->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&publisher->thread, test_publisher_main,
        publisher) != 0)
        return 1;
    return test_wait_publisher_ready(publisher);
}

/**
 * Starts a UDP consumer.
 * @return 0 on success, 1 on failure.
 */
static int test_udp_consumer_start(test_consumer_t *consumer,
    const char *self_id, const char *target_id, unsigned short index_port,
    unsigned short bind_port)
{
    memset(consumer, 0, sizeof(*consumer));
    consumer->host = TEST_HOST;
    consumer->index_port = index_port;
    consumer->self_id = self_id;
    consumer->target_id = target_id;
    consumer->bind_port = bind_port;
    consumer->protocol = REDP2P_PROTO_UDP;
    atomic_init(&consumer->result, 999);
    if (redp2p_context_create(&consumer->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&consumer->thread, test_consumer_main,
        consumer) != 0)
        return 1;
    return 0;
}

/**
 * Stops one consumer context.
 * @return 0 on success.
 */
static int test_consumer_stop(test_consumer_t *consumer) {
    struct sockaddr_in addr;
    test_socket_t fd;

    if (consumer->ctx != NULL) redp2p_context_request_stop(consumer->ctx);
    fd = socket(AF_INET, consumer->protocol == REDP2P_PROTO_TCP ?
        SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd != TEST_SOCKET_INVALID) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(consumer->bind_port);
        addr.sin_addr.s_addr = htonl(0x7f000001UL);
        if (consumer->protocol == REDP2P_PROTO_TCP)
            connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
        else
            sendto(fd, "", 0, 0, (const struct sockaddr *)&addr,
                sizeof(addr));
        test_socket_close(fd);
    }
    test_thread_join(consumer->thread);
    if (consumer->ctx != NULL) redp2p_context_destroy(consumer->ctx);
    consumer->ctx = NULL;
    return 0;
}

/**
 * Starts one loopback UDP echo backend.
 * @return 0 on success, 1 on failure.
 */
static int test_udp_echo_start(test_udp_echo_t *echo, unsigned short port) {
    struct sockaddr_in addr;

    memset(echo, 0, sizeof(*echo));
    atomic_init(&echo->stop, 0);
    echo->port = port;
    echo->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (echo->fd == TEST_SOCKET_INVALID) return 1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    if (bind(echo->fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        test_socket_close(echo->fd);
        return 1;
    }
    test_socket_timeout(echo->fd, 100U);
    return test_thread_start(&echo->thread, test_udp_echo_main, echo);
}

/**
 * Stops one loopback UDP echo backend.
 * @return 0 on success.
 */
static int test_udp_echo_stop(test_udp_echo_t *echo) {
    struct sockaddr_in addr;
    test_socket_t fd;

    atomic_store(&echo->stop, 1);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd != TEST_SOCKET_INVALID) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(echo->port);
        addr.sin_addr.s_addr = htonl(0x7f000001UL);
        sendto(fd, "", 0, 0, (const struct sockaddr *)&addr, sizeof(addr));
        test_socket_close(fd);
    }
    test_thread_join(echo->thread);
    test_socket_close(echo->fd);
    echo->fd = TEST_SOCKET_INVALID;
    return 0;
}

/**
 * Starts one loopback TCP echo backend.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_echo_start(test_tcp_echo_t *echo, unsigned short port) {
    struct sockaddr_in addr;
    unsigned int i;
    int reuse;

    memset(echo, 0, sizeof(*echo));
    atomic_init(&echo->stop, 0);
    echo->port = port;
    for (i = 0; i < TEST_TCP_CLIENTS; i++)
        echo->clients[i] = TEST_SOCKET_INVALID;
    echo->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (echo->fd == TEST_SOCKET_INVALID) return 1;
    reuse = 1;
    setsockopt(echo->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
        sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    if (bind(echo->fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(echo->fd, 8) != 0)
    {
        test_socket_close(echo->fd);
        return 1;
    }
    test_socket_timeout(echo->fd, 100U);
    return test_thread_start(&echo->thread, test_tcp_echo_main, echo);
}

/**
 * Stops one loopback TCP echo backend.
 * @return 0 on success.
 */
static int test_tcp_echo_stop(test_tcp_echo_t *echo) {
    atomic_store(&echo->stop, 1);
    test_socket_shutdown(echo->fd);
    test_thread_join(echo->thread);
    test_socket_close(echo->fd);
    echo->fd = TEST_SOCKET_INVALID;
    return 0;
}

/**
 * Starts one bounded incomplete-response control stub.
 * @param stub Control stub state.
 * @param port TCP control port.
 * @return 0 on success, 1 on failure.
 */
static int test_control_stub_start(test_control_stub_t *stub,
    unsigned short port)
{
    struct sockaddr_in addr;
    int reuse;

    memset(stub, 0, sizeof(*stub));
    stub->port = port;
    stub->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (stub->fd == TEST_SOCKET_INVALID) return 1;
    reuse = 1;
    setsockopt(stub->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
        sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    if (bind(stub->fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(stub->fd, 1) != 0)
    {
        test_socket_close(stub->fd);
        stub->fd = TEST_SOCKET_INVALID;
        return 1;
    }
    return test_thread_start(&stub->thread, test_control_stub_main, stub);
}

/**
 * Stops one completed incomplete-response control stub.
 * @param stub Control stub state.
 * @return 0 on success, 1 on failure.
 */
static int test_control_stub_stop(test_control_stub_t *stub) {
    int result;

    result = test_thread_join(stub->thread);
    if (stub->fd != TEST_SOCKET_INVALID) test_socket_close(stub->fd);
    stub->fd = TEST_SOCKET_INVALID;
    return result;
}

/**
 * Starts one publisher-control stub that drops its first heartbeat response.
 * @param stub Publisher-control stub state.
 * @param port TCP control port.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_control_stub_start(test_publisher_control_stub_t *stub,
    unsigned short port)
{
    struct sockaddr_in addr;
    int reuse;

    memset(stub, 0, sizeof(*stub));
    stub->port = port;
    atomic_init(&stub->stop, 0);
    atomic_init(&stub->register_count, 0);
    atomic_init(&stub->heartbeat_count, 0);
    atomic_init(&stub->dropped_heartbeat, 0);
    atomic_init(&stub->first_heartbeat_sequence, 0);
    atomic_init(&stub->second_heartbeat_sequence, 0);
    stub->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (stub->fd == TEST_SOCKET_INVALID) return 1;
    reuse = 1;
    setsockopt(stub->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
        sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    if (bind(stub->fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(stub->fd, 8) != 0)
    {
        test_socket_close(stub->fd);
        stub->fd = TEST_SOCKET_INVALID;
        return 1;
    }
    test_socket_timeout(stub->fd, 100U);
    return test_thread_start(&stub->thread, test_publisher_control_stub_main,
        stub);
}

/**
 * Stops one publisher-control stub after its observed requests are complete.
 * @param stub Publisher-control stub state.
 * @return 0 on success, 1 on failure.
 */
static int test_publisher_control_stub_stop(test_publisher_control_stub_t *stub)
{
    test_socket_t wake;
    int result;

    atomic_store(&stub->stop, 1);
    wake = test_tcp_connect(stub->port);
    if (wake != TEST_SOCKET_INVALID) test_socket_close(wake);
    result = test_thread_join(stub->thread);
    if (stub->fd != TEST_SOCKET_INVALID) test_socket_close(stub->fd);
    stub->fd = TEST_SOCKET_INVALID;
    return result;
}

/**
 * Verifies the real publisher register request protects the control secret.
 * @param body Captured serialized register request.
 * @return 1 when the wire contract is protected, 0 otherwise.
 */
static int test_publisher_register_wire_valid(const char *body,
    const char *secret)
{
    char pkey[65];
    char encrypted_secret[65];
    unsigned char bytes[32];

    if (!body || !test_json_string(body, "pkey", pkey,
        sizeof(pkey)) || !test_json_string(body, "secret", encrypted_secret,
        sizeof(encrypted_secret)) || !test_hex_decode(pkey, bytes, sizeof(bytes)) ||
        !test_hex_decode(encrypted_secret, bytes, sizeof(bytes)) ||
        (secret && strstr(body, secret)) ||
        strstr(body, "\"key\":") ||
        strstr(body, "\"plain_secret\":") ||
        strstr(body, "\"control_secret\":") ||
        strstr(body, "auth_scheme")) return 0;
    return 1;
}

/**
 * Starts one TCP publisher context.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_publisher_start(test_publisher_t *publisher,
    const char *id, unsigned short index_port, unsigned short bind_port)
{
    memset(publisher, 0, sizeof(*publisher));
    publisher->host = TEST_HOST;
    publisher->index_port = index_port;
    publisher->id = id;
    publisher->bind_port = bind_port;
    publisher->protocol = REDP2P_PROTO_TCP;
    atomic_init(&publisher->result, 999);
    if (redp2p_context_create(&publisher->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&publisher->thread, test_publisher_main,
        publisher) != 0)
        return 1;
    return test_wait_publisher_ready(publisher);
}

/**
 * Starts one TCP consumer context.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_consumer_start(test_consumer_t *consumer,
    const char *self_id, const char *target_id, unsigned short index_port,
    unsigned short bind_port)
{
    memset(consumer, 0, sizeof(*consumer));
    consumer->host = TEST_HOST;
    consumer->index_port = index_port;
    consumer->self_id = self_id;
    consumer->target_id = target_id;
    consumer->bind_port = bind_port;
    consumer->protocol = REDP2P_PROTO_TCP;
    atomic_init(&consumer->result, 999);
    if (redp2p_context_create(&consumer->ctx) != REDP2P_OK) return 1;
    if (test_thread_start(&consumer->thread, test_consumer_main,
        consumer) != 0)
        return 1;
    return test_wait_consumer_ready(consumer);
}

/**
 * Opens one bounded loopback TCP connection.
 * @param port TCP port.
 * @return Connected socket, or TEST_SOCKET_INVALID on failure.
 */
static test_socket_t test_tcp_connect(unsigned short port) {
    test_socket_t fd;
    struct sockaddr_in addr;
    unsigned int attempt;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    fd = TEST_SOCKET_INVALID;
    for (attempt = 0; attempt < 20U; attempt++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == TEST_SOCKET_INVALID) return TEST_SOCKET_INVALID;
        test_socket_timeout(fd, 10000U);
        if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0)
            break;
        test_socket_close(fd);
        fd = TEST_SOCKET_INVALID;
        test_sleep_ms(100U);
    }
    return fd;
}

/**
 * Fills one buffer with deterministic non-repeating test bytes.
 * @param data Destination buffer.
 * @param len Byte count.
 * @param seed Pattern seed.
 * @return None.
 */
static void test_tcp_pattern(unsigned char *data, size_t len,
unsigned int seed)
{
    size_t i;

    for (i = 0; i < len; i++)
        data[i] = (unsigned char)((i * 131U + i / 251U + seed) & 0xffU);
}

/**
 * Sends and verifies one exact exchange on an open TCP session.
 * @param fd Connected socket.
 * @param data Bytes to exchange.
 * @param len Byte count.
 * @return Received byte count, or -1 on failure.
 */
static int test_tcp_exchange(test_socket_t fd, const unsigned char *data,
size_t len)
{
    unsigned char *received;
    int result;

    received = (unsigned char *)malloc(len == 0 ? 1 : len);
    if (received == NULL) return -1;
    result = -1;
    if (test_socket_send_all(fd, data, len) == 0 &&
        test_socket_receive_exact(fd, received, len) == 0 &&
        memcmp(received, data, len) == 0)
        result = (int)len;
    free(received);
    return result;
}

/**
 * Sends bytes through one local TCP adapter and waits for their exact echo.
 * @param port TCP port.
 * @param data Bytes to exchange.
 * @param len Byte count.
 * @return Received byte count, or -1 on failure.
 */
static int test_tcp_roundtrip(unsigned short port, const unsigned char *data,
size_t len)
{
    test_socket_t fd;
    int result;

    fd = test_tcp_connect(port);
    if (fd == TEST_SOCKET_INVALID) return -1;
    result = test_tcp_exchange(fd, data, len);
    test_socket_close(fd);
    return result;
}

/**
 * Half-closes the sending direction of one TCP socket.
 * @param fd Connected socket.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_shutdown_send(test_socket_t fd) {
#ifdef _WIN32
    return shutdown(fd, SD_SEND) == 0 ? 0 : 1;
#else
    return shutdown(fd, SHUT_WR) == 0 ? 0 : 1;
#endif
}

/**
 * Drains bounded in-flight bytes and waits for peer closure.
 * @param fd Connected socket.
 * @param limit Maximum bytes accepted before closure.
 * @return 0 when closure is observed, 1 otherwise.
 */
static int test_tcp_wait_closed(test_socket_t fd, size_t limit) {
    unsigned char received[1024];
    size_t total;

    total = 0;
    while (total <= limit) {
        int n;

        n = (int)recv(fd, (char *)received, sizeof(received), 0);
        if (n == 0) return 0;
        if (n < 0 && test_socket_interrupted()) continue;
        if (n < 0) return 1;
        total += (size_t)n;
    }
    return 1;
}

/**
 * Issues one HTTP/1.1 JSON request to an index and checks status plus one
 * response-body fragment.
 * @param source Bound transport source IP, or NULL for the default.
 * @param port Index port.
 * @param body JSON request body.
 * @param body_len Request body byte count.
 * @param expected_status Expected HTTP status code.
 * @param expected_fragment Text fragment required inside the response, or
 *                          NULL when the response body is not inspected.
 * @return 0 on success, 1 on failure.
 */
static int test_http_request_from(const char *source, unsigned short port,
    const char *body,
    size_t body_len, int expected_status, const char *expected_fragment)
{
    test_socket_t fd;
    char head[512];
    char response[8192];
    const char *cursor;
    size_t total;
    int status;
    int digits;
    int n;

    if (source) {
        struct sockaddr_in local;
        struct sockaddr_in remote;
        memset(&local, 0, sizeof(local));
        memset(&remote, 0, sizeof(remote));
        local.sin_family = AF_INET;
        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        remote.sin_addr.s_addr = htonl(0x7f000001UL);
        if (inet_pton(AF_INET, source, &local.sin_addr) != 1) return 1;
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == TEST_SOCKET_INVALID) return 1;
        test_socket_timeout(fd, 2000U);
        if (bind(fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
            connect(fd, (const struct sockaddr *)&remote, sizeof(remote)) != 0)
        {
            test_socket_close(fd);
            return 1;
        }
    } else fd = test_tcp_connect(port);
    if (fd == TEST_SOCKET_INVALID) return 1;
    test_socket_timeout(fd, 2000U);
    n = snprintf(head, sizeof(head),
        "POST /redp2p/ HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %u\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n", TEST_HOST, (unsigned)body_len);
    if (n < 0 || (size_t)n >= sizeof(head) ||
        test_socket_send_all(fd, (const unsigned char *)head, (size_t)n) != 0 ||
        test_socket_send_all(fd, (const unsigned char *)body, body_len) != 0)
    {
        test_socket_close(fd);
        return 1;
    }
    total = 0;
    response[0] = '\0';
    while (total < sizeof(response) - 1) {
        int m;

        m = (int)recv(fd, response + total, sizeof(response) - 1 - total, 0);
        if (m <= 0) break;
        total += (size_t)m;
    }
    response[total] = '\0';
    test_socket_close(fd);
    if (total == 0 || strncmp(response, "HTTP/", 5) != 0) return 1;
    cursor = strchr(response, ' ');
    status = -1;
    if (cursor) {
        while (*cursor == ' ') cursor++;
        status = 0;
        for (digits = 0; digits < 3 && cursor[digits] >= '0' &&
            cursor[digits] <= '9'; digits++)
            status = status * 10 + (cursor[digits] - '0');
        if (digits != 3) status = -1;
    }
    if (status != expected_status) return 1;
    if (expected_fragment != NULL &&
        strstr(response, expected_fragment) == NULL)
        return 1;
    return 0;
}

/**
 * Checks one HTTP response over the default loopback source.
 * @param port Index port.
 * @param body Request JSON.
 * @param body_len JSON byte count.
 * @param expected_status Expected HTTP status.
 * @param expected_fragment Expected response text, or NULL.
 * @return 0 on success, 1 on mismatch.
 */
static int test_http_request(unsigned short port, const char *body,
    size_t body_len, int expected_status, const char *expected_fragment)
{
    return test_http_request_from(NULL, port, body, body_len, expected_status,
        expected_fragment);
}

/**
 * Sends one HTTP JSON request, then closes without reading its response.
 * @param source Bound transport source IP, or NULL for the default.
 * @param port Index port.
 * @param body JSON request body.
 * @param body_len Request body byte count.
 * @return 0 on send success, 1 on failure.
 */
static int test_http_send_and_close(unsigned short port, const char *body,
    size_t body_len)
{
    test_socket_t fd;
    char head[512];
    int n;

    fd = test_tcp_connect(port);
    if (fd == TEST_SOCKET_INVALID) return 1;
    test_socket_timeout(fd, 2000U);
    n = snprintf(head, sizeof(head),
        "POST /redp2p/ HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %u\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n", TEST_HOST, (unsigned)body_len);
    if (n < 0 || (size_t)n >= sizeof(head) ||
        test_socket_send_all(fd, (const unsigned char *)head, (size_t)n) != 0 ||
        test_socket_send_all(fd, (const unsigned char *)body, body_len) != 0)
    {
        test_socket_close(fd);
        return 1;
    }
    test_socket_close(fd);
    return 0;
}

/**
 * Sends one raw HTTP request and returns its response status code.
 * @param port Index port.
 * @param request Raw request bytes.
 * @param request_len Request byte count.
 * @return HTTP status code, or -1 on failure.
 */
static int test_http_raw_status(unsigned short port, const char *request,
    size_t request_len)
{
    test_socket_t fd;
    const char *cursor;
    char response[8192];
    size_t total;
    int status;
    int digits;

    fd = test_tcp_connect(port);
    if (fd == TEST_SOCKET_INVALID) return -1;
    test_socket_timeout(fd, 2000U);
    if (test_socket_send_all(fd, (const unsigned char *)request,
        request_len) != 0) {
        test_socket_close(fd);
        return -1;
    }
    total = 0;
    response[0] = '\0';
    while (total < sizeof(response) - 1) {
        int m;

        m = (int)recv(fd, response + total, sizeof(response) - 1 - total, 0);
        if (m <= 0) break;
        total += (size_t)m;
    }
    response[total] = '\0';
    test_socket_close(fd);
    if (total == 0 || strncmp(response, "HTTP/", 5) != 0) return -1;
    cursor = strchr(response, ' ');
    status = -1;
    if (cursor) {
        while (*cursor == ' ') cursor++;
        status = 0;
        for (digits = 0; digits < 3 && cursor[digits] >= '0' &&
            cursor[digits] <= '9'; digits++)
            status = status * 10 + (cursor[digits] - '0');
        if (digits != 3) status = -1;
    }
    return status;
}

/**
 * Sends one UDP datagram and waits for its echo.
 * @return Received length, or -1 on timeout or mismatch.
 */
static int test_udp_roundtrip(unsigned short port, const unsigned char *data,
    size_t len, unsigned int timeout_ms)
{
    test_socket_t fd;
    struct sockaddr_in addr;
    unsigned char received[REDP2P_UDP_PAYLOAD_MAX + 1];
    unsigned int attempts;
    unsigned int i;
    int n;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == TEST_SOCKET_INVALID) return -1;
    test_socket_timeout(fd, 500U);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    attempts = timeout_ms / 500U;
    if (attempts == 0) attempts = 1;
    n = -1;
    for (i = 0; i < attempts; i++) {
        if (sendto(fd, (const char *)data, len, 0,
            (const struct sockaddr *)&addr, sizeof(addr)) < 0)
            continue;
        n = (int)recvfrom(fd, (char *)received, sizeof(received), 0,
            NULL, NULL);
        if (n >= 0) break;
    }
    test_socket_close(fd);
    if (n < 0 || (size_t)n != len) {
        fprintf(stderr, "udp roundtrip length: sent=%lu received=%d\n",
            (unsigned long)len, n);
        return -1;
    }
    if (len > 0 && memcmp(received, data, len) != 0) {
        fprintf(stderr, "udp roundtrip payload mismatch\n");
        return -1;
    }
    return n;
}

/**
 * Sends one UDP datagram without waiting for a response.
 * @return 0 on success, 1 on failure.
 */
static int test_udp_send_only(unsigned short port, const unsigned char *data,
    size_t len)
{
    test_socket_t fd;
    struct sockaddr_in addr;
    int result;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == TEST_SOCKET_INVALID) return 1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7f000001UL);
    result = sendto(fd, (const char *)data, len, 0,
        (const struct sockaddr *)&addr, sizeof(addr)) < 0;
    test_socket_close(fd);
    return result;
}

/**
 * Records one publisher id.
 * @param id Publisher id.
 * @param userdata Publisher collection.
 * @return None.
 */
static void test_on_publisher(const char *id, void *userdata) {
    test_publishers_t *publishers;

    publishers = (test_publishers_t *)userdata;
    if (publishers->count >= 8) return;
    strncpy(publishers->ids[publishers->count], id, REDP2P_ID_MAX);
    publishers->ids[publishers->count][REDP2P_ID_MAX] = '\0';
    publishers->count++;
}

/**
 * Returns whether one publisher id was recorded.
 * @param publishers Publisher collection.
 * @param id Publisher id.
 * @return 1 when present, 0 otherwise.
 */
static int test_has_publisher(test_publishers_t *publishers, const char *id) {
    size_t i;

    for (i = 0; i < publishers->count; i++) {
        if (strcmp(publishers->ids[i], id) == 0) return 1;
    }
    return 0;
}

/**
 * Tests kc_redp2p_candidate_type_values.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_candidate_type_values(void) {
    const char *name = "kc_redp2p_candidate_type_values";
    const char *detail = "candidate type values preserve public ordering";
    int fail;

    fail = 0;
    fail += expect_int("host candidate value", 1, REDP2P_CAND_HOST);
    fail += expect_int("observed candidate value", 2, REDP2P_CAND_OBSERVED);
    fail += expect_int("relay candidate value", 3, REDP2P_CAND_RELAY);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_open(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_open";
    const char *detail = "open validates and allocates context";
    int fail;

    fail = 0;
    ctx = NULL;
    fail += expect_int("open NULL", REDP2P_ERROR, redp2p_context_create(NULL));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_true("context is set", ctx != NULL);
    if (ctx != NULL) redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_close(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_close";
    const char *detail = "close releases context";
    int fail;

    fail = 0;
    fail += expect_int("close NULL", REDP2P_ERROR, redp2p_context_destroy(NULL));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("close context", REDP2P_OK, redp2p_context_destroy(ctx));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_stop(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_stop";
    const char *detail = "stop is idempotent on context";
    int fail;

    fail = 0;
    fail += expect_int("stop NULL", REDP2P_EINVAL, redp2p_context_request_stop(NULL));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_true("stop initially clear", !redp2p_is_stop_requested(ctx));
    fail += expect_int("stop context", REDP2P_OK, redp2p_context_request_stop(ctx));
    fail += expect_true("stop requested", redp2p_is_stop_requested(ctx));
    fail += expect_int("stop context twice", REDP2P_OK, redp2p_context_request_stop(ctx));
    fail += expect_true("stop remains requested", redp2p_is_stop_requested(ctx));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_version(void) {
    const char *name = "kc_redp2p_version";
    const char *detail = "public version API returns the generated build version";
    uint64_t version = kc_redp2p_version();
    int fail;

    fail = expect_true("version is available", version != 0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_strerror(void) {
    const char *name = "kc_redp2p_strerror";
    const char *detail = "strerror maps codes to messages";
    int fail;

    fail = 0;
    fail += expect_string("OK text", "OK", redp2p_strerror(REDP2P_OK));
    fail += expect_string("ERROR text", "general error", redp2p_strerror(REDP2P_ERROR));
    fail += expect_string("ENET text", "network error", redp2p_strerror(REDP2P_ENET));
    fail += expect_string("ENOENT text", "peer not found", redp2p_strerror(REDP2P_ENOENT));
    fail += expect_string("ETIMEOUT text", "timeout", redp2p_strerror(REDP2P_ETIMEOUT));
    fail += expect_string("EFULL text", "peer table full", redp2p_strerror(REDP2P_EFULL));
    fail += expect_string("EINVAL text", "invalid argument",
        redp2p_strerror(REDP2P_EINVAL));
    fail += expect_string("EPROTO text", "protocol error",
        redp2p_strerror(REDP2P_EPROTO));
    fail += expect_string("EAUTH text", "authentication failed",
        redp2p_strerror(REDP2P_EAUTH));
    fail += expect_string("EVERSION text", "unsupported protocol version",
        redp2p_strerror(REDP2P_EVERSION));
    fail += expect_string("EPUNCH text", "direct connectivity failed",
        redp2p_strerror(REDP2P_EPUNCH));
    fail += expect_string("EEXIST text", "publisher already registered",
        redp2p_strerror(REDP2P_EEXIST));
    fail += expect_string("unknown text", "unknown error", redp2p_strerror(999));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_is_valid_id.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_is_valid_id(void) {
    const char *name = "kc_redp2p_is_valid_id";
    const char *detail = "publisher id validation enforces strict ids";
    int fail;

    fail = 0;
    fail += expect_int("alphanumeric id", 1, redp2p_is_valid_id("abcXYZ123"));
    fail += expect_int("single id", 1, redp2p_is_valid_id("a"));
    fail += expect_int("NULL id", 0, redp2p_is_valid_id(NULL));
    fail += expect_int("empty id", 0, redp2p_is_valid_id(""));
    fail += expect_int("punctuation id", 0, redp2p_is_valid_id("bad:id"));
    fail += expect_int("space id", 0, redp2p_is_valid_id("bad id"));
    fail += expect_int("long id", 0, redp2p_is_valid_id(
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_is_valid_pass_token.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_is_valid_pass_token(void) {
    const char *name = "kc_redp2p_is_valid_pass_token";
    const char *detail = "pass token validation accepts ordinary password bytes";
    char too_long[REDP2P_PASS_MAX + 2];
    int fail;

    fail = 0;
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    fail += expect_int("symbol pass", 1,
        redp2p_is_valid_pass_token("a!\"\\[]{}()<>|&*^$#?~`"));
    fail += expect_int("non-ASCII pass", 1,
        redp2p_is_valid_pass_token("pass\xc3\xb1"));
    fail += expect_int("single pass", 1, redp2p_is_valid_pass_token("x"));
    fail += expect_int("NULL pass", 0, redp2p_is_valid_pass_token(NULL));
    fail += expect_int("empty pass", 0, redp2p_is_valid_pass_token(""));
    fail += expect_int("space pass", 0, redp2p_is_valid_pass_token("bad pass"));
    fail += expect_int("tab pass", 0, redp2p_is_valid_pass_token("bad\tpass"));
    fail += expect_int("control pass", 0, redp2p_is_valid_pass_token("bad\x1fpass"));
    fail += expect_int("DEL pass", 0, redp2p_is_valid_pass_token("bad\x7fpass"));
    fail += expect_int("long pass", 0, redp2p_is_valid_pass_token(too_long));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Sends a punch using a real bound HTTP source address.
 * @param port Index port.
 * @param source Transport source IP.
 * @param target Publisher id.
 * @param status Expected HTTP status.
 * @return 0 on success, 1 on mismatch.
 */
static int test_rate_punch(unsigned short port, const char *source,
    const char *target, int status)
{
    char body[256];
    int n = snprintf(body, sizeof(body),
        "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"ratecaller\","
        "\"target_id\":\"%s\",\"session\":\"ratesess\"}", target);
    if (n < 0 || (size_t)n >= sizeof(body)) return 1;
    return test_http_request_from(source, port, body, (size_t)n, status,
        status == 200 ? "\"ok\":true" : "\"error\":\"rate_limited\"");
}

/**
 * Exercises dual bucket bursts, refill, isolation and pending-call atomicity.
 * @param port Isolated index port.
 * @return Number of failed observations.
 */
static int test_punch_rate_limits(unsigned short port)
{
    test_index_t index;
    int fail = 0;
    int i;
    const char *secret = "0123456789abcdef";

    fail += expect_int("start rate index", 0,
        test_index_start_configured(&index, port, 4, NULL, NULL));
    if (fail) return fail;
    redp2p_idx_set_max_consumers(index.ctx, 64);
    fail += expect_int("register rate target", 0,
        test_register_publisher(port, "rateone", secret, 41100));
    fail += expect_int("register isolated rate target", 0,
        test_register_publisher(port, "ratetwo", secret, 41101));
    for (i = 0; i < 20; i++)
        fail += expect_int("source burst admitted", 0,
            test_rate_punch(port, "127.0.0.20", "rateone", 200));
    fail += expect_int("source burst exhausted", 0,
        test_rate_punch(port, "127.0.0.20", "ratetwo", 429));
    for (i = 0; i < 20; i++)
        fail += expect_int("independent source reaches target capacity", 0,
            test_rate_punch(port, "127.0.0.21", "rateone", 200));
    fail += expect_int("target exhausted across sources", 0,
        test_rate_punch(port, "127.0.0.22", "rateone", 429));
    for (i = 0; i < 20; i++)
        fail += expect_int("target rejection preserves source credit", 0,
            test_rate_punch(port, "127.0.0.22", "ratetwo", 200));
    for (i = 0; i < 20; i++)
        fail += expect_int("source rejection preserves target credit", 0,
            test_rate_punch(port, "127.0.0.23", "ratetwo", 200));
    fail += expect_int("isolated target capacity enforced", 0,
        test_rate_punch(port, "127.0.0.24", "ratetwo", 429));
    test_sleep_ms(250U);
    fail += expect_int("both buckets refill", 0,
        test_rate_punch(port, "127.0.0.20", "rateone", 200));
    for (i = 0; i < 2; i++) {
        const char *target = i == 0 ? "rateone" : "ratetwo";
        int count = 0;
        int seq;
        for (seq = 1; seq <= 12; seq++) {
            char proof_input[80], proof[65], body[256], response[8192];
            unsigned char hash[32];
            const char *cursor;
            int n = snprintf(proof_input, sizeof(proof_input),
                "punch_poll\n%s\n%d", target, seq);
            test_hmac_sha256((const unsigned char *)secret, strlen(secret),
                (const unsigned char *)proof_input, (size_t)n, hash);
            test_hex_encode(hash, sizeof(hash), proof);
            n = snprintf(body, sizeof(body),
                "{\"op\":\"punch_poll\",\"id\":\"%s\",\"seq\":%d,"
                "\"proof\":\"%s\"}", target, seq, proof);
            fail += expect_int("drain rate test calls", 0,
                test_http_response(port, body, (size_t)n, response,
                    sizeof(response)));
            cursor = response;
            while ((cursor = strstr(cursor, "\"self_id\":")) != NULL) {
                count++;
                cursor++;
            }
        }
        fail += expect_int("rate rejections enqueue no calls", i == 0 ? 41 : 40,
            count);
    }
    test_index_stop(&index);
    return fail;
}

/**
 * Tests kc_redp2p_serve_index.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_serve_index(void) {
    test_index_t index;
    test_publisher_t first;
    test_publisher_t second;
    test_publisher_t third;
    redp2p_t *stopped;
    char too_many[4096];
    char overlong_line[TEST_HTTP_LINE_MAX * TEST_HTTP_HEADERS_MAX + 64];
    char incomplete_body[64];
    char overflow_req[1024];
    char worst_candidates[1024];
    char per_target_req[1024];
    char foreign_state_dir[768];
    test_socket_t timeout_fds[TEST_HTTP_CONNECTIONS];
    unsigned short port;
    uint64_t cooling_start;
    size_t used;
    unsigned int cooling_elapsed;
    int i;
    const char *name = "kc_redp2p_serve_index";
    const char *detail = "serve index responds to control requests";
    int fail;

    fail = 0;
    snprintf(foreign_state_dir, sizeof(foreign_state_dir),
        "%s/vip-takeover", test_home_path);
    port = (unsigned short)(test_port_base() + 1U);
    fail += expect_int("serve NULL", REDP2P_EINVAL,
        redp2p_idx_run(NULL, TEST_HOST, port));
    fail += expect_int("open stopped index context", REDP2P_OK,
        redp2p_context_create(&stopped));
    fail += expect_int("zero per-publisher consumer window restores default", REDP2P_OK,
        redp2p_idx_set_max_consumers(stopped, 0));
    fail += expect_int("stop index before entry", REDP2P_OK,
        redp2p_context_request_stop(stopped));
    fail += expect_int("serve honors prior stop", REDP2P_OK,
        redp2p_idx_run(stopped, TEST_HOST, port));
    fail += expect_true("prior-stopped index did not listen",
        !test_port_open(port));
    memset(&index, 0, sizeof(index));
    index.ctx = stopped;
    index.port = port;
    index.result = 999;
    if (test_thread_start(&index.thread, test_index_main, &index) != 0)
        return 1;
    if (!test_wait_port(port, 1)) return 1;
    fail += expect_true("index accepts TCP", test_port_open(port));
    fail += expect_int("reject malformed JSON", 0,
        test_http_request(port, "{\"op\":\"register\",id:1}",
            strlen("{\"op\":\"register\",id:1}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject non-object JSON", 0,
        test_http_request(port, "[1,2]", 5, 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject duplicate JSON fields", 0,
        test_http_request(port, "{\"op\":\"list\",\"op\":\"list\"}",
            strlen("{\"op\":\"list\",\"op\":\"list\"}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject missing op", 0,
        test_http_request(port, "{\"id\":\"x\"}",
            strlen("{\"id\":\"x\"}"), 400, "\"error\":\"bad_request\""));
    fail += expect_int("reject unknown op", 0,
        test_http_request(port, "{\"op\":\"frobnicate\"}",
            strlen("{\"op\":\"frobnicate\"}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject invalid register id", 0,
        test_http_request(port, "{\"op\":\"register\",\"id\":\"bad:id\"}",
            strlen("{\"op\":\"register\",\"id\":\"bad:id\"}"), 400,
            "\"error\":\"invalid_id\""));
    fail += expect_int("reject lookup missing id", 0,
        test_http_request(port, "{\"op\":\"lookup\"}",
            strlen("{\"op\":\"lookup\"}"), 400, "\"error\":\"bad_request\""));
    fail += expect_int("register publisher proof vector", 0,
        test_register_publisher(port, "proofpub", "0123456789abcdef", 41009));
    fail += expect_int("send heartbeat then lose response", 0,
        test_http_send_and_close(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":1,\"proof\":\"f4e616982629b13857d62c938dfd22a2ed831a4f4d1c735507cc0ffe374a1f1d\",\"proto\":1,\"udp_port\":41009}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":1,\"proof\":\"f4e616982629b13857d62c938dfd22a2ed831a4f4d1c735507cc0ffe374a1f1d\",\"proto\":1,\"udp_port\":41009}")));
    test_sleep_ms(50U);
    fail += expect_int("lost-response heartbeat sequence is consumed", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":1,\"proof\":\"f4e616982629b13857d62c938dfd22a2ed831a4f4d1c735507cc0ffe374a1f1d\",\"proto\":1,\"udp_port\":41009}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":1,\"proof\":\"f4e616982629b13857d62c938dfd22a2ed831a4f4d1c735507cc0ffe374a1f1d\",\"proto\":1,\"udp_port\":41009}"), 403,
            "\"error\":\"invalid_proof\""));
    fail += expect_int("next heartbeat sequence is accepted", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":2,\"proof\":\"f880045c5c1699a6aeaa15de885dffe0fe03893e3537cb3ad8e29d496137baf2\",\"proto\":1,\"udp_port\":41009}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":2,\"proof\":\"f880045c5c1699a6aeaa15de885dffe0fe03893e3537cb3ad8e29d496137baf2\",\"proto\":1,\"udp_port\":41009}"), 200,
            "\"ok\":true"));
    fail += expect_int("accept integral decimal udp port", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":3,\"proof\":\"8f41f86a9aca250afdb8dd8d3289e27430298c5514a9b82163485e819d8f43f0\",\"proto\":1,\"udp_port\":123.0}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":3,\"proof\":\"8f41f86a9aca250afdb8dd8d3289e27430298c5514a9b82163485e819d8f43f0\",\"proto\":1,\"udp_port\":123.0}"), 200,
            "\"ok\":true"));
    fail += expect_int("reject zero udp port", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":0}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":0}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject oversized udp port", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":65536}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":65536}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject negative udp port", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":-1}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":-1}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject fractional udp port", 0,
        test_http_request(port,
            "{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":123.9}",
            strlen("{\"op\":\"heartbeat\",\"id\":\"proofpub\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"proto\":1,\"udp_port\":123.9}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("reject punch_req missing session", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\"}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\"}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("reject punch_req invalid session", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"bad token\"}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"bad token\"}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("reject candidate type", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"bogus\",\"addr\":\"192.0.2.1\",\"port\":9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"bogus\",\"addr\":\"192.0.2.1\",\"port\":9}]}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("reject candidate address", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"not-an-address\",\"port\":9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"not-an-address\",\"port\":9}]}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("reject candidate port", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":0}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":0}]}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("accept minimum candidate port", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":1}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":1}]}"),
            200, "\"ok\":true"));
    fail += expect_int("accept maximum candidate port", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":65535}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":65535}]}"),
            200, "\"ok\":true"));
    fail += expect_int("reject fractional candidate port", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":123.9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":123.9}]}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("reject candidate non-object", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[1]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess1\",\"candidates\":[1]}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("register punch poll publisher", 0,
        test_register_publisher(port, "receiver", "0123456789abcdef", 41020));
    fail += expect_int("queue punch request", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"caller\",\"target_id\":\"receiver\",\"session\":\"sess3\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"caller\",\"target_id\":\"receiver\",\"session\":\"sess3\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}"),
            200, "\"ok\":true"));
    fail += expect_int("punch poll missing proof rejected", 0,
        test_http_request(port, "{\"op\":\"punch_poll\",\"id\":\"receiver\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\"}"), 400,
            "\"error\":\"bad_request\""));
    fail += expect_int("valid punch poll retrieves pending call", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":1,\"proof\":\"4d7ae8d35e447e0d7d6bb81517d4754c7aae87c8c8f6879c165d0f1596238781\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":1,\"proof\":\"4d7ae8d35e447e0d7d6bb81517d4754c7aae87c8c8f6879c165d0f1596238781\"}"),
            200, "\"self_id\":\"caller\""));
    fail += expect_int("punch poll replay of same sequence rejected", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":1,\"proof\":\"4d7ae8d35e447e0d7d6bb81517d4754c7aae87c8c8f6879c165d0f1596238781\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":1,\"proof\":\"4d7ae8d35e447e0d7d6bb81517d4754c7aae87c8c8f6879c165d0f1596238781\"}"),
            403, "\"error\":\"invalid_proof\""));
    fail += expect_int("queue second punch request", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"caller2\",\"target_id\":\"receiver\",\"session\":\"sess4\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":10}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"caller2\",\"target_id\":\"receiver\",\"session\":\"sess4\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":10}]}"),
            200, "\"ok\":true"));
    fail += expect_int("punch poll wrong proof rejected", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":2,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":2,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\"}"),
            403, "\"error\":\"invalid_proof\""));
    fail += expect_int("wrong proof did not consume pending call", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":2,\"proof\":\"ee91dc959c1a77635fccc4c84815fbd84e796203f93c095fbc264decf3c18c88\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":2,\"proof\":\"ee91dc959c1a77635fccc4c84815fbd84e796203f93c095fbc264decf3c18c88\"}"),
            200, "\"self_id\":\"caller2\""));
    fail += expect_int("raw session secret not used as proof", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":3,\"secret\":\"0123456789abcdef\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":3,\"secret\":\"0123456789abcdef\"}"),
            400, "\"error\":\"bad_request\""));
    fail += expect_int("consume punch poll", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":3,\"proof\":\"b92d0623f4feaa64d209876c950b60de9673fa4052a2d6f90714d7a8fc478c39\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":3,\"proof\":\"b92d0623f4feaa64d209876c950b60de9673fa4052a2d6f90714d7a8fc478c39\"}"),
            200, "\"calls\":[]"));
    fail += expect_int("queue concurrent punch request A", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerA\",\"target_id\":\"receiver\",\"session\":\"sess5\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":11}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerA\",\"target_id\":\"receiver\",\"session\":\"sess5\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":11}]}"),
            200, "\"ok\":true"));
    fail += expect_int("queue concurrent punch request B", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerB\",\"target_id\":\"receiver\",\"session\":\"sess6\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":12}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerB\",\"target_id\":\"receiver\",\"session\":\"sess6\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":12}]}"),
            200, "\"ok\":true"));
    fail += expect_int("wrong proof consumes none of queued calls", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":4,\"proof\":\"0000000000000000000000000000000000000000000000000000000000000000\"}"),
            403, "\"error\":\"invalid_proof\""));
    fail += expect_int("valid poll returns multiple pending calls", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":4,\"proof\":\"c6c8c7fdebc3a13afbaabefd24efc827ea72c712c3358517e8a1cbdf3f5418b4\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":4,\"proof\":\"c6c8c7fdebc3a13afbaabefd24efc827ea72c712c3358517e8a1cbdf3f5418b4\"}"),
            200, "\"self_id\":\"callerB\""));
    fail += expect_int("later poll retrieves no remaining calls", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":5,\"proof\":\"8f69ed0a98dd3dcd6560923a761ed91464b2fe63d15369bada52f1b5ee56c4b6\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":5,\"proof\":\"8f69ed0a98dd3dcd6560923a761ed91464b2fe63d15369bada52f1b5ee56c4b6\"}"),
            200, "\"calls\":[]"));

    /** Boundary: PUNCH_POLL_MAX+1 worst-case-candidate calls return at most
     * PUNCH_POLL_MAX on the first poll, leaving the remainder pending. */
    used = 0;
    for (i = 0; i < REDP2P_PEER_CANDIDATES_MAX; i++) {
        int written = snprintf(worst_candidates + used,
            sizeof(worst_candidates) - used,
            "%s{\"type\":\"host\",\"addr\":\"2001:db8:ffff:ffff:ffff:ffff:ffff:ffff\",\"port\":%u}",
            i == 0 ? "" : ",", (unsigned)(65535 - i));
        if (written < 0 || (size_t)written >= sizeof(worst_candidates) - used) {
            used = sizeof(worst_candidates);
            break;
        }
        used += (size_t)written;
    }
    fail += expect_true("build worst-case candidate block", used < sizeof(worst_candidates));
    for (i = 0; i < REDP2P_PUNCH_POLL_MAX + 1; ++i) {
        int written = snprintf(overflow_req, sizeof(overflow_req),
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"brem%d\",\"target_id\":\"receiver\",\"session\":\"bsess%d\",\"candidates\":[%s]}",
            i, i, worst_candidates);
        fail += expect_true("build boundary punch request",
            written > 0 && (size_t)written < sizeof(overflow_req));
        if (written <= 0 || (size_t)written >= sizeof(overflow_req))
            break;
        fail += expect_int("queue boundary punch request", 0,
            test_http_request(port, overflow_req, (size_t)written, 200,
                "\"ok\":true"));
    }
    fail += expect_int("boundary poll returns at most PUNCH_POLL_MAX calls", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":6,\"proof\":\"fba4caef890e5b27ace2a55bea151c13b5335163daa0f820a188941adcb15fd2\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":6,\"proof\":\"fba4caef890e5b27ace2a55bea151c13b5335163daa0f820a188941adcb15fd2\"}"),
            200, "\"self_id\":\"brem0\""));
    fail += expect_int(
        "remaining call stays pending and returns in a later poll",
        0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":7,\"proof\":\"4abf003ab9ffffe5f5972dc36801aa0fb12bf90ba43b6c8a4240faf3f7ad5819\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"receiver\",\"seq\":7,\"proof\":\"4abf003ab9ffffe5f5972dc36801aa0fb12bf90ba43b6c8a4240faf3f7ad5819\"}"),
            200, "\"self_id\":\"brem4\""));
    fail += expect_int("reject punch_req nonexistent target", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerX\",\"target_id\":\"ghost\",\"session\":\"sess8\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerX\",\"target_id\":\"ghost\",\"session\":\"sess8\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}"),
            404, "\"error\":\"not_found\""));
    for (i = 0; i < 8; ++i) {
        fail += expect_int("rejected target enqueues nothing", 0,
            test_http_request(port,
                "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"fill\",\"target_id\":\"ghost\",\"session\":\"sess9\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}",
                strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"fill\",\"target_id\":\"ghost\",\"session\":\"sess9\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}"),
                404, "\"error\":\"not_found\""));
    }
    fail += expect_int("active target accepted after rejected fills", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerY\",\"target_id\":\"receiver\",\"session\":\"sess10\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":13}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"callerY\",\"target_id\":\"receiver\",\"session\":\"sess10\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":13}]}"),
            200, "\"ok\":true"));

    fail += expect_int("register per-target quota publisher", 0,
        test_register_publisher(port, "quotaTgt", "0123456789abcdef", 41040));
    for (i = 0; i < REDP2P_MAX_PENDING_CALLS_PER_PUBLISHER; ++i) {
        int written = snprintf(per_target_req, sizeof(per_target_req),
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qcaller%d\",\"target_id\":\"quotaTgt\",\"session\":\"qsess%d\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":%d}]}",
            i, i, i + 60);
        fail += expect_true("build per-publisher punch request",
            written > 0 && (size_t)written < sizeof(per_target_req));
        if (written <= 0 || (size_t)written >= sizeof(per_target_req))
            break;
        fail += expect_int("queue call within default per-publisher limit", 0,
            test_http_request_from(i < 16 ? "127.0.0.10" : "127.0.0.11",
                port, per_target_req, (size_t)written, 200,
                "\"ok\":true"));
    }
    fail += expect_int("default publisher limit rejects call without mutation", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qcallerfull\",\"target_id\":\"quotaTgt\",\"session\":\"qsessfull\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qcallerfull\",\"target_id\":\"quotaTgt\",\"session\":\"qsessfull\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":9}]}"),
            429, "{\"ok\":false,\"error\":\"pending_limit_publisher\"}"));
    fail += expect_int("register second per-publisher quota publisher", 0,
        test_register_publisher(port, "quotaOther", "0123456789abcdef", 41041));
    fail += expect_int("other publisher unaffected by full window", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qother\",\"target_id\":\"quotaOther\",\"session\":\"qothersess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":70}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qother\",\"target_id\":\"quotaOther\",\"session\":\"qothersess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":70}]}"),
            200, "\"ok\":true"));
    fail += expect_int("successful punch_poll frees consumer slots", 0,
        test_http_request(port,
            "{\"op\":\"punch_poll\",\"id\":\"quotaTgt\",\"seq\":1,\"proof\":\"5cded4ae0b885464291f6f72cf95e24e54f20adb5bab5af899cb80f3778895c3\"}",
            strlen("{\"op\":\"punch_poll\",\"id\":\"quotaTgt\",\"seq\":1,\"proof\":\"5cded4ae0b885464291f6f72cf95e24e54f20adb5bab5af899cb80f3778895c3\"}"),
            200, "\"self_id\":\"qcaller0\""));
    fail += expect_int("publisher accepts new consumer after punch_poll", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qcallernew\",\"target_id\":\"quotaTgt\",\"session\":\"qsessnew\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":80}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"qcallernew\",\"target_id\":\"quotaTgt\",\"session\":\"qsessnew\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":80}]}"),
            200, "\"ok\":true"));
    used = (size_t)snprintf(too_many, sizeof(too_many),
        "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"client\",\"target_id\":\"proofpub\",\"session\":\"sess2\",\"candidates\":[");
    for (i = 0; i < REDP2P_PEER_CANDIDATES_MAX + 1 &&
        used < sizeof(too_many); i++)
    {
        int written;

        written = snprintf(too_many + used, sizeof(too_many) - used,
            "%s{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":%d}",
            i == 0 ? "" : ",", i + 1);
        if (written < 0 || (size_t)written >= sizeof(too_many) - used) {
            used = sizeof(too_many);
            break;
        }
        used += (size_t)written;
    }
    if (used < sizeof(too_many)) {
        int written = snprintf(too_many + used, sizeof(too_many) - used,
            "]}");
        if (written < 0 || (size_t)written >= sizeof(too_many) - used)
            used = sizeof(too_many);
        else
            used += (size_t)written;
    }
    fail += expect_true("build excessive candidate block",
        used < sizeof(too_many));
    if (used < sizeof(too_many))
        fail += expect_int("reject excessive candidate count", 0,
            test_http_request(port, too_many, used, 400,
                "\"error\":\"bad_request\""));
    used = (size_t)snprintf(overlong_line, sizeof(overlong_line),
        "POST /redp2p/ HTTP/1.1\r\nX-Filler: ");
    for (i = (int)used; (size_t)i < sizeof(overlong_line); i++)
        overlong_line[i] = 'a';
    fail += expect_int("reject overlong request headers", 431,
        test_http_raw_status(port, overlong_line, sizeof(overlong_line)));
    fail += expect_int("index usable after malformed requests", 0,
        test_http_request(port, "{\"op\":\"list\"}",
            strlen("{\"op\":\"list\"}"), 200, "\"ok\":true"));
    test_index_stop(&index);
    fail += expect_int("stopped index result", REDP2P_OK, index.result);
    fail += expect_true("index port closed", test_wait_port(port, 0));

    fail += test_punch_rate_limits((unsigned short)(test_port_base() + 10U));

    port = (unsigned short)(test_port_base() + 11U);
    memset(&index, 0, sizeof(index));
    index.port = port;
    index.result = 999;
    fail += expect_int("shorten pending-call TTL for expiry index", 0,
        test_setenv("REDP2P_PENDING_CALL_TTL_S", "2"));
    fail += expect_int("open expiry index", REDP2P_OK,
        redp2p_context_create(&index.ctx));
    fail += expect_int("set bounded per-publisher limit", REDP2P_OK,
        redp2p_idx_set_max_consumers(index.ctx, 4));
    if (fail != 0 ||
        test_thread_start(&index.thread, test_index_main, &index) != 0)
    {
        redp2p_context_destroy(index.ctx);
        test_setenv("REDP2P_PENDING_CALL_TTL_S", NULL);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_true("expiry index listens", test_wait_port(port, 1));
    fail += expect_int("register expiry publisher", 0,
        test_register_publisher(port, "expiryOne", "0123456789abcdef", 41060));
    for (i = 0; i < 4; ++i) {
        int written = snprintf(per_target_req, sizeof(per_target_req),
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"ecaller%d\",\"target_id\":\"expiryOne\",\"session\":\"esess%d\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":84}]}",
            i, i);

        fail += expect_true("build expiry punch request",
            written > 0 && (size_t)written < sizeof(per_target_req));
        if (written <= 0 || (size_t)written >= sizeof(per_target_req)) break;
        fail += expect_int("queue call into bounded publisher window", 0,
            test_http_request(port, per_target_req, (size_t)written, 200,
                "\"ok\":true"));
    }
    fail += expect_int("publisher window rejects call above its own limit", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"efull\",\"target_id\":\"expiryOne\",\"session\":\"efullsess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":85}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"efull\",\"target_id\":\"expiryOne\",\"session\":\"efullsess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":85}]}"),
        429, "{\"ok\":false,\"error\":\"pending_limit_publisher\"}"));
    cooling_start = test_now_ms();
    used = (size_t)snprintf(incomplete_body, sizeof(incomplete_body),
        "POST /redp2p/ HTTP/1.1\r\nContent-Length: 5\r\n\r\nab");
    for (i = 0; i < TEST_HTTP_CONNECTIONS; i++)
        timeout_fds[i] = TEST_SOCKET_INVALID;
    for (i = 0; i < TEST_HTTP_CONNECTIONS; i++) {
        timeout_fds[i] = test_tcp_connect(port);
        if (timeout_fds[i] == TEST_SOCKET_INVALID) break;
        test_socket_timeout(timeout_fds[i], 8000U);
        if (i == 0 && test_socket_send_all(timeout_fds[i],
            (const unsigned char *)incomplete_body, used) != 0)
        {
            test_socket_close(timeout_fds[i]);
            timeout_fds[i] = TEST_SOCKET_INVALID;
            break;
        }
    }
    fail += expect_int("open HTTP connection table for request deadlines",
        TEST_HTTP_CONNECTIONS, i);
    test_sleep_ms(6000U);
    for (i = 0; i < TEST_HTTP_CONNECTIONS; i++) {
        if (timeout_fds[i] != TEST_SOCKET_INVALID) {
            fail += expect_int("close zero-byte and partial requests on deadline",
                0, test_tcp_wait_closed(timeout_fds[i], 4096U));
            test_socket_close(timeout_fds[i]);
        }
    }
    fail += expect_int("index reuses expired HTTP connection slots", 0,
        test_http_request(port, "{\"op\":\"list\"}",
            strlen("{\"op\":\"list\"}"), 200, "\"ok\":true"));
    cooling_elapsed = (unsigned int)(test_now_ms() - cooling_start);
    if (cooling_elapsed < 3000U)
        test_sleep_ms(3000U - cooling_elapsed);
    fail += expect_int("expired pending calls release publisher capacity", 0,
        test_http_request(port,
            "{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"afterexpiry\",\"target_id\":\"expiryOne\",\"session\":\"afterexpirysess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":86}]}",
            strlen("{\"op\":\"punch_req\",\"udp_port\":41080,\"self_id\":\"afterexpiry\",\"target_id\":\"expiryOne\",\"session\":\"afterexpirysess\",\"candidates\":[{\"type\":\"host\",\"addr\":\"192.0.2.1\",\"port\":86}]}"),
            200, "\"ok\":true"));
    test_index_stop(&index);
    fail += expect_int("stopped expiry index result", REDP2P_OK, index.result);
    test_setenv("REDP2P_PENDING_CALL_TTL_S", NULL);

    port = (unsigned short)(test_port_base() + 2U);
    fail += expect_int("start capacity-limited index", 0,
        test_index_start_configured(&index, port, 2, NULL, NULL));
    fail += expect_int("start first capacity publisher", 0,
        test_publisher_start(&first, "capone", port,
            (unsigned short)(port + 20U)));
    fail += expect_true("first capacity publisher active",
        atomic_load(&first.result) == 999);
    fail += expect_int("start second capacity publisher", 0,
        test_publisher_start(&second, "captwo", port,
            (unsigned short)(port + 21U)));
    fail += expect_true("second capacity publisher active",
        atomic_load(&second.result) == 999);
    fail += expect_int("start over-capacity publisher", 0,
        test_publisher_start(&third, "capthree", port,
            (unsigned short)(port + 22U)));
    fail += expect_true("capacity reached is reported",
        test_publisher_wait_result(&third, 2000U));
    fail += expect_int("capacity rejection category", REDP2P_EFULL,
        atomic_load(&third.result));
    fail += expect_true("capacity rejection detail",
        strstr(redp2p_get_error(third.ctx), "full") != NULL);
    test_publisher_finish(&third);
    test_publisher_stop(&first);
    fail += expect_int("lookup removed disconnected publisher", 0,
        test_http_request(port, "{\"op\":\"lookup\",\"id\":\"capone\"}",
            strlen("{\"op\":\"lookup\",\"id\":\"capone\"}"), 404,
            "\"error\":\"not_found\""));
    fail += expect_int("start publisher after capacity release", 0,
        test_publisher_start(&third, "capthree", port,
            (unsigned short)(port + 22U)));
    fail += expect_true("released capacity is reusable",
        atomic_load(&third.result) == 999);
    test_publisher_stop(&third);
    test_publisher_stop(&second);
    test_index_stop(&index);

    port = (unsigned short)(test_port_base() + 3U);
    fail += expect_int("start reserved-seat index", 0,
        test_index_start_configured(&index, port, 2, "vip vippass",
            "globalpass"));
    fail += expect_int("start VIP publisher", 0,
        test_publisher_start_pass(&first, "vip", port,
            (unsigned short)(port + 20U), "vippass"));
    fail += expect_int("start available non-VIP publisher", 0,
        test_publisher_start_pass(&second, "regular", port,
            (unsigned short)(port + 21U), "globalpass"));
    fail += expect_true("VIP reserved seat active",
        atomic_load(&first.result) == 999);
    fail += expect_true("non-VIP capacity active",
        atomic_load(&second.result) == 999);
    fail += expect_int("start excess non-VIP publisher", 0,
        test_publisher_start_pass(&third, "excess", port,
            (unsigned short)(port + 22U), "globalpass"));
    fail += expect_true("VIP reservation limits non-VIP seats",
        test_publisher_wait_result(&third, 2000U));
    fail += expect_int("reserved capacity rejection category", REDP2P_EFULL,
        atomic_load(&third.result));
    test_publisher_finish(&third);
    fail += expect_int("start authorized VIP takeover publisher", 0,
        test_publisher_start_state(&third, "vip", port,
            (unsigned short)(port + 22U), "vippass", foreign_state_dir));
    fail += expect_true("authorized VIP takeover returns",
        test_publisher_wait_result(&third, 2000U));
    fail += expect_int("authorized VIP takeover category", REDP2P_EEXIST,
        atomic_load(&third.result));
    test_publisher_finish(&third);
    fail += expect_int("start wrong-password VIP publisher", 0,
        test_publisher_start_state(&third, "vip", port,
            (unsigned short)(port + 22U), "wrongpass", foreign_state_dir));
    fail += expect_true("wrong-password VIP publisher returns",
        test_publisher_wait_result(&third, 2000U));
    fail += expect_int("registration mismatch category", REDP2P_EAUTH,
        atomic_load(&third.result));
    fail += expect_true("registration mismatch detail",
        strstr(redp2p_get_error(third.ctx), "auth_failed") != NULL);
    test_publisher_finish(&third);
    test_publisher_stop(&second);
    test_publisher_stop(&first);
    test_index_stop(&index);

    port = (unsigned short)(test_port_base() + 4U);
    fail += expect_int("start zero-seat index", 0,
        test_index_start_configured(&index, port, 0, NULL, NULL));
    fail += expect_int("start publisher against zero-seat index", 0,
        test_publisher_start(&first, "noseat", port,
            (unsigned short)(port + 20U)));
    fail += expect_true("zero seats rejects publisher",
        test_publisher_wait_result(&first, 2000U));
    fail += expect_int("zero-seat rejection category", REDP2P_EFULL,
        atomic_load(&first.result));
    test_publisher_finish(&first);
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_wait.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_wait(void) {
    test_index_t index;
    test_publisher_t publisher;
    redp2p_t *ctx;
    unsigned short base;
    const char *name = "kc_redp2p_wait";
    const char *detail = "wait blocks until publisher is ready";
    int fail;

    fail = 0;
    base = (unsigned short)(test_port_base() + 20U);
    fail += expect_int("wait NULL", REDP2P_EINVAL,
        redp2p_pub_run(NULL, TEST_HOST, base, "pub", (unsigned short)(base + 1U)));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("stop wait before entry", REDP2P_OK, redp2p_context_request_stop(ctx));
    fail += expect_int("wait honors prior stop", REDP2P_OK,
        redp2p_pub_run(ctx, TEST_HOST, base, "pub",
            (unsigned short)(base + 1U)));
    fail += expect_true("wait stop consumed", !redp2p_is_stop_requested(ctx));
    fail += expect_int("wait without index", REDP2P_ENET,
        redp2p_pub_run(ctx, TEST_HOST, base, "pub", (unsigned short)(base + 1U)));
    redp2p_context_destroy(ctx);
    if (test_index_start(&index, (unsigned short)(base + 2U)) != 0) return 1;
    if (test_publisher_start(&publisher, "waitpub", (unsigned short)(base + 2U),
        (unsigned short)(base + 3U)) != 0) return 1;
    fail += expect_true("publisher remains running",
        atomic_load(&publisher.result) == 999);
    test_index_stop(&index);
    fail += expect_true("publisher exits after index stop",
        test_publisher_wait_result(&publisher, 3000U));
    fail += expect_int("index loss publisher category", REDP2P_ENET,
        atomic_load(&publisher.result));
    fail += expect_true("index loss publisher detail",
        strstr(redp2p_get_error(publisher.ctx), "index connect") != NULL);
    test_publisher_finish(&publisher);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_connect.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_connect(void) {
    test_control_stub_t stub;
    test_index_t index;
    test_publisher_t publisher;
    test_tcp_echo_t occupied;
    redp2p_t *ctx;
    unsigned short base;
    int stub_started;
    const char *name = "kc_redp2p_connect";
    const char *detail = "connect punches through to publisher";
    int fail;

    fail = 0;
    base = (unsigned short)(test_port_base() + 40U);
    fail += expect_int("connect NULL", REDP2P_EINVAL,
        redp2p_con_run(NULL, TEST_HOST, base, "client", "missing",
            (unsigned short)(base + 1U)));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    redp2p_set_local_port(ctx, (unsigned short)(base + 1U));
    fail += expect_int("stop connect before entry", REDP2P_OK, redp2p_context_request_stop(ctx));
    fail += expect_int("connect honors prior stop", REDP2P_OK,
        redp2p_con_run(ctx, TEST_HOST, base, "client", "missing",
            (unsigned short)(base + 1U)));
    fail += expect_true("connect stop consumed", !redp2p_is_stop_requested(ctx));
    redp2p_set_local_port(ctx, (unsigned short)(base + 1U));
    fail += expect_int("connect without index", REDP2P_ENET,
        redp2p_con_run(ctx, TEST_HOST, base, "client", "missing",
            (unsigned short)(base + 1U)));
    redp2p_context_destroy(ctx);
    if (test_index_start(&index, (unsigned short)(base + 2U)) != 0) return 1;
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    redp2p_set_local_port(ctx, (unsigned short)(base + 3U));
    fail += expect_int("connect missing target", REDP2P_ENOENT,
        redp2p_con_run(ctx, TEST_HOST, (unsigned short)(base + 2U), "client",
            "missing", (unsigned short)(base + 3U)));
    redp2p_context_destroy(ctx);
    fail += expect_int("start occupied-listener publisher", 0,
        test_publisher_start(&publisher, "occupied",
            (unsigned short)(base + 2U), (unsigned short)(base + 6U)));
    fail += expect_int("occupy consumer listener", 0,
        test_tcp_echo_start(&occupied, (unsigned short)(base + 7U)));
    fail += expect_int("open occupied-listener context", REDP2P_OK,
        redp2p_context_create(&ctx));
    redp2p_set_local_port(ctx, (unsigned short)(base + 7U));
    fail += expect_int("occupied consumer listener category", REDP2P_ENET,
        redp2p_con_run(ctx, TEST_HOST, (unsigned short)(base + 2U), "client",
            "occupied", (unsigned short)(base + 7U)));
    redp2p_context_destroy(ctx);
    test_tcp_echo_stop(&occupied);
    test_publisher_stop(&publisher);
    test_index_stop(&index);
    stub_started = test_control_stub_start(&stub,
        (unsigned short)(base + 4U));
    fail += expect_int("start incomplete index response", 0, stub_started);
    if (stub_started == 0) {
        fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
        redp2p_set_local_port(ctx, (unsigned short)(base + 5U));
        fail += expect_int("reject closed index response", REDP2P_ENET,
            redp2p_con_run(ctx, TEST_HOST, (unsigned short)(base + 4U),
                "client", "missing", (unsigned short)(base + 5U)));
        fail += expect_int("reject malformed index status line", REDP2P_EPROTO,
            redp2p_con_run(ctx, TEST_HOST, (unsigned short)(base + 4U),
                "client", "missing", (unsigned short)(base + 5U)));
        fail += expect_true("malformed status line detail",
            strstr(redp2p_get_error(ctx), "index status line") != NULL);
        redp2p_context_destroy(ctx);
        fail += expect_int("stop incomplete index response", 0,
            test_control_stub_stop(&stub));
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs one public-API UDP tunnel end-to-end round trip.
 * @return 0 on success, 1 on failure.
 */
static int test_udp_tunnel_case(void)
{
    test_index_t index;
    test_publisher_t publisher;
    test_consumer_t consumer;
    test_udp_echo_t echo;
    unsigned char payload[REDP2P_UDP_PAYLOAD_MAX + 1];
    unsigned short base;
    unsigned int elapsed;
    int rc;

    rc = 0;
    base = (unsigned short)(test_port_base() + 300U);
    memset(payload, 0x5a, sizeof(payload));
    rc += expect_int("start UDP echo", 0,
        test_udp_echo_start(&echo, (unsigned short)(base + 1U)));
    rc += expect_int("start UDP index", 0, test_index_start(&index, base));
    rc += expect_int("start UDP publisher", 0,
        test_udp_publisher_start(&publisher, "udppub", base,
            (unsigned short)(base + 1U)));
    rc += expect_int("start UDP consumer", 0,
        test_udp_consumer_start(&consumer, "udpclient", "udppub", base,
            (unsigned short)(base + 2U)));
    if (rc == 0) {
        int small_result = test_udp_roundtrip(
            (unsigned short)(base + 2U), payload, 3, 3000U);
        rc += expect_int("UDP small datagram", 3, small_result);
        if (small_result < 0)
            fprintf(stderr,
                "consumer result: %d error: %s\npublisher result: %d error: %s\n",
                atomic_load(&consumer.result), redp2p_get_error(consumer.ctx),
                atomic_load(&publisher.result),
                redp2p_get_error(publisher.ctx));
        rc += expect_int("UDP empty datagram", 0,
            test_udp_roundtrip((unsigned short)(base + 2U), payload, 0, 3000U));
        rc += expect_int("UDP maximum datagram", REDP2P_UDP_PAYLOAD_MAX,
            test_udp_roundtrip((unsigned short)(base + 2U), payload,
                REDP2P_UDP_PAYLOAD_MAX, 3000U));
        rc += expect_int("send UDP oversized datagram", 0,
            test_udp_send_only((unsigned short)(base + 2U), payload,
                REDP2P_UDP_PAYLOAD_MAX + 1));
        for (elapsed = 0; elapsed < 2000U; elapsed += 50U) {
            if (strstr(redp2p_get_error(consumer.ctx),
                "exceeds maximum") != NULL)
                break;
            test_sleep_ms(50U);
        }
        rc += expect_true("UDP oversized datagram rejected",
            redp2p_get_error(consumer.ctx)[0] != '\0');
        rc += expect_true("UDP oversized detail",
            strstr(redp2p_get_error(consumer.ctx), "exceeds maximum") != NULL);
    }
    test_consumer_stop(&consumer);
    test_publisher_stop(&publisher);
    test_index_stop(&index);
    test_udp_echo_stop(&echo);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests plaintext UDP datagrams and MTU enforcement.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_udp_tunnel(void) {
    const char *name = "kc_redp2p_udp_tunnel";
    const char *detail = "udp tunnel preserves datagrams";
    int fail = test_udp_tunnel_case();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Exercises bounded TCP stream lifecycle and payload behavior.
 * @param port Local TCP adapter port.
 * @param echo Running TCP backend stopped by the final close scenario.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_stream_coverage(unsigned short port,
    test_tcp_echo_t *echo, redp2p_t *pub_ctx, redp2p_t *con_ctx) {
    unsigned char first[1537];
    unsigned char second[3073];
    unsigned char concurrent_first[4096];
    unsigned char concurrent_first_received[4096];
    unsigned char concurrent_second[6145];
    unsigned char concurrent_second_received[6145];
    unsigned char half_payload[4097];
    unsigned char half_received[4097];
    unsigned char ready[1];
    unsigned char *large;
    test_socket_t fd;
    test_socket_t first_fd;
    test_socket_t second_fd;
    int fail_ctx;
    int result;
    int rc;

    rc = 0;
    fail_ctx = redp2p_test_set_stream_faults(pub_ctx, 7, 11);
    rc += expect_int("enable publisher stream faults", REDP2P_OK, fail_ctx);
    fail_ctx = redp2p_test_set_stream_faults(con_ctx, 7, 11);
    rc += expect_int("enable consumer stream faults", REDP2P_OK, fail_ctx);
    large = (unsigned char *)malloc(TEST_TCP_LARGE_SIZE);
    rc += expect_true("allocate TCP patterned payload", large != NULL);
    if (large != NULL) {
        test_tcp_pattern(large, TEST_TCP_LARGE_SIZE, 7U);
        rc += expect_int(
            "TCP KCP datagram drop/reorder recovery",
            (int)TEST_TCP_LARGE_SIZE,
            test_tcp_roundtrip(port, large, TEST_TCP_LARGE_SIZE));
    }
    rc += expect_int("disable publisher stream faults", REDP2P_OK,
        redp2p_test_set_stream_faults(pub_ctx, 0, 0));
    rc += expect_int("disable consumer stream faults", REDP2P_OK,
        redp2p_test_set_stream_faults(con_ctx, 0, 0));

    test_tcp_pattern(first, sizeof(first), 11U);
    test_tcp_pattern(second, sizeof(second), 23U);
    fd = test_tcp_connect(port);
    rc += expect_true("open TCP bidirectional session",
        fd != TEST_SOCKET_INVALID);
    if (fd != TEST_SOCKET_INVALID) {
        rc += expect_int("TCP first same-session direction",
            (int)sizeof(first), test_tcp_exchange(fd, first, sizeof(first)));
        rc += expect_int("TCP second same-session direction",
            (int)sizeof(second), test_tcp_exchange(fd, second, sizeof(second)));
        test_socket_close(fd);
    }

    test_tcp_pattern(half_payload, sizeof(half_payload), 31U);
    memset(half_received, 0, sizeof(half_received));
    fd = test_tcp_connect(port);
    rc += expect_true("open TCP half-close session",
        fd != TEST_SOCKET_INVALID);
    if (fd != TEST_SOCKET_INVALID) {
        result = test_socket_send_all(fd, half_payload,
            sizeof(half_payload));
        if (result == 0) result = test_tcp_shutdown_send(fd);
        if (result == 0)
            result = test_socket_receive_exact(fd, half_received,
                sizeof(half_received));
        if (result == 0 && memcmp(half_payload, half_received,
            sizeof(half_payload)) != 0)
            result = 1;
        rc += expect_int("TCP half-close preserves pending response", 0,
            result);
        rc += expect_int("TCP half-close reaches peer EOF", 0,
            test_tcp_wait_closed(fd, 0));
        test_socket_close(fd);
    }

    test_tcp_pattern(concurrent_first, sizeof(concurrent_first), 43U);
    test_tcp_pattern(concurrent_second, sizeof(concurrent_second), 59U);
    first_fd = test_tcp_connect(port);
    second_fd = test_tcp_connect(port);
    rc += expect_true("open bounded concurrent TCP sessions",
        first_fd != TEST_SOCKET_INVALID && second_fd != TEST_SOCKET_INVALID);
    if (first_fd != TEST_SOCKET_INVALID &&
        second_fd != TEST_SOCKET_INVALID)
    {
        result = test_socket_send_all(first_fd, concurrent_first,
            sizeof(concurrent_first));
        if (result == 0)
            result = test_socket_send_all(second_fd, concurrent_second,
                sizeof(concurrent_second));
        if (result == 0)
            result = test_socket_receive_exact(first_fd,
                concurrent_first_received,
                sizeof(concurrent_first));
        if (result == 0 && memcmp(concurrent_first, concurrent_first_received,
            sizeof(concurrent_first)) != 0)
            result = 1;
        if (result == 0)
            result = test_socket_receive_exact(second_fd,
                concurrent_second_received,
                sizeof(concurrent_second));
        if (result == 0 && memcmp(concurrent_second,
            concurrent_second_received,
            sizeof(concurrent_second)) != 0)
            result = 1;
        rc += expect_int("TCP concurrent session isolation", 0, result);
    }
    if (first_fd != TEST_SOCKET_INVALID) test_socket_close(first_fd);
    if (second_fd != TEST_SOCKET_INVALID) test_socket_close(second_fd);

    if (large != NULL) {
        fd = test_tcp_connect(port);
        rc += expect_true("open TCP client-close session",
            fd != TEST_SOCKET_INVALID);
        if (fd != TEST_SOCKET_INVALID) {
            rc += expect_int("send TCP client-close payload", 0,
                test_socket_send_all(fd, large, TEST_TCP_LARGE_SIZE / 2U));
            test_socket_close(fd);
        }
        rc += expect_int("TCP session after client close", (int)sizeof(first),
            test_tcp_roundtrip(port, first, sizeof(first)));
    }

    ready[0] = 0xa5U;
    fd = test_tcp_connect(port);
    rc += expect_true("open TCP backend-close session",
        fd != TEST_SOCKET_INVALID);
    if (fd != TEST_SOCKET_INVALID) {
        rc += expect_int("establish TCP backend-close session", 1,
            test_tcp_exchange(fd, ready, sizeof(ready)));
        if (large != NULL)
            rc += expect_int("send TCP backend-close payload", 0,
                test_socket_send_all(fd, large, TEST_TCP_LARGE_SIZE / 2U));
    }
    test_tcp_echo_stop(echo);
    if (fd != TEST_SOCKET_INVALID) {
        rc += expect_int("TCP backend close reaches client", 0,
            test_tcp_wait_closed(fd, TEST_TCP_LARGE_SIZE / 2U));
        test_socket_close(fd);
    }
    free(large);
    return rc == 0 ? 0 : 1;
}

/**
 * Runs the public-API TCP stream end-to-end coverage.
 * @return 0 on success, 1 on failure.
 */
static int test_tcp_tunnel_case(void)
{
    test_index_t index;
    test_publisher_t publisher;
    test_consumer_t consumer;
    test_tcp_echo_t echo;
    unsigned short base;
    int rc;

    rc = 0;
    base = (unsigned short)(test_port_base() + 400U);
    rc += expect_int("start TCP echo", 0,
        test_tcp_echo_start(&echo, (unsigned short)(base + 1U)));
    rc += expect_int("start TCP index", 0, test_index_start(&index, base));
    rc += expect_int("start TCP publisher", 0,
        test_tcp_publisher_start(&publisher, "tcppub", base,
            (unsigned short)(base + 1U)));
    rc += expect_int("start TCP consumer", 0,
        test_tcp_consumer_start(&consumer, "tcpclient", "tcppub", base,
            (unsigned short)(base + 2U)));
    if (rc == 0) {
        rc += test_tcp_stream_coverage((unsigned short)(base + 2U), &echo,
            publisher.ctx, consumer.ctx);
    } else {
        test_tcp_echo_stop(&echo);
    }
    test_consumer_stop(&consumer);
    test_publisher_stop(&publisher);
    test_index_stop(&index);
    return rc == 0 ? 0 : 1;
}

/**
 * Tests TCP stream through the public API.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_tcp_stream(void) {
    const char *name = "kc_redp2p_tcp_stream";
    const char *detail = "tcp stream transfers bytes end to end";
    int fail = test_tcp_tunnel_case();
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests redp2p_persisted_deregister.
 * @return 0 on success, 1 on failure.
 */
static int case_redp2p_persisted_deregister(void) {
    test_index_t first_index;
    test_index_t second_index;
    test_publisher_t first_publisher;
    test_publisher_t second_publisher;
    test_publisher_t publisher;
    test_publishers_t publishers;
    redp2p_t *client;
    char names[8][128];
    char paths[8][768];
    char key_data[64];
    char legacy_path[768];
    char blocked_home[640];
    char long_home[900];
    size_t key_len;
    unsigned short base;
    int first_index_started;
    int second_index_started;
    int first_publisher_started;
    int second_publisher_started;
    int publisher_started;
    int count;
    const char *name = "redp2p_persisted_deregister";
    const char *detail = "deregister removes published service";
    int fail;
#ifndef _WIN32
    mode_t old_umask;
    struct stat status;
#endif

    fail = 0;
    client = NULL;
    first_index_started = 0;
    second_index_started = 0;
    first_publisher_started = 0;
    second_publisher_started = 0;
    publisher_started = 0;
    memset(&first_index, 0, sizeof(first_index));
    memset(&second_index, 0, sizeof(second_index));
    memset(&first_publisher, 0, sizeof(first_publisher));
    memset(&second_publisher, 0, sizeof(second_publisher));
    memset(&publisher, 0, sizeof(publisher));
    test_key_cleanup();
#ifndef _WIN32
    old_umask = umask(000);
#endif
    base = (unsigned short)(test_port_base() + 60U);
    fail += expect_int("open client", REDP2P_OK, redp2p_context_create(&client));
    if (!client) goto cleanup;
    fail += expect_int("deregister NULL context", REDP2P_EINVAL,
        redp2p_test_deregister_persisted_publisher(NULL, TEST_HOST, base, "absent"));
    fail += expect_int("deregister NULL host", REDP2P_EINVAL,
        redp2p_test_deregister_persisted_publisher(client, NULL, base, "absent"));
    fail += expect_true("NULL host detail", redp2p_get_error(client)[0] != '\0');
    fail += expect_int("deregister empty host", REDP2P_EINVAL,
        redp2p_test_deregister_persisted_publisher(client, "", base, "absent"));
    fail += expect_int("deregister zero port", REDP2P_EINVAL,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, 0, "absent"));
    fail += expect_int("deregister invalid id", REDP2P_EINVAL,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "../unsafe"));
    fail += expect_int("deregister missing key", REDP2P_ENOENT,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));

    if (test_index_start(&first_index, (unsigned short)(base + 1U)) != 0)
        goto cleanup;
    first_index_started = 1;
    if (test_index_start(&second_index, (unsigned short)(base + 2U)) != 0)
        goto cleanup;
    second_index_started = 1;
    if (test_publisher_start(&first_publisher, "shared",
        (unsigned short)(base + 1U), (unsigned short)(base + 3U)) != 0)
        goto cleanup;
    first_publisher_started = 1;
    if (test_publisher_start(&second_publisher, "shared",
        (unsigned short)(base + 2U), (unsigned short)(base + 4U)) != 0)
        goto cleanup;
    second_publisher_started = 1;

    fail += expect_int("list two scoped keys", 0,
        test_key_list(names, 8, &count));
    fail += expect_int("same id has two scoped keys", 2, count);
    for (int i = 0; i < count; i++) {
        int valid_name;

        valid_name = strlen(names[i]) == 64;
        for (size_t j = 0; valid_name && j < strlen(names[i]); j++) {
            char byte;

            byte = names[i][j];
            valid_name = (byte >= '0' && byte <= '9') ||
                (byte >= 'a' && byte <= 'f');
        }
        fail += expect_true("scoped filename is bounded hex", valid_name);
        if (test_key_path(names[i], paths[i], sizeof(paths[i])) != 0) {
            fail++;
            continue;
        }
        fail += expect_int("read scoped key", 0,
            test_file_read(paths[i], key_data, sizeof(key_data), &key_len));
        fail += expect_true("scoped key stores secret and sequence",
            key_len >= 19 && key_data[16] == '\n' && key_data[key_len - 1] == '\n');
#ifndef _WIN32
        fail += expect_int("scoped key stat", 0, stat(paths[i], &status));
        fail += expect_int("scoped key permissions", 0600,
            (int)(status.st_mode & 0777));
#endif
    }

    fail += expect_int("deregister first scoped publisher", REDP2P_OK,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 1U),
            "shared"));
    fail += expect_string("successful deregistration clears detail", "",
        redp2p_get_error(client));
    fail += expect_int("list remaining scoped key", 0,
        test_key_list(names, 8, &count));
    fail += expect_int("successful deregistration deletes one key", 1, count);
    if (count != 1 || test_key_path(names[0], paths[0], sizeof(paths[0])) != 0 ||
        test_file_read(paths[0], key_data, sizeof(key_data), &key_len) != 0)
    {
        fail++;
        goto cleanup;
    }
    test_publisher_stop(&first_publisher);
    first_publisher_started = 0;

    fail += expect_int("write malformed key", 0,
        test_file_write(paths[0], "0123456789abcdeg", 16));
    fail += expect_int("reject malformed key", REDP2P_EPROTO,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 2U),
            "shared"));
    fail += expect_true("malformed key preserved", test_path_exists(paths[0]));
    fail += expect_int("write truncated key", 0,
        test_file_write(paths[0], "01234567", 8));
    fail += expect_int("reject truncated key", REDP2P_EPROTO,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 2U),
            "shared"));
    fail += expect_true("truncated key preserved", test_path_exists(paths[0]));
    fail += expect_int("write extra key", 0,
        test_file_write(paths[0], "0123456789abcdefx", 17));
    fail += expect_int("reject extra key", REDP2P_EPROTO,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 2U),
            "shared"));
    fail += expect_true("extra key preserved", test_path_exists(paths[0]));
    fail += expect_int("restore valid key", 0,
        test_file_write(paths[0], key_data, key_len));

    test_index_stop(&second_index);
    second_index_started = 0;
    fail += expect_int("failed deregistration category", REDP2P_ENET,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 2U),
            "shared"));
    fail += expect_true("failed deregistration preserves key",
        test_path_exists(paths[0]));
    test_publisher_stop(&second_publisher);
    second_publisher_started = 0;
    remove(paths[0]);
#ifndef _WIN32
    fail += expect_int("create key path directory", 0, mkdir(paths[0], 0700));
    fail += expect_int("reject key path directory", REDP2P_ERROR,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 2U),
            "shared"));
    fail += expect_int("remove key path directory", 0, rmdir(paths[0]));
#endif

    if (test_publisher_start(&publisher, "shutdown",
        (unsigned short)(base + 1U), (unsigned short)(base + 5U)) != 0)
        goto cleanup;
    publisher_started = 1;
    fail += expect_int("shutdown key created", 0,
        test_key_list(names, 8, &count));
    fail += expect_int("shutdown key count", 1, count);
    test_publisher_stop(&publisher);
    publisher_started = 0;
    fail += expect_int("shutdown key list", 0,
        test_key_list(names, 8, &count));
    fail += expect_int("successful shutdown deletes scoped key", 0, count);

    if (test_publisher_start(&publisher, "legacy",
        (unsigned short)(base + 1U), (unsigned short)(base + 6U)) != 0)
        goto cleanup;
    publisher_started = 1;
    if (test_key_list(names, 8, &count) != 0 || count != 1 ||
        test_key_path(names[0], paths[0], sizeof(paths[0])) != 0 ||
        test_file_read(paths[0], key_data, sizeof(key_data), &key_len) != 0 ||
        test_key_path("legacy", legacy_path, sizeof(legacy_path)) != 0)
    {
        fail++;
        goto cleanup;
    }
    fail += expect_int("create legacy key", 0,
        test_file_write(legacy_path, key_data, key_len));
    fail += expect_int("remove scoped key for migration", 0, remove(paths[0]));
    fail += expect_int("legacy deregistration", REDP2P_OK,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, (unsigned short)(base + 1U),
            "legacy"));
    fail += expect_true("successful legacy lookup removes legacy key",
        !test_path_exists(legacy_path));
    test_publisher_stop(&publisher);
    publisher_started = 0;

#ifdef _WIN32
    test_setenv("USERPROFILE", test_home_path);
    test_setenv("HOME", NULL);
    fail += expect_int("missing HOME uses USERPROFILE", REDP2P_ENOENT,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));
    test_setenv("HOME", "");
    fail += expect_int("empty HOME uses USERPROFILE", REDP2P_ENOENT,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));
#else
    test_setenv("HOME", NULL);
    fail += expect_int("missing HOME category", REDP2P_ERROR,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));
    fail += expect_true("missing HOME detail", redp2p_get_error(client)[0] != '\0');
    test_setenv("HOME", "");
    fail += expect_int("empty HOME category", REDP2P_ERROR,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));
#endif
    memset(long_home, 'x', sizeof(long_home) - 1);
    long_home[sizeof(long_home) - 1] = '\0';
    test_setenv("HOME", long_home);
    fail += expect_int("overlong HOME category", REDP2P_ERROR,
        redp2p_test_deregister_persisted_publisher(client, TEST_HOST, base, "absent"));
    test_setenv("HOME", test_home_path);

    if (snprintf(blocked_home, sizeof(blocked_home), "%s/blocked-home",
        test_home_path) < 0 || strlen(blocked_home) >= sizeof(blocked_home) ||
        test_file_write(blocked_home, "blocked", 7) != 0)
    {
        fail++;
        goto cleanup;
    }
    test_setenv("HOME", blocked_home);
    if (test_publisher_start(&publisher, "nosave",
        (unsigned short)(base + 1U), (unsigned short)(base + 7U)) != 0)
        goto cleanup;
    publisher_started = 1;
    fail += expect_true("unwritable HOME publisher returns",
        test_publisher_wait_result(&publisher, 3000U));
    fail += expect_int("unwritable HOME publication fails", REDP2P_ERROR,
        atomic_load(&publisher.result));
    test_setenv("HOME", test_home_path);
    test_publisher_finish(&publisher);
    publisher_started = 0;
    memset(&publishers, 0, sizeof(publishers));
    fail += expect_int("list after save rollback", REDP2P_OK,
        redp2p_idx_query_publishers(client, TEST_HOST,
            (unsigned short)(base + 1U), test_on_publisher, &publishers));
    fail += expect_true("save failure registration rolled back",
        !test_has_publisher(&publishers, "nosave"));

cleanup:
    test_setenv("HOME", test_home_path);
    if (publisher_started) test_publisher_stop(&publisher);
    if (second_publisher_started) test_publisher_stop(&second_publisher);
    if (first_publisher_started) test_publisher_stop(&first_publisher);
    if (second_index_started) test_index_stop(&second_index);
    if (first_index_started) test_index_stop(&first_index);
    if (client) redp2p_context_destroy(client);
#ifndef _WIN32
    umask(old_umask);
#endif
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests publisher heartbeat survival past heartbeat interval.
 * Verifies a publisher remains registered well beyond the heartbeat interval
 * (15s) with the default eviction timeout (120s).
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_heartbeat_refresh(void) {
    test_index_t index;
    test_publisher_t publisher;
    char names[2][128];
    char path[768];
    char state[64];
    size_t state_len;
    int count;
    unsigned short base;
    const char *name = "kc_redp2p_heartbeat";
    const char *detail = "heartbeat refreshes session state";
    int fail;

    fail = 0;
    state_len = 0;
    count = 0;
    test_key_cleanup();
    base = (unsigned short)(test_port_base() + 100U);
    if (test_index_start(&index, (unsigned short)(base + 1U)) != 0) return 1;
    test_setenv("REDP2P_HEARTBEAT_S", "1");
    if (test_publisher_start(&publisher, "hbping", (unsigned short)(base + 1U),
        (unsigned short)(base + 2U)) != 0) {
        test_index_stop(&index);
        return 1;
    }
    test_sleep_ms(1600U);
    fail += expect_int("publisher survives past heartbeat interval", 0,
        test_http_request((unsigned short)(base + 1U),
            "{\"op\":\"lookup\",\"id\":\"hbping\"}",
            strlen("{\"op\":\"lookup\",\"id\":\"hbping\"}"), 200,
            "\"udp_port\":"));
    fail += expect_int("list heartbeat session secret", 0,
        test_key_list(names, 2, &count));
    if (count == 1 && test_key_path(names[0], path, sizeof(path)) == 0 &&
        test_file_read(path, state, sizeof(state), &state_len) == 0)
        fail += expect_true("heartbeat reserves persisted sequence",
            state_len >= 19 && state[16] == '\n' && state[17] != '0' &&
            state[state_len - 1] == '\n');
    else
        fail++;
    test_publisher_stop(&publisher);
    fail += expect_int("next control sequence deregisters publisher", 0,
        test_http_request((unsigned short)(base + 1U),
            "{\"op\":\"lookup\",\"id\":\"hbping\"}",
            strlen("{\"op\":\"lookup\",\"id\":\"hbping\"}"), 404,
            "\"error\":\"not_found\""));
    test_setenv("REDP2P_HEARTBEAT_S", NULL);
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests publisher sequence advancement after a lost heartbeat response.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_lost_response(void) {
    test_publisher_control_stub_t stub;
    test_publisher_t publisher;
#ifndef _WIN32
    char key_names[2][128];
    char key_path[768];
    char key_data[128];
    char persisted_secret[REDP2P_KEY_SZ + 1];
    int key_count;
    size_t key_len;
#endif
    unsigned short base;
    unsigned int elapsed;
    const char *name = "kc_redp2p_lost_response";
    const char *detail = "publisher advances sequence after lost response";
    int publisher_started;
    int stub_started;
    int fail;

    fail = 0;
#ifndef _WIN32
    key_count = 0;
    key_len = 0;
#endif
    publisher_started = 0;
    stub_started = 0;
    memset(&publisher, 0, sizeof(publisher));
    test_key_cleanup();
    base = (unsigned short)(test_port_base() + 120U);
    fail += expect_int("start publisher control stub", 0,
        test_publisher_control_stub_start(&stub, base));
    if (fail != 0) goto cleanup;
    stub_started = 1;
    test_setenv("REDP2P_HEARTBEAT_S", "1");
    publisher.host = TEST_HOST;
    publisher.index_port = base;
    publisher.id = "lostreply";
    publisher.bind_port = (unsigned short)(base + 1U);
    publisher.protocol = REDP2P_PROTO_TCP;
    atomic_init(&publisher.result, 999);
    fail += expect_int("open lost-response publisher", REDP2P_OK,
        redp2p_context_create(&publisher.ctx));
    if (fail != 0) goto cleanup;
    fail += expect_int("start lost-response publisher", 0,
        test_thread_start(&publisher.thread, test_publisher_main, &publisher));
    if (fail != 0) goto cleanup;
    publisher_started = 1;
    for (elapsed = 0; elapsed < 5000U; elapsed += 20U) {
        if (atomic_load(&stub.heartbeat_count) >= 2 ||
            atomic_load(&publisher.result) != 999)
            break;
        test_sleep_ms(20U);
    }
    fail += expect_int("initial register occurs once", 1,
        atomic_load(&stub.register_count));
#ifndef _WIN32
    if (test_key_list(key_names, 2, &key_count) == 0 && key_count == 1 &&
        test_key_path(key_names[0], key_path, sizeof(key_path)) == 0 &&
        test_file_read(key_path, key_data, sizeof(key_data), &key_len) == 0 &&
        key_len >= REDP2P_KEY_SZ)
    {
        memcpy(persisted_secret, key_data, REDP2P_KEY_SZ);
        persisted_secret[REDP2P_KEY_SZ] = '\0';
        fail += expect_true("publisher register wire omits plaintext secret",
            test_publisher_register_wire_valid(stub.register_body,
                persisted_secret));
    } else {
        fail += expect_true("publisher register wire captures plaintext secret", 0);
    }
#else
    fail += expect_true("publisher register wire protects control secret",
        test_publisher_register_wire_valid(stub.register_body, NULL));
#endif
    fail += expect_int("first heartbeat response dropped", 1,
        atomic_load(&stub.dropped_heartbeat));
    fail += expect_true("publisher remains running after dropped response",
        atomic_load(&publisher.result) == 999);
    fail += expect_true("publisher sends next heartbeat",
        atomic_load(&stub.heartbeat_count) >= 2);
    fail += expect_true("dropped heartbeat commits a positive sequence",
        atomic_load(&stub.first_heartbeat_sequence) >= 1);
    fail += expect_true("retry advances sequence after lost response",
        atomic_load(&stub.second_heartbeat_sequence) >
        atomic_load(&stub.first_heartbeat_sequence));
    fail += expect_int("publisher does not reregister active session", 1,
        atomic_load(&stub.register_count));

cleanup:
    if (publisher_started) test_publisher_stop(&publisher);
    else if (publisher.ctx != NULL) redp2p_context_destroy(publisher.ctx);
    test_setenv("REDP2P_HEARTBEAT_S", NULL);
    if (stub_started)
        fail += expect_int("stop publisher control stub", 0,
            test_publisher_control_stub_stop(&stub));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_list_publishers.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_list_publishers(void) {
    test_index_t index;
    test_publisher_t publisher;
    test_publisher_t replacement;
    test_publishers_t publishers;
    redp2p_t *client;
    char duplicate_port[32];
    char foreign_state_dir[768];
    unsigned short base;
    const char *name = "kc_redp2p_list_publishers";
    const char *detail = "list publishers reports registered services";
    int fail;

    fail = 0;
    base = (unsigned short)(test_port_base() + 80U);
    snprintf(foreign_state_dir, sizeof(foreign_state_dir),
        "%s/duplicate-takeover", test_home_path);
    fail += expect_int("list NULL ctx", REDP2P_EINVAL,
        redp2p_idx_query_publishers(NULL, TEST_HOST, base, test_on_publisher, NULL));
    fail += expect_int("open client", REDP2P_OK, redp2p_context_create(&client));
    fail += expect_int("list NULL host", REDP2P_EINVAL,
        redp2p_idx_query_publishers(client, NULL, base, test_on_publisher, NULL));
    fail += expect_int("list NULL callback", REDP2P_EINVAL,
        redp2p_idx_query_publishers(client, TEST_HOST, base, NULL, NULL));
    fail += expect_int("list without index", REDP2P_ENET,
        redp2p_idx_query_publishers(client, TEST_HOST, base, test_on_publisher, NULL));
    redp2p_context_destroy(client);
    if (test_index_start(&index, (unsigned short)(base + 1U)) != 0) return 1;
    if (test_publisher_start(&publisher, "listed", (unsigned short)(base + 1U),
        (unsigned short)(base + 2U)) != 0) return 1;
    memset(&publishers, 0, sizeof(publishers));
    fail += expect_int("open client", REDP2P_OK, redp2p_context_create(&client));
    fail += expect_int("seed stale list detail", REDP2P_EINVAL,
        redp2p_pub_set_protocol(client, 7));
    fail += expect_int("list publishers", REDP2P_OK,
        redp2p_idx_query_publishers(client, TEST_HOST, (unsigned short)(base + 1U),
            test_on_publisher, &publishers));
    fail += expect_true("publisher listed", test_has_publisher(&publishers, "listed"));
    fail += expect_string("successful list clears detail", "",
        redp2p_get_error(client));
    redp2p_context_destroy(client);
    test_publisher_stop(&publisher);
    fail += expect_int("start original duplicate publisher", 0,
        test_publisher_start(&publisher, "duplicate",
            (unsigned short)(base + 1U), (unsigned short)(base + 3U)));
    fail += expect_int("start duplicate takeover publisher", 0,
        test_publisher_start_state(&replacement, "duplicate",
            (unsigned short)(base + 1U), (unsigned short)(base + 4U), NULL,
            foreign_state_dir));
    fail += expect_true("duplicate takeover is rejected",
        test_publisher_wait_result(&replacement, 2000U));
    fail += expect_int("duplicate takeover category", REDP2P_EEXIST,
        atomic_load(&replacement.result));
    fail += expect_true("duplicate takeover detail omits key",
        strstr(redp2p_get_error(replacement.ctx), "already_registered") != NULL &&
        strstr(redp2p_get_error(replacement.ctx), "key") == NULL);
    test_publisher_finish(&replacement);
    snprintf(duplicate_port, sizeof(duplicate_port), "\"udp_port\":%u",
        (unsigned)(base + 3U));
    fail += expect_int("duplicate takeover preserves endpoint", 0,
        test_http_request((unsigned short)(base + 1U),
            "{\"op\":\"lookup\",\"id\":\"duplicate\"}",
            strlen("{\"op\":\"lookup\",\"id\":\"duplicate\"}"), 200,
            duplicate_port));
    memset(&publishers, 0, sizeof(publishers));
    fail += expect_int("open duplicate list client", REDP2P_OK,
        redp2p_context_create(&client));
    fail += expect_int("list after old duplicate disconnect", REDP2P_OK,
        redp2p_idx_query_publishers(client, TEST_HOST,
            (unsigned short)(base + 1U), test_on_publisher, &publishers));
    fail += expect_true("original duplicate publisher remains active",
        test_has_publisher(&publishers, "duplicate"));
    redp2p_context_destroy(client);
    test_publisher_stop(&publisher);
    fail += expect_int("start released duplicate publisher", 0,
        test_publisher_start(&replacement, "duplicate",
            (unsigned short)(base + 1U), (unsigned short)(base + 4U)));
    fail += expect_true("released duplicate publisher is active",
        atomic_load(&replacement.result) == 999);
    test_publisher_stop(&replacement);
    fail += expect_int("lookup after current publisher disconnect", 0,
        test_http_request((unsigned short)(base + 1U),
            "{\"op\":\"lookup\",\"id\":\"duplicate\"}",
            strlen("{\"op\":\"lookup\",\"id\":\"duplicate\"}"), 404,
            "\"error\":\"not_found\""));
    test_index_stop(&index);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests context-owned error detail lifetime and clearing.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_get_error(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_get_error";
    const char *detail = "get error captures context error detail";
    int fail;

    fail = 0;
    fail += expect_string("NULL ctx empty", "", redp2p_get_error(NULL));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_string("cleared default", "", redp2p_get_error(ctx));
    fail += expect_int("invalid protocol category", REDP2P_EINVAL,
        redp2p_pub_set_protocol(ctx, 7));
    fail += expect_string("captured detail",
        "protocol must be REDP2P_PROTO_TCP or REDP2P_PROTO_UDP",
        redp2p_get_error(ctx));
    fail += expect_int("valid protocol", REDP2P_OK,
        redp2p_pub_set_protocol(ctx, REDP2P_PROTO_TCP));
    fail += expect_string("success clears detail", "", redp2p_get_error(ctx));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_seats.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_seats(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_seats";
    const char *detail = "set seats configures capacity with bounds";
    int fail;
    size_t max_peer_count;

    fail = 0;
    fail += expect_int("set seats NULL", REDP2P_EINVAL, redp2p_idx_set_capacity(NULL, 1));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    max_peer_count = SIZE_MAX / sizeof(redp2p_peer_t);
    fail += expect_int("set zero seats", REDP2P_OK, redp2p_idx_set_capacity(ctx, 0));
    fail += expect_int("set seats positive", REDP2P_OK, redp2p_idx_set_capacity(ctx, 2));
    fail += expect_int("reject allocation count overflow", REDP2P_EINVAL,
        redp2p_idx_set_capacity(ctx, max_peer_count + 1));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_max_consumers_per_publisher.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_max_consumers_per_publisher(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_max_consumers_per_publisher";
    const char *detail = "set max consumers per publisher configures a per-publisher safety window";
    int fail;

    fail = 0;
    fail += expect_int("set max consumers NULL", REDP2P_EINVAL,
        redp2p_idx_set_max_consumers(NULL, 1));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set zero consumers restores default", REDP2P_OK,
        redp2p_idx_set_max_consumers(ctx, 0));
    fail += expect_int("set consumers positive", REDP2P_OK,
        redp2p_idx_set_max_consumers(ctx, 8));
    fail += expect_int("set consumers large value", REDP2P_OK,
        redp2p_idx_set_max_consumers(ctx, (size_t)2097152));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_pow.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_pow(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_pow";
    const char *detail = "set pow validates difficulty values";
    int fail;

    fail = 0;
    fail += expect_int("set pow NULL", REDP2P_EINVAL, redp2p_idx_set_pow(NULL, 1));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set pow positive", REDP2P_OK, redp2p_idx_set_pow(ctx, 2));
    fail += expect_int("set pow negative", REDP2P_EINVAL, redp2p_idx_set_pow(ctx, -1));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_port.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_port(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_port";
    const char *detail = "set port validates bind port";
    int fail;

    fail = 0;
    fail += expect_int("set port NULL", REDP2P_EINVAL, redp2p_set_local_port(NULL, 1));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set port value", REDP2P_OK, redp2p_set_local_port(ctx, 12345));
    fail += expect_int("set port zero", REDP2P_EINVAL, redp2p_set_local_port(ctx, 0));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_protocol.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_protocol(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_protocol";
    const char *detail = "set protocol validates transport mode";
    int fail;

    fail = 0;
    fail += expect_int("set protocol NULL", REDP2P_EINVAL,
        redp2p_pub_set_protocol(NULL, REDP2P_PROTO_TCP));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set TCP", REDP2P_OK, redp2p_pub_set_protocol(ctx, REDP2P_PROTO_TCP));
    fail += expect_int("set UDP", REDP2P_OK, redp2p_pub_set_protocol(ctx, REDP2P_PROTO_UDP));
    fail += expect_int("set invalid protocol", REDP2P_EINVAL,
        redp2p_pub_set_protocol(ctx, 99));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_pass.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_pass(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_pass";
    const char *detail = "set pass validates pass token input";
    int fail;

    fail = 0;
    fail += expect_int("set pass NULL ctx", REDP2P_EINVAL,
        redp2p_set_registration_pass(NULL, "x"));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set pass", REDP2P_OK, redp2p_set_registration_pass(ctx, "secret"));
    fail += expect_int("clear pass", REDP2P_OK, redp2p_set_registration_pass(ctx, ""));
    fail += expect_int("set NULL pass", REDP2P_EINVAL, redp2p_set_registration_pass(ctx, NULL));
    fail += expect_int("set symbolic pass", REDP2P_OK,
        redp2p_set_registration_pass(ctx, "bad`pass"));
    fail += expect_int("set whitespace pass", REDP2P_EINVAL,
        redp2p_set_registration_pass(ctx, "bad pass"));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_vip.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_vip(void) {
    redp2p_t *ctx;
    char err[128];
    const char *name = "kc_redp2p_set_vip";
    const char *detail = "set vip reserves vip seat";
    int fail;

    fail = 0;
    fail += expect_int("set vip NULL ctx", REDP2P_ERROR,
        redp2p_idx_set_vips(NULL, "vip pass", err, sizeof(err)));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set vip pair", REDP2P_OK,
        redp2p_idx_set_vips(ctx, "vip pass", err, sizeof(err)));
    fail += expect_int("clear vip NULL", REDP2P_OK,
        redp2p_idx_set_vips(ctx, NULL, err, sizeof(err)));
    fail += expect_int("clear vip empty", REDP2P_OK,
        redp2p_idx_set_vips(ctx, "", err, sizeof(err)));
    fail += expect_int("reject odd vip tokens", REDP2P_ERROR,
        redp2p_idx_set_vips(ctx, "vip", err, sizeof(err)));
    fail += expect_int("reject bad vip id", REDP2P_ERROR,
        redp2p_idx_set_vips(ctx, "bad:id pass", err, sizeof(err)));
    fail += expect_int("accept symbolic vip pass", REDP2P_OK,
        redp2p_idx_set_vips(ctx, "other bad`pass", err, sizeof(err)));
    fail += expect_int("reject whitespace vip pass", REDP2P_ERROR,
        redp2p_idx_set_vips(ctx, "third bad pass", err, sizeof(err)));
    fail += expect_int("reject duplicate vip", REDP2P_ERROR,
        redp2p_idx_set_vips(ctx, "vip pass vip other", err, sizeof(err)));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_stun_url.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_stun_url(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_stun_url";
    const char *detail = "set stun url validates stun address";
    int fail;

    fail = 0;
    fail += expect_int("set stun NULL ctx", REDP2P_EINVAL,
        redp2p_set_stun_server(NULL, "stun:example.com:3478"));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set stun", REDP2P_OK,
        redp2p_set_stun_server(ctx, "stun:example.com:3478"));
    fail += expect_int("seed stun detail", REDP2P_EINVAL,
        redp2p_pub_set_protocol(ctx, 7));
    fail += expect_int("clear stun", REDP2P_OK, redp2p_set_stun_server(ctx, NULL));
    fail += expect_string("successful stun update clears detail", "",
        redp2p_get_error(ctx));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests optional TURN configuration.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_turn_url(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_turn_url";
    const char *detail = "TURN remains optional and validates relay settings";
    int fail;

    fail = 0;
    fail += expect_int("set turn NULL ctx", REDP2P_EINVAL,
        redp2p_set_turn_server(NULL, "turn:example.com:3478", "user", "pass"));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("set anonymous turn", REDP2P_OK,
        redp2p_set_turn_server(ctx, "turn:example.com:3478", NULL, NULL));
    fail += expect_int("set authenticated turn", REDP2P_OK,
        redp2p_set_turn_server(ctx, "turn:example.com:3478", "user", "pass"));
    fail += expect_int("accept UDP TURN URI", REDP2P_OK,
        redp2p_set_turn_server(ctx,
            "turn:example.com:3478?transport=udp", "user", "pass"));
    fail += expect_int("reject turn user without pass", REDP2P_EINVAL,
        redp2p_set_turn_server(ctx, "turn:example.com:3478", "user", NULL));
    fail += expect_int("reject unsupported turns scheme", REDP2P_EINVAL,
        redp2p_set_turn_server(ctx, "turns:example.com:5349", "user", "pass"));
    fail += expect_int("clear turn", REDP2P_OK,
        redp2p_set_turn_server(ctx, NULL, NULL, NULL));
    fail += expect_string("successful turn update clears detail", "",
        redp2p_get_error(ctx));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_stream_faults.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_stream_faults(void) {
    redp2p_t *ctx;
    const char *name = "kc_redp2p_set_stream_faults";
    const char *detail = "stream fault settings clear stale error detail";
    int fail;

    fail = 0;
    fail += expect_int("set stream faults NULL", REDP2P_EINVAL,
        redp2p_test_set_stream_faults(NULL, 1, 1));
    fail += expect_int("open context", REDP2P_OK, redp2p_context_create(&ctx));
    fail += expect_int("seed stream fault detail", REDP2P_EINVAL,
        redp2p_pub_set_protocol(ctx, 7));
    fail += expect_int("set stream faults", REDP2P_OK,
        redp2p_test_set_stream_faults(ctx, 7, 11));
    fail += expect_string("stream fault success clears detail", "",
        redp2p_get_error(ctx));
    fail += expect_int("reject negative stream faults", REDP2P_EINVAL,
        redp2p_test_set_stream_faults(ctx, -1, 0));
    redp2p_context_destroy(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_redp2p_set_state_dir API and key persistence with a custom
 * directory.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_set_state_dir(void) {
    redp2p_t *ctx;
    test_index_t index;
    test_publisher_t publisher;
    char custom_base[768];
    char keys_subdir[PATH_MAX];
    unsigned short base;
    const char *name = "kc_redp2p_set_state_dir";
    const char *detail = "set state dir configures state location";
    int fail;

    fail = 0;

    fail += expect_int("open ctx", REDP2P_OK, redp2p_context_create(&ctx));
    if (ctx) {
        fail += expect_int("set_state_dir NULL ctx", REDP2P_EINVAL,
            redp2p_test_set_state_dir(NULL, "/tmp"));
        fail += expect_int("set_state_dir NULL dir", REDP2P_EINVAL,
            redp2p_test_set_state_dir(ctx, NULL));
        fail += expect_int("set_state_dir empty", REDP2P_OK,
            redp2p_test_set_state_dir(ctx, ""));
        fail += expect_int("set_state_dir valid", REDP2P_OK,
            redp2p_test_set_state_dir(ctx, "/tmp/redp2p-custom"));
        fail += expect_int("set_state_dir long", REDP2P_EINVAL,
            redp2p_test_set_state_dir(ctx,
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        redp2p_context_destroy(ctx);
    }

    base = (unsigned short)(test_port_base() + 320U);
    snprintf(custom_base, sizeof(custom_base), "%s/custom", test_home_path);
    test_remove_tree(custom_base);
#ifdef _WIN32
    CreateDirectoryA(custom_base, NULL);
#else
    mkdir(custom_base, 0777);
#endif
    snprintf(keys_subdir, sizeof(keys_subdir), "%s/keys", custom_base);

    fail += expect_int("start index for state_dir test", 0,
        test_index_start_configured(&index, base, 4, NULL, "secret"));
    if (fail == 0) {
        fail += expect_int("start publisher with state_dir", 0,
            test_publisher_start_state(&publisher, "kdpub", base,
                (unsigned short)(base + 1U), "secret", custom_base));
    }
    if (fail == 0) {
        fail += expect_int("publisher reaches registered state", 0,
            test_wait_publisher_ready(&publisher));
#ifdef _WIN32
        fail += expect_int("key dir created", 0,
            (GetFileAttributesA(keys_subdir) != INVALID_FILE_ATTRIBUTES) ? 0 : 1);
#else
        {
            struct stat status;
            fail += expect_int("key dir created", 0, stat(keys_subdir, &status));
        }
#endif
        test_publisher_stop(&publisher);
        test_index_stop(&index);
    } else {
        test_publisher_stop(&publisher);
        test_index_stop(&index);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the stateless query and validation API cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_validation(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_redp2p_candidate_type_values();
    fail += case_kc_redp2p_version();
    fail += case_kc_redp2p_strerror();
    fail += case_kc_redp2p_is_valid_id();
    fail += case_kc_redp2p_is_valid_pass_token();
    test_grouped = grouped;
    case_result(fail, "kc_redp2p_validation",
        "candidate, version, strerror, and token validation helpers");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the context lifecycle and error-detail cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_context(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_redp2p_open();
    fail += case_kc_redp2p_close();
    fail += case_kc_redp2p_stop();
    fail += case_kc_redp2p_get_error();
    test_grouped = grouped;
    case_result(fail, "kc_redp2p_context",
        "context lifecycle and error detail");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the trivial context setter validation cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_setters(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_redp2p_set_seats();
    fail += case_kc_redp2p_set_max_consumers_per_publisher();
    fail += case_kc_redp2p_set_pow();
    fail += case_kc_redp2p_set_port();
    fail += case_kc_redp2p_set_protocol();
    fail += case_kc_redp2p_set_pass();
    fail += case_kc_redp2p_set_vip();
    fail += case_kc_redp2p_set_stun_url();
    fail += case_kc_redp2p_set_turn_url();
    fail += case_kc_redp2p_set_stream_faults();
    test_grouped = grouped;
    case_result(fail, "kc_redp2p_setters",
        "setters enforce transport and capacity bounds");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the registration proof, tampering, and ordering contract cases.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_register(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_redp2p_register_vector();
    fail += case_kc_redp2p_register_mutations();
    fail += case_kc_redp2p_register_admission();
    fail += case_kc_redp2p_register_secret_validation();
    fail += case_kc_redp2p_register_order();
    test_grouped = grouped;
    case_result(fail, "kc_redp2p_register",
        "registration proof, tampering, and ordering contract");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the publisher heartbeat session cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_heartbeat(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_redp2p_heartbeat_refresh();
    fail += case_kc_redp2p_lost_response();
    test_grouped = grouped;
    case_result(fail, "kc_redp2p_heartbeat",
        "heartbeat refreshes session and reports lost responses");
    return fail == 0 ? 0 : 1;
}

/**
 * Builds a browser-compatible RTC registration request against a live C index.
 * @param port Index port.
 * @param id Publisher identifier.
 * @param secret Sixteen-character browser control secret.
 * @return 0 on success, 1 on failure.
 */
static int test_rtc_register(unsigned short port, const char *id,
    const char *secret)
{
    char response[4096], nonce_hex[65], mac_hex[65], proof_hex[65], body[4096];
    unsigned char nonce[32], solution[8], hash[32], message[512];
    uint64_t issued_at, expires_at;
    size_t used, id_len, secret_len;
    int n;
    int shift;

    n = snprintf(body, sizeof(body),
        "{\"op\":\"challenge\",\"id\":\"%s\"}", id);
    if (n < 0 || (size_t)n >= sizeof(body) ||
        test_http_response(port, body, (size_t)n, response,
            sizeof(response)) != 0 ||
        !test_json_string(response, "nonce", nonce_hex, sizeof(nonce_hex)) ||
        !test_json_string(response, "mac", mac_hex, sizeof(mac_hex)) ||
        !test_json_u64(response, "issued_at", &issued_at) ||
        !test_json_u64(response, "expires_at", &expires_at) ||
        !test_hex_decode(nonce_hex, nonce, sizeof(nonce)))
        return 1;

    memset(solution, 0, sizeof(solution));
    used = 0;
    memcpy(message + used, "REDP2P-WEB-REGISTER",
        sizeof("REDP2P-WEB-REGISTER") - 1);
    used += sizeof("REDP2P-WEB-REGISTER") - 1;
    memcpy(message + used, nonce, sizeof(nonce));
    used += sizeof(nonce);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(issued_at >> shift);
    for (shift = 56; shift >= 0; shift -= 8)
        message[used++] = (unsigned char)(expires_at >> shift);
    id_len = strlen(id);
    secret_len = strlen(secret);
    message[used++] = (unsigned char)(id_len >> 8);
    message[used++] = (unsigned char)id_len;
    memcpy(message + used, id, id_len);
    used += id_len;
    message[used++] = (unsigned char)(secret_len >> 8);
    message[used++] = (unsigned char)secret_len;
    memcpy(message + used, secret, secret_len);
    used += secret_len;
    memcpy(message + used, solution, sizeof(solution));
    used += sizeof(solution);

    test_hmac_sha256((const unsigned char *)secret, secret_len,
        message, used, hash);
    test_hex_encode(hash, sizeof(hash), proof_hex);

    n = snprintf(body, sizeof(body),
        "{\"op\":\"register\",\"id\":\"%s\",\"transport\":\"rtc\","
        "\"nonce\":\"%s\",\"issued_at\":%llu,\"expires_at\":%llu,"
        "\"mac\":\"%s\",\"secret\":\"%s\","
        "\"pow_solution\":\"0000000000000000\",\"proof\":\"%s\"}",
        id, nonce_hex, (unsigned long long)issued_at,
        (unsigned long long)expires_at, mac_hex, secret, proof_hex);
    return n < 0 || (size_t)n >= sizeof(body) ? 1 :
        test_http_request(port, body, (size_t)n, 200, "\"ok\":true");
}

/**
 * Computes the browser RTC publisher control proof.
 * @param secret Publisher control secret.
 * @param op Control operation.
 * @param id Publisher identifier.
 * @param sequence Monotonic sequence.
 * @param extra1 Optional first canonical field.
 * @param extra2 Optional second canonical field.
 * @param out Output HMAC hex.
 * @return 1 on success, 0 on formatting failure.
 */
static int test_rtc_control_proof(const char *secret, const char *op,
    const char *id, uint64_t sequence, const char *extra1,
    const char *extra2, char out[65])
{
    char message[512];
    unsigned char hash[32];
    int n;

    if (extra1 && extra2)
        n = snprintf(message, sizeof(message), "%s\n%s\n%llu\n%s\n%s",
            op, id, (unsigned long long)sequence, extra1, extra2);
    else if (extra1)
        n = snprintf(message, sizeof(message), "%s\n%s\n%llu\n%s",
            op, id, (unsigned long long)sequence, extra1);
    else
        n = snprintf(message, sizeof(message), "%s\n%s\n%llu",
            op, id, (unsigned long long)sequence);
    if (n < 0 || (size_t)n >= sizeof(message)) return 0;
    test_hmac_sha256((const unsigned char *)secret, strlen(secret),
        (const unsigned char *)message, (size_t)n, hash);
    test_hex_encode(hash, sizeof(hash), out);
    return 1;
}

/**
 * Verifies the browser RTC lifecycle against the native C index.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_rtc_index(void)
{
    const char *id = "rtcsite";
    const char *secret = "0123456789abcdef";
    const char *offer = "offer-sdp";
    const char *answer = "answer-sdp";
    test_index_t index;
    unsigned short port;
    char body[4096], response[8192];
    char connection[33], capability[65], proof[65], digest_hex[65];
    unsigned char digest[32];
    char digest_input[128];
    int n;
    int fail = 0;

    memset(&index, 0, sizeof(index));
    port = (unsigned short)(test_port_base() + 391U);
    fail += expect_int("start RTC C index", 0, test_index_start(&index, port));
    if (fail == 0)
        fail += expect_int("register RTC publisher", 0,
            test_rtc_register(port, id, secret));

    if (fail == 0)
        fail += expect_int("lookup RTC transport", 0,
            test_http_request(port,
                "{\"op\":\"lookup\",\"id\":\"rtcsite\"}",
                strlen("{\"op\":\"lookup\",\"id\":\"rtcsite\"}"),
                200, "\"transport\":\"rtc\""));

    if (fail == 0)
        fail += expect_int("native punch rejects RTC publisher", 0,
            test_http_request(port,
                "{\"op\":\"punch_req\",\"self_id\":\"native\","
                "\"target_id\":\"rtcsite\",\"session\":\"s\","
                "\"udp_port\":1234,\"candidates\":[]}",
                strlen("{\"op\":\"punch_req\",\"self_id\":\"native\","
                    "\"target_id\":\"rtcsite\",\"session\":\"s\","
                    "\"udp_port\":1234,\"candidates\":[]}"),
                409, "\"error\":\"unsupported_transport\""));

    n = snprintf(body, sizeof(body),
        "{\"op\":\"connect\",\"id\":\"%s\","
        "\"offer\":{\"type\":\"offer\",\"sdp\":\"%s\"}}", id, offer);
    if (fail == 0) {
        fail += expect_true("RTC connect request fits",
            n >= 0 && (size_t)n < sizeof(body));
        if (fail == 0)
            fail += expect_int("RTC connect", 0,
                test_http_response(port, body, (size_t)n, response,
                    sizeof(response)));
        if (fail == 0) {
            fail += expect_true("RTC connect ok",
                strstr(response, "\"ok\":true") != NULL);
            fail += expect_true("RTC connection id",
                test_json_string(response, "connection", connection,
                    sizeof(connection)));
            fail += expect_true("RTC capability",
                test_json_string(response, "capability", capability,
                    sizeof(capability)));
        }
    }

    if (fail == 0) {
        fail += expect_true("RTC poll proof",
            test_rtc_control_proof(secret, "poll", id, 1, NULL, NULL, proof));
        n = snprintf(body, sizeof(body),
            "{\"op\":\"poll\",\"id\":\"%s\",\"seq\":1,\"proof\":\"%s\"}",
            id, proof);
        fail += expect_true("RTC publisher poll fits",
            n >= 0 && (size_t)n < sizeof(body));
        if (fail == 0)
            fail += expect_int("RTC publisher poll", 0,
                test_http_response(port, body, (size_t)n, response,
                    sizeof(response)));
        if (fail == 0) {
            fail += expect_true("RTC publisher receives connection",
                strstr(response, connection) != NULL);
            fail += expect_true("RTC publisher receives offer",
                strstr(response, offer) != NULL);
        }
    }

    if (fail == 0) {
        n = snprintf(digest_input, sizeof(digest_input), "answer\n%s", answer);
        fail += expect_true("RTC answer digest input fits",
            n >= 0 && (size_t)n < sizeof(digest_input));
        if (fail == 0) {
            test_sha256((const unsigned char *)digest_input, (size_t)n, digest);
            test_hex_encode(digest, sizeof(digest), digest_hex);
            fail += expect_true("RTC answer proof",
                test_rtc_control_proof(secret, "answer", id, 2,
                    connection, digest_hex, proof));
        }
        n = snprintf(body, sizeof(body),
            "{\"op\":\"answer\",\"id\":\"%s\",\"seq\":2,"
            "\"proof\":\"%s\",\"connection\":\"%s\","
            "\"answer\":{\"type\":\"answer\",\"sdp\":\"%s\"}}",
            id, proof, connection, answer);
        fail += expect_true("RTC answer request fits",
            n >= 0 && (size_t)n < sizeof(body));
        if (fail == 0)
            fail += expect_int("RTC answer", 0,
                test_http_request(port, body, (size_t)n, 200, "\"ok\":true"));
    }

    if (fail == 0) {
        n = snprintf(body, sizeof(body),
            "{\"op\":\"poll\",\"connection\":\"%s\","
            "\"capability\":\"%s\"}", connection, capability);
        fail += expect_true("RTC consumer poll fits",
            n >= 0 && (size_t)n < sizeof(body));
        if (fail == 0)
            fail += expect_int("RTC consumer poll", 0,
                test_http_response(port, body, (size_t)n, response,
                    sizeof(response)));
        if (fail == 0) {
            fail += expect_true("RTC consumer receives answer",
                strstr(response, answer) != NULL);
        }
    }

    if (fail == 0) {
        fail += expect_true("RTC deregister proof",
            test_rtc_control_proof(secret, "deregister", id, 3,
                NULL, NULL, proof));
        n = snprintf(body, sizeof(body),
            "{\"op\":\"deregister\",\"id\":\"%s\",\"seq\":3,"
            "\"proof\":\"%s\"}", id, proof);
        fail += expect_true("RTC deregister fits",
            n >= 0 && (size_t)n < sizeof(body));
        if (fail == 0)
            fail += expect_int("RTC deregister", 0,
                test_http_request(port, body, (size_t)n, 200, "\"ok\":true"));
        if (fail == 0)
            fail += expect_int("RTC publisher removed", 0,
                test_http_request(port,
                    "{\"op\":\"lookup\",\"id\":\"rtcsite\"}",
                    strlen("{\"op\":\"lookup\",\"id\":\"rtcsite\"}"),
                    404, "\"error\":\"not_found\""));
    }

    test_index_stop(&index);
    case_result(fail, "kc_redp2p_rtc_index",
        "native index coordinates browser RTC signaling and transport routing");
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies publisher TTL expiration against the private index engine.
 * Keeps protocol expiry coverage independent from runtime configuration.
 * @return 0 on success, 1 on failure.
 */
static int case_redp2p_protocol_ttl(void)
{
    test_index_t index;
    unsigned short port;
    int fail = 0;

    memset(&index, 0, sizeof(index));
    port = (unsigned short)(test_port_base() + 390U);
    fail += expect_int("start TTL index", 0, test_index_start(&index, port));
    if (fail == 0) {
        redp2p_lock(index.ctx);
        index.ctx->etimeout_sec = 1;
        redp2p_unlock(index.ctx);
        fail += expect_int("register TTL publisher", 0,
            test_register_publisher(port, "ttlcheck", "0123456789abcdef",
                (unsigned short)(port + 1U)));
        test_sleep_ms(2200U);
        fail += expect_int("expired publisher disappears", 0,
            test_http_request(port,
                "{\"op\":\"lookup\",\"id\":\"ttlcheck\"}",
                strlen("{\"op\":\"lookup\",\"id\":\"ttlcheck\"}"),
                404, "\"error\":\"not_found\""));
    }
    test_index_stop(&index);
    case_result(fail, "redp2p_protocol_ttl",
        "index expires stale publisher records by TTL");
    return fail == 0 ? 0 : 1;
}

/**
 * Resolves one routable local IPv4 address without sending traffic.
 * @param out Destination address text.
 * @return 0 on success, 1 when no local unicast route is available.
 */
static int test_local_unicast_ipv4(char out[INET_ADDRSTRLEN])
{
    test_socket_t fd;
    struct sockaddr_in target;
    struct sockaddr_in local;
    test_socklen_t len;

    if (!out) return 1;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == TEST_SOCKET_INVALID) return 1;

    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    if (inet_pton(AF_INET, "8.8.8.8", &target.sin_addr) != 1 ||
        connect(fd, (const struct sockaddr *)&target, sizeof(target)) != 0)
    {
        test_socket_close(fd);
        return 1;
    }

    memset(&local, 0, sizeof(local));
    len = sizeof(local);
    if (getsockname(fd, (struct sockaddr *)&local, &len) != 0 ||
        ((const unsigned char *)&local.sin_addr)[0] == 127 ||
        !inet_ntop(AF_INET, &local.sin_addr, out, INET_ADDRSTRLEN))
    {
        test_socket_close(fd);
        return 1;
    }

    test_socket_close(fd);
    return 0;
}

typedef struct {
    _Atomic int publisher_received;
    _Atomic int consumer_received;
    _Atomic int respond_status;
} test_direct_api_state_t;

typedef struct {
    kc_redp2p_pub_t *pub;
    kc_redp2p_con_t *con;
    _Atomic int closed;
} test_direct_close_state_t;

#ifdef REDP2P_TESTING
void redp2p_test_api_io_hold(int hold);
int redp2p_test_api_io_entered(void);
void redp2p_test_api_port_hold(int hold);
unsigned short redp2p_test_api_port_value(void);
#endif

#ifdef REDP2P_TESTING
typedef struct {
    kc_redp2p_pub_t *pub;
    kc_redp2p_con_t *con;
    kc_redp2p_client_t *client;
    const char *index;
    _Atomic int done;
    _Atomic int result;
} test_direct_race_state_t;

#ifdef _WIN32
#define TEST_API_THREAD(name) static DWORD WINAPI name(void *arg)
#define TEST_API_THREAD_RETURN() return 0
#else
#define TEST_API_THREAD(name) static void *name(void *arg)
#define TEST_API_THREAD_RETURN() return NULL
#endif

/**
 * Sends one byte from a direct consumer on a worker thread.
 * Summary: Holds the operation open for deterministic close-race coverage.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_send_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    atomic_store(&state->result, kc_redp2p_con_send(state->con, "x", 1));
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

/**
 * Closes a direct consumer on a worker thread.
 * Summary: Verifies close waits for an in-flight consumer send.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_con_close_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    kc_redp2p_con_close(state->con);
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

/**
 * Responds through a publisher client on a worker thread.
 * Summary: Holds the operation open for deterministic publisher-close coverage.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_respond_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    atomic_store(&state->result,
        kc_redp2p_client_respond(state->client, "y", 1));
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

/**
 * Closes a publisher client on a worker thread.
 * Summary: Holds client teardown open while publisher close starts
 *          concurrently.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_client_close_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    kc_redp2p_client_close(state->client);
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

/**
 * Closes a direct publisher on a worker thread.
 * Summary: Verifies publisher teardown waits for in-flight client I/O.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_pub_close_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    kc_redp2p_pub_close(state->pub);
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

/**
 * Creates one direct consumer on a worker thread.
 * Summary: Lets the port-race test steal the selected ephemeral port.
 * @param arg Direct race state.
 * @return Platform thread result.
 */
TEST_API_THREAD(test_direct_con_create_thread)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)arg;
    kc_redp2p_con_options_t options;
    kc_redp2p_con_t *con = NULL;
    int result;

    memset(&options, 0, sizeof(options));
    options.id = "retryport";
    options.index = state->index;
    result = kc_redp2p_con(&con, &options);
    state->con = con;
    atomic_store(&state->result, result);
    atomic_store(&state->done, 1);
    TEST_API_THREAD_RETURN();
}

#undef TEST_API_THREAD
#undef TEST_API_THREAD_RETURN

/**
 * Captures the stable publisher client from one receive callback.
 * Summary: Supplies a live client handle to concurrent lifecycle tests.
 * @param input Publisher input.
 * @param userdata Direct race state.
 * @return None.
 */
static void test_direct_capture_client(const kc_redp2p_pub_input_t *input,
    void *userdata)
{
    test_direct_race_state_t *state = (test_direct_race_state_t *)userdata;

    if (!input || !state || !input->client) return;
    state->client = input->client;
    atomic_store(&state->done, 1);
}
#endif

/**
 * Closes a publisher from inside its receive callback.
 * Summary: Exercises deferred publisher destruction on the adapter thread.
 * @param input Publisher input.
 * @param userdata Direct close state.
 * @return None.
 */
static void test_direct_pub_self_close(const kc_redp2p_pub_input_t *input,
    void *userdata)
{
    test_direct_close_state_t *state =
        (test_direct_close_state_t *)userdata;

    if (!input || !state || !state->pub) return;
    kc_redp2p_pub_close(state->pub);
    state->pub = NULL;
    atomic_store(&state->closed, 1);
}

/**
 * Echoes direct publisher input back to its client.
 * Summary: Produces consumer receive traffic for lifecycle tests.
 * @param input Publisher input.
 * @param userdata Unused.
 * @return None.
 */
static void test_direct_close_echo(const kc_redp2p_pub_input_t *input,
    void *userdata)
{
    (void)userdata;
    if (!input || !input->client) return;
    (void)kc_redp2p_client_respond(input->client, input->data, input->size);
}

/**
 * Closes a consumer from inside its receive callback.
 * Summary: Exercises deferred consumer destruction on the adapter thread.
 * @param data Received bytes.
 * @param size Received byte count.
 * @param userdata Direct close state.
 * @return None.
 */
static void test_direct_con_self_close(const void *data, size_t size,
    void *userdata)
{
    test_direct_close_state_t *state =
        (test_direct_close_state_t *)userdata;

    (void)data;
    (void)size;
    if (!state || !state->con) return;
    kc_redp2p_con_close(state->con);
    state->con = NULL;
    atomic_store(&state->closed, 1);
}

/**
 * Receives direct publisher data and responds through the stable client.
 * @param input Publisher input.
 * @param userdata Direct test state.
 * @return None.
 */
static void test_direct_api_pub_receive(const kc_redp2p_pub_input_t *input,
    void *userdata)
{
    test_direct_api_state_t *state = (test_direct_api_state_t *)userdata;

    if (!input || !state || input->size != 4 ||
        memcmp(input->data, "ping", 4) != 0)
    {
        if (state) atomic_store(&state->publisher_received, -1);
        return;
    }
    atomic_store(&state->publisher_received, 1);
    atomic_store(&state->respond_status,
        kc_redp2p_client_respond(input->client, "pong", 4));
}

/**
 * Receives direct consumer response data.
 * @param data Response bytes.
 * @param size Response size.
 * @param userdata Direct test state.
 * @return None.
 */
static void test_direct_api_con_receive(const void *data, size_t size,
    void *userdata)
{
    test_direct_api_state_t *state = (test_direct_api_state_t *)userdata;

    if (!state) return;
    atomic_store(&state->consumer_received,
        data && size == 4 && memcmp(data, "pong", 4) == 0 ? 1 : -1);
}


#define TEST_TURN_MAGIC 0x2112A442u
#define TEST_TURN_ALLOCATE_REQ 0x0003
#define TEST_TURN_ALLOCATE_OK 0x0103
#define TEST_TURN_ALLOCATE_ERR 0x0113
#define TEST_TURN_REFRESH_REQ 0x0004
#define TEST_TURN_REFRESH_OK 0x0104
#define TEST_TURN_PERMISSION_REQ 0x0008
#define TEST_TURN_PERMISSION_OK 0x0108
#define TEST_TURN_SEND_IND 0x0016
#define TEST_TURN_DATA_IND 0x0017
#define TEST_TURN_ATTR_USERNAME 0x0006
#define TEST_TURN_ATTR_ERROR_CODE 0x0009
#define TEST_TURN_ATTR_LIFETIME 0x000d
#define TEST_TURN_ATTR_XOR_PEER 0x0012
#define TEST_TURN_ATTR_DATA 0x0013
#define TEST_TURN_ATTR_REALM 0x0014
#define TEST_TURN_ATTR_NONCE 0x0015
#define TEST_TURN_ATTR_XOR_RELAYED 0x0016
#define TEST_TURN_MAX_ALLOCS 16
#define TEST_TURN_PACKET_MAX 8192

typedef struct {
    int used;
    struct sockaddr_storage client;
    socklen_t client_len;
    struct sockaddr_storage relay;
    socklen_t relay_len;
} test_turn_allocation_t;

typedef struct {
    test_socket_t fd;
    unsigned short port;
    _Atomic int stop;
    _Atomic int allocations;
    _Atomic int permissions;
    _Atomic int indications;
    test_turn_allocation_t allocs[TEST_TURN_MAX_ALLOCS];
    test_thread_t thread;
} test_turn_server_t;

static unsigned int test_turn_u16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

static void test_turn_put16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static void test_turn_put32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static int test_turn_header(const unsigned char *buf, int len)
{
    uint32_t magic;
    int body_len;

    if (!buf || len < 20) return -1;
    magic = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
        ((uint32_t)buf[6] << 8) | buf[7];
    body_len = (int)test_turn_u16(buf + 2);
    if (magic != TEST_TURN_MAGIC || body_len < 0 || 20 + body_len > len)
        return -1;
    return (int)test_turn_u16(buf);
}

static int test_turn_attr_find(const unsigned char *buf, int len,
    unsigned int type, int *attr_len)
{
    int offset = 20;

    while (offset + 4 <= len) {
        int current = (int)test_turn_u16(buf + offset);
        int size = (int)test_turn_u16(buf + offset + 2);
        int padded = (size + 3) & ~3;

        if (size < 0 || offset + 4 + padded > len) return -1;
        if ((unsigned int)current == type) {
            if (attr_len) *attr_len = size;
            return offset + 4;
        }
        offset += 4 + padded;
    }
    return -1;
}

static int test_turn_attr_add(unsigned char *buf, size_t cap, int *offset,
    unsigned int type, const void *data, size_t len)
{
    size_t padded;

    if (!buf || !offset || len > 65535U) return 0;
    padded = (len + 3U) & ~3U;
    if ((size_t)*offset + 4U + padded > cap) return 0;
    test_turn_put16(buf + *offset, type);
    test_turn_put16(buf + *offset + 2, (unsigned int)len);
    if (len > 0 && data) memcpy(buf + *offset + 4, data, len);
    if (padded > len) memset(buf + *offset + 4 + len, 0, padded - len);
    *offset += (int)(4U + padded);
    return 1;
}

static int test_turn_begin(unsigned char *out, size_t cap, unsigned int type,
    const unsigned char txid[12])
{
    if (!out || cap < 20 || !txid) return 0;
    memset(out, 0, 20);
    test_turn_put16(out, type);
    out[4] = 0x21; out[5] = 0x12; out[6] = 0xa4; out[7] = 0x42;
    memcpy(out + 8, txid, 12);
    return 20;
}

static void test_turn_finish(unsigned char *buf, int offset)
{
    test_turn_put16(buf + 2, (unsigned int)(offset - 20));
}

static int test_turn_sockaddr_equal(const struct sockaddr_storage *a,
    const struct sockaddr_storage *b)
{
    const struct sockaddr_in *av4;
    const struct sockaddr_in *bv4;

    if (!a || !b || a->ss_family != AF_INET || b->ss_family != AF_INET)
        return 0;
    av4 = (const struct sockaddr_in *)a;
    bv4 = (const struct sockaddr_in *)b;
    return av4->sin_port == bv4->sin_port &&
        memcmp(&av4->sin_addr, &bv4->sin_addr, sizeof(av4->sin_addr)) == 0;
}

static int test_turn_xor_addr(unsigned char out[8],
    const struct sockaddr_storage *addr)
{
    const struct sockaddr_in *v4;
    uint32_t raw;
    uint32_t xraw;
    unsigned short port;
    unsigned short xport;

    if (!out || !addr || addr->ss_family != AF_INET) return 0;
    v4 = (const struct sockaddr_in *)addr;
    port = ntohs(v4->sin_port);
    memcpy(&raw, &v4->sin_addr, sizeof(raw));
    raw = ntohl(raw);
    xport = port ^ (unsigned short)(TEST_TURN_MAGIC >> 16);
    xraw = raw ^ TEST_TURN_MAGIC;
    memset(out, 0, 8);
    out[1] = 0x01;
    out[2] = (unsigned char)(xport >> 8);
    out[3] = (unsigned char)xport;
    test_turn_put32(out + 4, xraw);
    return 1;
}

static int test_turn_decode_xor_addr(const unsigned char *data, int len,
    struct sockaddr_storage *out)
{
    struct sockaddr_in *v4;
    unsigned short xport;
    uint32_t xraw;
    uint32_t raw;

    if (!data || len < 8 || !out || data[1] != 0x01) return 0;
    xport = (unsigned short)test_turn_u16(data + 2);
    xraw = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
        ((uint32_t)data[6] << 8) | data[7];
    raw = xraw ^ TEST_TURN_MAGIC;
    memset(out, 0, sizeof(*out));
    v4 = (struct sockaddr_in *)out;
    v4->sin_family = AF_INET;
    v4->sin_port = htons(xport ^ (unsigned short)(TEST_TURN_MAGIC >> 16));
    raw = htonl(raw);
    memcpy(&v4->sin_addr, &raw, sizeof(raw));
    return 1;
}

static int test_turn_send(test_turn_server_t *server,
    const unsigned char *data, int len, const struct sockaddr_storage *to,
    socklen_t to_len)
{
    int sent;

    if (!server || !data || len <= 0 || !to) return 0;
    sent = (int)sendto(server->fd, (const char *)data, len, 0,
        (const struct sockaddr *)to, to_len);
    return sent == len;
}

static test_turn_allocation_t *test_turn_find_client(test_turn_server_t *server,
    const struct sockaddr_storage *client)
{
    int i;

    for (i = 0; i < TEST_TURN_MAX_ALLOCS; i++) {
        if (server->allocs[i].used &&
            test_turn_sockaddr_equal(&server->allocs[i].client, client))
            return &server->allocs[i];
    }
    return NULL;
}

static test_turn_allocation_t *test_turn_find_relay(test_turn_server_t *server,
    const struct sockaddr_storage *relay)
{
    int i;

    for (i = 0; i < TEST_TURN_MAX_ALLOCS; i++) {
        if (server->allocs[i].used &&
            test_turn_sockaddr_equal(&server->allocs[i].relay, relay))
            return &server->allocs[i];
    }
    return NULL;
}

static test_turn_allocation_t *test_turn_allocate_client(
    test_turn_server_t *server, const struct sockaddr_storage *client,
    socklen_t client_len)
{
    test_turn_allocation_t *allocation;
    int i;

    allocation = test_turn_find_client(server, client);
    if (allocation) return allocation;
    for (i = 0; i < TEST_TURN_MAX_ALLOCS; i++) {
        struct sockaddr_in *relay;

        if (server->allocs[i].used) continue;
        allocation = &server->allocs[i];
        memset(allocation, 0, sizeof(*allocation));
        allocation->used = 1;
        allocation->client = *client;
        allocation->client_len = client_len;
        relay = (struct sockaddr_in *)&allocation->relay;
        relay->sin_family = AF_INET;
        relay->sin_port = htons((unsigned short)(40000 + i));
        relay->sin_addr.s_addr = htonl(0xc000020aU + (unsigned int)i);
        allocation->relay_len = sizeof(*relay);
        atomic_fetch_add(&server->allocations, 1);
        return allocation;
    }
    return NULL;
}

static void test_turn_reply_error(test_turn_server_t *server,
    const unsigned char *request, const struct sockaddr_storage *client,
    socklen_t client_len)
{
    unsigned char response[512];
    unsigned char error[4] = {0, 0, 4, 1};
    const char realm[] = "redp2p-test";
    const char nonce[] = "test-nonce";
    int offset;

    offset = test_turn_begin(response, sizeof(response), TEST_TURN_ALLOCATE_ERR,
        request + 8);
    if (offset == 0 ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_ERROR_CODE, error, sizeof(error)) ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_REALM, realm, sizeof(realm) - 1) ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_NONCE, nonce, sizeof(nonce) - 1))
        return;
    test_turn_finish(response, offset);
    (void)test_turn_send(server, response, offset, client, client_len);
}

static void test_turn_reply_allocate(test_turn_server_t *server,
    const unsigned char *request, const struct sockaddr_storage *client,
    socklen_t client_len)
{
    test_turn_allocation_t *allocation;
    unsigned char response[512];
    unsigned char xaddr[8];
    unsigned char lifetime[4];
    int offset;

    allocation = test_turn_allocate_client(server, client, client_len);
    if (!allocation || !test_turn_xor_addr(xaddr, &allocation->relay)) return;
    test_turn_put32(lifetime, 600U);
    offset = test_turn_begin(response, sizeof(response), TEST_TURN_ALLOCATE_OK,
        request + 8);
    if (offset == 0 ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_XOR_RELAYED, xaddr, sizeof(xaddr)) ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_LIFETIME, lifetime, sizeof(lifetime)))
        return;
    test_turn_finish(response, offset);
    (void)test_turn_send(server, response, offset, client, client_len);
}

static void test_turn_reply_simple(test_turn_server_t *server,
    const unsigned char *request, unsigned int type,
    const struct sockaddr_storage *client, socklen_t client_len)
{
    unsigned char response[128];
    unsigned char lifetime[4];
    int offset;

    offset = test_turn_begin(response, sizeof(response), type, request + 8);
    if (offset == 0) return;
    if (type == TEST_TURN_REFRESH_OK) {
        test_turn_put32(lifetime, 600U);
        if (!test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_LIFETIME, lifetime, sizeof(lifetime)))
            return;
    }
    test_turn_finish(response, offset);
    (void)test_turn_send(server, response, offset, client, client_len);
}

static void test_turn_forward(test_turn_server_t *server,
    const unsigned char *request, int request_len,
    const struct sockaddr_storage *client)
{
    test_turn_allocation_t *source;
    test_turn_allocation_t *target;
    struct sockaddr_storage peer;
    unsigned char response[TEST_TURN_PACKET_MAX];
    unsigned char xaddr[8];
    int peer_len;
    int peer_off;
    int data_len;
    int data_off;
    int offset;

    source = test_turn_find_client(server, client);
    if (!source) return;
    peer_off = test_turn_attr_find(request, request_len,
        TEST_TURN_ATTR_XOR_PEER, &peer_len);
    data_off = test_turn_attr_find(request, request_len,
        TEST_TURN_ATTR_DATA, &data_len);
    if (peer_off < 0 || data_off < 0 || data_len < 0 ||
        !test_turn_decode_xor_addr(request + peer_off, peer_len, &peer))
        return;
    target = test_turn_find_relay(server, &peer);
    if (!target || !test_turn_xor_addr(xaddr, &source->relay)) return;

    offset = test_turn_begin(response, sizeof(response), TEST_TURN_DATA_IND,
        request + 8);
    if (offset == 0 ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_XOR_PEER, xaddr, sizeof(xaddr)) ||
        !test_turn_attr_add(response, sizeof(response), &offset,
            TEST_TURN_ATTR_DATA, request + data_off, (size_t)data_len))
        return;
    test_turn_finish(response, offset);
    if (test_turn_send(server, response, offset, &target->client,
        target->client_len))
        atomic_fetch_add(&server->indications, 1);
}

#ifdef _WIN32
static DWORD WINAPI test_turn_main(void *arg)
#else
static void *test_turn_main(void *arg)
#endif
{
    test_turn_server_t *server = (test_turn_server_t *)arg;
    unsigned char packet[TEST_TURN_PACKET_MAX];

    while (!atomic_load(&server->stop)) {
        struct sockaddr_storage client;
        socklen_t client_len = sizeof(client);
        int type;
        int n;

        n = (int)recvfrom(server->fd, (char *)packet, sizeof(packet), 0,
            (struct sockaddr *)&client, &client_len);
        if (n <= 0) continue;
        type = test_turn_header(packet, n);
        if (type < 0) continue;
        if (type == TEST_TURN_ALLOCATE_REQ) {
            if (test_turn_attr_find(packet, n, TEST_TURN_ATTR_USERNAME,
                NULL) < 0)
                test_turn_reply_error(server, packet, &client, client_len);
            else
                test_turn_reply_allocate(server, packet, &client, client_len);
        } else if (type == TEST_TURN_PERMISSION_REQ) {
            atomic_fetch_add(&server->permissions, 1);
            test_turn_reply_simple(server, packet, TEST_TURN_PERMISSION_OK,
                &client, client_len);
        } else if (type == TEST_TURN_REFRESH_REQ) {
            test_turn_reply_simple(server, packet, TEST_TURN_REFRESH_OK,
                &client, client_len);
        } else if (type == TEST_TURN_SEND_IND) {
            test_turn_forward(server, packet, n, &client);
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int test_turn_server_start(test_turn_server_t *server)
{
    struct sockaddr_in address;
    socklen_t length;

    memset(server, 0, sizeof(*server));
    server->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->fd == TEST_SOCKET_INVALID) return 1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server->fd, (const struct sockaddr *)&address,
        sizeof(address)) != 0)
    {
        test_socket_close(server->fd);
        server->fd = TEST_SOCKET_INVALID;
        return 1;
    }
    length = sizeof(address);
    if (getsockname(server->fd, (struct sockaddr *)&address, &length) != 0) {
        test_socket_close(server->fd);
        server->fd = TEST_SOCKET_INVALID;
        return 1;
    }
    server->port = ntohs(address.sin_port);
    test_socket_timeout(server->fd, 100U);
    if (test_thread_start(&server->thread, test_turn_main, server) != 0) {
        test_socket_close(server->fd);
        server->fd = TEST_SOCKET_INVALID;
        return 1;
    }
    return 0;
}

static int test_turn_server_stop(test_turn_server_t *server)
{
    struct sockaddr_in address;
    int result;

    if (!server || server->fd == TEST_SOCKET_INVALID) return 0;
    atomic_store(&server->stop, 1);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(server->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    (void)sendto(server->fd, "x", 1, 0,
        (const struct sockaddr *)&address, sizeof(address));
    result = test_thread_join(server->thread);
    test_socket_close(server->fd);
    server->fd = TEST_SOCKET_INVALID;
    return result;
}

static int test_turn_api_roundtrip(int protocol, const char *id,
    const char *index, const char *turn, test_turn_server_t *server,
    int force_turn)
{
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_con_t *con = NULL;
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    test_direct_api_state_t state;
    uint64_t deadline;
    int indications_before;
    int fail = 0;
    int status;

    indications_before = atomic_load(&server->indications);
    fail += test_setenv("REDP2P_TEST_FORCE_TURN", force_turn ? "1" : NULL);
    memset(&state, 0, sizeof(state));
    atomic_store(&state.respond_status, KC_REDP2P_ERROR);

    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = id;
    pub_options.index = index;
    pub_options.protocol = protocol;
    pub_options.turn = turn;
    pub_options.turn_user = "test-user";
    pub_options.turn_pass = "test-pass";
    pub_options.receive = test_direct_api_pub_receive;
    pub_options.userdata = &state;
    if (fail == 0) {
        status = kc_redp2p_pub(&pub, &pub_options);
        fail += expect_int("TURN pub create", KC_REDP2P_OK, status);
    }

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = id;
    con_options.index = index;
    con_options.turn = turn;
    con_options.turn_user = "test-user";
    con_options.turn_pass = "test-pass";
    con_options.receive = test_direct_api_con_receive;
    con_options.userdata = &state;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("TURN con create", KC_REDP2P_OK, status);
    }
    if (fail == 0) {
        status = kc_redp2p_con_send(con, "ping", 4);
        fail += expect_int("TURN send", KC_REDP2P_OK, status);
    }

    deadline = redp2p_now_ms() + 10000U;
    while (fail == 0 && atomic_load(&state.consumer_received) == 0 &&
        redp2p_now_ms() < deadline)
        test_sleep_ms(10U);

    fail += expect_int("TURN publisher receive", 1,
        atomic_load(&state.publisher_received));
    fail += expect_int("TURN respond", KC_REDP2P_OK,
        atomic_load(&state.respond_status));
    fail += expect_int("TURN consumer receive", 1,
        atomic_load(&state.consumer_received));
    if (force_turn) {
        fail += expect_true("TURN allocation used",
            atomic_load(&server->allocations) >= 2);
        fail += expect_true("TURN permission used",
            atomic_load(&server->permissions) > 0);
        fail += expect_true("TURN relay carried data",
            atomic_load(&server->indications) > indications_before);
    } else {
        fail += expect_int("direct path preferred over TURN", indications_before,
            atomic_load(&server->indications));
    }

    kc_redp2p_con_close(con);
    kc_redp2p_pub_close(pub);
    fail += test_setenv("REDP2P_TEST_FORCE_TURN", NULL);
    return fail == 0 ? 0 : 1;
}

/**
 * Exercises optional TURN as an internal fallback without any external server.
 * Summary: A local TURN protocol stub provides allocations and relays REDP2P
 * datagrams. Forced relay verifies both UDP semantics and TCP/KCP semantics,
 * while an unforced run verifies that direct UDP remains preferred.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_turn_api(void)
{
    kc_redp2p_idx_t *idx = NULL;
    kc_redp2p_idx_options_t idx_options;
    test_turn_server_t turn_server;
    char local_ip[INET_ADDRSTRLEN];
    char index[320];
    char turn[128];
    unsigned short port;
    int fail = 0;

    turn_server.fd = TEST_SOCKET_INVALID;
    fail += expect_int("start simulated TURN", 0,
        test_turn_server_start(&turn_server));
    fail += expect_int("resolve TURN API unicast", 0,
        test_local_unicast_ipv4(local_ip));
    port = (unsigned short)(test_port_base() + 414U);
    memset(&idx_options, 0, sizeof(idx_options));
    idx_options.host = local_ip;
    idx_options.port = port;
    idx_options.max_consumers = 32;
    if (fail == 0)
        fail += expect_int("TURN API index", KC_REDP2P_OK,
            kc_redp2p_idx(&idx, &idx_options));

    snprintf(index, sizeof(index), "%s:%u", local_ip, (unsigned)port);
    snprintf(turn, sizeof(turn), "turn:127.0.0.1:%u",
        (unsigned)turn_server.port);

    if (fail == 0)
        fail += test_turn_api_roundtrip(KC_REDP2P_UDP, "turndirect", index,
            turn, &turn_server, 0);
    if (fail == 0)
        fail += test_turn_api_roundtrip(KC_REDP2P_UDP, "turnudp", index,
            turn, &turn_server, 1);
    if (fail == 0)
        fail += test_turn_api_roundtrip(KC_REDP2P_TCP, "turntcp", index,
            turn, &turn_server, 1);

    test_setenv("REDP2P_TEST_FORCE_TURN", NULL);
    kc_redp2p_idx_close(idx);
    if (turn_server.fd != TEST_SOCKET_INVALID)
        fail += test_turn_server_stop(&turn_server);
    case_result(fail, "kc_redp2p_turn_api",
        "simulated TURN preserves the public TCP/UDP data API and remains fallback-only");
    return fail == 0 ? 0 : 1;
}

/**
 * Exercises one native protocol through the capability-level direct data API.
 * @param protocol KC_REDP2P_TCP or KC_REDP2P_UDP.
 * @param id Publisher identifier.
 * @param index Index endpoint.
 * @return 0 on success, 1 on failure.
 */
static int test_direct_api_roundtrip(int protocol, const char *id,
    const char *index)
{
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_con_t *con = NULL;
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    test_direct_api_state_t state;
    uint64_t deadline;
    int fail = 0;
    int status;

    memset(&state, 0, sizeof(state));
    atomic_store(&state.respond_status, KC_REDP2P_ERROR);
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = id;
    pub_options.index = index;
    pub_options.protocol = protocol;
    pub_options.receive = test_direct_api_pub_receive;
    pub_options.userdata = &state;

    status = kc_redp2p_pub(&pub, &pub_options);
    fail += expect_int("direct pub create", KC_REDP2P_OK, status);
    fail += expect_true("direct pub handle", pub != NULL);

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = id;
    con_options.index = index;
    con_options.receive = test_direct_api_con_receive;
    con_options.userdata = &state;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("direct con create", KC_REDP2P_OK, status);
        fail += expect_true("direct con handle", con != NULL);
    }
    if (fail == 0) {
        status = kc_redp2p_con_send(con, "ping", 4);
        fail += expect_int("direct send", KC_REDP2P_OK, status);
    }

    deadline = redp2p_now_ms() + 10000U;
    while (fail == 0 && atomic_load(&state.consumer_received) == 0 &&
        redp2p_now_ms() < deadline)
        test_sleep_ms(10U);

    fail += expect_int("direct publisher receive", 1,
        atomic_load(&state.publisher_received));
    fail += expect_int("direct respond", KC_REDP2P_OK,
        atomic_load(&state.respond_status));
    fail += expect_int("direct consumer receive", 1,
        atomic_load(&state.consumer_received));

    kc_redp2p_con_close(con);
    kc_redp2p_pub_close(pub);
    return fail == 0 ? 0 : 1;
}

#ifdef REDP2P_TESTING
/**
 * Waits for a test-only I/O synchronization point.
 * Summary: Bounds deterministic lifecycle-race synchronization.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return 0 when entered, 1 on timeout.
 */
static int test_direct_api_wait_hook(uint64_t timeout_ms)
{
    uint64_t deadline = redp2p_now_ms() + timeout_ms;

    while (!redp2p_test_api_io_entered() && redp2p_now_ms() < deadline)
        test_sleep_ms(1U);
    return redp2p_test_api_io_entered() ? 0 : 1;
}
#endif

/**
 * Exercises consumer close against an in-flight send.
 * Summary: Verifies teardown waits for the protected consumer I/O section.
 * @param index Index endpoint.
 * @return 0 on success, 1 on failure.
 */
static int test_direct_api_con_close_race(const char *index)
{
#ifdef REDP2P_TESTING
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    test_direct_race_state_t send_state;
    test_direct_race_state_t close_state;
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_con_t *con = NULL;
    test_thread_t send_thread;
    test_thread_t close_thread;
    int send_started = 0;
    int close_started = 0;
    int fail = 0;

    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "racecon";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_UDP;
    pub_options.receive = test_direct_close_echo;
    fail += expect_int("race con pub", KC_REDP2P_OK,
        kc_redp2p_pub(&pub, &pub_options));

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "racecon";
    con_options.index = index;
    if (fail == 0)
        fail += expect_int("race con create", KC_REDP2P_OK,
            kc_redp2p_con(&con, &con_options));

    memset(&send_state, 0, sizeof(send_state));
    memset(&close_state, 0, sizeof(close_state));
    send_state.con = con;
    close_state.con = con;
    redp2p_test_api_io_hold(1);
    if (fail == 0) {
        int rc = test_thread_start(&send_thread, test_direct_send_thread,
            &send_state);
        fail += rc;
        send_started = rc == 0;
    }
    if (fail == 0)
        fail += test_direct_api_wait_hook(5000U);
    if (fail == 0) {
        int rc = test_thread_start(&close_thread,
            test_direct_con_close_thread, &close_state);
        fail += rc;
        close_started = rc == 0;
    }
    test_sleep_ms(20U);
    if (fail == 0)
        fail += expect_int("con close waits for send", 0,
            atomic_load(&close_state.done));
    redp2p_test_api_io_hold(0);
    if (send_started) fail += test_thread_join(send_thread);
    if (close_started) fail += test_thread_join(close_thread);
    if (close_started)
        fail += expect_int("con close completes", 1,
            atomic_load(&close_state.done));

    if (!close_started && con) kc_redp2p_con_close(con);
    kc_redp2p_pub_close(pub);
    redp2p_test_api_io_hold(0);
    return fail == 0 ? 0 : 1;
#else
    (void)index;
    return 0;
#endif
}

/**
 * Exercises publisher close against in-flight client I/O.
 * Summary: Covers both respond and client-close serialization paths.
 * @param index Index endpoint.
 * @param close_client Nonzero to race client close, zero to race respond.
 * @return 0 on success, 1 on failure.
 */
static int test_direct_api_pub_close_race(const char *index, int close_client)
{
#ifdef REDP2P_TESTING
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    test_direct_race_state_t capture;
    test_direct_race_state_t io_state;
    test_direct_race_state_t close_state;
    kc_redp2p_pub_t *pub = NULL;
    kc_redp2p_con_t *con = NULL;
    test_thread_t io_thread;
    test_thread_t close_thread;
    uint64_t deadline;
    int io_started = 0;
    int close_started = 0;
    int fail = 0;

    memset(&capture, 0, sizeof(capture));
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = close_client ? "raceclientclose" : "racerespond";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.receive = test_direct_capture_client;
    pub_options.userdata = &capture;
    fail += expect_int("race pub create", KC_REDP2P_OK,
        kc_redp2p_pub(&pub, &pub_options));

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = pub_options.id;
    con_options.index = index;
    if (fail == 0)
        fail += expect_int("race pub con", KC_REDP2P_OK,
            kc_redp2p_con(&con, &con_options));
    if (fail == 0)
        fail += expect_int("race capture send", KC_REDP2P_OK,
            kc_redp2p_con_send(con, "x", 1));

    deadline = redp2p_now_ms() + 5000U;
    while (fail == 0 && !atomic_load(&capture.done) &&
        redp2p_now_ms() < deadline)
        test_sleep_ms(10U);
    fail += expect_int("race client captured", 1, atomic_load(&capture.done));

    memset(&io_state, 0, sizeof(io_state));
    memset(&close_state, 0, sizeof(close_state));
    io_state.client = capture.client;
    close_state.pub = pub;
    redp2p_test_api_io_hold(1);
    if (fail == 0) {
        int rc = test_thread_start(&io_thread,
            close_client ? test_direct_client_close_thread :
                test_direct_respond_thread, &io_state);
        fail += rc;
        io_started = rc == 0;
    }
    if (fail == 0)
        fail += test_direct_api_wait_hook(5000U);
    if (fail == 0) {
        int rc = test_thread_start(&close_thread,
            test_direct_pub_close_thread, &close_state);
        fail += rc;
        close_started = rc == 0;
    }
    test_sleep_ms(20U);
    if (fail == 0)
        fail += expect_int("pub close waits for client io", 0,
            atomic_load(&close_state.done));
    redp2p_test_api_io_hold(0);
    if (io_started) fail += test_thread_join(io_thread);
    if (close_started) fail += test_thread_join(close_thread);
    if (close_started)
        fail += expect_int("pub close completes", 1,
            atomic_load(&close_state.done));

    kc_redp2p_con_close(con);
    if (!close_started && pub) kc_redp2p_pub_close(pub);
    redp2p_test_api_io_hold(0);
    return fail == 0 ? 0 : 1;
#else
    (void)index;
    (void)close_client;
    return 0;
#endif
}

/**
 * Exercises retry after an ephemeral direct-consumer port is stolen.
 * Summary: Forces the local bind race and expects startup to recover.
 * @param index Index endpoint.
 * @return 0 on success, 1 on failure.
 */
static int test_direct_api_port_retry(const char *index)
{
#ifdef REDP2P_TESTING
    kc_redp2p_pub_options_t pub_options;
    test_direct_race_state_t state;
    kc_redp2p_pub_t *pub = NULL;
    test_thread_t con_thread;
    test_socket_t blocker = TEST_SOCKET_INVALID;
    int con_started = 0;
    struct sockaddr_in address;
    unsigned short port;
    uint64_t deadline;
    int fail = 0;
    int reuse = 1;

    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "retryport";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.receive = test_direct_close_echo;
    fail += expect_int("retry pub create", KC_REDP2P_OK,
        kc_redp2p_pub(&pub, &pub_options));

    memset(&state, 0, sizeof(state));
    state.index = index;
    atomic_store(&state.result, KC_REDP2P_ERROR);
    redp2p_test_api_port_hold(1);
    if (fail == 0) {
        int rc = test_thread_start(&con_thread,
            test_direct_con_create_thread, &state);
        fail += rc;
        con_started = rc == 0;
    }

    deadline = redp2p_now_ms() + 5000U;
    port = 0;
    while (fail == 0 && port == 0 && redp2p_now_ms() < deadline) {
        port = redp2p_test_api_port_value();
        if (port == 0) test_sleep_ms(1U);
    }
    fail += expect_true("retry selected port", port != 0);

    if (fail == 0) {
        blocker = socket(AF_INET, SOCK_STREAM, 0);
        fail += expect_true("retry blocker socket",
            blocker != TEST_SOCKET_INVALID);
    }
    if (fail == 0) {
        setsockopt(blocker, SOL_SOCKET, SO_REUSEADDR,
            (const char *)&reuse, sizeof(reuse));
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(0x7f000001u);
        fail += expect_int("retry steal port", 0,
            bind(blocker, (const struct sockaddr *)&address,
                sizeof(address)));
        if (fail == 0)
            fail += expect_int("retry listen stolen port", 0,
                listen(blocker, 1));
    }

    redp2p_test_api_port_hold(0);
    if (con_started) fail += test_thread_join(con_thread);
    if (con_started)
        fail += expect_int("retry consumer succeeds", KC_REDP2P_OK,
            atomic_load(&state.result));

    if (blocker != TEST_SOCKET_INVALID) test_socket_close(blocker);
    if (state.con) kc_redp2p_con_close(state.con);
    kc_redp2p_pub_close(pub);
    redp2p_test_api_port_hold(0);
    return fail == 0 ? 0 : 1;
#else
    (void)index;
    return 0;
#endif
}

/**
 * Exercises publisher and consumer close from their own callbacks.
 * Summary: Verifies adapter-thread self-close completes without self-join.
 * @param index Index endpoint.
 * @return 0 on success, 1 on failure.
 */
static int test_direct_api_self_close(const char *index)
{
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    test_direct_close_state_t state;
    kc_redp2p_pub_t *pub;
    kc_redp2p_con_t *con;
    uint64_t deadline;
    int fail;
    int status;

    fail = 0;
    pub = NULL;
    con = NULL;
    memset(&state, 0, sizeof(state));
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "selfclosepub";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.receive = test_direct_pub_self_close;
    pub_options.userdata = &state;
    status = kc_redp2p_pub(&pub, &pub_options);
    fail += expect_int("self-close pub create", KC_REDP2P_OK, status);
    state.pub = pub;

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "selfclosepub";
    con_options.index = index;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("self-close pub con", KC_REDP2P_OK, status);
    }
    if (fail == 0)
        fail += expect_int("self-close pub send", KC_REDP2P_OK,
            kc_redp2p_con_send(con, "x", 1));

    deadline = redp2p_now_ms() + 5000U;
    while (fail == 0 && !atomic_load(&state.closed) &&
        redp2p_now_ms() < deadline)
        test_sleep_ms(10U);
    fail += expect_int("publisher closes from callback", 1,
        atomic_load(&state.closed));
    kc_redp2p_con_close(con);
    if (state.pub) kc_redp2p_pub_close(state.pub);

    pub = NULL;
    con = NULL;
    memset(&state, 0, sizeof(state));
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "selfclosecon";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.receive = test_direct_close_echo;
    status = kc_redp2p_pub(&pub, &pub_options);
    fail += expect_int("self-close con pub", KC_REDP2P_OK, status);

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "selfclosecon";
    con_options.index = index;
    con_options.receive = test_direct_con_self_close;
    con_options.userdata = &state;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("self-close con create", KC_REDP2P_OK, status);
        state.con = con;
    }
    if (fail == 0)
        fail += expect_int("self-close con send", KC_REDP2P_OK,
            kc_redp2p_con_send(con, "x", 1));

    deadline = redp2p_now_ms() + 5000U;
    while (fail == 0 && !atomic_load(&state.closed) &&
        redp2p_now_ms() < deadline)
        test_sleep_ms(10U);
    fail += expect_int("consumer closes from callback", 1,
        atomic_load(&state.closed));
    if (state.con) kc_redp2p_con_close(state.con);
    kc_redp2p_pub_close(pub);
    return fail == 0 ? 0 : 1;
}

/**
 * Exercises direct receive/respond/send for TCP and UDP without public ports.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_direct_api(void)
{
    kc_redp2p_idx_t *idx = NULL;
    kc_redp2p_idx_options_t idx_options;
    char index[320];
    char local_ip[INET_ADDRSTRLEN];
    unsigned short port;
    int fail = 0;

    port = (unsigned short)(test_port_base() + 413U);
    fail += expect_int("resolve direct API unicast", 0,
        test_local_unicast_ipv4(local_ip));
    memset(&idx_options, 0, sizeof(idx_options));
    idx_options.host = local_ip;
    idx_options.port = port;
    idx_options.max_consumers = 32;
    if (fail == 0)
        fail += expect_int("direct API index", KC_REDP2P_OK,
            kc_redp2p_idx(&idx, &idx_options));

    snprintf(index, sizeof(index), "%s:%u", local_ip, (unsigned)port);
    if (fail == 0)
        fail += test_direct_api_roundtrip(KC_REDP2P_TCP, "directtcp", index);
    if (fail == 0)
        fail += test_direct_api_roundtrip(KC_REDP2P_UDP, "directudp", index);
    if (fail == 0)
        fail += test_direct_api_self_close(index);
    if (fail == 0)
        fail += test_direct_api_con_close_race(index);
    if (fail == 0)
        fail += test_direct_api_pub_close_race(index, 0);
    if (fail == 0)
        fail += test_direct_api_pub_close_race(index, 1);
    if (fail == 0)
        fail += test_direct_api_port_retry(index);

    kc_redp2p_idx_close(idx);
    case_result(fail, "kc_redp2p_direct_api",
        "native TCP and UDP support direct receive/respond/send");
    return fail == 0 ? 0 : 1;
}

/**
 * Exercises the normalized public idx/pub/con capability API end to end.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_redp2p_api(void)
{
    const char *name = "kc_redp2p_api";
    const char *detail =
        "idx owns the explicit port while pub/con expose peer channels and data";
    const unsigned char payload[] = "redp2p-public-api";
    kc_redp2p_idx_t *idx;
    kc_redp2p_pub_t *pub;
    kc_redp2p_con_t *con;
    kc_redp2p_idx_entry_t *entries;
    kc_redp2p_idx_options_t idx_options;
    kc_redp2p_pub_options_t pub_options;
    kc_redp2p_con_options_t con_options;
    char index[320];
    char local_ip[INET_ADDRSTRLEN];
    unsigned short idx_port;
    size_t count;
    int fail;
    int status;

    idx = NULL;
    pub = NULL;
    con = NULL;
    entries = NULL;
    count = 0;
    fail = 0;
    idx_port = (unsigned short)(test_port_base() + 410U);

    fail += expect_int("resolve local unicast", 0,
        test_local_unicast_ipv4(local_ip));

    memset(&idx_options, 0, sizeof(idx_options));
    idx_options.host = local_ip;
    idx_options.port = idx_port;
    idx_options.max_consumers = 32;
    if (fail == 0) {
        status = kc_redp2p_idx(&idx, &idx_options);
        fail += expect_int("public idx create", KC_REDP2P_OK, status);
        fail += expect_true("public idx handle", idx != NULL);
    }
    if (fail == 0) {
        status = kc_redp2p_idx_list(idx, &entries, &count);
        fail += expect_int("public empty list", KC_REDP2P_OK, status);
        fail += expect_true("public empty list count", count == 0);
        fail += expect_true("public empty list pointer", entries == NULL);
    }

    snprintf(index, sizeof(index), "%s:%u", local_ip, (unsigned)idx_port);
    memset(&test_direct_receive, 0, sizeof(test_direct_receive));
    atomic_store(&test_direct_connected, 0);
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "echo";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_TCP;
    pub_options.connect = test_direct_pub_connect;
    pub_options.receive = test_direct_pub_receive;
    if (fail == 0) {
        status = kc_redp2p_pub(&pub, &pub_options);
        fail += expect_int("public TCP pub create", KC_REDP2P_OK, status);
        fail += expect_true("public TCP pub handle", pub != NULL);
    }
    if (fail == 0) {
        status = kc_redp2p_idx_list(idx, &entries, &count);
        fail += expect_int("public populated list", KC_REDP2P_OK, status);
        fail += expect_true("public populated list count", count == 1);
        fail += expect_true("public populated list id",
            entries != NULL && strcmp(entries[0].id, "echo") == 0);
        kc_redp2p_free(entries);
        entries = NULL;
        count = 0;
    }

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "echo";
    con_options.index = index;
    con_options.receive = test_direct_con_receive;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("public TCP con create", KC_REDP2P_OK, status);
        fail += expect_true("public TCP con handle", con != NULL);
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 5000U;
        while (!atomic_load(&test_direct_connected) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public TCP publisher channel",
            atomic_load(&test_direct_connected));
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 5000U;
        while (!atomic_load(&test_direct_receive.received) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public TCP channel may send immediately",
            atomic_load(&test_direct_receive.received) &&
            test_direct_receive.size == 5 &&
            memcmp(test_direct_receive.data, "ready", 5) == 0);
        memset(&test_direct_receive, 0, sizeof(test_direct_receive));
    }
    if (fail == 0) {
        status = kc_redp2p_con_send(con, payload, sizeof(payload));
        fail += expect_int("public TCP send", KC_REDP2P_OK, status);
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 10000U;
        while (!atomic_load(&test_direct_receive.received) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public TCP receive",
            atomic_load(&test_direct_receive.received) &&
            test_direct_receive.size == sizeof(payload) &&
            memcmp(test_direct_receive.data, payload, sizeof(payload)) == 0);
    }

    kc_redp2p_con_close(con);
    con = NULL;
    kc_redp2p_pub_close(pub);
    pub = NULL;

    memset(&test_direct_receive, 0, sizeof(test_direct_receive));
    atomic_store(&test_direct_connected, 0);
    memset(&pub_options, 0, sizeof(pub_options));
    pub_options.id = "echoudp";
    pub_options.index = index;
    pub_options.protocol = KC_REDP2P_UDP;
    pub_options.connect = test_direct_pub_connect;
    pub_options.receive = test_direct_pub_receive;
    if (fail == 0) {
        status = kc_redp2p_pub(&pub, &pub_options);
        fail += expect_int("public UDP pub create", KC_REDP2P_OK, status);
    }

    memset(&con_options, 0, sizeof(con_options));
    con_options.id = "echoudp";
    con_options.index = index;
    con_options.receive = test_direct_con_receive;
    if (fail == 0) {
        status = kc_redp2p_con(&con, &con_options);
        fail += expect_int("public UDP con create", KC_REDP2P_OK, status);
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 5000U;
        while (!atomic_load(&test_direct_connected) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public UDP publisher channel",
            atomic_load(&test_direct_connected));
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 5000U;
        while (!atomic_load(&test_direct_receive.received) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public UDP channel may send immediately",
            atomic_load(&test_direct_receive.received) &&
            test_direct_receive.size == 5 &&
            memcmp(test_direct_receive.data, "ready", 5) == 0);
        memset(&test_direct_receive, 0, sizeof(test_direct_receive));
    }
    if (fail == 0) {
        status = kc_redp2p_con_send(con, payload, sizeof(payload));
        fail += expect_int("public UDP send", KC_REDP2P_OK, status);
    }
    if (fail == 0) {
        uint64_t deadline = test_now_ms() + 10000U;
        while (!atomic_load(&test_direct_receive.received) &&
            test_now_ms() < deadline)
            test_sleep_ms(10);
        fail += expect_true("public UDP receive",
            atomic_load(&test_direct_receive.received) &&
            test_direct_receive.size == sizeof(payload) &&
            memcmp(test_direct_receive.data, payload, sizeof(payload)) == 0);
    }

    kc_redp2p_con_close(con);
    kc_redp2p_pub_close(pub);
    kc_redp2p_idx_close(idx);

    fail += expect_string("public strerror OK", "OK",
        kc_redp2p_strerror(KC_REDP2P_OK));
    kc_redp2p_free(NULL);
    kc_redp2p_con_close(NULL);
    kc_redp2p_pub_close(NULL);
    kc_redp2p_idx_close(NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 18;
    test_case_current = 0;
    run_case(&rc, case_kc_redp2p_validation);
    run_case(&rc, case_kc_redp2p_register);
    run_case(&rc, case_kc_redp2p_context);
    run_case(&rc, case_kc_redp2p_setters);
    run_case(&rc, case_kc_redp2p_set_state_dir);
    run_case(&rc, case_kc_redp2p_serve_index);
    run_case(&rc, case_kc_redp2p_wait);
    run_case(&rc, case_kc_redp2p_connect);
    run_case(&rc, case_kc_redp2p_heartbeat);
    run_case(&rc, case_kc_redp2p_rtc_index);
    run_case(&rc, case_redp2p_protocol_ttl);
    run_case(&rc, case_kc_redp2p_udp_tunnel);
    run_case(&rc, case_kc_redp2p_tcp_stream);
    run_case(&rc, case_redp2p_persisted_deregister);
    run_case(&rc, case_kc_redp2p_list_publishers);
    run_case(&rc, case_kc_redp2p_api);
    run_case(&rc, case_kc_redp2p_direct_api);
    run_case(&rc, case_kc_redp2p_turn_api);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches one named test case.
 * @param name Public API function name.
 * @return 0 on success, 1 on failure, 2 for an unknown case.
 */
static int dispatch_case(const char *name) {
    if (strcmp(name, "all") == 0) return case_all();
    if (strcmp(name, "kc_redp2p_validation") == 0) return case_kc_redp2p_validation();
    if (strcmp(name, "kc_redp2p_register") == 0) return case_kc_redp2p_register();
    if (strcmp(name, "kc_redp2p_context") == 0) return case_kc_redp2p_context();
    if (strcmp(name, "kc_redp2p_setters") == 0) return case_kc_redp2p_setters();
    if (strcmp(name, "kc_redp2p_set_state_dir") == 0) return case_kc_redp2p_set_state_dir();
    if (strcmp(name, "kc_redp2p_serve_index") == 0) return case_kc_redp2p_serve_index();
    if (strcmp(name, "kc_redp2p_wait") == 0) return case_kc_redp2p_wait();
    if (strcmp(name, "kc_redp2p_connect") == 0) return case_kc_redp2p_connect();
    if (strcmp(name, "kc_redp2p_heartbeat") == 0) return case_kc_redp2p_heartbeat();
    if (strcmp(name, "kc_redp2p_rtc_index") == 0) return case_kc_redp2p_rtc_index();
    if (strcmp(name, "kc_redp2p_ttl_expiry") == 0) return case_redp2p_protocol_ttl();
    if (strcmp(name, "kc_redp2p_udp_tunnel") == 0) return case_kc_redp2p_udp_tunnel();
    if (strcmp(name, "kc_redp2p_tcp_stream") == 0) return case_kc_redp2p_tcp_stream();
    if (strcmp(name, "redp2p_persisted_deregister") == 0) return case_redp2p_persisted_deregister();
    if (strcmp(name, "kc_redp2p_list_publishers") == 0) return case_kc_redp2p_list_publishers();
    if (strcmp(name, "kc_redp2p_api") == 0) return case_kc_redp2p_api();
    if (strcmp(name, "kc_redp2p_direct_api") == 0) return case_kc_redp2p_direct_api();
    if (strcmp(name, "kc_redp2p_turn_api") == 0) return case_kc_redp2p_turn_api();
    fprintf(stderr, "unknown test case: %s\n", name);
    return 2;
}

/**
 * Runs one public API contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, non-zero on failure.
 */
int main(int argc, char **argv) {
    int rc;

    if (argc != 2) {
        fprintf(stderr, "expected one test case argument\n");
        return 2;
    }
    test_case_name = argv[1];
    if (strcmp(argv[1], "all") != 0) {
        test_case_total = 1;
        test_case_current = 1;
    }
    test_setenv("REDP2P_PUNCH_POLL_MS", "50");
    if (test_home() != 0) return 1;
    if (test_socket_start() != 0) {
        test_home_cleanup();
        return 1;
    }
    rc = dispatch_case(argv[1]);
    test_port_base_release();
    test_socket_stop();
    if (test_home_cleanup() != 0 && rc == 0) rc = 1;
    return rc;
}
