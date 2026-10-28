/**
 * packages.c - KaisarCode project installer.
 * Summary: Install and maintain verified artifacts for one project and native target.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU General Public License v3.0
 */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "packages.h"
#include "sha256.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <dirent.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_MAX_TOKENS 1048576
#define KC_MAX_FILES 128
#define KC_PATH_SIZE 1024

/**
 * Prefer the unified local distribution when configured, otherwise use the
 * optional explicit source or the standard published HTTPS distribution.
 * @return Package source URL or local distribution path.
 */
static const char *kc_source(void) {
    const char *source = getenv("KCDIST_DIR");
    if (!source || !*source) source = getenv("KCPKG_SOURCE");
    if (!source || !*source) source = "https://kaisarcode.com/dist";
    return source;
}

typedef struct {
    int start;
    int end;
    int next;
    char kind;
} kc_token_t;

static kc_token_t *kc_tokens;
static size_t kc_capacity;
static int kc_count;

/**
 * Reserve a new token, growing capacity for large distribution inventories.
 * @return Token index or -1 on allocation/size failure.
 */
static int kc_new_token(void) {
    int id;
    kc_token_t *grown;
    size_t next;
    if (kc_count >= KC_MAX_TOKENS) return -1;
    if ((size_t)kc_count == kc_capacity) {
        next = kc_capacity ? kc_capacity * 2U : 1024U;
        if (next > KC_MAX_TOKENS) next = KC_MAX_TOKENS;
        grown = realloc(kc_tokens, next * sizeof(*kc_tokens));
        if (!grown) return -1;
        kc_tokens = grown;
        kc_capacity = next;
    }
    id = kc_count++;
    return id;
}

/**
 * Release the JSON tokenizer after a manifest has been inspected.
 * @return None.
 */
static void kc_release_tokens(void) {
    free(kc_tokens);
    kc_tokens = NULL;
    kc_capacity = 0;
    kc_count = 0;
}

static char *kc_json;
static size_t kc_position;

/**
 * Skip insignificant JSON whitespace.
 * @return void.
 */
static void kc_space(void) {
    while (kc_json[kc_position] == ' ' || kc_json[kc_position] == '\n' ||
        kc_json[kc_position] == '\r' || kc_json[kc_position] == '\t') {
        kc_position++;
    }
}

/**
 * Parse a bounded JSON string token.
 * @return Token identifier or -1.
 */
static int kc_string(void) {
    int id;
    if (kc_json[kc_position] != '"') return -1;
    id = kc_new_token();
    if (id < 0) return -1;
    kc_position++;
    kc_tokens[id].start = (int)kc_position;
    kc_tokens[id].kind = 's';
    while (kc_json[kc_position] && kc_json[kc_position] != '"') {
        unsigned char current = (unsigned char)kc_json[kc_position];
        if (current < 0x20) return -1;
        if (current == '\\') {
            int j;
            kc_position++;
            current = (unsigned char)kc_json[kc_position];
            if (current == 'u') {
                if (strlen(kc_json + kc_position + 1) < 4) return -1;
                for (j = 1; j <= 4; j++) {
                    unsigned char hex = (unsigned char)kc_json[kc_position + j];
                    if (!((hex >= '0' && hex <= '9') ||
                        (hex >= 'a' && hex <= 'f') ||
                        (hex >= 'A' && hex <= 'F'))) return -1;
                }
                kc_position += 4;
            } else if (!strchr("\"\\/bfnrt", current)) {
                return -1;
            }
        }
        kc_position++;
    }
    if (kc_json[kc_position] != '"') return -1;
    kc_tokens[id].end = (int)kc_position++;
    kc_tokens[id].next = kc_count;
    return id;
}

/**
 * Parse a JSON value with exact object and array separators.
 * @param depth Recursion depth.
 * @return Token identifier or -1 on malformed input.
 */
static int kc_value(int depth) {
    int id;
    char kind;
    char end;
    if (depth > 64) return -1;
    kc_space();
    if (kc_json[kc_position] == '"') return kc_string();
    id = kc_new_token();
    if (id < 0) return -1;
    kc_tokens[id].start = (int)kc_position;
    kind = kc_json[kc_position];
    kc_tokens[id].kind = kind;
    if (kind == '{' || kind == '[') {
        end = kind == '{' ? '}' : ']';
        kc_position++;
        kc_space();
        if (kc_json[kc_position] != end) {
            for (;;) {
                if (kind == '{') {
                    kc_space();
                    if (kc_json[kc_position] != '"' || kc_string() < 0) return -1;
                    kc_space();
                    if (kc_json[kc_position++] != ':') return -1;
                }
                if (kc_value(depth + 1) < 0) return -1;
                kc_space();
                if (kc_json[kc_position] == end) break;
                if (kc_json[kc_position++] != ',') return -1;
                kc_space();
                if (kc_json[kc_position] == end) return -1;
            }
        }
        kc_position++;
    } else if (kind == 't' || kind == 'f' || kind == 'n') {
        const char *literal = kind == 't' ? "true" : kind == 'f' ? "false" : "null";
        size_t length = strlen(literal);
        if (strncmp(kc_json + kc_position, literal, length)) return -1;
        kc_position += length;
    } else if (kind == '-' || (kind >= '0' && kind <= '9')) {
        if (kc_json[kc_position] == '-') kc_position++;
        if (kc_json[kc_position] == '0') {
            kc_position++;
        } else {
            if (kc_json[kc_position] < '1' || kc_json[kc_position] > '9') return -1;
            while (kc_json[kc_position] >= '0' && kc_json[kc_position] <= '9') kc_position++;
        }
        if (kc_json[kc_position] == '.') {
            kc_position++;
            if (kc_json[kc_position] < '0' || kc_json[kc_position] > '9') return -1;
            while (kc_json[kc_position] >= '0' && kc_json[kc_position] <= '9') kc_position++;
        }
        if (kc_json[kc_position] == 'e' || kc_json[kc_position] == 'E') {
            kc_position++;
            if (kc_json[kc_position] == '+' || kc_json[kc_position] == '-') kc_position++;
            if (kc_json[kc_position] < '0' || kc_json[kc_position] > '9') return -1;
            while (kc_json[kc_position] >= '0' && kc_json[kc_position] <= '9') kc_position++;
        }
    } else {
        return -1;
    }
    kc_tokens[id].end = (int)kc_position;
    kc_tokens[id].next = kc_count;
    return id;
}

