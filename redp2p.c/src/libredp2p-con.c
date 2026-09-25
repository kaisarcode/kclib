/**
 * libredp2p-con.c - REDP2P.
 * Summary: Consumer peer establishment and local service sessions.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libredp2p-peer.h"

#include "monocypher.h"
#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

typedef struct redp2p_udp_consumer_session {
    redp2p_fd_t fd;
    redp2p_fd_t tcp_fd;
    struct sockaddr_storage client_addr;
    struct sockaddr_storage peer_addr;
    uint64_t last_rx;
    uint64_t last_ka;
    int active;
    int is_tcp;
    redp2p_stream_state_t stream;
} redp2p_udp_consumer_session_t;

typedef struct {
    redp2p_t *ctx;
    const char *index_host;
    const char *self_id;
    const char *target_id;
    const char *udp_any_host;
    unsigned short index_port;
    redp2p_fd_t local_fd;
    redp2p_fd_t tcp_listen_fd;
    redp2p_udp_consumer_session_t *sessions;
    int n_sessions;
    int cap_sessions;
    int platform_initialized;
    redp2p_candidate_t peer_candidates[REDP2P_PEER_CANDIDATES_MAX];
    int n_peer_candidates;
} redp2p_consumer_runtime_t;

/**
 * Generates one secure stream session identifier.
 * @return 1 on success, 0 on error.
 */
static int redp2p_stream_make_session_id(unsigned char out[REDP2P_SESSION_ID_SZ],
    char hex[REDP2P_SESSION_ID_SZ * 2 + 1])
{
    if (redp2p_fill_random(out, REDP2P_SESSION_ID_SZ) != 0) return 0;
    return redp2p_hex_encode(out, REDP2P_SESSION_ID_SZ, hex,
        REDP2P_SESSION_ID_SZ * 2 + 1);
}

/**
 * Closes one consumer-side UDP session and wipes TCP stream state.
 * @return None.
 */
static void redp2p_consumer_session_close(redp2p_udp_consumer_session_t *sess) {
    if (!sess) return;
    if (sess->tcp_fd != REDP2P_FD_INVALID) {
        REDP2P_FD_CLOSE(sess->tcp_fd);
        sess->tcp_fd = REDP2P_FD_INVALID;
    }
    if (!REDP2P_ISERR(sess->fd)) {
        REDP2P_FD_CLOSE(sess->fd);
        sess->fd = REDP2P_FD_INVALID;
    }
    if (sess->is_tcp) redp2p_stream_wipe(&sess->stream);
    sess->active = 0;
}

/**
 * Create tcp listener.
 * @return 0 on success, -1 on error.
 */
static redp2p_fd_t redp2p_create_tcp_listener(
    const char *bind_host,
    unsigned short bind_port)
{
    redp2p_fd_t fd;
    struct addrinfo hints;
    struct addrinfo *ai;
    struct addrinfo *it;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)bind_port);
    if (getaddrinfo(bind_host, port_str, &hints, &ai) != 0)
        return REDP2P_FD_INVALID;
    fd = REDP2P_FD_INVALID;
    for (it = ai; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (REDP2P_ISERR(fd)) continue;
        {
            int reuse = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                (void *)&reuse, sizeof(reuse));
        }
#ifdef IPV6_V6ONLY
        if (it->ai_family == AF_INET6) {
            int v6only = 0;
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
                (void *)&v6only, sizeof(v6only));
        }
#endif
        if (bind(fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0 &&
            listen(fd, 32) == 0)
            break;
        REDP2P_FD_CLOSE(fd);
        fd = REDP2P_FD_INVALID;
    }
    freeaddrinfo(ai);
    return fd;
}

/**
 * Sends one HTTP punch request carrying the local candidates.
 * @param runtime Consumer runtime owning index, identity, and session settings.
 * @param session_hex Session token used by the punch exchange.
 * @param cands Local candidates, may be NULL when cand_count is zero.
 * @param cand_count Local candidate count.
 * @param udp_port Actual source port of the punch UDP socket.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_send_punch_req_cands(
redp2p_consumer_runtime_t *runtime,
const char *session_hex,
const redp2p_candidate_t *cands,
int cand_count,
unsigned short udp_port)
{
    redp2p_t *ctx;
    JSON_Value *request;
    JSON_Object *obj;
    int result;

    ctx = runtime->ctx;
    if ((cand_count < 0 || cand_count > REDP2P_PEER_CANDIDATES_MAX) ||
        (cand_count > 0 && !cands) || !session_hex)
        return REDP2P_EPROTO;
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "connect: punch request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "punch_req");
    json_object_set_string(obj, "self_id", runtime->self_id);
    json_object_set_string(obj, "target_id", runtime->target_id);
    json_object_set_string(obj, "session", session_hex);
    json_object_set_number(obj, "udp_port", (double)udp_port);
    redp2p_append_candidates(obj, "candidates", cands, cand_count);
    result = redp2p_http_client(ctx, "connect", runtime->index_host,
        runtime->index_port, request, NULL);
    json_value_free(request);
    return result;
}

/**
 * Finds an active UDP session for one local client address.
 * @param runtime Consumer runtime containing the session table.
 * @param client_addr Local client address to match.
 * @return Matching session index, or -1 when no session matches.
 */
