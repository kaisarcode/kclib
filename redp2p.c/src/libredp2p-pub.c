/**
 * libredp2p-pub.c - REDP2P.
 * Summary: Publisher persistence, registration and service sessions.
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

#ifdef _WIN32
#include <direct.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

#define REDP2P_IPV4_LOOPBACK             0x7f000001u
#define REDP2P_CTRTOK_PUNCH_SERVER "REDP2P_CTRTOK_PUNCH:server"

#ifdef _WIN32
static SRWLOCK g_key_mutex = SRWLOCK_INIT;
#else
static pthread_mutex_t g_key_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

typedef struct redp2p_udp_server_session {
    redp2p_fd_t backend_fd;
    redp2p_fd_t tcp_fd;
    struct sockaddr_storage peer_addr;
    uint64_t last_rx;
    uint64_t last_ka;
    int active;
    int is_tcp;
    redp2p_stream_state_t stream;
} redp2p_udp_server_session_t;

typedef struct {
    redp2p_t *borrowed_ctx;
    const char *borrowed_index_host;
    const char *borrowed_self_id;
    const char *borrowed_udp_any_host;
    unsigned short index_port;
    redp2p_fd_t owned_udp_fd;
    redp2p_udp_server_session_t *owned_sessions;
    int session_count;
    int session_capacity;
    uint64_t last_heartbeat;
    uint64_t last_punch_poll;
} redp2p_publisher_runtime_t;

typedef struct {
    char dir[768];
    char scoped[848];
    char legacy[848];
} redp2p_key_paths_t;

/**
 * Solves one register proof-of-work challenge.
 * @param ctx        Publisher context.
 * @param nonce      Raw challenge nonce.
 * @param issued_at  Challenge issue timestamp.
 * @param expires_at Challenge expiry timestamp.
 * @param id         Service identifier.
 * @param bits       Difficulty target.
 * @param solution   Output uint64 solution.
 * @return 1 on success, 0 on failure.
 */
static int redp2p_solve_register_pow(redp2p_t *ctx,
    const unsigned char nonce[32], uint64_t issued_at, uint64_t expires_at,
    const char *id, int bits, uint64_t *solution)
{
    uint64_t counter;
    unsigned char hash[32];

    if (!nonce || !id || !solution || bits < 0 || bits > 32) return 0;
    counter = 0;
    for (;;) {
        if (!redp2p_hash_register_pow(nonce, issued_at, expires_at, id,
            counter, hash)) return 0;
        if (redp2p_count_leading_zero_bits(hash) >= bits) {
            *solution = counter;
            crypto_wipe(hash, sizeof(hash));
            return 1;
        }
        if (counter == UINT64_MAX) break;
        counter++;
        if ((counter & 0xfffff) == 0 && redp2p_is_stop_requested(ctx)) break;
    }
    crypto_wipe(hash, sizeof(hash));
    return 0;
}

/**
 * Closes one publisher-side UDP session and wipes TCP stream state.
 * @return None.
 */
static void redp2p_server_session_close(redp2p_udp_server_session_t *sess) {
    if (!sess) return;
    if (sess->backend_fd != REDP2P_FD_INVALID) {
        REDP2P_FD_CLOSE(sess->backend_fd);
        sess->backend_fd = REDP2P_FD_INVALID;
    }
    if (sess->tcp_fd != REDP2P_FD_INVALID) {
        REDP2P_FD_CLOSE(sess->tcp_fd);
        sess->tcp_fd = REDP2P_FD_INVALID;
    }
    if (sess->is_tcp) redp2p_stream_wipe(&sess->stream);
    sess->active = 0;
}

/**
 * Connect local tcp.
 * @return 0 on success, -1 on error.
 */
static redp2p_fd_t redp2p_connect_local_tcp(unsigned short port) {
    redp2p_fd_t fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (REDP2P_ISERR(fd)) return REDP2P_FD_INVALID;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        REDP2P_FD_CLOSE(fd);
        return REDP2P_FD_INVALID;
    }
    return fd;
}

/**
 * Generate key.
 * @return Status code.
 */
static int redp2p_generate_key(char *out) {
    unsigned char random_bytes[REDP2P_KEY_SZ / 2];

    if (!out) return 0;
    if (redp2p_fill_random(random_bytes, sizeof(random_bytes)) != 0)
        return 0;
    return redp2p_hex_encode(random_bytes, sizeof(random_bytes), out,
        REDP2P_KEY_SZ + 1);
}

/**
 * Sets the base directory for persistent publisher keys and session state.
 * Summary: Stores library-owned data under <dir>/keys/.
 * An empty path restores the platform default persistence directory.
 * @param ctx Open context.
 * @param dir Base directory path (non-NULL).
 * @return REDP2P_OK or REDP2P_EINVAL.
 */
int redp2p_set_state_dir(redp2p_t *ctx, const char *dir) {
    if (!ctx) return REDP2P_EINVAL;
    if (dir == NULL) return REDP2P_EINVAL;
    if (dir[0] == '\0') {
        ctx->state_dir[0] = '\0';
        redp2p_set_error(ctx, NULL);
        return REDP2P_OK;
    }
    if (strlen(dir) >= sizeof(ctx->state_dir)) {
        redp2p_set_error(ctx, "state_dir path is too long");
        return REDP2P_EINVAL;
    }
    strcpy(ctx->state_dir, dir);
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Serializes access to persisted publisher session secrets within this process.
 * @return None.
 */
static void redp2p_key_lock(void) {
#ifdef _WIN32
    AcquireSRWLockExclusive(&g_key_mutex);
#else
    pthread_mutex_lock(&g_key_mutex);
#endif
}

/**
 * Releases process-local publisher session secret serialization.
 * @return None.
 */
static void redp2p_key_unlock(void) {
#ifdef _WIN32
    ReleaseSRWLockExclusive(&g_key_mutex);
#else
    pthread_mutex_unlock(&g_key_mutex);
#endif
}

/**
 * Produces a portable SHA-256 digest for one index and publisher scope.
 * @param index_host Index host text.
 * @param index_port Index port.
 * @param id Publisher identifier.
 * @param out Output digest.
 * @return None.
 */
static void redp2p_key_scope_hash(const char *index_host,
    unsigned short index_port, const char *id, unsigned char out[32])
{
    static const unsigned char domain[] = "redp2p-key-v1";
    redp2p_sha256_t hash;
    unsigned char digest[32];
    unsigned char port[2];

    port[0] = (unsigned char)(index_port >> 8);
    port[1] = (unsigned char)index_port;
    redp2p_sha256_init(&hash);
    redp2p_sha256_update(&hash, domain, sizeof(domain));
    redp2p_sha256_update(&hash, (const unsigned char *)index_host,
        strlen(index_host) + 1);
    redp2p_sha256_update(&hash, port, sizeof(port));
    redp2p_sha256_update(&hash, (const unsigned char *)id, strlen(id) + 1);
    redp2p_sha256_final(&hash, digest);
    memcpy(out, digest, sizeof(digest));
    crypto_wipe(&hash, sizeof(hash));
    crypto_wipe(digest, sizeof(digest));
    crypto_wipe(port, sizeof(port));
}

/**
 * Builds bounded scoped and legacy publisher session secret paths.
 * @param ctx Context receiving error detail.
 * @param index_host Index host.
 * @param index_port Index port.
 * @param id Publisher identifier.
 * @param paths Output paths.
 * @return REDP2P_OK on success or REDP2P_ERROR for invalid HOME/path length.
 */
static int redp2p_key_paths(redp2p_t *ctx, const char *index_host,
    unsigned short index_port, const char *id, redp2p_key_paths_t *paths)
{
    const char *base;
    unsigned char digest[32];
    char filename[65];
    int n;

    if (ctx->state_dir[0] != '\0') {
        n = snprintf(paths->dir, sizeof(paths->dir),
            "%s/keys", ctx->state_dir);
    } else {
#ifndef _WIN32
        base = getenv("XDG_DATA_HOME");
        if (base && base[0])
            n = snprintf(paths->dir, sizeof(paths->dir),
                "%s/redp2p/keys", base);
        else
#endif
        {
            base = getenv("HOME");
#ifdef _WIN32
            if (!base || !base[0]) base = getenv("USERPROFILE");
#endif
            if (!base || !base[0]) {
                redp2p_set_error(ctx, "state: HOME is missing or empty");
                return REDP2P_ERROR;
            }
            n = snprintf(paths->dir, sizeof(paths->dir),
                "%s/.local/share/redp2p/keys", base);
        }
    }
    if (n < 0 || (size_t)n >= sizeof(paths->dir)) {
        redp2p_set_error(ctx, "key: HOME path is too long");
        return REDP2P_ERROR;
    }
    redp2p_key_scope_hash(index_host, index_port, id, digest);
    if (!redp2p_hex_encode(digest, sizeof(digest), filename,
        sizeof(filename)))
    {
        redp2p_set_error(ctx, "key: scope encoding failed");
        crypto_wipe(digest, sizeof(digest));
        crypto_wipe(filename, sizeof(filename));
        return REDP2P_ERROR;
    }
    n = snprintf(paths->scoped, sizeof(paths->scoped), "%s/%s",
        paths->dir, filename);
    if (n < 0 || (size_t)n >= sizeof(paths->scoped)) {
        redp2p_set_error(ctx, "key: scoped path is too long");
        crypto_wipe(digest, sizeof(digest));
        crypto_wipe(filename, sizeof(filename));
        return REDP2P_ERROR;
    }
    n = snprintf(paths->legacy, sizeof(paths->legacy), "%s/%s",
        paths->dir, id);
    if (n < 0 || (size_t)n >= sizeof(paths->legacy)) {
        redp2p_set_error(ctx, "key: legacy path is too long");
        crypto_wipe(digest, sizeof(digest));
        crypto_wipe(filename, sizeof(filename));
        return REDP2P_ERROR;
    }
    crypto_wipe(digest, sizeof(digest));
    crypto_wipe(filename, sizeof(filename));
    return REDP2P_OK;
}

/**
 * Creates a directory hierarchy and rejects non-directory collisions.
 * @param path Mutable directory path.
 * @return 0 on success or -1 on failure with errno set where available.
 */
static int redp2p_mkdir_p(char *path) {
    char *p;

    for (p = path + 1; *p; p++) {
        int result;

        if (*p != '/' && *p != '\\') continue;
        *p = '\0';
#ifdef _WIN32
        result = _mkdir(path);
        if (result != 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
#else
        result = mkdir(path, 0755);
        if (result != 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
#endif
        *p = '/';
    }
#ifdef _WIN32
    if (_mkdir(path) != 0 && errno != EEXIST) return -1;
#else
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return -1;
    if (chmod(path, 0700) != 0) return -1;
#endif
    return 0;
}

/**
 * Validates one complete publisher session secret file payload.
 * @param data File bytes.
 * @param len File byte count.
 * @param key Output key.
 * @return 1 for exact key content with an optional line ending, otherwise 0.
 */
static int redp2p_key_parse(const char *data, size_t len,
    char key[REDP2P_KEY_STR_SZ], uint64_t *sequence)
{
    size_t i;
    uint64_t value;

    if (!data || !key || !sequence || len < REDP2P_KEY_SZ + 1) return 0;
    for (i = 0; i < REDP2P_KEY_SZ; i++) {
        if (redp2p_hex_decode_nibble(data[i]) < 0) return 0;
    }
    if (data[REDP2P_KEY_SZ] != '\n') return 0;
    value = 0;
    for (i = REDP2P_KEY_SZ + 1; i < len; i++) {
        unsigned char digit;

        if (data[i] == '\n' && i + 1 == len) break;
        if (data[i] < '0' || data[i] > '9') return 0;
        digit = (unsigned char)(data[i] - '0');
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    if (i == REDP2P_KEY_SZ + 1 || i + 1 != len) return 0;
    memcpy(key, data, REDP2P_KEY_SZ);
    key[REDP2P_KEY_SZ] = '\0';
    *sequence = value;
    return 1;
}

#ifdef _WIN32
/**
 * Reads one Windows key file without following reparse points.
 * @param ctx Context receiving error detail.
 * @param path Exact path to load.
 * @param data Output file bytes.
 * @param capacity Output buffer capacity.
 * @param total Output byte count.
 * @return REDP2P_OK, REDP2P_ENOENT, or REDP2P_ERROR.
 */
static int redp2p_load_key_windows(
redp2p_t *ctx,
const char *path,
char *data,
size_t capacity,
size_t *total)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD got;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND)
            return REDP2P_ENOENT;
        redp2p_set_error(ctx, "key: cannot open persisted key (%lu)",
            (unsigned long)GetLastError());
        return REDP2P_ERROR;
    }
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        redp2p_set_error(ctx, "key: persisted key is not a regular file");
        CloseHandle(file);
        return REDP2P_ERROR;
    }
    *total = 0;
    while (*total < capacity) {
        if (!ReadFile(file, data + *total, (DWORD)(capacity - *total),
            &got, NULL))
        {
            redp2p_set_error(ctx, "key: persisted key read failed (%lu)",
                (unsigned long)GetLastError());
            CloseHandle(file);
            return REDP2P_ERROR;
        }
        if (got == 0) break;
        *total += got;
    }
    if (!CloseHandle(file)) {
        redp2p_set_error(ctx, "key: persisted key close failed");
        return REDP2P_ERROR;
    }
    return REDP2P_OK;
}
#endif