/**
 * Match a token against a member name.
 * @param id Token identifier.
 * @param key Member name.
 * @return Nonzero when equal.
 */
static int kc_equals(int id, const char *key) {
    size_t length = strlen(key);
    return id >= 0 && kc_tokens[id].end - kc_tokens[id].start == (int)length &&
        !strncmp(kc_json + kc_tokens[id].start, key, length);
}

/**
 * Find a named JSON object member.
 * @param id Object token.
 * @param key Field name.
 * @return Member value token or -1.
 */
static int kc_member(int id, const char *key) {
    int i;
    if (id < 0 || kc_tokens[id].kind != '{') return -1;
    i = id + 1;
    while (i < kc_tokens[id].next) {
        int value = i + 1;
        if (value >= kc_count) return -1;
        if (kc_equals(i, key)) return value;
        i = kc_tokens[value].next;
    }
    return -1;
}

/**
 * Extract a bounded JSON field.
 * @param id String token.
 * @param out Destination buffer.
 * @param capacity Destination capacity.
 * @return 0 on success or -1 on failure.
 */
static int kc_extract(int id, char *out, size_t capacity) {
    size_t length;
    if (id < 0 || kc_tokens[id].kind != 's') return -1;
    length = (size_t)(kc_tokens[id].end - kc_tokens[id].start);
    if (length >= capacity) return -1;
    memcpy(out, kc_json + kc_tokens[id].start, length);
    out[length] = '\0';
    if (strchr(out, '\\')) return -1;
    return 0;
}

/**
 * Load a bounded manifest into memory.
 * @param filename Local manifest path.
 * @return 0 on success or -1 on failure.
 */
static int kc_load(const char *filename) {
    FILE *input = fopen(filename, "rb");
    long length;
    if (!input) return -1;
    if (fseek(input, 0, SEEK_END) || (length = ftell(input)) < 0 ||
        length > 8000000 || fseek(input, 0, SEEK_SET)) {
        fclose(input);
        return -1;
    }
    kc_json = malloc((size_t)length + 1);
    if (!kc_json) {
        fclose(input);
        return -1;
    }
    if (fread(kc_json, 1, (size_t)length, input) != (size_t)length) {
        fclose(input);
        free(kc_json);
        kc_json = NULL;
        return -1;
    }
    fclose(input);
    kc_json[length] = '\0';
    kc_position = 0;
    kc_count = 0;
    if (kc_value(0) < 0) {
        free(kc_json);
        kc_json = NULL;
        return -1;
    }
    kc_space();
    if (kc_json[kc_position]) {
        free(kc_json);
        kc_json = NULL;
        return -1;
    }
    return 0;
}

/**
 * Check a relative artifact path for unsafe components.
 * @param path Candidate relative path.
 * @return Nonzero when safe.
 */
static int kc_safe_path(const char *path) {
    const char *cursor = path;
    if (!*path || *path == '/' || strchr(path, '\\')) return 0;
    while (*cursor) {
        const char *end = strchr(cursor, '/');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (!length || (length == 1 && cursor[0] == '.') ||
            (length == 2 && cursor[0] == '.' && cursor[1] == '.')) return 0;
        if (!end) break;
        cursor = end + 1;
    }
    return 1;
}

/**
 * Validate the published SHA-256 value.
 * @param hash Candidate lowercase hexadecimal hash.
 * @return Nonzero when valid.
 */
static int kc_valid_hash(const char *hash) {
    size_t i;
    if (strlen(hash) != 64) return 0;
    for (i = 0; i < 64; i++) {
        if (!((hash[i] >= '0' && hash[i] <= '9') ||
            (hash[i] >= 'a' && hash[i] <= 'f'))) return 0;
    }
    return 1;
}

typedef struct {
    char file[128];
    char path[512];
    char hash[65];
} kc_artifact_t;

/**
 * Ensure the given filename has no path components.
 * @param name Simple artifact filename.
 * @return 1 when safe, 0 otherwise.
 */
