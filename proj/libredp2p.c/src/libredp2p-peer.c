/**
 * libredp2p-peer.c - REDP2P.
 * Summary: Shared direct-peer transport, discovery and HTTP client.
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

#include "ikcp.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/time.h>
#endif

#define REDP2P_PUNCH_ATTEMPTS   10
#define REDP2P_PUNCH_INTERVAL_MS 200
#define REDP2P_SESSION_MAGIC             0x50434b52u
#define REDP2P_SESSION_VERSION           2u
#define REDP2P_STREAM_SEND_WINDOW       64
#define REDP2P_STREAM_RECV_WINDOW       128
#define REDP2P_STREAM_KCP_INTERVAL_MS   20
#define REDP2P_STREAM_KCP_FAST_RESEND   2
#define REDP2P_STREAM_MAX_WAIT_SEND     128
#define REDP2P_STREAM_HELLO_MS          500
#define REDP2P_STREAM_CLOSE_MS          500
#define REDP2P_LINK_MTU                 1500
#define REDP2P_IPV4_UDP_OVERHEAD        (20 + 8)
#define REDP2P_IPV6_UDP_OVERHEAD        (40 + 8)
#define REDP2P_MAX_DATAGRAM_V4          (REDP2P_LINK_MTU - REDP2P_IPV4_UDP_OVERHEAD)
#define REDP2P_MAX_DATAGRAM_V6          (REDP2P_LINK_MTU - REDP2P_IPV6_UDP_OVERHEAD)
#define REDP2P_PUNCH_DIRECT_ROUNDS 200
#define REDP2P_PUNCH_DIRECT_WAIT_MS 100
#define REDP2P_PUNCH_CONFIRM_BURST 3
#define REDP2P_PUNCH_SWEEP_WAIT_MS 20
#define REDP2P_PUNCH_TOTAL_MS 20000
#define REDP2P_STUN_KEEPALIVE_MS 10000
#define REDP2P_SESSION_TYPE_HELLO      1u
#define REDP2P_SESSION_TYPE_HELLO_ACK  2u
#define REDP2P_SESSION_TYPE_CLOSE      4u
#define REDP2P_SESSION_TYPE_CLOSE_ACK  5u
#define REDP2P_SESSION_TYPE_RESET      6u
#define REDP2P_STUN_ATTR_DATA 0x0013

_Static_assert(REDP2P_STREAM_MAX_FRAME <= REDP2P_MAX_DATAGRAM_V4,
    "Stream frame exceeds IPv4 datagram limit");
_Static_assert(REDP2P_STREAM_MAX_FRAME <= REDP2P_MAX_DATAGRAM_V6,
    "Stream frame exceeds IPv6 datagram limit");
_Static_assert(REDP2P_SESSION_ENVELOPE_SZ + REDP2P_UDP_PAYLOAD_MAX <=
    REDP2P_MAX_DATAGRAM_V6, "UDP session frame exceeds IPv6 datagram limit");
_Static_assert(REDP2P_SESSION_ENVELOPE_SZ + REDP2P_UDP_PAYLOAD_MAX <=
    REDP2P_MAX_DATAGRAM_V4, "UDP session frame exceeds IPv4 datagram limit");

#ifdef _WIN32
typedef HANDLE redp2p_thread_t;
#define REDP2P_THREAD_RET unsigned long __stdcall
#else
typedef pthread_t redp2p_thread_t;
#define REDP2P_THREAD_RET void *
#endif

/**
 * Returns the socket address length for a stored address family.
 * @param addr Stored socket address.
 * @return Socket address length, or 0 for unsupported families.
 */
static socklen_t redp2p_sockaddr_len(const struct sockaddr_storage *addr);

/**
 * Reports whether direct-punch tracing is enabled.
 * @return 1 when REDP2P_PUNCH_TRACE=1, otherwise 0.
 */
static int redp2p_punch_trace_enabled(void)
{
    const char *value;

    value = getenv("REDP2P_PUNCH_TRACE");
    return value && strcmp(value, "1") == 0;
}

/**
 * Formats one socket endpoint for punch diagnostics.
 * @param addr Endpoint to format.
 * @param out Output text.
 * @param out_cap Output capacity.
 * @return None.
 */
static void redp2p_punch_trace_addr(const struct sockaddr_storage *addr,
    char *out, size_t out_cap)
{
    char host[INET6_ADDRSTRLEN];
    unsigned short port;

    if (!out || out_cap == 0) return;
    snprintf(out, out_cap, "?");
    if (!addr) return;
    host[0] = '\0';
    port = redp2p_sockaddr_port(addr);
    if (addr->ss_family == AF_INET) {
        if (!inet_ntop(AF_INET,
            &((const struct sockaddr_in *)addr)->sin_addr,
            host, sizeof(host)))
            return;
        snprintf(out, out_cap, "%s:%u", host, (unsigned)port);
        return;
    }
    if (addr->ss_family == AF_INET6) {
        if (!inet_ntop(AF_INET6,
            &((const struct sockaddr_in6 *)addr)->sin6_addr,
            host, sizeof(host)))
            return;
        snprintf(out, out_cap, "[%s]:%u", host, (unsigned)port);
    }
}

/**
 * Writes one candidate to stderr when punch tracing is enabled.
 * @param label Diagnostic label.
 * @param candidate Candidate to print.
 * @return None.
 */
static void redp2p_punch_trace_candidate(const char *label,
    const redp2p_candidate_t *candidate)
{
    const char *type;

    if (!redp2p_punch_trace_enabled() || !candidate) return;
    if (candidate->type == REDP2P_CAND_SRFLX) type = "srflx";
    else if (candidate->type == REDP2P_CAND_HOST) type = "host";
    else if (candidate->type == REDP2P_CAND_OBSERVED) type = "observed";
    else if (candidate->type == REDP2P_CAND_RELAY) type = "relay";
    else type = "unknown";
    fprintf(stderr, "[PUNCH] %s type=%s endpoint=%s:%u priority=%u\n",
        label ? label : "candidate", type, candidate->addr,
        (unsigned)candidate->port, candidate->priority);
}

/**
 * Shuts down the write side of one local stream-adapter socket.
 * @return None.
 */
static void redp2p_adapter_shutdown_write(redp2p_fd_t fd);

#ifdef REDP2P_TESTING
/**
 * Decides whether to drop one complete outgoing KCP datagram.
 * Summary: Part of the transport fault simulation mechanism. Cadences are
 * context state configured only through the test-visible path; they stay
 * zero in normal operation, so the send path is a single comparison.
 * @param ctx Context holding the fault cadence and counter.
 * @return 1 when the datagram should be dropped, 0 otherwise.
 */
static int redp2p_stream_should_drop(redp2p_t *ctx) {
    if (ctx->fault_drop_every <= 0) return 0;
    ctx->fault_drop_counter++;
    return (ctx->fault_drop_counter % ctx->fault_drop_every) == 0;
}

/**
 * Decides whether to delay one complete KCP datagram to force reordering.
 * Summary: Part of the transport fault simulation mechanism. See
 * redp2p_stream_should_drop().
 * @param ctx Context holding the fault cadence and counter.
 * @return 1 when the datagram should be delayed, 0 otherwise.
 */
static int redp2p_stream_should_reorder(redp2p_t *ctx) {
    if (ctx->fault_reorder_every <= 0) return 0;
    ctx->fault_reorder_counter++;
    return (ctx->fault_reorder_counter % ctx->fault_reorder_every) == 0;
}

#else
#define redp2p_stream_should_drop(ctx) 0
#define redp2p_stream_should_reorder(ctx) 0
#endif

/**
 * Loads one little-endian u32.
 * @return Decoded value.
 */
static uint32_t redp2p_load_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

/**
 * Stores one little-endian u32.
 * @return None.
 */
static void redp2p_store_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

/**
 * Encodes one REDP2P session envelope around an opaque payload.
 * @return Encoded byte length, or 0 when an argument is invalid.
 */
static size_t redp2p_session_pack(uint8_t type, uint8_t role, uint8_t protocol,
    const unsigned char session_id[REDP2P_SESSION_ID_SZ],
    const void *payload, size_t payload_len, unsigned char *out)
{
    if (!out || !session_id || (payload_len > 0 && !payload) ||
        (role != REDP2P_SESSION_ROLE_INITIATOR &&
        role != REDP2P_SESSION_ROLE_RESPONDER) ||
        (protocol != REDP2P_PROTO_TCP && protocol != REDP2P_PROTO_UDP) ||
        type < REDP2P_SESSION_TYPE_HELLO ||
        type > REDP2P_SESSION_TYPE_KEEPALIVE)
        return 0;
    if (payload_len > REDP2P_UDP_PAYLOAD_MAX) return 0;
    redp2p_store_u32_le(out, REDP2P_SESSION_MAGIC);
    out[4] = REDP2P_SESSION_VERSION;
    out[5] = type;
    out[6] = role;
    out[7] = protocol;
    memcpy(out + 8, session_id, REDP2P_SESSION_ID_SZ);
    if (payload_len > 0) memcpy(out + REDP2P_SESSION_ENVELOPE_SZ, payload,
        payload_len);
    return REDP2P_SESSION_ENVELOPE_SZ + payload_len;
}

/**
 * Decodes the common structure of one REDP2P session envelope.
 * @return 1 on success, 0 when the datagram is not a valid envelope.
 */
int redp2p_session_unpack(const unsigned char *buf, size_t len,
    redp2p_session_envelope_t *envelope)
{
    if (!buf || !envelope || len < REDP2P_SESSION_ENVELOPE_SZ)
        return 0;
    if (redp2p_load_u32_le(buf) != REDP2P_SESSION_MAGIC ||
        buf[4] != REDP2P_SESSION_VERSION ||
        (buf[6] != REDP2P_SESSION_ROLE_INITIATOR &&
        buf[6] != REDP2P_SESSION_ROLE_RESPONDER) ||
        (buf[7] != REDP2P_PROTO_TCP && buf[7] != REDP2P_PROTO_UDP))
        return 0;
    envelope->type = buf[5];
    envelope->role = buf[6];
    envelope->protocol = buf[7];
    memcpy(envelope->session_id, buf + 8, REDP2P_SESSION_ID_SZ);
    envelope->payload = buf + REDP2P_SESSION_ENVELOPE_SZ;
    envelope->payload_len = len - REDP2P_SESSION_ENVELOPE_SZ;
    return 1;
}

/**
 * Sends one UDP session envelope with the reserved session field zeroed.
 * @return Sent byte count, or -1 on invalid payload or socket failure.
 */
int redp2p_udp_send(redp2p_t *ctx, redp2p_fd_t fd,
const struct sockaddr_storage *peer, int via_turn, uint8_t role, uint8_t type,
const void *payload, size_t payload_len)
{
    unsigned char frame[REDP2P_SESSION_ENVELOPE_SZ + REDP2P_UDP_PAYLOAD_MAX];
    const unsigned char session_id[REDP2P_SESSION_ID_SZ] = {0};
    size_t len;

    len = redp2p_session_pack(type, role, REDP2P_PROTO_UDP, session_id,
        payload, payload_len, frame);
    if (!len) return -1;
    return redp2p_transport_sendto(ctx, fd, frame, len, peer, via_turn);
}

/**
 * Validates UDP session types and payload bounds after common decoding.
 * @return 1 for eligible UDP DATA or KEEPALIVE, 0 otherwise.
 */
int redp2p_udp_envelope_valid(const redp2p_session_envelope_t *envelope,
uint8_t expected_role)
{
    return envelope->protocol == REDP2P_PROTO_UDP &&
        envelope->role == expected_role &&
        ((envelope->type == REDP2P_SESSION_TYPE_DATA &&
        envelope->payload_len <= REDP2P_UDP_PAYLOAD_MAX) ||
        (envelope->type == REDP2P_SESSION_TYPE_KEEPALIVE &&
        envelope->payload_len == 0));
}

/**
 * Validates TCP session types and payload bounds after common decoding.
 * @return 1 for eligible TCP envelopes, 0 otherwise.
 */
int redp2p_stream_envelope_valid(const redp2p_session_envelope_t *envelope)
{
    return envelope->protocol == REDP2P_PROTO_TCP &&
        envelope->payload_len <= REDP2P_STREAM_KCP_MTU &&
        envelope->type >= REDP2P_SESSION_TYPE_HELLO &&
        envelope->type <= REDP2P_SESSION_TYPE_KEEPALIVE &&
        (envelope->type == REDP2P_SESSION_TYPE_DATA ?
        envelope->payload_len > 0 : envelope->payload_len == 0);
}

/**
 * Sends one already encoded stream datagram.
 * @return 0 on success, -1 on socket failure.
 */
static int redp2p_stream_send_datagram(redp2p_stream_adapter_t *adapter,
    const unsigned char *frame, size_t frame_len)
{
    if (!adapter) return -1;
    return redp2p_transport_sendto(adapter->ctx, adapter->fd, frame, frame_len,
        &adapter->peer_addr, adapter->via_turn) < 0 ? -1 : 0;
}

/**
 * Emits one complete KCP datagram through the REDP2P session envelope.
 * @return 0 on success, -1 after a transport failure.
 */
static int redp2p_stream_kcp_output(const char *buf, int len, ikcpcb *kcp,
    void *user)
{
    redp2p_stream_adapter_t *adapter;
    unsigned char frame[REDP2P_STREAM_MAX_FRAME];
    size_t frame_len;

    (void)kcp;
    adapter = (redp2p_stream_adapter_t *)user;
    if (!adapter || len <= 0 || len > REDP2P_STREAM_KCP_MTU) return -1;
    frame_len = redp2p_session_pack(REDP2P_SESSION_TYPE_DATA, adapter->role,
        adapter->protocol, adapter->session_id, buf, (size_t)len, frame);
    if (frame_len == 0) return -1;
    if (redp2p_stream_should_drop(adapter->ctx)) {
        return 0;
    }
    if (!adapter->fault_pending_used &&
        redp2p_stream_should_reorder(adapter->ctx))
    {
        memcpy(adapter->fault_pending, frame, frame_len);
        adapter->fault_pending_len = frame_len;
        adapter->fault_pending_used = 1;
        return 0;
    }
    if (redp2p_stream_send_datagram(adapter, frame, frame_len) != 0 ||
        (adapter->fault_pending_used &&
        redp2p_stream_send_datagram(adapter, adapter->fault_pending,
            adapter->fault_pending_len) != 0))
    {
        adapter->send_error = 1;
        return -1;
    }
    adapter->fault_pending_used = 0;
    return 0;
}

/**
 * Derives the session-local KCP routing value from the full REDP2P identifier.
 * @return Deterministic 32-bit KCP conversation value.
 */
static uint32_t redp2p_stream_conv(
    const unsigned char session_id[REDP2P_SESSION_ID_SZ])
{
    return redp2p_load_u32_le(session_id) ^ redp2p_load_u32_le(session_id + 4) ^
        redp2p_load_u32_le(session_id + 8) ^
        redp2p_load_u32_le(session_id + 12);
}

/**
 * Initializes one conservative stream-mode KCP session and REDP2P adapter.
 * @return 0 on success, -1 on allocation or KCP configuration failure.
 */
