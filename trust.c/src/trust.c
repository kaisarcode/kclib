/**
 * trust.c - Command line interface for the trust tool.
 * Summary: Thin CLI adapter over libtrust and CLI-owned trust persistence.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libtrust.h"
#include "monocypher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define KC_TRUST_CLI_MAX_KEY_PATH 1024
#define KC_TRUST_CLI_MAX_TRUST_DIR 1024
#define KC_TRUST_CLI_MAX_TRUST_PATH 1200
#define KC_TRUST_CLI_IDENTITY_SIZE (KC_TRUST_SK_SIZE + KC_TRUST_PK_SIZE)

/**
 * Converts bytes to a lowercase hex string.
 * @param bytes Source bytes.
 * @param len Number of bytes.
 * @param hex Destination hex string (must have room for len*2+1).
 * @return Nothing.
 */
static void kc_trust_bytes_to_hex(const unsigned char *bytes, size_t len,
    char *hex) {
    for (size_t i = 0; i < len; i++)
        sprintf(hex + i * 2, "%02x", bytes[i]);
    hex[len * 2] = '\0';
}

/**
 * Encodes bytes into standard Base64.
 * @param data Source bytes.
 * @param len Source length.
 * @return malloc'd NUL-terminated Base64 string, or NULL on failure.
 */
static char *kc_trust_base64_encode(const unsigned char *data, size_t len) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;
    size_t i = 0, j = 0;
    while (i + 3 <= len) {
        uint32_t value = ((uint32_t)data[i] << 16) |
            ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[j++] = alphabet[(value >> 18) & 0x3f];
        out[j++] = alphabet[(value >> 12) & 0x3f];
        out[j++] = alphabet[(value >> 6) & 0x3f];
        out[j++] = alphabet[value & 0x3f];
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t value = (uint32_t)data[i] << 16;
        out[j++] = alphabet[(value >> 18) & 0x3f];
        out[j++] = alphabet[(value >> 12) & 0x3f];
        out[j++] = '=';
        out[j++] = '=';
    } else if (i + 2 == len) {
        uint32_t value = ((uint32_t)data[i] << 16) |
            ((uint32_t)data[i + 1] << 8);
        out[j++] = alphabet[(value >> 18) & 0x3f];
        out[j++] = alphabet[(value >> 12) & 0x3f];
        out[j++] = alphabet[(value >> 6) & 0x3f];
        out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}

/**
 * Joins a directory and child name using the platform separator.
 * @param directory Parent directory.
 * @param child Child name.
 * @param path Destination buffer.
 * @param path_cap Destination buffer capacity.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_join_path(const char *directory, const char *child,
    char *path, size_t path_cap) {
#ifdef _WIN32
    const char separator = '\\';
#else
    const char separator = '/';
#endif
    int length = snprintf(path, path_cap, "%s%c%s", directory, separator,
        child);
    return length >= 0 && (size_t)length < path_cap ? 0 : -1;
}

/**
 * Prints a diagnostic to stderr.
 * @param fmt Format string.
 * @return Nothing.
 */
static void kc_trust_cli_diag(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/**
 * Reads a file into a buffer, reporting actual length.
 * @param path File path.
 * @param buf Destination buffer.
 * @param cap Buffer capacity.
 * @param out_len Actual bytes read.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_read_binary_file(const char *path,
    unsigned char *buf, size_t cap, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t done = 0;
    while (done < cap) {
        size_t n = fread(buf + done, 1, cap - done, f);
        done += n;
        if (n == 0) break;
    }
    if (ferror(f)) { fclose(f); return -1; }
    if (done == cap) {
        unsigned char extra;
        if (fread(&extra, 1, 1, f) == 1) done++;
        if (ferror(f)) { fclose(f); return -1; }
    }
    if (fclose(f) != 0) return -1;
    *out_len = done;
    return 0;
}

/**
 * Inspects a trust-owned directory.
 * @param path Directory path.
 * @param label Diagnostic name.
 * @return 1 for a safe directory, 0 when absent, -1 when unsafe.
 */
static int kc_trust_cli_directory_status(const char *path, const char *label) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return 0;
        kc_trust_cli_diag("trust: cannot inspect %s directory: %s",
            label, path);
        return -1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        kc_trust_cli_diag("trust: unsafe %s directory: %s", label, path);
        return -1;
    }
#else
    struct stat status;
    if (lstat(path, &status) != 0) {
        if (errno == ENOENT) return 0;
        kc_trust_cli_diag("trust: cannot inspect %s directory: %s",
            label, path);
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        kc_trust_cli_diag("trust: unsafe %s directory: %s", label, path);
        return -1;
    }
    if (status.st_mode & 0022) {
        kc_trust_cli_diag(
            "trust: %s directory is writable by group or others: %s",
            label, path);
        return -1;
    }
#endif
    return 1;
}