static int redp2p_consumer_session_find(
const redp2p_consumer_runtime_t *runtime,
const struct sockaddr_storage *client_addr)
{
    int i;

    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) continue;
        if (redp2p_sockaddr_equal(&runtime->sessions[i].client_addr,
            client_addr))
            return i;
    }
    return -1;
}

/**
 * Transfers one initialized session into a compatible table slot.
 * @param runtime Consumer runtime that owns the session table.
 * @param session Initialized session whose descriptors transfer on success.
 * @return Inserted session index, or -1 on allocation failure.
 */
static int redp2p_consumer_session_insert(
redp2p_consumer_runtime_t *runtime,
redp2p_udp_consumer_session_t *session)
{
    redp2p_udp_consumer_session_t *new_sessions;
    int index;
    int new_cap;

    index = -1;
    for (int i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) {
            index = i;
            break;
        }
    }
    if (index < 0 && runtime->n_sessions >= runtime->cap_sessions) {
        new_cap = runtime->cap_sessions == 0 ? 8 : runtime->cap_sessions * 2;
        new_sessions = (redp2p_udp_consumer_session_t *)realloc(
            runtime->sessions, (size_t)new_cap * sizeof(*runtime->sessions));
        if (!new_sessions) {
            redp2p_consumer_session_close(session);
            crypto_wipe(session, sizeof(*session));
            return -1;
        }
        runtime->sessions = new_sessions;
        runtime->cap_sessions = new_cap;
    }
    if (index < 0) index = runtime->n_sessions++;
    runtime->sessions[index] = *session;
    crypto_wipe(session, sizeof(*session));
    return index;
}

/**
 * Closes every active consumer session in table order.
 * @param runtime Consumer runtime that owns the sessions.
 * @return None.
 */
static void redp2p_consumer_session_close_all(
redp2p_consumer_runtime_t *runtime)
{
    int i;

    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) continue;
        if (runtime->sessions[i].is_tcp &&
            !redp2p_stream_is_done(&runtime->sessions[i].stream))
        {
            redp2p_stream_fail(runtime->ctx, &runtime->sessions[i].stream);
        }
        redp2p_consumer_session_close(&runtime->sessions[i]);
    }
}

/**
 * Establishes one peer path and transfers its descriptor only on success.
 * @param runtime Consumer runtime containing control and peer settings.
 * @param is_tcp Non-zero selects the TCP stream session identifier format.
 * @param out_fd Output peer descriptor.
 * @param out_peer Output selected peer address.
 * @param session_bin Output binary session identifier.
 * @param session_hex Output hexadecimal session identifier.
 * @param skip_iteration Output set when the current event iteration must end.
 * @return REDP2P_OK on success, or the existing establishment error code.
 */