int redp2p_stream_init(redp2p_t *ctx, redp2p_stream_state_t *st,
    int initiator, redp2p_fd_t fd,
    const struct sockaddr_storage *peer_addr,
    const unsigned char session_id[REDP2P_SESSION_ID_SZ],
    const char *session_hex, uint8_t service_protocol, int via_turn)
{
    uint64_t now;

    memset(st, 0, sizeof(*st));
    st->adapter = (redp2p_stream_adapter_t *)calloc(1, sizeof(*st->adapter));
    if (!st->adapter) {
        redp2p_set_error(ctx, "stream: KCP adapter allocation failed");
        return -1;
    }
    st->adapter->ctx = ctx;
    st->adapter->fd = fd;
    st->adapter->peer_addr = *peer_addr;
    st->adapter->role = initiator ? REDP2P_SESSION_ROLE_INITIATOR :
        REDP2P_SESSION_ROLE_RESPONDER;
    st->adapter->protocol = service_protocol;
    st->adapter->via_turn = via_turn ? 1 : 0;
    memcpy(st->adapter->session_id, session_id, REDP2P_SESSION_ID_SZ);
    st->kcp = ikcp_create(redp2p_stream_conv(session_id), st->adapter);
    if (!st->kcp || ikcp_setmtu(st->kcp, via_turn ?
        REDP2P_STREAM_TURN_KCP_MTU : REDP2P_STREAM_KCP_MTU) != 0 ||
        ikcp_wndsize(st->kcp, REDP2P_STREAM_SEND_WINDOW,
            REDP2P_STREAM_RECV_WINDOW) != 0 ||
        ikcp_nodelay(st->kcp, 0, REDP2P_STREAM_KCP_INTERVAL_MS,
            REDP2P_STREAM_KCP_FAST_RESEND, 0) != 0)
    {
        if (st->kcp) ikcp_release(st->kcp);
        free(st->adapter);
        memset(st, 0, sizeof(*st));
        redp2p_set_error(ctx, "stream: KCP initialization failed");
        return -1;
    }
    st->kcp->stream = 1;
    ikcp_setoutput(st->kcp, redp2p_stream_kcp_output);
    st->enabled = 1;
    st->initiator = initiator;
    st->service_protocol = service_protocol;
    memcpy(st->session_id, session_id, REDP2P_SESSION_ID_SZ);
    if (session_hex) memcpy(st->session_hex, session_hex,
        REDP2P_SESSION_ID_SZ * 2 + 1);
    now = redp2p_now_ms();
    st->last_tx_ms = now;
    st->last_keepalive_ms = now;
    st->next_update_ms = (uint32_t)now;
    return 0;
}

/**
 * Reports whether the local stream adapter may queue more bytes in KCP.
 * @return 1 when local TCP reads may continue, 0 otherwise.
 */
int redp2p_stream_can_send_data(const redp2p_stream_state_t *st) {
    return st->ready && !st->adapter_read_eof && st->kcp &&
        ikcp_waitsnd(st->kcp) < REDP2P_STREAM_MAX_WAIT_SEND;
}

/**
 * Sends one REDP2P tunnel control envelope outside KCP.
 * @return 0 on success, -1 on socket failure.
 */
static int redp2p_stream_send_control(redp2p_t *ctx, redp2p_stream_state_t *st,
    uint8_t type)
{
    unsigned char frame[REDP2P_SESSION_ENVELOPE_SZ];
    size_t frame_len;

    frame_len = redp2p_session_pack(type, st->adapter->role,
        st->service_protocol, st->session_id, NULL, 0, frame);
    if (frame_len == 0 ||
        redp2p_stream_send_datagram(st->adapter, frame, frame_len) != 0)
    {
        redp2p_set_error(ctx, "stream: UDP control send failed");
        return -1;
    }
    st->last_tx_ms = redp2p_now_ms();
    return 0;
}

/**
 * Notifies an established peer of terminal stream failure once.
 * @return None.
 */
void redp2p_stream_fail(redp2p_t *ctx, redp2p_stream_state_t *st) {
    if (!st || !st->ready || st->reset_sent || st->reset_received) return;
    st->reset_sent = 1;
    redp2p_stream_send_control(ctx, st, REDP2P_SESSION_TYPE_RESET);
}

/**
 * Flushes at most one reconstructed KCP chunk into the local stream adapter.
 *
 * Partial and would-block writes stay in the stream state. This bounds work
 * per session and prevents one slow adapter from blocking the event loop.
 * @return 0 on success, -1 on stream or socket failure.
 */
int redp2p_stream_flush_adapter(redp2p_t *ctx, redp2p_stream_state_t *st,
    redp2p_fd_t adapter_fd)
{
    int available;
    int received;
    int written;

    if (!st || !st->kcp || adapter_fd == REDP2P_FD_INVALID) return -1;

    if (st->pending_adapter_off < st->pending_adapter_len) {
        written = redp2p_sock_write(adapter_fd,
            (const char *)st->pending_adapter + st->pending_adapter_off,
            (int)(st->pending_adapter_len - st->pending_adapter_off));
        if (written < 0) {
            if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
            redp2p_set_error(ctx, "stream: local adapter write failed");
            return -1;
        }
        if (written == 0) {
            redp2p_set_error(ctx, "stream: local adapter write closed");
            return -1;
        }
        st->pending_adapter_off += (size_t)written;
        if (st->pending_adapter_off < st->pending_adapter_len) return 0;
        st->pending_adapter_off = 0;
        st->pending_adapter_len = 0;
        return 0;
    }

    available = ikcp_peeksize(st->kcp);
    if (available < 0) {
        if (st->peer_adapter_eof && !st->adapter_write_shutdown) {
            redp2p_adapter_shutdown_write(adapter_fd);
            st->adapter_write_shutdown = 1;
        }
        return 0;
    }
    if (available > (int)sizeof(st->pending_adapter)) {
        redp2p_set_error(ctx, "stream: KCP receive chunk exceeds buffer");
        return -1;
    }
    received = ikcp_recv(st->kcp, (char *)st->pending_adapter,
        (int)sizeof(st->pending_adapter));
    if (received < 0) {
        redp2p_set_error(ctx, "stream: KCP receive failed");
        return -1;
    }
    st->pending_adapter_off = 0;
    st->pending_adapter_len = (size_t)received;
    if (received == 0) return 0;

    written = redp2p_sock_write(adapter_fd,
        (const char *)st->pending_adapter, received);
    if (written < 0) {
        if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
        redp2p_set_error(ctx, "stream: local adapter write failed");
        return -1;
    }
    if (written == 0) {
        redp2p_set_error(ctx, "stream: local adapter write closed");
        return -1;
    }
    st->pending_adapter_off = (size_t)written;
    if (st->pending_adapter_off == st->pending_adapter_len) {
        st->pending_adapter_off = 0;
        st->pending_adapter_len = 0;
    }
    return 0;
}

/**
 * Dispatches one validated session datagram through handshake or KCP.
 * @return 0 on success, -1 on protocol, transport, or local socket failure.
 */
int redp2p_stream_process_packet(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t adapter_fd,
    const unsigned char *buf, size_t len)
{
    redp2p_session_envelope_t envelope;
    uint8_t expected_role;
    int input_result;

    if (!redp2p_session_unpack(buf, len, &envelope) ||
        !redp2p_stream_envelope_valid(&envelope))
        return 0;
    expected_role = st->initiator ? REDP2P_SESSION_ROLE_RESPONDER :
        REDP2P_SESSION_ROLE_INITIATOR;
    if (envelope.role != expected_role ||
        envelope.protocol != st->service_protocol ||
        memcmp(envelope.session_id, st->session_id,
            REDP2P_SESSION_ID_SZ) != 0)
        return 0;
    if (envelope.type == REDP2P_SESSION_TYPE_HELLO && !st->initiator) {
        if (redp2p_stream_send_control(ctx, st,
            REDP2P_SESSION_TYPE_HELLO_ACK) != 0)
            return -1;
        st->ready = 1;
        return 0;
    }
    if (envelope.type == REDP2P_SESSION_TYPE_HELLO_ACK && st->initiator) {
        st->ready = 1;
        if (ctx && ctx->direct_mode) {
            atomic_store(&ctx->channel_status, REDP2P_OK);
            atomic_store(&ctx->channel_state, 1);
        }
        return 0;
    }
    if (!st->ready) return 0;
    if (envelope.type == REDP2P_SESSION_TYPE_DATA) {
        input_result = ikcp_input(st->kcp, (const char *)envelope.payload,
            (long)envelope.payload_len);
        if (input_result != 0) {
            redp2p_set_error(ctx, "stream: KCP input failed (%d)", input_result);
            return -1;
        }
        st->next_update_ms = (uint32_t)redp2p_now_ms();
        return redp2p_stream_flush_adapter(ctx, st, adapter_fd);
    }
    if (envelope.type == REDP2P_SESSION_TYPE_CLOSE) {
        st->peer_adapter_eof = 1;
        if (redp2p_stream_flush_adapter(ctx, st, adapter_fd) != 0) return -1;
        return redp2p_stream_send_control(ctx, st,
            REDP2P_SESSION_TYPE_CLOSE_ACK);
    }
    if (envelope.type == REDP2P_SESSION_TYPE_CLOSE_ACK) {
        st->close_acked = 1;
        return 0;
    }
    if (envelope.type == REDP2P_SESSION_TYPE_RESET) {
        st->reset_received = 1;
        redp2p_set_error(ctx, "stream: peer reset");
        return -1;
    }
    return 0;
}

/**
 * Reads one local stream-adapter chunk and queues its bytes in KCP.
 * Peer delivery remains UDP datagrams, optionally relayed through TURN.
 * @return 0 on success, -1 on adapter or KCP failure.
 */
int redp2p_stream_pump_adapter(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t adapter_fd)
{
    unsigned char buf[REDP2P_BUF];
    int n;
    int sent;

    if (!redp2p_stream_can_send_data(st)) return 0;
    n = redp2p_sock_read(adapter_fd, (char *)buf, (int)sizeof(buf));
    if (n < 0) {
        if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
        redp2p_set_error(ctx, "stream: local adapter read failed");
        return -1;
    }
    if (n == 0) {
        st->adapter_read_eof = 1;
        return 0;
    }
    sent = ikcp_send(st->kcp, (const char *)buf, n);
    if (sent != n) {
        redp2p_set_error(ctx, "stream: KCP send failed");
        return -1;
    }
    st->next_update_ms = (uint32_t)redp2p_now_ms();
    return 0;
}

/**
 * Advances one KCP session and REDP2P tunnel lifecycle when due.
 * @return 0 on success, -1 on transport failure.
 */
int redp2p_stream_tick(redp2p_t *ctx, redp2p_stream_state_t *st)
{
    uint64_t now;
    uint32_t current;

    if (!st->enabled) return 0;
    now = redp2p_now_ms();
    current = (uint32_t)now;
    if (st->initiator && !st->ready &&
        (!st->hello_sent || now - st->last_hello_ms >= REDP2P_STREAM_HELLO_MS))
    {
        if (redp2p_stream_send_control(ctx, st,
            REDP2P_SESSION_TYPE_HELLO) != 0)
            return -1;
        st->hello_sent = 1;
        st->last_hello_ms = now;
    }
    if (st->ready && (int32_t)(current - st->next_update_ms) >= 0) {
        ikcp_update(st->kcp, current);
        st->next_update_ms = ikcp_check(st->kcp, current);
    }
    if (st->adapter->send_error) {
        redp2p_set_error(ctx, "stream: KCP UDP output failed");
        return -1;
    }
    if (st->ready && st->adapter_read_eof && !st->close_acked &&
        ikcp_waitsnd(st->kcp) == 0 &&
        (!st->close_sent || now - st->last_close_ms >= REDP2P_STREAM_CLOSE_MS))
    {
        if (redp2p_stream_send_control(ctx, st, REDP2P_SESSION_TYPE_CLOSE) != 0)
            return -1;
        st->close_sent = 1;
        st->last_close_ms = now;
    }
    if (st->ready && now - st->last_keepalive_ms >=
        (uint64_t)REDP2P_KEEPALIVE_S * 1000u)
    {
        if (redp2p_stream_send_control(ctx, st,
            REDP2P_SESSION_TYPE_KEEPALIVE) != 0)
            return -1;
        st->last_keepalive_ms = now;
    }
    return 0;
}

/**
 * Returns the next KCP update delay for select scheduling.
 * @return Milliseconds until work is due, capped at one second.
 */
uint32_t redp2p_stream_wait_ms(const redp2p_stream_state_t *st,
    uint64_t now)
{
    uint32_t current;
    int32_t difference;

    if (!st || !st->enabled) return 1000;
    if (st->pending_adapter_off < st->pending_adapter_len) return 10;
    if (st->peer_adapter_eof && !st->adapter_write_shutdown) return 10;
    if (!st->ready) {
        if (!st->initiator) return 1000;
        if (!st->hello_sent ||
            now - st->last_hello_ms >= REDP2P_STREAM_HELLO_MS)
            return 0;
        return (uint32_t)(REDP2P_STREAM_HELLO_MS -
            (now - st->last_hello_ms));
    }
    current = (uint32_t)now;
    difference = (int32_t)(st->next_update_ms - current);
    if (difference <= 0) return 0;
    if (difference > 1000) return 1000;
    return (uint32_t)difference;
}

/**
 * Reports whether one adapted reliable stream is fully closed on both sides.
 * @return 1 when the stream may be cleaned up, 0 otherwise.
 */
int redp2p_stream_is_done(const redp2p_stream_state_t *st) {
    return st->adapter_read_eof && st->close_acked && st->peer_adapter_eof &&
        st->adapter_write_shutdown && st->pending_adapter_len == 0 && st->kcp &&
        ikcp_waitsnd(st->kcp) == 0;
}

/**
 * Releases KCP and wipes all per-session stream material.
 * @return None.
 */
void redp2p_stream_wipe(redp2p_stream_state_t *st) {
    if (!st) return;
    if (st->kcp) ikcp_release(st->kcp);
    if (st->adapter) {
        crypto_wipe(st->adapter, sizeof(*st->adapter));
        free(st->adapter);
    }
    crypto_wipe(st, sizeof(*st));
}

/**
 * Resolve.
 * @return 0 on success, -1 on error.
 */
static int redp2p_resolve(
    const char *host,
    unsigned short port,
    int socktype,
    struct sockaddr_storage *out,
    socklen_t *out_len)
{
    struct addrinfo hints;
    struct addrinfo *ai;
    char port_str[16];

    if (!out || !out_len) return -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = host ? AF_UNSPEC : AF_INET6;
    hints.ai_socktype = socktype;

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0) return -1;

    if ((size_t)ai->ai_addrlen > sizeof(*out)) {
        freeaddrinfo(ai);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out, ai->ai_addr, ai->ai_addrlen);
    *out_len = (socklen_t)ai->ai_addrlen;
    freeaddrinfo(ai);
    return 0;
}

/**
 * Resolves one IPv4 endpoint.
 * Summary: STUN discovery is IPv4-only today, so resolver ordering must not
 *          select an IPv6 answer that the STUN path then rejects.
 * @param host Hostname to resolve.
 * @param port Port to resolve.
 * @param socktype Socket type.
 * @param out Output socket address.
 * @param out_len Output socket address length.
 * @return 0 on success, -1 on resolution failure.
 */