#ifndef _WIN32
/**
 * Reads one POSIX key file without following symbolic links when supported.
 * @param ctx Context receiving error detail.
 * @param path Exact path to load.
 * @param data Output file bytes.
 * @param capacity Output buffer capacity.
 * @param total Output byte count.
 * @return REDP2P_OK, REDP2P_ENOENT, REDP2P_EPROTO, or REDP2P_ERROR.
 */
static int redp2p_load_key_posix(
redp2p_t *ctx,
const char *path,
char *data,
size_t capacity,
size_t *total)
{
    int fd;
    ssize_t got;
    int flags;
    struct stat status;

    flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags);
    if (fd < 0) {
        if (errno == ENOENT) return REDP2P_ENOENT;
        redp2p_set_error(ctx, "key: cannot open persisted key: %s",
            strerror(errno));
        return REDP2P_ERROR;
    }
    if (fstat(fd, &status) != 0) {
        redp2p_set_error(ctx, "key: cannot inspect persisted key: %s",
            strerror(errno));
        close(fd);
        return REDP2P_ERROR;
    }
    if (!S_ISREG(status.st_mode)) {
        redp2p_set_error(ctx, "key: persisted key is not a regular file");
        close(fd);
        return REDP2P_ERROR;
    }
    if (status.st_size < REDP2P_KEY_SZ + 3 || status.st_size > 64)
    {
        redp2p_set_error(ctx, "key: persisted key has unexpected size");
        close(fd);
        return REDP2P_EPROTO;
    }
    *total = 0;
    while (*total < capacity) {
        got = read(fd, data + *total, capacity - *total);
        if (got > 0) {
            *total += (size_t)got;
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) {
            redp2p_set_error(ctx, "key: persisted key read failed: %s",
                strerror(errno));
            close(fd);
            return REDP2P_ERROR;
        }
        break;
    }
    if (close(fd) != 0) {
        redp2p_set_error(ctx, "key: persisted key close failed: %s",
            strerror(errno));
        return REDP2P_ERROR;
    }
    return REDP2P_OK;
}
#endif

/**
 * Loads and strictly validates one publisher session secret path.
 * @param ctx Context receiving error detail.
 * @param path Exact path to load.
 * @param key Output key.
 * @return REDP2P_OK, REDP2P_ENOENT, REDP2P_EPROTO, or REDP2P_ERROR.
 */
static int redp2p_load_key_path(redp2p_t *ctx, const char *path,
    char key[REDP2P_KEY_STR_SZ], uint64_t *sequence)
{
    char data[65];
    size_t total;
    int result;

#ifdef _WIN32
    result = redp2p_load_key_windows(ctx, path, data, sizeof(data), &total);
#else
    result = redp2p_load_key_posix(ctx, path, data, sizeof(data), &total);
#endif
    if (result != REDP2P_OK) {
        crypto_wipe(data, sizeof(data));
        return result;
    }
    if (!redp2p_key_parse(data, total, key, sequence)) {
        redp2p_set_error(ctx, "key: persisted key is malformed");
        crypto_wipe(data, sizeof(data));
        return REDP2P_EPROTO;
    }
    crypto_wipe(data, sizeof(data));
    return REDP2P_OK;
}

#ifdef _WIN32
/**
 * Durably replaces one Windows key file through a private temporary file.
 * @param ctx Context receiving error detail.
 * @param paths Scoped key paths.
 * @param temp Temporary key path.
 * @param content Complete key file content.
 * @param total Content byte count.
 * @return REDP2P_OK on replacement or REDP2P_ERROR on failure.
 */