static int redp2p_consumer_establish_peer(
redp2p_consumer_runtime_t *runtime,
int is_tcp,
redp2p_fd_t *out_fd,
struct sockaddr_storage *out_peer,
unsigned char session_bin[REDP2P_SESSION_ID_SZ],
char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1],
int *skip_iteration)
{
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    struct sockaddr_storage peer_addr;
    struct sockaddr_storage punch_sa;
    redp2p_fd_t peer_fd;
    socklen_t punch_sa_len;
    unsigned short punch_port;
    int candidate_count;
    int result;
    size_t random_size;
    size_t hex_size;

    *out_fd = REDP2P_FD_INVALID;
    memset(out_peer, 0, sizeof(*out_peer));
    memset(session_bin, 0, REDP2P_SESSION_ID_SZ);
    memset(session_hex, 0, REDP2P_SESSION_ID_SZ * 2 + 1);
    if (skip_iteration) *skip_iteration = 0;

    if (is_tcp) {
        if (!redp2p_stream_make_session_id(session_bin, session_hex)) {
            if (skip_iteration) *skip_iteration = 1;
            crypto_wipe(session_bin, REDP2P_SESSION_ID_SZ);
            crypto_wipe(session_hex, REDP2P_SESSION_ID_SZ * 2 + 1);
            return REDP2P_ENET;
        }
    } else {
        random_size = 8;
        hex_size = 17;
        if (redp2p_fill_random(session_bin, random_size) != 0 ||
            !redp2p_hex_encode(session_bin, random_size, session_hex, hex_size))
        {
            if (skip_iteration) *skip_iteration = 1;
            crypto_wipe(session_bin, REDP2P_SESSION_ID_SZ);
            crypto_wipe(session_hex, REDP2P_SESSION_ID_SZ * 2 + 1);
            return REDP2P_ENET;
        }
    }

    peer_fd = redp2p_create_socket(runtime->udp_any_host, 0);
    candidate_count = 0;
    if (REDP2P_ISERR(peer_fd) ||
        redp2p_gather_candidates(runtime->ctx, peer_fd, candidates,
            REDP2P_PEER_CANDIDATES_MAX, &candidate_count) != REDP2P_OK)
    {
        if (skip_iteration) *skip_iteration = 1;
        crypto_wipe(session_bin, REDP2P_SESSION_ID_SZ);
        crypto_wipe(session_hex, REDP2P_SESSION_ID_SZ * 2 + 1);
        if (!REDP2P_ISERR(peer_fd)) REDP2P_FD_CLOSE(peer_fd);
        return REDP2P_ENET;
    }
    punch_port = 0;
    punch_sa_len = sizeof(punch_sa);
    if (getsockname(peer_fd, (struct sockaddr *)&punch_sa, &punch_sa_len) == 0)
        punch_port = redp2p_sockaddr_port(&punch_sa);
    result = redp2p_send_punch_req_cands(runtime, session_hex, candidates,
        candidate_count, punch_port);
    if (result != REDP2P_OK) {
        if (skip_iteration) *skip_iteration = 1;
        crypto_wipe(session_bin, REDP2P_SESSION_ID_SZ);
        crypto_wipe(session_hex, REDP2P_SESSION_ID_SZ * 2 + 1);
        REDP2P_FD_CLOSE(peer_fd);
        return result;
    }

    memset(&peer_addr, 0, sizeof(peer_addr));
    result = redp2p_punch_select(runtime->ctx, runtime->ctx->sweep,
        (int)peer_fd,
        session_hex, runtime->self_id, runtime->target_id,
        runtime->peer_candidates, runtime->n_peer_candidates, &peer_addr);
    if (result != REDP2P_OK) {
        REDP2P_FD_CLOSE((int)peer_fd);
        crypto_wipe(session_bin, REDP2P_SESSION_ID_SZ);
        crypto_wipe(session_hex, REDP2P_SESSION_ID_SZ * 2 + 1);
        return result;
    }

    *out_fd = (int)peer_fd;
    *out_peer = peer_addr;
    return REDP2P_OK;
}

/**
 * Initializes one established TCP consumer session.
 * @param runtime Consumer runtime containing stream identities.
 * @param session Session receiving ownership of peer and client descriptors.
 * @param peer_fd Established peer descriptor.
 * @param peer_addr Selected peer address.
 * @param client_fd Accepted local TCP descriptor.
 * @param session_bin Binary stream session identifier.
 * @param session_hex Hexadecimal stream session identifier.
 * @return 0 on success, -1 when KCP initialization fails.
 */
static int redp2p_consumer_tcp_session_init(
redp2p_consumer_runtime_t *runtime,
redp2p_udp_consumer_session_t *session,
redp2p_fd_t peer_fd,
const struct sockaddr_storage *peer_addr,
redp2p_fd_t client_fd,
const unsigned char session_bin[REDP2P_SESSION_ID_SZ],
const char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1])
{
    memset(session, 0, sizeof(*session));
    session->fd = peer_fd;
    session->tcp_fd = client_fd;
    session->peer_addr = *peer_addr;
    if (redp2p_stream_init(runtime->ctx, &session->stream, 1, peer_fd,
        peer_addr, session_bin, session_hex, REDP2P_PROTO_TCP) != 0)
        return -1;
    session->active = 1;
    session->is_tcp = 1;
    session->last_rx = redp2p_now_s();
    session->last_ka = session->last_rx;
    memset(&session->client_addr, 0, sizeof(session->client_addr));
    return 0;
}

/**
 * Initializes one established UDP consumer session.
 * @param session Session receiving ownership of the peer descriptor.
 * @param peer_fd Established peer descriptor.
 * @param peer_addr Selected peer address.
 * @param client_addr Local datagram source address.
 * @return None.
 */
