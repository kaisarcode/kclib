/**
 * libredp2p-peer.h - REDP2P.
 * Summary: Shared direct-peer transport, discovery and HTTP client.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef REDP2P_PEER_H
#define REDP2P_PEER_H

#include "libredp2p-core.h"

typedef struct json_value_t JSON_Value;

#define REDP2P_DISCONNECT_S     10
#define REDP2P_KEEPALIVE_S       3
#define REDP2P_SESSION_ID_SZ     16
#define REDP2P_SESSION_ENVELOPE_SZ       24
#define REDP2P_STREAM_KCP_MTU           1400
#define REDP2P_STREAM_MAX_FRAME         (REDP2P_SESSION_ENVELOPE_SZ + REDP2P_STREAM_KCP_MTU)
#define REDP2P_CTRTOK_PUNCH_PING   "REDP2P_CTRTOK_PUNCH_PING:"
#define REDP2P_CTRTOK_PUNCH_PONG   "REDP2P_CTRTOK_PUNCH_PONG:"
#define REDP2P_SESSION_TYPE_DATA        3u
#define REDP2P_SESSION_TYPE_KEEPALIVE  7u
#define REDP2P_SESSION_ROLE_INITIATOR 1u
#define REDP2P_SESSION_ROLE_RESPONDER 2u

typedef struct {
    uint8_t type;
    uint8_t role;
    uint8_t protocol;
    unsigned char session_id[REDP2P_SESSION_ID_SZ];
    const unsigned char *payload;
    size_t payload_len;
} redp2p_session_envelope_t;

typedef struct {
    redp2p_t *ctx;
    redp2p_fd_t fd;
    struct sockaddr_storage peer_addr;
    unsigned char session_id[REDP2P_SESSION_ID_SZ];
    uint8_t role;
    uint8_t protocol;
    int send_error;
    int fault_pending_used;
    size_t fault_pending_len;
    unsigned char fault_pending[REDP2P_STREAM_MAX_FRAME];
} redp2p_stream_adapter_t;

typedef struct {
    int enabled;
    int initiator;
    int ready;
    int hello_sent;
    int reset_sent;
    int reset_received;
    int local_eof;
    int close_sent;
    int close_acked;
    int remote_close;
    unsigned char session_id[REDP2P_SESSION_ID_SZ];
    char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1];
    uint8_t transport_protocol;
    struct IKCPCB *kcp;
    redp2p_stream_adapter_t *adapter;
    uint64_t last_tx_ms;
    uint64_t last_hello_ms;
    uint64_t last_close_ms;
    uint64_t last_keepalive_ms;
    uint32_t next_update_ms;
} redp2p_stream_state_t;

/**
 * Decodes the common structure of one REDP2P session envelope.
 * @return 1 on success, 0 when the datagram is not a valid envelope.
 */
REDP2P_INTERNAL int redp2p_session_unpack(const unsigned char *buf, size_t len,
    redp2p_session_envelope_t *envelope);

/**
 * Sends one UDP session envelope with the reserved session field zeroed.
 * @return Sent byte count, or -1 on invalid payload or socket failure.
 */
REDP2P_INTERNAL int redp2p_udp_send(redp2p_fd_t fd,
const struct sockaddr_storage *peer, uint8_t role, uint8_t type,
const void *payload, size_t payload_len);

/**
 * Validates UDP session types and payload bounds after common decoding.
 * @return 1 for eligible UDP DATA or KEEPALIVE, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_udp_envelope_valid(const redp2p_session_envelope_t *envelope,
uint8_t expected_role);

/**
 * Validates TCP session types and payload bounds after common decoding.
 * @return 1 for eligible TCP envelopes, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_stream_envelope_valid(const redp2p_session_envelope_t *envelope);

/**
 * Initializes one conservative stream-mode KCP session and REDP2P adapter.
 * @return 0 on success, -1 on allocation or KCP configuration failure.
 */
REDP2P_INTERNAL int redp2p_stream_init(redp2p_t *ctx, redp2p_stream_state_t *st,
    int initiator, redp2p_fd_t fd,
    const struct sockaddr_storage *peer_addr,
    const unsigned char session_id[REDP2P_SESSION_ID_SZ],
    const char *session_hex, uint8_t transport_protocol);

/**
 * Reports whether the local TCP side may queue more bytes in KCP.
 * @return 1 when local TCP reads may continue, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_stream_can_send_data(const redp2p_stream_state_t *st);

/**
 * Notifies an established peer of terminal stream failure once.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_stream_fail(redp2p_t *ctx, redp2p_stream_state_t *st);

/**
 * Dispatches one validated session datagram through handshake or KCP.
 * @return 0 on success, -1 on protocol, transport, or local socket failure.
 */
