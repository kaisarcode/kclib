/**
 * libinit.c - Persistent Startup Registration
 * Summary: Core implementation for the init library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libinit.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
#ifndef _WIN32
#  include <dirent.h>
#  include <pwd.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#define KC_INIT_BUF   4096
#define KC_INIT_PATH  512

typedef enum {
    KC_INIT_BACKEND_NONE = 0,
    KC_INIT_BACKEND_SYSTEMD,
    KC_INIT_BACKEND_RUNIT,
    KC_INIT_BACKEND_OPENRC,
    KC_INIT_BACKEND_SYSV
} kc_init_backend_t;

typedef void (*kc_init_row_handler_t)(
    const char *key,
    const char *user,
    const char *cmd,
    void *userdata
);

typedef struct kc_init {
    char dir[KC_INIT_PATH];
#ifndef _WIN32
    kc_init_backend_t backend;
#endif
} kc_init_t;

/**
 * Duplicates one string.
 * @param text Source string.
 * @return Allocated copy, or NULL on failure.
 */
static char *kc_init_strdup(const char *text) {
    char *copy;
    size_t size;

    if (!text) {
        return NULL;
    }

    size = strlen(text) + 1;
    copy = (char *)malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, size);
    return copy;
}

/**
 * Resolve the active per-user metadata directory.
 * KC_INIT_DIR is reserved for isolated tests. Normal callers follow the
 * platform user-data convention under kaisarcode/init.c.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @return Zero on success, nonzero on failure.
 */
static int kc_init_resolve_dir(char *out, size_t cap) {
    const char *override;

    if (!out || cap == 0U) return 1;
    override = getenv("KC_INIT_DIR");
    if (override && override[0]) {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }

#ifdef _WIN32
    {
        const char *base = getenv("LOCALAPPDATA");

        if (!base || !base[0]) base = getenv("APPDATA");
        if (!base || !base[0]) return 1;
        return (size_t)snprintf(
            out,
            cap,
            "%s\\kaisarcode\\init.c",
            base
        ) < cap ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *home = NULL;
        const char *sudo_user = getenv("SUDO_USER");

        if (xdg && xdg[0]) {
            return (size_t)snprintf(
                out,
                cap,
                "%s/kaisarcode/init.c",
                xdg
            ) < cap ? 0 : 1;
        }

        if (sudo_user && sudo_user[0]) {
            struct passwd *pw = getpwnam(sudo_user);
            if (pw && pw->pw_dir && pw->pw_dir[0]) home = pw->pw_dir;
        }
        if (!home) home = getenv("HOME");
        if (!home || !home[0]) return 1;

        return (size_t)snprintf(
            out,
            cap,
            "%s/.local/share/kaisarcode/init.c",
            home
        ) < cap ? 0 : 1;
    }
#endif
}

/**
 * Check whether one registration key is safe for backend paths and names.
 * @param key Registration key.
 * @return 1 when valid, otherwise 0.
 */
static int kc_init_key_valid(const char *key) {
    const unsigned char *p;

    if (!key || !key[0]) return 0;
    if (strcmp(key, ".") == 0 || strcmp(key, "..") == 0) return 0;

    for (p = (const unsigned char *)key; *p; p++) {
        if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

/**
 * Check whether one startup command fits the supported one-line contract.
 * @param cmd Startup command.
 * @return 1 when valid, otherwise 0.
 */
static int kc_init_cmd_valid(const char *cmd) {
    size_t size;

    if (!cmd || !cmd[0]) return 0;
    size = strlen(cmd);
    if (size >= KC_INIT_BUF) return 0;
    return strchr(cmd, '\n') == NULL && strchr(cmd, '\r') == NULL;
}

/**
 * Check whether a string ends with one suffix.
 * @param text Source string.
 * @param suffix Suffix string.
 * @return 1 when text ends with suffix, otherwise 0.
 */
static int kc_init_has_suffix(const char *text, const char *suffix) {
    size_t text_size;
    size_t suffix_size;

    if (!text || !suffix) return 0;
    text_size = strlen(text);
    suffix_size = strlen(suffix);
    if (suffix_size > text_size) return 0;
    return strcmp(text + text_size - suffix_size, suffix) == 0;
}

/**
 * Checks if the current process has administrative/root privileges.
 * @return 1 if admin/root, 0 otherwise.
 */
static int kc_init_is_admin(void) {
#ifdef _WIN32
    HKEY hk;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software", 0,
            KEY_READ | KEY_WRITE, &hk) == ERROR_SUCCESS) {
        RegCloseKey(hk);
        return 1;
    }
    return 0;
#else
    return geteuid() == 0;
#endif
}

/**
 * Writes the user name to a metadata file.
 * @param path File path.
 * @param user User name.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_write_user(const char *path, const char *user) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 1;
    fprintf(fp, "%s", user);
    fclose(fp);
#ifndef _WIN32
    chmod(path, 0644);
#endif
    return 0;
}

/**
 * Reads the user name from a metadata file.
 * @param path File path.
 * @param out  Output buffer.
 * @param cap  Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_read_user(const char *path, char *out, size_t cap) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 1;
    if (!fgets(out, (int)cap, fp)) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    size_t n = strlen(out);
    if (n > 0 && out[n - 1] == '\n') out[n - 1] = '\0';
    return 0;
}

/**
 * Creates a directory and all intermediate parents.
 * @param path Directory path.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_ensure_dir(const char *path) {
#ifdef _WIN32
    char buf[KC_INIT_PATH];
    char *p;
    if ((size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return 1;
    for (p = buf + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            if (!CreateDirectoryA(buf, NULL)
                    && GetLastError() != ERROR_ALREADY_EXISTS)
                return 1;
            *p = '\\';
        }
    }
    if (!CreateDirectoryA(buf, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS)
        return 1;
    return 0;
#else
    char buf[KC_INIT_PATH];
    char *p;
    if ((size_t)snprintf(buf, sizeof(buf), "%s", path) >= sizeof(buf))
        return 1;
    for (p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 1;
            chmod(buf, 0755);
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 1;
    chmod(buf, 0755);
    return 0;
#endif
}

/**
 * Detects the original user who invoked the tool.
 * @return User name string (caller must not free).
 */
