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

    /** Integrity mode; zero auto-selects both algorithms. */
    unsigned int integrity;
} kc_stun_credentials_t;

typedef struct {
    kc_stun_endpoint_t peer;
    const char *host;
    unsigned short port;
    uint64_t timeout_ms;

    /** Borrowed transport; send only, never closed. */
    const kc_stun_transport_t *transport;
    kc_stun_credentials_t credentials;
    const kc_stun_attribute_t *attributes;
    size_t attribute_count;
} kc_stun_client_options_t;

typedef int (*kc_stun_credentials_fn)(void *context, const char *username, kc_stun_credentials_t *credentials);

/**
 * Selects the response code for a binding request.
 * @return 0 to accept; otherwise a STUN error code.
 */
typedef unsigned short (*kc_stun_binding_fn)(void *context, const kc_stun_endpoint_t *peer, const kc_stun_attribute_t *attributes, size_t count);

typedef struct {
    const char *host;
    unsigned short port;

    /** Borrowed transport; the caller dispatches datagrams. */
    const kc_stun_transport_t *transport;
    kc_stun_credentials_fn credentials;
    kc_stun_binding_fn binding;
    void *context;
} kc_stun_server_options_t;

/**
 * Identifies a complete RFC 8489 STUN datagram in an external buffer.
 * Checks the header bits, magic cookie, aligned message length and exact
 * datagram size; it does not authenticate the contents or validate attributes.
 * @param bytes Datagram bytes.
 * @param size Datagram size.
 * @return 1 for a STUN envelope, otherwise 0.
 */
int kc_stun_is_message(const void *bytes, size_t size);

#define KC_STUN_REQUEST 0x0001u
#define KC_STUN_INDICATION 0x0011u
#define KC_STUN_SUCCESS_RESPONSE 0x0101u
#define KC_STUN_ERROR_RESPONSE 0x0111u
#define KC_STUN_PRIORITY 0x0024u
#define KC_STUN_USE_CANDIDATE 0x0025u
#define KC_STUN_ICE_CONTROLLED 0x8029u
#define KC_STUN_ICE_CONTROLLING 0x802au

/**
 * A decoded STUN message backed by the caller's immutable byte buffer.
 * Attributes are returned as views: do not free their data pointers.
 */
typedef struct {
    unsigned short type;
    unsigned char transaction_id[12];
    kc_stun_attribute_t *attributes;
    size_t attribute_count;
} kc_stun_message_t;

/**
 * Parses a complete STUN datagram into attribute views.
 * Call kc_stun_message_free to release only the attribute array.
 * @return KC_STUN_OK or a negative status.
 */
int kc_stun_decode(const void *bytes,size_t size,kc_stun_message_t *out);

/** Frees decoded attribute views without freeing caller-owned bytes. */
void kc_stun_message_free(kc_stun_message_t *message);

/**
 * Encodes a STUN message in an externally supplied buffer.
 * If credentials has a key, appends requested SHA-1/SHA-256 integrity.
 * Always appends FINGERPRINT. The caller supplies transaction_id.
 * Raw attributes must not include integrity or fingerprint attributes.
 * @param size In: buffer capacity; out: encoded length.
 */
int kc_stun_encode(const kc_stun_message_t *message,
    const kc_stun_credentials_t *credentials,void *out,size_t *size);

/** Verifies a message's FINGERPRINT; absence is an error. */
int kc_stun_verify_fingerprint(const void *bytes,size_t size);

/** Verifies a message's HMAC with caller-supplied credentials. */
int kc_stun_verify_integrity(const void *bytes,size_t size,
    const void *key,size_t key_size);

/** Encodes the ERROR-CODE attribute value for a STUN error response. */
int kc_stun_encode_error(unsigned short code,const char *reason,
    void *out,size_t *size);

/** Decodes an ERROR-CODE attribute and optionally copies the reason. */
int kc_stun_decode_error(const kc_stun_attribute_t *attribute,
    unsigned short *code,char *reason,size_t reason_size);

/** Compares two STUN transaction identifiers. */
int kc_stun_transaction_matches(const kc_stun_message_t *message,
    const unsigned char id[12]);

/** Decodes XOR-MAPPED-ADDRESS to caller-owned numeric host storage. */
int kc_stun_decode_mapped(const kc_stun_attribute_t *attribute,
    const unsigned char id[12],char *host,size_t host_size,
    unsigned short *port);

/** Encodes an XOR-MAPPED-ADDRESS value into caller-owned storage. */
int kc_stun_encode_mapped(const kc_stun_endpoint_t *endpoint,
    const unsigned char id[12],void *out,size_t *size);

/**
 * Begins one authenticated or anonymous Binding transaction.
 * @param out Destination client pointer.
 * @param options Client options.
 * @return KC_STUN_OK, or a negative status.
 */