REDP2P_INTERNAL int redp2p_stream_process_packet(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t tcp_fd,
    const unsigned char *buf, size_t len);

/**
 * Reads one local TCP chunk and queues its bytes in KCP stream mode.
 * @return 0 on success, -1 on local socket or KCP failure.
 */
REDP2P_INTERNAL int redp2p_stream_pump_tcp(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t tcp_fd);

/**
 * Advances one KCP session and REDP2P tunnel lifecycle when due.
 * @return 0 on success, -1 on transport failure.
 */
REDP2P_INTERNAL int redp2p_stream_tick(redp2p_t *ctx, redp2p_stream_state_t *st);

/**
 * Returns the next KCP update delay for select scheduling.
 * @return Milliseconds until work is due, capped at one second.
 */
REDP2P_INTERNAL uint32_t redp2p_stream_wait_ms(const redp2p_stream_state_t *st,
    uint64_t now);

/**
 * Reports whether one TCP stream is fully closed on both sides.
 * @return 1 when the stream may be cleaned up, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_stream_is_done(const redp2p_stream_state_t *st);

/**
 * Releases KCP and wipes all per-session stream material.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_stream_wipe(redp2p_stream_state_t *st);

/**
 * Sends bytes to one stored socket address.
 * @param fd   UDP socket.
 * @param buf  Bytes to send.
 * @param len  Byte count.
 * @param addr Destination address.
 * @return sendto result, or -1 for unsupported address families.
 */
REDP2P_INTERNAL int redp2p_sendto_addr(redp2p_fd_t fd, const void *buf, size_t len,
    const struct sockaddr_storage *addr);

/**
 * Reports whether a host string is an IPv6 literal.
 * @param host Host string.
 * @return 1 for IPv6 literals, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_host_is_ipv6_literal(const char *host);

/**
 * Creates one UDP or TCP socket bound to one local address.
 * @return Valid descriptor, or REDP2P_FD_INVALID on failure.
 */
REDP2P_INTERNAL redp2p_fd_t redp2p_create_socket(
    const char *bind_host,
    unsigned short bind_port);

/**
 * Issues one HTTP/1.1 JSON request to the index and parses its reply.
 * @param ctx           Context for error details, or NULL.
 * @param phase         Diagnostic phase prefix.
 * @param host          Index host.
 * @param port          Index port.
 * @param request       Request JSON value, serialized but not freed here.
 * @param response_out  Optional output JSON value owned by the caller.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
REDP2P_INTERNAL int redp2p_http_client(redp2p_t *ctx, const char *phase,
    const char *host, unsigned short port, JSON_Value *request,
    JSON_Value **response_out);

/**
 * Resolves the effective local port for pub or con.
 * Summary: A nonzero argument overrides a default context port, while a nonzero
 * argument conflicting with an explicitly set context port is rejected.
 * @param ctx        Open context.
 * @param arg_port  Port argument from redp2p_wait or redp2p_connect.
 * @param out        Effective port output.
 * @return REDP2P_OK on success, REDP2P_ERROR on conflicting ports.
 */
REDP2P_INTERNAL int redp2p_resolve_port(redp2p_t *ctx, unsigned short arg_port,
    unsigned short *out);

/**
 * Parses a UDP punch packet.
 * @param text     Input packet text.
 * @param prefix   Required packet prefix.
 * @param sess_id  Output session id.
 * @param from_id  Output source id.
 * @param to_id    Output destination id.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_parse_punch_packet(const char *text, const char *prefix,
    char sess_id[REDP2P_CTRL_SESSION_MAX + 1],
    char from_id[REDP2P_ID_MAX + 1], char to_id[REDP2P_ID_MAX + 1]);

/**
 * Gather candidates.
 * Summary: Gathers candidates for hole punching.
 * @param udp_fd     UDP socket fd.
 * @param index_host Index hostname.
 * @param index_port Index port.
 * @param out        Output array.
 * @param out_cap    Array capacity.
 * @param out_count  Output count.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_gather_candidates(redp2p_t *ctx, int udp_fd,
    redp2p_candidate_t *out, int out_cap, int *out_count);

/**
 * Punch select.
 * Summary: Selects a candidate and performs hole punching.
 * @param ctx                   Context.
 * @param udp_fd                 UDP socket fd.
 * @param session_id             Session ID.
 * @param from_id                From peer ID.
 * @param to_id                  To peer ID.
 * @param remote_candidates      Remote candidate array.
 * @param remote_candidate_count Remote candidate count.
 * @param selected_addr          Selected output address.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_punch_select(redp2p_t *ctx, int sweep_limit, int udp_fd, const char *session_id, const char *from_id, const char *to_id, const redp2p_candidate_t *remote_candidates, int remote_candidate_count, struct sockaddr_storage *selected_addr);

#endif