static int redp2p_resolve_ipv4(
    const char *host,
    unsigned short port,
    int socktype,
    struct sockaddr_storage *out,
    socklen_t *out_len)
{
    struct addrinfo hints;
    struct addrinfo *ai;
    char port_str[16];

    if (!host || !out || !out_len) return -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = socktype;

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0) return -1;
    if ((size_t)ai->ai_addrlen > sizeof(*out)) {
        freeaddrinfo(ai);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out, ai->ai_addr, ai->ai_addrlen);
    *out_len = (socklen_t)ai->ai_addrlen;
    freeaddrinfo(ai);
    return 0;
}

/**
 * Returns the socket address length for a stored address family.
 * @param addr Stored socket address.
 * @return Socket address length, or 0 for unsupported families.
 */
static socklen_t redp2p_sockaddr_len(const struct sockaddr_storage *addr) {
    if (!addr) return 0;
    if (addr->ss_family == AF_INET) return sizeof(struct sockaddr_in);
    if (addr->ss_family == AF_INET6) return sizeof(struct sockaddr_in6);
    return 0;
}

/**
 * Sets the UDP or TCP port in a stored socket address.
 * @param addr Stored socket address.
 * @param port Host-order port.
 * @return 1 on success, 0 for unsupported families.
 */
static int redp2p_sockaddr_set_port(struct sockaddr_storage *addr,
    unsigned short port)
{
    if (!addr) return 0;
    if (addr->ss_family == AF_INET) {
        ((struct sockaddr_in *)addr)->sin_port = htons(port);
        return 1;
    }
    if (addr->ss_family == AF_INET6) {
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
        return 1;
    }
    return 0;
}

/**
 * Sends bytes to one stored socket address.
 * @param fd   UDP socket.
 * @param buf  Bytes to send.
 * @param len  Byte count.
 * @param addr Destination address.
 * @return sendto result, or -1 for unsupported address families.
 */
int redp2p_sendto_addr(redp2p_fd_t fd, const void *buf, size_t len,
    const struct sockaddr_storage *addr)
{
    socklen_t addr_len;

    addr_len = redp2p_sockaddr_len(addr);
    if (addr_len == 0) return -1;
    return (int)sendto(fd, buf, len, 0, (const struct sockaddr *)addr,
        addr_len);
}

/**
 * Reports whether a host string is an IPv6 literal.
 * @param host Host string.
 * @return 1 for IPv6 literals, 0 otherwise.
 */
int redp2p_host_is_ipv6_literal(const char *host) {
    struct in6_addr addr;

    return host && inet_pton(AF_INET6, host, &addr) == 1;
}

/**
 * Creates one UDP or TCP socket bound to one local address.
 * @return Valid descriptor, or REDP2P_FD_INVALID on failure.
 */
redp2p_fd_t redp2p_create_socket(
    const char *bind_host,
    unsigned short bind_port)
{
    redp2p_fd_t fd;
    struct addrinfo hints;
    struct addrinfo *ai;
    struct addrinfo *it;
    char port_str[16];

    if (redp2p_platform_init() != 0) return REDP2P_FD_INVALID;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
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
        if (bind(fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0)
            break;
        REDP2P_FD_CLOSE(fd);
        fd = REDP2P_FD_INVALID;
    }
    freeaddrinfo(ai);
    return fd;
}

/**
 * Shuts down the local write side of one TCP socket.
 * @return None.
 */
static void redp2p_adapter_shutdown_write(redp2p_fd_t fd) {
#ifdef _WIN32
    shutdown(fd, SD_SEND);
#else
    shutdown(fd, SHUT_WR);
#endif
}

/**
 * Restores one socket to blocking mode after a bounded nonblocking connect.
 * @param fd Socket descriptor.
 * @return 0 on success, -1 on error.
 */
static int redp2p_set_blocking(redp2p_fd_t fd)
{
#ifdef _WIN32
    u_long mode = 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
}

/**
 * Waits until one nonblocking TCP connect completes.
 * @param fd Socket descriptor.
 * @param timeout_ms Maximum wait in milliseconds.
 * @return 1 when connected, 0 on timeout, -1 on socket failure.
 */
static int redp2p_wait_connected(redp2p_fd_t fd, int timeout_ms)
{
    redp2p_pollfd_t pollfd;
    int result;
    int socket_error;
#ifdef _WIN32
    int error_len;
#else
    socklen_t error_len;
#endif

    pollfd.fd = fd;
#ifdef _WIN32
    pollfd.events = POLLWRNORM;
#else
    pollfd.events = POLLOUT;
#endif
    pollfd.revents = 0;
    result = redp2p_poll_wait(&pollfd, 1, timeout_ms);
    if (result <= 0) return result;

    socket_error = 0;
    error_len = (int)sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&socket_error,
        &error_len) != 0)
        return -1;
    return socket_error == 0 ? 1 : -1;
}

/**
 * Opens one TCP connection with a bounded connect timeout.
 * @return Connected socket, or REDP2P_FD_INVALID on error or timeout.
 */
static redp2p_fd_t redp2p_tcp_connect(const char *host, unsigned short port)
{
    redp2p_fd_t fd;
    struct addrinfo hints;
    struct addrinfo *ai;
    struct addrinfo *it;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0)
        return REDP2P_FD_INVALID;

    fd = REDP2P_FD_INVALID;
    for (it = ai; it; it = it->ai_next) {
        int connected;

        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (REDP2P_ISERR(fd)) continue;
        if (redp2p_set_nonblock(fd) != 0) {
            REDP2P_FD_CLOSE(fd);
            fd = REDP2P_FD_INVALID;
            continue;
        }

        connected = connect(fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0;
        if (!connected) {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
                error == WSAEALREADY)
                connected = redp2p_wait_connected(fd,
                    REDP2P_HTTP_TIMEOUT_S * 1000) > 0;
#else
            if (errno == EINPROGRESS || errno == EWOULDBLOCK ||
                errno == EAGAIN)
                connected = redp2p_wait_connected(fd,
                    REDP2P_HTTP_TIMEOUT_S * 1000) > 0;
#endif
        }

        if (connected && redp2p_set_blocking(fd) == 0) break;
        REDP2P_FD_CLOSE(fd);
        fd = REDP2P_FD_INVALID;
    }
    freeaddrinfo(ai);
    return fd;
}

/**
 * Reports whether raw control bytes are safe for C-string parsing.
 * @param data Raw control bytes.
 * @param len  Byte count.
 * @return 1 when safe, 0 when a prohibited control byte is present.
 */
static int redp2p_control_bytes_valid(const char *data, size_t len) {
    size_t i;

    if (!data) return 0;
    for (i = 0; i < len; i++) {
        unsigned char byte = (unsigned char)data[i];

        if (byte == '\r') continue;
        if (byte < 0x20 || byte == 0x7f) return 0;
    }
    return 1;
}

/**
 * Tcp readline.
 * @return Complete line length, or a negative value on timeout, EOF, invalid
 * bytes, or capacity exhaustion.
 */
static int redp2p_wait_readable(redp2p_fd_t fd, int timeout_ms)
{
    redp2p_pollfd_t pollfd;
    int result;

    pollfd.fd = fd;
    pollfd.events = REDP2P_POLLIN;
    pollfd.revents = 0;
    result = redp2p_poll_wait(&pollfd, 1, timeout_ms);
    if (result <= 0) return result;
    return redp2p_poll_readable(&pollfd) ? 1 : -1;
}

/**
 * Reads one CRLF-terminated TCP line with a bounded timeout.
 * @param fd Socket descriptor.
 * @param buf Destination buffer.
 * @param cap Destination capacity.
 * @param timeout_sec Maximum wait in seconds.
 * @return Line length, 0 on close, or -1 on failure.
 */
static int redp2p_tcp_readline(redp2p_fd_t fd, char *buf, int cap,
    int timeout_sec) {
    int total = 0;
    int n;
    char byte;

    if (cap < 1) return -1;

    for (;;) {
        int wait_ms = (timeout_sec > 0 && total == 0) ?
            timeout_sec * 1000 : 1000;

        n = redp2p_wait_readable(fd, wait_ms);
        if (n <= 0) return -1;

        n = redp2p_sock_read(fd, &byte, 1);
        if (n <= 0) return -2;

        if (byte == '\n') {
            buf[total] = '\0';
            return total;
        }
        if (byte == '\r') continue;
        if (!redp2p_control_bytes_valid(&byte, 1)) return -3;
        if (total >= cap - 1) return -4;
        buf[total++] = byte;
    }
}

/**
 * Maps one index JSON error code to a library result code.
 * @param code Index reply error code.
 * @return REDP2P_* result code.
 */
static int redp2p_http_map_error(const char *code)
{
    if (!code) return REDP2P_EPROTO;
    if (strcmp(code, "bad_request") == 0 || strcmp(code, "busy") == 0)
        return REDP2P_EPROTO;
    if (strcmp(code, "invalid_id") == 0)
        return REDP2P_EINVAL;
    if (strcmp(code, "auth_failed") == 0 || strcmp(code, "invalid_key") == 0 ||
        strcmp(code, "invalid_proof") == 0)
        return REDP2P_EAUTH;
    if (strcmp(code, "not_found") == 0)
        return REDP2P_ENOENT;
    if (strcmp(code, "table_full") == 0)
        return REDP2P_EFULL;
    if (strcmp(code, "already_registered") == 0)
        return REDP2P_EEXIST;
    if (strcmp(code, "internal") == 0)
        return REDP2P_ERROR;
    return REDP2P_EPROTO;
}

/**
 * Reads one bounded chunk of one index HTTP response body.
 * @param fd  Response socket.
 * @param buf Output buffer.
 * @param cap Output buffer capacity.
 * @return Bytes read, or a negative value on timeout or failure.
 */
static int redp2p_http_read_some(redp2p_fd_t fd, char *buf, int cap)
{
    if (cap < 1) return -1;
    if (redp2p_wait_readable(fd, REDP2P_HTTP_TIMEOUT_S * 1000) <= 0)
        return -1;
    return redp2p_sock_read(fd, buf, cap);
}

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
int redp2p_http_client(redp2p_t *ctx, const char *phase,
    const char *host, unsigned short port, JSON_Value *request,
    JSON_Value **response_out)
{
    char head[REDP2P_HTTP_LINE_MAX * 6];
    char body[REDP2P_HTTP_BODY_MAX + 1];
    char line[REDP2P_HTTP_LINE_MAX];
    redp2p_fd_t fd;
    size_t body_size;
    long content_length;
    int status;
    int line_result;
    int off;
    int i;
    int n;

    if (!phase || !host || !host[0] || port == 0 || !request)
        return REDP2P_EINVAL;
    if (response_out) *response_out = NULL;
    body_size = json_serialization_size(request);
    if (body_size == 0 || body_size > sizeof(body)) {
        redp2p_set_error(ctx, "%s: index request is too large", phase);
        return REDP2P_EPROTO;
    }
    fd = redp2p_tcp_connect(host, port);
    if (REDP2P_ISERR(fd)) {
        redp2p_set_error(ctx, "%s: index connect %s:%u failed",
            phase, host, (unsigned)port);
        return REDP2P_ENET;
    }
    if (json_serialize_to_buffer(request, body, sizeof(body)) != JSONSuccess) {
        REDP2P_FD_CLOSE(fd);
        redp2p_set_error(ctx, "%s: index request serialization failed", phase);
        return REDP2P_ERROR;
    }
    n = snprintf(head, sizeof(head),
        "POST /redp2p/ HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %u\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "\r\n", host, (unsigned)(body_size - 1));
    if (n < 0 || (size_t)n >= sizeof(head) ||
        redp2p_write_all(fd, head, n) != 0 ||
        redp2p_write_all(fd, body, (int)(body_size - 1)) != 0)
    {
        REDP2P_FD_CLOSE(fd);
        redp2p_set_error(ctx, "%s: index request write failed", phase);
        return REDP2P_ENET;
    }
    line_result = redp2p_tcp_readline(fd, line, (int)sizeof(line),
        REDP2P_HTTP_TIMEOUT_S);
    if (line_result < 0) {
        REDP2P_FD_CLOSE(fd);
        redp2p_set_error(ctx, line_result == -1 ?
            "%s: index response timed out" : "%s: index response failed",
            phase);
        return line_result == -1 ? REDP2P_ETIMEOUT : REDP2P_ENET;
    }
    if (strncmp(line, "HTTP/", 5) != 0) {
        REDP2P_FD_CLOSE(fd);
        redp2p_set_error(ctx, "%s: malformed index status line", phase);
        return REDP2P_EPROTO;
    }
    {
        const char *cursor;
        int d;

        cursor = strchr(line, ' ');
        status = 0;
        if (!cursor) {
            REDP2P_FD_CLOSE(fd);
            redp2p_set_error(ctx, "%s: malformed index status line", phase);
            return REDP2P_EPROTO;
        }
        while (*cursor == ' ') cursor++;
        for (d = 0; d < 3 && cursor[d] >= '0' && cursor[d] <= '9'; d++)
            status = status * 10 + (cursor[d] - '0');
        if (d != 3) {
            REDP2P_FD_CLOSE(fd);
            redp2p_set_error(ctx, "%s: malformed index status line", phase);
            return REDP2P_EPROTO;
        }
    }
    content_length = -1;
    for (i = 0; i < REDP2P_HTTP_HEADERS_MAX; i++) {
        char *colon;

        line_result = redp2p_tcp_readline(fd, line, (int)sizeof(line),
            REDP2P_HTTP_TIMEOUT_S);
        if (line_result < 0) {
            REDP2P_FD_CLOSE(fd);
            redp2p_set_error(ctx, line_result == -1 ?
                "%s: index response timed out" : "%s: index response failed",
                phase);
            return line_result == -1 ? REDP2P_ETIMEOUT : REDP2P_ENET;
        }
        if (line_result == 0) break;
        colon = strchr(line, ':');
        if (colon) {
            char name[32];
            const char *value;

            if ((size_t)(colon - line) >= sizeof(name)) {
                REDP2P_FD_CLOSE(fd);
                redp2p_set_error(ctx, "%s: malformed index response headers",
                    phase);
                return REDP2P_EPROTO;
            }
            memcpy(name, line, (size_t)(colon - line));
            name[colon - line] = '\0';
            value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            if (redp2p_ascii_casecmp(name, "Content-Length") == 0) {
                if (!redp2p_parse_u(value, 0, REDP2P_HTTP_BODY_MAX,
                    &content_length))
                {
                    REDP2P_FD_CLOSE(fd);
                    redp2p_set_error(ctx,
                        "%s: malformed index response length", phase);
                    return REDP2P_EPROTO;
                }
            } else if (redp2p_ascii_casecmp(name, "Transfer-Encoding") == 0) {
                REDP2P_FD_CLOSE(fd);
                redp2p_set_error(ctx,
                    "%s: unsupported index response encoding", phase);
                return REDP2P_EPROTO;
            }
        }
    }
    if (content_length < 0) content_length = 0;
    off = 0;
    while (off < content_length) {
        line_result = redp2p_http_read_some(fd, body + off,
            (int)content_length - off);
        if (line_result <= 0) {
            REDP2P_FD_CLOSE(fd);
            redp2p_set_error(ctx, line_result == -1 ?
                "%s: index response timed out" : "%s: index response failed",
                phase);
            return line_result == -1 ? REDP2P_ETIMEOUT : REDP2P_ENET;
        }
        off += line_result;
    }
    body[off] = '\0';
    REDP2P_FD_CLOSE(fd);
    if (status == 200) {
        JSON_Value *parsed;

        parsed = json_parse_string(body);
        if (!parsed || json_value_get_type(parsed) != JSONObject) {
            if (parsed) json_value_free(parsed);
            redp2p_set_error(ctx, "%s: malformed index response", phase);
            return REDP2P_EPROTO;
        }
        if (response_out) *response_out = parsed;
        else json_value_free(parsed);
        return REDP2P_OK;
    }
    {
        JSON_Value *parsed;
        char code[REDP2P_HTTP_LINE_MAX];
        const char *code_ref;

        parsed = json_parse_string(body);
        code_ref = NULL;
        if (parsed) {
            if (json_value_get_type(parsed) == JSONObject)
                code_ref = json_object_get_string(json_value_get_object(parsed),
                    "error");
            if (code_ref) {
                snprintf(code, sizeof(code), "%s", code_ref);
                code_ref = code;
            }
            json_value_free(parsed);
        }
        if (code_ref)
            redp2p_set_error(ctx, "%s: index request failed (%s)", phase,
                code_ref);
        else
            redp2p_set_error(ctx, "%s: index request failed (HTTP %d)", phase,
                status);
        return code_ref ? redp2p_http_map_error(code_ref) :
            (status == 503 ? REDP2P_EFULL : REDP2P_EPROTO);
    }
}

/**
 * Configures the local service or listener port used by pub and con.
 * Summary: Marks the port as explicit for precedence over arguments.
 * @param ctx  Open context.
 * @param port Local bind port.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_local_port(redp2p_t *ctx, unsigned short port) {
    if (!ctx) return REDP2P_EINVAL;
    if (port == 0) {
        redp2p_set_error(ctx, "port must be between 1 and 65535");
        return REDP2P_EINVAL;
    }
    ctx->bind_port = port;
    ctx->explicit_port = 1;
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Set protocol.
 * @return 0 on success, -1 on error.
 */
int redp2p_pub_set_protocol(redp2p_t *ctx, int proto) {
    if (!ctx) return REDP2P_EINVAL;
    if (proto != REDP2P_PROTO_TCP && proto != REDP2P_PROTO_UDP) {
        redp2p_set_error(ctx, "protocol must be REDP2P_PROTO_TCP or REDP2P_PROTO_UDP");
        return REDP2P_EINVAL;
    }
    ctx->proto = proto;
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Resolves the effective local port for pub or con.
 * Summary: A nonzero argument overrides a default context port, while a nonzero
 * argument conflicting with an explicitly set context port is rejected.
 * @param ctx        Open context.
 * @param arg_port  Port argument from redp2p_pub_run or redp2p_con_run.
 * @param out        Effective port output.
 * @return REDP2P_OK on success, REDP2P_ERROR on conflicting ports.
 */
int redp2p_resolve_port(redp2p_t *ctx, unsigned short arg_port,
    unsigned short *out)
{
    if (arg_port == 0) {
        *out = ctx->bind_port;
        return REDP2P_OK;
    }
    if (ctx->explicit_port && ctx->bind_port != 0 &&
        ctx->bind_port != arg_port)
        return REDP2P_ERROR;
    *out = arg_port;
    return REDP2P_OK;
}

/**
 * Reports whether a transient punch id is bounded and safe.
 * @param text Input token.
 * @return 1 when valid, 0 otherwise.
 */
static int redp2p_is_punch_id(const char *text) {
    size_t i;

    if (!text || !text[0]) return 0;
    for (i = 0; text[i]; i++) {
        if (i >= REDP2P_ID_MAX) return 0;
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
            (text[i] >= 'A' && text[i] <= 'Z') ||
            (text[i] >= '0' && text[i] <= '9') || text[i] == '-'))
            return 0;
    }
    return 1;
}