/**
 * Creates a missing trust-owned directory.
 * @param path Directory path.
 * @param label Diagnostic name.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_ensure_directory(const char *path, const char *label) {
    int status = kc_trust_cli_directory_status(path, label);
    if (status == 1) return 0;
    if (status < 0) return -1;
#ifdef _WIN32
    if (!CreateDirectoryA(path, NULL)) {
#else
    if (mkdir(path, 0700) != 0) {
#endif
        kc_trust_cli_diag("trust: cannot create %s directory: %s",
            label, path);
        return -1;
    }
    return kc_trust_cli_directory_status(path, label) == 1 ? 0 : -1;
}

/**
 * Resolves the CLI state directory with exact precedence.
 * POSIX: TRUST_STATE_DIR, then $XDG_DATA_HOME/trust, then
 * $HOME/.local/share/trust, otherwise failure.
 * Windows: TRUST_STATE_DIR, then %LOCALAPPDATA%\trust, then
 * %USERPROFILE%\.trust, otherwise failure.
 * @param state_path Destination state-directory path.
 * @param state_path_cap Destination capacity.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_state_path(char *state_path, size_t state_path_cap) {
    const char *value = getenv("TRUST_STATE_DIR");
    if (value && value[0] != '\0') {
        size_t len = strlen(value);
        if (len >= state_path_cap) return -1;
        memcpy(state_path, value, len + 1);
        return 0;
    }
#ifdef _WIN32
    value = getenv("LOCALAPPDATA");
    if (value && value[0] != '\0')
        return kc_trust_cli_join_path(value, "trust", state_path,
            state_path_cap) == 0 ? 0 : -1;
    value = getenv("USERPROFILE");
    if (value && value[0] != '\0')
        return kc_trust_cli_join_path(value, ".trust", state_path,
            state_path_cap) == 0 ? 0 : -1;
#else
    value = getenv("XDG_DATA_HOME");
    if (value && value[0] != '\0')
        return kc_trust_cli_join_path(value, "trust", state_path,
            state_path_cap) == 0 ? 0 : -1;
    value = getenv("HOME");
    if (value && value[0] != '\0')
        return kc_trust_cli_join_path(value, ".local/share/trust",
            state_path, state_path_cap) == 0 ? 0 : -1;
#endif
    return -1;
}

/**
 * Resolves the CLI identity file path.
 * Precedence: explicit key path, then TRUST_KEY, then <state>/id.
 * TRUST_KEY affects only the identity path, never the trust directory.
 * @param key_path Explicit key path (or NULL).
 * @param identity_path Destination identity-file path.
 * @param identity_path_cap Destination capacity.
 * @param create_state Non-zero creates a missing default state directory.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_resolve_identity_path(const char *key_path,
    char *identity_path, size_t identity_path_cap, int create_state) {
    const char *value = key_path;
    if (!value || value[0] == '\0') {
        value = getenv("TRUST_KEY");
        if (value && value[0] == '\0') value = NULL;
    }
    if (value) {
        size_t len = strlen(value);
        if (!identity_path_cap || len >= identity_path_cap) return -1;
        memcpy(identity_path, value, len + 1);
        return 0;
    }
    char state_path[KC_TRUST_CLI_MAX_TRUST_DIR];
    if (kc_trust_cli_state_path(state_path, sizeof(state_path)) != 0)
        return -1;
    int state_status = kc_trust_cli_directory_status(state_path, "state");
    if (state_status < 0) return -1;
    if (state_status == 0) {
        if (!create_state) return -1;
        if (kc_trust_cli_ensure_directory(state_path, "state") != 0)
            return -1;
    }
    return kc_trust_cli_join_path(state_path, "id", identity_path,
        identity_path_cap) == 0 ? 0 : -1;
}

/**
 * Loads the CLI identity and creates a library context from it.
 * Requires exactly KC_TRUST_CLI_IDENTITY_SIZE bytes; the context is created
 * from the first 32 secret bytes and the stored public half is not validated.
 * @param key_path Explicit key path (or NULL).
 * @param identity_path Destination resolved identity-file path.
 * @param identity_path_cap Destination capacity.
 * @param out Destination context pointer.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_load_identity(const char *key_path,
    char *identity_path, size_t identity_path_cap, kc_trust_t **out) {
    unsigned char identity[KC_TRUST_CLI_IDENTITY_SIZE];
    size_t len = 0;
    *out = NULL;
    if (kc_trust_cli_resolve_identity_path(key_path, identity_path,
        identity_path_cap, 0) != 0)
        return -1;
    if (kc_trust_cli_read_binary_file(identity_path, identity,
        sizeof(identity), &len) != 0 || len != sizeof(identity)) {
        crypto_wipe(identity, sizeof(identity));
        return -1;
    }
    int status = kc_trust_create(out, identity);
    crypto_wipe(identity, sizeof(identity));
    return status == KC_TRUST_OK ? 0 : -1;
}

/**
 * Resolves and optionally creates the configured trust directory.
 * @param trust_path Destination trust-directory path.
 * @param trust_path_cap Destination capacity.
 * @param create Non-zero creates missing directories.
 * @return 0 when available, 1 when absent, -1 on failure.
 */
