/**
 * test.c - libtls public API contract tests.
 * Summary: Exercises client/server TLS through in-memory byte transport.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libtls.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_HOST "localhost"
#define TEST_LARGE_SIZE 70000U
#define TEST_MAX_STEPS 200000U

static const unsigned char test_ca[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBhDCCASugAwIBAgIUdAI09dCzLbvdoEMrJ3iVBLNlZpowCgYIKoZIzj0EAwIw\n"
    "GDEWMBQGA1UEAwwNdGxzaW8gdGVzdCBDQTAeFw0yNjEwMTYyMDE2MzBaFw0zNjEw\n"
    "MTMyMDE2MzBaMBgxFjAUBgNVBAMMDXRsc2lvIHRlc3QgQ0EwWTATBgcqhkjOPQIB\n"
    "BggqhkjOPQMBBwNCAARTGdP+upd/n2xdVJ4xu1mzuOXXc7jkuhnjKoW1vYuQK9UW\n"
    "WaK8VcTDl0XXjy4G31/vgkmMje37zq7pElRQjYYRo1MwUTAdBgNVHQ4EFgQUl/Pu\n"
    "LHzP5SwKhyiLo/NKMoXhivwwHwYDVR0jBBgwFoAUl/PuLHzP5SwKhyiLo/NKMoXh\n"
    "ivwwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNHADBEAiBOjtAkbM2JevQx\n"
    "wYGaJloVN1Up1MPrn2KSHs60Kt2TjgIgXIzD6xz5mT2H7r8Ey7Pg1wONq12bB2No\n"
    "H9c5uRLOXME=\n"
    "-----END CERTIFICATE-----\n";

static const unsigned char test_cert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBnDCCAUGgAwIBAgIUP4nY0m5PvQwX/EDP6fYUATc6TCwwCgYIKoZIzj0EAwIw\n"
    "GDEWMBQGA1UEAwwNdGxzaW8gdGVzdCBDQTAeFw0yNjEwMTYyMDE2MzFaFw0zNjEw\n"
    "MTMyMDE2MzFaMBQxEjAQBgNVBAMMCWxvY2FsaG9zdDBZMBMGByqGSM49AgEGCCqG\n"
    "SM49AwEHA0IABFwKGLENj+lCLXfWFedw2TUCHn09mrEJlAH3+3X+EnFS9Zukf71A\n"
    "JFO4LoTmxMA5K6A9EtXNofxVM+Lv74nGZn6jbTBrMBQGA1UdEQQNMAuCCWxvY2Fs\n"
    "aG9zdDATBgNVHSUEDDAKBggrBgEFBQcDATAdBgNVHQ4EFgQUUYVjC9eJnrfUbkHt\n"
    "9sDQxGr2GiEwHwYDVR0jBBgwFoAUl/PuLHzP5SwKhyiLo/NKMoXhivwwCgYIKoZI\n"
    "zj0EAwIDSQAwRgIhAOVk87TY0Rdz+5iyLgaMJhGgK1InQaxCJDH/VtyAQIuEAiEA\n"
    "9t0jx9dvAmb4B0qZOzTvcRqCKwt86OpiHx9RDpE6dfc=\n"
    "-----END CERTIFICATE-----\n";

static const unsigned char test_key[] =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIDSENh4UYsMXDPv2XF6cF6+3O3vHcwReaeUNuRX25Ez9oAoGCCqGSM49\n"
    "AwEHoUQDQgAEXAoYsQ2P6UItd9YV53DZNQIefT2asQmUAff7df4ScVL1m6R/vUAk\n"
    "U7guhObEwDkroD0S1c2h/FUz4u/vicZmfg==\n"
    "-----END EC PRIVATE KEY-----\n";

typedef struct chunk {
    unsigned char *data;
    size_t size;
    size_t offset;
    struct chunk *next;
} chunk_t;

typedef struct {
    chunk_t *head;
    chunk_t *tail;
} wire_t;

typedef struct {
    wire_t *wire;
    unsigned char *plain;
    size_t size;
    size_t capacity;
    int failed;
} side_t;

typedef struct {
    kc_tls_t *client;
    kc_tls_t *server;
    wire_t c2s;
    wire_t s2c;
    side_t client_side;
    side_t server_side;
} pair_t;

static int case_total;
static int case_current;

/**
 * Records one assertion result.
 * @param label Assertion description.
 * @param condition Nonzero when the assertion holds.
 * @return One when the assertion failed, otherwise zero.
 */
static int expect(const char *label, int condition) {
    if (!condition) printf("[FAIL] %s\n", label);
    return condition ? 0 : 1;
}

/**
 * Prints one test case result.
 * @param fail Nonzero when the case failed.
 * @param name Case name.
 * @param detail Case description.
 * @return void.
 */