static int redp2p_save_key_windows(
redp2p_t *ctx,
const redp2p_key_paths_t *paths,
const char *temp,
const char *content,
size_t total)
{
    HANDLE file;
    DWORD written;
    size_t offset;
    int result;

    result = REDP2P_ERROR;
    file = CreateFileA(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        redp2p_set_error(ctx, "key: temporary key create failed (%lu)",
            (unsigned long)GetLastError());
    } else {
        offset = 0;
        while (offset < total && WriteFile(file, content + offset,
            (DWORD)(total - offset), &written, NULL) && written > 0)
            offset += written;
        if (offset != total || !FlushFileBuffers(file)) {
            redp2p_set_error(ctx, "key: temporary key write failed (%lu)",
                (unsigned long)GetLastError());
        } else if (!CloseHandle(file)) {
            file = INVALID_HANDLE_VALUE;
            redp2p_set_error(ctx, "key: temporary key close failed");
        } else {
            file = INVALID_HANDLE_VALUE;
            if (MoveFileExA(temp, paths->scoped,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                result = REDP2P_OK;
            else
                redp2p_set_error(ctx, "key: atomic replacement failed (%lu)",
                    (unsigned long)GetLastError());
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    if (result != REDP2P_OK) DeleteFileA(temp);
    return result;
}
#endif

#ifndef _WIN32
/**
 * Durably replaces one POSIX key file and synchronizes its directory entry.
 * @param ctx Context receiving error detail.
 * @param paths Scoped key paths.
 * @param temp Temporary key path.
 * @param content Complete key file content.
 * @param total Content byte count.
 * @return REDP2P_OK on replacement or REDP2P_ERROR on failure.
 */
static int redp2p_save_key_posix(
redp2p_t *ctx,
const redp2p_key_paths_t *paths,
const char *temp,
const char *content,
size_t total)
{
    int fd;
    int flags;
    size_t offset;
    int result;

    result = REDP2P_ERROR;
    flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(temp, flags, 0600);
    if (fd < 0) {
        redp2p_set_error(ctx, "key: temporary key create failed: %s",
            strerror(errno));
    } else {
        offset = 0;
        while (offset < total) {
            ssize_t written;

            written = write(fd, content + offset, total - offset);
            if (written > 0) {
                offset += (size_t)written;
                continue;
            }
            if (written < 0 && errno == EINTR) continue;
            break;
        }
        if (offset != total || fchmod(fd, 0600) != 0 || fsync(fd) != 0) {
            redp2p_set_error(ctx, "key: temporary key write failed: %s",
                strerror(errno));
        } else if (close(fd) != 0) {
            fd = -1;
            redp2p_set_error(ctx, "key: temporary key close failed: %s",
                strerror(errno));
        } else {
            int dir_fd;

            fd = -1;
            if (rename(temp, paths->scoped) != 0) {
                redp2p_set_error(ctx, "key: atomic replacement failed: %s",
                    strerror(errno));
            } else {
                dir_fd = open(paths->dir, O_RDONLY
#ifdef O_DIRECTORY
                    | O_DIRECTORY
#endif
                );
                if (dir_fd < 0) {
                    redp2p_set_error(ctx,
                        "key: key directory sync failed: %s",
                        strerror(errno));
                } else if (fsync(dir_fd) != 0) {
                    redp2p_set_error(ctx,
                        "key: key directory sync failed: %s",
                        strerror(errno));
                    close(dir_fd);
                } else if (close(dir_fd) != 0) {
                    redp2p_set_error(ctx,
                        "key: key directory close failed: %s",
                        strerror(errno));
                } else {
                    result = REDP2P_OK;
                }
            }
        }
        if (fd >= 0) close(fd);
    }
    if (result != REDP2P_OK) unlink(temp);
    return result;
}
#endif

/**
 * Atomically persists one scoped publisher session secret.
 * @param ctx Context receiving error detail.
 * @param paths Scoped key paths.
 * @param key Publisher session secret.
 * @return REDP2P_OK on durable replacement or REDP2P_ERROR on failure.
 */
static int redp2p_save_key(redp2p_t *ctx, const redp2p_key_paths_t *paths,
    const char *key, uint64_t sequence)
{
    unsigned char random[8];
    char suffix[17];
    char temp[896];
    char dir[sizeof(paths->dir)];
    char content[64];
    size_t total;
    int name_len;
    int result;

    memcpy(dir, paths->dir, sizeof(dir));
    if (redp2p_mkdir_p(dir) != 0) {
        redp2p_set_error(ctx, "key: cannot create key directory: %s",
            strerror(errno));
        crypto_wipe(dir, sizeof(dir));
        return REDP2P_ERROR;
    }
    name_len = -1;
    if (redp2p_fill_random(random, sizeof(random)) == 0 &&
        redp2p_hex_encode(random, sizeof(random), suffix, sizeof(suffix)))
        name_len = snprintf(temp, sizeof(temp), "%s/.%s.tmp", paths->dir,
            suffix);
    if (name_len < 0 || (size_t)name_len >= sizeof(temp))
    {
        redp2p_set_error(ctx, "key: cannot create temporary key name");
        crypto_wipe(random, sizeof(random));
        crypto_wipe(suffix, sizeof(suffix));
        crypto_wipe(temp, sizeof(temp));
        crypto_wipe(dir, sizeof(dir));
        return REDP2P_ERROR;
    }
    name_len = snprintf(content, sizeof(content), "%s\n%llu\n", key,
        (unsigned long long)sequence);
    if (name_len < 0 || (size_t)name_len >= sizeof(content)) {
        redp2p_set_error(ctx, "key: cannot encode persisted sequence");
        crypto_wipe(random, sizeof(random));
        crypto_wipe(suffix, sizeof(suffix));
        crypto_wipe(temp, sizeof(temp));
        crypto_wipe(dir, sizeof(dir));
        crypto_wipe(content, sizeof(content));
        return REDP2P_ERROR;
    }
    total = (size_t)name_len;
    redp2p_key_lock();
#ifdef _WIN32
    result = redp2p_save_key_windows(ctx, paths, temp, content, total);
#else
    result = redp2p_save_key_posix(ctx, paths, temp, content, total);
#endif
    redp2p_key_unlock();
    crypto_wipe(random, sizeof(random));
    crypto_wipe(suffix, sizeof(suffix));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(dir, sizeof(dir));
    crypto_wipe(content, sizeof(content));
    return result;
}

/**
 * Removes one persisted path only while it still contains the observed key.
 * @param ctx Context receiving error detail.
 * @param path Exact persisted path.
 * @param key Observed publisher session secret.
 * @return REDP2P_OK when absent or removed, otherwise REDP2P_ERROR.
 */
static int redp2p_remove_key(redp2p_t *ctx, const char *path, const char *key) {
    char current[REDP2P_KEY_STR_SZ];
    uint64_t sequence;
    int loaded;
    int result;

    redp2p_key_lock();
    loaded = redp2p_load_key_path(ctx, path, current, &sequence);
    if (loaded == REDP2P_ENOENT) {
        crypto_wipe(current, sizeof(current));
        redp2p_key_unlock();
        return REDP2P_OK;
    }
    if (loaded != REDP2P_OK || strcmp(current, key) != 0) {
        if (loaded == REDP2P_OK)
            redp2p_set_error(ctx, "key: persisted key changed before removal");
        crypto_wipe(current, sizeof(current));
        redp2p_key_unlock();
        return REDP2P_ERROR;
    }
#ifdef _WIN32
    result = DeleteFileA(path) ? REDP2P_OK : REDP2P_ERROR;
    if (result != REDP2P_OK)
        redp2p_set_error(ctx, "key: persisted key removal failed (%lu)",
            (unsigned long)GetLastError());
#else
    result = unlink(path) == 0 ? REDP2P_OK : REDP2P_ERROR;
    if (result != REDP2P_OK)
        redp2p_set_error(ctx, "key: persisted key removal failed: %s",
            strerror(errno));
#endif
    crypto_wipe(current, sizeof(current));
    redp2p_key_unlock();
    return result;
}

/**
 * Discards one local session file that cannot be parsed by this version.
 * @param ctx Context receiving error detail.
 * @param path Exact persisted path.
 * @return REDP2P_OK when absent or removed, otherwise REDP2P_ERROR.
 */
static int redp2p_discard_key(redp2p_t *ctx, const char *path)
{
    int result;

    redp2p_key_lock();
#ifdef _WIN32
    result = DeleteFileA(path) ? REDP2P_OK : REDP2P_ERROR;
    if (result != REDP2P_OK && GetLastError() == ERROR_FILE_NOT_FOUND)
        result = REDP2P_OK;
    else if (result != REDP2P_OK)
        redp2p_set_error(ctx, "key: persisted key removal failed (%lu)",
            (unsigned long)GetLastError());
#else
    result = unlink(path) == 0 || errno == ENOENT ? REDP2P_OK : REDP2P_ERROR;
    if (result != REDP2P_OK)
        redp2p_set_error(ctx, "key: persisted key removal failed: %s",
            strerror(errno));
#endif
    redp2p_key_unlock();
    return result;
}

/**
 * Deregisters one publisher with an explicit observed session secret.
 * @param index_host Index host.
 * @param index_port Index port.
 * @param id Publisher identifier.
 * @param key Publisher session secret.
 * @param sequence Next publisher control sequence.
 * @return 0 on success, -1 on error.
 */
static int redp2p_deregister_with_key(
    redp2p_t *ctx,
    const char *index_host,
    unsigned short index_port,
    const char *id,
    const char *key,
    uint64_t sequence)
{
    JSON_Value *request;
    JSON_Object *obj;
    char proof[65];
    int result;

    if (!key || key[0] == '\0' || sequence == 0) return REDP2P_ENOENT;
    if (!redp2p_control_proof(key, "deregister", id, sequence, 0, 0,
        NULL, 0, proof))
        return REDP2P_ERROR;
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "deregister: index request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "deregister");
    json_object_set_string(obj, "id", id);
    json_object_set_number(obj, "seq", (double)sequence);
    json_object_set_string(obj, "proof", proof);
    result = redp2p_http_client(ctx, "deregister", index_host, index_port,
        request, NULL);
    json_value_free(request);
    crypto_wipe(proof, sizeof(proof));
    if (result != REDP2P_OK) return result;
    redp2p_set_error(ctx, NULL);
    return REDP2P_OK;
}

/**
 * Deregister.
 * @return 0 on success, -1 on error.
 */
int redp2p_deregister(
    redp2p_t *ctx,
    const char *index_host,
    unsigned short index_port,
    const char *id)
{
    redp2p_key_paths_t paths;
    const char *loaded_path;
    char key[REDP2P_KEY_STR_SZ];
    uint64_t sequence;
    int result;

    if (!ctx) return REDP2P_EINVAL;
    memset(key, 0, sizeof(key));
    redp2p_set_error(ctx, NULL);
    if (!index_host || !index_host[0]) {
        redp2p_set_error(ctx, "deregister: index host is missing");
        return REDP2P_EINVAL;
    }
    if (index_port == 0) {
        redp2p_set_error(ctx, "deregister: index port must be nonzero");
        return REDP2P_EINVAL;
    }
    if (!redp2p_is_valid_id(id)) {
        redp2p_set_error(ctx, "deregister: publisher id is invalid");
        return REDP2P_EINVAL;
    }
    result = redp2p_key_paths(ctx, index_host, index_port, id, &paths);
    if (result != REDP2P_OK) return result;
    redp2p_key_lock();
    sequence = 0;
    result = redp2p_load_key_path(ctx, paths.scoped, key, &sequence);
    loaded_path = paths.scoped;
    if (result == REDP2P_ENOENT) {
        result = redp2p_load_key_path(ctx, paths.legacy, key, &sequence);
        loaded_path = paths.legacy;
    }
    redp2p_key_unlock();
    if (result == REDP2P_ENOENT) {
        redp2p_set_error(ctx, "deregister: no persisted key for publisher");
        crypto_wipe(key, sizeof(key));
        return REDP2P_ENOENT;
    }
    if (result == REDP2P_OK) {
        if (sequence == 9007199254740991ULL ||
            redp2p_save_key(ctx, &paths, key, sequence + 1) != REDP2P_OK)
        {
            crypto_wipe(key, sizeof(key));
            return REDP2P_ERROR;
        }
        result = redp2p_deregister_with_key(ctx, index_host, index_port, id,
            key, sequence + 1);
        if (result == REDP2P_OK)
            result = redp2p_remove_key(ctx, loaded_path, key);
    }
    crypto_wipe(key, sizeof(key));
    return result;
}

/**
 * Validates publisher inputs and opens its control and UDP transports.
 * @param runtime Runtime that borrows inputs and owns opened descriptors.
 * @param ctx Publisher context to borrow.
 * @param index_host Index host to borrow.
 * @param index_port Index control port.
 * @param self_id Publisher identifier to borrow.
 * @param bind_port Requested local backend port.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_initialize(
redp2p_publisher_runtime_t *runtime,
redp2p_t *ctx,
const char *index_host,
unsigned short index_port,
const char *self_id,
unsigned short bind_port)
{
    unsigned short effective_port;

    memset(runtime, 0, sizeof(*runtime));
    runtime->borrowed_ctx = ctx;
    runtime->borrowed_index_host = index_host;
    runtime->borrowed_self_id = self_id;
    runtime->index_port = index_port;
    runtime->owned_udp_fd = REDP2P_FD_INVALID;
    if (ctx->proto != REDP2P_PROTO_TCP && ctx->proto != REDP2P_PROTO_UDP) {
        redp2p_set_error(ctx, "wait: invalid transport protocol");
        return REDP2P_EINVAL;
    }
    if (redp2p_resolve_port(ctx, bind_port, &effective_port) != REDP2P_OK) {
        redp2p_set_error(ctx, "wait: conflicting local ports");
        return REDP2P_EINVAL;
    }
    if (effective_port == 0 || !index_host || !self_id ||
        !redp2p_is_valid_id(self_id) || index_port == 0)
    {
        redp2p_set_error(ctx, "wait: invalid index, service id, or local port");
        return REDP2P_EINVAL;
    }
    ctx->bind_port = effective_port;
    runtime->borrowed_udp_any_host = redp2p_host_is_ipv6_literal(index_host) ?
        "::" : "0.0.0.0";
    runtime->owned_udp_fd = redp2p_create_socket(
        runtime->borrowed_udp_any_host, 0);
    if (REDP2P_ISERR(runtime->owned_udp_fd)) {
        return REDP2P_ENET;
    }
    return REDP2P_OK;
}

/**
 * Completes the registration challenge, proof, and response exchange.
 * @param runtime Initialized publisher runtime.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_register(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    JSON_Value *request;
    JSON_Value *response;
    JSON_Object *obj;
    JSON_Object *out;
    char nonce_hex[65];
    char mac_hex[65];
    char index_pkey_hex[65];
    char publisher_pkey_hex[65];
    char encrypted_secret_hex[65];
    char solution_hex[17];
    char proof[65];
    char access_proof[65];
    unsigned char nonce[32];
    unsigned char index_pkey[32];
    unsigned char publisher_skey[32];
    unsigned char publisher_pkey[32];
    unsigned char encrypted_secret[32];
    unsigned char message[384];
    unsigned char proof_hash[32];
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    int candidate_count;
    int bits;
    uint64_t issued_at;
    uint64_t expires_at;
    uint64_t solution;
    size_t message_len;
    int result;

    ctx = runtime->borrowed_ctx;
    if (!redp2p_generate_key(ctx->key)) {
        redp2p_set_error(ctx, "wait: publisher secret generation failed");
        return REDP2P_ERROR;
    }
    ctx->sequence = 0;
    memset(nonce_hex, 0, sizeof(nonce_hex));
    memset(mac_hex, 0, sizeof(mac_hex));
    memset(index_pkey_hex, 0, sizeof(index_pkey_hex));
    memset(publisher_pkey_hex, 0, sizeof(publisher_pkey_hex));
    memset(encrypted_secret_hex, 0, sizeof(encrypted_secret_hex));
    memset(solution_hex, 0, sizeof(solution_hex));
    memset(proof, 0, sizeof(proof));
    memset(access_proof, 0, sizeof(access_proof));
    memset(nonce, 0, sizeof(nonce));
    memset(index_pkey, 0, sizeof(index_pkey));
    memset(publisher_skey, 0, sizeof(publisher_skey));
    memset(publisher_pkey, 0, sizeof(publisher_pkey));
    memset(encrypted_secret, 0, sizeof(encrypted_secret));
    memset(message, 0, sizeof(message));
    memset(proof_hash, 0, sizeof(proof_hash));
    candidate_count = 0;
    if (redp2p_gather_candidates(ctx, runtime->owned_udp_fd, candidates,
        REDP2P_PEER_CANDIDATES_MAX, &candidate_count) != REDP2P_OK)
    {
        redp2p_set_error(ctx, "wait: local candidate gather failed");
        result = REDP2P_ENET;
        goto cleanup;
    }
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "wait: registration request allocation failed");
        result = REDP2P_ERROR;
        goto cleanup;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "challenge");
    json_object_set_string(obj, "id", runtime->borrowed_self_id);
    response = NULL;
    result = redp2p_http_client(ctx, "wait", runtime->borrowed_index_host,
        runtime->index_port, request, &response);
    json_value_free(request);
    if (result != REDP2P_OK) goto cleanup;
    out = json_value_get_object(response);
    if (!out || !redp2p_json_require_hex(out, "nonce", nonce_hex,
        sizeof(nonce_hex), 64) || !redp2p_json_require_hex(out, "mac",
        mac_hex, sizeof(mac_hex), 64) || !redp2p_json_require_u64(out,
        "issued_at", &issued_at) || !redp2p_json_require_u64(out,
        "expires_at", &expires_at) || !redp2p_hex_decode(nonce_hex, nonce,
        sizeof(nonce)) || !redp2p_json_require_hex(out, "pkey",
        index_pkey_hex, sizeof(index_pkey_hex), 64) || !redp2p_hex_decode(
        index_pkey_hex, index_pkey, sizeof(index_pkey)) ||
        !json_object_has_value_of_type(out, "bits", JSONNumber))
    {
        json_value_free(response);
        redp2p_set_error(ctx, "wait: malformed registration challenge");
        result = REDP2P_EPROTO;
        goto cleanup;
    }
    bits = (int)json_object_get_number(out, "bits");
    json_value_free(response);
    if (bits < 0 || bits > 32 || expires_at <= issued_at ||
        expires_at - issued_at != 60 || !redp2p_solve_register_pow(ctx, nonce,
        issued_at, expires_at, runtime->borrowed_self_id, bits, &solution) ||
        !redp2p_register_message(nonce, issued_at, expires_at,
            runtime->borrowed_self_id, ctx->key, ctx->proto, ctx->bind_port,
            candidates, candidate_count, solution, message, &message_len))
    {
        redp2p_set_error(ctx, "wait: registration proof solve failed");
        result = REDP2P_ERROR;
        goto cleanup;
    }
    if (ctx->pass[0] && !redp2p_admission_proof(ctx->pass, message,
        message_len, access_proof))
    {
        redp2p_set_error(ctx, "wait: admission proof generation failed");
        result = REDP2P_ERROR;
        goto cleanup;
    }
    snprintf(solution_hex, sizeof(solution_hex), "%016llx",
        (unsigned long long)solution);
    redp2p_hmac_sha256_bytes((const unsigned char *)ctx->key, strlen(ctx->key),
        message, message_len, proof_hash);
    if (!redp2p_hex_encode(proof_hash, sizeof(proof_hash), proof,
        sizeof(proof))) {
        redp2p_set_error(ctx, "wait: registration proof encode failed");
        result = REDP2P_ERROR;
        goto cleanup;
    }
    if (redp2p_fill_random(publisher_skey, sizeof(publisher_skey)) != 0 ||
        !redp2p_registration_secret_encrypt(publisher_skey, index_pkey,
            nonce, (const unsigned char *)ctx->key, publisher_pkey,
            encrypted_secret) || !redp2p_hex_encode(publisher_pkey,
            sizeof(publisher_pkey), publisher_pkey_hex,
            sizeof(publisher_pkey_hex)) || !redp2p_hex_encode(encrypted_secret,
            sizeof(encrypted_secret), encrypted_secret_hex,
            sizeof(encrypted_secret_hex)))
    {
        redp2p_set_error(ctx, "wait: registration secret protection failed");
        result = REDP2P_EAUTH;
        goto cleanup;
    }
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "wait: registration request allocation failed");
        result = REDP2P_ERROR;
        goto cleanup;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "register");
    json_object_set_string(obj, "id", runtime->borrowed_self_id);
    json_object_set_string(obj, "nonce", nonce_hex);
    json_object_set_number(obj, "issued_at", (double)issued_at);
    json_object_set_number(obj, "expires_at", (double)expires_at);
    json_object_set_string(obj, "mac", mac_hex);
    json_object_set_string(obj, "pow_solution", solution_hex);
    json_object_set_string(obj, "proof", proof);
    if (ctx->pass[0]) json_object_set_string(obj, "access_proof", access_proof);
    json_object_set_string(obj, "pkey", publisher_pkey_hex);
    json_object_set_string(obj, "secret", encrypted_secret_hex);
    json_object_set_number(obj, "proto", (double)ctx->proto);
    json_object_set_number(obj, "udp_port", (double)ctx->bind_port);
    redp2p_append_candidates(obj, "candidates", candidates,
        candidate_count);
    response = NULL;
    result = redp2p_http_client(ctx, "wait", runtime->borrowed_index_host,
        runtime->index_port, request, &response);
    json_value_free(request);
    if (result != REDP2P_OK) goto cleanup;
    out = json_value_get_object(response);
    if (!out || !json_object_has_value_of_type(out, "ok", JSONBoolean) ||
        !json_object_get_boolean(out, "ok"))
    {
        json_value_free(response);
        redp2p_set_error(ctx, "wait: malformed registration response");
        result = REDP2P_EPROTO;
        goto cleanup;
    }
    json_value_free(response);
    result = REDP2P_OK;

cleanup:
    crypto_wipe(nonce_hex, sizeof(nonce_hex));
    crypto_wipe(mac_hex, sizeof(mac_hex));
    crypto_wipe(index_pkey_hex, sizeof(index_pkey_hex));
    crypto_wipe(publisher_pkey_hex, sizeof(publisher_pkey_hex));
    crypto_wipe(encrypted_secret_hex, sizeof(encrypted_secret_hex));
    crypto_wipe(solution_hex, sizeof(solution_hex));
    crypto_wipe(proof, sizeof(proof));
    crypto_wipe(access_proof, sizeof(access_proof));
    crypto_wipe(nonce, sizeof(nonce));
    crypto_wipe(index_pkey, sizeof(index_pkey));
    crypto_wipe(publisher_skey, sizeof(publisher_skey));
    crypto_wipe(publisher_pkey, sizeof(publisher_pkey));
    crypto_wipe(encrypted_secret, sizeof(encrypted_secret));
    crypto_wipe(message, sizeof(message));
    crypto_wipe(proof_hash, sizeof(proof_hash));
    return result;
}

/**
 * Persists the publisher session secret and rolls registration back on failure.
 * @param runtime Registered publisher runtime.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_persist_registration(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    redp2p_key_paths_t paths;
    int path_result;

    ctx = runtime->borrowed_ctx;
    if (ctx->key[0] == '\0') {
        redp2p_set_error(ctx, "wait: publisher session secret is missing");
        return REDP2P_EPROTO;
    }
    path_result = redp2p_key_paths(ctx, runtime->borrowed_index_host,
        runtime->index_port, runtime->borrowed_self_id, &paths);
    if (path_result != REDP2P_OK ||
        redp2p_save_key(ctx, &paths, ctx->key, ctx->sequence) != REDP2P_OK)
    {
        redp2p_deregister_with_key(NULL, runtime->borrowed_index_host,
            runtime->index_port, runtime->borrowed_self_id, ctx->key,
            ctx->sequence + 1);
        if (path_result == REDP2P_OK)
            redp2p_remove_key(NULL, paths.scoped, ctx->key);
        ctx->key[0] = '\0';
        return REDP2P_ERROR;
    }
    return REDP2P_OK;
}

/**
 * Finds an active publisher session for one peer address and transport ID.
 * @param runtime Publisher runtime containing the session table.
 * @param peer_addr Peer address to match.
 * @param session_id TCP session identifier, or NULL for UDP.
 * @return Matching session index, or -1 when no session matches.
 */
static int redp2p_publisher_session_find(
const redp2p_publisher_runtime_t *runtime,
const struct sockaddr_storage *peer_addr,
const unsigned char *session_id)
{
    int i;

    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) continue;
        if (!redp2p_sockaddr_equal(&runtime->owned_sessions[i].peer_addr,
            peer_addr))
            continue;
        if (!runtime->owned_sessions[i].is_tcp && !session_id) return i;
        if (runtime->owned_sessions[i].is_tcp && session_id &&
            memcmp(runtime->owned_sessions[i].stream.session_id, session_id,
                REDP2P_SESSION_ID_SZ) == 0)
            return i;
    }
    return -1;
}

/**
 * Transfers one initialized publisher session into the owned table.
 * @param runtime Publisher runtime that owns the session table.
 * @param session Initialized session whose descriptors transfer on success.
 * @return Inserted session index, or -1 on allocation failure.
 */
static int redp2p_publisher_session_insert(
redp2p_publisher_runtime_t *runtime,
redp2p_udp_server_session_t *session)
{
    redp2p_udp_server_session_t *new_sessions;
    int index;
    int new_capacity;
    int i;

    index = -1;
    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) {
            index = i;
            break;
        }
    }
    if (index < 0 && runtime->session_count >= runtime->session_capacity) {
        new_capacity = runtime->session_capacity == 0 ? 8 :
            runtime->session_capacity * 2;
        new_sessions = (redp2p_udp_server_session_t *)realloc(
            runtime->owned_sessions,
            (size_t)new_capacity * sizeof(*runtime->owned_sessions));
        if (!new_sessions) {
            redp2p_server_session_close(session);
            crypto_wipe(session, sizeof(*session));
            return -1;
        }
        runtime->owned_sessions = new_sessions;
        runtime->session_capacity = new_capacity;
    }
    if (index < 0) index = runtime->session_count++;
    runtime->owned_sessions[index] = *session;
    crypto_wipe(session, sizeof(*session));
    return index;
}