static int kc_trust_cli_trust_dir(char *trust_path, size_t trust_path_cap,
    int create) {
    char state_path[KC_TRUST_CLI_MAX_KEY_PATH];
    if (kc_trust_cli_state_path(state_path, sizeof(state_path)) != 0) {
        kc_trust_cli_diag(
            "trust: state directory unavailable; set TRUST_STATE_DIR");
        return -1;
    }
    int state_status = kc_trust_cli_directory_status(state_path, "state");
    if (state_status < 0) return -1;
    if (state_status == 0) {
        if (!create) return 1;
        if (kc_trust_cli_ensure_directory(state_path, "state") != 0) return -1;
    }
    if (kc_trust_cli_join_path(state_path, "trust", trust_path,
        trust_path_cap) != 0) {
        kc_trust_cli_diag("trust: trust directory path is too long");
        return -1;
    }
    int trust_status = kc_trust_cli_directory_status(trust_path, "trust");
    if (trust_status < 0) return -1;
    if (trust_status == 0) {
        if (!create) return 1;
        if (kc_trust_cli_ensure_directory(trust_path, "trust") != 0) return -1;
    }
    return 0;
}

/**
 * Hashes a peer identifier using BLAKE2b-256 for safe file naming.
 * @param peer_id Peer identifier bytes.
 * @param peer_id_len Peer identifier length.
 * @param hex_out Destination hex string (65 bytes).
 * @return Nothing.
 */
static void kc_trust_cli_peer_id_hash(const unsigned char *peer_id,
    size_t peer_id_len, char *hex_out) {
    unsigned char hash[32];
    crypto_blake2b(hash, 32, peer_id, peer_id_len);
    kc_trust_bytes_to_hex(hash, 32, hex_out);
    crypto_wipe(hash, 32);
}

/**
 * Returns the file path for a trust binding.
 * @param trust_dir Trust directory.
 * @param peer_id_hex Hex-encoded peer identifier hash.
 * @param buf Destination buffer.
 * @param buf_cap Buffer capacity.
 * @return Pointer to buf on success, NULL on failure.
 */
static const char *kc_trust_cli_trust_path(const char *trust_dir,
    const char *peer_id_hex, char *buf, size_t buf_cap) {
    return kc_trust_cli_join_path(trust_dir, peer_id_hex, buf, buf_cap) == 0 ?
        buf : NULL;
}

/**
 * Validates one persisted trust filename.
 * @param name Directory entry name.
 * @return 1 for a lowercase 64-character hex hash, 0 otherwise.
 */
static int kc_trust_cli_trust_name_valid(const char *name) {
    size_t i;
    if (strlen(name) != 64) return 0;
    for (i = 0; i < 64; i++) {
        if (!((name[i] >= '0' && name[i] <= '9') ||
            (name[i] >= 'a' && name[i] <= 'f'))) return 0;
    }
    return 1;
}

/**
 * Validates one persisted trust directory entry.
 * @param trust_dir Trust directory.
 * @param name Directory entry name.
 * @return 1 for a valid exact binding file, 0 otherwise.
 */
static int kc_trust_cli_trust_entry_valid(const char *trust_dir,
    const char *name) {
    char path[KC_TRUST_CLI_MAX_TRUST_PATH];
    unsigned char peer_pk[KC_TRUST_PK_SIZE];
    size_t key_len = 0;
    int valid;
    if (!kc_trust_cli_trust_name_valid(name)) return 0;
    if (!kc_trust_cli_trust_path(trust_dir, name, path, sizeof(path)))
        return 0;
    valid = kc_trust_cli_read_binary_file(path, peer_pk, sizeof(peer_pk),
        &key_len) == 0 && key_len == KC_TRUST_PK_SIZE;
    crypto_wipe(peer_pk, sizeof(peer_pk));
    return valid;
}

/**
 * Writes a trust binding to disk (atomic).
 * @param peer_id Peer identifier bytes.
 * @param peer_id_len Peer identifier length.
 * @param peer_pk 32-byte public key.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_trust_write(const unsigned char *peer_id,
    size_t peer_id_len, const unsigned char peer_pk[KC_TRUST_PK_SIZE]) {
    char trust_dir[KC_TRUST_CLI_MAX_TRUST_DIR];
    char hex[65];
    char path[KC_TRUST_CLI_MAX_TRUST_PATH];
    char temporary[KC_TRUST_CLI_MAX_TRUST_PATH + 32];
    int path_len;
    if (kc_trust_cli_trust_dir(trust_dir, sizeof(trust_dir), 1) != 0)
        return -1;
    kc_trust_cli_peer_id_hash(peer_id, peer_id_len, hex);
    if (!kc_trust_cli_trust_path(trust_dir, hex, path, sizeof(path)))
        return -1;
#ifdef _WIN32
    {
        unsigned long pid = (unsigned long)GetCurrentProcessId();
        path_len = snprintf(temporary, sizeof(temporary), "%s.tmp.%lu",
            path, pid);
    }
#else
    path_len = snprintf(temporary, sizeof(temporary), "%s.tmp.%lu",
        path, (unsigned long)getpid());
#endif
    if (path_len < 0 || (size_t)path_len >= sizeof(temporary)) return -1;
#ifdef _WIN32
    {
        HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written = 0;
        int failed;
        if (file == INVALID_HANDLE_VALUE) return -1;
        failed = !WriteFile(file, peer_pk, KC_TRUST_PK_SIZE, &written,
            NULL) || written != KC_TRUST_PK_SIZE ||
            !FlushFileBuffers(file);
        if (!CloseHandle(file)) failed = 1;
        if (failed || !MoveFileExA(temporary, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary);
            return -1;
        }
    }
#else
    {
        int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
        FILE *f;
        size_t written;
        int failed;
        if (fd < 0) return -1;
        f = fdopen(fd, "wb");
        if (!f) { close(fd); remove(temporary); return -1; }
        written = fwrite(peer_pk, 1, KC_TRUST_PK_SIZE, f);
        failed = written != KC_TRUST_PK_SIZE;
        if (fclose(f) != 0) failed = 1;
        if (failed || rename(temporary, path) != 0) {
            remove(temporary);
            return -1;
        }
    }
#endif
    return 0;
}

/**
 * Removes a trust binding from disk.
 * @param peer_id Peer identifier bytes.
 * @param peer_id_len Peer identifier length.
 * @return 0 on success, -1 if not found or on failure.
 */
