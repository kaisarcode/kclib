/**
 * librtc.c - Native WebRTC DataChannel capability.
 * Summary: Core implementation for native WebRTC peer connections.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "librtc.h"

#include <limits.h>
#include <stdlib.h>

#include <rtc/rtc.h>

#ifndef KC_RTC_BUILD_VERSION
#define KC_RTC_BUILD_VERSION 0
#endif

struct kc_rtc_peer {
    int id;
};

/**
 * Creates one native WebRTC peer connection.
 * @param out Destination peer handle.
 * @param options Optional ICE server configuration, or NULL.
 * @return KC_RTC_OK on success, otherwise a negative status.
 */
int kc_rtc_peer(kc_rtc_peer_t **out, const kc_rtc_peer_options_t *options) {
    rtcConfiguration config = {0};
    kc_rtc_peer_t *peer;
    int id;

    if (!out) {
        return KC_RTC_EINVAL;
    }
    *out = NULL;

    if (options) {
        if (options->ice_server_count > (size_t)INT_MAX) {
            return KC_RTC_EINVAL;
        }
        if (options->ice_server_count > 0 && !options->ice_servers) {
            return KC_RTC_EINVAL;
        }
        config.iceServers = options->ice_servers;
        config.iceServersCount = (int)options->ice_server_count;
    }

    id = rtcCreatePeerConnection(&config);
    if (id < 0) {
        return KC_RTC_ERROR;
    }

    peer = (kc_rtc_peer_t *)calloc(1, sizeof(*peer));
    if (!peer) {
        rtcClosePeerConnection(id);
        rtcDeletePeerConnection(id);
        return KC_RTC_ERROR;
    }

    peer->id = id;
    *out = peer;
    return KC_RTC_OK;
}

/**
 * Closes and releases one peer connection. NULL is a safe no-op.
 * @param peer Peer connection returned by kc_rtc_peer().
 * @return None.
 */
void kc_rtc_peer_close(kc_rtc_peer_t *peer) {
    if (!peer) {
        return;
    }

    rtcClosePeerConnection(peer->id);
    rtcDeletePeerConnection(peer->id);
    free(peer);
}

/**
 * Returns a stable static description for one RTC status.
 * @param status RTC status code.
 * @return Stable static status description.
 */
const char *kc_rtc_strerror(int status) {
    switch (status) {
        case KC_RTC_OK:
            return "ok";
        case KC_RTC_ERROR:
            return "rtc error";
        case KC_RTC_EINVAL:
            return "invalid argument";
        default:
            return "unknown rtc status";
    }
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_rtc_version(void) {
    return (uint64_t)KC_RTC_BUILD_VERSION;
}