/**
 * Closes all active publisher sessions in table order.
 * @param runtime Publisher runtime that owns the sessions.
 * @return None.
 */
static void redp2p_publisher_session_close_all(
redp2p_publisher_runtime_t *runtime)
{
    int i;

    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) continue;
        if (runtime->owned_sessions[i].is_tcp &&
            !redp2p_stream_is_done(&runtime->owned_sessions[i].stream))
        {
            redp2p_stream_fail(runtime->borrowed_ctx,
                &runtime->owned_sessions[i].stream);
        }
        redp2p_server_session_close(&runtime->owned_sessions[i]);
    }
}

/**
 * Selects a direct peer endpoint while preserving nonfatal punch failures.
 * @param runtime Publisher runtime owning the UDP descriptor.
 * @param connection_id Consumer identifier.
 * @param session_token Session token used by the punch exchange.
 * @param candidates Remote candidate array.
 * @param candidate_count Remote candidate count.
 * @param peer_addr Selected peer address.
 * @return 1 on selection, or 0 for a nonfatal punch failure.
 */
static int redp2p_publisher_select_peer(
redp2p_publisher_runtime_t *runtime,
const char *connection_id,
const char *session_token,
redp2p_candidate_t *candidates,
int candidate_count,
struct sockaddr_storage *peer_addr)
{
    memset(peer_addr, 0, sizeof(*peer_addr));
    return redp2p_punch_select(runtime->borrowed_ctx,
        runtime->borrowed_ctx->sweep, runtime->owned_udp_fd, session_token,
        runtime->borrowed_self_id, connection_id, candidates, candidate_count,
        peer_addr) == REDP2P_OK;
}