static int kc_trust_cli_trust_remove(const unsigned char *peer_id,
    size_t peer_id_len) {
    char trust_dir[KC_TRUST_CLI_MAX_TRUST_DIR];
    int directory_status;
    char hex[65];
    char path[KC_TRUST_CLI_MAX_TRUST_PATH];
    directory_status = kc_trust_cli_trust_dir(trust_dir,
        sizeof(trust_dir), 0);
    if (directory_status != 0)
        return directory_status < 0 ? -2 : -1;
    kc_trust_cli_peer_id_hash(peer_id, peer_id_len, hex);
    if (!kc_trust_cli_trust_path(trust_dir, hex, path, sizeof(path)))
        return -2;
    return remove(path) == 0 ? 0 : -1;
}

/**
 * Loads a trust binding from disk.
 * @param peer_id Peer identifier bytes.
 * @param peer_id_len Peer identifier length.
 * @param peer_pk Destination for the 32-byte public key.
 * @return 0 on success, -2 on state error, -1 otherwise.
 */
static int kc_trust_cli_trust_load(const unsigned char *peer_id,
    size_t peer_id_len, unsigned char peer_pk[KC_TRUST_PK_SIZE]) {
    char trust_dir[KC_TRUST_CLI_MAX_TRUST_DIR];
    int directory_status;
    char hex[65];
    char path[KC_TRUST_CLI_MAX_TRUST_PATH];
    size_t key_len = 0;
    directory_status = kc_trust_cli_trust_dir(trust_dir,
        sizeof(trust_dir), 0);
    if (directory_status != 0)
        return directory_status < 0 ? -2 : -1;
    kc_trust_cli_peer_id_hash(peer_id, peer_id_len, hex);
    if (!kc_trust_cli_trust_path(trust_dir, hex, path, sizeof(path)))
        return -2;
    if (kc_trust_cli_read_binary_file(path, peer_pk, KC_TRUST_PK_SIZE,
        &key_len) != 0) return -1;
    return key_len == KC_TRUST_PK_SIZE ? 0 : -1;
}

/**
 * Lists persisted trust binding hashes.
 * @param out Receives a malloc'd NULL-terminated array of hex strings.
 * @param count Receives the number of entries.
 * @return 0 on success, -1 on failure.
 */
static int kc_trust_cli_list_peers(char ***out, int *count) {
    char dir[KC_TRUST_CLI_MAX_TRUST_DIR];
    int capacity = 32;
    int n = 0;
    char **list;
    int directory_status;
    *out = NULL;
    *count = 0;
    directory_status = kc_trust_cli_trust_dir(dir, sizeof(dir), 0);
    if (directory_status != 0)
        return directory_status < 0 ? -1 : 0;
    list = (char **)malloc((size_t)capacity * sizeof(char *));
    if (!list) return -1;
#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        char pattern[KC_TRUST_CLI_MAX_TRUST_DIR + 3];
        HANDLE h;
        if (kc_trust_cli_join_path(dir, "*", pattern, sizeof(pattern)) != 0) {
            free(list);
            return -1;
        }
        h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (kc_trust_cli_trust_entry_valid(dir, fd.cFileName)) {
                    if (n >= capacity) {
                        capacity *= 2;
                        {
                            char **nl = (char **)realloc(list,
                                (size_t)capacity * sizeof(char *));
                            if (!nl) { break; }
                            list = nl;
                        }
                    }
                    list[n++] = strdup(fd.cFileName);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *d = opendir(dir);
        struct dirent *ent;
        if (d) {
            while ((ent = readdir(d)) != NULL) {
                if (kc_trust_cli_trust_entry_valid(dir, ent->d_name)) {
                    if (n >= capacity) {
                        capacity *= 2;
                        {
                            char **nl = (char **)realloc(list,
                                (size_t)capacity * sizeof(char *));
                            if (!nl) break;
                            list = nl;
                        }
                    }
                    list[n++] = strdup(ent->d_name);
                }
            }
            closedir(d);
        }
    }
#endif
    *out = list;
    *count = n;
    return 0;
}