static void print_case(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", case_current, case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Releases all queued transport chunks.
 * @param wire Transport queue.
 * @return void.
 */
static void wire_clear(wire_t *wire) {
    chunk_t *chunk;

    chunk = wire->head;
    while (chunk != NULL) {
        chunk_t *next;

        next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    memset(wire, 0, sizeof(*wire));
}

/**
 * Appends bytes to one transport queue.
 * @param wire Transport queue.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return Zero on success, otherwise one.
 */
static int wire_append(wire_t *wire, const void *data, size_t size) {
    chunk_t *chunk;

    if (size == 0U) return 0;
    chunk = (chunk_t *)calloc(1, sizeof(*chunk));
    if (chunk == NULL) return 1;
    chunk->data = (unsigned char *)malloc(size);
    if (chunk->data == NULL) {
        free(chunk);
        return 1;
    }
    memcpy(chunk->data, data, size);
    chunk->size = size;
    if (wire->tail != NULL) wire->tail->next = chunk;
    else wire->head = chunk;
    wire->tail = chunk;
    return 0;
}

/**
 * Appends plaintext to one test side.
 * @param side Test side.
 * @param data Input bytes.
 * @param size Input byte count.
 * @return Zero on success, otherwise one.
 */
static int side_append(side_t *side, const void *data, size_t size) {
    size_t need;
    size_t capacity;
    unsigned char *resized;

    if (size == 0U) return 0;
    if (side->size > SIZE_MAX - size) return 1;
    need = side->size + size;
    if (need > side->capacity) {
        capacity = side->capacity != 0U ? side->capacity : 256U;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = need;
                break;
            }
            capacity *= 2U;
        }
        resized = (unsigned char *)realloc(side->plain, capacity);
        if (resized == NULL) return 1;
        side->plain = resized;
        side->capacity = capacity;
    }
    memcpy(side->plain + side->size, data, size);
    side->size += size;
    return 0;
}

/**
 * Collects authenticated plaintext emitted by tls.
 * @param data Plaintext bytes.
 * @param size Plaintext byte count.
 * @param userdata Test side.
 * @return void.
 */
static void plaintext(const void *data, size_t size, void *userdata) {
    side_t *side;

    side = (side_t *)userdata;
    if (side_append(side, data, size) != 0) side->failed = 1;
}

/**
 * Collects TLS wire bytes emitted by tls.
 * @param data TLS wire bytes.
 * @param size TLS wire byte count.
 * @param userdata Test side.
 * @return void.
 */
static void ciphertext(const void *data, size_t size, void *userdata) {
    side_t *side;

    side = (side_t *)userdata;
    if (wire_append(side->wire, data, size) != 0) side->failed = 1;
}

/**
 * Initializes one in-memory client/server pair.
 * @param pair Pair state.
 * @return void.
 */
static void pair_init(pair_t *pair) {
    memset(pair, 0, sizeof(*pair));
    pair->client_side.wire = &pair->c2s;
    pair->server_side.wire = &pair->s2c;
}

/**
 * Closes one in-memory client/server pair.
 * @param pair Pair state.
 * @return void.
 */
static void pair_close(pair_t *pair) {
    kc_tls_close(pair->client);
    kc_tls_close(pair->server);
    pair->client = NULL;
    pair->server = NULL;
    wire_clear(&pair->c2s);
    wire_clear(&pair->s2c);
    free(pair->client_side.plain);
    free(pair->server_side.plain);
}

/**
 * Opens the server side of one test pair.
 * @param pair Pair state.
 * @return Public tls status code.
 */
static int open_server(pair_t *pair) {
    kc_tls_options_t options;

    memset(&options, 0, sizeof(options));
    options.role = KC_TLS_SERVER;
    options.certificate = test_cert;
    options.certificate_size = sizeof(test_cert) - 1U;
    options.private_key = test_key;
    options.private_key_size = sizeof(test_key) - 1U;
    return kc_tls_open(
        &pair->server,
        &options,
        plaintext,
        ciphertext,
        &pair->server_side
    );
}

/**
 * Opens the client side of one test pair.
 * @param pair Pair state.
 * @param host Expected server hostname.
 * @return Public tls status code.
 */
static int open_client(pair_t *pair, const char *host) {
    kc_tls_options_t options;

    memset(&options, 0, sizeof(options));
    options.role = KC_TLS_CLIENT;
    options.host = host;
    options.ca = test_ca;
    options.ca_size = sizeof(test_ca) - 1U;
    return kc_tls_open(
        &pair->client,
        &options,
        plaintext,
        ciphertext,
        &pair->client_side
    );
}

