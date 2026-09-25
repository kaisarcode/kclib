/**
 * libtrust.c - Portable identity trust and message cryptography.
 * Summary: Scoped trust relationships using Noise one-way patterns.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#ifdef __EMSCRIPTEN__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#endif

#include "libtrust.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "monocypher.h"

#ifndef KC_TRUST_BUILD_VERSION
#define KC_TRUST_BUILD_VERSION 0
#endif

#define KC_TRUST_PATH_SIZE 4096
#define KC_TRUST_SK_SIZE 32
#define KC_TRUST_PK_SIZE 32
#define KC_TRUST_PSK_SIZE 32
#define KC_TRUST_UID_BYTES 16
#define KC_TRUST_MAC_SIZE 16
#define KC_TRUST_NONCE_SIZE 12
#define KC_TRUST_HASH_SIZE 64
#define KC_TRUST_BLAKE2B_BLOCK 128
#define KC_TRUST_RECORD_MAGIC_SIZE 4
#define KC_TRUST_PENDING_RECORD_SIZE (KC_TRUST_RECORD_MAGIC_SIZE + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE + KC_TRUST_PSK_SIZE)
#define KC_TRUST_PEER_RECORD_SIZE (KC_TRUST_RECORD_MAGIC_SIZE + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE + KC_TRUST_PK_SIZE)
#define KC_TRUST_LOCAL_RECORD_SIZE (KC_TRUST_RECORD_MAGIC_SIZE + KC_TRUST_UID_BYTES)

#define KC_TRUST_INVITE_VERSION 1
#define KC_TRUST_INVITE_RAW_SIZE (1 + (2 * KC_TRUST_UID_BYTES) + KC_TRUST_PK_SIZE + KC_TRUST_PSK_SIZE)

#define KC_TRUST_XPSK1_MESSAGE_SIZE (KC_TRUST_PK_SIZE + (KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE) + KC_TRUST_MAC_SIZE)
#define KC_TRUST_CONFIRM_RAW_SIZE (1 + (2 * KC_TRUST_UID_BYTES) + KC_TRUST_XPSK1_MESSAGE_SIZE)

#define KC_TRUST_K_HANDSHAKE_SIZE (KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE)
#define KC_TRUST_TRANSPORT_MESSAGE_MAX 65535
#define KC_TRUST_TRANSPORT_PLAINTEXT_MAX (KC_TRUST_TRANSPORT_MESSAGE_MAX - KC_TRUST_MAC_SIZE)
#define KC_TRUST_LOGICAL_LENGTH_SIZE 8
#define KC_TRUST_LENGTH_RECORD_SIZE (KC_TRUST_LOGICAL_LENGTH_SIZE + KC_TRUST_MAC_SIZE)
#define KC_TRUST_PAYLOAD_BASE_SIZE (KC_TRUST_K_HANDSHAKE_SIZE + KC_TRUST_LENGTH_RECORD_SIZE)

static const unsigned char KC_TRUST_PENDING_MAGIC[4] = { 'K', 'T', 'P', '1' };
static const unsigned char KC_TRUST_PEER_MAGIC[4] = { 'K', 'T', 'R', '1' };
static const unsigned char KC_TRUST_LOCAL_MAGIC[4] = { 'K', 'T', 'L', '1' };

struct kc_trust {
    char dir[KC_TRUST_PATH_SIZE];
};

typedef struct {
    size_t size;
} kc_trust_alloc_header_t;

typedef struct {
    unsigned char k[32];
    uint64_t n;
    int has_key;
} kc_trust_cipher_state_t;

typedef struct {
    unsigned char ck[KC_TRUST_HASH_SIZE];
    unsigned char h[KC_TRUST_HASH_SIZE];
    kc_trust_cipher_state_t cipher;
} kc_trust_symmetric_state_t;

#ifdef _WIN32
static int kc_trust_read_random(unsigned char *buf, size_t size) {
    if (!buf || size > 0xFFFFFFFFU) return -1;
    return BCryptGenRandom(NULL, buf, (ULONG)size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
}
#elif defined(__EMSCRIPTEN__)
static int kc_trust_read_random(unsigned char *buf, size_t size) {
    size_t done = 0;
    if (!buf) return -1;
    while (done < size) {
        size_t part = size - done;
        if (part > 256U) part = 256U;
        if (getentropy(buf + done, part) != 0) return -1;
        done += part;
    }
    return 0;
}
#else
static int kc_trust_read_random(unsigned char *buf, size_t size) {
    FILE *f;
    size_t done = 0;
    if (!buf) return -1;
    f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    while (done < size) {
        size_t n = fread(buf + done, 1, size - done, f);
        if (n == 0) {
            fclose(f);
            return -1;
        }
        done += n;
    }
    return fclose(f) == 0 ? 0 : -1;
}
#endif

static void *kc_trust_alloc(size_t size) {
    kc_trust_alloc_header_t *header;
    if (size > SIZE_MAX - sizeof(*header)) return NULL;
    header = (kc_trust_alloc_header_t *)malloc(sizeof(*header) + (size ? size : 1));
    if (!header) return NULL;
    header->size = size ? size : 1;
    memset(header + 1, 0, header->size);
    return header + 1;
}

void kc_trust_free(void *ptr) {
    kc_trust_alloc_header_t *header;
    if (!ptr) return;
    header = ((kc_trust_alloc_header_t *)ptr) - 1;
    crypto_wipe(ptr, header->size);
    crypto_wipe(header, sizeof(*header));
    free(header);
}

static char *kc_trust_public_strdup(const char *value) {
    size_t size;
    char *copy;
    if (!value) return NULL;
    size = strlen(value) + 1;
    copy = (char *)kc_trust_alloc(size);
    if (!copy) return NULL;
    memcpy(copy, value, size);
    return copy;
}

static int kc_trust_path_join(char *out, size_t cap,
    const char *a, const char *b) {
#ifdef _WIN32
    return (size_t)snprintf(out, cap, "%s\\%s", a, b) < cap ? 0 : -1;
#else
    return (size_t)snprintf(out, cap, "%s/%s", a, b) < cap ? 0 : -1;
#endif
}

static int kc_trust_resolve_dir(char *out, size_t cap) {
    const char *override = getenv("KC_TRUST_DIR");
    if (!out || cap == 0) return -1;
    if (override && override[0])
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : -1;
#ifdef _WIN32
    {
        const char *base = getenv("LOCALAPPDATA");
        if (!base || !base[0]) base = getenv("APPDATA");
        if (!base || !base[0]) return -1;
        return (size_t)snprintf(out, cap, "%s\\kaisarcode\\trust.c", base) < cap ? 0 : -1;
    }
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *home = getenv("HOME");
        if (xdg && xdg[0])
            return (size_t)snprintf(out, cap, "%s/kaisarcode/trust.c", xdg) < cap ? 0 : -1;
        if (!home || !home[0]) return -1;
        return (size_t)snprintf(out, cap, "%s/.local/share/kaisarcode/trust.c", home) < cap ? 0 : -1;
    }
#endif
}

#ifdef _WIN32
static int kc_trust_dir_secure(const char *path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return 0;
    return 1;
}

static int kc_trust_mkdirs(const char *path) {
    char buf[KC_TRUST_PATH_SIZE];
    char *p;
    if (!path || (size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return -1;
    p = buf;
    if (buf[0] && buf[1] == ':') p = buf + 3;
    for (; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            if (buf[0] && !CreateDirectoryA(buf, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) return -1;
            *p = saved;
        }
    }
    if (!CreateDirectoryA(buf, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return -1;
    return kc_trust_dir_secure(path) ? 0 : -1;
}
#else
static int kc_trust_dir_secure(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return 0;
    if ((st.st_mode & 0022) != 0) return 0;
    return 1;
}

static int kc_trust_mkdirs(const char *path) {
    char buf[KC_TRUST_PATH_SIZE];
    char *p;
    struct stat st;
    if (!path || (size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return -1;
    for (p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0700) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(buf, 0700) != 0 && errno != EEXIST) return -1;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    if ((st.st_mode & 0022) != 0 && chmod(path, 0700) != 0) return -1;
    return kc_trust_dir_secure(path) ? 0 : -1;
}
#endif

static int kc_trust_store_dirs(kc_trust_t *trust) {
    char pending[KC_TRUST_PATH_SIZE];
    char peers[KC_TRUST_PATH_SIZE];
    char local[KC_TRUST_PATH_SIZE];
    if (!trust) return -1;
    if (kc_trust_mkdirs(trust->dir) != 0) return -1;
    if (kc_trust_path_join(pending, sizeof(pending), trust->dir, "pending") != 0)
        return -1;
    if (kc_trust_path_join(peers, sizeof(peers), trust->dir, "peers") != 0)
        return -1;
    if (kc_trust_path_join(local, sizeof(local), trust->dir, "local") != 0)
        return -1;
    if (kc_trust_mkdirs(pending) != 0 || kc_trust_mkdirs(peers) != 0 ||
        kc_trust_mkdirs(local) != 0)
        return -1;
    return 0;
}

static int kc_trust_uid_format(const unsigned char uid[KC_TRUST_UID_BYTES],
    char out[KC_TRUST_UID_SIZE + 1]) {
    static const char hex[] = "0123456789abcdef";
    static const int hyphen_after[] = { 4, 6, 8, 10 };
    size_t pos = 0;
    int h = 0;
    if (!uid || !out) return -1;
    for (int i = 0; i < KC_TRUST_UID_BYTES; i++) {
        out[pos++] = hex[uid[i] >> 4];
        out[pos++] = hex[uid[i] & 15];
        if (h < 4 && i + 1 == hyphen_after[h]) {
            out[pos++] = '-';
            h++;
        }
    }
    out[pos] = '\0';
    return pos == KC_TRUST_UID_SIZE ? 0 : -1;
}

static int kc_trust_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int kc_trust_uid_parse(const char *text,
    unsigned char uid[KC_TRUST_UID_BYTES]) {
    static const int hyphens[] = { 8, 13, 18, 23 };
    size_t i = 0;
    size_t out = 0;
    int h = 0;
    if (!text || strlen(text) != KC_TRUST_UID_SIZE || !uid) return -1;
    while (i < KC_TRUST_UID_SIZE) {
        int hi;
        int lo;
        if (h < 4 && (int)i == hyphens[h]) {
            if (text[i] != '-') return -1;
            i++;
            h++;
            continue;
        }
        if (i + 1 >= KC_TRUST_UID_SIZE || out >= KC_TRUST_UID_BYTES)
            return -1;
        hi = kc_trust_hex_value(text[i]);
        lo = kc_trust_hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        uid[out++] = (unsigned char)((hi << 4) | lo);
        i += 2;
    }
    return out == KC_TRUST_UID_BYTES ? 0 : -1;
}

static int kc_trust_uid_new(unsigned char uid[KC_TRUST_UID_BYTES],
    char text[KC_TRUST_UID_SIZE + 1]) {
    if (kc_trust_read_random(uid, KC_TRUST_UID_BYTES) != 0) return -1;
    uid[6] = (unsigned char)((uid[6] & 0x0fU) | 0x40U);
    uid[8] = (unsigned char)((uid[8] & 0x3fU) | 0x80U);
    return kc_trust_uid_format(uid, text);
}

static int kc_trust_record_path(const kc_trust_t *trust, const char *kind,
    const char *uid, char out[KC_TRUST_PATH_SIZE]) {
    unsigned char parsed[KC_TRUST_UID_BYTES];
    char canonical[KC_TRUST_UID_SIZE + 1];
    char dir[KC_TRUST_PATH_SIZE];
    if (!trust || !kind || !uid || !out) return -1;
    if (kc_trust_uid_parse(uid, parsed) != 0 ||
        kc_trust_uid_format(parsed, canonical) != 0) return -1;
    if (kc_trust_path_join(dir, sizeof(dir), trust->dir, kind) != 0)
        return -1;
    return kc_trust_path_join(out, KC_TRUST_PATH_SIZE, dir, canonical);
}

static int kc_trust_write_file(const char *path,
    const unsigned char *data, size_t size) {
#ifdef _WIN32
    HANDLE file;
    DWORD written = 0;
    if (!path || (!data && size)) return -1;
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return -1;
    if (size > 0xFFFFFFFFU ||
        !WriteFile(file, data, (DWORD)size, &written, NULL) ||
        written != (DWORD)size || !FlushFileBuffers(file)) {
        CloseHandle(file);
        return -1;
    }
    return CloseHandle(file) ? 0 : -1;
#else
    int fd;
    size_t done = 0;
    if (!path || (!data && size)) return -1;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        return -1;
    }
    while (done < size) {
        ssize_t n = write(fd, data + done, size - done);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return -1;
    }
    return close(fd) == 0 ? 0 : -1;
#endif
}

static int kc_trust_read_file(const char *path,
    unsigned char *data, size_t size) {
    FILE *f;
    size_t done;
    int extra;
    if (!path || !data) return -1;
    f = fopen(path, "rb");
    if (!f) return -1;
    done = fread(data, 1, size, f);
    extra = fgetc(f);
    if (fclose(f) != 0) return -1;
    return done == size && extra == EOF ? 0 : -1;
}

static int kc_trust_remove_file(const char *path) {
#ifdef _WIN32
    return DeleteFileA(path) ? 0 : -1;
#else
    return unlink(path) == 0 ? 0 : -1;
#endif
}

static int kc_trust_file_exists(const char *path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES &&
        !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static int kc_trust_pending_write(kc_trust_t *trust, const char *remote_uid,
    const unsigned char local_uid[KC_TRUST_UID_BYTES],
    const unsigned char sk[KC_TRUST_SK_SIZE],
    const unsigned char psk[KC_TRUST_PSK_SIZE]) {
    unsigned char record[KC_TRUST_PENDING_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc;
    if (kc_trust_record_path(trust, "pending", remote_uid, path) != 0)
        return -1;
    memcpy(record, KC_TRUST_PENDING_MAGIC, 4);
    memcpy(record + 4, local_uid, KC_TRUST_UID_BYTES);
    memcpy(record + 4 + KC_TRUST_UID_BYTES, sk, KC_TRUST_SK_SIZE);
    memcpy(record + 4 + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE,
        psk, KC_TRUST_PSK_SIZE);
    rc = kc_trust_write_file(path, record, sizeof(record));
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_pending_read(kc_trust_t *trust, const char *remote_uid,
    unsigned char local_uid[KC_TRUST_UID_BYTES],
    unsigned char sk[KC_TRUST_SK_SIZE],
    unsigned char psk[KC_TRUST_PSK_SIZE]) {
    unsigned char record[KC_TRUST_PENDING_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc = -1;
    if (kc_trust_record_path(trust, "pending", remote_uid, path) != 0)
        return -1;
    if (kc_trust_read_file(path, record, sizeof(record)) == 0 &&
        memcmp(record, KC_TRUST_PENDING_MAGIC, 4) == 0) {
        memcpy(local_uid, record + 4, KC_TRUST_UID_BYTES);
        memcpy(sk, record + 4 + KC_TRUST_UID_BYTES, KC_TRUST_SK_SIZE);
        memcpy(psk, record + 4 + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE,
            KC_TRUST_PSK_SIZE);
        rc = 0;
    }
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_pending_remove(kc_trust_t *trust, const char *uid) {
    char path[KC_TRUST_PATH_SIZE];
    if (kc_trust_record_path(trust, "pending", uid, path) != 0) return -1;
    return kc_trust_remove_file(path);
}

static int kc_trust_peer_write(kc_trust_t *trust, const char *remote_uid,
    const unsigned char local_uid[KC_TRUST_UID_BYTES],
    const unsigned char local_sk[KC_TRUST_SK_SIZE],
    const unsigned char remote_pk[KC_TRUST_PK_SIZE]) {
    unsigned char record[KC_TRUST_PEER_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc;
    if (kc_trust_record_path(trust, "peers", remote_uid, path) != 0)
        return -1;
    memcpy(record, KC_TRUST_PEER_MAGIC, 4);
    memcpy(record + 4, local_uid, KC_TRUST_UID_BYTES);
    memcpy(record + 4 + KC_TRUST_UID_BYTES, local_sk, KC_TRUST_SK_SIZE);
    memcpy(record + 4 + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE,
        remote_pk, KC_TRUST_PK_SIZE);
    rc = kc_trust_write_file(path, record, sizeof(record));
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_peer_read(kc_trust_t *trust, const char *remote_uid,
    unsigned char local_uid[KC_TRUST_UID_BYTES],
    unsigned char local_sk[KC_TRUST_SK_SIZE],
    unsigned char remote_pk[KC_TRUST_PK_SIZE]) {
    unsigned char record[KC_TRUST_PEER_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc = -1;
    if (kc_trust_record_path(trust, "peers", remote_uid, path) != 0)
        return -1;
    if (kc_trust_read_file(path, record, sizeof(record)) == 0 &&
        memcmp(record, KC_TRUST_PEER_MAGIC, 4) == 0) {
        memcpy(local_uid, record + 4, KC_TRUST_UID_BYTES);
        memcpy(local_sk, record + 4 + KC_TRUST_UID_BYTES, KC_TRUST_SK_SIZE);
        memcpy(remote_pk, record + 4 + KC_TRUST_UID_BYTES + KC_TRUST_SK_SIZE,
            KC_TRUST_PK_SIZE);
        rc = 0;
    }
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_peer_remove(kc_trust_t *trust, const char *uid) {
    char path[KC_TRUST_PATH_SIZE];
    if (kc_trust_record_path(trust, "peers", uid, path) != 0) return -1;
    return kc_trust_remove_file(path);
}

static int kc_trust_local_write(kc_trust_t *trust, const char *local_uid,
    const unsigned char remote_uid[KC_TRUST_UID_BYTES]) {
    unsigned char record[KC_TRUST_LOCAL_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc;
    if (kc_trust_record_path(trust, "local", local_uid, path) != 0)
        return -1;
    memcpy(record, KC_TRUST_LOCAL_MAGIC, 4);
    memcpy(record + 4, remote_uid, KC_TRUST_UID_BYTES);
    rc = kc_trust_write_file(path, record, sizeof(record));
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_local_read(kc_trust_t *trust, const char *local_uid,
    unsigned char remote_uid[KC_TRUST_UID_BYTES]) {
    unsigned char record[KC_TRUST_LOCAL_RECORD_SIZE];
    char path[KC_TRUST_PATH_SIZE];
    int rc = -1;
    if (kc_trust_record_path(trust, "local", local_uid, path) != 0)
        return -1;
    if (kc_trust_read_file(path, record, sizeof(record)) == 0 &&
        memcmp(record, KC_TRUST_LOCAL_MAGIC, 4) == 0) {
        memcpy(remote_uid, record + 4, KC_TRUST_UID_BYTES);
        rc = 0;
    }
    crypto_wipe(record, sizeof(record));
    return rc;
}

static int kc_trust_local_remove(kc_trust_t *trust, const char *uid) {
    char path[KC_TRUST_PATH_SIZE];
    if (kc_trust_record_path(trust, "local", uid, path) != 0) return -1;
    return kc_trust_remove_file(path);
}

static int kc_trust_base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static char *kc_trust_base64_encode(const unsigned char *data, size_t size) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_size;
    char *out;
    size_t i = 0;
    size_t p = 0;
    if (!data && size) return NULL;
    if (size > (SIZE_MAX - 2) / 3) return NULL;
    out_size = ((size + 2) / 3) * 4;
    out = (char *)kc_trust_alloc(out_size + 1);
    if (!out) return NULL;
    while (i + 3 <= size) {
        unsigned v = ((unsigned)data[i] << 16) |
            ((unsigned)data[i + 1] << 8) | data[i + 2];
        out[p++] = alphabet[(v >> 18) & 63];
        out[p++] = alphabet[(v >> 12) & 63];
        out[p++] = alphabet[(v >> 6) & 63];
        out[p++] = alphabet[v & 63];
        i += 3;
    }
    if (i < size) {
        unsigned v = (unsigned)data[i] << 16;
        out[p++] = alphabet[(v >> 18) & 63];
        if (i + 1 < size) {
            v |= (unsigned)data[i + 1] << 8;
            out[p++] = alphabet[(v >> 12) & 63];
            out[p++] = alphabet[(v >> 6) & 63];
            out[p++] = '=';
        } else {
            out[p++] = alphabet[(v >> 12) & 63];
            out[p++] = '=';
            out[p++] = '=';
        }
    }
    out[p] = '\0';
    return out;
}

static unsigned char *kc_trust_base64_decode(const char *text,
    size_t *out_size) {
    size_t len;
    size_t size;
    unsigned char *out;
    size_t p = 0;
    if (out_size) *out_size = 0;
    if (!text || !out_size) return NULL;
    len = strlen(text);
    if (len == 0 || (len % 4) != 0) return NULL;
    size = (len / 4) * 3;
    if (text[len - 1] == '=') size--;
    if (text[len - 2] == '=') size--;
    out = (unsigned char *)malloc(size ? size : 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i += 4) {
        int a = kc_trust_base64_value(text[i]);
        int b = kc_trust_base64_value(text[i + 1]);
        int c = text[i + 2] == '=' ? -2 : kc_trust_base64_value(text[i + 2]);
        int d = text[i + 3] == '=' ? -2 : kc_trust_base64_value(text[i + 3]);
        unsigned v;
        int last = i + 4 == len;
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (!last && (c == -2 || d == -2)) ||
            (c == -2 && d != -2)) {
            crypto_wipe(out, size ? size : 1);
            free(out);
            return NULL;
        }
        if (c == -2) c = 0;
        if (d == -2) d = 0;
        v = ((unsigned)a << 18) | ((unsigned)b << 12) |
            ((unsigned)c << 6) | (unsigned)d;
        if (p < size) out[p++] = (unsigned char)(v >> 16);
        if (p < size) out[p++] = (unsigned char)(v >> 8);
        if (p < size) out[p++] = (unsigned char)v;
    }
    *out_size = size;
    return out;
}

static void kc_trust_hmac_blake2b(unsigned char out[KC_TRUST_HASH_SIZE],
    const unsigned char *key, size_t key_size,
    const unsigned char *data, size_t data_size) {
    unsigned char kpad[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char outer[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char inner_pad[KC_TRUST_BLAKE2B_BLOCK];
    unsigned char inner[KC_TRUST_HASH_SIZE];
    crypto_blake2b_ctx hash;
    memset(kpad, 0, sizeof(kpad));
    memcpy(kpad, key, key_size);
    memcpy(outer, kpad, sizeof(outer));
    memcpy(inner_pad, kpad, sizeof(inner_pad));
    for (size_t i = 0; i < sizeof(kpad); i++) {
        outer[i] ^= 0x5c;
        inner_pad[i] ^= 0x36;
    }
    crypto_blake2b_init(&hash, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&hash, inner_pad, sizeof(inner_pad));
    if (data_size) crypto_blake2b_update(&hash, data, data_size);
    crypto_blake2b_final(&hash, inner);
    crypto_blake2b_init(&hash, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&hash, outer, sizeof(outer));
    crypto_blake2b_update(&hash, inner, sizeof(inner));
    crypto_blake2b_final(&hash, out);
    crypto_wipe(kpad, sizeof(kpad));
    crypto_wipe(outer, sizeof(outer));
    crypto_wipe(inner_pad, sizeof(inner_pad));
    crypto_wipe(inner, sizeof(inner));
}

static void kc_trust_hkdf2(unsigned char out1[KC_TRUST_HASH_SIZE],
    unsigned char out2[KC_TRUST_HASH_SIZE],
    const unsigned char ck[KC_TRUST_HASH_SIZE],
    const unsigned char *ikm, size_t ikm_size) {
    unsigned char temp[KC_TRUST_HASH_SIZE];
    unsigned char input[KC_TRUST_HASH_SIZE + 1];
    unsigned char one = 1;
    kc_trust_hmac_blake2b(temp, ck, KC_TRUST_HASH_SIZE, ikm, ikm_size);
    kc_trust_hmac_blake2b(out1, temp, KC_TRUST_HASH_SIZE, &one, 1);
    memcpy(input, out1, KC_TRUST_HASH_SIZE);
    input[KC_TRUST_HASH_SIZE] = 2;
    kc_trust_hmac_blake2b(out2, temp, KC_TRUST_HASH_SIZE,
        input, sizeof(input));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(input, sizeof(input));
}

static void kc_trust_hkdf3(unsigned char out1[KC_TRUST_HASH_SIZE],
    unsigned char out2[KC_TRUST_HASH_SIZE],
    unsigned char out3[KC_TRUST_HASH_SIZE],
    const unsigned char ck[KC_TRUST_HASH_SIZE],
    const unsigned char *ikm, size_t ikm_size) {
    unsigned char temp[KC_TRUST_HASH_SIZE];
    unsigned char input[KC_TRUST_HASH_SIZE + 1];
    unsigned char one = 1;
    kc_trust_hmac_blake2b(temp, ck, KC_TRUST_HASH_SIZE, ikm, ikm_size);
    kc_trust_hmac_blake2b(out1, temp, KC_TRUST_HASH_SIZE, &one, 1);
    memcpy(input, out1, KC_TRUST_HASH_SIZE);
    input[KC_TRUST_HASH_SIZE] = 2;
    kc_trust_hmac_blake2b(out2, temp, KC_TRUST_HASH_SIZE,
        input, sizeof(input));
    memcpy(input, out2, KC_TRUST_HASH_SIZE);
    input[KC_TRUST_HASH_SIZE] = 3;
    kc_trust_hmac_blake2b(out3, temp, KC_TRUST_HASH_SIZE,
        input, sizeof(input));
    crypto_wipe(temp, sizeof(temp));
    crypto_wipe(input, sizeof(input));
}

static void kc_trust_cipher_empty(kc_trust_cipher_state_t *cipher) {
    memset(cipher, 0, sizeof(*cipher));
}

static void kc_trust_cipher_key(kc_trust_cipher_state_t *cipher,
    const unsigned char key[32]) {
    memcpy(cipher->k, key, 32);
    cipher->n = 0;
    cipher->has_key = 1;
}

static int kc_trust_cipher_encrypt(kc_trust_cipher_state_t *cipher,
    const unsigned char *ad, size_t ad_size,
    const unsigned char *plain, size_t plain_size,
    unsigned char *out) {
    unsigned char nonce[KC_TRUST_NONCE_SIZE] = {0};
    crypto_aead_ctx aead;
    if (!cipher || !cipher->has_key || cipher->n == UINT64_MAX || !out)
        return -1;
    for (size_t i = 0; i < 8; i++)
        nonce[4 + i] = (unsigned char)(cipher->n >> (8 * i));
    crypto_aead_init_ietf(&aead, cipher->k, nonce);
    crypto_aead_write(&aead, out, out + plain_size, ad, ad_size,
        plain, plain_size);
    crypto_wipe(&aead, sizeof(aead));
    cipher->n++;
    return 0;
}

static int kc_trust_cipher_decrypt(kc_trust_cipher_state_t *cipher,
    const unsigned char *ad, size_t ad_size,
    const unsigned char *data, size_t data_size,
    unsigned char *out) {
    unsigned char nonce[KC_TRUST_NONCE_SIZE] = {0};
    crypto_aead_ctx aead;
    size_t plain_size;
    int rc;
    if (!cipher || !cipher->has_key || cipher->n == UINT64_MAX ||
        !data || data_size < KC_TRUST_MAC_SIZE || !out)
        return -1;
    plain_size = data_size - KC_TRUST_MAC_SIZE;
    for (size_t i = 0; i < 8; i++)
        nonce[4 + i] = (unsigned char)(cipher->n >> (8 * i));
    crypto_aead_init_ietf(&aead, cipher->k, nonce);
    rc = crypto_aead_read(&aead, out, data + plain_size,
        ad, ad_size, data, plain_size);
    crypto_wipe(&aead, sizeof(aead));
    if (rc != 0) return -1;
    cipher->n++;
    return 0;
}

static void kc_trust_mix_hash(kc_trust_symmetric_state_t *state,
    const unsigned char *data, size_t size) {
    unsigned char next[KC_TRUST_HASH_SIZE];
    crypto_blake2b_ctx hash;
    crypto_blake2b_init(&hash, KC_TRUST_HASH_SIZE);
    crypto_blake2b_update(&hash, state->h, KC_TRUST_HASH_SIZE);
    if (size) crypto_blake2b_update(&hash, data, size);
    crypto_blake2b_final(&hash, next);
    memcpy(state->h, next, sizeof(next));
    crypto_wipe(next, sizeof(next));
}

static void kc_trust_mix_key(kc_trust_symmetric_state_t *state,
    const unsigned char *input, size_t size) {
    unsigned char ck[KC_TRUST_HASH_SIZE];
    unsigned char key[KC_TRUST_HASH_SIZE];
    kc_trust_hkdf2(ck, key, state->ck, input, size);
    memcpy(state->ck, ck, sizeof(ck));
    kc_trust_cipher_key(&state->cipher, key);
    crypto_wipe(ck, sizeof(ck));
    crypto_wipe(key, sizeof(key));
}

static void kc_trust_mix_key_and_hash(kc_trust_symmetric_state_t *state,
    const unsigned char psk[KC_TRUST_PSK_SIZE]) {
    unsigned char ck[KC_TRUST_HASH_SIZE];
    unsigned char hash[KC_TRUST_HASH_SIZE];
    unsigned char key[KC_TRUST_HASH_SIZE];
    kc_trust_hkdf3(ck, hash, key, state->ck, psk, KC_TRUST_PSK_SIZE);
    memcpy(state->ck, ck, sizeof(ck));
    kc_trust_mix_hash(state, hash, sizeof(hash));
    kc_trust_cipher_key(&state->cipher, key);
    crypto_wipe(ck, sizeof(ck));
    crypto_wipe(hash, sizeof(hash));
    crypto_wipe(key, sizeof(key));
}

static int kc_trust_encrypt_and_hash(kc_trust_symmetric_state_t *state,
    const unsigned char *plain, size_t plain_size, unsigned char *out) {
    if (kc_trust_cipher_encrypt(&state->cipher, state->h,
        KC_TRUST_HASH_SIZE, plain, plain_size, out) != 0) return -1;
    kc_trust_mix_hash(state, out, plain_size + KC_TRUST_MAC_SIZE);
    return 0;
}

static int kc_trust_decrypt_and_hash(kc_trust_symmetric_state_t *state,
    const unsigned char *data, size_t data_size, unsigned char *out) {
    if (kc_trust_cipher_decrypt(&state->cipher, state->h,
        KC_TRUST_HASH_SIZE, data, data_size, out) != 0) return -1;
    kc_trust_mix_hash(state, data, data_size);
    return 0;
}

static void kc_trust_split(const kc_trust_symmetric_state_t *state,
    kc_trust_cipher_state_t *first, kc_trust_cipher_state_t *second) {
    unsigned char first_key[KC_TRUST_HASH_SIZE];
    unsigned char second_key[KC_TRUST_HASH_SIZE];
    kc_trust_hkdf2(first_key, second_key, state->ck, NULL, 0);
    kc_trust_cipher_key(first, first_key);
    kc_trust_cipher_key(second, second_key);
    crypto_wipe(first_key, sizeof(first_key));
    crypto_wipe(second_key, sizeof(second_key));
}

static void kc_trust_noise_init(kc_trust_symmetric_state_t *state,
    const char *name,
    const unsigned char initiator_uid[KC_TRUST_UID_BYTES],
    const unsigned char responder_uid[KC_TRUST_UID_BYTES]) {
    size_t name_size = strlen(name);
    memset(state, 0, sizeof(*state));
    if (name_size <= KC_TRUST_HASH_SIZE) {
        memcpy(state->h, name, name_size);
    } else {
        crypto_blake2b(state->h, KC_TRUST_HASH_SIZE,
            (const unsigned char *)name, name_size);
    }
    memcpy(state->ck, state->h, KC_TRUST_HASH_SIZE);
    kc_trust_cipher_empty(&state->cipher);
    kc_trust_mix_hash(state, initiator_uid, KC_TRUST_UID_BYTES);
    kc_trust_mix_hash(state, responder_uid, KC_TRUST_UID_BYTES);
}

static int kc_trust_x25519(unsigned char out[32],
    const unsigned char sk[32], const unsigned char pk[32]) {
    unsigned char zero[32] = {0};
    crypto_x25519(out, sk, pk);
    if (crypto_verify32(out, zero) == 0) {
        crypto_wipe(out, 32);
        return -1;
    }
    return 0;
}

static void kc_trust_store_u64(unsigned char out[8], uint64_t value) {
    for (size_t i = 0; i < 8; i++)
        out[7 - i] = (unsigned char)(value >> (8 * i));
}

static uint64_t kc_trust_load_u64(const unsigned char in[8]) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) value = (value << 8) | in[i];
    return value;
}

static size_t kc_trust_record_count(size_t size) {
    if (size == 0) return 0;
    return 1 + (size - 1) / KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
}

static size_t kc_trust_payload_size(size_t size) {
    return KC_TRUST_PAYLOAD_BASE_SIZE + size +
        kc_trust_record_count(size) * KC_TRUST_MAC_SIZE;
}

static int kc_trust_xpsk1_write(
    const unsigned char initiator_uid[KC_TRUST_UID_BYTES],
    const unsigned char responder_uid[KC_TRUST_UID_BYTES],
    const unsigned char local_sk[KC_TRUST_SK_SIZE],
    const unsigned char remote_pk[KC_TRUST_PK_SIZE],
    const unsigned char psk[KC_TRUST_PSK_SIZE],
    unsigned char out[KC_TRUST_XPSK1_MESSAGE_SIZE]) {
    static const char name[] = "Noise_Xpsk1_25519_ChaChaPoly_BLAKE2b";
    kc_trust_symmetric_state_t state;
    unsigned char local_pk[KC_TRUST_PK_SIZE];
    unsigned char eph_sk[KC_TRUST_SK_SIZE];
    unsigned char dh[32];
    unsigned char *p = out;
    int rc = -1;
    crypto_x25519_public_key(local_pk, local_sk);
    kc_trust_noise_init(&state, name, initiator_uid, responder_uid);
    kc_trust_mix_hash(&state, remote_pk, KC_TRUST_PK_SIZE);
    if (kc_trust_read_random(eph_sk, sizeof(eph_sk)) != 0) goto done;
    crypto_x25519_public_key(p, eph_sk);
    kc_trust_mix_hash(&state, p, KC_TRUST_PK_SIZE);
    kc_trust_mix_key(&state, p, KC_TRUST_PK_SIZE);
    if (kc_trust_x25519(dh, eph_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    p += KC_TRUST_PK_SIZE;
    if (kc_trust_encrypt_and_hash(&state, local_pk, KC_TRUST_PK_SIZE, p) != 0)
        goto done;
    p += KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE;
    if (kc_trust_x25519(dh, local_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    kc_trust_mix_key_and_hash(&state, psk);
    if (kc_trust_encrypt_and_hash(&state, NULL, 0, p) != 0) goto done;
    rc = 0;
done:
    crypto_wipe(local_pk, sizeof(local_pk));
    crypto_wipe(eph_sk, sizeof(eph_sk));
    crypto_wipe(dh, sizeof(dh));
    crypto_wipe(&state, sizeof(state));
    if (rc != 0) crypto_wipe(out, KC_TRUST_XPSK1_MESSAGE_SIZE);
    return rc;
}

static int kc_trust_xpsk1_read(
    const unsigned char initiator_uid[KC_TRUST_UID_BYTES],
    const unsigned char responder_uid[KC_TRUST_UID_BYTES],
    const unsigned char local_sk[KC_TRUST_SK_SIZE],
    const unsigned char psk[KC_TRUST_PSK_SIZE],
    const unsigned char message[KC_TRUST_XPSK1_MESSAGE_SIZE],
    unsigned char remote_pk[KC_TRUST_PK_SIZE]) {
    static const char name[] = "Noise_Xpsk1_25519_ChaChaPoly_BLAKE2b";
    kc_trust_symmetric_state_t state;
    unsigned char local_pk[KC_TRUST_PK_SIZE];
    unsigned char dh[32];
    unsigned char empty[1];
    const unsigned char *p = message;
    int rc = -1;
    crypto_x25519_public_key(local_pk, local_sk);
    kc_trust_noise_init(&state, name, initiator_uid, responder_uid);
    kc_trust_mix_hash(&state, local_pk, KC_TRUST_PK_SIZE);
    kc_trust_mix_hash(&state, p, KC_TRUST_PK_SIZE);
    kc_trust_mix_key(&state, p, KC_TRUST_PK_SIZE);
    if (kc_trust_x25519(dh, local_sk, p) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    p += KC_TRUST_PK_SIZE;
    if (kc_trust_decrypt_and_hash(&state, p,
        KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE, remote_pk) != 0) goto done;
    p += KC_TRUST_PK_SIZE + KC_TRUST_MAC_SIZE;
    if (kc_trust_x25519(dh, local_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    kc_trust_mix_key_and_hash(&state, psk);
    if (kc_trust_decrypt_and_hash(&state, p, KC_TRUST_MAC_SIZE, empty) != 0)
        goto done;
    rc = 0;
done:
    crypto_wipe(local_pk, sizeof(local_pk));
    crypto_wipe(dh, sizeof(dh));
    crypto_wipe(empty, sizeof(empty));
    crypto_wipe(&state, sizeof(state));
    if (rc != 0) crypto_wipe(remote_pk, KC_TRUST_PK_SIZE);
    return rc;
}

static int kc_trust_k_write(
    const unsigned char initiator_uid[KC_TRUST_UID_BYTES],
    const unsigned char responder_uid[KC_TRUST_UID_BYTES],
    const unsigned char local_sk[KC_TRUST_SK_SIZE],
    const unsigned char remote_pk[KC_TRUST_PK_SIZE],
    const unsigned char *message, size_t message_size,
    unsigned char *out) {
    static const char name[] = "Noise_K_25519_ChaChaPoly_BLAKE2b";
    kc_trust_symmetric_state_t state;
    kc_trust_cipher_state_t first;
    kc_trust_cipher_state_t second;
    unsigned char local_pk[KC_TRUST_PK_SIZE];
    unsigned char eph_sk[KC_TRUST_SK_SIZE];
    unsigned char dh[32];
    unsigned char logical[8];
    unsigned char *p = out;
    size_t offset = 0;
    int rc = -1;
    crypto_x25519_public_key(local_pk, local_sk);
    kc_trust_noise_init(&state, name, initiator_uid, responder_uid);
    kc_trust_mix_hash(&state, local_pk, KC_TRUST_PK_SIZE);
    kc_trust_mix_hash(&state, remote_pk, KC_TRUST_PK_SIZE);
    if (kc_trust_read_random(eph_sk, sizeof(eph_sk)) != 0) goto done;
    crypto_x25519_public_key(p, eph_sk);
    kc_trust_mix_hash(&state, p, KC_TRUST_PK_SIZE);
    if (kc_trust_x25519(dh, eph_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    if (kc_trust_x25519(dh, local_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    p += KC_TRUST_PK_SIZE;
    if (kc_trust_encrypt_and_hash(&state, NULL, 0, p) != 0) goto done;
    p += KC_TRUST_MAC_SIZE;
    kc_trust_split(&state, &first, &second);
    kc_trust_store_u64(logical, (uint64_t)message_size);
    if (kc_trust_cipher_encrypt(&first, NULL, 0, logical, sizeof(logical), p) != 0)
        goto done;
    p += KC_TRUST_LENGTH_RECORD_SIZE;
    while (offset < message_size) {
        size_t part = message_size - offset;
        if (part > KC_TRUST_TRANSPORT_PLAINTEXT_MAX)
            part = KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
        if (kc_trust_cipher_encrypt(&first, NULL, 0, message + offset,
            part, p) != 0) goto done;
        p += part + KC_TRUST_MAC_SIZE;
        offset += part;
    }
    rc = 0;
done:
    crypto_wipe(local_pk, sizeof(local_pk));
    crypto_wipe(eph_sk, sizeof(eph_sk));
    crypto_wipe(dh, sizeof(dh));
    crypto_wipe(logical, sizeof(logical));
    crypto_wipe(&first, sizeof(first));
    crypto_wipe(&second, sizeof(second));
    crypto_wipe(&state, sizeof(state));
    return rc;
}

static int kc_trust_k_read(
    const unsigned char initiator_uid[KC_TRUST_UID_BYTES],
    const unsigned char responder_uid[KC_TRUST_UID_BYTES],
    const unsigned char local_sk[KC_TRUST_SK_SIZE],
    const unsigned char remote_pk[KC_TRUST_PK_SIZE],
    const unsigned char *data, size_t data_size,
    unsigned char **out_message, size_t *out_message_size) {
    static const char name[] = "Noise_K_25519_ChaChaPoly_BLAKE2b";
    kc_trust_symmetric_state_t state;
    kc_trust_cipher_state_t first;
    kc_trust_cipher_state_t second;
    unsigned char local_pk[KC_TRUST_PK_SIZE];
    unsigned char dh[32];
    unsigned char empty[1];
    unsigned char logical[8];
    unsigned char *message = NULL;
    const unsigned char *p = data;
    uint64_t logical_size;
    size_t message_size;
    size_t offset = 0;
    int rc = -1;
    if (data_size < KC_TRUST_PAYLOAD_BASE_SIZE) return -1;
    crypto_x25519_public_key(local_pk, local_sk);
    kc_trust_noise_init(&state, name, initiator_uid, responder_uid);
    kc_trust_mix_hash(&state, remote_pk, KC_TRUST_PK_SIZE);
    kc_trust_mix_hash(&state, local_pk, KC_TRUST_PK_SIZE);
    kc_trust_mix_hash(&state, p, KC_TRUST_PK_SIZE);
    if (kc_trust_x25519(dh, local_sk, p) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    if (kc_trust_x25519(dh, local_sk, remote_pk) != 0) goto done;
    kc_trust_mix_key(&state, dh, sizeof(dh));
    crypto_wipe(dh, sizeof(dh));
    p += KC_TRUST_PK_SIZE;
    if (kc_trust_decrypt_and_hash(&state, p, KC_TRUST_MAC_SIZE, empty) != 0)
        goto done;
    p += KC_TRUST_MAC_SIZE;
    kc_trust_split(&state, &first, &second);
    if (kc_trust_cipher_decrypt(&first, NULL, 0, p,
        KC_TRUST_LENGTH_RECORD_SIZE, logical) != 0) goto done;
    p += KC_TRUST_LENGTH_RECORD_SIZE;
    logical_size = kc_trust_load_u64(logical);
    if (logical_size > KC_TRUST_MAX_MESSAGE) goto done;
    message_size = (size_t)logical_size;
    if (data_size != kc_trust_payload_size(message_size)) goto done;
    message = (unsigned char *)kc_trust_alloc(message_size ? message_size : 1);
    if (!message) goto done;
    while (offset < message_size) {
        size_t part = message_size - offset;
        if (part > KC_TRUST_TRANSPORT_PLAINTEXT_MAX)
            part = KC_TRUST_TRANSPORT_PLAINTEXT_MAX;
        if (kc_trust_cipher_decrypt(&first, NULL, 0, p,
            part + KC_TRUST_MAC_SIZE, message + offset) != 0) goto done;
        p += part + KC_TRUST_MAC_SIZE;
        offset += part;
    }
    *out_message = message;
    *out_message_size = message_size;
    message = NULL;
    rc = 0;
done:
    kc_trust_free(message);
    crypto_wipe(local_pk, sizeof(local_pk));
    crypto_wipe(dh, sizeof(dh));
    crypto_wipe(empty, sizeof(empty));
    crypto_wipe(logical, sizeof(logical));
    crypto_wipe(&first, sizeof(first));
    crypto_wipe(&second, sizeof(second));
    crypto_wipe(&state, sizeof(state));
    return rc;
}

uint64_t kc_trust_version(void) {
    return (uint64_t)KC_TRUST_BUILD_VERSION;
}

int kc_trust_init(kc_trust_t **out) {
    kc_trust_t *trust;
    if (out) *out = NULL;
    if (!out) return KC_TRUST_ERROR;
    trust = (kc_trust_t *)calloc(1, sizeof(*trust));
    if (!trust) return KC_TRUST_ERROR;
    if (kc_trust_resolve_dir(trust->dir, sizeof(trust->dir)) != 0 ||
        kc_trust_store_dirs(trust) != 0) {
        crypto_wipe(trust, sizeof(*trust));
        free(trust);
        return KC_TRUST_ERROR;
    }
    *out = trust;
    return KC_TRUST_OK;
}

int kc_trust_invite(kc_trust_t *trust, char **out_uid, char **out_code) {
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char sk[KC_TRUST_SK_SIZE];
    unsigned char pk[KC_TRUST_PK_SIZE];
    unsigned char psk[KC_TRUST_PSK_SIZE];
    unsigned char raw[KC_TRUST_INVITE_RAW_SIZE];
    char remote_uid_text[KC_TRUST_UID_SIZE + 1];
    char local_uid_text[KC_TRUST_UID_SIZE + 1];
    char *uid_copy = NULL;
    char *code = NULL;
    int rc = KC_TRUST_ERROR;

    if (out_uid) *out_uid = NULL;
    if (out_code) *out_code = NULL;
    if (!trust || !out_uid || !out_code) return KC_TRUST_ERROR;

    if (kc_trust_uid_new(remote_uid, remote_uid_text) != 0 ||
        kc_trust_uid_new(local_uid, local_uid_text) != 0 ||
        kc_trust_read_random(sk, sizeof(sk)) != 0 ||
        kc_trust_read_random(psk, sizeof(psk)) != 0)
        goto done;

    crypto_x25519_public_key(pk, sk);

    raw[0] = KC_TRUST_INVITE_VERSION;
    memcpy(raw + 1, remote_uid, KC_TRUST_UID_BYTES);
    memcpy(raw + 1 + KC_TRUST_UID_BYTES, local_uid, KC_TRUST_UID_BYTES);
    memcpy(raw + 1 + (2 * KC_TRUST_UID_BYTES), pk, KC_TRUST_PK_SIZE);
    memcpy(raw + 1 + (2 * KC_TRUST_UID_BYTES) + KC_TRUST_PK_SIZE,
        psk, KC_TRUST_PSK_SIZE);

    if (kc_trust_pending_write(trust, remote_uid_text, local_uid, sk, psk) != 0)
        goto done;

    uid_copy = kc_trust_public_strdup(remote_uid_text);
    code = kc_trust_base64_encode(raw, sizeof(raw));
    if (!uid_copy || !code) {
        kc_trust_pending_remove(trust, remote_uid_text);
        goto done;
    }

    *out_uid = uid_copy;
    *out_code = code;
    uid_copy = NULL;
    code = NULL;
    rc = KC_TRUST_OK;

done:
    kc_trust_free(uid_copy);
    kc_trust_free(code);
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(sk, sizeof(sk));
    crypto_wipe(pk, sizeof(pk));
    crypto_wipe(psk, sizeof(psk));
    crypto_wipe(raw, sizeof(raw));
    return rc;
}

int kc_trust_join(kc_trust_t *trust, const char *code,
    char **out_uid, char **out_confirmation) {
    unsigned char *raw = NULL;
    size_t raw_size = 0;
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char remote_pk[KC_TRUST_PK_SIZE];
    unsigned char psk[KC_TRUST_PSK_SIZE];
    unsigned char local_sk[KC_TRUST_SK_SIZE];
    unsigned char confirmation[KC_TRUST_CONFIRM_RAW_SIZE];
    char local_uid_text[KC_TRUST_UID_SIZE + 1];
    char remote_uid_text[KC_TRUST_UID_SIZE + 1];
    char *uid_copy = NULL;
    char *confirmation_text = NULL;
    int peer_written = 0;
    int local_written = 0;
    int rc = KC_TRUST_ERROR;

    if (out_uid) *out_uid = NULL;
    if (out_confirmation) *out_confirmation = NULL;
    if (!trust || !code || !out_uid || !out_confirmation)
        return KC_TRUST_ERROR;

    raw = kc_trust_base64_decode(code, &raw_size);
    if (!raw || raw_size != KC_TRUST_INVITE_RAW_SIZE ||
        raw[0] != KC_TRUST_INVITE_VERSION)
        goto done;

    memcpy(local_uid, raw + 1, KC_TRUST_UID_BYTES);
    memcpy(remote_uid, raw + 1 + KC_TRUST_UID_BYTES, KC_TRUST_UID_BYTES);
    memcpy(remote_pk, raw + 1 + (2 * KC_TRUST_UID_BYTES), KC_TRUST_PK_SIZE);
    memcpy(psk, raw + 1 + (2 * KC_TRUST_UID_BYTES) + KC_TRUST_PK_SIZE,
        KC_TRUST_PSK_SIZE);

    if (kc_trust_uid_format(local_uid, local_uid_text) != 0 ||
        kc_trust_uid_format(remote_uid, remote_uid_text) != 0 ||
        kc_trust_read_random(local_sk, sizeof(local_sk)) != 0)
        goto done;

    confirmation[0] = KC_TRUST_INVITE_VERSION;
    memcpy(confirmation + 1, local_uid, KC_TRUST_UID_BYTES);
    memcpy(confirmation + 1 + KC_TRUST_UID_BYTES,
        remote_uid, KC_TRUST_UID_BYTES);

    if (kc_trust_xpsk1_write(local_uid, remote_uid, local_sk, remote_pk, psk,
        confirmation + 1 + (2 * KC_TRUST_UID_BYTES)) != 0)
        goto done;

    uid_copy = kc_trust_public_strdup(remote_uid_text);
    confirmation_text = kc_trust_base64_encode(confirmation,
        sizeof(confirmation));
    if (!uid_copy || !confirmation_text) goto done;

    if (kc_trust_peer_write(trust, remote_uid_text, local_uid,
        local_sk, remote_pk) != 0)
        goto done;
    peer_written = 1;

    if (kc_trust_local_write(trust, local_uid_text, remote_uid) != 0)
        goto done;
    local_written = 1;

    *out_uid = uid_copy;
    *out_confirmation = confirmation_text;
    uid_copy = NULL;
    confirmation_text = NULL;
    rc = KC_TRUST_OK;

done:
    if (rc != KC_TRUST_OK) {
        if (local_written) kc_trust_local_remove(trust, local_uid_text);
        if (peer_written) kc_trust_peer_remove(trust, remote_uid_text);
    }
    if (raw) {
        crypto_wipe(raw, raw_size ? raw_size : 1);
        free(raw);
    }
    kc_trust_free(uid_copy);
    kc_trust_free(confirmation_text);
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    crypto_wipe(psk, sizeof(psk));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(confirmation, sizeof(confirmation));
    return rc;
}

int kc_trust_confirm(kc_trust_t *trust, const char *confirmation,
    char **out_uid) {
    unsigned char *raw = NULL;
    size_t raw_size = 0;
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char pending_local_uid[KC_TRUST_UID_BYTES];
    unsigned char local_sk[KC_TRUST_SK_SIZE];
    unsigned char psk[KC_TRUST_PSK_SIZE];
    unsigned char remote_pk[KC_TRUST_PK_SIZE];
    char remote_uid_text[KC_TRUST_UID_SIZE + 1];
    char local_uid_text[KC_TRUST_UID_SIZE + 1];
    char *uid_copy = NULL;
    int peer_written = 0;
    int local_written = 0;
    int rc = KC_TRUST_ERROR;

    if (out_uid) *out_uid = NULL;
    if (!trust || !confirmation || !out_uid) return KC_TRUST_ERROR;

    raw = kc_trust_base64_decode(confirmation, &raw_size);
    if (!raw || raw_size != KC_TRUST_CONFIRM_RAW_SIZE ||
        raw[0] != KC_TRUST_INVITE_VERSION)
        goto done;

    memcpy(remote_uid, raw + 1, KC_TRUST_UID_BYTES);
    memcpy(local_uid, raw + 1 + KC_TRUST_UID_BYTES, KC_TRUST_UID_BYTES);

    if (kc_trust_uid_format(remote_uid, remote_uid_text) != 0 ||
        kc_trust_uid_format(local_uid, local_uid_text) != 0 ||
        kc_trust_pending_read(trust, remote_uid_text, pending_local_uid,
            local_sk, psk) != 0 ||
        memcmp(local_uid, pending_local_uid, KC_TRUST_UID_BYTES) != 0)
        goto done;

    if (kc_trust_xpsk1_read(remote_uid, local_uid, local_sk, psk,
        raw + 1 + (2 * KC_TRUST_UID_BYTES), remote_pk) != 0)
        goto done;

    uid_copy = kc_trust_public_strdup(remote_uid_text);
    if (!uid_copy) goto done;

    if (kc_trust_peer_write(trust, remote_uid_text, local_uid,
        local_sk, remote_pk) != 0)
        goto done;
    peer_written = 1;

    if (kc_trust_local_write(trust, local_uid_text, remote_uid) != 0)
        goto done;
    local_written = 1;

    if (kc_trust_pending_remove(trust, remote_uid_text) != 0)
        goto done;

    *out_uid = uid_copy;
    uid_copy = NULL;
    rc = KC_TRUST_OK;

done:
    if (rc != KC_TRUST_OK) {
        if (local_written) kc_trust_local_remove(trust, local_uid_text);
        if (peer_written) kc_trust_peer_remove(trust, remote_uid_text);
    }
    if (raw) {
        crypto_wipe(raw, raw_size ? raw_size : 1);
        free(raw);
    }
    kc_trust_free(uid_copy);
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(pending_local_uid, sizeof(pending_local_uid));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(psk, sizeof(psk));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    return rc;
}

int kc_trust_seal(kc_trust_t *trust, const char *uid,
    const void *message, size_t message_size,
    void **out_data, size_t *out_size) {
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char local_sk[KC_TRUST_SK_SIZE];
    unsigned char remote_pk[KC_TRUST_PK_SIZE];
    unsigned char *data = NULL;
    size_t data_size;
    int rc = KC_TRUST_ERROR;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!trust || !uid || !out_data || !out_size ||
        (message_size && !message) || message_size > KC_TRUST_MAX_MESSAGE)
        return KC_TRUST_ERROR;

    if (kc_trust_uid_parse(uid, remote_uid) != 0 ||
        kc_trust_peer_read(trust, uid, local_uid, local_sk, remote_pk) != 0)
        goto done;

    data_size = kc_trust_payload_size(message_size);
    data = (unsigned char *)kc_trust_alloc(data_size);
    if (!data) goto done;

    if (kc_trust_k_write(local_uid, remote_uid, local_sk, remote_pk,
        (const unsigned char *)message, message_size, data) != 0)
        goto done;

    *out_data = data;
    *out_size = data_size;
    data = NULL;
    rc = KC_TRUST_OK;

done:
    kc_trust_free(data);
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    return rc;
}

int kc_trust_unseal(kc_trust_t *trust, const char *uid,
    const void *data, size_t data_size,
    void **out_message, size_t *out_message_size) {
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char stored_local_uid[KC_TRUST_UID_BYTES];
    unsigned char local_sk[KC_TRUST_SK_SIZE];
    unsigned char remote_pk[KC_TRUST_PK_SIZE];
    char remote_uid_text[KC_TRUST_UID_SIZE + 1];
    unsigned char *message = NULL;
    size_t message_size = 0;
    int rc = KC_TRUST_ERROR;

    if (out_message) *out_message = NULL;
    if (out_message_size) *out_message_size = 0;
    if (!trust || !uid || !data || !out_message || !out_message_size)
        return KC_TRUST_ERROR;

    if (kc_trust_uid_parse(uid, local_uid) != 0 ||
        kc_trust_local_read(trust, uid, remote_uid) != 0 ||
        kc_trust_uid_format(remote_uid, remote_uid_text) != 0 ||
        kc_trust_peer_read(trust, remote_uid_text, stored_local_uid,
            local_sk, remote_pk) != 0 ||
        memcmp(local_uid, stored_local_uid, KC_TRUST_UID_BYTES) != 0)
        goto done;

    if (kc_trust_k_read(remote_uid, local_uid, local_sk, remote_pk,
        (const unsigned char *)data, data_size,
        &message, &message_size) != 0)
        goto done;

    *out_message = message;
    *out_message_size = message_size;
    message = NULL;
    rc = KC_TRUST_OK;

done:
    kc_trust_free(message);
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(stored_local_uid, sizeof(stored_local_uid));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    return rc;
}

int kc_trust_revoke(kc_trust_t *trust, const char *uid) {
    unsigned char remote_uid[KC_TRUST_UID_BYTES];
    unsigned char local_uid[KC_TRUST_UID_BYTES];
    unsigned char local_sk[KC_TRUST_SK_SIZE];
    unsigned char remote_pk[KC_TRUST_PK_SIZE];
    unsigned char psk[KC_TRUST_PSK_SIZE];
    char local_uid_text[KC_TRUST_UID_SIZE + 1];
    int removed = 0;

    if (!trust || !uid || kc_trust_uid_parse(uid, remote_uid) != 0)
        return KC_TRUST_ERROR;

    if (kc_trust_peer_read(trust, uid, local_uid, local_sk, remote_pk) == 0) {
        if (kc_trust_uid_format(local_uid, local_uid_text) == 0)
            kc_trust_local_remove(trust, local_uid_text);
        if (kc_trust_peer_remove(trust, uid) == 0) removed = 1;
    }

    if (kc_trust_pending_read(trust, uid, local_uid, local_sk, psk) == 0) {
        if (kc_trust_pending_remove(trust, uid) == 0) removed = 1;
    }

    crypto_wipe(remote_uid, sizeof(remote_uid));
    crypto_wipe(local_uid, sizeof(local_uid));
    crypto_wipe(local_sk, sizeof(local_sk));
    crypto_wipe(remote_pk, sizeof(remote_pk));
    crypto_wipe(psk, sizeof(psk));
    return removed ? KC_TRUST_OK : KC_TRUST_ERROR;
}

void kc_trust_close(kc_trust_t *trust) {
    if (!trust) return;
    crypto_wipe(trust, sizeof(*trust));
    free(trust);
}