/**
 * Opens a backend and creates a publisher session when the peer is new.
 * @param runtime Publisher runtime that owns created session resources.
 * @param session_hex Canonical TCP stream session token.
 * @param session_id Decoded TCP stream session identifier.
 * @param peer_addr Selected peer address.
 * @return 1 when the peer is ready, or 0 on a nonfatal backend failure.
 */
static int redp2p_publisher_open_session(
redp2p_publisher_runtime_t *runtime,
const char *session_hex,
const unsigned char session_id[REDP2P_SESSION_ID_SZ],
const struct sockaddr_storage *peer_addr)
{
    redp2p_udp_server_session_t session;
    redp2p_t *ctx;

    ctx = runtime->borrowed_ctx;
    if (redp2p_publisher_session_find(runtime, peer_addr,
        ctx->proto == REDP2P_PROTO_TCP ? session_id : NULL) >= 0)
        return 1;
    memset(&session, 0, sizeof(session));
    session.backend_fd = REDP2P_FD_INVALID;
    session.tcp_fd = REDP2P_FD_INVALID;
    session.peer_addr = *peer_addr;
    session.last_rx = redp2p_now_s();
    session.last_ka = session.last_rx;
    session.is_tcp = ctx->proto == REDP2P_PROTO_TCP ? 1 : 0;
    if (session.is_tcp) {
        session.backend_fd = redp2p_connect_local_tcp(ctx->bind_port);
        if (REDP2P_ISERR(session.backend_fd)) {
            redp2p_set_error(ctx, "local backend connect failed on port %u",
                (unsigned)ctx->bind_port);
            crypto_wipe(&session, sizeof(session));
            return 0;
        }
        if (redp2p_stream_init(ctx, &session.stream, 0,
            runtime->owned_udp_fd, peer_addr, session_id, session_hex,
            REDP2P_PROTO_TCP) != 0)
        {
            REDP2P_FD_CLOSE(session.backend_fd);
            crypto_wipe(&session, sizeof(session));
            return 0;
        }
    } else {
        session.backend_fd = redp2p_create_socket(
            runtime->borrowed_udp_any_host, 0);
        if (REDP2P_ISERR(session.backend_fd)) {
            crypto_wipe(&session, sizeof(session));
            return 0;
        }
    }
    session.active = 1;
    return redp2p_publisher_session_insert(runtime, &session) >= 0;
}