static void redp2p_consumer_udp_session_init(
redp2p_udp_consumer_session_t *session,
redp2p_fd_t peer_fd,
const struct sockaddr_storage *peer_addr,
const struct sockaddr_storage *client_addr)
{
    memset(session, 0, sizeof(*session));
    session->fd = peer_fd;
    session->tcp_fd = REDP2P_FD_INVALID;
    session->peer_addr = *peer_addr;
    session->client_addr = *client_addr;
    session->last_rx = redp2p_now_s();
    session->last_ka = session->last_rx;
    session->active = 1;
    session->is_tcp = 0;
}

/**
 * Initializes consumer validation, platform state, and local descriptors.
 * @param runtime Consumer runtime receiving owned resources.
 * @param ctx Public context borrowed for the runtime lifetime.
 * @param index_host Index host borrowed for the runtime lifetime.
 * @param index_port Index control port.
 * @param self_id Consumer identity borrowed for the runtime lifetime.
 * @param target_id Publisher identity borrowed for the runtime lifetime.
 * @param bind_port Requested local adapter port.
 * @param should_run Output set when the consumer loop should start.
 * @return REDP2P_OK on success, or the existing initialization error code.
 */
static int redp2p_consumer_runtime_init(
redp2p_consumer_runtime_t *runtime,
redp2p_t *ctx,
const char *index_host,
unsigned short index_port,
const char *self_id,
const char *target_id,
unsigned short bind_port,
int *should_run)
{
    unsigned short effective_port;

    memset(runtime, 0, sizeof(*runtime));
    runtime->ctx = ctx;
    runtime->index_host = index_host;
    runtime->index_port = index_port;
    runtime->self_id = self_id;
    runtime->target_id = target_id;
    runtime->local_fd = REDP2P_FD_INVALID;
    runtime->tcp_listen_fd = REDP2P_FD_INVALID;
    *should_run = 0;
    redp2p_set_error(runtime->ctx, NULL);
    if (redp2p_is_stop_requested(runtime->ctx)) {
        atomic_store(&runtime->ctx->stop_requested, 0);
        return REDP2P_OK;
    }
    if (redp2p_resolve_port(runtime->ctx, bind_port, &effective_port) !=
        REDP2P_OK)
    {
        redp2p_set_error(runtime->ctx, "connect: conflicting local ports");
        return REDP2P_EINVAL;
    }
    if (effective_port == 0 || !runtime->index_host || !runtime->self_id ||
        !runtime->target_id || !redp2p_is_valid_id(runtime->self_id) ||
        !redp2p_is_valid_id(runtime->target_id) || runtime->index_port == 0)
    {
        redp2p_set_error(runtime->ctx,
            "connect: invalid index, identity, or local port");
        return REDP2P_EINVAL;
    }
    runtime->ctx->bind_port = effective_port;
    if (redp2p_platform_init() != 0) {
        redp2p_set_error(runtime->ctx, "connect: platform init failed");
        return REDP2P_ENET;
    }
    runtime->platform_initialized = 1;
    runtime->udp_any_host = redp2p_host_is_ipv6_literal(runtime->index_host) ?
        "::" : "0.0.0.0";
    *should_run = 1;
    return REDP2P_OK;
}

/**
 * Resolves the target publisher, learns its announced protocol, and stores
 * its candidates.
 * @param runtime Initialized consumer runtime.
 * @return REDP2P_OK on success, or the existing lookup error code.
 */