int kc_stun_client(kc_stun_client_t **out, const kc_stun_client_options_t *options);

/**
 * Drives retransmissions and owned-socket reception.
 * @param client Client to drive.
 * @param wait_ms Maximum wait in milliseconds.
 * @return OK, PENDING, or a negative status.
 */
int kc_stun_client_poll(kc_stun_client_t *client, uint64_t wait_ms);

/**
 * Delivers one datagram to a client on borrowed transport.
 * @param client Client receiving the datagram.
 * @param peer Source endpoint.
 * @param bytes Datagram bytes.
 * @param size Byte count.
 * @return The current client status.
 */
int kc_stun_client_receive(kc_stun_client_t *client, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);

/**
 * Returns the local endpoint of a client.
 * @param client Client to inspect.
 * @return Local endpoint, or NULL when unspecified.
 */
const kc_stun_endpoint_t *kc_stun_client_local(const kc_stun_client_t *client);

/**
 * Returns the endpoint observed by the peer.
 * @param client Client to inspect.
 * @return Mapped endpoint after success, or NULL.
 */
const kc_stun_endpoint_t *kc_stun_client_mapped(const kc_stun_client_t *client);

/**
 * Returns the parsed attributes of the last response.
 * Returned attributes remain valid until refresh or close.
 * @param client Client to inspect.
 * @param count Destination attribute count.
 * @return Attribute array, or NULL when absent.
 */
const kc_stun_attribute_t *kc_stun_client_attributes(const kc_stun_client_t *client, size_t *count);

/**
 * Sends an application datagram through the same mapping.
 * @param client Client owning the mapping.
 * @param peer Destination endpoint.
 * @param bytes Datagram bytes.
 * @param size Byte count.
 * @return A STUN status code.
 */
int kc_stun_client_send(kc_stun_client_t *client, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);

/**
 * Reads a datagram on an owned socket, including data preserved during Binding.
 * Unrelated STUN messages are returned to the caller for dispatch; they are
 * not automatically application payloads. The receive queue is bounded.
 * @param client Client owning the socket.
 * @param out Destination datagram.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return A STUN status code.
 */
int kc_stun_client_read(kc_stun_client_t *client, kc_stun_datagram_t *out, uint64_t timeout_ms);

/**
 * Releases one datagram and resets its fields.
 * @param datagram Datagram to release.
 * @return void.
 */
void kc_stun_datagram_free(kc_stun_datagram_t *datagram);

/**
 * Sends a Binding Indication to keep the UDP path alive.
 * @param client Client owning the mapping.
 * @return A STUN status code.
 */
int kc_stun_client_keepalive(kc_stun_client_t *client);

/**
 * Performs a new Binding transaction on the same mapping.
 * @param client Client to refresh.
 * @param timeout_ms Transaction timeout in milliseconds.
 * @return A STUN status code.
 */
int kc_stun_client_refresh(kc_stun_client_t *client, uint64_t timeout_ms);

/**
 * Returns the error code of a rejected transaction.
 * @param client Client to inspect.
 * @return Error code, or zero when not rejected.
 */
unsigned short kc_stun_client_error(const kc_stun_client_t *client);

/**
 * Releases a client and its owned resources.
 * @param client Client to release, or NULL.
 * @return void.
 */
void kc_stun_client_close(kc_stun_client_t *client);

/**
 * Starts a STUN Binding responder.
 * Owned UDP receives on an internal thread.
 * @param out Destination server pointer.
 * @param options Server options.
 * @return KC_STUN_OK, or a negative status.
 */
int kc_stun_server(kc_stun_server_t **out, const kc_stun_server_options_t *options);

/**
 * Processes one borrowed-transport datagram.
 * @param server Server handling the datagram.
 * @param peer Source endpoint.
 * @param bytes Datagram bytes.
 * @param size Byte count.
 * @return A STUN status code.
 */
int kc_stun_server_receive(kc_stun_server_t *server, const kc_stun_endpoint_t *peer, const void *bytes, size_t size);

/**
 * Returns the bound or configured server UDP port.
 * @param server Server to inspect.
 * @return Server port.
 */
unsigned short kc_stun_server_port(const kc_stun_server_t *server);

/**
 * Stops a server and releases owned resources.
 * @param server Server to close.
 * @return void.
 */
void kc_stun_server_close(kc_stun_server_t *server);

/**
 * Returns a human-readable status description.
 * @param status STUN status code.
 * @return Static description text.
 */
const char *kc_stun_strerror(int status);

/**
 * Returns the library build version.
 * @return Build version number.
 */
uint64_t kc_stun_version(void);
#ifdef __cplusplus
}
#endif
#endif