/**
 * Processes one publisher punch_poll reply containing pending calls.
 * @param runtime Publisher runtime owning UDP and session state.
 * @param out     Punch poll reply object.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_process_punch_calls(
redp2p_publisher_runtime_t *runtime,
JSON_Object *out)
{
    JSON_Array *calls;
    size_t count;
    size_t i;

    if (!out || !json_object_has_value_of_type(out, "calls", JSONArray)) {
        redp2p_set_error(runtime->borrowed_ctx,
            "wait: malformed punch poll response");
        return REDP2P_EPROTO;
    }
    calls = json_object_get_array(out, "calls");
    count = json_array_get_count(calls);
    for (i = 0; i < count; i++) {
        JSON_Object *call;
        const char *connection_id;
        const char *remote_token;
        redp2p_candidate_t remote_candidates[REDP2P_PEER_CANDIDATES_MAX];
        struct sockaddr_storage peer_addr;
        char session_hex[REDP2P_SESSION_ID_SZ * 2 + 1];
        unsigned char session_id[REDP2P_SESSION_ID_SZ];
        int candidate_count;

        memset(session_hex, 0, sizeof(session_hex));
        memset(session_id, 0, sizeof(session_id));
        call = json_array_get_object(calls, i);
        if (!call) return REDP2P_EPROTO;
        connection_id = json_object_get_string(call, "self_id");
        remote_token = json_object_get_string(call, "session");
        if (!connection_id || !redp2p_is_valid_id(connection_id) ||
            !remote_token || !redp2p_is_session_token(remote_token) ||
            !redp2p_parse_candidates(call, "candidates",
                remote_candidates, &candidate_count))
            return REDP2P_EPROTO;
        if (runtime->borrowed_ctx->proto == REDP2P_PROTO_TCP) {
            if (!redp2p_is_hex_token(remote_token,
                REDP2P_SESSION_ID_SZ * 2))
                continue;
            memcpy(session_hex, remote_token, REDP2P_SESSION_ID_SZ * 2);
            session_hex[REDP2P_SESSION_ID_SZ * 2] = '\0';
            if (!redp2p_hex_decode(session_hex, session_id,
                sizeof(session_id)))
                continue;
        } else {
            snprintf(session_hex, sizeof(session_hex), "%s", remote_token);
        }
        if (!redp2p_publisher_select_peer(runtime, connection_id,
            session_hex, remote_candidates, candidate_count, &peer_addr))
            continue;
        if (!redp2p_publisher_open_session(runtime, session_hex,
            session_id, &peer_addr))
            continue;
        redp2p_sendto_addr(runtime->owned_udp_fd, REDP2P_CTRTOK_PUNCH_SERVER,
            strlen(REDP2P_CTRTOK_PUNCH_SERVER), &peer_addr);
    }
    return REDP2P_OK;
}

/**
 * Builds the publisher read set and closes unrepresentable backends.
 * @param runtime Publisher runtime containing all descriptors.
 * @param read_fds Output descriptor set.
 * @param max_fd Output highest descriptor.
 * @return 1 when selectable, 0 to retry, or REDP2P_ENET on fatal failure.
 */
static int redp2p_publisher_build_fdset(
redp2p_publisher_runtime_t *runtime,
fd_set *read_fds,
int *max_fd)
{
    redp2p_t *ctx;
    int failed;
    int i;

    ctx = runtime->borrowed_ctx;
    FD_ZERO(read_fds);
    *max_fd = -1;
    if (!redp2p_fdset_add(runtime->owned_udp_fd, read_fds, max_fd))
    {
        redp2p_set_error(ctx,
            "wait: essential descriptor cannot be represented by fd_set");
        return REDP2P_ENET;
    }
    failed = 0;
    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) continue;
        if (runtime->owned_sessions[i].backend_fd == REDP2P_FD_INVALID) continue;
        if (runtime->owned_sessions[i].is_tcp &&
            !redp2p_stream_can_send_data(&runtime->owned_sessions[i].stream))
            continue;
        if (redp2p_fdset_add(runtime->owned_sessions[i].backend_fd, read_fds,
            max_fd))
            continue;
        redp2p_set_error(ctx,
            "wait: backend descriptor cannot be represented by fd_set");
        if (runtime->owned_sessions[i].is_tcp)
            redp2p_stream_fail(ctx, &runtime->owned_sessions[i].stream);
        redp2p_server_session_close(&runtime->owned_sessions[i]);
        failed = 1;
    }
    return failed ? 0 : 1;
}

/**
 * Re-registers the publisher and refreshes its persisted key after expiry.
 * @param runtime Publisher runtime owning UDP and identity state.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_reregister(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    redp2p_key_paths_t paths;
    int result;

    ctx = runtime->borrowed_ctx;
    result = redp2p_publisher_register(runtime);
    if (result != REDP2P_OK) return result;
    if (redp2p_key_paths(ctx, runtime->borrowed_index_host,
        runtime->index_port, runtime->borrowed_self_id, &paths) == REDP2P_OK &&
        redp2p_save_key(ctx, &paths, ctx->key, ctx->sequence) != REDP2P_OK)
    {
        redp2p_deregister_with_key(NULL, runtime->borrowed_index_host,
            runtime->index_port, runtime->borrowed_self_id, ctx->key,
            ctx->sequence + 1);
        ctx->key[0] = '\0';
        return REDP2P_ERROR;
    }
    return REDP2P_OK;
}

/**
 * Sends one due publisher heartbeat, re-registering when the record expired.
 * @param runtime Publisher runtime owning UDP and identity state.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_heartbeat(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    JSON_Value *request;
    JSON_Value *response;
    JSON_Object *obj;
    redp2p_candidate_t candidates[REDP2P_PEER_CANDIDATES_MAX];
    redp2p_key_paths_t paths;
    char proof[65];
    int candidate_count;
    int result;
    uint64_t sequence;

    ctx = runtime->borrowed_ctx;
    if (redp2p_now_s() - runtime->last_heartbeat < ctx->heartbeat_s)
        return REDP2P_OK;
    if (ctx->key[0] == '\0') return REDP2P_ENOENT;
    if (ctx->sequence == 9007199254740991ULL) {
        redp2p_set_error(ctx, "wait: publisher control sequence exhausted");
        return REDP2P_EPROTO;
    }
    candidate_count = 0;
    if (redp2p_gather_candidates(ctx, runtime->owned_udp_fd, candidates,
        REDP2P_PEER_CANDIDATES_MAX, &candidate_count) != REDP2P_OK)
    {
        redp2p_set_error(ctx, "wait: local candidate gather failed");
        return REDP2P_ENET;
    }
    sequence = ctx->sequence + 1;
    if (!redp2p_control_proof(ctx->key, "heartbeat",
        runtime->borrowed_self_id, sequence, ctx->proto,
        ctx->bind_port, candidates, candidate_count, proof))
    {
        redp2p_set_error(ctx, "wait: heartbeat proof generation failed");
        return REDP2P_ERROR;
    }
    if (redp2p_key_paths(ctx, runtime->borrowed_index_host,
        runtime->index_port, runtime->borrowed_self_id, &paths) != REDP2P_OK ||
        redp2p_save_key(ctx, &paths, ctx->key, sequence) != REDP2P_OK)
    {
        crypto_wipe(proof, sizeof(proof));
        return REDP2P_ERROR;
    }
    ctx->sequence = sequence;
    request = json_value_init_object();
    if (!request) {
        redp2p_set_error(ctx, "wait: heartbeat request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "heartbeat");
    json_object_set_string(obj, "id", runtime->borrowed_self_id);
    json_object_set_number(obj, "seq", (double)sequence);
    json_object_set_string(obj, "proof", proof);
    json_object_set_number(obj, "proto", (double)ctx->proto);
    json_object_set_number(obj, "udp_port", (double)ctx->bind_port);
    redp2p_append_candidates(obj, "candidates", candidates,
        candidate_count);
    response = NULL;
    result = redp2p_http_client(ctx, "wait", runtime->borrowed_index_host,
        runtime->index_port, request, &response);
    json_value_free(request);
    if (response) json_value_free(response);
    crypto_wipe(proof, sizeof(proof));
    if (result == REDP2P_ENET &&
        strncmp(redp2p_get_error(ctx), "wait: index response", 20) == 0)
    {
        runtime->last_heartbeat = redp2p_now_s();
        return REDP2P_OK;
    }
    if (result == REDP2P_OK || result == REDP2P_ENOENT)
    {
        if (result != REDP2P_OK) result = redp2p_publisher_reregister(runtime);
        if (result == REDP2P_OK)
            runtime->last_heartbeat = redp2p_now_s();
    }
    return result;
}

/**
 * Polls the index for pending punch calls and processes them.
 * @param runtime Publisher runtime owning UDP and session state.
 * @return REDP2P_OK on success, or a negative error code on failure.
 */