static const char *kc_init_detect_user(void) {
    const char *u;
    u = getenv("SUDO_USER");
    if (u && u[0]) return u;
    u = getenv("LOGNAME");
    if (u && u[0] && strcmp(u, "root") != 0) return u;
    u = getenv("USER");
    if (u && u[0] && strcmp(u, "root") != 0) return u;
    return "root";
}

/**
 * Composes the metadata file path for a key.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param out  Output buffer.
 * @param cap  Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_meta_path(
    const char *dir, const char *key, char *out, size_t cap
) {
#ifdef _WIN32
    if ((size_t)snprintf(out, cap, "%s\\%s", dir, key) >= cap)
        return 1;
    return 0;
#else
    if ((size_t)snprintf(out, cap, "%s/%s", dir, key) >= cap)
        return 1;
    return 0;
#endif
}

/**
 * Check whether registration metadata exists in the active metadata directory.
 * @param init Startup entry handle.
 * @param key Registration key.
 * @return 1 when metadata exists, otherwise 0.
 */
static int kc_init_entry_exists(const kc_init_t *init, const char *key) {
    char path[KC_INIT_PATH];

    if (!init || !key) return 0;
    if (kc_init_meta_path(init->dir, key, path, sizeof(path)) != 0) return 0;
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    {
        struct stat st;
        return stat(path, &st) == 0;
    }
#endif
}

/**
 * Writes the command string to a metadata file.
 * @param path  File path.
 * @param cmd   Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_write_meta(const char *path, const char *cmd) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 1;
    fprintf(fp, "%s", cmd);
    fclose(fp);
#ifndef _WIN32
    chmod(path, 0644);
#endif
    return 0;
}

/**
 * Reads the command string from a metadata file.
 * @param path     File path.
 * @param out      Output buffer.
 * @param cap      Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_read_meta(const char *path, char *out, size_t cap) {
    FILE *f;
    size_t n;

    f = fopen(path, "r");
    if (!f) return 1;
    if (!fgets(out, (int)cap, f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    n = strlen(out);
    if (n > 0 && out[n - 1] == '\n') out[n - 1] = '\0';
    return 0;
}

#ifndef _WIN32
/**
 * Composes the backend metadata file path for a key.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param out  Output buffer.
 * @param cap  Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_backend_path(
    const char *dir, const char *key, char *out, size_t cap
) {
    if ((size_t)snprintf(out, cap, "%s/%s.backend", dir, key) >= cap)
        return 1;
    return 0;
}

/**
 * Writes the backend type to a metadata file.
 * @param path    File path.
 * @param backend Backend type.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_write_backend(const char *path, kc_init_backend_t backend) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 1;
    fprintf(fp, "%d", backend);
    fclose(fp);
    chmod(path, 0644);
    return 0;
}

/**
 * Reads the backend type from a metadata file.
 * @param path File path.
 * @return Backend type or KC_INIT_BACKEND_NONE on failure.
 */
static kc_init_backend_t kc_init_read_backend(const char *path) {
    FILE *f;
    int b;

    f = fopen(path, "r");
    if (!f) return KC_INIT_BACKEND_NONE;
    if (fscanf(f, "%d", &b) != 1) {
        fclose(f);
        return KC_INIT_BACKEND_NONE;
    }
    fclose(f);
    return (kc_init_backend_t)b;
}
#endif

#ifndef _WIN32
/**
 * Checks if a command exists and is executable.
 * @param cmd Command name.
 * @return 1 if exists, 0 otherwise.
 */
static int kc_init_cmd_exists(const char *cmd) {
    char path[KC_INIT_PATH];
    int found = 0;

    if (snprintf(path, sizeof(path), "command -v %s > /dev/null 2>&1",
            cmd) < (int)sizeof(path)) {
        if (system(path) == 0) found = 1;
    }
    return found;
}

/**
 * Detects the active init system backend.
 * @return Backend enum value.
 */
static kc_init_backend_t kc_init_detect_backend(void) {
    char buf[KC_INIT_PATH];
    ssize_t len;

    len = readlink("/proc/1/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';

        if (strstr(buf, "systemd")) {
            if (kc_init_cmd_exists("systemctl")) return KC_INIT_BACKEND_SYSTEMD;
        }
        if (strstr(buf, "runit")) {
            if (kc_init_cmd_exists("sv")) return KC_INIT_BACKEND_RUNIT;
        }
        if (strstr(buf, "openrc")) {
            if (kc_init_cmd_exists("rc-update")) return KC_INIT_BACKEND_OPENRC;
        }
        if (strstr(buf, "init")) {
            if (kc_init_cmd_exists("update-rc.d") ||
                    kc_init_cmd_exists("chkconfig"))
                return KC_INIT_BACKEND_SYSV;
        }
    }

    if (kc_init_cmd_exists("systemctl")) return KC_INIT_BACKEND_SYSTEMD;
    if (kc_init_cmd_exists("rc-update")) return KC_INIT_BACKEND_OPENRC;
    if (kc_init_cmd_exists("update-rc.d") || kc_init_cmd_exists("chkconfig"))
        return KC_INIT_BACKEND_SYSV;

    return KC_INIT_BACKEND_NONE;
}
#endif

#ifndef _WIN32

