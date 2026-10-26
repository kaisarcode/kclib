/**
 * libstun.h - STUN Binding client and server.
 * Summary: Runs authenticated Binding transactions on owned or borrowed UDP.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */
#ifndef KC_STUN_H
#define KC_STUN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KC_STUN_OK 0
#define KC_STUN_PENDING 1
#define KC_STUN_EINVAL -1
#define KC_STUN_ENET -2
#define KC_STUN_ETIMEOUT -3
#define KC_STUN_EPROTO -4
#define KC_STUN_ENOMEM -5
#define KC_STUN_ECLOSED -6
#define KC_STUN_EAUTH -7
#define KC_STUN_EREJECTED -8
#define KC_STUN_BINDING 0x0001u
#define KC_STUN_USERNAME 0x0006u
#define KC_STUN_MESSAGE_INTEGRITY 0x0008u
#define KC_STUN_ERROR_CODE 0x0009u
#define KC_STUN_UNKNOWN_ATTRIBUTES 0x000au
#define KC_STUN_REALM 0x0014u
#define KC_STUN_NONCE 0x0015u
#define KC_STUN_XOR_MAPPED_ADDRESS 0x0020u
#define KC_STUN_MESSAGE_INTEGRITY_SHA256 0x001cu
#define KC_STUN_FINGERPRINT 0x8028u
#define KC_STUN_HMAC_SHA1 1u
#define KC_STUN_HMAC_SHA256 2u
#define KC_STUN_HMAC_BOTH 3u

typedef struct kc_stun_client kc_stun_client_t;
typedef struct kc_stun_server kc_stun_server_t;
typedef struct { const char *host; unsigned short port; } kc_stun_endpoint_t;
typedef struct { unsigned short type; const void *data; size_t size; } kc_stun_attribute_t;
typedef struct { char *host; unsigned short port; void *data; size_t data_size; } kc_stun_datagram_t;

typedef int (*kc_stun_udp_send_fn)(void *context, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);
typedef struct { void *context; kc_stun_udp_send_fn send; } kc_stun_transport_t;

typedef struct {
    const char *username;
    const void *key;
    size_t key_size;
    unsigned int integrity; /* 0 auto-selects BOTH with key; otherwise SHA1/SHA256/BOTH */
} kc_stun_credentials_t;

typedef struct {
    kc_stun_endpoint_t peer;
    const char *host;
    unsigned short port;
    uint64_t timeout_ms;
    const kc_stun_transport_t *transport; /* Borrowed: send only, never closed. */
    kc_stun_credentials_t credentials;
    const kc_stun_attribute_t *attributes;
    size_t attribute_count;
} kc_stun_client_options_t;

typedef int (*kc_stun_credentials_fn)(void *context, const char *username, kc_stun_credentials_t *credentials);
/* Return 0 to accept; otherwise a STUN error code (e.g. 487). */
typedef unsigned short (*kc_stun_binding_fn)(void *context, const kc_stun_endpoint_t *peer, const kc_stun_attribute_t *attributes, size_t count);

typedef struct {
    const char *host;
    unsigned short port;
    const kc_stun_transport_t *transport; /* Borrowed; caller dispatches datagrams. */
    kc_stun_credentials_fn credentials;
    kc_stun_binding_fn binding;
    void *context;
} kc_stun_server_options_t;

/** Begins one authenticated or anonymous Binding transaction. Client is pending until poll/receive completes. */
int kc_stun_client(kc_stun_client_t **out, const kc_stun_client_options_t *options);
/** Drives retransmissions and, for owned UDP, receives responses; returns OK, PENDING or an error. */
int kc_stun_client_poll(kc_stun_client_t *client, uint64_t wait_ms);
/** Delivers an inbound UDP datagram to a client on borrowed transport; ignores unrelated packets. */
int kc_stun_client_receive(kc_stun_client_t *client, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);
/** Local endpoint, or NULL when transport is borrowed and its local address is not specified. */
const kc_stun_endpoint_t *kc_stun_client_local(const kc_stun_client_t *client);
/** Endpoint observed by peer after a successful Binding transaction. */
const kc_stun_endpoint_t *kc_stun_client_mapped(const kc_stun_client_t *client);
/** Parsed response attributes, valid until client_refresh() or client_close(). */
const kc_stun_attribute_t *kc_stun_client_attributes(const kc_stun_client_t *client, size_t *count);
/** Sends an application UDP datagram through the same mapping (owned or borrowed). */
int kc_stun_client_send(kc_stun_client_t *client, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);
/** Reads an application datagram on an owned socket after the STUN transaction completes. */
int kc_stun_client_read(kc_stun_client_t *client, kc_stun_datagram_t *out, uint64_t timeout_ms);
/** Releases one datagram and resets its fields. */
void kc_stun_datagram_free(kc_stun_datagram_t *datagram);
/** Sends an unauthenticated STUN Binding Indication to keep the peer UDP path alive. */
int kc_stun_client_keepalive(kc_stun_client_t *client);
/** Performs a new Binding transaction on the same UDP mapping. */
int kc_stun_client_refresh(kc_stun_client_t *client, uint64_t timeout_ms);
/** Returns STUN error code from a rejected Binding transaction, or zero. */
unsigned short kc_stun_client_error(const kc_stun_client_t *client);
/** Releases the client and its owned socket, never a borrowed transport. */
void kc_stun_client_close(kc_stun_client_t *client);
/** Starts a STUN Binding responder; owned UDP receives on an internal thread. */
int kc_stun_server(kc_stun_server_t **out, const kc_stun_server_options_t *options);
/** Processes one borrowed-transport datagram; responses use the provided send callback. */
int kc_stun_server_receive(kc_stun_server_t *server, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);
/** Returns the bound server UDP port (or configured borrowed port). */
unsigned short kc_stun_server_port(const kc_stun_server_t *server);
/** Stops server, releases owned sockets, and joins its worker. */
void kc_stun_server_close(kc_stun_server_t *server);
/** Human-readable error code. */
const char *kc_stun_strerror(int status);
/** Build version. */
uint64_t kc_stun_version(void);
#ifdef __cplusplus
}
#endif
#endif