static int redp2p_consumer_initial_lookup(redp2p_consumer_runtime_t *runtime)
{
    redp2p_t *ctx;
    JSON_Value *request;
    JSON_Value *response;
    JSON_Object *obj;
    JSON_Object *out;
    double proto_number;
    int proto;
    int result;

    ctx = runtime->ctx;
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "connect: lookup request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "lookup");
    json_object_set_string(obj, "id", runtime->target_id);
    response = NULL;
    result = redp2p_http_client(ctx, "connect", runtime->index_host,
        runtime->index_port, request, &response);
    json_value_free(request);
    if (result != REDP2P_OK) {
        if (result == REDP2P_ENOENT)
            redp2p_set_error(ctx, "connect: target publisher not found");
        return result;
    }
    out = json_value_get_object(response);
    runtime->n_peer_candidates = 0;
    if (!out || !json_object_has_value_of_type(out, "proto", JSONNumber) ||
        !redp2p_parse_candidates(out, "candidates",
            runtime->peer_candidates, &runtime->n_peer_candidates))
    {
        json_value_free(response);
        redp2p_set_error(ctx, "connect: malformed lookup response");
        return REDP2P_EPROTO;
    }
    proto_number = json_object_get_number(out, "proto");
    proto = (int)proto_number;
    if ((double)proto != proto_number ||
        (proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP))
    {
        json_value_free(response);
        redp2p_set_error(ctx, "connect: invalid publisher protocol");
        return REDP2P_EPROTO;
    }
    ctx->proto = proto;
    json_value_free(response);
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Opens the application-facing local port after the publisher protocol is
 * learned from the index.
 */
static int redp2p_consumer_open_local(redp2p_consumer_runtime_t *runtime)
{
    if (runtime->ctx->proto == REDP2P_PROTO_UDP) {
        runtime->local_fd = redp2p_create_socket("127.0.0.1",
            runtime->ctx->bind_port);
        if (REDP2P_ISERR(runtime->local_fd)) {
            redp2p_set_error(runtime->ctx, "connect: local UDP bind failed");
            return REDP2P_ENET;
        }
        return REDP2P_OK;
    }

    runtime->local_fd = redp2p_create_socket(runtime->udp_any_host, 0);
    if (REDP2P_ISERR(runtime->local_fd)) {
        redp2p_set_error(runtime->ctx,
            "connect: peer UDP socket setup failed");
        return REDP2P_ENET;
    }
    runtime->tcp_listen_fd = redp2p_create_tcp_listener("127.0.0.1",
        runtime->ctx->bind_port);
    if (REDP2P_ISERR(runtime->tcp_listen_fd)) {
        redp2p_set_error(runtime->ctx,
            "connect: local TCP bind/listen failed");
        return REDP2P_ENET;
    }
    return REDP2P_OK;
}

/**
 * Builds the consumer read set while closing unrepresentable sessions.
 * @param runtime Consumer runtime containing all descriptors.
 * @param fds Output descriptor set.
 * @param maxfd Output highest descriptor.
 * @return 1 when selectable, 0 after a session close, or -1 on fatal error.
 */
static int redp2p_consumer_fdset_build(
redp2p_consumer_runtime_t *runtime,
fd_set *fds,
int *maxfd)
{
    int failed;
    int i;

    FD_ZERO(fds);
    *maxfd = -1;
    if (!redp2p_fdset_add(runtime->local_fd, fds, maxfd)) {
        redp2p_set_error(runtime->ctx,
            "connect: local descriptor cannot be represented by fd_set");
        return -1;
    }
    if (!REDP2P_ISERR(runtime->tcp_listen_fd) &&
        !redp2p_fdset_add(runtime->tcp_listen_fd, fds, maxfd))
    {
        redp2p_set_error(runtime->ctx,
            "connect: listener cannot be represented by fd_set");
        return -1;
    }

    failed = 0;
    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) continue;
        if (!redp2p_fdset_add(runtime->sessions[i].fd, fds, maxfd)) {
            redp2p_set_error(runtime->ctx,
                "connect: peer descriptor cannot be represented by fd_set");
            if (runtime->sessions[i].is_tcp) {
                redp2p_stream_fail(runtime->ctx, &runtime->sessions[i].stream);
            }
            redp2p_consumer_session_close(&runtime->sessions[i]);
            failed = 1;
            continue;
        }
        if (!runtime->sessions[i].is_tcp ||
            runtime->sessions[i].tcp_fd == REDP2P_FD_INVALID ||
            !redp2p_stream_can_send_data(&runtime->sessions[i].stream))
            continue;
        if (!redp2p_fdset_add(runtime->sessions[i].tcp_fd, fds, maxfd)) {
            redp2p_set_error(runtime->ctx,
                "connect: client descriptor cannot be represented by fd_set");
            redp2p_stream_fail(runtime->ctx, &runtime->sessions[i].stream);
            redp2p_consumer_session_close(&runtime->sessions[i]);
            failed = 1;
        }
    }
    return failed ? 0 : 1;
}

/**
 * Accepts and establishes one pending local TCP client.
 * @param runtime Consumer runtime owning accepted sessions.
 * @param fds Selected descriptor set.
 * @return 1 when the current event iteration must end, 0 otherwise.
 */