/**
 * Returns the init.d directory, preferring /etc/init.d.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_initd_dir(char *out, size_t cap) {
    struct stat st;
    const char *path;

    path = "/etc/init.d";
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if ((size_t)snprintf(out, cap, "%s", path) < cap)
            return 0;
    }
    path = "/etc/rc.d/init.d";
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if ((size_t)snprintf(out, cap, "%s", path) < cap)
            return 0;
    }
    return 1;
}

/**
 * Returns the rc.d directory prefix, preferring /etc/rc.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_rcd_prefix(char *out, size_t cap) {
    struct stat st;
    const char *path;

    path = "/etc/rc2.d";
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if ((size_t)snprintf(out, cap, "/etc/rc") < cap)
            return 0;
    }
    path = "/etc/rc.d";
    if ((size_t)snprintf(out, cap, "/etc/rc.d/rc") >= cap)
        return 1;
    return 0;
}

/**
 * Composes the init.d script path for a key.
 * @param initd Init.d directory.
 * @param key   Registration key name.
 * @param out   Output buffer.
 * @param cap   Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_script_path(
    const char *initd, const char *key, char *out, size_t cap
) {
    if ((size_t)snprintf(out, cap, "%s/%s", initd, key) >= cap)
        return 1;
    return 0;
}

/**
 * Writes the init.d startup script for a key and command.
 * @param script  Script file path.
 * @param key     Registration key name.
 * @param cmd     Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_write_script_sysv(
    const char *script, const char *key, const char *cmd
) {
    FILE *f;

    f = fopen(script, "w");
    if (!f) return 1;
    fprintf(f, "#!/bin/sh\n");
    fputs("### BEGIN INIT INFO\n", f);
    fprintf(f, "# Provides:          init-%s\n", key);
    fprintf(f, "# Required-Start:    $remote_fs $syslog $network\n");
    fprintf(f, "# Required-Stop:     $remote_fs $syslog $network\n");
    fprintf(f, "# Default-Start:     2 3 4 5\n");
    fprintf(f, "# Default-Stop:      0 1 6\n");
    fprintf(f, "# Short-Description: init.c entry %s\n", key);
    fputs("### END INIT INFO\n", f);
    const char *user_name = kc_init_detect_user();
    if (strcmp(user_name, "root") != 0) {
        fprintf(f, "case \"$1\" in\n  start)\n");
        fputs("    start-stop-daemon --start ", f);
        fprintf(f, "--user %s ", user_name);
        fputs("--exec /bin/sh ", f);
        fprintf(f, "-- -c 'exec %s' &\n", cmd);
        fprintf(f, "    ;;\n  *)\n    exec %s\n    ;;\nesac\n", cmd);
    } else {
        fprintf(f, "exec %s\n", cmd);
    }
    fclose(f);
    if (chmod(script, 0755) != 0) return 1;
    return 0;
}

/**
 * Creates rc*.d symlinks for the given script and key.
 * @param rcd_prefix rc.d directory prefix.
 * @param script     Absolute path to the init.d script.
 * @param key        Registration key name.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_create_links(
    const char *rcd_prefix, const char *script, const char *key
) {
    static const int levels[] = { 2, 3, 4, 5 };
    char link[KC_INIT_PATH];
    size_t i;

    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if ((size_t)snprintf(link, sizeof(link),
                "%s%d.d/S99%s", rcd_prefix, levels[i], key)
                >= sizeof(link))
            return 1;
        (void)unlink(link);
        if (symlink(script, link) != 0 && errno != EEXIST)
            return 1;
    }
    return 0;
}

/**
 * Removes rc*.d symlinks for a key.
 * @param rcd_prefix rc.d directory prefix.
 * @param key        Registration key name.
 * @return None.
 */
static void kc_init_remove_links(
    const char *rcd_prefix, const char *key
) {
    static const int levels[] = { 2, 3, 4, 5 };
    char link[KC_INIT_PATH];
    size_t i;

    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if ((size_t)snprintf(link, sizeof(link),
                "%s%d.d/S99%s", rcd_prefix, levels[i], key)
                < sizeof(link))
            (void)unlink(link);
    }
}

