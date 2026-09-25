/**
 * libredp2p-core.h - REDP2P.
 * Summary: Context lifecycle, shared codecs, cryptography and platform helpers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef REDP2P_CORE_H
#define REDP2P_CORE_H

#include "libredp2p.h"

#include <errno.h>
#include <stdatomic.h>

#include <time.h>

typedef struct redp2p redp2p_t;

#define REDP2P_OK       KC_REDP2P_OK
#define REDP2P_ERROR    KC_REDP2P_ERROR
#define REDP2P_ENET     KC_REDP2P_ENET
#define REDP2P_ENOENT   KC_REDP2P_ENOENT
#define REDP2P_ETIMEOUT KC_REDP2P_ETIMEOUT
#define REDP2P_EFULL    KC_REDP2P_EFULL
#define REDP2P_EINVAL   KC_REDP2P_EINVAL
#define REDP2P_EPROTO   KC_REDP2P_EPROTO
#define REDP2P_EAUTH    KC_REDP2P_EAUTH
#define REDP2P_EVERSION KC_REDP2P_EVERSION
#define REDP2P_EPUNCH   KC_REDP2P_EPUNCH
#define REDP2P_EEXIST   KC_REDP2P_EEXIST

#define REDP2P_ID_MAX KC_REDP2P_ID_MAX
#define REDP2P_PORT_DEFAULT KC_REDP2P_PORT_DEFAULT
#define REDP2P_PROTO_TCP KC_REDP2P_TCP
#define REDP2P_PROTO_UDP KC_REDP2P_UDP

#define REDP2P_ADDR_MAX 47
#define REDP2P_BUF 4096
#define REDP2P_HEARTBEAT_S 15
#define REDP2P_KEY_SZ 16
#define REDP2P_KEY_STR_SZ 33
#define REDP2P_PASS_MAX 255
#define REDP2P_STATE_DIR_MAX 511
#define REDP2P_UDP_PAYLOAD_MAX 1412
#define REDP2P_PEER_CANDIDATES_MAX 8
#define REDP2P_PUNCH_POLL_MAX 4
#define REDP2P_MAX_PENDING_CALLS_GLOBAL 4096
#define REDP2P_MAX_PENDING_CALLS_PER_PUBLISHER 32
#define REDP2P_MAX_CONSUMERS_PER_PUBLISHER REDP2P_MAX_PENDING_CALLS_PER_PUBLISHER

#define REDP2P_STUN_MAGIC 0x2112A442
#define REDP2P_STUN_ATTR_MAPPED_ADDR     0x0001
#define REDP2P_STUN_ATTR_XOR_MAPPED_ADDR 0x0020
#define REDP2P_STUN_BINDING      0x0001
#define REDP2P_STUN_BINDING_RESP 0x0101

typedef struct redp2p_options {
    size_t seats;
    int pow;
    char pass[REDP2P_PASS_MAX + 1];
    char *vip;
    int sweep;
    char stun_url[256];
} redp2p_options_t;

typedef enum {
    REDP2P_CAND_HOST = 1,
    REDP2P_CAND_OBSERVED
} redp2p_candidate_type_t;

typedef struct {
    redp2p_candidate_type_t type;
    char addr[REDP2P_ADDR_MAX + 1];
    unsigned short port;
    unsigned int priority;
} redp2p_candidate_t;

typedef struct {
    char id[REDP2P_ID_MAX + 1];
    char key[REDP2P_KEY_STR_SZ];
    uint64_t sequence;
    uint64_t last_seen;
    int proto;
    unsigned short udp_port;
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    int n_candidates;
} redp2p_peer_t;

typedef void (*redp2p_publisher_cb)(const char *id, void *userdata);

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET redp2p_fd_t;
typedef WSAPOLLFD redp2p_pollfd_t;
#  define REDP2P_POLLIN POLLRDNORM
#  define REDP2P_FD_INVALID  INVALID_SOCKET
#  define REDP2P_FD_CLOSE(f) closesocket(f)
#  define REDP2P_ISERR(f)    ((f) == INVALID_SOCKET)
#  define REDP2P_LASTERR()   ((int)WSAGetLastError())
#  define REDP2P_EWOULD      WSAEWOULDBLOCK
#else
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <poll.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <pthread.h>
#  include <sys/types.h>
typedef int redp2p_fd_t;
typedef struct pollfd redp2p_pollfd_t;
#  define REDP2P_POLLIN POLLIN
#  define REDP2P_FD_INVALID  (-1)
#  define REDP2P_FD_CLOSE(f) close(f)
#  define REDP2P_ISERR(f)    ((f) < 0)
#  define REDP2P_LASTERR()   errno
#  define REDP2P_EWOULD      EAGAIN
#  define INVALID_SOCKET     (-1)
#  define SOCKET_ERROR       (-1)
#endif

typedef struct json_object_t JSON_Object;

#if defined(__GNUC__) && !defined(_WIN32)
#define REDP2P_INTERNAL __attribute__((visibility("hidden")))
#else
#define REDP2P_INTERNAL
#endif

#define REDP2P_SWEEP_MAX   1024
#define REDP2P_SWEEP_DEFAULT 20
#define REDP2P_POW_MAX      32
#define REDP2P_CTRL_SESSION_MAX    63
#define REDP2P_HTTP_LINE_MAX      256
#define REDP2P_HTTP_HEADERS_MAX    32
#define REDP2P_HTTP_BODY_MAX     REDP2P_BUF
#define REDP2P_HTTP_TIMEOUT_S       5
#define REDP2P_HTTP_BUF_MAX (REDP2P_HTTP_LINE_MAX * REDP2P_HTTP_HEADERS_MAX + \
    REDP2P_HTTP_BODY_MAX + 128)

typedef struct {
    uint32_t state[8];
    uint64_t count;
    unsigned char buf[64];
} redp2p_sha256_t;

typedef struct {
    char id[REDP2P_ID_MAX + 1];
    char pass[REDP2P_PASS_MAX + 1];
} redp2p_vip_entry_t;

/**
 * Pending call.
 * Summary: Stores one request-driven punch introduction awaiting pickup.
 */
