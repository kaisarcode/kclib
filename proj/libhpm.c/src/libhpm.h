/**
 * libhpm.h - Hole-Punch-Man direct UDP paths.
 * Summary: Establishes a direct UDP path to one supplied peer endpoint.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_HPM_H
#define KC_HPM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_hpm_path kc_hpm_path_t;

#define KC_HPM_OK        0
#define KC_HPM_EINVAL   -1
#define KC_HPM_ENET     -2
#define KC_HPM_ETIMEOUT -3
#define KC_HPM_ENOMEM   -4
#define KC_HPM_ECLOSED  -5

typedef struct {
    const char *host;
    unsigned short port;
} kc_hpm_endpoint_t;

typedef struct {
    const char *host;
    unsigned short port;
    const kc_hpm_endpoint_t *peers;
    size_t peer_count;
    uint64_t timeout_ms;
} kc_hpm_options_t;

/**
 * Establish a direct bidirectional UDP path to one supplied peer endpoint.
 * NULL or empty host binds all local interfaces. Port zero requests an
 * operating-system-selected local port. peers must contain at least one
 * endpoint with a host and nonzero port. timeout_ms zero uses the default.
 *
 * Peer endpoint discovery, STUN, rendezvous, relay, identity, and application
 * policy are outside this operation.
 *
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_connect(
    kc_hpm_path_t **out,
    const kc_hpm_options_t *options
);

/**
 * Send one UDP datagram through an established path.
 * The supplied bytes are consumed before this function returns.
 *
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_send(
    kc_hpm_path_t *path,
    const void *data,
    size_t data_size
);

/**
 * Receive one UDP datagram through an established path.
 * The returned buffer is owned by the caller and released with kc_hpm_free().
 * timeout_ms zero waits indefinitely.
 *
 * @return KC_HPM_OK on success, otherwise a negative status.
 */
int kc_hpm_receive(
    kc_hpm_path_t *path,
    void **out_data,
    size_t *out_size,
    uint64_t timeout_ms
);

/**
 * Returns the numeric local host selected for the path, or NULL.
 * @param path Path handle.
 * @return Local host string or NULL if unknown.
 */
const char *kc_hpm_local_host(const kc_hpm_path_t *path);

/**
 * Returns the local UDP port selected for the path, or zero.
 * @param path Path handle.
 * @return Local port or 0 if unknown.
 */
unsigned short kc_hpm_local_port(const kc_hpm_path_t *path);

/**
 * Returns the numeric remote host selected for the path, or NULL.
 * @param path Path handle.
 * @return Peer host string or NULL if unknown.
 */
const char *kc_hpm_peer_host(const kc_hpm_path_t *path);

/**
 * Returns the remote UDP port selected for the path, or zero.
 * @param path Path handle.
 * @return Peer port or 0 if unknown.
 */
unsigned short kc_hpm_peer_port(const kc_hpm_path_t *path);

/**
 * Releases memory returned by libhpm. NULL is accepted.
 * @param ptr Pointer to free.
 * @return None.
 */
void kc_hpm_free(void *ptr);

/**
 * Closes and releases an established path. NULL is accepted.
 * @param path Path handle.
 * @return None.
 */
void kc_hpm_close(kc_hpm_path_t *path);

/**
 * Returns a static message for a public status code.
 * @param status Status code.
 * @return Static error message string.
 */
const char *kc_hpm_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version number.
 */
uint64_t kc_hpm_version(void);

#ifdef __cplusplus
}
#endif

#endif