static int redp2p_publisher_punch_poll(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    JSON_Value *request;
    JSON_Value *response;
    JSON_Object *obj;
    JSON_Object *out;
    redp2p_key_paths_t paths;
    char proof[65];
    int result;
    uint64_t sequence;

    ctx = runtime->borrowed_ctx;
    if (redp2p_now_ms() - runtime->last_punch_poll < ctx->punch_poll_ms)
        return REDP2P_OK;
    if (ctx->key[0] == '\0') return REDP2P_ENOENT;
    if (ctx->sequence == 9007199254740991ULL) {
        redp2p_set_error(ctx, "wait: publisher control sequence exhausted");
        return REDP2P_EPROTO;
    }
    sequence = ctx->sequence + 1;
    if (!redp2p_control_proof(ctx->key, "punch_poll",
        runtime->borrowed_self_id, sequence, 0, 0, NULL, 0, proof))
    {
        redp2p_set_error(ctx, "wait: punch poll proof generation failed");
        return REDP2P_ERROR;
    }
    if (redp2p_key_paths(ctx, runtime->borrowed_index_host,
        runtime->index_port, runtime->borrowed_self_id, &paths) != REDP2P_OK ||
        redp2p_save_key(ctx, &paths, ctx->key, sequence) != REDP2P_OK)
    {
        crypto_wipe(proof, sizeof(proof));
        return REDP2P_ERROR;
    }
    ctx->sequence = sequence;
    request = json_value_init_object();
    if (!request) {
        crypto_wipe(proof, sizeof(proof));
        redp2p_set_error(ctx, "wait: punch poll request allocation failed");
        return REDP2P_ERROR;
    }
    obj = json_value_get_object(request);
    json_object_set_string(obj, "op", "punch_poll");
    json_object_set_string(obj, "id", runtime->borrowed_self_id);
    json_object_set_number(obj, "seq", (double)sequence);
    json_object_set_string(obj, "proof", proof);
    response = NULL;
    result = redp2p_http_client(ctx, "wait", runtime->borrowed_index_host,
        runtime->index_port, request, &response);
    json_value_free(request);
    crypto_wipe(proof, sizeof(proof));
    if (result != REDP2P_OK) {
        if (result == REDP2P_ENOENT)
            result = redp2p_publisher_reregister(runtime);
        runtime->last_punch_poll = redp2p_now_ms();
        if (result == REDP2P_OK || result == REDP2P_ETIMEOUT)
            return REDP2P_OK;
        if (response) json_value_free(response);
        return result;
    }
    out = json_value_get_object(response);
    result = redp2p_publisher_process_punch_calls(runtime, out);
    json_value_free(response);
    runtime->last_punch_poll = redp2p_now_ms();
    return result;
}

/**
 * Receives one peer datagram and forwards eligible payload to its backend.
 * @param runtime Publisher runtime owning UDP and session state.
 * @param read_fds Descriptor set returned by select.
 * @return 0 to continue, 1 to skip the iteration, or a negative error code.
 */
static int redp2p_publisher_receive_peer(
redp2p_publisher_runtime_t *runtime,
const fd_set *read_fds)
{
    redp2p_udp_server_session_t *session;
    struct sockaddr_storage from;
    struct sockaddr_in backend_addr;
    char buf[REDP2P_BUF + 1];
    char pong[256];
    char ping_session[64] = {0};
    char ping_from[64] = {0};
    char ping_to[64] = {0};
    socklen_t from_length;
    int receive_flags;
    int found;
    int n;
    redp2p_session_envelope_t envelope;

    if (!FD_ISSET(runtime->owned_udp_fd, read_fds)) return 0;
    from_length = sizeof(from);
    receive_flags = 0;
#ifndef _WIN32
    receive_flags |= MSG_TRUNC;
#endif
    n = (int)recvfrom(runtime->owned_udp_fd, buf, sizeof(buf) - 1,
        receive_flags, (struct sockaddr *)&from, &from_length);
    if (n < 0) return 0;
#ifndef _WIN32
    if ((size_t)n > sizeof(buf) - 1) return 0;
#endif
    if ((size_t)n >= sizeof(buf)) n = (int)(sizeof(buf) - 1);
    buf[n] = '\0';
    found = -1;
    if (runtime->borrowed_ctx->proto == REDP2P_PROTO_TCP) {
        if (redp2p_session_unpack((const unsigned char *)buf, (size_t)n,
            &envelope) && redp2p_stream_envelope_valid(&envelope))
        {
            found = redp2p_publisher_session_find(runtime, &from,
                envelope.session_id);
        }
    } else {
        found = redp2p_publisher_session_find(runtime, &from, NULL);
    }
    if (found < 0) {
        if (strncmp(buf, REDP2P_CTRTOK_PUNCH_PING,
            strlen(REDP2P_CTRTOK_PUNCH_PING)) == 0 &&
            redp2p_parse_punch_packet(buf, REDP2P_CTRTOK_PUNCH_PING,
                ping_session, ping_from, ping_to))
        {
            snprintf(pong, sizeof(pong), "%s%s:%s:%s",
                REDP2P_CTRTOK_PUNCH_PONG, ping_session, ping_to, ping_from);
            sendto(runtime->owned_udp_fd, pong, strlen(pong), 0,
                (const struct sockaddr *)&from, from_length);
        }
        return 0;
    }
    session = &runtime->owned_sessions[found];
    if (!session->is_tcp) {
        if (!redp2p_session_unpack((const unsigned char *)buf, (size_t)n,
            &envelope) || !redp2p_udp_envelope_valid(&envelope,
            REDP2P_SESSION_ROLE_INITIATOR))
            return 0;
        if (envelope.type == REDP2P_SESSION_TYPE_KEEPALIVE) {
            session->last_rx = redp2p_now_s();
            return 0;
        }
    }
    if (session->is_tcp) {
        if (session->backend_fd != REDP2P_FD_INVALID &&
            redp2p_stream_process_packet(runtime->borrowed_ctx, &session->stream,
                session->backend_fd, (const unsigned char *)buf,
                (size_t)n) != 0)
        {
            redp2p_stream_fail(runtime->borrowed_ctx, &session->stream);
            redp2p_server_session_close(session);
        }
    } else {
        memset(&backend_addr, 0, sizeof(backend_addr));
        backend_addr.sin_family = AF_INET;
        backend_addr.sin_port = htons(runtime->borrowed_ctx->bind_port);
        inet_pton(AF_INET, "127.0.0.1", &backend_addr.sin_addr);
        sendto(session->backend_fd, (const char *)envelope.payload,
            envelope.payload_len, 0,
            (const struct sockaddr *)&backend_addr, sizeof(backend_addr));
    }
    session->last_rx = redp2p_now_s();
    return 0;
}

/**
 * Processes ready backend descriptors and forwards data to peers.
 * @param runtime Publisher runtime owning backend and peer descriptors.
 * @param read_fds Descriptor set returned by select.
 * @return None.
 */
static void redp2p_publisher_process_backends(
redp2p_publisher_runtime_t *runtime,
const fd_set *read_fds)
{
    struct sockaddr_in backend_from;
    char buf[REDP2P_BUF];
    socklen_t backend_from_length;
    int i;
    int n;

    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) continue;
        if (runtime->owned_sessions[i].backend_fd == REDP2P_FD_INVALID) continue;
        if (!FD_ISSET(runtime->owned_sessions[i].backend_fd, read_fds))
            continue;
        if (runtime->owned_sessions[i].is_tcp) {
            if (redp2p_stream_pump_tcp(runtime->borrowed_ctx,
                &runtime->owned_sessions[i].stream,
                runtime->owned_sessions[i].backend_fd) != 0)
            {
                redp2p_stream_fail(runtime->borrowed_ctx,
                    &runtime->owned_sessions[i].stream);
                redp2p_server_session_close(&runtime->owned_sessions[i]);
            }
            continue;
        }
        backend_from_length = sizeof(backend_from);
        n = (int)recvfrom(runtime->owned_sessions[i].backend_fd, buf,
            sizeof(buf), 0, (struct sockaddr *)&backend_from,
            &backend_from_length);
        if (n < 0) continue;
        if (backend_from.sin_family != AF_INET ||
            backend_from.sin_port != htons(runtime->borrowed_ctx->bind_port) ||
            ntohl(backend_from.sin_addr.s_addr) != REDP2P_IPV4_LOOPBACK)
            continue;
        redp2p_udp_send(runtime->owned_udp_fd,
            &runtime->owned_sessions[i].peer_addr,
            REDP2P_SESSION_ROLE_RESPONDER, REDP2P_SESSION_TYPE_DATA,
            buf, (size_t)n);
        runtime->owned_sessions[i].last_rx = redp2p_now_s();
    }
}

/**
 * Advances publisher stream, keepalive, and disconnect state.
 * @param runtime Publisher runtime owning all sessions.
 * @return None.
 */
static void redp2p_publisher_maintain_sessions(
redp2p_publisher_runtime_t *runtime)
{
    int i;

    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active) continue;
        if (runtime->owned_sessions[i].is_tcp) {
            if (redp2p_stream_tick(runtime->borrowed_ctx,
                &runtime->owned_sessions[i].stream) != 0)
            {
                redp2p_stream_fail(runtime->borrowed_ctx,
                    &runtime->owned_sessions[i].stream);
                redp2p_server_session_close(&runtime->owned_sessions[i]);
                continue;
            }
            if (redp2p_stream_is_done(&runtime->owned_sessions[i].stream)) {
                redp2p_server_session_close(&runtime->owned_sessions[i]);
                continue;
            }
        }
        if (redp2p_now_s() - runtime->owned_sessions[i].last_ka >
            REDP2P_KEEPALIVE_S)
        {
            if (!runtime->owned_sessions[i].is_tcp)
                redp2p_udp_send(runtime->owned_udp_fd,
                    &runtime->owned_sessions[i].peer_addr,
                    REDP2P_SESSION_ROLE_RESPONDER,
                    REDP2P_SESSION_TYPE_KEEPALIVE, NULL, 0);
            runtime->owned_sessions[i].last_ka = redp2p_now_s();
        }
        if (redp2p_now_s() - runtime->owned_sessions[i].last_rx >
            REDP2P_DISCONNECT_S)
            redp2p_server_session_close(&runtime->owned_sessions[i]);
    }
}