typedef struct {
    char caller_id[REDP2P_ID_MAX + 1];
    char target_id[REDP2P_ID_MAX + 1];
    char sess_id[REDP2P_CTRL_SESSION_MAX + 1];
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    int n_candidates;
    uint64_t ts;
} redp2p_pending_call_t;

/**
 * Stores one in-flight index HTTP connection and its partial request buffer.
 */
typedef struct {
    redp2p_fd_t fd;
    char buf[REDP2P_HTTP_BUF_MAX + 1];
    int buf_len;
    uint64_t ts;
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
} redp2p_index_conn_t;

/**
 * Stores token credit in thousandths and its monotonic accounting time.
 */
typedef struct {
    unsigned int credit;
    uint64_t updated_ms;
} redp2p_rate_bucket_t;

/**
 * Keeps target rate state under the ownership of one publisher record.
 */
typedef struct {
    redp2p_peer_t peer;
    redp2p_rate_bucket_t punch_bucket;
} redp2p_index_peer_t;

typedef struct redp2p_rate_source redp2p_rate_source_t;

struct redp2p {
    redp2p_index_peer_t *peers;
    size_t n_peers;
    size_t peers_alloc;
    redp2p_rate_source_t *rate_sources;
    size_t n_rate_sources;
    size_t n_peers_cap;
    size_t nonvip_cap;
    int seats_set;
    redp2p_vip_entry_t *vips;
    size_t n_vips;
    size_t vips_cap;
    redp2p_index_conn_t *conns;
    int n_conns;
    int conns_cap;
    char key[REDP2P_KEY_STR_SZ];
    unsigned char challenge_key[32];
    uint64_t sequence;
    char pass[REDP2P_PASS_MAX + 1];
    int pow_bits;
    unsigned short bind_port;
    int explicit_port;
    int proto;
    int sweep;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    char stun_url[512];
    char state_dir[REDP2P_STATE_DIR_MAX + 1];
    char err_buf[256];
    int fault_drop_every;
    int fault_reorder_every;
    int fault_drop_counter;
    int fault_reorder_counter;
    unsigned long prune_interval_s;
    unsigned long etimeout_sec;
    unsigned long heartbeat_s;
    unsigned long punch_poll_ms;
    unsigned long pending_ttl_s;
    redp2p_pending_call_t *pending_calls;
    size_t pending_calls_cap;
    int n_pending_calls;
    size_t max_consumers_per_publisher;
    _Atomic int stop_requested;
    redp2p_fd_t wake_write_fd;
    _Atomic int ready_state;
    _Atomic int ready_status;
};