/**
 * Parses a UDP punch packet.
 * @param text     Input packet text.
 * @param prefix   Required packet prefix.
 * @param sess_id  Output session id.
 * @param from_id  Output source id.
 * @param to_id    Output destination id.
 * @return 1 on success, 0 on malformed input.
 */
int redp2p_parse_punch_packet(const char *text, const char *prefix,
    char sess_id[REDP2P_CTRL_SESSION_MAX + 1],
    char from_id[REDP2P_ID_MAX + 1], char to_id[REDP2P_ID_MAX + 1])
{
    const char *cursor;
    size_t prefix_len;

    if (!text || !prefix) return 0;
    prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) != 0) return 0;
    cursor = text + prefix_len;
    if (!redp2p_parse_field(&cursor, sess_id, REDP2P_CTRL_SESSION_MAX + 1,
        ':'))
        return 0;
    if (!redp2p_parse_field(&cursor, from_id, REDP2P_ID_MAX + 1, ':'))
        return 0;
    if (!redp2p_parse_field(&cursor, to_id, REDP2P_ID_MAX + 1, '\0'))
        return 0;
    if (*cursor != '\0') return 0;
    while (to_id[0]) {
        size_t len = strlen(to_id);
        if (to_id[len - 1] != '\n' && to_id[len - 1] != '\r') break;
        to_id[len - 1] = '\0';
    }
    return redp2p_is_session_token(sess_id) && redp2p_is_punch_id(from_id) &&
        redp2p_is_punch_id(to_id);
}

/**
 * STUN put16.
 * @return None.
 */
static void redp2p_stun_put16(unsigned char *b, int o, int v) {
    b[o] = (unsigned char)(v >> 8); b[o + 1] = (unsigned char)(v);
}

/**
 * STUN length.
 * @return None.
 */
static void redp2p_stun_len(unsigned char *buf, int o) {
    redp2p_stun_put16(buf, 2, o - 20);
}

/**
 * STUN gen id.
 * @return None.
 */
static int redp2p_stun_gen_id(unsigned char id[12]) {
    return redp2p_fill_random(id, 12) == 0;
}

/**
 * STUN build.
 * @return offset after header.
 */
static int redp2p_stun_build(unsigned char *buf, int mt, const unsigned char id[12]) {
    memset(buf, 0, 20);
    redp2p_stun_put16(buf, 0, mt);
    redp2p_stun_put16(buf, 4, 0x2112); buf[6] = 0xA4; buf[7] = 0x42;
    memcpy(buf + 8, id, 12);
    return 20;
}

/**
 * STUN hdr.
 * @return message type, or -1 on error.
 */
static int redp2p_stun_hdr(const unsigned char *buf, int len, unsigned char id[12]) {
    if (len < 20) return -1;
    uint32_t mg = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
        ((uint32_t)buf[6] << 8) | buf[7];
    if (mg != REDP2P_STUN_MAGIC) return -1;
    int msg_len = (buf[2] << 8) | buf[3];
    if (msg_len & 3) return -1;
    if (20 + msg_len > len) return -1;
    int mt = (buf[0] << 8) | buf[1];
    memcpy(id, buf + 8, 12);
    return mt;
}

/**
 * STUN find attr.
 * @return offset of attr, or -1.
 */
static int redp2p_stun_find(const unsigned char *buf, int len, int t, int *al) {
    int o = 20;
    while (o + 4 <= len) {
        int at = (buf[o] << 8) | buf[o + 1];
        int av = (buf[o + 2] << 8) | buf[o + 3];
        int pad = (av & 3) ? 4 - (av & 3) : 0;
        if (o + 4 + av > len) return -1;
        if (at == t) { if (al) *al = av; return o + 4; }
        o += 4 + av + pad;
    }
    return -1;
}

/**
 * STUN rd xaddr.
 * @return 0 on success, -1 on error.
 */
static int redp2p_stun_rd_xaddr(const unsigned char *buf, int o, int al,
    unsigned char id[12], char *addr, int acap, unsigned short *port)
{
    (void)id;
    if (al < 8) return -1;
    if (buf[o + 1] != 1) return -1;
    unsigned short xp = ((unsigned short)buf[o + 2] << 8) | buf[o + 3];
    uint32_t xa = ((uint32_t)buf[o + 4] << 24) | ((uint32_t)buf[o + 5] << 16) |
        ((uint32_t)buf[o + 6] << 8) | buf[o + 7];
    *port = xp ^ (unsigned short)(REDP2P_STUN_MAGIC >> 16);
    uint32_t a = xa ^ REDP2P_STUN_MAGIC;
    snprintf(addr, (size_t)acap, "%u.%u.%u.%u",
        (unsigned)(a >> 24) & 0xff, (unsigned)(a >> 16) & 0xff,
        (unsigned)(a >> 8) & 0xff, (unsigned)(a & 0xff));
    return 0;
}

/**
 * STUN binding.
 * Summary: Sends Binding Request and returns srflx ip:port for udp_fd.
 * @param ctx      REDP2P context.
 * @param udp_fd   Bound UDP socket.
 * @param out_ip   Output address buffer.
 * @param out_cap  Output address buffer size.
 * @param out_port Output port.
 * @return 0 on success, -1 on error.
 */
static int redp2p_stun_binding(redp2p_t *ctx, int udp_fd,
    char *out_ip, int out_cap, unsigned short *out_port)
{
    unsigned char tx[4096], rx[4096], tx_id[12], rx_id[12];
    char host[256];
    unsigned short port;
    struct sockaddr_storage from;
    socklen_t from_len;
    struct sockaddr_storage srv;
    socklen_t srv_len;
    int off, rl, n, mt, ao, al;
    size_t sl;

    if (!ctx || !ctx->stun_url[0] || !out_ip || out_cap <= 0 || !out_port)
        return -1;

    const char *p = ctx->stun_url;
    const char *co;
    long lport;

    if (strncmp(p, "stun:", 5) != 0) return -1;
    p += 5;

    co = strchr(p, ':');
    if (!co || co == p) return -1;

    sl = (size_t)(co - p);
    if (sl >= sizeof(host)) return -1;
    memcpy(host, p, sl);
    host[sl] = '\0';

    if (*(co + 1) == '\0') return -1;
    if (!redp2p_parse_u(co + 1, 1, 65535, &lport)) return -1;
    port = (unsigned short)lport;

    if (redp2p_resolve_ipv4(host, port, SOCK_DGRAM, &srv, &srv_len) != 0)
        return -1;

    if (!redp2p_stun_gen_id(tx_id)) return -1;
    off = redp2p_stun_build(tx, REDP2P_STUN_BINDING, tx_id);
    redp2p_stun_len(tx, off);

    if (sendto(udp_fd, (const char *)tx, (size_t)off, 0,
        (const struct sockaddr *)&srv, srv_len) < 0) return -1;

    n = redp2p_wait_readable((redp2p_fd_t)udp_fd, 3000);
    if (n <= 0) return -1;
    from_len = sizeof(from);
    rl = (int)recvfrom(udp_fd, (char *)rx, sizeof(rx), 0,
        (struct sockaddr *)&from, &from_len);
    if (rl < 20) return -1;
    if (!redp2p_sockaddr_equal(&from, &srv)) return -1;

    mt = redp2p_stun_hdr(rx, rl, rx_id);
    if (mt != REDP2P_STUN_BINDING_RESP) return -1;
    if (memcmp(tx_id, rx_id, sizeof(tx_id)) != 0) return -1;

    ao = redp2p_stun_find(rx, rl, REDP2P_STUN_ATTR_XOR_MAPPED_ADDR, &al);
    if (ao < 0) return -1;
    if (redp2p_stun_rd_xaddr(rx, ao, al, tx_id, out_ip, out_cap, out_port) != 0)
        return -1;
    return 0;
}

/**
 * Refreshes the STUN mapping without waiting for the response.
 * Summary: Mirrors the raw punch helper by keeping the mapping active on the
 *          same UDP socket while peer probes are in flight.
 * @param ctx REDP2P context containing the STUN URL.
 * @param udp_fd Bound UDP socket used for peer traffic.
 * @return 0 when sent, -1 when STUN is unavailable or the send fails.
 */
int redp2p_stun_keepalive(redp2p_t *ctx, int udp_fd)
{
    unsigned char tx[20], tx_id[12];
    struct sockaddr_storage srv;
    socklen_t srv_len;
    char host[256];
    const char *p;
    const char *co;
    unsigned short port;
    long lport;
    size_t sl;
    int off;

    if (!ctx || !ctx->stun_url[0]) return -1;
    p = ctx->stun_url;
    if (strncmp(p, "stun:", 5) != 0) return -1;
    p += 5;
    co = strchr(p, ':');
    if (!co || co == p || *(co + 1) == '\0') return -1;
    sl = (size_t)(co - p);
    if (sl >= sizeof(host)) return -1;
    memcpy(host, p, sl);
    host[sl] = '\0';
    if (!redp2p_parse_u(co + 1, 1, 65535, &lport)) return -1;
    port = (unsigned short)lport;
    if (redp2p_resolve_ipv4(host, port, SOCK_DGRAM, &srv, &srv_len) != 0 ||
        !redp2p_stun_gen_id(tx_id))
        return -1;
    off = redp2p_stun_build(tx, REDP2P_STUN_BINDING, tx_id);
    redp2p_stun_len(tx, off);
    return sendto(udp_fd, (const char *)tx, (size_t)off, 0,
        (const struct sockaddr *)&srv, srv_len) < 0 ? -1 : 0;
}

/**
 * Set stun url.
 * @return 0 on success, -1 on error.
 */
