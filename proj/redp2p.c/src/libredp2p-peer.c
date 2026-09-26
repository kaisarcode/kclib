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
#define REDP2P_PUNCH_DIRECT_ROUNDS 3
#define REDP2P_PUNCH_DIRECT_WAIT_MS 500
#define REDP2P_PUNCH_SWEEP_WAIT_MS 20
#define REDP2P_PUNCH_TOTAL_MS 4000
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
 * Shuts down the local write side of one TCP socket.
 * @return None.
 */
static void redp2p_shutdown_write(redp2p_fd_t fd);

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
int redp2p_udp_send(redp2p_fd_t fd,
const struct sockaddr_storage *peer, uint8_t role, uint8_t type,
const void *payload, size_t payload_len)
{
    unsigned char frame[REDP2P_SESSION_ENVELOPE_SZ + REDP2P_UDP_PAYLOAD_MAX];
    const unsigned char session_id[REDP2P_SESSION_ID_SZ] = {0};
    size_t len;

    len = redp2p_session_pack(type, role, REDP2P_PROTO_UDP, session_id,
        payload, payload_len, frame);
    if (!len) return -1;
    return redp2p_sendto_addr(fd, frame, len, peer);
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
    socklen_t peer_len;

    peer_len = redp2p_sockaddr_len(&adapter->peer_addr);
    if (peer_len == 0 || sendto(adapter->fd, (const char *)frame, frame_len, 0,
        (const struct sockaddr *)&adapter->peer_addr, peer_len) < 0)
        return -1;
    return 0;
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
    const char *session_hex, uint8_t transport_protocol)
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
    st->adapter->protocol = transport_protocol;
    memcpy(st->adapter->session_id, session_id, REDP2P_SESSION_ID_SZ);
    st->kcp = ikcp_create(redp2p_stream_conv(session_id), st->adapter);
    if (!st->kcp || ikcp_setmtu(st->kcp, REDP2P_STREAM_KCP_MTU) != 0 ||
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
    st->transport_protocol = transport_protocol;
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
 * Reports whether the local TCP side may queue more bytes in KCP.
 * @return 1 when local TCP reads may continue, 0 otherwise.
 */
int redp2p_stream_can_send_data(const redp2p_stream_state_t *st) {
    return st->ready && !st->local_eof && st->kcp &&
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
        st->transport_protocol, st->session_id, NULL, 0, frame);
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
 * Flushes at most one reconstructed KCP chunk into the local TCP socket.
 *
 * Partial and would-block writes stay in the stream state. This bounds work
 * per session and prevents one slow local socket from blocking the event loop.
 * @return 0 on success, -1 on stream or socket failure.
 */
int redp2p_stream_flush_tcp(redp2p_t *ctx, redp2p_stream_state_t *st,
    redp2p_fd_t tcp_fd)
{
    int available;
    int received;
    int written;

    if (!st || !st->kcp || tcp_fd == REDP2P_FD_INVALID) return -1;

    if (st->pending_tcp_off < st->pending_tcp_len) {
        written = redp2p_sock_write(tcp_fd,
            (const char *)st->pending_tcp + st->pending_tcp_off,
            (int)(st->pending_tcp_len - st->pending_tcp_off));
        if (written < 0) {
            if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
            redp2p_set_error(ctx, "stream: local TCP write failed");
            return -1;
        }
        if (written == 0) {
            redp2p_set_error(ctx, "stream: local TCP write closed");
            return -1;
        }
        st->pending_tcp_off += (size_t)written;
        if (st->pending_tcp_off < st->pending_tcp_len) return 0;
        st->pending_tcp_off = 0;
        st->pending_tcp_len = 0;
        return 0;
    }

    available = ikcp_peeksize(st->kcp);
    if (available < 0) {
        if (st->remote_close && !st->remote_shutdown) {
            redp2p_shutdown_write(tcp_fd);
            st->remote_shutdown = 1;
        }
        return 0;
    }
    if (available > (int)sizeof(st->pending_tcp)) {
        redp2p_set_error(ctx, "stream: KCP receive chunk exceeds buffer");
        return -1;
    }
    received = ikcp_recv(st->kcp, (char *)st->pending_tcp,
        (int)sizeof(st->pending_tcp));
    if (received < 0) {
        redp2p_set_error(ctx, "stream: KCP receive failed");
        return -1;
    }
    st->pending_tcp_off = 0;
    st->pending_tcp_len = (size_t)received;
    if (received == 0) return 0;

    written = redp2p_sock_write(tcp_fd,
        (const char *)st->pending_tcp, received);
    if (written < 0) {
        if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
        redp2p_set_error(ctx, "stream: local TCP write failed");
        return -1;
    }
    if (written == 0) {
        redp2p_set_error(ctx, "stream: local TCP write closed");
        return -1;
    }
    st->pending_tcp_off = (size_t)written;
    if (st->pending_tcp_off == st->pending_tcp_len) {
        st->pending_tcp_off = 0;
        st->pending_tcp_len = 0;
    }
    return 0;
}

/**
 * Dispatches one validated session datagram through handshake or KCP.
 * @return 0 on success, -1 on protocol, transport, or local socket failure.
 */
int redp2p_stream_process_packet(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t tcp_fd,
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
        envelope.protocol != st->transport_protocol ||
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
        return redp2p_stream_flush_tcp(ctx, st, tcp_fd);
    }
    if (envelope.type == REDP2P_SESSION_TYPE_CLOSE) {
        st->remote_close = 1;
        if (redp2p_stream_flush_tcp(ctx, st, tcp_fd) != 0) return -1;
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
 * Reads one local TCP chunk and queues its bytes in KCP stream mode.
 * @return 0 on success, -1 on local socket or KCP failure.
 */
int redp2p_stream_pump_tcp(redp2p_t *ctx,
    redp2p_stream_state_t *st, redp2p_fd_t tcp_fd)
{
    unsigned char buf[REDP2P_BUF];
    int n;
    int sent;

    if (!redp2p_stream_can_send_data(st)) return 0;
    n = redp2p_sock_read(tcp_fd, (char *)buf, (int)sizeof(buf));
    if (n < 0) {
        if (REDP2P_LASTERR() == REDP2P_EWOULD) return 0;
        redp2p_set_error(ctx, "stream: local TCP read failed");
        return -1;
    }
    if (n == 0) {
        st->local_eof = 1;
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
    if (st->ready && st->local_eof && !st->close_acked &&
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
    if (st->pending_tcp_off < st->pending_tcp_len) return 10;
    if (st->remote_close && !st->remote_shutdown) return 10;
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
 * Reports whether one TCP stream is fully closed on both sides.
 * @return 1 when the stream may be cleaned up, 0 otherwise.
 */
int redp2p_stream_is_done(const redp2p_stream_state_t *st) {
    return st->local_eof && st->close_acked && st->remote_close &&
        st->remote_shutdown && st->pending_tcp_len == 0 && st->kcp &&
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
static void redp2p_shutdown_write(redp2p_fd_t fd) {
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

    if (redp2p_resolve(host, port, SOCK_DGRAM, &srv, &srv_len) != 0) return -1;
    if (srv.ss_family != AF_INET) return -1;

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

    stun_ip[0] = '\0';
    stun_port = 0;
    *out_count = 0;
    if (getsockname(udp_fd, (struct sockaddr *)&udp_sa, &udp_sa_len) == 0) {
        unsigned short udp_port = redp2p_sockaddr_port(&udp_sa);

        if (udp_sa.ss_family == AF_INET &&
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
        if (udp_sa.ss_family == AF_INET6 &&
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

        redp2p_fd_t test_fd = socket(AF_INET, SOCK_DGRAM, 0);
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

        if (ctx && ctx->stun_url[0]) {
            redp2p_stun_binding(ctx, udp_fd, stun_ip, (int)sizeof(stun_ip),
                &stun_port);
        }
        if (stun_ip[0] != '\0' && *out_count < out_cap) {
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
                out[*out_count].type = REDP2P_CAND_HOST;
                snprintf(out[*out_count].addr, sizeof(out[*out_count].addr),
                    "%.47s", stun_ip);
                out[*out_count].port = srflx_port;
                out[*out_count].priority = redp2p_candidate_priority(&out[*out_count]);
                (*out_count)++;
            }
        }
    }
    if (!redp2p_normalize_candidates(out, out_count)) return REDP2P_ERROR;
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
static int redp2p_punch_send_candidate(int udp_fd,
    const redp2p_candidate_t *candidate, const char *ping_msg,
    int *unsupported)
{
    struct sockaddr_storage cand_sa;
    socklen_t cand_len;

    if (!redp2p_candidate_sockaddr(candidate, &cand_sa)) {
        if (unsupported) (*unsupported)++;
        return 0;
    }
    cand_len = redp2p_sockaddr_len(&cand_sa);
    if (cand_len == 0) {
        if (unsupported) (*unsupported)++;
        return 0;
    }
    return sendto(udp_fd, ping_msg, strlen(ping_msg), 0,
        (struct sockaddr *)&cand_sa, cand_len) >= 0;
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
static int redp2p_punch_wait_response(int udp_fd, const char *session_id,
    const char *from_id, const char *to_id, int wait_ms, uint64_t deadline_ms,
    struct sockaddr_storage *selected_addr, int *malformed, int *mismatched)
{
    uint64_t wait_deadline_ms;

    wait_deadline_ms = redp2p_now_ms() + (uint64_t)wait_ms;
    if (wait_deadline_ms > deadline_ms) wait_deadline_ms = deadline_ms;
    while (redp2p_now_ms() < wait_deadline_ms) {
        char recv_buf[1024];
        struct sockaddr_storage src_addr;
        socklen_t src_len = sizeof(src_addr);
        uint64_t now;
        int remaining_ms;
        int n;

        now = redp2p_now_ms();
        remaining_ms = (int)(wait_deadline_ms - now);
        if (redp2p_wait_readable((redp2p_fd_t)udp_fd, remaining_ms) <= 0)
            return REDP2P_ETIMEOUT;
        n = recvfrom(udp_fd, recv_buf, sizeof(recv_buf) - 1, 0,
            (struct sockaddr *)&src_addr, &src_len);
        if (n > 0) {
            char rx_sess[64] = {0};
            char rx_from[REDP2P_ID_MAX + 1] = {0};
            char rx_to[REDP2P_ID_MAX + 1] = {0};
            int is_ping;
            int is_pong;

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
                if (malformed) (*malformed)++;
                continue;
            }
            if (((is_ping && strcmp(rx_from, to_id) == 0 &&
                strcmp(rx_to, from_id) == 0) ||
                (is_pong && strcmp(rx_from, from_id) == 0 &&
                strcmp(rx_to, to_id) == 0)) &&
                strcmp(rx_sess, session_id) == 0)
            {
                *selected_addr = src_addr;
                if (is_ping) {
                    char pong_msg[256];
                    snprintf(pong_msg, sizeof(pong_msg), "%s%s:%s:%s\n",
                        REDP2P_CTRTOK_PUNCH_PONG, session_id, to_id, from_id);
                    sendto(udp_fd, pong_msg, strlen(pong_msg), 0,
                        (struct sockaddr *)&src_addr, src_len);
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
int redp2p_punch_select(redp2p_t *ctx, int sweep_limit, int udp_fd, const char *session_id, const char *from_id, const char *to_id, const redp2p_candidate_t *remote_candidates, int remote_candidate_count, struct sockaddr_storage *selected_addr) {
    char ping_msg[256];
    uint64_t deadline_ms;
    int direct_count;
    int sent_count;
    int malformed_count;
    int mismatch_count;
    int unsupported_count;

    (void)ctx;
    if (remote_candidate_count <= 0) {
        redp2p_set_error(ctx, "punch: no candidates");
        return REDP2P_ERROR;
    }
    if (sweep_limit < 0) sweep_limit = 0;
    deadline_ms = redp2p_now_ms() + REDP2P_PUNCH_TOTAL_MS;
    direct_count = 0;
    sent_count = 0;
    malformed_count = 0;
    mismatch_count = 0;
    unsupported_count = 0;
    for (int c = 0; c < remote_candidate_count; c++) {
        if (remote_candidates[c].priority < 300u) direct_count++;
    }
    snprintf(ping_msg, sizeof(ping_msg), "%s%s:%s:%s\n",
        REDP2P_CTRTOK_PUNCH_PING, session_id, from_id, to_id);
    for (int i = 0; direct_count > 0 && i < REDP2P_PUNCH_DIRECT_ROUNDS; i++) {
        for (int c = 0; c < remote_candidate_count; c++) {
            if (remote_candidates[c].priority >= 300u) continue;
            sent_count += redp2p_punch_send_candidate(udp_fd,
                &remote_candidates[c], ping_msg, &unsupported_count);
        }
        if (redp2p_punch_wait_response(udp_fd, session_id, from_id, to_id,
            REDP2P_PUNCH_DIRECT_WAIT_MS, deadline_ms, selected_addr,
            &malformed_count, &mismatch_count) == REDP2P_OK)
            return REDP2P_OK;
    }
    for (int sweep = 1; sweep <= sweep_limit && redp2p_now_ms() < deadline_ms;
        sweep++)
    {
        for (int sign = -1; sign <= 1 && redp2p_now_ms() < deadline_ms;
            sign += 2)
        {
            int offset = sweep * sign;
            for (int c = 0; c < remote_candidate_count; c++) {
                struct sockaddr_storage exact_sa;
                int test_port;

                sent_count += redp2p_punch_send_candidate(udp_fd,
                    &remote_candidates[c], ping_msg, &unsupported_count);
                if (!redp2p_candidate_sockaddr(&remote_candidates[c], &exact_sa))
                    continue;
                if (exact_sa.ss_family != AF_INET) continue;
                if (remote_candidates[c].type != REDP2P_CAND_OBSERVED)
                    continue;
                test_port = remote_candidates[c].port + offset;
                if (test_port <= 0 || test_port > 65535) continue;
                redp2p_sockaddr_set_port(&exact_sa, (unsigned short)test_port);
                if (redp2p_sendto_addr(udp_fd, ping_msg, strlen(ping_msg),
                    &exact_sa) >= 0)
                    sent_count++;
            }
            if (redp2p_punch_wait_response(udp_fd, session_id, from_id, to_id,
                REDP2P_PUNCH_SWEEP_WAIT_MS, deadline_ms, selected_addr,
                &malformed_count, &mismatch_count) == REDP2P_OK)
                return REDP2P_OK;
        }
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
        redp2p_set_error(ctx, "punch: direct connectivity timed out");
    } else {
        redp2p_set_error(ctx, "punch: direct connectivity attempts exhausted");
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