/**
 * Parses one strict unsigned decimal integer.
 * Summary: Rejects NULL, empty, leading/trailing garbage, signs, and overflow.
 * @param text    Input text to parse.
 * @param min     Inclusive lower bound.
 * @param max     Inclusive upper bound.
 * @param out     Output parsed value.
 * @return 1 on valid parse within bounds, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_parse_u(const char *text, long min, long max, long *out);

/**
 * Compares two ASCII strings case-insensitively.
 * Summary: Keeps HTTP header-name matching portable across platforms that
 *          do not declare strcasecmp (macOS with strict C, Windows).
 * @param a Left string.
 * @param b Right string.
 * @return 0 when equal ignoring ASCII case, nonzero otherwise.
 */
REDP2P_INTERNAL int redp2p_ascii_casecmp(const char *a, const char *b);

/**
 * Sha256 init.
 * @return Status code.
 */
REDP2P_INTERNAL void redp2p_sha256_init(redp2p_sha256_t *ctx);

/**
 * Sha256 update.
 * @return Status code.
 */
REDP2P_INTERNAL void redp2p_sha256_update(redp2p_sha256_t *ctx, const unsigned char *data, size_t len);

/**
 * Sha256 final.
 * @return Status code.
 */
REDP2P_INTERNAL void redp2p_sha256_final(redp2p_sha256_t *ctx, unsigned char hash[32]);

/**
 * Computes one HMAC-SHA256 digest.
 * @param key     Shared password material.
 * @param msg     Input message bytes.
 * @param msg_len Input message length.
 * @param hash    Output digest buffer.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_hmac_sha256_bytes(const unsigned char *key, size_t key_len,
    const unsigned char *msg, size_t msg_len, unsigned char hash[32]);

/**
 * Computes a password-bound admission proof for a canonical registration.
 * @param password Nonempty index admission password.
 * @param registration_message Canonical registration bytes.
 * @param registration_message_len Canonical byte count, at most 384.
 * @param proof Output buffer for 64 lowercase hexadecimal characters and NUL.
 * @return 1 on success, 0 on invalid input.
 */
REDP2P_INTERNAL int redp2p_admission_proof(const char *password,
const unsigned char *registration_message, size_t registration_message_len,
char proof[65]);

/**
 * Builds the canonical message and proof for one publisher control request.
 * @param key Publisher session secret.
 * @param op Control operation name.
 * @param id Publisher identifier.
 * @param sequence Strictly increasing control sequence.
 * @param proto Publisher transport protocol for heartbeat, or zero.
 * @param udp_port Publisher UDP port for heartbeat, or zero.
 * @param candidates Publisher candidates for heartbeat, or NULL.
 * @param n_candidates Candidate count for heartbeat.
 * @param proof Output hexadecimal HMAC proof.
 * @return 1 on success, 0 when the canonical message cannot be represented.
 */
REDP2P_INTERNAL int redp2p_control_proof(const char *key, const char *op,
    const char *id, uint64_t sequence, int proto, unsigned short udp_port,
    const redp2p_candidate_t *candidates, int n_candidates, char proof[65]);