/**
 * Writes a JSON error to stdout and returns 1.
 * @param error Stable error string.
 * @return 1 always.
 */
static int kc_trust_cli_error(const char *error) {
    fprintf(stdout, "{\"ok\":false,\"error\":\"%s\"}\n", error);
    fflush(stdout);
    return 1;
}

/**
 * Reads all of stdin into an allocated buffer.
 * @param out Destination pointer for allocated buffer.
 * @param out_len Destination for the length.
 * @param max_len Maximum accepted input length.
 * @param report_errors Non-zero writes diagnostics to stderr.
 * @return 0 on success, -1 on failure, -2 when input exceeds max_len.
 */
static int kc_trust_cli_read_stdin_binary(unsigned char **out,
    size_t *out_len, size_t max_len, int report_errors) {
    size_t cap = max_len < 4096 ? max_len : 4096;
    size_t len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap > 0 ? cap : 1);
    if (!buf) {
        if (report_errors) fprintf(stderr, "trust: failed to read stdin\n");
        return -1;
    }
    while (len < max_len) {
        if (len >= cap) {
            size_t new_cap = cap > max_len - cap ? max_len : cap * 2;
            unsigned char *nb = (unsigned char *)realloc(buf, new_cap);
            if (!nb) {
                if (report_errors)
                    fprintf(stderr, "trust: failed to read stdin\n");
                free(buf);
                return -1;
            }
            buf = nb;
            cap = new_cap;
        }
        size_t n = fread(buf + len, 1, cap - len, stdin);
        len += n;
        if (n == 0) {
            if (ferror(stdin)) {
                if (report_errors)
                    fprintf(stderr, "trust: failed to read stdin\n");
                free(buf);
                return -1;
            }
            break;
        }
    }
    if (len == max_len) {
        unsigned char extra;
        size_t n = fread(&extra, 1, 1, stdin);
        if (n == 1) {
            if (report_errors)
                fprintf(stderr, "trust: input too large (max %zu bytes)\n",
                    max_len);
            free(buf);
            return -2;
        }
        if (ferror(stdin)) {
            if (report_errors) fprintf(stderr, "trust: failed to read stdin\n");
            free(buf);
            return -1;
        }
    }
    *out = buf;
    *out_len = len;
    return 0;
}

/**
 * Reads a recipient or trust public key from a key file.
 * @param key_file Key file path.
 * @param peer_pk Destination for the 32-byte public key.
 * @return 0 on success, 1 on failure.
 */
static int kc_trust_cli_read_key_file(const char *key_file,
    unsigned char peer_pk[KC_TRUST_PK_SIZE]) {
    FILE *kf = fopen(key_file, "rb");
    if (!kf) {
        fprintf(stderr, "trust: cannot open %s\n", key_file);
        return 1;
    }
    unsigned char buf[KC_TRUST_CLI_IDENTITY_SIZE];
    size_t done = 0;
    while (done < sizeof(buf)) {
        size_t n = fread(buf + done, 1, sizeof(buf) - done, kf);
        done += n;
        if (n == 0) break;
    }
    if (done == sizeof(buf)) {
        unsigned char extra;
        if (fread(&extra, 1, 1, kf) == 1) done++;
    }
    int read_ok = !ferror(kf);
    if (fclose(kf) != 0) read_ok = 0;
    if (!read_ok || (done != KC_TRUST_PK_SIZE &&
        done != KC_TRUST_CLI_IDENTITY_SIZE)) {
        fprintf(stderr, "trust: key file must be 32 or 64 bytes\n");
        crypto_wipe(buf, sizeof(buf));
        return 1;
    }
    if (done == KC_TRUST_PK_SIZE) {
        memcpy(peer_pk, buf, KC_TRUST_PK_SIZE);
    } else {
        crypto_x25519_public_key(peer_pk, buf);
    }
    crypto_wipe(buf, sizeof(buf));
    return 0;
}

/**
 * Prints CLI usage to stdout.
 * @param name Program name.
 * @return Nothing.
 */