/**
 * Delivers one queued TLS wire fragment.
 * @param wire Transport queue.
 * @param tls Destination TLS session.
 * @param fragment Maximum fragment size, or zero for all bytes.
 * @return Public tls status code, or 1000 when the queue is empty.
 */
static int deliver(wire_t *wire, kc_tls_t *tls, size_t fragment) {
    chunk_t *chunk;
    size_t available;
    size_t size;
    int result;

    if (wire->head == NULL) return 1000;
    chunk = wire->head;
    available = chunk->size - chunk->offset;
    size = available;
    if (fragment != 0U && size > fragment) size = fragment;
    result = kc_tls_receive(tls, chunk->data + chunk->offset, size);
    chunk->offset += size;
    if (chunk->offset == chunk->size) {
        wire->head = chunk->next;
        if (wire->head == NULL) wire->tail = NULL;
        free(chunk->data);
        free(chunk);
    }
    return result;
}

/**
 * Pumps queued TLS bytes between both sides.
 * @param pair Pair state.
 * @param fragment Maximum delivery fragment size.
 * @return Public tls status code.
 */
static int pump(pair_t *pair, size_t fragment) {
    size_t steps;

    for (steps = 0U; steps < TEST_MAX_STEPS; steps++) {
        int progress;

        progress = 0;
        if (pair->c2s.head != NULL && pair->server != NULL) {
            int result;

            result = deliver(&pair->c2s, pair->server, fragment);
            if (result != KC_TLS_OK) return result;
            progress = 1;
        }
        if (pair->s2c.head != NULL && pair->client != NULL) {
            int result;

            result = deliver(&pair->s2c, pair->client, fragment);
            if (result != KC_TLS_OK) return result;
            progress = 1;
        }
        if (!progress) return KC_TLS_OK;
    }
    return KC_TLS_ETLS;
}

/**
 * Validates public argument handling.
 * @return One when the case failed, otherwise zero.
 */
static int case_validation(void) {
    kc_tls_options_t options;
    kc_tls_t *tls;
    int fail;

    memset(&options, 0, sizeof(options));
    tls = (kc_tls_t *)1;
    fail = 0;
    fail += expect(
        "open NULL output",
        kc_tls_open(
            NULL,
            &options,
            plaintext,
            ciphertext,
            NULL
        ) == KC_TLS_EINVAL
    );
    fail += expect(
        "open NULL options clears output",
        kc_tls_open(
            &tls,
            NULL,
            plaintext,
            ciphertext,
            NULL
        ) == KC_TLS_EINVAL && tls == NULL
    );
    options.role = KC_TLS_CLIENT;
    options.host = "";
    fail += expect(
        "client requires host",
        kc_tls_open(
            &tls,
            &options,
            plaintext,
            ciphertext,
            NULL
        ) == KC_TLS_EINVAL
    );
    memset(&options, 0, sizeof(options));
    options.role = KC_TLS_SERVER;
    fail += expect(
        "server requires credentials",
        kc_tls_open(
            &tls,
            &options,
            plaintext,
            ciphertext,
            NULL
        ) == KC_TLS_EINVAL
    );
    fail += expect(
        "send NULL handle",
        kc_tls_send(NULL, "x", 1U) == KC_TLS_EINVAL
    );
    fail += expect(
        "receive NULL handle",
        kc_tls_receive(NULL, "x", 1U) == KC_TLS_EINVAL
    );
    kc_tls_close(NULL);
    print_case(
        fail,
        "validation",
        "covers roles, credentials, callbacks, and handles"
    );
    return fail != 0;
}

/**
 * Validates client/server exchange through memory transport.
 * @return One when the case failed, otherwise zero.
 */