/**
 * Appends bounded binary bytes to a canonical message.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param data Bytes to append.
 * @param len Byte count.
 * @return 1 on success, 0 on overflow.
 */
REDP2P_INTERNAL int redp2p_append_bytes(unsigned char *out, size_t cap, size_t *used,
    const unsigned char *data, size_t len);

/**
 * Appends one uint64 in big-endian form.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param used Bytes used and updated.
 * @param value Value to append.
 * @return 1 on success, 0 on overflow.
 */
REDP2P_INTERNAL int redp2p_append_u64_be(unsigned char *out, size_t cap, size_t *used,
    uint64_t value);

/**
 * Computes one registration proof-of-work digest.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id Publisher identifier.
 * @param solution Candidate solution.
 * @param hash Output digest.
 * @return 1 on success, 0 on invalid input.
 */
REDP2P_INTERNAL int redp2p_hash_register_pow(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    uint64_t solution, unsigned char hash[32]);

/**
 * Builds the canonical binary registration proof message.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id Publisher identifier.
 * @param secret Publisher secret text.
 * @param proto Public protocol value.
 * @param udp_port Publisher UDP port.
 * @param candidates Normalized candidates.
 * @param n_candidates Candidate count.
 * @param solution PoW solution.
 * @param out Output buffer.
 * @param out_len Output byte count.
 * @return 1 on success, 0 on invalid input or overflow.
 */
REDP2P_INTERNAL int redp2p_register_message(const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, const char *id,
    const char *secret, int proto, unsigned short udp_port,
    const redp2p_candidate_t *candidates, int n_candidates,
    uint64_t solution, unsigned char *out, size_t *out_len);

/**
 * Derives the stateless index X25519 key pair for one registration challenge.
 * @param challenge_key Index registration challenge key.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param index_skey Output index secret key.
 * @param index_pkey Output index public key.
 * @return 1 on success, 0 on invalid input.
 */
REDP2P_INTERNAL int redp2p_registration_index_key(
    const unsigned char challenge_key[32], const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at, unsigned char index_skey[32],
    unsigned char index_pkey[32]);

/**
 * Encrypts one registration control secret for the index challenge key.
 * @param publisher_skey Fresh publisher X25519 secret key.
 * @param index_pkey Challenge index X25519 public key.
 * @param nonce Raw challenge nonce.
 * @param secret Existing 16-byte publisher control secret.
 * @param publisher_pkey Output publisher X25519 public key.
 * @param encrypted_secret Output ciphertext followed by authentication tag.
 * @return 1 on success, 0 when the shared secret is unusable.
 */
REDP2P_INTERNAL int redp2p_registration_secret_encrypt(
    const unsigned char publisher_skey[32], const unsigned char index_pkey[32],
    const unsigned char nonce[32], const unsigned char secret[REDP2P_KEY_SZ],
    unsigned char publisher_pkey[32], unsigned char encrypted_secret[32]);

/**
 * Decrypts one registration control secret after challenge authentication.
 * @param challenge_key Index registration challenge key.
 * @param nonce Raw challenge nonce.
 * @param issued_at Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param publisher_pkey Publisher X25519 public key.
 * @param encrypted_secret Ciphertext followed by authentication tag.
 * @param secret Output recovered publisher control secret.
 * @return 1 on success, 0 on cryptographic failure.
 */
REDP2P_INTERNAL int redp2p_registration_secret_decrypt(
    const unsigned char challenge_key[32], const unsigned char nonce[32],
    uint64_t issued_at, uint64_t expires_at,
    const unsigned char publisher_pkey[32],
    const unsigned char encrypted_secret[32],
    unsigned char secret[REDP2P_KEY_SZ]);

/**
 * Encodes a digest as lowercase hex.
 * @param hash     Input digest bytes.
 * @param hash_len Digest length in bytes.
 * @param out      Output hex buffer.
 * @param out_cap  Output buffer capacity.
 * @return 1 on success, 0 on failure.
 */
REDP2P_INTERNAL int redp2p_hex_encode(const unsigned char *hash, size_t hash_len,
    char *out, size_t out_cap);