static int redp2p_consumer_tcp_accept(
redp2p_consumer_runtime_t *runtime,
const fd_set *fds)
{
    unsigned char session_bin[REDP2P_SESSION_ID_SZ];
    char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1];
    struct sockaddr_storage peer_addr;
    redp2p_udp_consumer_session_t session;
    redp2p_fd_t client_fd;
    redp2p_fd_t peer_fd;
    int skip_iteration;

    if (REDP2P_ISERR(runtime->tcp_listen_fd) ||
        !FD_ISSET(runtime->tcp_listen_fd, fds))
        return 0;
    client_fd = accept(runtime->tcp_listen_fd, NULL, NULL);
    if (REDP2P_ISERR(client_fd)) return 0;
    if (redp2p_consumer_establish_peer(runtime, 1, &peer_fd, &peer_addr,
        session_bin, session_hex, &skip_iteration) != REDP2P_OK)
    {
        REDP2P_FD_CLOSE(client_fd);
        return skip_iteration;
    }
    if (redp2p_consumer_tcp_session_init(runtime, &session, peer_fd, &peer_addr,
        client_fd, session_bin, session_hex) != 0)
    {
        REDP2P_FD_CLOSE(peer_fd);
        REDP2P_FD_CLOSE(client_fd);
        crypto_wipe(session_bin, sizeof(session_bin));
        crypto_wipe(session_hex, sizeof(session_hex));
        return 1;
    }
    crypto_wipe(session_bin, sizeof(session_bin));
    crypto_wipe(session_hex, sizeof(session_hex));
    return redp2p_consumer_session_insert(runtime, &session) < 0 ? 1 : 0;
}

/**
 * Receives one local UDP datagram and finds or creates its peer session.
 * @param runtime Consumer runtime owning UDP sessions.
 * @param fds Selected descriptor set.
 * @return 1 when processing may continue, or 0 after oversized input.
 */
static int redp2p_consumer_udp_receive(
redp2p_consumer_runtime_t *runtime,
const fd_set *fds)
{
    unsigned char session_bin[REDP2P_SESSION_ID_SZ];
    char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1];
    char buf[REDP2P_BUF];
    struct sockaddr_storage from;
    struct sockaddr_storage peer_addr;
    redp2p_udp_consumer_session_t session;
    redp2p_fd_t peer_fd;
    socklen_t fromlen;
    int found;
    int n;

    if (!REDP2P_ISERR(runtime->tcp_listen_fd) ||
        !FD_ISSET(runtime->local_fd, fds))
        return 1;
    fromlen = sizeof(from);
    n = (int)recvfrom(runtime->local_fd, buf, sizeof(buf), 0,
        (struct sockaddr *)&from, &fromlen);
    if (n < 0) return 1;
    if ((size_t)n > REDP2P_UDP_PAYLOAD_MAX) {
        redp2p_set_error(runtime->ctx,
            "UDP datagram exceeds maximum payload size");
        return 0;
    }

    found = redp2p_consumer_session_find(runtime, &from);
    if (found < 0 && redp2p_consumer_establish_peer(runtime, 0, &peer_fd,
        &peer_addr, session_bin, session_hex, NULL) == REDP2P_OK)
    {
        redp2p_consumer_udp_session_init(&session, peer_fd, &peer_addr, &from);
        crypto_wipe(session_bin, sizeof(session_bin));
        crypto_wipe(session_hex, sizeof(session_hex));
        found = redp2p_consumer_session_insert(runtime, &session);
    }
    if (found >= 0) {
        redp2p_udp_send(runtime->sessions[found].fd,
            &runtime->sessions[found].peer_addr,
            REDP2P_SESSION_ROLE_INITIATOR, REDP2P_SESSION_TYPE_DATA,
            buf, (size_t)n);
        runtime->sessions[found].last_rx = redp2p_now_s();
    }
    return 1;
}

/**
 * Pumps readable local TCP clients into their peer streams.
 * @param runtime Consumer runtime containing TCP sessions.
 * @param fds Selected descriptor set.
 * @return None.
 */
static void redp2p_consumer_tcp_pump(
redp2p_consumer_runtime_t *runtime,
const fd_set *fds)
{
    int i;

    if (REDP2P_ISERR(runtime->tcp_listen_fd)) return;
    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) continue;
        if (!FD_ISSET(runtime->sessions[i].tcp_fd, fds)) continue;
        if (redp2p_stream_pump_tcp(runtime->ctx,
            &runtime->sessions[i].stream, runtime->sessions[i].tcp_fd) != 0)
        {
            redp2p_stream_fail(runtime->ctx, &runtime->sessions[i].stream);
            redp2p_consumer_session_close(&runtime->sessions[i]);
        }
    }
}

/**
 * Receives selected peer packets in session-table order.
 * @param runtime Consumer runtime containing peer sessions.
 * @param fds Selected descriptor set.
 * @return None.
 */
