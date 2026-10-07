/**
 * libredp2p-idx.h - REDP2P.
 * Summary: Private index core and transport coordination helpers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef REDP2P_IDX_H
#define REDP2P_IDX_H

#include "libredp2p-core.h"

#include "parson.h"

#include <stdio.h>

#define REDP2P_IDX_NATIVE_RATE_TARGET_CAP 40000U

REDP2P_INTERNAL int redp2p_constant_time_equal(const unsigned char *a,
    const unsigned char *b, size_t len);
REDP2P_INTERNAL int redp2p_challenge_mac_input(
    const unsigned char nonce[32], uint64_t issued_at, uint64_t expires_at,
    unsigned char *out, size_t *out_len);
REDP2P_INTERNAL int redp2p_http_write_response(redp2p_fd_t fd, int status,
    const char *reason, const char *content_type, const char *body);
REDP2P_INTERNAL size_t redp2p_find_peer(redp2p_t *ctx, const char *id);
REDP2P_INTERNAL void redp2p_evict_stale(redp2p_t *ctx);
REDP2P_INTERNAL int redp2p_peer_is_stale(redp2p_t *ctx, size_t index);
REDP2P_INTERNAL int redp2p_add_peer(redp2p_t *ctx, const char *id,
    const char *key);
REDP2P_INTERNAL int redp2p_remove_peer(redp2p_t *ctx, const char *id);
REDP2P_INTERNAL int redp2p_index_respond(redp2p_fd_t fd, int status,
    const char *reason, JSON_Value *value);
REDP2P_INTERNAL void redp2p_index_respond_error(redp2p_fd_t fd, int status,
    const char *code);
REDP2P_INTERNAL int redp2p_index_require_id(JSON_Object *obj,
    const char *field, char *id, size_t id_cap);
REDP2P_INTERNAL int redp2p_index_require_id_response(JSON_Object *req,
    redp2p_fd_t fd, char *id, size_t id_cap);
REDP2P_INTERNAL int redp2p_index_require_sequence(JSON_Object *obj,
    uint64_t *sequence);
REDP2P_INTERNAL const char *redp2p_index_password(redp2p_t *ctx,
    const char *id);

REDP2P_INTERNAL void redp2p_idx_native_register(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_native_heartbeat(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_native_lookup(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_native_punch_req(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req,
    const struct sockaddr_storage *peer);
REDP2P_INTERNAL void redp2p_idx_native_punch_poll(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_native_prune(redp2p_t *ctx);
REDP2P_INTERNAL int redp2p_idx_native_rate_allow(redp2p_t *ctx,
    const struct sockaddr_storage *peer, size_t target);

REDP2P_INTERNAL void redp2p_idx_webrtc_register(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_webrtc_heartbeat(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_webrtc_lookup(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_webrtc_connect(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req, const struct sockaddr_storage *peer);
REDP2P_INTERNAL void redp2p_idx_webrtc_poll(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_webrtc_answer(redp2p_t *ctx,
    redp2p_fd_t fd, JSON_Object *req);
REDP2P_INTERNAL void redp2p_idx_webrtc_prune(redp2p_t *ctx);
REDP2P_INTERNAL void redp2p_idx_webrtc_remove_publisher(redp2p_t *ctx,
    const char *id);
REDP2P_INTERNAL void redp2p_idx_webrtc_destroy(redp2p_t *ctx);

#endif