/**
 * Counts leading zero bits in one digest.
 * @param hash Input digest bytes.
 * @return Count of leading zero bits.
 */
REDP2P_INTERNAL int redp2p_count_leading_zero_bits(const unsigned char hash[32]);

/**
 * Returns whether one context requested stop.
 * @param ctx Context to inspect.
 * @return 1 when stop was requested, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_is_stop_requested(redp2p_t *ctx);

/**
 * Lock.
 * @return Status code.
 */
REDP2P_INTERNAL void redp2p_lock(redp2p_t *ctx);

/**
 * Unlock.
 * @return Status code.
 */
REDP2P_INTERNAL void redp2p_unlock(redp2p_t *ctx);

/**
 * Returns a millisecond timestamp.
 * @return Monotonic-ish timestamp in milliseconds.
 */
REDP2P_INTERNAL uint64_t redp2p_now_ms(void);

/**
 * Returns a second timestamp.
 * Summary: Used for elapsed-time logic to avoid wall-clock jumps.
 * @return Monotonic-ish timestamp in seconds.
 */
REDP2P_INTERNAL uint64_t redp2p_now_s(void);

/**
 * Fills a buffer with secure random bytes.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_fill_random(unsigned char *buf, size_t len);

/**
 * Decodes one hex nibble.
 * @return Nibble value, or -1 on error.
 */
REDP2P_INTERNAL int redp2p_hex_decode_nibble(char c);

/**
 * Decodes a fixed-size hex string.
 * @return 1 on success, 0 on error.
 */
REDP2P_INTERNAL int redp2p_hex_decode(const char *hex, unsigned char *out, size_t out_len);

/**
 * Platform init.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_platform_init(void);

/**
 * Platform cleanup.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_platform_cleanup(void);

/**
 * Set nonblock.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_set_nonblock(redp2p_fd_t fd);

/**
 * Waits for socket readiness without FD_SETSIZE limits.
 * @return Ready descriptor count, 0 on timeout, or -1 on error.
 */
REDP2P_INTERNAL int redp2p_poll_wait(redp2p_pollfd_t *fds, size_t count,
    int timeout_ms);

/**
 * Reports whether one poll entry should be processed as readable.
 */
REDP2P_INTERNAL int redp2p_poll_readable(const redp2p_pollfd_t *fd);

/**
 * Creates the internal socket wakeup pair used by a running idx/pub/con loop.
 */
REDP2P_INTERNAL int redp2p_wake_open(redp2p_t *ctx,
    redp2p_fd_t *read_fd, redp2p_fd_t *write_fd);

/**
 * Drains pending wake bytes from a nonblocking wake socket.
 */
REDP2P_INTERNAL void redp2p_wake_drain(redp2p_fd_t read_fd);

/**
 * Detaches and closes one internal wakeup pair.
 */
REDP2P_INTERNAL void redp2p_wake_close(redp2p_t *ctx,
    redp2p_fd_t read_fd, redp2p_fd_t write_fd);

/**
 * Returns the UDP or TCP port from a stored socket address.
 * @param addr Stored socket address.
 * @return Host-order port, or 0 for unsupported families.
 */
REDP2P_INTERNAL unsigned short redp2p_sockaddr_port(
    const struct sockaddr_storage *addr);

/**
 * Compares stored socket endpoints by family, address, and port.
 * @param a First address.
 * @param b Second address.
 * @return 1 when endpoints match, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_sockaddr_equal(const struct sockaddr_storage *a,
    const struct sockaddr_storage *b);

/**
 * Applies the index destination policy to one raw candidate address.
 *
 * Host candidates may only name reachable unicast endpoints. Loopback,
 * unspecified, multicast, broadcast, and reserved destinations are refused.
 * IPv6 IPv4-mapped addresses are resolved through the IPv4 policy so that
 * loopback and multicast cannot be smuggled past the v6 checks.
 *
 * @param family Address family, AF_INET or AF_INET6.
 * @param host   Raw network-order address bytes.
 * @param port   Candidate UDP port.
 * @return 1 when the endpoint is acceptable, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_candidate_dest_allowed(int family, const void *host,
    unsigned short port);

/**
 * Adds one descriptor to one select set with descriptor-range validation.
 * Summary: Rejects descriptors that cannot be represented by fd_set.
 * @param fd    Descriptor to add.
 * @param set   Select set to update.
 * @param maxfd Current maximum descriptor, updated on success.
 * @return 1 when added, 0 when rejected.
 */