static void redp2p_consumer_peer_receive(
redp2p_consumer_runtime_t *runtime,
const fd_set *fds)
{
    int i;

    for (i = 0; i < runtime->n_sessions; i++) {
        char buf[REDP2P_BUF];
        struct sockaddr_storage from;
        socklen_t fromlen;
        int n;
        redp2p_session_envelope_t envelope;

        if (!runtime->sessions[i].active) continue;
        if (!FD_ISSET(runtime->sessions[i].fd, fds)) continue;
        fromlen = sizeof(from);
        n = (int)recvfrom(runtime->sessions[i].fd, buf, sizeof(buf), 0,
            (struct sockaddr *)&from, &fromlen);
        if (n < 0) continue;
        if (!redp2p_sockaddr_equal(&from, &runtime->sessions[i].peer_addr))
            continue;
        if (!runtime->sessions[i].is_tcp) {
            if (!redp2p_session_unpack((const unsigned char *)buf, (size_t)n,
                &envelope) || !redp2p_udp_envelope_valid(&envelope,
                REDP2P_SESSION_ROLE_RESPONDER))
                continue;
            if (envelope.type == REDP2P_SESSION_TYPE_KEEPALIVE) {
                runtime->sessions[i].last_rx = redp2p_now_s();
                continue;
            }
        }
        if (runtime->sessions[i].is_tcp) {
            if (runtime->sessions[i].tcp_fd != REDP2P_FD_INVALID &&
                redp2p_stream_process_packet(runtime->ctx,
                    &runtime->sessions[i].stream,
                    runtime->sessions[i].tcp_fd,
                    (const unsigned char *)buf, (size_t)n) != 0)
            {
                redp2p_stream_fail(runtime->ctx,
                    &runtime->sessions[i].stream);
                redp2p_consumer_session_close(&runtime->sessions[i]);
            }
        } else {
            redp2p_sendto_addr(runtime->local_fd, envelope.payload,
                envelope.payload_len, &runtime->sessions[i].client_addr);
        }
        runtime->sessions[i].last_rx = redp2p_now_s();
    }
}

/**
 * Advances stream state, keepalives, and idle expiry in table order.
 * @param runtime Consumer runtime containing active sessions.
 * @return None.
 */
static void redp2p_consumer_session_maintain(
redp2p_consumer_runtime_t *runtime)
{
    int i;

    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active) continue;
        if (runtime->sessions[i].is_tcp) {
            if (redp2p_stream_tick(runtime->ctx,
                &runtime->sessions[i].stream) != 0)
            {
                redp2p_stream_fail(runtime->ctx,
                    &runtime->sessions[i].stream);
                redp2p_consumer_session_close(&runtime->sessions[i]);
                continue;
            }
            if (redp2p_stream_is_done(&runtime->sessions[i].stream)) {
                redp2p_consumer_session_close(&runtime->sessions[i]);
                continue;
            }
        }
        if (redp2p_now_s() - runtime->sessions[i].last_ka >
            REDP2P_KEEPALIVE_S)
        {
            if (!runtime->sessions[i].is_tcp) {
                redp2p_udp_send(runtime->sessions[i].fd,
                    &runtime->sessions[i].peer_addr,
                    REDP2P_SESSION_ROLE_INITIATOR,
                    REDP2P_SESSION_TYPE_KEEPALIVE, NULL, 0);
            }
            runtime->sessions[i].last_ka = redp2p_now_s();
        }
        if (redp2p_now_s() - runtime->sessions[i].last_rx >
            REDP2P_DISCONNECT_S)
        {
            redp2p_consumer_session_close(&runtime->sessions[i]);
        }
    }
}

/**
 * Finds the nearest consumer-side KCP deadline.
 * @return Milliseconds until select should wake, capped at one second.
 */
static uint32_t redp2p_consumer_wait_ms(
    const redp2p_consumer_runtime_t *runtime)
{
    uint32_t wait_ms;
    uint32_t session_wait;
    uint64_t now;
    int i;

    wait_ms = 1000;
    now = redp2p_now_ms();
    for (i = 0; i < runtime->n_sessions; i++) {
        if (!runtime->sessions[i].active || !runtime->sessions[i].is_tcp)
            continue;
        session_wait = redp2p_stream_wait_ms(&runtime->sessions[i].stream, now);
        if (session_wait < wait_ms) wait_ms = session_wait;
    }
    return wait_ms;
}

/**
 * Runs the consumer event loop with the existing phase ordering.
 * @param runtime Initialized and target-validated consumer runtime.
 * @return REDP2P_OK on stop, or REDP2P_ENET on fatal descriptor failure.
 */