static int kc_safe_filename(const char *name) {
    size_t i;
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/**
 * Join two path components into a bounded buffer.
 * @param output Destination buffer.
 * @param capacity Destination size.
 * @param left First path component.
 * @param right Second path component.
 * @return 0 on success or 1 on overflow.
 */
static int kc_join(char *output, size_t capacity, const char *left, const char *right) {
    int count = snprintf(output, capacity, "%s/%s", left, right);
    return count < 0 || (size_t)count >= capacity;
}

/**
 * Create one directory if it does not already exist.
 * @param path Directory path.
 * @return 0 on success or 1 on failure.
 */
static int kc_mkdir_one(const char *path) {
#if defined(_WIN32)
    if (_mkdir(path) == 0) return 0;
#else
    if (mkdir(path, 0700) == 0) return 0;
#endif
    if (errno == EEXIST) {
        struct stat info;
#if defined(_WIN32)
        return stat(path, &info) == 0 && (info.st_mode & _S_IFDIR) != 0 ? 0 : 1;
#else
        return stat(path, &info) == 0 && S_ISDIR(info.st_mode) ? 0 : 1;
#endif
    }
    return 1;
}

/**
 * Create all parent directories of a local path.
 * @param path Directory path.
 * @return 0 on success or 1 on failure.
 */
static int kc_mkdirs(const char *path) {
    char current[KC_PATH_SIZE];
    size_t i, length = strlen(path);
    if (length >= sizeof(current) || !length) return 1;
    memcpy(current, path, length + 1);
    for (i = 1; i < length; i++) {
        if ((current[i] == '/' || current[i] == '\\') &&
            current[i - 1] != ':' && current[i - 1] != '/' &&
            current[i - 1] != '\\') {
            char saved = current[i];
            current[i] = '\0';
            if (kc_mkdir_one(current)) return 1;
            current[i] = saved;
        }
    }
    return kc_mkdir_one(current);
}

/**
 * Find the per-user root for installed kclib files.
 * @param output Destination path.
 * @param capacity Destination capacity.
 * @return 0 on success or 1 on failure.
 */
static int kc_install_home(char *output, size_t capacity) {
    const char *root = getenv("KCPKG_HOME");
    const char *suffix = "";
    int count;
    if (!root || !*root) {
#if defined(_WIN32)
        root = getenv("LOCALAPPDATA");
        suffix = "/KaisarCode/kclib";
#else
        root = getenv("XDG_DATA_HOME");
        if (root && *root) suffix = "/kaisarcode/kclib";
        else {
            root = getenv("HOME");
            suffix = "/.local/share/kaisarcode/kclib";
        }
#endif
    }
    if (!root || !*root) return 1;
    count = snprintf(output, capacity, "%s%s", root, suffix);
    return count < 0 || (size_t)count >= capacity;
}

/**
 * Copy one local source file into a new destination file.
 * @param source Input file.
 * @param destination Output file.
 * @return 0 on success or 1 on failure.
 */
static int kc_copy(const char *source, const char *destination) {
    FILE *input = fopen(source, "rb");
    FILE *output;
    unsigned char buffer[8192];
    size_t count;
    int failed = 0;
    if (!input) return 1;
    output = fopen(destination, "wb");
    if (!output) {
        fclose(input);
        return 1;
    }
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            failed = 1;
            break;
        }
    }
    if (ferror(input)) failed = 1;
    if (fclose(output)) failed = 1;
    fclose(input);
    if (failed) remove(destination);
    return failed;
}

/**
 * Download one file using curl without invoking a shell.
 * @param url HTTPS resource URL.
 * @param destination Local output filename.
 * @return 0 on success or 1 on failure.
 */
