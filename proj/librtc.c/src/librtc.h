/**
 * librtc.h - Native WebRTC DataChannel capability.
 * Summary: Public API for native WebRTC peer connections.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_RTC_H
#define KC_RTC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_rtc_peer kc_rtc_peer_t;

#define KC_RTC_OK 0
#define KC_RTC_ERROR -1
#define KC_RTC_EINVAL -2

typedef struct {
    const char **ice_servers;
    size_t ice_server_count;
} kc_rtc_peer_options_t;

/**
 * Creates one native WebRTC peer connection.
 * @param out Destination peer handle.
 * @param options Optional ICE server configuration, or NULL.
 * @return KC_RTC_OK on success, otherwise a negative status.
 */
int kc_rtc_peer(kc_rtc_peer_t **out, const kc_rtc_peer_options_t *options);

/**
 * Closes and releases one peer connection. NULL is a safe no-op.
 * @param peer Peer connection returned by kc_rtc_peer().
 * @return None.
 */
void kc_rtc_peer_close(kc_rtc_peer_t *peer);

/**
 * Returns a stable static description for one RTC status.
 * @param status RTC status code.
 * @return Stable static status description.
 */
const char *kc_rtc_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_rtc_version(void);

#ifdef __cplusplus
}
#endif

#endif