static void kc_print_help(const char *name) {
    printf("Usage: %s <command> [arguments] [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("    init                Generate a new identity keypair\n");
    printf("    pk                  Write local public key\n");
    printf("    seal <key_file>     Seal up to 64 MiB from stdin\n");
    printf("    open [peer_id]      Open protected stdin as JSON\n");
    printf("    trust <peer_id> <key> Trust a binding (peer_id max 256 bytes)\n");
    printf("    forget <peer_id>    Remove a binding (peer_id max 256 bytes)\n");
    printf("    peers               List persisted binding hashes\n");
    printf("\n");
    printf("Options:\n");
    printf("    --key <path>        Override the identity key path\n");
    printf("    -h, --help          Show this help\n");
    printf("    -v, --version       Show version\n");
}

/**
 * Prints the build version to stdout.
 * @return Nothing.
 */
static void kc_print_version(void) {
    printf("trust build %llu\n", (unsigned long long)kc_trust_version());
}

/**
 * Handles the init command: generates a new identity keypair.
 * @param key_path Optional key path override.
 * @return 0 on success, 1 on failure.
 */
static int cmd_init(const char *key_path) {
    char identity_path[KC_TRUST_CLI_MAX_KEY_PATH];
    if (kc_trust_cli_resolve_identity_path(key_path, identity_path,
        sizeof(identity_path), 1) != 0) {
        fprintf(stderr, "trust: failed to generate identity\n");
        return 1;
    }
    unsigned char secret_key[KC_TRUST_SK_SIZE];
    unsigned char public_key[KC_TRUST_PK_SIZE];
    if (kc_trust_generate(secret_key, public_key) != KC_TRUST_OK) {
        crypto_wipe(secret_key, sizeof(secret_key));
        crypto_wipe(public_key, sizeof(public_key));
        fprintf(stderr, "trust: failed to generate identity\n");
        return 1;
    }
    unsigned char identity[KC_TRUST_CLI_IDENTITY_SIZE];
    memcpy(identity, secret_key, KC_TRUST_SK_SIZE);
    memcpy(identity + KC_TRUST_SK_SIZE, public_key, KC_TRUST_PK_SIZE);
    crypto_wipe(secret_key, sizeof(secret_key));
    crypto_wipe(public_key, sizeof(public_key));
#ifdef _WIN32
    if (GetFileAttributesA(identity_path) != INVALID_FILE_ATTRIBUTES) {
        crypto_wipe(identity, sizeof(identity));
        fprintf(stderr, "trust: failed to generate identity\n");
        return 1;
    }
    {
        HANDLE file = CreateFileA(identity_path, GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written = 0;
        int failed = 0;
        if (file == INVALID_HANDLE_VALUE) {
            crypto_wipe(identity, sizeof(identity));
            fprintf(stderr, "trust: failed to generate identity\n");
            return 1;
        }
        failed = !WriteFile(file, identity, KC_TRUST_CLI_IDENTITY_SIZE, &written,
            NULL) || written != KC_TRUST_CLI_IDENTITY_SIZE ||
            !FlushFileBuffers(file);
        if (!CloseHandle(file)) failed = 1;
        crypto_wipe(identity, sizeof(identity));
        if (failed) {
            DeleteFileA(identity_path);
            fprintf(stderr, "trust: failed to generate identity\n");
            return 1;
        }
    }
#else
    {
        int fd = open(identity_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        FILE *f;
        size_t written;
        int failed;
        if (fd < 0) {
            crypto_wipe(identity, sizeof(identity));
            fprintf(stderr, "trust: failed to generate identity\n");
            return 1;
        }
        f = fdopen(fd, "wb");
        if (!f) {
            close(fd);
            remove(identity_path);
            crypto_wipe(identity, sizeof(identity));
            fprintf(stderr, "trust: failed to generate identity\n");
            return 1;
        }
        written = fwrite(identity, 1, sizeof(identity), f);
        failed = written != sizeof(identity);
        if (fclose(f) != 0) failed = 1;
        crypto_wipe(identity, sizeof(identity));
        if (failed) {
            remove(identity_path);
            fprintf(stderr, "trust: failed to generate identity\n");
            return 1;
        }
    }
#endif
    fprintf(stderr, "trust: identity generated\n");
    return 0;
}

/**
 * Writes the local public key as exactly 32 raw bytes.
 * @param key_path Optional key path override.
 * @return 0 on success, 1 on failure.
 */
static int cmd_pk(const char *key_path) {
    char identity_path[KC_TRUST_CLI_MAX_KEY_PATH];
    kc_trust_t *ctx = NULL;
    if (kc_trust_cli_load_identity(key_path, identity_path,
        sizeof(identity_path), &ctx) != 0) {
        fprintf(stderr, "trust: failed to load identity\n");
        return 1;
    }
    const unsigned char *pk = kc_trust_public_key(ctx);
    if (!pk) {
        kc_trust_close(ctx);
        fprintf(stderr, "trust: failed to extract public key\n");
        return 1;
    }
    unsigned char local_pk[KC_TRUST_PK_SIZE];
    memcpy(local_pk, pk, KC_TRUST_PK_SIZE);
    kc_trust_close(ctx);
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        crypto_wipe(local_pk, sizeof(local_pk));
        return 1;
    }
#endif
    size_t written = fwrite(local_pk, 1, KC_TRUST_PK_SIZE, stdout);
    crypto_wipe(local_pk, sizeof(local_pk));
    return written == KC_TRUST_PK_SIZE && fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Handles the seal command: seals stdin for a recipient.
 * @param key_path Optional key path override.
 * @param key_file Recipient key file path (or NULL).
 * @return 0 on success, 1 on failure.
 */
static int cmd_seal(const char *key_path, const char *key_file) {
    if (!key_file) {
        fprintf(stderr, "trust: seal requires a recipient key file\n");
        return 1;
    }
    unsigned char peer_pk[KC_TRUST_PK_SIZE];
    if (kc_trust_cli_read_key_file(key_file, peer_pk) != 0) return 1;
    unsigned char *input = NULL;
    size_t input_len = 0;
    if (kc_trust_cli_read_stdin_binary(&input, &input_len,
        KC_TRUST_MAX_MESSAGE, 1) != 0) return 1;
    char identity_path[KC_TRUST_CLI_MAX_KEY_PATH];
    kc_trust_t *ctx = NULL;
    if (kc_trust_cli_load_identity(key_path, identity_path,
        sizeof(identity_path), &ctx) != 0) {
        free(input);
        crypto_wipe(peer_pk, sizeof(peer_pk));
        fprintf(stderr, "trust: failed to load identity\n");
        return 1;
    }
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    int rc = kc_trust_seal(ctx, peer_pk, input, input_len, &payload,
        &payload_len);
    free(input);
    crypto_wipe(peer_pk, sizeof(peer_pk));
    kc_trust_close(ctx);
    if (rc != KC_TRUST_OK || !payload) {
        fprintf(stderr, "trust: seal failed\n");
        return 1;
    }
    size_t written = fwrite(payload, 1, payload_len, stdout);
    crypto_wipe(payload, payload_len);
    kc_trust_free(payload);
    return written == payload_len ? 0 : 1;
}

/**
 * Handles the open command: opens a sealed message from stdin.
 * @param key_path Optional key path override.
 * @param peer_id_str Optional peer identifier for trust evaluation.
 * @return 0 on success, 1 on failure.
 */
static int cmd_open(const char *key_path, const char *peer_id_str) {
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        return kc_trust_cli_error("invalid_input");
#endif
    unsigned char *input = NULL;
    size_t input_len = 0;
    int read_status = kc_trust_cli_read_stdin_binary(&input, &input_len,
        KC_TRUST_MAX_PAYLOAD, 0);
    if (read_status != 0)
        return kc_trust_cli_error(read_status == -2 ?
            "message_too_large" : "invalid_input");
    char identity_path[KC_TRUST_CLI_MAX_KEY_PATH];
    kc_trust_t *ctx = NULL;
    if (kc_trust_cli_load_identity(key_path, identity_path,
        sizeof(identity_path), &ctx) != 0) {
        free(input);
        return kc_trust_cli_error("identity_error");
    }
    int has_trust = 0;
    unsigned char loaded_pk[KC_TRUST_PK_SIZE];
    if (peer_id_str) {
        size_t peer_id_len = strlen(peer_id_str);
        int load_status = kc_trust_cli_trust_load(
            (const unsigned char *)peer_id_str, peer_id_len, loaded_pk);
        if (load_status == 0) {
            kc_trust_trust(ctx, (const unsigned char *)peer_id_str,
                peer_id_len, loaded_pk);
            has_trust = 1;
        } else if (load_status == -2) {
            kc_trust_close(ctx);
            free(input);
            return kc_trust_cli_error("state_error");
        }
    }
    crypto_wipe(loaded_pk, sizeof(loaded_pk));
    kc_trust_result_t *result_lib = NULL;
    int rc = kc_trust_open(ctx, input, input_len,
        peer_id_str ? (const unsigned char *)peer_id_str : NULL,
        peer_id_str ? strlen(peer_id_str) : 0, &result_lib);
    free(input);
    if (has_trust && peer_id_str) {
        kc_trust_forget(ctx, (const unsigned char *)peer_id_str,
            strlen(peer_id_str));
    }
    kc_trust_close(ctx);
    if (rc != KC_TRUST_OK || !result_lib) {
        kc_trust_result_free(result_lib);
        return kc_trust_cli_error("authentication_failed");
    }
    const char *status_str = result_lib->status == KC_TRUST_OK ? "ok" :
        result_lib->status == KC_TRUST_PEER_NEW ? "peer_new" :
        result_lib->status == KC_TRUST_PEER_CHANGED ? "peer_changed" : NULL;
    if (!status_str) {
        kc_trust_result_free(result_lib);
        return kc_trust_cli_error("authentication_failed");
    }
    char peer_hex[KC_TRUST_PK_SIZE * 2 + 1];
    kc_trust_bytes_to_hex(result_lib->peer_pk, KC_TRUST_PK_SIZE, peer_hex);
    char *msg_b64 = kc_trust_base64_encode(result_lib->message,
        result_lib->message_len);
    kc_trust_result_free(result_lib);
    if (!msg_b64) {
        return kc_trust_cli_error("authentication_failed");
    }
    printf("{\"ok\":true,\"status\":\"%s\",\"peer_pk\":\"%s\","
        "\"message\":\"%s\"}\n", status_str, peer_hex, msg_b64);
    free(msg_b64);
    int failed = fflush(stdout) != 0 || ferror(stdout);
    return failed ? 1 : 0;
}

/**
 * Handles the trust command: persists a peer binding.
 * @param key_path Optional key path override.
 * @param peer_id_str Peer identifier string.
 * @param key_file Peer key file path.
 * @return 0 on success, 1 on failure.
 */
static int cmd_trust(const char *key_path, const char *peer_id_str,
    const char *key_file) {
    (void)key_path;
    if (!peer_id_str || !key_file) {
        fprintf(stderr, "trust: trust requires <peer_id> <key_file>\n");
        return 1;
    }
    size_t peer_id_len = strlen(peer_id_str);
    if (peer_id_len == 0 || peer_id_len > KC_TRUST_MAX_PEER_ID) {
        fprintf(stderr, "trust: peer id must be 1 to %d bytes\n",
            KC_TRUST_MAX_PEER_ID);
        return 1;
    }
    unsigned char peer_pk[KC_TRUST_PK_SIZE];
    if (kc_trust_cli_read_key_file(key_file, peer_pk) != 0) return 1;
    if (kc_trust_cli_trust_write((const unsigned char *)peer_id_str,
        peer_id_len, peer_pk) != 0) {
        crypto_wipe(peer_pk, sizeof(peer_pk));
        fprintf(stderr, "trust: failed to write trust binding\n");
        return 1;
    }
    crypto_wipe(peer_pk, sizeof(peer_pk));
    fprintf(stderr, "trust: trusted %s\n", peer_id_str);
    return 0;
}

/**
 * Handles the forget command: removes a peer binding.
 * @param key_path Optional key path override.
 * @param peer_id_str Peer identifier string.
 * @return 0 on success, 1 on failure.
 */
static int cmd_forget(const char *key_path, const char *peer_id_str) {
    (void)key_path;
    if (!peer_id_str) {
        fprintf(stderr, "trust: forget requires <peer_id>\n");
        return 1;
    }
    size_t peer_id_len = strlen(peer_id_str);
    if (peer_id_len == 0 || peer_id_len > KC_TRUST_MAX_PEER_ID) {
        fprintf(stderr, "trust: peer id must be 1 to %d bytes\n",
            KC_TRUST_MAX_PEER_ID);
        return 1;
    }
    int remove_status = kc_trust_cli_trust_remove(
        (const unsigned char *)peer_id_str, peer_id_len);
    if (remove_status != 0) {
        fprintf(stderr, "trust: %s\n", remove_status == -2 ?
            "state_error" : "peer not found");
        return 1;
    }
    fprintf(stderr, "trust: forgot %s\n", peer_id_str);
    return 0;
}

/**
 * Handles the peers command: lists trusted peers from disk.
 * @param key_path Optional key path override.
 * @return 0 on success.
 */
static int cmd_peers(const char *key_path) {
    (void)key_path;
    char **list = NULL;
    int count = 0;
    int rc = kc_trust_cli_list_peers(&list, &count);
    if (rc != 0) {
        for (int i = 0; i < count; i++) free(list[i]);
        free(list);
        fprintf(stderr, "trust: failed to list peers\n");
        return 1;
    }
    for (int i = 0; i < count; i++) {
        if (list[i]) printf("%s\n", list[i]);
        free(list[i]);
    }
    free(list);
    return 0;
}

/**
 * CLI entry point. Parses arguments and dispatches to commands.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv) {
    const char *key_path = NULL;

    if (argc < 2) {
        kc_print_help(argv[0]);
        return 0;
    }

    int j = 1;
    while (j < argc) {
        if (strcmp(argv[j], "-h") == 0 || strcmp(argv[j], "--help") == 0) {
            kc_print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[j], "-v") == 0 || strcmp(argv[j], "--version") == 0) {
            kc_print_version();
            return 0;
        }
        if (strcmp(argv[j], "--key") == 0) {
            j++;
            if (j >= argc) {
                fprintf(stderr, "trust: missing value for --key\n");
                return 1;
            }
            key_path = argv[j];
            j++;
            continue;
        }
        break;
    }

    if (j >= argc) {
        kc_print_help(argv[0]);
        return 0;
    }

    const char *cmd = argv[j];
    int i = j + 1;

    {
        int k = i;
        while (k < argc) {
            if (strcmp(argv[k], "--key") == 0) {
                k++;
                if (k < argc) key_path = argv[k];
            }
            k++;
        }
    }

    int rc = 0;

    if (strcmp(cmd, "init") == 0) {
        rc = cmd_init(key_path);
    } else if (strcmp(cmd, "pk") == 0) {
        rc = cmd_pk(key_path);
    } else if (strcmp(cmd, "seal") == 0) {
        const char *key_file = NULL;
        if (i < argc && argv[i][0] != '-') key_file = argv[i];
        rc = cmd_seal(key_path, key_file);
    } else if (strcmp(cmd, "open") == 0) {
        const char *peer_id = NULL;
        if (i < argc && argv[i][0] != '-') peer_id = argv[i];
        rc = cmd_open(key_path, peer_id);
    } else if (strcmp(cmd, "trust") == 0) {
        const char *peer_id = NULL;
        const char *key_file = NULL;
        if (i < argc && argv[i][0] != '-') { peer_id = argv[i]; i++; }
        if (i < argc && argv[i][0] != '-') { key_file = argv[i]; i++; }
        rc = cmd_trust(key_path, peer_id, key_file);
    } else if (strcmp(cmd, "forget") == 0) {
        const char *peer_id = NULL;
        if (i < argc && argv[i][0] != '-') peer_id = argv[i];
        rc = cmd_forget(key_path, peer_id);
    } else if (strcmp(cmd, "peers") == 0) {
        rc = cmd_peers(key_path);
    } else {
        fprintf(stderr, "trust: unknown command '%s'\n", cmd);
        return 1;
    }

    return rc;
}