static int kc_download(const char *url, const char *destination) {
    const char *const args[] = {
        "curl", "--fail", "--location", "--silent",
        "--proto", "=https", "--proto-redir", "=https",
        "--output", destination, url, NULL
    };
#if defined(_WIN32)
    return _spawnvp(_P_WAIT, "curl.exe", args) == 0 ? 0 : 1;
#else
    pid_t child;
    int status;
    int error = posix_spawnp(&child, "curl", NULL, NULL, (char *const *)args, environ);
    if (error) return 1;
    if (waitpid(child, &status, 0) != child) return 1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

/**
 * Retrieve a file from either a local distribution or an HTTPS source.
 * @param source Distribution root.
 * @param path Relative artifact path.
 * @param destination Staged output file.
 * @return 0 on success or 1 on failure.
 */
static int kc_fetch(const char *source, const char *path, const char *destination) {
    char location[KC_PATH_SIZE];
    if (!kc_safe_path(path) || kc_join(location, sizeof(location), source, path)) return 1;
    if (!strncmp(source, "https://", 8)) return kc_download(location, destination);
    if (strstr(source, "://")) return 1;
    return kc_copy(location, destination);
}

/**
 * Collect current-target artifacts from one project's manifest.
 * @param manifest Local per-project manifest filename.
 * @param project Expected project directory such as libnetl.c.
 * @param platform Target operating system.
 * @param arch Target architecture.
 * @param list Output array of artifact metadata.
 * @param count Number of resulting artifacts.
 * @return 0 on success, 1 when target is absent, 2 on invalid inventory.
 */
static int kc_collect(const char *manifest, const char *project,
    const char *platform, const char *arch, kc_artifact_t list[KC_MAX_FILES],
    int *count) {
    int metadata, entries, index, error = 0;
    char declared_name[128];
    char expected_name[128];
    size_t name_length = strlen(project);
    if (kc_load(manifest)) {
        kc_release_tokens();
        return 2;
    }
    *count = 0;
    if (!name_length || name_length >= sizeof(expected_name)) {
        error = 2;
    } else {
        size_t stem = name_length;
        if (stem > 2 && !strcmp(project + stem - 2, ".c")) stem -= 2;
        memcpy(expected_name, project, stem);
        expected_name[stem] = '\0';
    }
    metadata = error ? -1 : kc_member(0, "metadata");
    if (!error && (metadata < 0 || kc_tokens[metadata].kind != '{' ||
        kc_extract(kc_member(metadata, "name"), declared_name,
            sizeof(declared_name)) || strcmp(declared_name, expected_name)))
        error = 2;
    entries = error ? -1 : kc_member(0, "artifacts");
    if (!error && (entries < 0 || kc_tokens[entries].kind != '['))
        error = 2;
    if (!error) {
        for (index = entries + 1; index < kc_tokens[entries].next;
            index = kc_tokens[index].next) {
            char entry_platform[64], entry_arch[64];
            char expected[512];
            kc_artifact_t *item;
            int i;
            if (kc_tokens[index].kind != '{' ||
                kc_extract(kc_member(index, "platform"), entry_platform, sizeof(entry_platform)) ||
                kc_extract(kc_member(index, "arch"), entry_arch, sizeof(entry_arch))) {
                error = 2;
                break;
            }
            if (strcmp(entry_platform, platform) || strcmp(entry_arch, arch)) continue;
            if (*count >= KC_MAX_FILES) {
                error = 2;
                break;
            }
            item = &list[*count];
            if (kc_extract(kc_member(index, "path"), item->path, sizeof(item->path)) ||
                kc_extract(kc_member(index, "file"), item->file, sizeof(item->file)) ||
                kc_extract(kc_member(index, "sha256"), item->hash, sizeof(item->hash)) ||
                !kc_safe_path(item->path) || !kc_safe_filename(item->file) ||
                !strcmp(item->file, "manifest.json") ||
                !kc_valid_hash(item->hash) ||
                snprintf(expected, sizeof(expected), "%s/%s/%s/%s", project,
                    platform, arch, item->file) >= (int)sizeof(expected) ||
                strcmp(expected, item->path)) {
                error = 2;
                break;
            }
            for (i = 0; i < *count; i++) {
                if (!strcmp(item->file, list[i].file)) {
                    error = 2;
                    break;
                }
            }
            if (error) break;
            (*count)++;
        }
    }
    free(kc_json);
    kc_json = NULL;
    kc_release_tokens();
    return error ? error : (*count == 0 ? 1 : 0);
}

/**
 * Read the installed project's manifest for a specific native target.
 * @param directory Installation directory.
 * @param project Project name (libNAME.c).
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param list Output file records.
 * @param count Output file count.
 * @return 0 if the installed manifest describes this directory.
 */
static int kc_installed_files(const char *directory, const char *project,
    const char *platform, const char *arch,
    kc_artifact_t list[KC_MAX_FILES], int *count) {
    char manifest[KC_PATH_SIZE];
    if (kc_join(manifest, sizeof(manifest), directory, "manifest.json"))
        return 1;
    return kc_collect(manifest, project, platform, arch, list, count) != 0;
}

/**
 * Confirm the manifest-listed files exist as regular files.
 * Unlisted files in the same directory do not affect package status.
 * @param directory Installed target directory.
 * @param list Manifest artifact entries.
 * @param count Artifact count.
 * @return 0 when all declared files exist.
 */
static int kc_owned_directory(const char *directory,
    const kc_artifact_t *list, int count) {
    char path[KC_PATH_SIZE];
    int i;
    struct stat folder;
#if defined(_WIN32)
    if (stat(directory, &folder) || (folder.st_mode & _S_IFDIR) == 0)
        return 1;
#else
    if (lstat(directory, &folder) || !S_ISDIR(folder.st_mode))
        return 1;
#endif
    for (i = 0; i < count; i++) {
        struct stat info;
        if (kc_join(path, sizeof(path), directory, list[i].file))
            return 1;
#if defined(_WIN32)
        if (stat(path, &info) || (info.st_mode & _S_IFREG) == 0)
            return 1;
#else
        if (lstat(path, &info) || !S_ISREG(info.st_mode))
            return 1;
#endif
    }
    return 0;
}

/**
 * Verify only the files listed by a package manifest.
 * @return 0 when every listed file matches its SHA-256 checksum.
 */
static int kc_verify_files(const char *directory,
    const kc_artifact_t *files, int count) {
    char path[KC_PATH_SIZE], digest[65];
    int i;
    if (kc_owned_directory(directory, files, count)) return 1;
    for (i = 0; i < count; i++) {
        if (kc_join(path, sizeof(path), directory, files[i].file) ||
            kc_sha256_file(path, digest) || strcmp(files[i].hash, digest))
            return 1;
    }
    return 0;
}

/**
 * Remove only a directory's manifest and the artifact files it names.
 * Leave unlisted files and directories intact.
 * @param directory Installed package directory.
 * @param project Expected library project.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @return 0 on success.
 */
static int kc_remove_owned(const char *directory, const char *project,
    const char *platform, const char *arch) {
    kc_artifact_t files[KC_MAX_FILES];
    int count, i;
    char path[KC_PATH_SIZE];
#if !defined(_WIN32)
    {
        struct stat dir;
        if (lstat(directory, &dir) || !S_ISDIR(dir.st_mode)) return 1;
    }
#endif
    if (kc_installed_files(directory, project, platform, arch, files, &count))
        return 1;
    for (i = 0; i < count; i++) {
        if (kc_join(path, sizeof(path), directory, files[i].file))
            return 1;
        if (remove(path) && errno != ENOENT) return 1;
    }
    if (kc_join(path, sizeof(path), directory, "manifest.json") ||
        remove(path)) return 1;
#if defined(_WIN32)
    if (_rmdir(directory) != 0 && errno != ENOTEMPTY && errno != EEXIST)
        return 1;
#else
    if (rmdir(directory) != 0 && errno != ENOTEMPTY && errno != EEXIST)
        return 1;
#endif
    return 0;
}

/**
 * Discard files whose names are declared by the incoming package.
 * Used only when recovering an old directory without a valid local manifest.
 * Leave other files and directories alone.
 * @return 0 when all matching regular files were removed.
 */
static int kc_remove_replaced(const char *directory,
    const kc_artifact_t *list, int count) {
    char path[KC_PATH_SIZE];
    int i;
    for (i = 0; i < count; i++) {
        if (kc_join(path, sizeof(path), directory, list[i].file))
            return 1;
#if !defined(_WIN32)
        {
            struct stat info;
            if (lstat(path, &info) == 0 && !S_ISREG(info.st_mode))
                return 1;
        }
#endif
        if (remove(path) && errno != ENOENT) return 1;
    }
    if (kc_join(path, sizeof(path), directory, "manifest.json"))
        return 1;
    if (remove(path) && errno != ENOENT) return 1;
    return 0;
}

/**
 * Move non-package entries from an old installation to its replacement.
 * An existing destination name is never overwritten.
 * @return 0 when the old directory can be removed.
 */
static int kc_preserve_extra(const char *old_dir, const char *new_dir) {
    char src[KC_PATH_SIZE], dst[KC_PATH_SIZE];
    int failed = 0;
#if defined(_WIN32)
    struct _finddata_t entry;
    intptr_t handle;
    char glob[KC_PATH_SIZE];
    if (kc_join(glob, sizeof(glob), old_dir, "*")) return 1;
    handle = _findfirst(glob, &entry);
    if (handle == -1) return 1;
    do {
        struct stat found;
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, ".."))
            continue;
        if (kc_join(src, sizeof(src), old_dir, entry.name) ||
            kc_join(dst, sizeof(dst), new_dir, entry.name) ||
            stat(dst, &found) == 0 || rename(src, dst))
            failed = 1;
    } while (_findnext(handle, &entry) == 0);
    _findclose(handle);
    if (_rmdir(old_dir) != 0) failed = 1;
