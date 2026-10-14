/**
 * libstun.h - STUN UDP mappings and Binding endpoints.
 * Summary: Discovers UDP mappings and serves STUN Binding requests.
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

typedef struct kc_stun_mapping kc_stun_mapping_t;
typedef struct kc_stun_server kc_stun_server_t;

#define KC_STUN_OK        0
#define KC_STUN_EINVAL   -1
#define KC_STUN_ENET     -2
#define KC_STUN_ETIMEOUT -3
#define KC_STUN_EPROTO   -4
#define KC_STUN_ENOMEM   -5
#define KC_STUN_ECLOSED  -6

typedef struct {
    const char *host;
    unsigned short port;
} kc_stun_endpoint_t;

typedef struct {
    const char *server;
    const char *host;
    unsigned short port;
    uint64_t timeout_ms;
} kc_stun_options_t;

typedef struct {
    const char *host;
    unsigned short port;
} kc_stun_server_options_t;

typedef struct {
    char *host;
    unsigned short port;
    void *data;
    size_t data_size;
} kc_stun_datagram_t;

/**
 * Starts an autonomous UDP STUN Binding endpoint.
 *
 * NULL or empty host listens on available IPv4 and IPv6 wildcard addresses.
 * An explicit host restricts the listener to address families resolved for
 * that host. Port zero requests an operating-system-selected local port.
 * The server replies to UDP STUN Binding Requests with XOR-MAPPED-ADDRESS
 * until kc_stun_server_close() is called.
 *
 * @return KC_STUN_OK on success, otherwise a negative status.
 */
int kc_stun_listen(
    kc_stun_server_t **out,
    const kc_stun_server_options_t *options
);

/**
 * Returns the UDP port used by a running STUN server, or zero.
 */
unsigned short kc_stun_server_port(const kc_stun_server_t *server);

/**
 * Stops and releases a STUN server. NULL is accepted.
 */
void kc_stun_server_close(kc_stun_server_t *server);

/**
 * Discovers the externally observed endpoint of a bound UDP mapping and keeps
 * that mapping alive as an object.
 *
 * server accepts host:port, [IPv6]:port, or a host without a port (3478 is
 * used). An optional stun: prefix is accepted. NULL or empty host binds all
 * local interfaces. Port zero requests an operating-system-selected local
 * port. timeout_ms zero uses the default discovery timeout.
 *
 * @return KC_STUN_OK on success, otherwise a negative status.
 */
int kc_stun_discover(
    kc_stun_mapping_t **out,
    const kc_stun_options_t *options
);

/**
 * Sends a fresh STUN Binding Request through the existing UDP mapping.
 *
 * The response is consumed internally by kc_stun_receive() when it arrives and
 * may update the mapped endpoint. This operation does not wait for the server.
 *
 * @return KC_STUN_OK on success, otherwise a negative status.
 */
int kc_stun_refresh(kc_stun_mapping_t *mapping);

/**
 * Sends one UDP datagram through the preserved local mapping.
 *
 * @return KC_STUN_OK on success, otherwise a negative status.
 */
int kc_stun_send(
    kc_stun_mapping_t *mapping,
    const kc_stun_endpoint_t *peer,
    const void *data,
    size_t data_size
);

/**
 * Receives one non-STUN UDP datagram through the preserved local mapping.
 *
 * STUN responses from the configured server are consumed internally and are
 * not returned as application datagrams. timeout_ms zero waits indefinitely.
 * The returned datagram owns host and data; release them with
 * kc_stun_datagram_free().
 *
 * @return KC_STUN_OK on success, otherwise a negative status.
 */
int kc_stun_receive(
    kc_stun_mapping_t *mapping,
    kc_stun_datagram_t *out,
    uint64_t timeout_ms
);

/**
 * Returns the numeric local bind host for the mapping, or NULL.
 */
const char *kc_stun_local_host(const kc_stun_mapping_t *mapping);

/**
 * Returns the local UDP port for the mapping, or zero.
 */
unsigned short kc_stun_local_port(const kc_stun_mapping_t *mapping);

/**
 * Returns the numeric endpoint observed by the STUN server, or NULL.
 */
const char *kc_stun_mapped_host(const kc_stun_mapping_t *mapping);

/**
 * Returns the UDP port observed by the STUN server, or zero.
 */
unsigned short kc_stun_mapped_port(const kc_stun_mapping_t *mapping);

/**
 * Releases allocations owned by one received datagram and resets it.
 * NULL is accepted.
 */
void kc_stun_datagram_free(kc_stun_datagram_t *datagram);

/**
 * Closes and releases a live mapping. NULL is accepted.
 */
void kc_stun_close(kc_stun_mapping_t *mapping);

/**
 * Returns a static message for a public status code.
 */
const char *kc_stun_strerror(int status);

/**
 * Returns the build version generated at compile time.
 */
uint64_t kc_stun_version(void);

#ifdef __cplusplus
}
#endif

#endif