static int case_exchange(void) {
    pair_t pair;
    unsigned char *request;
    unsigned char *response;
    size_t i;
    int fail;
    int result;

    pair_init(&pair);
    request = (unsigned char *)malloc(TEST_LARGE_SIZE);
    response = (unsigned char *)malloc(TEST_LARGE_SIZE);
    if (request == NULL || response == NULL) {
        free(request);
        free(response);
        pair_close(&pair);
        print_case(1, "exchange", "allocation failure");
        return 1;
    }
    for (i = 0U; i < TEST_LARGE_SIZE; i++) {
        request[i] = (unsigned char)((i * 19U + 3U) & 255U);
        response[i] = (unsigned char)((i * 37U + 11U) & 255U);
    }

    fail = 0;
    fail += expect("server open", open_server(&pair) == KC_TLS_OK);
    fail += expect("client open", open_client(&pair, TEST_HOST) == KC_TLS_OK);
    fail += expect("client hello produced", pair.c2s.head != NULL);
    fail += expect(
        "queued A",
        kc_tls_send(pair.client, "A", 1U) == KC_TLS_OK
    );
    fail += expect(
        "queued B",
        kc_tls_send(pair.client, "B", 1U) == KC_TLS_OK
    );
    fail += expect(
        "queued large payload",
        kc_tls_send(pair.client, request, TEST_LARGE_SIZE) == KC_TLS_OK
    );

    result = pump(&pair, 7U);
    fail += expect("fragmented handshake/request", result == KC_TLS_OK);
    fail += expect(
        "server exact plaintext size",
        pair.server_side.size == TEST_LARGE_SIZE + 2U
    );
    if (pair.server_side.size == TEST_LARGE_SIZE + 2U) {
        fail += expect(
            "server plaintext order",
            pair.server_side.plain[0] == 'A' &&
            pair.server_side.plain[1] == 'B' &&
            memcmp(
                pair.server_side.plain + 2U,
                request,
                TEST_LARGE_SIZE
            ) == 0
        );
    }

    fail += expect(
        "server response",
        kc_tls_send(pair.server, response, TEST_LARGE_SIZE) == KC_TLS_OK
    );
    result = pump(&pair, 13U);
    fail += expect("fragmented response", result == KC_TLS_OK);
    fail += expect(
        "client exact response",
        pair.client_side.size == TEST_LARGE_SIZE &&
        memcmp(pair.client_side.plain, response, TEST_LARGE_SIZE) == 0
    );
    fail += expect(
        "callback allocations",
        !pair.client_side.failed && !pair.server_side.failed
    );

    kc_tls_close(pair.server);
    pair.server = NULL;
    result = KC_TLS_OK;
    while (pair.s2c.head != NULL && result == KC_TLS_OK) {
        result = deliver(&pair.s2c, pair.client, 5U);
    }
    fail += expect("peer close_notify", result == KC_TLS_CLOSED);

    wire_clear(&pair.c2s);
    free(request);
    free(response);
    pair_close(&pair);
    print_case(
        fail,
        "exchange",
        "covers client/server TLS, queued sends, fragmentation, binary I/O"
    );
    return fail != 0;
}

/**
 * Validates hostname verification before plaintext delivery.
 * @return One when the case failed, otherwise zero.
 */
static int case_verification(void) {
    pair_t pair;
    int fail;
    int result;

    pair_init(&pair);
    fail = 0;
    fail += expect("server open", open_server(&pair) == KC_TLS_OK);
    fail += expect(
        "wrong-host client open",
        open_client(&pair, "wrong.example") == KC_TLS_OK
    );
    fail += expect(
        "queue before verification",
        kc_tls_send(pair.client, "secret", 6U) == KC_TLS_OK
    );
    result = pump(&pair, 3U);
    fail += expect(
        "hostname verification failure",
        result == KC_TLS_EVERIFY
    );
    fail += expect(
        "unverified plaintext withheld",
        pair.server_side.size == 0U
    );
    pair_close(&pair);
    print_case(
        fail,
        "verification",
        "rejects hostname mismatch before application plaintext"
    );
    return fail != 0;
}

/**
 * Validates status strings and generated version metadata.
 * @return One when the case failed, otherwise zero.
 */
static int case_metadata(void) {
    static const int codes[] = {
        KC_TLS_OK,
        KC_TLS_CLOSED,
        KC_TLS_EINVAL,
        KC_TLS_ETLS,
        KC_TLS_EVERIFY,
        KC_TLS_ENOMEM
    };
    size_t i;
    int fail;

    fail = 0;
    for (i = 0U; i < sizeof(codes) / sizeof(codes[0]); i++) {
        fail += expect(
            "status string",
            kc_tls_strerror(codes[i])[0] != '\0'
        );
    }
    fail += expect(
        "unknown status",
        strcmp(kc_tls_strerror(9999), "unknown tls status") == 0
    );
    fail += expect("nonzero version", kc_tls_version() != 0U);
    print_case(
        fail,
        "metadata",
        "covers public statuses and generated version"
    );
    return fail != 0;
}

/**
 * Runs all libtls contract tests.
 * @param argc Argument count.
 * @param argv Argument vector; argv[1] must be all.
 * @return Zero when all cases pass, otherwise nonzero.
 */
int main(int argc, char **argv) {
    int failed;

    if (argc != 2 || strcmp(argv[1], "all") != 0) return 2;
    case_total = 4;
    case_current = 0;
    failed = 0;
    case_current++;
    failed += case_validation();
    case_current++;
    failed += case_exchange();
    case_current++;
    failed += case_verification();
    case_current++;
    failed += case_metadata();
    printf("%d passed, %d failed\n\n", case_total - failed, failed);
    return failed == 0 ? 0 : 1;
}