int redp2p_set_stun_server(redp2p_t *ctx, const char *url) {
    if (!ctx) return REDP2P_EINVAL;
    if (url) {
        strncpy(ctx->stun_url, url, sizeof(ctx->stun_url) - 1);
        ctx->stun_url[sizeof(ctx->stun_url) - 1] = '\0';
    } else {
        ctx->stun_url[0] = '\0';
    }
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

#define REDP2P_TURN_ALLOCATE_REQ        0x0003
#define REDP2P_TURN_ALLOCATE_OK         0x0103
#define REDP2P_TURN_ALLOCATE_ERR        0x0113
#define REDP2P_TURN_REFRESH_REQ         0x0004
#define REDP2P_TURN_REFRESH_OK          0x0104
#define REDP2P_TURN_REFRESH_ERR         0x0114
#define REDP2P_TURN_PERMISSION_REQ      0x0008
#define REDP2P_TURN_PERMISSION_OK       0x0108
#define REDP2P_TURN_PERMISSION_ERR      0x0118
#define REDP2P_TURN_SEND_IND            0x0016
#define REDP2P_TURN_DATA_IND            0x0017
#define REDP2P_TURN_ATTR_USERNAME       0x0006
#define REDP2P_TURN_ATTR_INTEGRITY      0x0008
#define REDP2P_TURN_ATTR_ERROR_CODE     0x0009
#define REDP2P_TURN_ATTR_LIFETIME       0x000d
#define REDP2P_TURN_ATTR_XOR_PEER       0x0012
#define REDP2P_TURN_ATTR_DATA           0x0013
#define REDP2P_TURN_ATTR_REALM          0x0014
#define REDP2P_TURN_ATTR_NONCE          0x0015
#define REDP2P_TURN_ATTR_XOR_RELAYED    0x0016
#define REDP2P_TURN_ATTR_TRANSPORT      0x0019
#define REDP2P_TURN_DEFAULT_PORT        3478
#define REDP2P_TURN_DEFAULT_LIFETIME    600
#define REDP2P_TURN_PERMISSION_MS       240000
#define REDP2P_TURN_PACKET_MAX          8192

typedef struct {
    uint32_t state[4];
    uint64_t total;
    unsigned char block[64];
    size_t used;
} redp2p_md5_t;

typedef struct {
    uint32_t state[5];
    uint64_t total;
    unsigned char block[64];
    size_t used;
} redp2p_sha1_t;

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static uint32_t redp2p_turn_rotl32(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32u - bits));
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static uint32_t redp2p_turn_load_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_store_le32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_md5_block(redp2p_md5_t *ctx,
    const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,
        0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,
        0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,
        0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,
        0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,
        0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,
        0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,
        0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,
        0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u
    };
    static const unsigned char s[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    uint32_t m[16], a, b, cc, d;
    int i;

    for (i = 0; i < 16; i++) m[i] = redp2p_turn_load_le32(block + i * 4);
    a = ctx->state[0]; b = ctx->state[1]; cc = ctx->state[2]; d = ctx->state[3];
    for (i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        uint32_t old_d;

        if (i < 16) { f = (b & cc) | ((~b) & d); g = i; }
        else if (i < 32) { f = (d & b) | ((~d) & cc); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d; g = (3 * i + 5) & 15; }
        else { f = cc ^ (b | (~d)); g = (7 * i) & 15; }
        old_d = d;
        d = cc;
        cc = b;
        b = b + redp2p_turn_rotl32(a + f + k[i] + m[g], s[i]);
        a = old_d;
    }
    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += cc; ctx->state[3] += d;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_md5_init(redp2p_md5_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301u; ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu; ctx->state[3] = 0x10325476u;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_md5_update(redp2p_md5_t *ctx,
    const unsigned char *data, size_t len)
{
    size_t take;

    ctx->total += len;
    while (len > 0) {
        take = sizeof(ctx->block) - ctx->used;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take; data += take; len -= take;
        if (ctx->used == sizeof(ctx->block)) {
            redp2p_turn_md5_block(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_md5_final(redp2p_md5_t *ctx, unsigned char out[16])
{
    unsigned char pad[72];
    uint64_t bits;
    size_t pad_len;
    int i;

    bits = ctx->total * 8u;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    pad_len = ctx->used < 56 ? 56 - ctx->used : 120 - ctx->used;
    redp2p_turn_md5_update(ctx, pad, pad_len);
    for (i = 0; i < 8; i++) pad[i] = (unsigned char)(bits >> (i * 8));
    redp2p_turn_md5_update(ctx, pad, 8);
    for (i = 0; i < 4; i++) redp2p_turn_store_le32(out + i * 4, ctx->state[i]);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static uint32_t redp2p_turn_load_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_store_be32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24); p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8); p[3] = (unsigned char)value;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_sha1_block(redp2p_sha1_t *ctx,
    const unsigned char block[64])
{
    uint32_t w[80], a, b, cc, d, e;
    int i;

    for (i = 0; i < 16; i++) w[i] = redp2p_turn_load_be32(block + i * 4);
    for (i = 16; i < 80; i++)
        w[i] = redp2p_turn_rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^
            w[i - 16], 1);
    a = ctx->state[0]; b = ctx->state[1]; cc = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) { f = (b & cc) | ((~b) & d); k = 0x5a827999u; }
        else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdcu; }
        else { f = b ^ cc ^ d; k = 0xca62c1d6u; }
        t = redp2p_turn_rotl32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = redp2p_turn_rotl32(b, 30); b = a; a = t;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += cc;
    ctx->state[3] += d; ctx->state[4] += e;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_sha1_init(redp2p_sha1_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301u; ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu; ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_sha1_update(redp2p_sha1_t *ctx,
    const unsigned char *data, size_t len)
{
    size_t take;

    ctx->total += len;
    while (len > 0) {
        take = sizeof(ctx->block) - ctx->used;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += take; data += take; len -= take;
        if (ctx->used == sizeof(ctx->block)) {
            redp2p_turn_sha1_block(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_sha1_final(redp2p_sha1_t *ctx, unsigned char out[20])
{
    unsigned char pad[72];
    uint64_t bits;
    size_t pad_len;
    int i;

    bits = ctx->total * 8u;
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    pad_len = ctx->used < 56 ? 56 - ctx->used : 120 - ctx->used;
    redp2p_turn_sha1_update(ctx, pad, pad_len);
    for (i = 0; i < 8; i++) pad[i] = (unsigned char)(bits >> (56 - i * 8));
    redp2p_turn_sha1_update(ctx, pad, 8);
    for (i = 0; i < 5; i++) redp2p_turn_store_be32(out + i * 4, ctx->state[i]);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_hmac_sha1(const unsigned char *key, size_t key_len,
    const unsigned char *data, size_t len, unsigned char out[20])
{
    unsigned char k0[64], inner[20], pad[64];
    redp2p_sha1_t sha;
    size_t i;

    memset(k0, 0, sizeof(k0));
    if (key_len > sizeof(k0)) {
        redp2p_turn_sha1_init(&sha);
        redp2p_turn_sha1_update(&sha, key, key_len);
        redp2p_turn_sha1_final(&sha, k0);
    } else if (key_len > 0) {
        memcpy(k0, key, key_len);
    }
    for (i = 0; i < sizeof(pad); i++) pad[i] = k0[i] ^ 0x36u;
    redp2p_turn_sha1_init(&sha);
    redp2p_turn_sha1_update(&sha, pad, sizeof(pad));
    redp2p_turn_sha1_update(&sha, data, len);
    redp2p_turn_sha1_final(&sha, inner);
    for (i = 0; i < sizeof(pad); i++) pad[i] = k0[i] ^ 0x5cu;
    redp2p_turn_sha1_init(&sha);
    redp2p_turn_sha1_update(&sha, pad, sizeof(pad));
    redp2p_turn_sha1_update(&sha, inner, sizeof(inner));
    redp2p_turn_sha1_final(&sha, out);
    crypto_wipe(k0, sizeof(k0)); crypto_wipe(inner, sizeof(inner));
    crypto_wipe(pad, sizeof(pad)); crypto_wipe(&sha, sizeof(sha));
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_attr(unsigned char *buf, size_t cap, int *off,
    uint16_t type, const void *data, size_t len)
{
    size_t padded;

    if (!buf || !off || len > 65535u) return 0;
    padded = (len + 3u) & ~3u;
    if ((size_t)*off + 4u + padded > cap) return 0;
    redp2p_stun_put16(buf, *off, type);
    redp2p_stun_put16(buf, *off + 2, (int)len);
    if (len > 0 && data) memcpy(buf + *off + 4, data, len);
    if (padded > len) memset(buf + *off + 4 + len, 0, padded - len);
    *off += (int)(4u + padded);
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_long_term_key(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char key[16])
{
    char material[REDP2P_TURN_USER_MAX + REDP2P_TURN_REALM_MAX +
        REDP2P_PASS_MAX + 3];
    int n;
    redp2p_md5_t md5;

    if (!ctx || !allocation || !ctx->turn_user[0] ||
        !allocation->realm[0]) return 0;
    n = snprintf(material, sizeof(material), "%s:%s:%s", ctx->turn_user,
        allocation->realm, ctx->turn_pass);
    if (n < 0 || (size_t)n >= sizeof(material)) return 0;
    redp2p_turn_md5_init(&md5);
    redp2p_turn_md5_update(&md5, (const unsigned char *)material, (size_t)n);
    redp2p_turn_md5_final(&md5, key);
    crypto_wipe(material, sizeof(material)); crypto_wipe(&md5, sizeof(md5));
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_integrity(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char *buf,
    size_t cap, int *off)
{
    unsigned char key[16], mac[20];

    if (!redp2p_turn_long_term_key(ctx, allocation, key)) return 0;
    if ((size_t)*off + 24u > cap) {
        crypto_wipe(key, sizeof(key));
        return 0;
    }
    redp2p_stun_put16(buf, 2, *off + 24 - 20);
    redp2p_turn_hmac_sha1(key, sizeof(key), buf, (size_t)*off, mac);
    if (!redp2p_turn_attr(buf, cap, off, REDP2P_TURN_ATTR_INTEGRITY,
        mac, sizeof(mac)))
    {
        crypto_wipe(key, sizeof(key)); crypto_wipe(mac, sizeof(mac));
        return 0;
    }
    redp2p_stun_len(buf, *off);
    crypto_wipe(key, sizeof(key)); crypto_wipe(mac, sizeof(mac));
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_auth_attrs(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char *buf,
    size_t cap, int *off)
{
    return redp2p_turn_attr(buf, cap, off, REDP2P_TURN_ATTR_USERNAME,
        ctx->turn_user, strlen(ctx->turn_user)) &&
        redp2p_turn_attr(buf, cap, off, REDP2P_TURN_ATTR_REALM,
            allocation->realm, strlen(allocation->realm)) &&
        redp2p_turn_attr(buf, cap, off, REDP2P_TURN_ATTR_NONCE,
            allocation->nonce, strlen(allocation->nonce));
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_parse_url(const char *url, char *host, size_t host_cap,
    unsigned short *port)
{
    const char *p;
    const char *target_end;
    const char *end;
    const char *port_text;
    const char *query;
    char port_buf[16];
    size_t host_len;
    size_t port_len;
    long parsed;

    if (!url || strncmp(url, "turn:", 5) != 0 || !host || !port) return 0;
    p = url + 5;
    if (!*p) return 0;
    query = strchr(p, '?');
    if (query) {
        if (strcmp(query, "?transport=udp") != 0) return 0;
        target_end = query;
    } else {
        target_end = p + strlen(p);
    }
    if (target_end == p) return 0;
    port_text = NULL;
    if (*p == '[') {
        end = memchr(p + 1, ']', (size_t)(target_end - (p + 1)));
        if (!end) return 0;
        host_len = (size_t)(end - (p + 1));
        p++;
        if (end + 1 < target_end) {
            if (end[1] != ':') return 0;
            port_text = end + 2;
        } else if (end + 1 != target_end) {
            return 0;
        }
    } else {
        const char *scan;
        const char *first_colon;
        const char *last_colon;

        first_colon = NULL;
        last_colon = NULL;
        for (scan = p; scan < target_end; scan++) {
            if (*scan == ':') {
                if (!first_colon) first_colon = scan;
                last_colon = scan;
            }
        }
        if (first_colon && first_colon == last_colon) {
            host_len = (size_t)(first_colon - p);
            port_text = first_colon + 1;
        } else {
            host_len = (size_t)(target_end - p);
        }
    }
    if (host_len == 0 || host_len >= host_cap) return 0;
    memcpy(host, p, host_len);
    host[host_len] = '\0';
    *port = REDP2P_TURN_DEFAULT_PORT;
    if (!port_text) return 1;
    if (port_text >= target_end) return 0;
    port_len = (size_t)(target_end - port_text);
    if (port_len == 0 || port_len >= sizeof(port_buf)) return 0;
    memcpy(port_buf, port_text, port_len);
    port_buf[port_len] = '\0';
    if (!redp2p_parse_u(port_buf, 1, 65535, &parsed)) return 0;
    *port = (unsigned short)parsed;
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_xor_addr(unsigned char *out, size_t cap,
    const struct sockaddr_storage *addr, const unsigned char txid[12],
    size_t *out_len)
{
    unsigned short port;
    unsigned char raw[16], mask[16];
    size_t n, i;

    if (!out || !addr || !out_len || cap < 8) return 0;
    memset(out, 0, cap);
    if (addr->ss_family == AF_INET) {
        port = ntohs(((const struct sockaddr_in *)addr)->sin_port);
        memcpy(raw, &((const struct sockaddr_in *)addr)->sin_addr, 4);
        out[1] = 0x01; n = 4;
    } else if (addr->ss_family == AF_INET6 && cap >= 20) {
        port = ntohs(((const struct sockaddr_in6 *)addr)->sin6_port);
        memcpy(raw, &((const struct sockaddr_in6 *)addr)->sin6_addr, 16);
        out[1] = 0x02; n = 16;
    } else {
        return 0;
    }
    out[2] = (unsigned char)((port ^ (REDP2P_STUN_MAGIC >> 16)) >> 8);
    out[3] = (unsigned char)(port ^ (REDP2P_STUN_MAGIC >> 16));
    mask[0] = 0x21; mask[1] = 0x12; mask[2] = 0xa4; mask[3] = 0x42;
    memcpy(mask + 4, txid, 12);
    for (i = 0; i < n; i++) out[4 + i] = raw[i] ^ mask[i];
    *out_len = 4 + n;
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_decode_xor_addr(const unsigned char *buf, int offset,
    int attr_len, const unsigned char txid[12], struct sockaddr_storage *addr,
    socklen_t *addr_len)
{
    unsigned short xport, port;
    unsigned char mask[16], raw[16];
    int family, n, i;

    if (!buf || !addr || attr_len < 8) return 0;
    family = buf[offset + 1];
    if (family == 0x01) n = 4;
    else if (family == 0x02 && attr_len >= 20) n = 16;
    else return 0;
    xport = (unsigned short)(((unsigned)buf[offset + 2] << 8) |
        buf[offset + 3]);
    port = xport ^ (unsigned short)(REDP2P_STUN_MAGIC >> 16);
    mask[0] = 0x21; mask[1] = 0x12; mask[2] = 0xa4; mask[3] = 0x42;
    memcpy(mask + 4, txid, 12);
    for (i = 0; i < n; i++) raw[i] = buf[offset + 4 + i] ^ mask[i];
    memset(addr, 0, sizeof(*addr));
    if (n == 4) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)addr;
        v4->sin_family = AF_INET; v4->sin_port = htons(port);
        memcpy(&v4->sin_addr, raw, 4);
        if (addr_len) *addr_len = sizeof(*v4);
    } else {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;
        v6->sin6_family = AF_INET6; v6->sin6_port = htons(port);
        memcpy(&v6->sin6_addr, raw, 16);
        if (addr_len) *addr_len = sizeof(*v6);
    }
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_copy_attr_string(const unsigned char *buf, int len,
    int type, char *out, size_t out_cap)
{
    int attr_len, offset;

    offset = redp2p_stun_find(buf, len, type, &attr_len);
    if (offset < 0 || attr_len < 0 || (size_t)attr_len >= out_cap) return 0;
    memcpy(out, buf + offset, (size_t)attr_len);
    out[attr_len] = '\0';
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_error_code(const unsigned char *buf, int len)
{
    int attr_len, offset;

    offset = redp2p_stun_find(buf, len, REDP2P_TURN_ATTR_ERROR_CODE, &attr_len);
    if (offset < 0 || attr_len < 4) return 0;
    return (buf[offset + 2] & 0x07) * 100 + buf[offset + 3];
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_auth_challenge(redp2p_turn_allocation_t *allocation,
    const unsigned char *buf, int len)
{
    int code;

    code = redp2p_turn_error_code(buf, len);
    if (code != 401 && code != 438) return 0;
    if (!allocation) return 0;
    if (!redp2p_turn_copy_attr_string(buf, len, REDP2P_TURN_ATTR_REALM,
        allocation->realm, sizeof(allocation->realm)) &&
        !allocation->realm[0])
        return 0;
    if (!redp2p_turn_copy_attr_string(buf, len, REDP2P_TURN_ATTR_NONCE,
        allocation->nonce, sizeof(allocation->nonce)))
        return 0;
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static redp2p_turn_allocation_t *redp2p_turn_find_allocation(
    redp2p_t *ctx, redp2p_fd_t fd)
{
    int i;

    if (!ctx) return NULL;
    for (i = 0; i < REDP2P_TURN_ALLOCATIONS_MAX; i++) {
        if (ctx->turn_allocations[i].used &&
            ctx->turn_allocations[i].fd == fd)
            return &ctx->turn_allocations[i];
    }
    return NULL;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static redp2p_turn_allocation_t *redp2p_turn_reserve_allocation(
    redp2p_t *ctx, redp2p_fd_t fd)
{
    redp2p_turn_allocation_t *allocation;
    int i;

    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (allocation) {
        memset(allocation, 0, sizeof(*allocation));
        allocation->fd = fd;
        allocation->used = 1;
        return allocation;
    }
    for (i = 0; i < REDP2P_TURN_ALLOCATIONS_MAX; i++) {
        if (!ctx->turn_allocations[i].used) {
            allocation = &ctx->turn_allocations[i];
            memset(allocation, 0, sizeof(*allocation));
            allocation->fd = fd;
            allocation->used = 1;
            return allocation;
        }
    }
    return NULL;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_send_raw(redp2p_turn_allocation_t *allocation,
    const unsigned char *buf, size_t len)
{
    if (!allocation || !allocation->used || !allocation->server_addr_len)
        return -1;
    return (int)sendto(allocation->fd, (const char *)buf, len, 0,
        (const struct sockaddr *)&allocation->server_addr,
        allocation->server_addr_len);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_wait_response(redp2p_turn_allocation_t *allocation,
    const unsigned char txid[12], unsigned char *out, size_t cap, int *out_len)
{
    uint64_t deadline;

    if (!allocation) return 0;
    deadline = redp2p_now_ms() + 3000;
    while (redp2p_now_ms() < deadline) {
        struct sockaddr_storage from;
        socklen_t from_len;
        unsigned char rxid[12];
        int remaining;
        int n;

        remaining = (int)(deadline - redp2p_now_ms());
        if (redp2p_wait_readable(allocation->fd, remaining) <= 0) return 0;
        from_len = sizeof(from);
        n = (int)recvfrom(allocation->fd, (char *)out, cap, 0,
            (struct sockaddr *)&from, &from_len);
        if (n < 20 || !redp2p_sockaddr_equal(&from,
            &allocation->server_addr))
            continue;
        if (redp2p_stun_hdr(out, n, rxid) < 0 ||
            memcmp(rxid, txid, 12) != 0)
            continue;
        *out_len = n;
        return 1;
    }
    return 0;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_add_auth(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char *buf,
    size_t cap, int *off)
{
    if (!redp2p_turn_auth_attrs(ctx, allocation, buf, cap, off)) return 0;
    return redp2p_turn_integrity(ctx, allocation, buf, cap, off);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_build_allocate(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char *buf,
    size_t cap, unsigned char txid[12], int authenticated)
{
    unsigned char transport[4] = {17, 0, 0, 0};
    int off;

    if (!redp2p_stun_gen_id(txid)) return 0;
    off = redp2p_stun_build(buf, REDP2P_TURN_ALLOCATE_REQ, txid);
    if (!redp2p_turn_attr(buf, cap, &off, REDP2P_TURN_ATTR_TRANSPORT,
        transport, sizeof(transport)))
        return 0;
    if (authenticated) {
        if (!redp2p_turn_add_auth(ctx, allocation, buf, cap, &off)) return 0;
    } else {
        redp2p_stun_len(buf, off);
    }
    return off;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_parse_allocate(redp2p_turn_allocation_t *allocation,
    const unsigned char *buf, int len, const unsigned char txid[12])
{
    int attr_len;
    int offset;
    uint32_t lifetime;

    if (!allocation) return 0;
    offset = redp2p_stun_find(buf, len, REDP2P_TURN_ATTR_XOR_RELAYED,
        &attr_len);
    if (offset < 0 || !redp2p_turn_decode_xor_addr(buf, offset, attr_len,
        txid, &allocation->relay_addr, &allocation->relay_addr_len))
        return 0;
    allocation->lifetime_s = REDP2P_TURN_DEFAULT_LIFETIME;
    offset = redp2p_stun_find(buf, len, REDP2P_TURN_ATTR_LIFETIME, &attr_len);
    if (offset >= 0 && attr_len == 4) {
        lifetime = redp2p_turn_load_be32(buf + offset);
        if (lifetime > 0) allocation->lifetime_s = lifetime;
    }
    allocation->refresh_at_ms = redp2p_now_ms() +
        (uint64_t)allocation->lifetime_s * 500u;
    allocation->ready = 1;
    memset(allocation->permissions, 0, sizeof(allocation->permissions));
    return 1;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_allocate(redp2p_t *ctx, redp2p_fd_t fd)
{
    unsigned char tx[4096];
    unsigned char rx[4096];
    unsigned char txid[12];
    unsigned char rxid[12];
    redp2p_turn_allocation_t *allocation;
    char host[256];
    unsigned short port;
    int n;
    int len;
    int type;
    int attempt;
    int status;

    if (!ctx || !ctx->turn_url[0]) return REDP2P_EINVAL;
    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (allocation && allocation->ready) return REDP2P_OK;
    allocation = redp2p_turn_reserve_allocation(ctx, fd);
    if (!allocation) return REDP2P_ERROR;
    status = REDP2P_ERROR;
    if (!redp2p_turn_parse_url(ctx->turn_url, host, sizeof(host), &port)) {
        status = REDP2P_EINVAL;
        goto fail;
    }
    if (redp2p_resolve(host, port, SOCK_DGRAM, &allocation->server_addr,
        &allocation->server_addr_len) != 0)
    {
        status = REDP2P_ENET;
        goto fail;
    }
    n = redp2p_turn_build_allocate(ctx, allocation, tx, sizeof(tx), txid, 0);
    if (n <= 0) goto fail;
    if (redp2p_turn_send_raw(allocation, tx, (size_t)n) < 0 ||
        !redp2p_turn_wait_response(allocation, txid, rx, sizeof(rx), &len))
    {
        status = REDP2P_ENET;
        goto fail;
    }
    type = redp2p_stun_hdr(rx, len, rxid);
    if (type == REDP2P_TURN_ALLOCATE_OK) {
        if (redp2p_turn_parse_allocate(allocation, rx, len, txid))
            return REDP2P_OK;
        status = REDP2P_EPROTO;
        goto fail;
    }
    if (type != REDP2P_TURN_ALLOCATE_ERR) {
        status = REDP2P_EPROTO;
        goto fail;
    }
    if (!redp2p_turn_auth_challenge(allocation, rx, len) ||
        !ctx->turn_user[0])
    {
        status = REDP2P_EAUTH;
        goto fail;
    }
    for (attempt = 0; attempt < 2; attempt++) {
        n = redp2p_turn_build_allocate(ctx, allocation, tx, sizeof(tx), txid, 1);
        if (n <= 0) goto fail;
        if (redp2p_turn_send_raw(allocation, tx, (size_t)n) < 0 ||
            !redp2p_turn_wait_response(allocation, txid, rx, sizeof(rx), &len))
        {
            status = REDP2P_ENET;
            goto fail;
        }
        type = redp2p_stun_hdr(rx, len, rxid);
        if (type == REDP2P_TURN_ALLOCATE_OK) {
            if (redp2p_turn_parse_allocate(allocation, rx, len, txid))
                return REDP2P_OK;
            status = REDP2P_EPROTO;
            goto fail;
        }
        if (type != REDP2P_TURN_ALLOCATE_ERR) {
            status = REDP2P_EPROTO;
            goto fail;
        }
        if (!redp2p_turn_auth_challenge(allocation, rx, len)) {
            status = REDP2P_EAUTH;
            goto fail;
        }
    }
    status = REDP2P_EAUTH;

fail:
    memset(allocation, 0, sizeof(*allocation));
    return status;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_build_refresh(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation, unsigned char *buf, size_t cap,
    unsigned char txid[12], uint32_t lifetime_s)
{
    unsigned char lifetime[4];
    int off;

    if (!allocation || !redp2p_stun_gen_id(txid)) return 0;
    redp2p_turn_store_be32(lifetime, lifetime_s);
    off = redp2p_stun_build(buf, REDP2P_TURN_REFRESH_REQ, txid);
    if (!redp2p_turn_attr(buf, cap, &off, REDP2P_TURN_ATTR_LIFETIME,
        lifetime, sizeof(lifetime)) ||
        !redp2p_turn_add_auth(ctx, allocation, buf, cap, &off))
        return 0;
    return off;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
static void redp2p_turn_refresh_if_due(redp2p_t *ctx, redp2p_fd_t fd)
{
    unsigned char tx[2048];
    unsigned char txid[12];
    redp2p_turn_allocation_t *allocation;
    int n;

    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (!allocation || !allocation->ready ||
        redp2p_now_ms() < allocation->refresh_at_ms)
        return;
    n = redp2p_turn_build_refresh(ctx, allocation, tx, sizeof(tx), txid,
        allocation->lifetime_s ? allocation->lifetime_s :
        REDP2P_TURN_DEFAULT_LIFETIME);
    if (n > 0 && redp2p_turn_send_raw(allocation, tx, (size_t)n) >= 0)
        allocation->refresh_at_ms = redp2p_now_ms() +
            (uint64_t)(allocation->lifetime_s ?
            allocation->lifetime_s : REDP2P_TURN_DEFAULT_LIFETIME) * 500u;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_same_host(const struct sockaddr_storage *a,
    const struct sockaddr_storage *b)
{
    if (!a || !b || a->ss_family != b->ss_family) return 0;
    if (a->ss_family == AF_INET)
        return memcmp(&((const struct sockaddr_in *)a)->sin_addr,
            &((const struct sockaddr_in *)b)->sin_addr,
            sizeof(struct in_addr)) == 0;
    if (a->ss_family == AF_INET6)
        return memcmp(&((const struct sockaddr_in6 *)a)->sin6_addr,
            &((const struct sockaddr_in6 *)b)->sin6_addr,
            sizeof(struct in6_addr)) == 0;
    return 0;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_permission_slot(redp2p_turn_allocation_t *allocation,
    const struct sockaddr_storage *peer)
{
    int i;
    int free_slot;

    if (!allocation) return -1;
    free_slot = -1;
    for (i = 0; i < REDP2P_TURN_PERMISSIONS_MAX; i++) {
        if (allocation->permissions[i].used &&
            redp2p_turn_same_host(&allocation->permissions[i].addr, peer))
            return i;
        if (!allocation->permissions[i].used && free_slot < 0) free_slot = i;
    }
    return free_slot >= 0 ? free_slot : 0;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_permission(redp2p_t *ctx, redp2p_fd_t fd,
    const struct sockaddr_storage *peer)
{
    unsigned char tx[2048];
    unsigned char rx[2048];
    unsigned char txid[12];
    unsigned char rxid[12];
    unsigned char xaddr[20];
    redp2p_turn_allocation_t *allocation;
    size_t xaddr_len;
    int off;
    int slot;
    int len;
    int type;
    int attempt;
    uint64_t now;

    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (!allocation || !allocation->ready) return 0;
    slot = redp2p_turn_permission_slot(allocation, peer);
    if (slot < 0) return 0;
    now = redp2p_now_ms();
    if (allocation->permissions[slot].used &&
        allocation->permissions[slot].expires_ms > now)
        return 1;
    for (attempt = 0; attempt < 2; attempt++) {
        if (!redp2p_stun_gen_id(txid) ||
            !redp2p_turn_xor_addr(xaddr, sizeof(xaddr), peer, txid,
                &xaddr_len))
            return 0;
        off = redp2p_stun_build(tx, REDP2P_TURN_PERMISSION_REQ, txid);
        if (!redp2p_turn_attr(tx, sizeof(tx), &off,
            REDP2P_TURN_ATTR_XOR_PEER, xaddr, xaddr_len) ||
            !redp2p_turn_add_auth(ctx, allocation, tx, sizeof(tx), &off))
            return 0;
        if (redp2p_turn_send_raw(allocation, tx, (size_t)off) < 0 ||
            !redp2p_turn_wait_response(allocation, txid, rx, sizeof(rx),
                &len))
            return 0;
        type = redp2p_stun_hdr(rx, len, rxid);
        if (type == REDP2P_TURN_PERMISSION_OK) {
            allocation->permissions[slot].addr = *peer;
            allocation->permissions[slot].expires_ms =
                redp2p_now_ms() + REDP2P_TURN_PERMISSION_MS;
            allocation->permissions[slot].used = 1;
            return 1;
        }
        if (type != REDP2P_TURN_PERMISSION_ERR ||
            !redp2p_turn_auth_challenge(allocation, rx, len))
            return 0;
    }
    return 0;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_send_indication(redp2p_t *ctx, redp2p_fd_t fd,
    const void *data, size_t len, const struct sockaddr_storage *peer)
{
    unsigned char tx[REDP2P_TURN_PACKET_MAX];
    unsigned char txid[12];
    unsigned char xaddr[20];
    redp2p_turn_allocation_t *allocation;
    size_t xaddr_len;
    int off;

    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (!allocation || !allocation->ready || len > 65535u ||
        !redp2p_turn_permission(ctx, fd, peer) ||
        !redp2p_stun_gen_id(txid) ||
        !redp2p_turn_xor_addr(xaddr, sizeof(xaddr), peer, txid, &xaddr_len))
        return -1;
    off = redp2p_stun_build(tx, REDP2P_TURN_SEND_IND, txid);
    if (!redp2p_turn_attr(tx, sizeof(tx), &off, REDP2P_TURN_ATTR_XOR_PEER,
        xaddr, xaddr_len) ||
        !redp2p_turn_attr(tx, sizeof(tx), &off, REDP2P_TURN_ATTR_DATA,
            data, len))
        return -1;
    redp2p_stun_len(tx, off);
    return redp2p_turn_send_raw(allocation, tx, (size_t)off) < 0 ?
        -1 : (int)len;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
static int redp2p_turn_candidate(redp2p_t *ctx, redp2p_fd_t fd,
    redp2p_candidate_t *candidate)
{
    redp2p_turn_allocation_t *allocation;
    char text[REDP2P_ADDR_MAX + 1];
    unsigned short port;
    int status;

    if (!candidate) return REDP2P_EINVAL;
    status = redp2p_turn_allocate(ctx, fd);
    if (status != REDP2P_OK) return status;
    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (!allocation || !allocation->ready) return REDP2P_ENET;
    redp2p_turn_refresh_if_due(ctx, fd);
    if (allocation->relay_addr.ss_family == AF_INET) {
        const struct sockaddr_in *v4 =
            (const struct sockaddr_in *)&allocation->relay_addr;
        if (!inet_ntop(AF_INET, &v4->sin_addr, text, sizeof(text)))
            return REDP2P_EPROTO;
        port = ntohs(v4->sin_port);
    } else if (allocation->relay_addr.ss_family == AF_INET6) {
        const struct sockaddr_in6 *v6 =
            (const struct sockaddr_in6 *)&allocation->relay_addr;
        if (!inet_ntop(AF_INET6, &v6->sin6_addr, text, sizeof(text)))
            return REDP2P_EPROTO;
        port = ntohs(v6->sin6_port);
    } else {
        return REDP2P_EPROTO;
    }
    memset(candidate, 0, sizeof(*candidate));
    candidate->type = REDP2P_CAND_RELAY;
    snprintf(candidate->addr, sizeof(candidate->addr), "%s", text);
    candidate->port = port;
    candidate->priority = redp2p_candidate_priority(candidate);
    return REDP2P_OK;
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
int redp2p_set_turn_server(redp2p_t *ctx, const char *url,
    const char *username, const char *password)
{
    if (!ctx) return REDP2P_EINVAL;
    if (!url) {
        ctx->turn_url[0] = '\0';
        ctx->turn_user[0] = '\0';
        ctx->turn_pass[0] = '\0';
        memset(ctx->turn_allocations, 0, sizeof(ctx->turn_allocations));
        redp2p_set_error(ctx, NULL);
        return REDP2P_OK;
    }
    if (strncmp(url, "turn:", 5) != 0 || strlen(url) > REDP2P_TURN_URL_MAX ||
        (!!username != !!password) ||
        (username && strlen(username) > REDP2P_TURN_USER_MAX) ||
        (password && strlen(password) > REDP2P_PASS_MAX))
    {
        redp2p_set_error(ctx, "invalid TURN configuration");
        return REDP2P_EINVAL;
    }
    snprintf(ctx->turn_url, sizeof(ctx->turn_url), "%s", url);
    snprintf(ctx->turn_user, sizeof(ctx->turn_user), "%s",
        username ? username : "");
    snprintf(ctx->turn_pass, sizeof(ctx->turn_pass), "%s",
        password ? password : "");
    memset(ctx->turn_allocations, 0, sizeof(ctx->turn_allocations));
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Sends one datagram through the selected peer transport path.
 * Summary: Uses direct UDP or the active TURN allocation transparently.
 * @return Sent byte count, or a negative value on transport failure.
 */
int redp2p_transport_sendto(redp2p_t *ctx, redp2p_fd_t fd,
    const void *buf, size_t len, const struct sockaddr_storage *addr,
    int via_turn)
{
    if (via_turn) {
        redp2p_turn_refresh_if_due(ctx, fd);
        return redp2p_turn_send_indication(ctx, fd, buf, len, addr);
    }
    return redp2p_sendto_addr(fd, buf, len, addr);
}

/**
 * Releases one TURN allocation remotely before local socket teardown.
 * Summary: Sends an authenticated Refresh with lifetime zero without making
 *          cleanup wait for a response that will be discarded on close.
 * @return None.
 */
static void redp2p_turn_deallocate(redp2p_t *ctx,
    redp2p_turn_allocation_t *allocation)
{
    unsigned char tx[2048];
    unsigned char txid[12];
    int n;
    int attempt;

    if (!ctx || !allocation || !allocation->ready) return;
    n = redp2p_turn_build_refresh(ctx, allocation, tx, sizeof(tx), txid, 0);
    if (n <= 0) return;
    for (attempt = 0; attempt < 2; attempt++)
        (void)redp2p_turn_send_raw(allocation, tx, (size_t)n);
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return None.
 */
void redp2p_transport_forget(redp2p_t *ctx, redp2p_fd_t fd)
{
    redp2p_turn_allocation_t *allocation;

    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (allocation) {
        redp2p_turn_deallocate(ctx, allocation);
        crypto_wipe(allocation, sizeof(*allocation));
        allocation->fd = REDP2P_FD_INVALID;
    }
}

/**
 * Handles one internal TURN transport operation.
 * Summary: Supports TURN framing, state, authentication, or relay I/O.
 * @return Operation result or decoded value.
 */
int redp2p_transport_recvfrom(redp2p_t *ctx, redp2p_fd_t fd,
    void *buf, size_t cap, int flags, struct sockaddr_storage *from,
    socklen_t *from_len, int *via_turn)
{
    unsigned char packet[REDP2P_TURN_PACKET_MAX];
    struct sockaddr_storage raw_from;
    redp2p_turn_allocation_t *allocation;
    socklen_t raw_len;
    unsigned char txid[12];
    int n;
    int type;
    int peer_len = 0;
    int peer_off;
    int data_len = 0;
    int data_off;
    size_t copy_len;

    if (via_turn) *via_turn = 0;
    allocation = redp2p_turn_find_allocation(ctx, fd);
    if (!allocation || !allocation->ready) {
        return (int)recvfrom(fd, (char *)buf, cap, flags,
            (struct sockaddr *)from, from_len);
    }
    redp2p_turn_refresh_if_due(ctx, fd);
    raw_len = sizeof(raw_from);
    n = (int)recvfrom(fd, (char *)packet, sizeof(packet), flags,
        (struct sockaddr *)&raw_from, &raw_len);
    if (n < 0) return n;
#ifndef _WIN32
    if ((size_t)n > sizeof(packet)) return -2;
#endif
    if ((size_t)n > sizeof(packet)) return -2;
    if (!allocation || !allocation->ready ||
        !redp2p_sockaddr_equal(&raw_from, &allocation->server_addr))
    {
        if ((size_t)n > cap) return n;
        copy_len = (size_t)n;
        if (copy_len > 0) memcpy(buf, packet, copy_len);
        if (from) *from = raw_from;
        if (from_len) *from_len = raw_len;
        return n;
    }
    type = redp2p_stun_hdr(packet, n, txid);
    if (type == REDP2P_TURN_DATA_IND) {
        struct sockaddr_storage peer;
        socklen_t decoded_len;

        peer_off = redp2p_stun_find(packet, n, REDP2P_TURN_ATTR_XOR_PEER,
            &peer_len);
        data_off = redp2p_stun_find(packet, n, REDP2P_TURN_ATTR_DATA,
            &data_len);
        if (peer_off < 0 || data_off < 0 || data_len < 0 ||
            (size_t)data_len > cap ||
            !redp2p_turn_decode_xor_addr(packet, peer_off, peer_len, txid,
                &peer, &decoded_len))
            return -2;
        if (data_len > 0) memcpy(buf, packet + data_off, (size_t)data_len);
        if (from) *from = peer;
        if (from_len) *from_len = decoded_len;
        if (via_turn) *via_turn = 1;
        return data_len;
    }
    if (type == REDP2P_TURN_REFRESH_OK) {
        int lifetime_len;
        int lifetime_off;

        lifetime_off = redp2p_stun_find(packet, n, REDP2P_TURN_ATTR_LIFETIME,
            &lifetime_len);
        if (lifetime_off >= 0 && lifetime_len == 4) {
            allocation->lifetime_s =
                redp2p_turn_load_be32(packet + lifetime_off);
            if (allocation->lifetime_s == 0) allocation->ready = 0;
            else allocation->refresh_at_ms = redp2p_now_ms() +
                (uint64_t)allocation->lifetime_s * 500u;
        }
    } else if (type == REDP2P_TURN_REFRESH_ERR ||
        type == REDP2P_TURN_PERMISSION_ERR ||
        type == REDP2P_TURN_ALLOCATE_ERR)
    {
        if (redp2p_turn_auth_challenge(allocation, packet, n)) {
            allocation->refresh_at_ms = redp2p_now_ms();
            memset(allocation->permissions, 0,
                sizeof(allocation->permissions));
        }
    }
    return -2;
}

#ifdef REDP2P_TESTING
/**
 * Configures stream fault simulation for one context.
 * Summary: Impairs outgoing KCP datagrams with deterministic loss and
 * reordering so tunnel recovery can be exercised over loopback, where a
 * lossy path cannot be produced from outside the process. Cadences live in
 * context state, default to zero after open, and reset when the context is
 * closed. Intended for transport resilience drills, diagnostics, and test
 * harnesses; normal operation leaves faults disabled.
 * @param ctx Context whose outgoing stream is impaired.
 * @param drop_every Drop every Nth outgoing datagram; 0 disables drops.
 * @param reorder_every Delay every Nth outgoing datagram to force
 *     reordering; 0 disables reordering.
 * @return REDP2P_OK on success or REDP2P_EINVAL on invalid arguments.
 */
int redp2p_test_set_stream_faults(redp2p_t *ctx,
    int drop_every, int reorder_every)
{
    if (!ctx || drop_every < 0 || reorder_every < 0) return REDP2P_EINVAL;
    redp2p_lock(ctx);
    ctx->fault_drop_every = drop_every;
    ctx->fault_reorder_every = reorder_every;
    ctx->fault_drop_counter = 0;
    ctx->fault_reorder_counter = 0;
    redp2p_unlock(ctx);
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

#endif

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
int redp2p_gather_candidates(redp2p_t *ctx, int udp_fd,
    redp2p_candidate_t *out, int out_cap, int *out_count) {
    struct sockaddr_storage udp_sa;
    char stun_ip[REDP2P_ADDR_MAX + 1];
    unsigned short stun_port;
    socklen_t udp_sa_len = sizeof(udp_sa);
    const char *force_turn = getenv("REDP2P_FORCE_TURN");
    int direct_enabled = !(force_turn && strcmp(force_turn, "1") == 0);
    int turn_status = REDP2P_OK;

    memset(&udp_sa, 0, sizeof(udp_sa));
    stun_ip[0] = '\0';
    stun_port = 0;
    *out_count = 0;
    if (getsockname(udp_fd, (struct sockaddr *)&udp_sa, &udp_sa_len) == 0) {
        unsigned short udp_port = redp2p_sockaddr_port(&udp_sa);

        if (direct_enabled && udp_sa.ss_family == AF_INET &&
            redp2p_candidate_dest_allowed(AF_INET,
                &((struct sockaddr_in *)&udp_sa)->sin_addr, udp_port) &&
            *out_count < out_cap)
        {
            char host[64];
            inet_ntop(AF_INET, &((struct sockaddr_in *)&udp_sa)->sin_addr,
                host, sizeof(host));
            out[*out_count].type = REDP2P_CAND_HOST;
            strcpy(out[*out_count].addr, host);
            out[*out_count].port = udp_port;
            out[*out_count].priority = redp2p_candidate_priority(&out[*out_count]);
            (*out_count)++;
        }
        if (direct_enabled && udp_sa.ss_family == AF_INET6 &&
            redp2p_candidate_dest_allowed(AF_INET6,
                &((struct sockaddr_in6 *)&udp_sa)->sin6_addr, udp_port) &&
            *out_count < out_cap)
        {
            char host[64];
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&udp_sa)->sin6_addr,
                host, sizeof(host));
            out[*out_count].type = REDP2P_CAND_HOST;
            strcpy(out[*out_count].addr, host);
            out[*out_count].port = udp_port;
            out[*out_count].priority = redp2p_candidate_priority(&out[*out_count]);
            (*out_count)++;
        }

        redp2p_fd_t test_fd = direct_enabled ? socket(AF_INET, SOCK_DGRAM, 0) :
            REDP2P_FD_INVALID;
        if (!REDP2P_ISERR(test_fd)) {
            struct sockaddr_in target;
            memset(&target, 0, sizeof(target));
            target.sin_family = AF_INET;
            target.sin_port = htons(53);
            inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);

            if (connect(test_fd, (struct sockaddr *)&target, sizeof(target)) == 0) {
                struct sockaddr_in local_sa;
                socklen_t local_sa_len = sizeof(local_sa);
                if (getsockname(test_fd, (struct sockaddr *)&local_sa, &local_sa_len) == 0) {
                    char local_ip[64];
                    inet_ntop(AF_INET, &local_sa.sin_addr, local_ip, sizeof(local_ip));

                    if (redp2p_candidate_dest_allowed(AF_INET,
                            &local_sa.sin_addr, udp_port) && *out_count < out_cap) {
                        out[*out_count].type = REDP2P_CAND_HOST;
                        strcpy(out[*out_count].addr, local_ip);
                        out[*out_count].port = udp_port;
                        out[*out_count].priority = redp2p_candidate_priority(&out[*out_count]);
                        (*out_count)++;
                    }
                }
            }
            REDP2P_FD_CLOSE(test_fd);
        }

        if (direct_enabled && ctx && ctx->stun_url[0]) {
            redp2p_stun_binding(ctx, udp_fd, stun_ip, (int)sizeof(stun_ip),
                &stun_port);
        }
        if (direct_enabled && stun_ip[0] != '\0' && *out_count < out_cap) {
            struct in_addr stun_v4;
            struct in6_addr stun_v6;
            unsigned short srflx_port = stun_port ? stun_port : udp_port;

            if ((inet_pton(AF_INET, stun_ip, &stun_v4) == 1 &&
                redp2p_candidate_dest_allowed(AF_INET, &stun_v4,
                    srflx_port)) ||
                (inet_pton(AF_INET6, stun_ip, &stun_v6) == 1 &&
                redp2p_candidate_dest_allowed(AF_INET6, &stun_v6,
                    srflx_port)))
            {
                out[*out_count].type = REDP2P_CAND_SRFLX;
                snprintf(out[*out_count].addr, sizeof(out[*out_count].addr),
                    "%.47s", stun_ip);
                out[*out_count].port = srflx_port;
                out[*out_count].priority = redp2p_candidate_priority(&out[*out_count]);
                (*out_count)++;
            }
        }
        if (ctx && ctx->turn_url[0] && *out_count < out_cap) {
            redp2p_candidate_t relay;

            turn_status = redp2p_turn_candidate(ctx, (redp2p_fd_t)udp_fd,
                &relay);
            if (turn_status == REDP2P_OK) out[(*out_count)++] = relay;
        }
    }
    if (!direct_enabled) {
        if (!ctx || !ctx->turn_url[0]) {
            redp2p_set_error(ctx,
                "candidate gather: forced TURN requires a TURN server");
            return REDP2P_EINVAL;
        }
        if (turn_status != REDP2P_OK) {
            if (turn_status == REDP2P_EAUTH)
                redp2p_set_error(ctx,
                    "candidate gather: TURN authentication failed");
            else if (turn_status == REDP2P_EINVAL)
                redp2p_set_error(ctx,
                    "candidate gather: invalid TURN configuration");
            else if (turn_status == REDP2P_EPROTO)
                redp2p_set_error(ctx,
                    "candidate gather: invalid TURN response");
            else
                redp2p_set_error(ctx,
                    "candidate gather: TURN relay unavailable");
            return turn_status;
        }
    }
    if (!redp2p_normalize_candidates(out, out_count)) return REDP2P_ERROR;
    if (redp2p_punch_trace_enabled()) {
        char local_text[96];
        int i;

        local_text[0] = '\0';
        redp2p_punch_trace_addr(&udp_sa, local_text, sizeof(local_text));
        fprintf(stderr, "[PUNCH] gather fd=%d local=%s count=%d\n",
            udp_fd, local_text, *out_count);
        for (i = 0; i < *out_count; i++)
            redp2p_punch_trace_candidate("local", &out[i]);
    }
    if (!direct_enabled && *out_count == 0) {
        redp2p_set_error(ctx,
            "candidate gather: forced TURN produced no relay candidate");
        return REDP2P_ENET;
    }
    return REDP2P_OK;
}

/**
 * Sends one punch packet to one candidate endpoint.
 * @param udp_fd      UDP socket fd.
 * @param candidate   Candidate endpoint.
 * @param ping_msg    Punch packet text.
 * @param unsupported Unsupported candidate counter.
 * @return 1 when a packet was sent, 0 otherwise.
 */
static int redp2p_punch_send_candidate(redp2p_t *ctx, int udp_fd,
    const redp2p_candidate_t *candidate, const char *ping_msg, int via_turn,
    int *unsupported)
{
    struct sockaddr_storage cand_sa;

    if (!redp2p_candidate_sockaddr(candidate, &cand_sa)) {
        if (unsupported) (*unsupported)++;
        return 0;
    }
    return redp2p_transport_sendto(ctx, (redp2p_fd_t)udp_fd, ping_msg,
        strlen(ping_msg), &cand_sa, via_turn) >= 0 ? 1 : 0;
}

/**
 * Waits for one valid punch response within the monotonic deadline.
 * @param udp_fd       UDP socket fd.
 * @param session_id   Expected session id.
 * @param from_id      Local peer id.
 * @param to_id        Remote peer id.
 * @param wait_ms      Maximum wait for this step.
 * @param deadline_ms  Absolute monotonic deadline.
 * @param selected_addr Output selected address.
 * @param malformed    Malformed packet counter.
 * @param mismatched   Session or peer mismatch counter.
 * @return REDP2P_OK on valid response, REDP2P_ETIMEOUT otherwise.
 */
static int redp2p_punch_wait_response(redp2p_t *ctx, int udp_fd,
    const char *session_id, const char *from_id, const char *to_id, int wait_ms,
    uint64_t deadline_ms, struct sockaddr_storage *selected_addr,
    int *selected_via_turn, redp2p_punch_packet_handler_t packet_handler,
    void *packet_userdata, int *malformed, int *mismatched)
{
    uint64_t wait_deadline_ms;

    wait_deadline_ms = redp2p_now_ms() + (uint64_t)wait_ms;
    if (wait_deadline_ms > deadline_ms) wait_deadline_ms = deadline_ms;
    while (redp2p_now_ms() < wait_deadline_ms) {
        char recv_buf[REDP2P_BUF + 1];
        struct sockaddr_storage src_addr;
        socklen_t src_len = sizeof(src_addr);
        uint64_t now;
        int remaining_ms;
        int n;
        int via_turn;

        now = redp2p_now_ms();
        remaining_ms = (int)(wait_deadline_ms - now);
        if (redp2p_wait_readable((redp2p_fd_t)udp_fd, remaining_ms) <= 0)
            return REDP2P_ETIMEOUT;
        via_turn = 0;
        n = redp2p_transport_recvfrom(ctx, (redp2p_fd_t)udp_fd, recv_buf,
            sizeof(recv_buf) - 1, 0, &src_addr, &src_len, &via_turn);
        if (n == -2) continue;
        if (n > 0) {
            if (redp2p_punch_trace_enabled()) {
                char endpoint[96];

                redp2p_punch_trace_addr(&src_addr, endpoint,
                    sizeof(endpoint));
                fprintf(stderr,
                    "[PUNCH] recv source=%s bytes=%d via_turn=%d\n",
                    endpoint, n, via_turn);
            }
            char rx_sess[64] = {0};
            char rx_from[REDP2P_ID_MAX + 1] = {0};
            char rx_to[REDP2P_ID_MAX + 1] = {0};
            int is_ping;
            int is_pong;

            if (packet_handler && packet_handler(packet_userdata,
                (const unsigned char *)recv_buf, (size_t)n, &src_addr,
                via_turn))
                continue;
            recv_buf[n] = '\0';
            is_pong = redp2p_parse_punch_packet(recv_buf,
                REDP2P_CTRTOK_PUNCH_PONG,
                rx_sess, rx_from, rx_to);
            is_ping = 0;
            if (!is_pong) {
                is_ping = redp2p_parse_punch_packet(recv_buf,
                    REDP2P_CTRTOK_PUNCH_PING, rx_sess, rx_from, rx_to);
            }
            if (!is_ping && !is_pong) {
                unsigned char stun_id[12];

                if (redp2p_stun_hdr((const unsigned char *)recv_buf, n,
                    stun_id) >= 0)
                    continue;
                if (malformed) (*malformed)++;
                continue;
            }
            if (((is_ping && strcmp(rx_from, to_id) == 0 &&
                strcmp(rx_to, from_id) == 0) ||
                (is_pong && strcmp(rx_from, from_id) == 0 &&
                strcmp(rx_to, to_id) == 0)) &&
                strcmp(rx_sess, session_id) == 0)
            {
                if (is_ping) {
                    char pong_msg[256];
                    int burst;

                    snprintf(pong_msg, sizeof(pong_msg), "%s%s:%s:%s\n",
                        REDP2P_CTRTOK_PUNCH_PONG, session_id, rx_from, rx_to);
                    for (burst = 0; burst < REDP2P_PUNCH_CONFIRM_BURST;
                        burst++)
                    {
                        redp2p_transport_sendto(ctx, (redp2p_fd_t)udp_fd,
                            pong_msg, strlen(pong_msg), &src_addr, via_turn);
                    }
                }
                *selected_addr = src_addr;
                if (selected_via_turn) *selected_via_turn = via_turn;
                if (redp2p_punch_trace_enabled()) {
                    char endpoint[96];

                    redp2p_punch_trace_addr(&src_addr, endpoint,
                        sizeof(endpoint));
                    fprintf(stderr,
                        "[PUNCH] selected source=%s kind=%s\n",
                        endpoint, is_ping ? "ping" : "pong");
                }
                return REDP2P_OK;
            }
            if (mismatched) (*mismatched)++;
        }
    }
    return REDP2P_ETIMEOUT;
}

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
int redp2p_punch_select(redp2p_t *ctx, int sweep_limit, int udp_fd,
    const char *session_id, const char *from_id, const char *to_id,
    const redp2p_candidate_t *remote_candidates, int remote_candidate_count,
    struct sockaddr_storage *selected_addr, int *selected_via_turn,
    redp2p_punch_packet_handler_t packet_handler, void *packet_userdata) {
    char ping_msg[256];
    uint64_t deadline_ms;
    int direct_count;
    int relay_count;
    int local_turn;
    int sent_count;
    uint64_t next_stun_keepalive_ms;
    int malformed_count;
    int mismatch_count;
    int unsupported_count;

    if (selected_via_turn) *selected_via_turn = 0;
    if (redp2p_punch_trace_enabled()) {
        struct sockaddr_storage local_sa;
        socklen_t local_len;
        char local_text[96];
        int c;

        memset(&local_sa, 0, sizeof(local_sa));
        local_len = sizeof(local_sa);
        local_text[0] = '\0';
        if (getsockname(udp_fd, (struct sockaddr *)&local_sa, &local_len) == 0)
            redp2p_punch_trace_addr(&local_sa, local_text,
                sizeof(local_text));
        fprintf(stderr,
            "[PUNCH] begin fd=%d local=%s session=%s self=%s peer=%s "
            "remote_count=%d\n",
            udp_fd, local_text, session_id, from_id, to_id,
            remote_candidate_count);
        for (c = 0; c < remote_candidate_count; c++)
            redp2p_punch_trace_candidate("remote", &remote_candidates[c]);
    }
    if (remote_candidate_count <= 0) {
        redp2p_set_error(ctx, "punch: no candidates");
        return REDP2P_ERROR;
    }
    if (sweep_limit < 0) sweep_limit = 0;
    deadline_ms = redp2p_now_ms() + REDP2P_PUNCH_TOTAL_MS;
    direct_count = 0;
    relay_count = 0;
    local_turn = redp2p_turn_find_allocation(ctx,
        (redp2p_fd_t)udp_fd) != NULL;
    sent_count = 0;
    next_stun_keepalive_ms = 0;
    malformed_count = 0;
    mismatch_count = 0;
    unsupported_count = 0;
    for (int c = 0; c < remote_candidate_count; c++) {
        if (remote_candidates[c].type == REDP2P_CAND_RELAY) relay_count++;
        else if (remote_candidates[c].priority < 300u) direct_count++;
    }
    snprintf(ping_msg, sizeof(ping_msg), "%s%s:%s:%s\n",
        REDP2P_CTRTOK_PUNCH_PING, session_id, from_id, to_id);
    if (ctx && ctx->stun_url[0]) {
        (void)redp2p_stun_keepalive(ctx, udp_fd);
        next_stun_keepalive_ms = redp2p_now_ms() + REDP2P_STUN_KEEPALIVE_MS;
    }
    for (int i = 0; direct_count > 0 && i < REDP2P_PUNCH_DIRECT_ROUNDS; i++) {
        if (next_stun_keepalive_ms > 0 &&
            redp2p_now_ms() >= next_stun_keepalive_ms)
        {
            (void)redp2p_stun_keepalive(ctx, udp_fd);
            next_stun_keepalive_ms =
                redp2p_now_ms() + REDP2P_STUN_KEEPALIVE_MS;
        }
        for (int c = 0; c < remote_candidate_count; c++) {
            if (remote_candidates[c].type != REDP2P_CAND_SRFLX) continue;
            sent_count += redp2p_punch_send_candidate(ctx, udp_fd,
                &remote_candidates[c], ping_msg, 0, &unsupported_count);
        }
        for (int c = 0; c < remote_candidate_count; c++) {
            if (remote_candidates[c].type == REDP2P_CAND_SRFLX ||
                remote_candidates[c].priority >= 300u)
                continue;
            sent_count += redp2p_punch_send_candidate(ctx, udp_fd,
                &remote_candidates[c], ping_msg, 0, &unsupported_count);
        }
        if (redp2p_punch_wait_response(ctx, udp_fd, session_id, from_id,
            to_id, REDP2P_PUNCH_DIRECT_WAIT_MS, deadline_ms, selected_addr,
            selected_via_turn, packet_handler, packet_userdata,
            &malformed_count, &mismatch_count) == REDP2P_OK)
            return REDP2P_OK;
    }
    for (int sweep = 1; direct_count > 0 && sweep <= sweep_limit &&
        redp2p_now_ms() < deadline_ms; sweep++)
    {
        for (int sign = -1; sign <= 1 && redp2p_now_ms() < deadline_ms;
            sign += 2)
        {
            int offset = sweep * sign;
            for (int c = 0; c < remote_candidate_count; c++) {
                struct sockaddr_storage exact_sa;
                int test_port;

                if (remote_candidates[c].type == REDP2P_CAND_RELAY) continue;
                sent_count += redp2p_punch_send_candidate(ctx, udp_fd,
                    &remote_candidates[c], ping_msg, 0, &unsupported_count);
                if (!redp2p_candidate_sockaddr(&remote_candidates[c], &exact_sa))
                    continue;
                if (exact_sa.ss_family != AF_INET) continue;
                if (remote_candidates[c].type != REDP2P_CAND_OBSERVED)
                    continue;
                test_port = remote_candidates[c].port + offset;
                if (test_port <= 0 || test_port > 65535) continue;
                redp2p_sockaddr_set_port(&exact_sa, (unsigned short)test_port);
                if (redp2p_transport_sendto(ctx, (redp2p_fd_t)udp_fd,
                    ping_msg, strlen(ping_msg), &exact_sa, 0) >= 0)
                    sent_count++;
            }
            if (redp2p_punch_wait_response(ctx, udp_fd, session_id,
                from_id, to_id, REDP2P_PUNCH_SWEEP_WAIT_MS, deadline_ms,
                selected_addr, selected_via_turn, packet_handler,
                packet_userdata, &malformed_count,
                &mismatch_count) == REDP2P_OK)
                return REDP2P_OK;
        }
    }
    if ((relay_count > 0 || local_turn) && redp2p_now_ms() < deadline_ms) {
        while (redp2p_now_ms() < deadline_ms) {
            for (int cidx = 0; cidx < remote_candidate_count; cidx++) {
                if (remote_candidates[cidx].type == REDP2P_CAND_RELAY) {
                    sent_count += redp2p_punch_send_candidate(ctx, udp_fd,
                        &remote_candidates[cidx], ping_msg, local_turn,
                        &unsupported_count);
                }
                if (local_turn &&
                    remote_candidates[cidx].type != REDP2P_CAND_RELAY)
                {
                    sent_count += redp2p_punch_send_candidate(ctx, udp_fd,
                        &remote_candidates[cidx], ping_msg, 1,
                        &unsupported_count);
                }
            }
            if (redp2p_punch_wait_response(ctx, udp_fd, session_id,
                from_id, to_id, REDP2P_PUNCH_DIRECT_WAIT_MS, deadline_ms,
                selected_addr, selected_via_turn, packet_handler,
                packet_userdata, &malformed_count,
                &mismatch_count) == REDP2P_OK)
                return REDP2P_OK;
        }
    }
    if (redp2p_punch_trace_enabled()) {
        fprintf(stderr,
            "[PUNCH] failed self=%s peer=%s sent=%d malformed=%d "
            "mismatch=%d unsupported=%d deadline=%d\n",
            from_id, to_id, sent_count, malformed_count, mismatch_count,
            unsupported_count, redp2p_now_ms() >= deadline_ms ? 1 : 0);
    }
    if (sent_count == 0) {
        redp2p_set_error(ctx, "punch: no valid peer candidates");
    } else if (malformed_count > 0) {
        redp2p_set_error(ctx, "punch: malformed peer response");
    } else if (mismatch_count > 0) {
        redp2p_set_error(ctx, "punch: peer session identity mismatch");
    } else if (unsupported_count > 0) {
        redp2p_set_error(ctx, "punch: peer address family unsupported");
    } else if (redp2p_now_ms() >= deadline_ms) {
        redp2p_set_error(ctx, "punch: peer connectivity timed out");
    } else {
        redp2p_set_error(ctx, "punch: peer connectivity attempts exhausted");
    }
    return REDP2P_EPUNCH;
}

/**
 * List publishers.
 * @return 0 on success, negative error code on failure.
 */
int redp2p_idx_query_publishers(
    redp2p_t *ctx,
    const char *index_host,
    unsigned short index_port,
    redp2p_publisher_cb cb,
    void *userdata)
{
    JSON_Value *request;
    JSON_Value *response;
    JSON_Object *obj;
    JSON_Object *out;
    JSON_Array *ids;
    size_t count;
    size_t i;
    int result;

    if (!ctx) return REDP2P_EINVAL;
    if (!index_host || !index_host[0] || index_port == 0 || !cb) {
        redp2p_set_error(ctx, "list: invalid arguments");
        return REDP2P_EINVAL;
    }
    redp2p_set_error(ctx, NULL);
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "list: index request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "list");
    response = NULL;
    result = redp2p_http_client(ctx, "list", index_host, index_port, request,
        &response);
    json_value_free(request);
    if (result != REDP2P_OK) return result;
    out = json_value_get_object(response);
    if (!out || !json_object_has_value_of_type(out, "ids", JSONArray)) {
        json_value_free(response);
        redp2p_set_error(ctx, "list: malformed index response");
        return REDP2P_EPROTO;
    }
    ids = json_object_get_array(out, "ids");
    count = json_array_get_count(ids);
    for (i = 0; i < count; i++) {
        const char *id;

        id = json_array_get_string(ids, i);
        if (!id || !redp2p_is_valid_id(id)) {
            json_value_free(response);
            redp2p_set_error(ctx, "list: malformed publisher id");
            return REDP2P_EPROTO;
        }
        cb(id, userdata);
    }
    json_value_free(response);
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

#ifdef REDP2P_TEST_RANDOM
/**
 * Generates one STUN transaction identifier through the test-visible path.
 * @param out Output transaction identifier.
 * @return 1 on success, 0 on error.
 */
int redp2p_test_stun_gen_id(unsigned char out[12]) {
    return redp2p_stun_gen_id(out);
}
#endif

#ifdef REDP2P_TEST_RANDOM
/**
 * Compares STUN transaction identifiers through the test-visible path.
 * @param expected Expected transaction identifier.
 * @param actual   Actual transaction identifier.
 * @return 1 when equal, 0 otherwise.
 */
int redp2p_test_stun_id_matches(const unsigned char expected[12],
const unsigned char actual[12])
{
    return memcmp(expected, actual, 12) == 0;
}
#endif