/**
 * Registers or replaces a named startup entry on SysV.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update_sysv(
    const char *dir, const char *key, const char *cmd
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char initd[KC_INIT_PATH];
    char rcd[KC_INIT_PATH];
    char script[KC_INIT_PATH];

    if (kc_init_ensure_dir(dir) != 0) return 1;
    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0) return 1;
    if (kc_init_write_meta(meta, cmd) != 0) return 1;

    const char *user_name = kc_init_detect_user();
    if (strcmp(user_name, "root") != 0) {
        char umeta[KC_INIT_PATH];
        if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta))
            (void)kc_init_write_user(umeta, user_name);
    }

    if (!kc_init_is_admin()) return 0;

    if (kc_init_initd_dir(initd, sizeof(initd)) != 0) {
        (void)remove(meta);
        return 1;
    }
    if (kc_init_script_path(initd, key, script, sizeof(script)) != 0) {
        (void)remove(meta);
        return 1;
    }
    if (kc_init_write_script_sysv(script, key, cmd) != 0) {
        (void)remove(meta);
        return 1;
    }

    const char *sudo_user = getenv("SUDO_USER");
    if (sudo_user && sudo_user[0]) {
        char umeta[KC_INIT_PATH];
        if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta))
            (void)kc_init_write_user(umeta, sudo_user);
    }
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0)
        (void)kc_init_write_backend(bmeta, KC_INIT_BACKEND_SYSV);

    if (kc_init_rcd_prefix(rcd, sizeof(rcd)) != 0) {
        (void)remove(meta);
        (void)remove(script);
        return 1;
    }
    if (kc_init_create_links(rcd, script, key) != 0) {
        (void)remove(meta);
        (void)remove(script);
        kc_init_remove_links(rcd, key);
        return 1;
    }
    return 0;
}

/**
 * Removes a named startup entry on SysV.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete_sysv(
    const char *dir, const char *key
) {
    char meta[KC_INIT_PATH];
    char initd[KC_INIT_PATH];
    char rcd[KC_INIT_PATH];
    char script[KC_INIT_PATH];

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0)
        return 0;
    (void)remove(meta);

    if (kc_init_initd_dir(initd, sizeof(initd)) == 0 &&
            kc_init_script_path(initd, key, script,
                sizeof(script)) == 0) {
        (void)remove(script);
    }
    if (kc_init_rcd_prefix(rcd, sizeof(rcd)) == 0)
        kc_init_remove_links(rcd, key);

    return 0;
}

/**
 * Calls the list callback for one key on SysV.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_ls_row_sysv(
    const char *dir, const char *key, kc_init_row_handler_t cb, void *userdata
) {
    char meta[KC_INIT_PATH];
    char umeta[KC_INIT_PATH];
    char cmd[KC_INIT_BUF];
    char user[64] = "root";

    if (cb) {
        if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0 &&
                kc_init_read_meta(meta, cmd, sizeof(cmd)) == 0) {
            if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta)) {
                (void)kc_init_read_user(umeta, user, sizeof(user));
            }
            cb(key, user, cmd, userdata);
        } else {
            cb(key, "", "", userdata);
        }
    }
    return 0;
}

/**
 * Lists all registered startup entries on SysV.
 * @param dir  Metadata directory.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list_sysv(const char *dir, kc_init_row_handler_t cb, void *userdata) {
    DIR *dp;
    struct dirent *de;

    dp = opendir(dir);
    if (dp) {
        while ((de = readdir(dp))) {
            if (de->d_name[0] == '.') continue;
            if (kc_init_has_suffix(de->d_name, ".user") ||
                    kc_init_has_suffix(de->d_name, ".backend"))
                continue;

            (void)kc_init_ls_row_sysv(dir, de->d_name, cb, userdata);
        }
        closedir(dp);
    }
    return 0;
}

/**
 * Lists one registered startup entry on SysV.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list_one_sysv(
    const char *dir, const char *key, kc_init_row_handler_t cb, void *userdata
) {
    char meta[KC_INIT_PATH];
    struct stat st;

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0 &&
            stat(meta, &st) == 0) {
        return kc_init_ls_row_sysv(dir, key, cb, userdata);
    }
    return 1;
}

/**
 * Registers or replaces a named startup entry on systemd.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update_systemd(
    const char *dir, const char *key, const char *cmd
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char service[KC_INIT_PATH];
    char enable[KC_INIT_PATH];
    FILE *f;

    if (kc_init_ensure_dir(dir) != 0) return 1;
    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0) return 1;
    if (kc_init_write_meta(meta, cmd) != 0) return 1;
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0)
        (void)kc_init_write_backend(bmeta, KC_INIT_BACKEND_SYSTEMD);

    const char *user_name = kc_init_detect_user();
    if (strcmp(user_name, "root") != 0) {
        char umeta[KC_INIT_PATH];
        if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta))
            (void)kc_init_write_user(umeta, user_name);
    }

    if (!kc_init_is_admin()) return 0;

    if ((size_t)snprintf(service, sizeof(service),
            "/etc/systemd/system/init-%s.service", key) >= sizeof(service))
        return 1;

    f = fopen(service, "w");
    if (!f) return 1;
    fprintf(f, "[Unit]\nDescription=init.c entry %s\nAfter=network.target\n\n",
        key);
    fprintf(f, "[Service]\nType=simple\nRemainAfterExit=yes\n");
    
    if (strcmp(user_name, "root") != 0) {
        fprintf(f, "User=%s\n", user_name);
        fprintf(f, "Group=%s\n", user_name);
        fprintf(f, "WorkingDirectory=/home/%s\n", user_name);
    }

    fprintf(f, "ExecStart=%s\nRestart=on-failure\n\n", cmd);
    fprintf(f, "[Install]\nWantedBy=multi-user.target\n");
    fclose(f);

    (void)system("systemctl daemon-reload");
    snprintf(enable, sizeof(enable),
        "systemctl enable init-%s.service", key);
    return system(enable) == 0 ? 0 : 1;
}

/**
 * Removes a named startup entry on systemd.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete_systemd(
    const char *dir, const char *key
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char service[KC_INIT_PATH];
    char disable[KC_INIT_PATH];

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0) {
        (void)remove(meta);
    }
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0) {
        (void)remove(bmeta);
    }

    if (kc_init_is_admin()) {
        snprintf(disable, sizeof(disable),
            "systemctl disable --now init-%s.service > /dev/null 2>&1", key);
        (void)system(disable);

        if ((size_t)snprintf(service, sizeof(service),
                "/etc/systemd/system/init-%s.service", key) < sizeof(service)) {
            (void)remove(service);
        }
        (void)system("systemctl daemon-reload");
    }

    return 0;
}

/**
 * Registers or replaces a named startup entry on runit.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update_runit(
    const char *dir, const char *key, const char *cmd
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char sv_dir[KC_INIT_PATH];
    char run_script[KC_INIT_PATH];
    char link[KC_INIT_PATH];
    FILE *f;

    if (kc_init_ensure_dir(dir) != 0) return 1;
    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0) return 1;
    if (kc_init_write_meta(meta, cmd) != 0) return 1;
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0)
        (void)kc_init_write_backend(bmeta, KC_INIT_BACKEND_RUNIT);

    if (!kc_init_is_admin()) return 0;

    if ((size_t)snprintf(sv_dir, sizeof(sv_dir), "/etc/sv/init-%s", key)
            >= sizeof(sv_dir))
        return 1;
    if (kc_init_ensure_dir(sv_dir) != 0) return 1;

    if ((size_t)snprintf(run_script, sizeof(run_script), "%s/run", sv_dir)
            >= sizeof(run_script))
        return 1;

    f = fopen(run_script, "w");
    if (!f) return 1;
    fprintf(f, "#!/bin/sh\nexec %s\n", cmd);
    fclose(f);
    (void)chmod(run_script, 0755);

    if ((size_t)snprintf(link, sizeof(link), "/etc/service/init-%s", key)
            < sizeof(link)) {
        (void)unlink(link);
        (void)symlink(sv_dir, link);
    }

    return 0;
}

/**
 * Removes a named startup entry on runit.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete_runit(
    const char *dir, const char *key
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char sv_dir[KC_INIT_PATH];
    char link[KC_INIT_PATH];
    char cmd[KC_INIT_BUF];

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0) {
        (void)remove(meta);
    }
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0) {
        (void)remove(bmeta);
    }

    if (kc_init_is_admin()) {
        if ((size_t)snprintf(link, sizeof(link), "/etc/service/init-%s", key)
                < sizeof(link)) {
            (void)unlink(link);
        }

        if ((size_t)snprintf(sv_dir, sizeof(sv_dir), "/etc/sv/init-%s", key)
                < sizeof(sv_dir)) {
            struct stat st;
            if (stat(sv_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                snprintf(cmd, sizeof(cmd), "rm -rf %s", sv_dir);
                (void)system(cmd);
            }
        }
    }

    return 0;
}

/**
 * Registers or replaces a named startup entry on OpenRC.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update_openrc(
    const char *dir, const char *key, const char *cmd
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char script[KC_INIT_PATH];
    char update[KC_INIT_PATH];
    FILE *f;

    if (kc_init_ensure_dir(dir) != 0) return 1;
    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0) return 1;
    if (kc_init_write_meta(meta, cmd) != 0) return 1;
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0)
        (void)kc_init_write_backend(bmeta, KC_INIT_BACKEND_OPENRC);

    const char *user_name = kc_init_detect_user();
    if (strcmp(user_name, "root") != 0) {
        char umeta[KC_INIT_PATH];
        if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta))
            (void)kc_init_write_user(umeta, user_name);
    }

    if (!kc_init_is_admin()) return 0;

    if ((size_t)snprintf(script, sizeof(script), "/etc/init.d/init-%s", key)
            >= sizeof(script))
        return 1;

    f = fopen(script, "w");
    if (!f) return 1;
    fprintf(f, "#!/sbin/openrc-run\n");
    fprintf(f, "description=\"init.c entry %s\"\n", key);
    fprintf(f, "start() {\n");
    fprintf(f, "    ebegin \"Starting init-%s\"\n", key);
    if (strcmp(user_name, "root") != 0) {
        fputs("    start-stop-daemon --start ", f);
        fprintf(f, "--user %s ", user_name);
        fputs("--exec /bin/sh ", f);
        fprintf(f, "-- -c 'exec %s'\n", cmd);
    } else {
        fputs("    start-stop-daemon --start ", f);
        fputs("--exec /bin/sh ", f);
        fprintf(f, "-- -c 'exec %s'\n", cmd);
    }
    fprintf(f, "    eend $?\n}\n");
    fprintf(f, "depend() {\n    need net\n}\n");
    fclose(f);
    (void)chmod(script, 0755);

    snprintf(update, sizeof(update),
        "rc-update add init-%s default > /dev/null 2>&1", key);
    return system(update) == 0 ? 0 : 1;
}

/**
 * Removes a named startup entry on OpenRC.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete_openrc(
    const char *dir, const char *key
) {
    char meta[KC_INIT_PATH];
    char bmeta[KC_INIT_PATH];
    char script[KC_INIT_PATH];
    char update[KC_INIT_PATH];

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0) {
        (void)remove(meta);
    }
    if (kc_init_backend_path(dir, key, bmeta, sizeof(bmeta)) == 0) {
        (void)remove(bmeta);
    }

    if (kc_init_is_admin()) {
        snprintf(update, sizeof(update),
            "rc-update del init-%s default > /dev/null 2>&1", key);
        (void)system(update);

        if ((size_t)snprintf(script, sizeof(script), "/etc/init.d/init-%s", key)
                < sizeof(script)) {
            (void)remove(script);
        }
    }

    return 0;
}

#endif

#ifdef _WIN32

/**
 * Returns the Windows Startup folder path.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_startup_dir(char *out, size_t cap) {
    char appdata[KC_INIT_PATH];
    DWORD r;

    if (kc_init_is_admin()) {
        r = GetEnvironmentVariableA("PROGRAMDATA", appdata,
            sizeof(appdata));
        if (r != 0 && r < (DWORD)sizeof(appdata)) {
            if ((size_t)snprintf(out, cap,
                    "%s\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",
                    appdata) < cap)
                return 0;
        }
    }

    r = GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    if (r == 0 || r >= (DWORD)sizeof(appdata)) return 1;
    if ((size_t)snprintf(out, cap,
            "%s\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",
            appdata) >= cap) return 1;
    return 0;
}

/**
 * Composes the .cmd launcher path in the Startup folder.
 * @param startup Startup folder path.
 * @param key     Registration key name.
 * @param out     Output buffer.
 * @param cap     Buffer capacity.
 * @return 0 on success, 1 on overflow.
 */