static int redp2p_consumer_loop(redp2p_consumer_runtime_t *runtime) {
    int result;

    result = REDP2P_OK;
    redp2p_set_nonblock(runtime->local_fd);
    if (!REDP2P_ISERR(runtime->tcp_listen_fd))
        redp2p_set_nonblock(runtime->tcp_listen_fd);

    while (!runtime->ctx->stop_requested) {
        fd_set fds;
        struct timeval tv;
        int fdset_result;
        int maxfd;
        int selected;
        uint32_t wait_ms;

        fdset_result = redp2p_consumer_fdset_build(runtime, &fds, &maxfd);
        if (fdset_result < 0) {
            result = REDP2P_ENET;
            break;
        }
        if (fdset_result == 0) continue;
        wait_ms = redp2p_consumer_wait_ms(runtime);
        tv.tv_sec = (long)(wait_ms / 1000u);
        tv.tv_usec = (long)((wait_ms % 1000u) * 1000u);
        selected = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (selected < 0) continue;
        if (runtime->ctx->stop_requested) break;

        if (redp2p_consumer_tcp_accept(runtime, &fds)) {
            redp2p_consumer_session_maintain(runtime);
            continue;
        }
        if (!REDP2P_ISERR(runtime->tcp_listen_fd)) {
            redp2p_consumer_tcp_pump(runtime, &fds);
        } else if (!redp2p_consumer_udp_receive(runtime, &fds)) {
            redp2p_consumer_session_maintain(runtime);
            continue;
        }
        redp2p_consumer_peer_receive(runtime, &fds);
        redp2p_consumer_session_maintain(runtime);
    }
    return result;
}

/**
 * Releases every resource owned by one consumer runtime.
 * @param runtime Consumer runtime to release.
 * @param reset_stop Non-zero consumes the context stop request.
 * @return None.
 */
static void redp2p_consumer_runtime_cleanup(
redp2p_consumer_runtime_t *runtime,
int reset_stop)
{
    redp2p_consumer_session_close_all(runtime);
    if (runtime->sessions) {
        crypto_wipe(runtime->sessions,
            (size_t)runtime->cap_sessions * sizeof(*runtime->sessions));
    }
    free(runtime->sessions);
    runtime->sessions = NULL;
    runtime->n_sessions = 0;
    runtime->cap_sessions = 0;
    if (!REDP2P_ISERR(runtime->local_fd)) {
        REDP2P_FD_CLOSE(runtime->local_fd);
        runtime->local_fd = REDP2P_FD_INVALID;
    }
    if (!REDP2P_ISERR(runtime->tcp_listen_fd)) {
        REDP2P_FD_CLOSE(runtime->tcp_listen_fd);
        runtime->tcp_listen_fd = REDP2P_FD_INVALID;
    }
    if (runtime->platform_initialized) {
        redp2p_platform_cleanup();
        runtime->platform_initialized = 0;
    }
    if (reset_stop) atomic_store(&runtime->ctx->stop_requested, 0);
}

/**
 * Runs the consumer lifecycle for one local edge adapter.
 * @return Existing public REDP2P result code.
 */
int redp2p_connect(
redp2p_t *ctx,
const char *index_host,
unsigned short index_port,
const char *self_id,
const char *target_id,
unsigned short bind_port)
{
    redp2p_consumer_runtime_t runtime;
    int loop_ran;
    int result;
    int should_run;

    if (!ctx) return REDP2P_EINVAL;
    loop_ran = 0;

    result = redp2p_consumer_runtime_init(&runtime, ctx, index_host, index_port,
        self_id, target_id, bind_port, &should_run);
    if (result != REDP2P_OK || !should_run) {
        atomic_store(&ctx->ready_status, result);
        atomic_store(&ctx->ready_state, result == REDP2P_OK ? 1 : -1);
        redp2p_consumer_runtime_cleanup(&runtime, 0);
        return result;
    }
    result = redp2p_consumer_initial_lookup(&runtime);
    if (result == REDP2P_OK)
        result = redp2p_consumer_open_local(&runtime);
    if (result == REDP2P_OK) {
        atomic_store(&ctx->ready_status, REDP2P_OK);
        atomic_store(&ctx->ready_state, 1);
        loop_ran = 1;
        result = redp2p_consumer_loop(&runtime);
    } else {
        atomic_store(&ctx->ready_status, result);
        atomic_store(&ctx->ready_state, -1);
    }
    redp2p_consumer_runtime_cleanup(&runtime, loop_ran);
    return result;
}

#ifdef REDP2P_TEST_RANDOM
/**
 * Generates one stream session identifier through the test-visible path.
 * @param out Output binary identifier.
 * @param hex Output hex identifier.
 * @return 1 on success, 0 on error.
 */
int redp2p_test_stream_make_session_id(
unsigned char out[REDP2P_SESSION_ID_SZ],
char hex[REDP2P_SESSION_ID_SZ * 2 + 1]
)
{
    return redp2p_stream_make_session_id(out, hex);
}
#endif