REDP2P_INTERNAL int redp2p_fdset_add(redp2p_fd_t fd, fd_set *set, int *maxfd);

/**
 * Sock read.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_sock_read(redp2p_fd_t fd, char *buf, int len);

/**
 * Writes one socket chunk without raising SIGPIPE where the platform supports it.
 * @return Bytes written, or -1 on error.
 */
REDP2P_INTERNAL int redp2p_sock_write(redp2p_fd_t fd, const char *buf, int len);

/**
 * Write all.
 * @return 0 on success, -1 on error.
 */
REDP2P_INTERNAL int redp2p_write_all(redp2p_fd_t fd, const char *buf, int len);

/**
 * Records one per-context detail error message.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_set_error(redp2p_t *ctx, const char *fmt, ...);

/**
 * Copies one colon-delimited field from a cursor.
 * @param cursor Input cursor updated after the field.
 * @param out    Output field buffer.
 * @param cap    Output field capacity.
 * @param delim  Required delimiter or NUL for final field.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_parse_field(const char **cursor, char *out, size_t cap,
    char delim);

/**
 * Reports whether a token is lowercase or uppercase hexadecimal.
 * @param text Input token.
 * @param len  Required token length.
 * @return 1 when valid, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_is_hex_token(const char *text, size_t len);

/**
 * Reports whether a session token is bounded and alphanumeric.
 * @param text Input token.
 * @return 1 when valid, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_is_session_token(const char *text);

/**
 * Returns the deterministic local priority for one candidate.
 * @param candidate Candidate to rank.
 * @return Lower priority values are attempted first.
 */
REDP2P_INTERNAL unsigned int redp2p_candidate_priority(
    const redp2p_candidate_t *candidate);

/**
 * Normalizes candidate priority, removes duplicates, and sorts the list.
 * @param candidates Candidate array.
 * @param count      Candidate count in and out.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_normalize_candidates(redp2p_candidate_t *candidates,
    int *count);

/**
 * Converts a candidate into a socket address.
 * @param candidate Candidate to convert.
 * @param out       Output socket address.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_candidate_sockaddr(const redp2p_candidate_t *candidate,
    struct sockaddr_storage *out);

/**
 * Extracts and validates one fixed-width hexadecimal token field.
 * @param obj    Request object.
 * @param field  Field name.
 * @param out    Output token.
 * @param out_cap Output token capacity.
 * @param hex_len Required hex length.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_json_require_hex(JSON_Object *obj, const char *field,
    char *out, size_t out_cap, size_t hex_len);

/**
 * Extracts a fixed-width lowercase hexadecimal token field.
 * @param obj Request object.
 * @param field Field name.
 * @param out Output token.
 * @param out_cap Output capacity.
 * @param hex_len Required token length.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_json_require_lower_hex(JSON_Object *obj,
    const char *field, char *out, size_t out_cap, size_t hex_len);

/**
 * Extracts one exact nonnegative timestamp JSON number.
 * @param obj Request object.
 * @param field Field name.
 * @param value Output timestamp.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_json_require_u64(JSON_Object *obj, const char *field,
    uint64_t *value);

/**
 * Validates one JSON number as a UDP port before storing it.
 * @param value Parsed JSON numeric value.
 * @param port Output UDP port.
 * @return 1 on a finite integral port in range, 0 otherwise.
 */
REDP2P_INTERNAL int redp2p_json_require_port(double value, unsigned short *port);