#else
    DIR *stream = opendir(old_dir);
    struct dirent *entry;
    if (!stream) return 1;
    while ((entry = readdir(stream)) != NULL) {
        struct stat found;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (kc_join(src, sizeof(src), old_dir, entry->d_name) ||
            kc_join(dst, sizeof(dst), new_dir, entry->d_name) ||
            lstat(dst, &found) == 0 || errno != ENOENT ||
            rename(src, dst))
            failed = 1;
    }
    closedir(stream);
    if (rmdir(old_dir) != 0) failed = 1;
#endif
    return failed;
}

/**
 * Check whether all local artifacts and the manifest match the new release.
 * @param directory Installed target.
 * @param project Expected project name.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param new_manifest Downloaded manifest.
 * @param new_files Newly resolved files.
 * @param new_count Number of published files.
 * @return 1 when both contents and digests match.
 */
static int kc_is_current(const char *directory, const char *project,
    const char *platform, const char *arch, const char *new_manifest,
    const kc_artifact_t *new_files, int new_count) {
    kc_artifact_t old_files[KC_MAX_FILES];
    char path[KC_PATH_SIZE], digest[65], local_digest[65];
    int count, i, j, found;
    if (kc_installed_files(directory, project, platform, arch, old_files, &count) ||
        count != new_count || kc_verify_files(directory, old_files, count))
        return 0;
    if (kc_join(path, sizeof(path), directory, "manifest.json") ||
        kc_sha256_file(path, local_digest) ||
        kc_sha256_file(new_manifest, digest) || strcmp(local_digest, digest))
        return 0;
    for (i = 0; i < count; i++) {
        found = 0;
        for (j = 0; j < new_count; j++) {
            if (!strcmp(old_files[i].file, new_files[j].file) &&
                !strcmp(old_files[i].hash, new_files[j].hash)) {
                found = 1;
                break;
            }
        }
        if (!found || kc_join(path, sizeof(path), directory, old_files[i].file) ||
            kc_sha256_file(path, digest) || strcmp(old_files[i].hash, digest))
            return 0;
    }
    return 1;
}

/**
 * Remove partially staged files after a failed installation.
 * @param staging Stage directory.
 * @param items Known artifact file entries.
 * @param count Number of artifacts.
 * @return None.
 */
static void kc_cleanup(const char *staging, const kc_artifact_t *items, int count) {
    char file[KC_PATH_SIZE];
    int i;
    if (!kc_join(file, sizeof(file), staging, "manifest.json")) remove(file);
    for (i = 0; i < count; i++) {
        if (!kc_join(file, sizeof(file), staging, items[i].file)) remove(file);
    }
#if defined(_WIN32)
    _rmdir(staging);
#else
    rmdir(staging);
#endif
}

/**
 * Normalize a library name to a project directory name.
 * @param name Library identifier such as netl or libnetl.
 * @param output Destination buffer.
 * @param capacity Destination capacity.
 * @return 0 for valid names.
 */
static int kc_project_name(const char *name, char *output, size_t capacity) {
    int length;
    if (!*name || !kc_safe_filename(name) || strchr(name, '.')) return 1;
    length = !strncmp(name, "lib", 3) ?
        snprintf(output, capacity, "%s.c", name) :
        snprintf(output, capacity, "lib%s.c", name);
    return length < 0 || (size_t)length >= capacity;
}

/**
 * Resolve the installation directory of a library without modifying it.
 * @param project Name of the library project.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param directory Installed target path.
 * @return 0 for a valid bounded path.
 */
static int kc_target_path(const char *project, const char *platform,
    const char *arch, char directory[KC_PATH_SIZE]) {
    char root[KC_PATH_SIZE], intermediate[KC_PATH_SIZE];
    char target[KC_PATH_SIZE];
    if (kc_install_home(root, sizeof(root)) ||
        kc_join(intermediate, sizeof(intermediate), root, project) ||
        kc_join(target, sizeof(target), intermediate, platform) ||
        kc_join(directory, KC_PATH_SIZE, target, arch)) return 1;
    return 0;
}

/**
 * Create a temporary file for probing the published project manifest.
 * @return 0 on success.
 */
static int kc_info_temp(char output[KC_PATH_SIZE]);

/**
 * Resolve either a library (libNAME.c) or an application (NAME) without
 * hardcoding project names. An existing local directory takes precedence;
 * otherwise the two possible published paths are probed.
 * @return 0 on success.
 */
static int kc_resolve_project(const char *name, const char *source,
    const char *platform, const char *arch, char *project, size_t capacity) {
    char alternate[128], target[KC_PATH_SIZE], path[KC_PATH_SIZE];
    char temporary[KC_PATH_SIZE];
    struct stat info;
    int result = 1;
    if (kc_project_name(name, project, capacity) ||
        kc_target_path(project, platform, arch, target)) return 1;
    if (stat(target, &info) == 0) return 0;
    if (!kc_safe_filename(name) || strchr(name, '.') ||
        snprintf(alternate, sizeof(alternate), "%s", name) >=
            (int)sizeof(alternate)) return 1;
    if (strcmp(alternate, project)) {
        if (kc_target_path(alternate, platform, arch, target)) return 1;
        if (stat(target, &info) == 0) {
            if (snprintf(project, capacity, "%s", alternate) >=
                (int)capacity) return 1;
            return 0;
        }
    }
    if (kc_info_temp(temporary)) return 1;
    if (!kc_join(path, sizeof(path), project, "manifest.json") &&
        !kc_fetch(source, path, temporary)) result = 0;
    if (result && strcmp(alternate, project) &&
        !kc_join(path, sizeof(path), alternate, "manifest.json") &&
        !kc_fetch(source, path, temporary)) {
        if (snprintf(project, capacity, "%s", alternate) <
            (int)capacity) result = 0;
    }
    remove(temporary);
    return result;
}

/**
 * Create a fresh stage and apply an install or update.
 * @param name Library name.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param update Nonzero to replace an existing install.
 * @return 0 on success.
 */
