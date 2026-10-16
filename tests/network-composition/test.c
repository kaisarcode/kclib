/**
 * test.c - Network building-block composition smoke tests.
 * Summary: Verifies nets, netl, tlsio, and http compose as independent consumers.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhttp.h"
#include "libnetl.h"
#include "libnets.h"
#include "libtlsio.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

#define TEST_HOST "localhost"
#define NETWORK_HOST "127.0.0.1"
#define WAIT_STEPS 500
#define WAIT_MS 10UL
#define TLS_FRAGMENT 17U
#define RAW_CLOSE_TOKEN "remote-close"

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

typedef enum {
    SERVER_RAW = 1,
    SERVER_HTTP = 2
} server_mode_t;

typedef enum {
    CLIENT_RAW = 1,
    CLIENT_HTTP = 2
} client_mode_t;

typedef struct server server_t;
typedef struct server_session server_session_t;

struct server_session {
    server_t *server;
    kc_netl_peer_t *peer;
    kc_tlsio_t *tls;
    kc_http_parser_t *http;
    unsigned char raw[256];
    size_t raw_size;
    int peer_closing;
    server_session_t *next;
};

struct server {
    server_mode_t mode;
    kc_netl_t *listener;
    server_session_t *sessions;
    atomic_int session_total;
    atomic_int session_active;
    atomic_int disconnect_count;
    atomic_int request_count;
    atomic_int error;
};

typedef struct {
    client_mode_t mode;
    kc_nets_t *network;
    kc_tlsio_t *tls;
    kc_http_parser_t *http;
    unsigned char plain[512];
    atomic_int plain_size;
    atomic_int disconnect_count;
    atomic_int tls_closed;
    atomic_int response_complete;
    atomic_int closing;
    atomic_int error;
    int response_status;
    unsigned char response_body[128];
    size_t response_body_size;
} client_t;

static void sleep_ms(unsigned long milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;

    delay.tv_sec = (time_t)(milliseconds / 1000UL);
    delay.tv_nsec = (long)((milliseconds % 1000UL) * 1000000UL);
    (void)nanosleep(&delay, NULL);
#endif
}

static int wait_at_least(atomic_int *value, int expected) {
    int index;

    for (index = 0; index < WAIT_STEPS; index++) {
        if (atomic_load(value) >= expected) return 0;
        sleep_ms(WAIT_MS);
    }
    return 1;
}

static int wait_equal(atomic_int *value, int expected) {
    int index;

    for (index = 0; index < WAIT_STEPS; index++) {
        if (atomic_load(value) == expected) return 0;
        sleep_ms(WAIT_MS);
    }
    return 1;
}

static int expect_true(const char *label, int condition) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", label);
    return 1;
}

static int bytes_contain(
    const unsigned char *data,
    size_t size,
    const unsigned char *needle,
    size_t needle_size
) {
    size_t index;

    if (needle_size == 0U || needle_size > size) return 0;
    for (index = 0U; index + needle_size <= size; index++) {
        if (memcmp(data + index, needle, needle_size) == 0) return 1;
    }
    return 0;
}

static server_session_t *server_find_session(
    server_t *server,
    kc_netl_peer_t *peer
) {
    server_session_t *session;

    for (session = server->sessions; session != NULL; session = session->next) {
        if (session->peer == peer) return session;
    }
    return NULL;
}

static void server_ciphertext(
    const void *data,
    size_t data_size,
    void *userdata
) {
    server_session_t *session;
    const unsigned char *bytes;
    size_t offset;

    session = (server_session_t *)userdata;
    bytes = (const unsigned char *)data;
    for (offset = 0U; offset < data_size;) {
        size_t size;
        int result;

        size = data_size - offset;
        if (size > TLS_FRAGMENT) size = TLS_FRAGMENT;
        result = kc_netl_respond(session->peer, bytes + offset, size);
        if (result != KC_NETL_OK && !session->peer_closing) {
            atomic_store(&session->server->error, 1);
            return;
        }
        offset += size;
    }
}

static void server_http_request(
    const kc_http_request_t *request,
    void *userdata
) {
    static const unsigned char body[] = "composed";
    server_session_t *session;
    kc_http_response_build_t response;
    void *wire;
    size_t wire_size;
    int status;
    int result;

    session = (server_session_t *)userdata;
    if (
        request->method == NULL ||
        strcmp(request->method, "GET") != 0 ||
        request->target == NULL ||
        strcmp(request->target, "/composition") != 0
    ) {
        atomic_store(&session->server->error, 1);
        return;
    }

    memset(&response, 0, sizeof(response));
    status = 200;
    response.status = &status;
    response.body = body;
    response.body_size = sizeof(body) - 1U;

    wire = NULL;
    wire_size = 0U;
    result = kc_http_response(&response, &wire, &wire_size);
    if (result != KC_HTTP_OK) {
        atomic_store(&session->server->error, 1);
        return;
    }
    result = kc_tlsio_send(session->tls, wire, wire_size);
    kc_http_free(wire);
    if (result != KC_TLSIO_OK) {
        atomic_store(&session->server->error, 1);
        return;
    }
    atomic_fetch_add(&session->server->request_count, 1);
}

static void server_http_error(int status, void *userdata) {
    server_session_t *session;

    (void)status;
    session = (server_session_t *)userdata;
    atomic_store(&session->server->error, 1);
}

static void server_plaintext(
    const void *data,
    size_t data_size,
    void *userdata
) {
    server_session_t *session;

    session = (server_session_t *)userdata;
    if (session->server->mode == SERVER_HTTP) {
        if (
            kc_http_parser_write(session->http, data, data_size) != KC_HTTP_OK
        ) {
            atomic_store(&session->server->error, 1);
        }
        return;
    }

    if (kc_tlsio_send(session->tls, data, data_size) != KC_TLSIO_OK) {
        atomic_store(&session->server->error, 1);
        return;
    }
    if (session->raw_size + data_size > sizeof(session->raw)) {
        atomic_store(&session->server->error, 1);
        return;
    }
    memcpy(session->raw + session->raw_size, data, data_size);
    session->raw_size += data_size;

    if (
        !session->peer_closing &&
        bytes_contain(
            session->raw,
            session->raw_size,
            (const unsigned char *)RAW_CLOSE_TOKEN,
            sizeof(RAW_CLOSE_TOKEN) - 1U
        )
    ) {
        session->peer_closing = 1;
        kc_netl_peer_close(session->peer);
    }
}

static server_session_t *server_open_session(
    server_t *server,
    kc_netl_peer_t *peer
) {
    server_session_t *session;
    kc_tlsio_options_t options;
    int result;

    session = (server_session_t *)calloc(1, sizeof(*session));
    if (session == NULL) {
        atomic_store(&server->error, 1);
        return NULL;
    }
    session->server = server;
    session->peer = peer;

    if (server->mode == SERVER_HTTP) {
        result = kc_http_parser_open(
            &session->http,
            server_http_request,
            NULL,
            server_http_error,
            session
        );
        if (result != KC_HTTP_OK) {
            free(session);
            atomic_store(&server->error, 1);
            return NULL;
        }
    }

    memset(&options, 0, sizeof(options));
    options.role = KC_TLSIO_SERVER;
    options.certificate = test_cert;
    options.certificate_size = sizeof(test_cert) - 1U;
    options.private_key = test_key;
    options.private_key_size = sizeof(test_key) - 1U;
    result = kc_tlsio_open(
        &session->tls,
        &options,
        server_plaintext,
        server_ciphertext,
        session
    );
    if (result != KC_TLSIO_OK) {
        if (session->http != NULL) kc_http_parser_close(session->http);
        free(session);
        atomic_store(&server->error, 1);
        return NULL;
    }

    session->next = server->sessions;
    server->sessions = session;
    atomic_fetch_add(&server->session_total, 1);
    atomic_fetch_add(&server->session_active, 1);
    return session;
}

static void server_receive(
    const kc_netl_input_t *input,
    void *userdata
) {
    server_t *server;
    server_session_t *session;
    const unsigned char *data;
    size_t offset;

    server = (server_t *)userdata;
    session = server_find_session(server, input->peer);
    if (session == NULL) {
        session = server_open_session(server, input->peer);
        if (session == NULL) {
            kc_netl_peer_close(input->peer);
            return;
        }
    }

    data = (const unsigned char *)input->data;
    for (offset = 0U; offset < input->data_size;) {
        size_t size;
        int result;

        size = input->data_size - offset;
        if (size > TLS_FRAGMENT) size = TLS_FRAGMENT;
        result = kc_tlsio_receive(session->tls, data + offset, size);
        offset += size;
        if (result == KC_TLSIO_CLOSED) {
            session->peer_closing = 1;
            kc_tlsio_close(session->tls);
            session->tls = NULL;
            kc_netl_peer_close(session->peer);
            return;
        }
        if (result != KC_TLSIO_OK) {
            atomic_store(&server->error, 1);
            session->peer_closing = 1;
            kc_netl_peer_close(session->peer);
            return;
        }
        if (session->peer_closing) return;
    }
}

static void server_disconnect(kc_netl_peer_t *peer, void *userdata) {
    server_t *server;
    server_session_t **link;
    server_session_t *session;

    server = (server_t *)userdata;
    link = &server->sessions;
    while (*link != NULL && (*link)->peer != peer) link = &(*link)->next;
    if (*link == NULL) {
        atomic_store(&server->error, 1);
        return;
    }

    session = *link;
    *link = session->next;
    session->peer_closing = 1;
    if (session->tls != NULL) kc_tlsio_close(session->tls);
    if (session->http != NULL) kc_http_parser_close(session->http);
    free(session);
    atomic_fetch_sub(&server->session_active, 1);
    atomic_fetch_add(&server->disconnect_count, 1);
}

static void server_error(int status, void *userdata) {
    server_t *server;

    (void)status;
    server = (server_t *)userdata;
    atomic_store(&server->error, 1);
}

static int server_start(server_t *server, server_mode_t mode) {
    kc_netl_options_t options;

    memset(server, 0, sizeof(*server));
    server->mode = mode;
    atomic_init(&server->session_total, 0);
    atomic_init(&server->session_active, 0);
    atomic_init(&server->disconnect_count, 0);
    atomic_init(&server->request_count, 0);
    atomic_init(&server->error, 0);

    memset(&options, 0, sizeof(options));
    options.host = NETWORK_HOST;
    options.port = 0;
    options.protocol = KC_NETL_TCP;
    return kc_netl_open(
        &server->listener,
        &options,
        server_receive,
        server_disconnect,
        server_error,
        server
    );
}

static void server_stop(server_t *server) {
    kc_netl_close(server->listener);
    server->listener = NULL;
}

static void client_http_response(
    const kc_http_response_t *response,
    void *userdata
) {
    client_t *client;

    client = (client_t *)userdata;
    client->response_status = response->status;
    if (response->body_size > sizeof(client->response_body)) {
        atomic_store(&client->error, 1);
        return;
    }
    if (response->body_size != 0U) {
        memcpy(client->response_body, response->body, response->body_size);
    }
    client->response_body_size = response->body_size;
    atomic_store(&client->response_complete, 1);
}

static void client_http_error(int status, void *userdata) {
    client_t *client;

    (void)status;
    client = (client_t *)userdata;
    atomic_store(&client->error, 1);
}

static void client_plaintext(
    const void *data,
    size_t data_size,
    void *userdata
) {
    client_t *client;
    int current;

    client = (client_t *)userdata;
    if (client->mode == CLIENT_HTTP) {
        if (kc_http_parser_write(client->http, data, data_size) != KC_HTTP_OK) {
            atomic_store(&client->error, 1);
        }
        return;
    }

    current = atomic_load(&client->plain_size);
    if (
        data_size > sizeof(client->plain) ||
        (size_t)current > sizeof(client->plain) - data_size
    ) {
        atomic_store(&client->error, 1);
        return;
    }
    memcpy(client->plain + current, data, data_size);
    atomic_store(&client->plain_size, current + (int)data_size);
}

static void client_ciphertext(
    const void *data,
    size_t data_size,
    void *userdata
) {
    client_t *client;
    const unsigned char *bytes;
    size_t offset;

    client = (client_t *)userdata;
    bytes = (const unsigned char *)data;
    for (offset = 0U; offset < data_size;) {
        size_t size;
        int result;

        size = data_size - offset;
        if (size > TLS_FRAGMENT) size = TLS_FRAGMENT;
        result = kc_nets_send(client->network, bytes + offset, size);
        if (result != KC_NETS_OK) {
            if (!atomic_load(&client->closing)) {
                atomic_store(&client->error, 1);
            }
            return;
        }
        offset += size;
    }
}

static void client_network_receive(
    const void *data,
    size_t data_size,
    void *userdata
) {
    client_t *client;
    const unsigned char *bytes;
    size_t offset;

    client = (client_t *)userdata;
    if (atomic_load(&client->closing) || client->tls == NULL) return;

    bytes = (const unsigned char *)data;
    for (offset = 0U; offset < data_size;) {
        size_t size;
        int result;

        size = data_size - offset;
        if (size > TLS_FRAGMENT) size = TLS_FRAGMENT;
        result = kc_tlsio_receive(client->tls, bytes + offset, size);
        offset += size;
        if (result == KC_TLSIO_CLOSED) {
            atomic_store(&client->tls_closed, 1);
            return;
        }
        if (result != KC_TLSIO_OK) {
            atomic_store(&client->error, 1);
            return;
        }
    }
}

static void client_disconnect(void *userdata) {
    client_t *client;

    client = (client_t *)userdata;
    atomic_fetch_add(&client->disconnect_count, 1);
}

static void client_network_error(int status, void *userdata) {
    client_t *client;

    (void)status;
    client = (client_t *)userdata;
    if (!atomic_load(&client->closing)) atomic_store(&client->error, 1);
}

static int client_open(
    client_t *client,
    unsigned short port,
    client_mode_t mode
) {
    kc_nets_options_t network_options;
    kc_tlsio_options_t tls_options;
    int result;

    memset(client, 0, sizeof(*client));
    client->mode = mode;
    atomic_init(&client->plain_size, 0);
    atomic_init(&client->disconnect_count, 0);
    atomic_init(&client->tls_closed, 0);
    atomic_init(&client->response_complete, 0);
    atomic_init(&client->closing, 0);
    atomic_init(&client->error, 0);

    if (mode == CLIENT_HTTP) {
        result = kc_http_parser_open(
            &client->http,
            NULL,
            client_http_response,
            client_http_error,
            client
        );
        if (result != KC_HTTP_OK) return result;
    }

    memset(&network_options, 0, sizeof(network_options));
    network_options.host = NETWORK_HOST;
    network_options.port = port;
    network_options.protocol = KC_NETS_TCP;
    result = kc_nets_open(
        &client->network,
        &network_options,
        client_network_receive,
        client_disconnect,
        client_network_error,
        client
    );
    if (result != KC_NETS_OK) {
        if (client->http != NULL) kc_http_parser_close(client->http);
        client->http = NULL;
        return result;
    }

    memset(&tls_options, 0, sizeof(tls_options));
    tls_options.role = KC_TLSIO_CLIENT;
    tls_options.host = TEST_HOST;
    tls_options.ca = test_ca;
    tls_options.ca_size = sizeof(test_ca) - 1U;
    result = kc_tlsio_open(
        &client->tls,
        &tls_options,
        client_plaintext,
        client_ciphertext,
        client
    );
    if (result != KC_TLSIO_OK) {
        kc_nets_close(client->network);
        client->network = NULL;
        kc_http_parser_close(client->http);
        client->http = NULL;
        return result;
    }
    return KC_TLSIO_OK;
}

static void client_finish(client_t *client, int clean) {
    if (client->tls != NULL) {
        atomic_store(&client->closing, 1);
        kc_tlsio_close(client->tls);
        client->tls = NULL;
        if (clean) (void)wait_at_least(&client->disconnect_count, 1);
    }
    kc_nets_close(client->network);
    client->network = NULL;
    if (client->http != NULL) {
        kc_http_parser_close(client->http);
        client->http = NULL;
    }
}

static int raw_composition_case(void) {
    static const unsigned char first_a[] = "alpha-";
    static const unsigned char second_a[] = "one";
    static const unsigned char message_b[] = {
        'b', 'e', 't', 'a', '-', 0x00, '-', 't', 'w', 'o'
    };
    static const unsigned char expected_a[] = "alpha-one";
    server_t server;
    client_t client_a;
    client_t client_b;
    client_t remote;
    unsigned short port;
    int fail;

    fail = 0;
    if (server_start(&server, SERVER_RAW) != KC_NETL_OK) return 1;
    port = kc_netl_port(server.listener);
    fail += expect_true("raw server bound an ephemeral port", port != 0U);

    if (client_open(&client_a, port, CLIENT_RAW) != KC_TLSIO_OK) {
        server_stop(&server);
        return 1;
    }
    if (client_open(&client_b, port, CLIENT_RAW) != KC_TLSIO_OK) {
        client_finish(&client_a, 0);
        server_stop(&server);
        return 1;
    }

    fail += expect_true(
        "client A first send",
        kc_tlsio_send(
            client_a.tls,
            first_a,
            sizeof(first_a) - 1U
        ) == KC_TLSIO_OK
    );
    fail += expect_true(
        "client A second send",
        kc_tlsio_send(
            client_a.tls,
            second_a,
            sizeof(second_a) - 1U
        ) == KC_TLSIO_OK
    );
    fail += expect_true(
        "client B binary send",
        kc_tlsio_send(client_b.tls, message_b, sizeof(message_b)) == KC_TLSIO_OK
    );

    fail += expect_true(
        "client A receives echoed plaintext",
        wait_at_least(&client_a.plain_size, (int)(sizeof(expected_a) - 1U)) == 0
    );
    fail += expect_true(
        "client B receives isolated binary plaintext",
        wait_at_least(&client_b.plain_size, (int)sizeof(message_b)) == 0
    );
    fail += expect_true(
        "client A plaintext matches",
        memcmp(client_a.plain, expected_a, sizeof(expected_a) - 1U) == 0
    );
    fail += expect_true(
        "client B plaintext matches",
        memcmp(client_b.plain, message_b, sizeof(message_b)) == 0
    );
    fail += expect_true(
        "server created one TLS session per peer",
        wait_at_least(&server.session_total, 2) == 0
    );

    if (client_open(&remote, port, CLIENT_RAW) != KC_TLSIO_OK) {
        client_finish(&client_a, 0);
        client_finish(&client_b, 0);
        server_stop(&server);
        return 1;
    }
    fail += expect_true(
        "remote-close payload send",
        kc_tlsio_send(
            remote.tls,
            RAW_CLOSE_TOKEN,
            sizeof(RAW_CLOSE_TOKEN) - 1U
        ) == KC_TLSIO_OK
    );
    fail += expect_true(
        "remote transport disconnect propagates",
        wait_at_least(&remote.disconnect_count, 1) == 0
    );
    client_finish(&remote, 0);

    client_finish(&client_a, 1);
    client_finish(&client_b, 1);
    fail += expect_true(
        "all raw peers cleaned up",
        wait_equal(&server.session_active, 0) == 0
    );
    fail += expect_true(
        "clean client A transport closed",
        atomic_load(&client_a.disconnect_count) >= 1
    );
    fail += expect_true(
        "clean client B transport closed",
        atomic_load(&client_b.disconnect_count) >= 1
    );
    fail += expect_true("raw server has no composition errors", !atomic_load(&server.error));
    fail += expect_true("client A has no errors", !atomic_load(&client_a.error));
    fail += expect_true("client B has no errors", !atomic_load(&client_b.error));
    server_stop(&server);

    printf(
        "[%s] nets + tlsio / netl + tlsio: persistent peers, fragmented TLS, "
        "multiple sends, binary isolation, remote disconnect, clean close\n",
        fail == 0 ? "PASS" : "FAIL"
    );
    return fail != 0;
}

static int http_composition_case(void) {
    static const kc_http_field_t request_headers[] = {
        {"Host", TEST_HOST}
    };
    static const unsigned char expected_body[] = "composed";
    server_t server;
    client_t client;
    kc_http_request_build_t request;
    void *wire;
    size_t wire_size;
    unsigned short port;
    int result;
    int fail;

    fail = 0;
    if (server_start(&server, SERVER_HTTP) != KC_NETL_OK) return 1;
    port = kc_netl_port(server.listener);
    if (client_open(&client, port, CLIENT_HTTP) != KC_TLSIO_OK) {
        server_stop(&server);
        return 1;
    }

    memset(&request, 0, sizeof(request));
    request.method = "GET";
    request.target = "/composition";
    request.headers = request_headers;
    request.header_count = sizeof(request_headers) / sizeof(request_headers[0]);

    wire = NULL;
    wire_size = 0U;
    result = kc_http_request(&request, &wire, &wire_size);
    fail += expect_true("HTTP request builds", result == KC_HTTP_OK);
    if (result == KC_HTTP_OK) {
        fail += expect_true(
            "HTTP request crosses TLS and nets",
            kc_tlsio_send(client.tls, wire, wire_size) == KC_TLSIO_OK
        );
        kc_http_free(wire);
    }

    fail += expect_true(
        "HTTP response parsed after nets and TLS",
        wait_at_least(&client.response_complete, 1) == 0
    );
    fail += expect_true(
        "server parsed one HTTP request",
        wait_at_least(&server.request_count, 1) == 0
    );
    fail += expect_true("HTTP response status", client.response_status == 200);
    fail += expect_true(
        "HTTP response body",
        client.response_body_size == sizeof(expected_body) - 1U &&
        memcmp(
            client.response_body,
            expected_body,
            sizeof(expected_body) - 1U
        ) == 0
    );
    fail += expect_true("HTTP server has no composition errors", !atomic_load(&server.error));
    fail += expect_true("HTTP client has no composition errors", !atomic_load(&client.error));

    client_finish(&client, 1);
    fail += expect_true(
        "HTTP TLS peer cleaned up",
        wait_equal(&server.session_active, 0) == 0
    );
    fail += expect_true(
        "HTTP clean transport close propagated",
        atomic_load(&client.disconnect_count) >= 1
    );
    server_stop(&server);

    printf(
        "[%s] http + tlsio + nets: request and response stay consumer-composed\n",
        fail == 0 ? "PASS" : "FAIL"
    );
    return fail != 0;
}

int main(void) {
    int failed;

    failed = 0;
    failed += raw_composition_case();
    failed += http_composition_case();

    printf(
        "%s: network building-block composition smoke tests\n",
        failed == 0 ? "PASS" : "FAIL"
    );
    return failed == 0 ? 0 : 1;
}