/**
 * Parses one bounded JSON candidate array into candidate records.
 * @param obj       Request object.
 * @param field     Array field name.
 * @param out       Output candidate array.
 * @param out_count Output candidate count.
 * @return 1 on success, 0 on malformed input.
 */
REDP2P_INTERNAL int redp2p_parse_candidates(JSON_Object *obj, const char *field,
    redp2p_candidate_t *out, int *out_count);

/**
 * Appends one candidate array to a JSON reply object.
 * @param obj   Reply object.
 * @param field Output array field name.
 * @param cands Candidate array.
 * @param n     Candidate count.
 * @return None.
 */
REDP2P_INTERNAL void redp2p_append_candidates(JSON_Object *obj,
    const char *field, const redp2p_candidate_t *cands, int n);

/* Legacy protocol/runtime entry points retained only for the private engine. */
REDP2P_INTERNAL redp2p_options_t redp2p_options_default(void);
REDP2P_INTERNAL void redp2p_options_load_env(redp2p_options_t *opts);
REDP2P_INTERNAL void redp2p_options_free(redp2p_options_t *opts);
REDP2P_INTERNAL int redp2p_open(redp2p_t **out);
REDP2P_INTERNAL int redp2p_close(redp2p_t *ctx);
REDP2P_INTERNAL int redp2p_stop(redp2p_t *ctx);
REDP2P_INTERNAL int redp2p_stop_requested(redp2p_t *ctx);
REDP2P_INTERNAL uint64_t redp2p_version(void);
REDP2P_INTERNAL const char *redp2p_strerror(int code);
REDP2P_INTERNAL const char *redp2p_get_error(redp2p_t *ctx);
REDP2P_INTERNAL int redp2p_is_valid_id(const char *id);
REDP2P_INTERNAL int redp2p_is_valid_pass_token(const char *pass);
REDP2P_INTERNAL int redp2p_serve_index(redp2p_t *ctx, const char *host,
    unsigned short port);
REDP2P_INTERNAL int redp2p_wait(redp2p_t *ctx, const char *index_host,
    unsigned short index_port, const char *self_id, unsigned short bind_port);
REDP2P_INTERNAL int redp2p_connect(redp2p_t *ctx, const char *index_host,
    unsigned short index_port, const char *self_id, const char *target_id,
    unsigned short bind_port);
REDP2P_INTERNAL int redp2p_deregister(redp2p_t *ctx, const char *index_host,
    unsigned short index_port, const char *id);
REDP2P_INTERNAL int redp2p_list_publishers(redp2p_t *ctx,
    const char *index_host, unsigned short index_port,
    redp2p_publisher_cb cb, void *userdata);
REDP2P_INTERNAL int redp2p_set_seats(redp2p_t *ctx, size_t seats);
REDP2P_INTERNAL int redp2p_set_max_consumers_per_publisher(redp2p_t *ctx,
    size_t n);
REDP2P_INTERNAL int redp2p_set_pow(redp2p_t *ctx, int bits);
REDP2P_INTERNAL int redp2p_set_port(redp2p_t *ctx, unsigned short port);
REDP2P_INTERNAL int redp2p_set_protocol(redp2p_t *ctx, int proto);
REDP2P_INTERNAL int redp2p_set_pass(redp2p_t *ctx, const char *pass);
REDP2P_INTERNAL int redp2p_set_vip(redp2p_t *ctx, const char *vip,
    char *err, size_t err_cap);
REDP2P_INTERNAL int redp2p_set_sweep(redp2p_t *ctx, int sweep);
REDP2P_INTERNAL int redp2p_set_stun_url(redp2p_t *ctx, const char *url);
REDP2P_INTERNAL int redp2p_set_stream_faults(redp2p_t *ctx,
    int drop_every, int reorder_every);
REDP2P_INTERNAL int redp2p_set_state_dir(redp2p_t *ctx, const char *dir);
REDP2P_INTERNAL uint16_t redp2p_get_bind_port(redp2p_t *ctx);

#endif