static int kc_init_launcher_path(
    const char *startup, const char *key, char *out, size_t cap
) {
    if ((size_t)snprintf(out, cap, "%s\\%s.cmd", startup, key) >= cap)
        return 1;
    return 0;
}

/**
 * Writes the .cmd startup launcher for a command.
 * @param path Launcher file path.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_write_launcher(const char *path, const char *cmd) {
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "@echo off\r\n");
    fprintf(f, "%s\r\n", cmd);
    fclose(f);
    return 0;
}

/**
 * Adds the command to the HKCU Run registry key.
 * @param key Registration key name.
 * @param cmd Command string.
 * @return None.
 */
static void kc_init_reg_set(const char *key, const char *cmd) {
    HKEY hk;
    HKEY root = kc_init_is_admin() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    if (RegOpenKeyExA(root,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    RegSetValueExA(hk, key, 0, REG_SZ,
        (const BYTE *)cmd, (DWORD)(strlen(cmd) + 1));
    RegCloseKey(hk);
}

/**
 * Removes the command from the HKCU Run registry key.
 * @param key Registration key name.
 * @return None.
 */
static void kc_init_reg_delete(const char *key) {
    HKEY hk;
    HKEY root = kc_init_is_admin() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    if (RegOpenKeyExA(root,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    RegDeleteValueA(hk, key);
    RegCloseKey(hk);
}

/**
 * Registers or replaces a named startup entry on Windows.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update_win32(
    const char *dir, const char *key, const char *cmd
) {
    char meta[KC_INIT_PATH];
    char startup[KC_INIT_PATH];
    char launcher[KC_INIT_PATH];

    if (kc_init_ensure_dir(dir) != 0) return 1;
    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0) return 1;
    if (kc_init_write_meta(meta, cmd) != 0) return 1;

    const char *user_name = kc_init_detect_user();
    if (strcmp(user_name, "root") != 0) {
        char umeta[KC_INIT_PATH];
        if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta))
            (void)kc_init_write_user(umeta, user_name);
    }

    if (kc_init_startup_dir(startup, sizeof(startup)) != 0) {
        (void)DeleteFileA(meta);
        return 1;
    }
    if (kc_init_ensure_dir(startup) != 0) {
        (void)DeleteFileA(meta);
        return 1;
    }
    if (kc_init_launcher_path(startup, key, launcher,
            sizeof(launcher)) != 0) {
        (void)DeleteFileA(meta);
        return 1;
    }
    if (kc_init_write_launcher(launcher, cmd) != 0) {
        (void)DeleteFileA(meta);
        return 1;
    }
    kc_init_reg_set(key, cmd);
    return 0;
}

/**
 * Removes a named startup entry on Windows.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete_win32(
    const char *dir, const char *key
) {
    char meta[KC_INIT_PATH];
    char startup[KC_INIT_PATH];
    char launcher[KC_INIT_PATH];

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) != 0)
        return 0;
    (void)DeleteFileA(meta);

    if (kc_init_startup_dir(startup, sizeof(startup)) == 0 &&
            kc_init_launcher_path(startup, key, launcher,
                sizeof(launcher)) == 0) {
        (void)DeleteFileA(launcher);
    }
    kc_init_reg_delete(key);

    return 0;
}

/**
 * Calls the list callback for one key on Windows.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_ls_row_win32(
    const char *dir, const char *key, kc_init_row_handler_t cb, void *userdata
) {
    char meta[KC_INIT_PATH];
    char umeta[KC_INIT_PATH];
    char cmd[KC_INIT_BUF];
    char user[64] = "root";

    if (cb) {
        if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0 &&
                kc_init_read_meta(meta, cmd, sizeof(cmd)) == 0) {
            if ((size_t)snprintf(umeta, sizeof(umeta), "%s.user", meta) < sizeof(umeta)) {
                (void)kc_init_read_user(umeta, user, sizeof(user));
            }
            cb(key, user, cmd, userdata);
        } else {
            cb(key, "", "", userdata);
        }
    }
    return 0;
}

/**
 * Lists all registered startup entries on Windows.
 * @param dir  Metadata directory.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list_win32(const char *dir, kc_init_row_handler_t cb, void *userdata) {
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[KC_INIT_PATH];

    if ((size_t)snprintf(pattern, sizeof(pattern),
            "%s\\*", dir) < sizeof(pattern)) {
        h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == '.') continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (kc_init_has_suffix(fd.cFileName, ".user") ||
                        kc_init_has_suffix(fd.cFileName, ".backend")) continue;
                (void)kc_init_ls_row_win32(dir, fd.cFileName, cb, userdata);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }

    return 0;
}

/**
 * Lists one registered startup entry on Windows.
 * @param dir  Metadata directory.
 * @param key  Registration key name.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list_one_win32(
    const char *dir, const char *key, kc_init_row_handler_t cb, void *userdata
) {
    char meta[KC_INIT_PATH];
    DWORD attr;

    if (kc_init_meta_path(dir, key, meta, sizeof(meta)) == 0) {
        attr = GetFileAttributesA(meta);
        if (attr != INVALID_FILE_ATTRIBUTES)
            return kc_init_ls_row_win32(dir, key, cb, userdata);
    }
    return 1;
}

#endif

/**
 * Dispatches update to the platform backend.
 * @param ctx  Context pointer.
 * @param key  Registration key name.
 * @param cmd  Command string.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_update(
    kc_init_t *ctx, const char *key, const char *cmd
) {
#ifdef _WIN32
    return kc_init_run_update_win32(ctx->dir, key, cmd);
#else
    if (!kc_init_is_admin()) {
        return 1;
    }

    switch (ctx->backend) {
        case KC_INIT_BACKEND_SYSTEMD:
            return kc_init_run_update_systemd(ctx->dir, key, cmd);
        case KC_INIT_BACKEND_RUNIT:
            return kc_init_run_update_runit(ctx->dir, key, cmd);
        case KC_INIT_BACKEND_OPENRC:
            return kc_init_run_update_openrc(ctx->dir, key, cmd);
        case KC_INIT_BACKEND_SYSV:
            return kc_init_run_update_sysv(ctx->dir, key, cmd);
        default:
            return 1;
    }
#endif
}

/**
 * Dispatches delete to the platform backend.
 * @param ctx  Context pointer.
 * @param key  Registration key name.
 * @return 0 always (best-effort cleanup).
 */
static int kc_init_run_delete(kc_init_t *ctx, const char *key) {
#ifdef _WIN32
    return kc_init_run_delete_win32(ctx->dir, key);
#else
    if (!kc_init_is_admin()) {
        return 1;
    }

    kc_init_backend_t b = KC_INIT_BACKEND_NONE;
    char bmeta[KC_INIT_PATH];

    if (kc_init_backend_path(ctx->dir, key, bmeta, sizeof(bmeta)) == 0) {
        b = kc_init_read_backend(bmeta);
    }
    if (b == KC_INIT_BACKEND_NONE) {
        b = ctx->backend;
    }

    switch (b) {
        case KC_INIT_BACKEND_SYSTEMD:
            return kc_init_run_delete_systemd(ctx->dir, key);
        case KC_INIT_BACKEND_RUNIT:
            return kc_init_run_delete_runit(ctx->dir, key);
        case KC_INIT_BACKEND_OPENRC:
            return kc_init_run_delete_openrc(ctx->dir, key);
        case KC_INIT_BACKEND_SYSV:
            return kc_init_run_delete_sysv(ctx->dir, key);
        default:
            return kc_init_run_delete_sysv(ctx->dir, key);
    }
#endif
}

/**
 * Dispatches list-all to the platform backend.
 * @param ctx  Context pointer.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list(kc_init_t *ctx, kc_init_row_handler_t cb, void *userdata) {
#ifdef _WIN32
    return kc_init_run_list_win32(ctx->dir, cb, userdata);
#else
    return kc_init_run_list_sysv(ctx->dir, cb, userdata);
#endif
}

/**
 * Dispatches list-one to the platform backend.
 * @param ctx  Context pointer.
 * @param key  Registration key name.
 * @param cb   Callback, or NULL.
 * @param userdata Opaque pointer.
 * @return 0 on success, 1 on failure.
 */
static int kc_init_run_list_one(kc_init_t *ctx, const char *key, kc_init_row_handler_t cb, void *userdata) {
#ifdef _WIN32
    return kc_init_run_list_one_win32(ctx->dir, key, cb, userdata);
#else
    return kc_init_run_list_one_sysv(ctx->dir, key, cb, userdata);
#endif
}

typedef struct {
    char *key;
    char *user;
    char *cmd;
} kc_init_collect_entry_t;

typedef struct {
    kc_init_collect_entry_t *entries;
    size_t count;
    size_t capacity;
    int failed;
} kc_init_collect_t;

/**
 * Release temporary list collector storage.
 * @param collect Collector state.
 * @return None.
 */
static void kc_init_collect_clear(kc_init_collect_t *collect) {
    size_t i;

    if (!collect) return;
    for (i = 0; i < collect->count; i++) {
        free(collect->entries[i].key);
        free(collect->entries[i].user);
        free(collect->entries[i].cmd);
    }
    free(collect->entries);
    memset(collect, 0, sizeof(*collect));
}

/**
 * Collect one backend list row into owned temporary storage.
 * @param key Registration key.
 * @param user Registration user.
 * @param cmd Registration command.
 * @param userdata Collector state.
 * @return None.
 */
static void kc_init_collect_row(
    const char *key,
    const char *user,
    const char *cmd,
    void *userdata
) {
    kc_init_collect_t *collect;
    kc_init_collect_entry_t *next;
    kc_init_collect_entry_t *entry;
    size_t capacity;

    collect = (kc_init_collect_t *)userdata;
    if (!collect || collect->failed) return;

    if (collect->count == collect->capacity) {
        capacity = collect->capacity ? collect->capacity * 2U : 8U;
        next = (kc_init_collect_entry_t *)realloc(
            collect->entries,
            capacity * sizeof(*next)
        );
        if (!next) {
            collect->failed = 1;
            return;
        }
        collect->entries = next;
        collect->capacity = capacity;
    }

    entry = &collect->entries[collect->count];
    memset(entry, 0, sizeof(*entry));
    entry->key = kc_init_strdup(key ? key : "");
    entry->user = kc_init_strdup(user ? user : "");
    entry->cmd = kc_init_strdup(cmd ? cmd : "");
    if (!entry->key || !entry->user || !entry->cmd) {
        free(entry->key);
        free(entry->user);
        free(entry->cmd);
        memset(entry, 0, sizeof(*entry));
        collect->failed = 1;
        return;
    }
    collect->count++;
}

/**
 * Materialize collected rows into one caller-owned allocation.
 * @param collect Collector state.
 * @param out_entries Receives the entry array.
 * @param out_count Receives the entry count.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on allocation failure.
 */
static int kc_init_collect_finish(
    const kc_init_collect_t *collect,
    kc_init_entry_t **out_entries,
    size_t *out_count
) {
    kc_init_entry_t *entries;
    char *cursor;
    size_t total;
    size_t i;

    *out_entries = NULL;
    *out_count = 0U;
    if (collect->count == 0U) return KC_INIT_OK;

    if (collect->count > ((size_t)-1) / sizeof(*entries)) {
        return KC_INIT_ERROR;
    }
    total = collect->count * sizeof(*entries);

    for (i = 0; i < collect->count; i++) {
        size_t sizes[3];
        size_t j;

        sizes[0] = strlen(collect->entries[i].key) + 1U;
        sizes[1] = strlen(collect->entries[i].user) + 1U;
        sizes[2] = strlen(collect->entries[i].cmd) + 1U;
        for (j = 0; j < 3U; j++) {
            if (sizes[j] > (size_t)-1 - total) return KC_INIT_ERROR;
            total += sizes[j];
        }
    }

    entries = (kc_init_entry_t *)malloc(total);
    if (!entries) return KC_INIT_ERROR;
    cursor = (char *)(entries + collect->count);

    for (i = 0; i < collect->count; i++) {
        size_t size;

        size = strlen(collect->entries[i].key) + 1U;
        memcpy(cursor, collect->entries[i].key, size);
        entries[i].name = cursor;
        cursor += size;

        size = strlen(collect->entries[i].user) + 1U;
        memcpy(cursor, collect->entries[i].user, size);
        entries[i].user = cursor;
        cursor += size;

        size = strlen(collect->entries[i].cmd) + 1U;
        memcpy(cursor, collect->entries[i].cmd, size);
        entries[i].cmd = cursor;
        cursor += size;
    }

    *out_entries = entries;
    *out_count = collect->count;
    return KC_INIT_OK;
}

/**
 * Create or replace one persistent startup registration.
 * The active init backend is detected internally.
 * @param name Registration name.
 * @param options Startup registration options.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_create(
    const char *name,
    const kc_init_options_t *options
) {
    kc_init_t init;

    if (!kc_init_key_valid(name) || !options ||
            !kc_init_cmd_valid(options->cmd)) {
        return KC_INIT_ERROR;
    }

    memset(&init, 0, sizeof(init));
    if (kc_init_resolve_dir(init.dir, sizeof(init.dir)) != 0) {
        return KC_INIT_ERROR;
    }
#ifndef _WIN32
    init.backend = kc_init_detect_backend();
    if (init.backend == KC_INIT_BACKEND_NONE) return KC_INIT_ERROR;
#endif

    if (kc_init_run_update(&init, name, options->cmd) != 0) {
        return KC_INIT_ERROR;
    }
    return KC_INIT_OK;
}

/**
 * Get one persistent startup registration by name.
 * The returned entry and its strings share one allocation released with
 * kc_init_free().
 * @param name Registration name.
 * @param out_entry Receives the allocated entry, or NULL when absent.
 * @return KC_INIT_OK on success, KC_INIT_NOT_FOUND when absent,
 *         or KC_INIT_ERROR on failure.
 */
int kc_init_get(
    const char *name,
    kc_init_entry_t **out_entry
) {
    kc_init_t init;
    kc_init_collect_t collect;
    kc_init_entry_t *entries;
    size_t count;
    int rc;

    if (out_entry) *out_entry = NULL;
    if (!out_entry || !kc_init_key_valid(name)) return KC_INIT_ERROR;

    memset(&init, 0, sizeof(init));
    memset(&collect, 0, sizeof(collect));
    if (kc_init_resolve_dir(init.dir, sizeof(init.dir)) != 0) {
        return KC_INIT_ERROR;
    }

    rc = kc_init_run_list_one(
        &init,
        name,
        kc_init_collect_row,
        &collect
    );
    if (rc != 0 || collect.count == 0U) {
        kc_init_collect_clear(&collect);
        return KC_INIT_NOT_FOUND;
    }
    if (collect.failed || collect.count != 1U) {
        kc_init_collect_clear(&collect);
        return KC_INIT_ERROR;
    }

    entries = NULL;
    count = 0U;
    rc = kc_init_collect_finish(&collect, &entries, &count);
    kc_init_collect_clear(&collect);
    if (rc != KC_INIT_OK || count != 1U) {
        free(entries);
        return KC_INIT_ERROR;
    }

    *out_entry = entries;
    return KC_INIT_OK;
}

/**
 * List persistent startup registrations in the active user namespace.
 * The returned array and its strings share one allocation released with
 * kc_init_free().
 * @param out_entries Receives the allocated entry array, or NULL when empty.
 * @param out_count Receives the number of entries.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_list(
    kc_init_entry_t **out_entries,
    size_t *out_count
) {
    kc_init_t init;
    kc_init_collect_t collect;
    int rc;

    if (out_entries) *out_entries = NULL;
    if (out_count) *out_count = 0U;
    if (!out_entries || !out_count) return KC_INIT_ERROR;

    memset(&init, 0, sizeof(init));
    memset(&collect, 0, sizeof(collect));
    if (kc_init_resolve_dir(init.dir, sizeof(init.dir)) != 0) {
        return KC_INIT_ERROR;
    }

    rc = kc_init_run_list(&init, kc_init_collect_row, &collect);
    if (rc != 0 || collect.failed) {
        kc_init_collect_clear(&collect);
        return KC_INIT_ERROR;
    }

    rc = kc_init_collect_finish(&collect, out_entries, out_count);
    kc_init_collect_clear(&collect);
    return rc;
}

/**
 * Remove one persistent startup registration.
 * Missing registrations are a successful no-op.
 * @param name Registration name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_delete(const char *name) {
    kc_init_t init;

    if (!kc_init_key_valid(name)) return KC_INIT_ERROR;
    memset(&init, 0, sizeof(init));
    if (kc_init_resolve_dir(init.dir, sizeof(init.dir)) != 0) {
        return KC_INIT_ERROR;
    }
#ifndef _WIN32
    init.backend = kc_init_detect_backend();
#endif

    if (!kc_init_entry_exists(&init, name)) return KC_INIT_OK;
    return kc_init_run_delete(&init, name) == 0
        ? KC_INIT_OK
        : KC_INIT_ERROR;
}

/**
 * Release memory returned by the init library.
 * @param ptr Allocation returned by the init library, or NULL.
 * @return None.
 */
void kc_init_free(void *ptr) {
    free(ptr);
}

#ifndef KC_INIT_BUILD_VERSION
#define KC_INIT_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_init_version(void) {
    return (uint64_t)KC_INIT_BUILD_VERSION;
}