static int kc_apply(const char *name, const char *platform,
    const char *arch, int update) {
    char project[128], root[KC_PATH_SIZE], staging[KC_PATH_SIZE];
    char target[KC_PATH_SIZE], parent[KC_PATH_SIZE], backup[KC_PATH_SIZE];
    char destination[KC_PATH_SIZE], artifact_path[KC_PATH_SIZE];
    char manifest[KC_PATH_SIZE], sha[65];
    const char *source = getenv("KCPKG_SOURCE");
    kc_artifact_t list[KC_MAX_FILES], old[KC_MAX_FILES];
    struct stat installed;
    int count = 0, old_count = 0, i, prepared = 0, error = 1;
    int existing = 0, local_manifest_valid = 0;
    unsigned long pid;
    source = kc_source();
    if (kc_resolve_project(name, source, platform, arch, project,
            sizeof(project)) ||
        kc_target_path(project, platform, arch, target)) {
        fprintf(stderr, "kc: cannot find project manifest: %s\n", name);
        return 1;
    }
    if (kc_install_home(root, sizeof(root)) || kc_mkdirs(root) ||
        kc_join(parent, sizeof(parent), root, project) ||
        kc_mkdirs(parent) ||
        kc_join(destination, sizeof(destination), parent, platform) ||
        kc_mkdirs(destination)) {
        fputs("kc: cannot prepare installation directory\n", stderr);
        return 1;
    }
#if defined(_WIN32)
    if (stat(target, &installed) == 0) {
        if ((installed.st_mode & _S_IFDIR) == 0) {
#else
    if (lstat(target, &installed) == 0) {
        if (!S_ISDIR(installed.st_mode)) {
#endif
            fprintf(stderr, "kc: package path is not a directory: %s\n", target);
            return 1;
        }
        existing = 1;
        local_manifest_valid = !kc_installed_files(target, project, platform,
            arch, old, &old_count);
        if (!update && local_manifest_valid &&
            !kc_verify_files(target, old, old_count)) {
            printf("Already installed: %s (%s/%s)\n", project, platform, arch);
            return 0;
        }
    } else if (errno != ENOENT) {
        fprintf(stderr, "kc: cannot inspect package directory: %s\n", target);
        return 1;
    } else if (update) {
        fprintf(stderr, "kc: not installed: %s\n", name);
        return 1;
    }
#if defined(_WIN32)
    pid = (unsigned long)_getpid();
#else
    pid = (unsigned long)getpid();
#endif
    for (i = 0; i < 100; i++) {
        int length = snprintf(staging, sizeof(staging), "%s/.kc-%lu-%d",
            destination, pid, i);
        if (length < 0 || (size_t)length >= sizeof(staging)) return 1;
#if defined(_WIN32)
        if (_mkdir(staging) == 0) break;
#else
        if (mkdir(staging, 0700) == 0) break;
#endif
        if (errno != EEXIST) return 1;
    }
    if (i == 100) return 1;
    prepared = 1;
    {
        char manifest_path[KC_PATH_SIZE];
        if (kc_join(manifest, sizeof(manifest), staging, "manifest.json") ||
            kc_join(manifest_path, sizeof(manifest_path), project,
                "manifest.json")) {
            fputs("kc: manifest path is too long\n", stderr);
            goto finish;
        }
        if (kc_fetch(source, manifest_path, manifest)) {
            fprintf(stderr, "kc: cannot fetch %s from %s\n",
                manifest_path, source);
            goto finish;
        }
    }
    {
        int result = kc_collect(manifest, project, platform, arch, list, &count);
        if (result == 2) {
            fprintf(stderr, "kc: invalid or unsupported manifest.json from %s\n",
                source);
            goto finish;
        }
        if (result != 0) {
            fprintf(stderr, "kc: no published files for %s on %s/%s in %s\n",
                name, platform, arch, source);
            goto finish;
        }
    }
    if (update && local_manifest_valid &&
        kc_is_current(target, project, platform, arch,
            manifest, list, count)) {
        printf("Up to date: %s (%s/%s)\n", project, platform, arch);
        error = 0;
        goto finish;
    }
    if (existing && !local_manifest_valid &&
        !kc_verify_files(target, list, count)) {
        char installed_manifest[KC_PATH_SIZE];
        if (kc_join(installed_manifest, sizeof(installed_manifest), target,
                "manifest.json") ||
            kc_copy(manifest, installed_manifest)) {
            fputs("kc: cannot save recovered package manifest\n", stderr);
            goto finish;
        }
        printf("Recovered: %s (%s/%s)\n", project, platform, arch);
        printf("Location: %s\n", target);
        error = 0;
        goto finish;
    }
    for (i = 0; i < count; i++) {
        if (kc_join(artifact_path, sizeof(artifact_path), staging, list[i].file) ||
            kc_fetch(source, list[i].path, artifact_path) ||
            kc_sha256_file(artifact_path, sha) || strcmp(sha, list[i].hash)) {
            fprintf(stderr, "kc: download or SHA-256 failure: %s\n", list[i].file);
            goto finish;
        }
#if !defined(_WIN32)
        if (!strchr(list[i].file, '.') && chmod(artifact_path, 0700) != 0) goto finish;
#endif
    }
    if (existing) {
        for (i = 0; i < 100; i++) {
            int length = snprintf(backup, sizeof(backup), "%s/.kc-old-%lu-%d",
                destination, pid, i);
            if (length < 0 || (size_t)length >= sizeof(backup)) goto finish;
            if (stat(backup, &installed) != 0) break;
        }
        if (i == 100 || rename(target, backup)) {
            fputs("kc: cannot stage old installation\n", stderr);
            goto finish;
        }
        if (rename(staging, target)) {
            if (rename(backup, target))
                fprintf(stderr, "kc: recovery needed: %s\n", backup);
            goto finish;
        }
        prepared = 0;
        if (local_manifest_valid) {
            if (kc_remove_owned(backup, project, platform, arch)) {
                fprintf(stderr, "kc: old files need cleanup: %s\n", backup);
            }
        } else if (kc_remove_replaced(backup, list, count)) {
            fprintf(stderr, "kc: old files need cleanup: %s\n", backup);
        }
        if (stat(backup, &installed) == 0 &&
            kc_preserve_extra(backup, target)) {
            fprintf(stderr, "kc: files remain in old directory: %s\n", backup);
        }
        printf("%s: %s (%s/%s)\n", update ? "Updated" : "Repaired",
            project, platform, arch);
    } else {
        if (rename(staging, target)) {
            fprintf(stderr, "kc: cannot finish installation: %s\n", target);
            goto finish;
        }
        prepared = 0;
        printf("Installed: %s (%s/%s)\n", project, platform, arch);
    }
    printf("Location: %s\n", target);
    error = 0;
finish:
    if (prepared) kc_cleanup(staging, list, count);
    return error;
}

/**
 * Install the complete native-target distribution of a kclib.
 * @param name Library name.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @return 0 on success.
 */
int kc_packages_install(const char *name, const char *platform, const char *arch) {
    return kc_apply(name, platform, arch, 0);
}

/**
 * Replace one installed kclib after verifying new published files.
 * @param name Library name.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @return 0 on success.
 */
int kc_packages_update(const char *name, const char *platform, const char *arch) {
    return kc_apply(name, platform, arch, 1);
}

/**
 * Remove only files listed by the installed library manifest.
 * @param name Library name.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @return 0 on success.
 */
int kc_packages_remove(const char *name, const char *platform, const char *arch) {
    char project[128], target[KC_PATH_SIZE];
    if (kc_project_name(name, project, sizeof(project)) ||
        kc_target_path(project, platform, arch, target)) return 1;
    {
        struct stat existing;
        if (stat(target, &existing) != 0 &&
            kc_safe_filename(name) && !strchr(name, '.') &&
            strcmp(name, project) &&
            snprintf(project, sizeof(project), "%s", name) <
                (int)sizeof(project) &&
            kc_target_path(project, platform, arch, target))
            return 1;
    }
    if (kc_remove_owned(target, project, platform, arch)) {
        fprintf(stderr, "kc: not installed or invalid package manifest: %s\n", target);
        return 1;
    }
    printf("Removed: %s (%s/%s)\n", project, platform, arch);
    return 0;
}

/**
 * Create an exclusive temporary file for downloading a project manifest.
 * @param output Path buffer for the temporary file.
 * @return 0 on success.
 */
static int kc_info_temp(char output[KC_PATH_SIZE]) {
    char root[KC_PATH_SIZE];
    if (kc_install_home(root, sizeof(root)) || kc_mkdirs(root)) return 1;
#if defined(_WIN32)
    return GetTempFileNameA(root, "kcp", 0, output) ? 0 : 1;
#else
    int fd, length;
    length = snprintf(output, KC_PATH_SIZE, "%s/.kc-info-XXXXXX", root);
    if (length < 0 || length >= KC_PATH_SIZE) return 1;
    fd = mkstemp(output);
    if (fd < 0) return 1;
    close(fd);
    return 0;
#endif
}

/**
 * Read and print the metadata fields of a single project manifest.
 * @param path Downloaded manifest.
 * @param project Expected project directory name.
 * @return 0 for a valid metadata record.
 */
static int kc_info_metadata(const char *path, const char *project) {
    static const char *fields[] = {
        "name", "description", "author", "license", "website", "repository"
    };
    static const char *labels[] = {
        "Name", "Description", "Author", "License", "Website", "Repository"
    };
    char values[6][KC_PATH_SIZE];
    char expected[128];
    int metadata, artifacts, i, error = 0;
    size_t length = strlen(project);
    if (!length || length >= sizeof(expected)) return 1;
    if (length > 2 && !strcmp(project + length - 2, ".c"))
        length -= 2;
    memcpy(expected, project, length);
    expected[length] = '\0';
    if (kc_load(path)) {
        kc_release_tokens();
        return 1;
    }
    metadata = kc_member(0, "metadata");
    artifacts = kc_member(0, "artifacts");
    if (metadata < 0 || kc_tokens[metadata].kind != '{' ||
        artifacts < 0 || kc_tokens[artifacts].kind != '[')
        error = 1;
    for (i = 0; i < 6 && !error; i++) {
        int value = kc_member(metadata, fields[i]);
        values[i][0] = '\0';
        if (value < 0) {
            if (i == 0) error = 1;
            continue;
        }
        if (kc_extract(value, values[i], sizeof(values[i]))) error = 1;
    }
    if (!error && strcmp(values[0], expected)) error = 1;
    if (!error) {
        for (i = 0; i < 6; i++) {
            if (values[i][0]) printf("%s: %s\n", labels[i], values[i]);
        }
    }
    free(kc_json);
    kc_json = NULL;
    kc_release_tokens();
    return error;
}

/**
 * Show published package metadata and its local installation status.
 * @param name Library name, with or without the lib prefix.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @return 0 on success.
 */
int kc_packages_info(const char *name, const char *platform, const char *arch) {
    char project[128], target[KC_PATH_SIZE], manifest[KC_PATH_SIZE];
    char manifest_path[KC_PATH_SIZE], local_manifest[KC_PATH_SIZE];
    const char *source = getenv("KCPKG_SOURCE");
    kc_artifact_t installed_files[KC_MAX_FILES];
    struct stat info;
    int exists, count = 0, fetched = 0, result;
    source = kc_source();
    if (kc_resolve_project(name, source, platform, arch, project,
            sizeof(project)) ||
        kc_target_path(project, platform, arch, target) ||
        kc_join(manifest_path, sizeof(manifest_path), project, "manifest.json") ||
        kc_join(local_manifest, sizeof(local_manifest), target, "manifest.json")) {
        fprintf(stderr, "kc: invalid project name: %s\n", name);
        return 1;
    }
    exists = stat(target, &info) == 0;
    source = kc_source();
    if (exists && stat(local_manifest, &info) == 0) {
        result = kc_info_metadata(local_manifest, project);
    } else {
        if (kc_info_temp(manifest)) {
            fputs("kc: cannot create temporary manifest file\n", stderr);
            return 1;
        }
        fetched = 1;
        if (kc_fetch(source, manifest_path, manifest)) {
            fprintf(stderr, "kc: cannot fetch %s from %s\n",
                manifest_path, source);
            remove(manifest);
            return 1;
        }
        result = kc_info_metadata(manifest, project);
    }
    if (result) {
        if (fetched) remove(manifest);
        fprintf(stderr, "kc: invalid package metadata: %s\n", name);
        return 1;
    }
    if (!exists) {
        if (fetched) remove(manifest);
        puts("Installed: no");
        return 0;
    }
    result = !kc_installed_files(target, project, platform, arch,
        installed_files, &count) &&
        !kc_verify_files(target, installed_files, count);
    if (!result && fetched &&
        kc_collect(manifest, project, platform, arch, installed_files,
            &count) == 0) {
        result = !kc_verify_files(target, installed_files, count);
    }
    if (fetched) remove(manifest);
    if (result) {
        puts("Installed: yes");
    } else {
        puts("Installed: incomplete");
        puts("Reason: Package manifest or declared files are missing or changed");
    }
    printf("Location: %s\n", target);
    return 0;
}

/**
 * Visit installed libraries in the native-target directory.
 * @param name Directory name such as libnetl.c.
 * @param root Per-user installation root.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param update Nonzero to update, zero to list.
 * @return Number visited, or -1 on error.
 */
static int kc_visit(const char *name, const char *root,
    const char *platform, const char *arch, int update) {
    char target[KC_PATH_SIZE], project[128];
    char path[KC_PATH_SIZE];
    struct stat info;
    kc_artifact_t files[KC_MAX_FILES];
    int count;
    size_t length = strlen(name);
    if (!length || length >= sizeof(project) || !kc_safe_filename(name) ||
        (strchr(name, '.') && (length < 6 || strncmp(name, "lib", 3) ||
            strcmp(name + length - 2, ".c")))) return 0;
    strcpy(project, name);
    if (kc_join(path, sizeof(path), root, project) ||
        kc_join(target, sizeof(target), path, platform) ||
        kc_join(path, sizeof(path), target, arch)) return -1;
    if (stat(path, &info) ||
        kc_installed_files(path, project, platform, arch, files, &count))
        return 0;
    if (length > 2 && !strcmp(project + length - 2, ".c"))
        project[length - 2] = '\0';
    if (update) return kc_packages_update(project, platform, arch) ? -1 : 1;
    if (kc_verify_files(path, files, count))
        printf("%s (incomplete)\n", project);
    else
        puts(project);
    return 1;
}

/**
 * List installed native-target kclibs, optionally updating them all.
 * @param platform Native platform.
 * @param arch Native architecture.
 * @param update Nonzero to update installed libraries.
 * @return 0 on success.
 */
int kc_packages_each(const char *platform, const char *arch, int update) {
    char root[KC_PATH_SIZE];
    int total = 0, failures = 0;
    if (kc_install_home(root, sizeof(root))) return 1;
#if defined(_WIN32)
    {
        struct _finddata_t entry;
        char glob[KC_PATH_SIZE];
        intptr_t handle;
        if (kc_join(glob, sizeof(glob), root, "*")) return 1;
        handle = _findfirst(glob, &entry);
        if (handle != -1) {
            do {
                int result = kc_visit(entry.name, root, platform, arch, update);
                if (result < 0) failures++;
                else total += result;
            } while (_findnext(handle, &entry) == 0);
            _findclose(handle);
        }
    }
#else
    {
        DIR *stream = opendir(root);
        struct dirent *entry;
        if (!stream) {
            if (errno == ENOENT) {
                puts("No installed projects");
                return 0;
            }
            return 1;
        }
        while ((entry = readdir(stream)) != NULL) {
            int result = kc_visit(entry->d_name, root, platform, arch, update);
            if (result < 0) failures++;
            else total += result;
        }
        closedir(stream);
    }
#endif
    if (total == 0 && failures == 0) puts("No installed projects");
    return failures ? 1 : 0;
}

/**
 * List projects advertised by the selected distribution root.
 * The published projects.txt contains only project directory names; package
 * metadata and file ownership remain in each project's own manifest.
 * @return 0 on success, 1 if the index is absent or invalid.
 */
int kc_packages_list_all(void) {
    const char *source = kc_source();
    char temporary[KC_PATH_SIZE];
    char line[256];
    FILE *input;
    int count = 0, failed = 0;
    if (kc_info_temp(temporary)) {
        fputs("kc: cannot create temporary project listing\n", stderr);
        return 1;
    }
    if (kc_fetch(source, "projects.txt", temporary)) {
        fprintf(stderr, "kc: cannot fetch projects.txt from %s\n", source);
        remove(temporary);
        return 1;
    }
    input = fopen(temporary, "rb");
    if (!input) {
        remove(temporary);
        return 1;
    }
    while (fgets(line, sizeof(line), input)) {
        size_t length = strlen(line);
        if (length && line[length - 1] == '\n')
            line[--length] = '\0';
        else if (!feof(input)) {
            failed = 1;
            break;
        }
        if (length && line[length - 1] == '\r')
            line[--length] = '\0';
        if (!length || length >= 128 || !kc_safe_filename(line)) {
            failed = 1;
            break;
        }
        /* Match the names printed by "kc list" for installed libraries. */
        if (length > 2 && !strcmp(line + length - 2, ".c")) {
            if (length < 6 || strncmp(line, "lib", 3)) {
                failed = 1;
                break;
            }
            line[length - 2] = '\0';
        } else if (strchr(line, '.')) {
            failed = 1;
            break;
        }
        puts(line);
        count++;
    }
    if (ferror(input)) failed = 1;
    if (fclose(input)) failed = 1;
    remove(temporary);
    if (failed) {
        fputs("kc: invalid projects.txt in distribution\n", stderr);
        return 1;
    }
    if (!count) puts("No published projects");
    return 0;
}