/**
 * Finds the nearest publisher-side KCP deadline.
 * @return Milliseconds until select should wake, capped at one second.
 */
static uint32_t redp2p_publisher_wait_ms(
    const redp2p_publisher_runtime_t *runtime)
{
    uint32_t wait_ms;
    uint32_t session_wait;
    uint64_t now;
    int i;

    wait_ms = 1000;
    now = redp2p_now_ms();
    {
        int64_t until_poll = (int64_t)runtime->last_punch_poll +
            runtime->borrowed_ctx->punch_poll_ms - (int64_t)now;

        if (until_poll < 1) until_poll = 1;
        if ((uint64_t)until_poll < wait_ms) wait_ms = (uint32_t)until_poll;
    }
    for (i = 0; i < runtime->session_count; i++) {
        if (!runtime->owned_sessions[i].active ||
            !runtime->owned_sessions[i].is_tcp)
            continue;
        session_wait = redp2p_stream_wait_ms(
            &runtime->owned_sessions[i].stream, now);
        if (session_wait < wait_ms) wait_ms = session_wait;
    }
    return wait_ms;
}

/**
 * Runs the publisher event loop in its established event order.
 * @param runtime Registered publisher runtime.
 * @return REDP2P_OK on shutdown, or a negative error code on failure.
 */
static int redp2p_publisher_event_loop(
redp2p_publisher_runtime_t *runtime)
{
    fd_set read_fds;
    struct timeval timeout;
    int stage_result;
    int max_fd;
    int selected;
    int result;
    uint32_t wait_ms;

    result = REDP2P_OK;
    runtime->last_heartbeat = redp2p_now_s();
    runtime->last_punch_poll = redp2p_now_ms();
    redp2p_set_nonblock(runtime->owned_udp_fd);
    while (!runtime->borrowed_ctx->stop_requested) {
        stage_result = redp2p_publisher_build_fdset(runtime, &read_fds, &max_fd);
        if (stage_result < 0) {
            result = stage_result;
            break;
        }
        if (stage_result == 0) continue;
        wait_ms = redp2p_publisher_wait_ms(runtime);
        timeout.tv_sec = (long)(wait_ms / 1000u);
        timeout.tv_usec = (long)((wait_ms % 1000u) * 1000u);
        selected = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (selected < 0) continue;
        if (runtime->borrowed_ctx->stop_requested) break;
        stage_result = redp2p_publisher_heartbeat(runtime);
        if (stage_result != REDP2P_OK && stage_result != REDP2P_ETIMEOUT) {
            result = stage_result;
            break;
        }
        stage_result = redp2p_publisher_punch_poll(runtime);
        if (stage_result != REDP2P_OK && stage_result != REDP2P_ETIMEOUT) {
            result = stage_result;
            break;
        }
        stage_result = redp2p_publisher_receive_peer(runtime, &read_fds);
        if (stage_result < 0) {
            result = stage_result;
            break;
        }
        if (stage_result > 0) {
            redp2p_publisher_maintain_sessions(runtime);
            continue;
        }
        redp2p_publisher_process_backends(runtime, &read_fds);
        redp2p_publisher_maintain_sessions(runtime);
    }
    redp2p_publisher_session_close_all(runtime);
    return result;
}

/**
 * Loads one persisted publisher session and proves its current ownership.
 * @param runtime Initialized publisher runtime.
 * @return REDP2P_OK on resumed session, REDP2P_ENOENT when no state exists,
 *         or a negative error code.
 */
static int redp2p_publisher_resume_session(
redp2p_publisher_runtime_t *runtime)
{
    redp2p_t *ctx;
    redp2p_key_paths_t paths;
    char key[REDP2P_KEY_STR_SZ];
    uint64_t sequence;
    int result;

    ctx = runtime->borrowed_ctx;
    result = redp2p_key_paths(ctx, runtime->borrowed_index_host,
        runtime->index_port, runtime->borrowed_self_id, &paths);
    if (result != REDP2P_OK) return result;
    memset(key, 0, sizeof(key));
    sequence = 0;
    redp2p_key_lock();
    result = redp2p_load_key_path(ctx, paths.scoped, key, &sequence);
    redp2p_key_unlock();
    if (result == REDP2P_EPROTO) {
        result = redp2p_discard_key(ctx, paths.scoped);
        crypto_wipe(key, sizeof(key));
        if (result != REDP2P_OK) return result;
        redp2p_set_error(ctx, NULL);
        return REDP2P_ENOENT;
    }
    if (result != REDP2P_OK) {
        crypto_wipe(key, sizeof(key));
        return result;
    }
    memcpy(ctx->key, key, sizeof(ctx->key));
    ctx->sequence = sequence;
    crypto_wipe(key, sizeof(key));
    return redp2p_publisher_heartbeat(runtime);
}

/**
 * Deregisters the publisher and removes its persisted key when accepted.
 * @param runtime Publisher runtime borrowing registration identity.
 * @param wait_result Current wait result, updated on key removal failure.
 * @return None.
 */
static void redp2p_publisher_remove_registration(
redp2p_publisher_runtime_t *runtime,
int *wait_result)
{
    redp2p_t *ctx;
    redp2p_key_paths_t paths;
    char prior_error[sizeof(runtime->borrowed_ctx->err_buf)];
    int deregistered;
    int removed;

    ctx = runtime->borrowed_ctx;
    if (ctx->key[0] != '\0') {
        memcpy(prior_error, ctx->err_buf, sizeof(prior_error));
        removed = REDP2P_OK;
        deregistered = redp2p_deregister_with_key(ctx,
            runtime->borrowed_index_host, runtime->index_port,
            runtime->borrowed_self_id, ctx->key, ctx->sequence + 1);
        if (deregistered == REDP2P_OK) {
            if (redp2p_key_paths(ctx, runtime->borrowed_index_host,
                runtime->index_port, runtime->borrowed_self_id, &paths) !=
                REDP2P_OK ||
                redp2p_remove_key(ctx, paths.scoped, ctx->key) != REDP2P_OK)
            {
                removed = REDP2P_ERROR;
                *wait_result = REDP2P_ERROR;
            }
        }
        if (prior_error[0] != '\0' && deregistered == REDP2P_OK &&
            removed == REDP2P_OK)
            memcpy(ctx->err_buf, prior_error, sizeof(ctx->err_buf));
        crypto_wipe(prior_error, sizeof(prior_error));
    }
    crypto_wipe(ctx->key, sizeof(ctx->key));
}

/**
 * Releases all publisher-owned runtime resources and optionally resets stop.
 * @param runtime Publisher runtime whose owned resources are released.
 * @param reset_stop Whether to clear the context stop request.
 * @return None.
 */
static void redp2p_publisher_runtime_cleanup(
redp2p_publisher_runtime_t *runtime,
int reset_stop)
{
    redp2p_publisher_session_close_all(runtime);
    if (runtime->owned_sessions)
        crypto_wipe(runtime->owned_sessions,
            (size_t)runtime->session_capacity *
            sizeof(*runtime->owned_sessions));
    free(runtime->owned_sessions);
    runtime->owned_sessions = NULL;
    if (!REDP2P_ISERR(runtime->owned_udp_fd))
        REDP2P_FD_CLOSE(runtime->owned_udp_fd);
    runtime->owned_udp_fd = REDP2P_FD_INVALID;
    if (reset_stop)
        atomic_store(&runtime->borrowed_ctx->stop_requested, 0);
}

/**
 * Registers a publisher, serves sessions, and tears registration down.
 * @return 0 on success, or a negative error code on failure.
 */
int redp2p_wait(
    redp2p_t *ctx,
    const char *index_host,
    unsigned short index_port,
    const char *self_id,
    unsigned short bind_port)
{
    redp2p_publisher_runtime_t runtime;
    int wait_result;

    if (!ctx) return REDP2P_EINVAL;
    redp2p_set_error(ctx, NULL);
    if (redp2p_is_stop_requested(ctx)) {
        atomic_store(&ctx->stop_requested, 0);
        return REDP2P_OK;
    }
    wait_result = redp2p_publisher_initialize(&runtime, ctx, index_host,
        index_port, self_id, bind_port);
    if (wait_result != REDP2P_OK) return wait_result;
    wait_result = redp2p_publisher_resume_session(&runtime);
    if (wait_result == REDP2P_ENOENT) {
        wait_result = redp2p_publisher_register(&runtime);
        if (wait_result == REDP2P_OK)
            wait_result = redp2p_publisher_persist_registration(&runtime);
    }
    if (wait_result != REDP2P_OK) {
        redp2p_publisher_runtime_cleanup(&runtime, 0);
        return wait_result;
    }
    redp2p_set_error(ctx, NULL);
    wait_result = redp2p_publisher_event_loop(&runtime);
    redp2p_publisher_remove_registration(&runtime, &wait_result);
    redp2p_publisher_runtime_cleanup(&runtime, 1);
    return wait_result;
}

#ifdef REDP2P_TEST_RANDOM
/**
 * Generates one publisher session secret through the test-visible path.
 * @param out Output key buffer.
 * @return 1 on success, 0 on error.
 */
int redp2p_test_generate_key(char *out) {
    return redp2p_generate_key(out);
}
#endif
