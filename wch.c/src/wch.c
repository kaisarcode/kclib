/**
 * wch.c - Managed file watcher CLI.
 * Summary: Registers named resident watchers and dispatches events to commands.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwch.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define KC_WCH_PATH_MAX MAX_PATH
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define KC_WCH_PATH_MAX 4096
#endif

#define KC_WCH_NAME_MAX 128
#define KC_WCH_CMD_MAX 4096

typedef struct {
    char name[KC_WCH_NAME_MAX];
    char path[KC_WCH_PATH_MAX];
    char command[KC_WCH_CMD_MAX];
    int recursive;
} kc_wch_registration_t;

typedef struct {
    const char *command;
} kc_wch_dispatch_t;

/**
 * Prints command usage information.
 * @return None.
 */
static void kc_wch_help(void) {
    printf("Usage: wch <name> [options] <path> <command...>\n");
    printf("\n");
    printf("Commands:\n");
    printf("  wch <name> <path> <cmd>       Register or replace a watcher\n");
    printf("  wch <name> -r <path> <cmd>    Register a recursive watcher\n");
    printf("  wch -l [name]                 List watchers\n");
    printf("  wch <name> -l                 List one watcher\n");
    printf("  wch -d <name>                 Delete a watcher\n");
    printf("  wch <name> -d                 Delete a watcher\n");
    printf("\n");
    printf("Options:\n");
    printf("  -r, --recursive               Watch directories recursively\n");
    printf("  -l, --list                    List watchers\n");
    printf("  -d, --delete                  Stop and delete a watcher\n");
    printf("  -h, --help                    Show this help\n");
    printf("  -v, --version                 Show version\n");
}

/**
 * Prints the binary version.
 * @return None.
 */
static void kc_wch_print_version(void) {
    printf("wch build %llu\n", (unsigned long long)kc_wch_version());
}

/**
 * Validates one registration name.
 * @param name Registration name.
 * @return 1 when valid, otherwise 0.
 */
static int kc_wch_valid_name(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0' ||
            strlen(name) >= KC_WCH_NAME_MAX) {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];

        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            return 0;
        }
    }
    return 1;
}

/**
 * Resolves the runtime directory.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_runtime_dir(char *out, size_t cap) {
    const char *override = getenv("KC_WCH_DIR");

    if (override != NULL && override[0] != '\0') {
        return (size_t)snprintf(out, cap, "%s", override) < cap ? 0 : 1;
    }

#ifdef _WIN32
    {
        char temp[MAX_PATH];
        DWORD n = GetTempPathA((DWORD)sizeof(temp), temp);

        if (n == 0 || n >= sizeof(temp)) return 1;
        return (size_t)snprintf(out, cap, "%swch.c", temp) < cap ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_RUNTIME_DIR");

        if (xdg != NULL && xdg[0] != '\0') {
            return (size_t)snprintf(out, cap, "%s/wch.c", xdg) < cap ? 0 : 1;
        }
        return (size_t)snprintf(
            out, cap, "/tmp/wch.c-%lu", (unsigned long)getuid()
        ) < cap ? 0 : 1;
    }
#endif
}

/**
 * Ensures the runtime directory exists.
 * @param dir Runtime directory.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_ensure_dir(const char *dir) {
#ifdef _WIN32
    if (CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
#else
    if (mkdir(dir, 0700) == 0 || errno == EEXIST) {
        return 0;
    }
#endif
    return 1;
}

/**
 * Composes one registration file path.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @param ext File suffix.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_state_path(
    char *out,
    size_t cap,
    const char *dir,
    const char *name,
    const char *ext
) {
#ifdef _WIN32
    return (size_t)snprintf(out, cap, "%s\\%s%s", dir, name, ext) < cap ? 0 : 1;
#else
    return (size_t)snprintf(out, cap, "%s/%s%s", dir, name, ext) < cap ? 0 : 1;
#endif
}

/**
 * Writes one registration metadata file.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_write_registration(
    const char *dir,
    const kc_wch_registration_t *registration
) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(
            path, sizeof(path), dir, registration->name, ".meta") != 0) {
        return 1;
    }

    file = fopen(path, "wb");
    if (file == NULL) return 1;

    if (fprintf(file, "%d\n%s\n%s\n",
            registration->recursive,
            registration->path,
            registration->command) < 0) {
        fclose(file);
        return 1;
    }

    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Removes one trailing newline from a string.
 * @param text Mutable string.
 * @return None.
 */
static void kc_wch_chomp(char *text) {
    size_t len = strlen(text);

    while (len > 0U &&
            (text[len - 1U] == '\n' || text[len - 1U] == '\r')) {
        text[--len] = '\0';
    }
}

/**
 * Reads one registration metadata file.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @param registration Destination registration.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_read_registration(
    const char *dir,
    const char *name,
    kc_wch_registration_t *registration
) {
    char path[KC_WCH_PATH_MAX];
    char recursive[32];
    FILE *file;

    if (kc_wch_state_path(path, sizeof(path), dir, name, ".meta") != 0) {
        return 1;
    }

    file = fopen(path, "rb");
    if (file == NULL) return 1;

    memset(registration, 0, sizeof(*registration));
    snprintf(registration->name, sizeof(registration->name), "%s", name);

    if (fgets(recursive, sizeof(recursive), file) == NULL ||
            fgets(registration->path, sizeof(registration->path), file) == NULL ||
            fgets(registration->command, sizeof(registration->command), file) == NULL) {
        fclose(file);
        return 1;
    }
    fclose(file);

    kc_wch_chomp(recursive);
    kc_wch_chomp(registration->path);
    kc_wch_chomp(registration->command);
    registration->recursive = atoi(recursive) != 0;
    return 0;
}

/**
 * Writes one process ID file.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @param pid Process ID.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_write_pid(const char *dir, const char *name, long pid) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(path, sizeof(path), dir, name, ".pid") != 0) {
        return 1;
    }

    file = fopen(path, "wb");
    if (file == NULL) return 1;
    if (fprintf(file, "%ld\n", pid) < 0) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Reads one process ID file.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @param out_pid Destination process ID.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_read_pid(
    const char *dir,
    const char *name,
    long *out_pid
) {
    char path[KC_WCH_PATH_MAX];
    FILE *file;

    if (kc_wch_state_path(path, sizeof(path), dir, name, ".pid") != 0) {
        return 1;
    }

    file = fopen(path, "rb");
    if (file == NULL) return 1;
    if (fscanf(file, "%ld", out_pid) != 1) {
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

/**
 * Tests whether one resident watcher process is alive.
 * @param pid Process ID.
 * @return 1 when alive, otherwise 0.
 */
static int kc_wch_pid_alive(long pid) {
#ifdef _WIN32
    HANDLE process;
    DWORD code;

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (process == NULL) return 0;
    if (!GetExitCodeProcess(process, &code)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);
    return code == STILL_ACTIVE;
#else
    if (pid <= 0) return 0;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

/**
 * Stops one resident watcher process.
 * @param pid Process ID.
 * @return None.
 */
static void kc_wch_stop_pid(long pid) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);

    if (process != NULL) {
        TerminateProcess(process, 0);
        CloseHandle(process);
    }
#else
    if (pid > 0) {
        (void)kill((pid_t)pid, SIGTERM);
    }
#endif
}

/**
 * Removes one registration and PID file.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @return None.
 */
static void kc_wch_remove_state(const char *dir, const char *name) {
    char path[KC_WCH_PATH_MAX];

    if (kc_wch_state_path(path, sizeof(path), dir, name, ".meta") == 0) {
        (void)remove(path);
    }
    if (kc_wch_state_path(path, sizeof(path), dir, name, ".pid") == 0) {
        (void)remove(path);
    }
}

/**
 * Maps one event type to its command argument.
 * @param type Event type constant.
 * @return Event label.
 */
static const char *kc_wch_event_label(int type) {
    if (type == KC_WCH_UPD) return "upd";
    if (type == KC_WCH_DEL) return "del";
    return "add";
}

#ifndef _WIN32
/**
 * Appends one POSIX shell-quoted argument.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @param pos Current write offset.
 * @param value Argument value.
 * @return 0 on success, 1 on overflow.
 */
static int kc_wch_shell_arg(
    char *out,
    size_t cap,
    size_t *pos,
    const char *value
) {
    size_t i;

    if (*pos + 1U >= cap) return 1;
    out[(*pos)++] = '\'';

    for (i = 0; value[i] != '\0'; i++) {
        if (value[i] == '\'') {
            const char *escape = "'\\''";
            size_t n = strlen(escape);

            if (*pos + n >= cap) return 1;
            memcpy(out + *pos, escape, n);
            *pos += n;
        } else {
            if (*pos + 1U >= cap) return 1;
            out[(*pos)++] = value[i];
        }
    }

    if (*pos + 2U > cap) return 1;
    out[(*pos)++] = '\'';
    out[*pos] = '\0';
    return 0;
}
#endif

/**
 * Dispatches one watcher event to a child process.
 * @param event Watcher event.
 * @param userdata Dispatch configuration.
 * @return None.
 */
static void kc_wch_dispatch_event(
    const kc_wch_event_t *event,
    void *userdata
) {
    kc_wch_dispatch_t *dispatch = (kc_wch_dispatch_t *)userdata;
    const char *label;

    if (event == NULL || event->path == NULL ||
            dispatch == NULL || dispatch->command == NULL) {
        return;
    }

    label = kc_wch_event_label(event->type);

#ifdef _WIN32
    {
        char command[KC_WCH_CMD_MAX + KC_WCH_PATH_MAX + 128];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;

        if ((size_t)snprintf(
                command, sizeof(command),
                "cmd.exe /c %s %s \"%s\"",
                dispatch->command, label, event->path) >= sizeof(command)) {
            return;
        }

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);

        if (CreateProcessA(
                NULL, command, NULL, NULL, FALSE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
#else
    {
        pid_t pid = fork();

        if (pid == 0) {
            char command[KC_WCH_CMD_MAX + KC_WCH_PATH_MAX + 128];
            size_t pos = 0U;
            size_t base = strlen(dispatch->command);

            if (base + 2U >= sizeof(command)) _exit(127);
            memcpy(command, dispatch->command, base);
            pos = base;
            command[pos++] = ' ';
            command[pos] = '\0';

            if (kc_wch_shell_arg(
                    command, sizeof(command), &pos, label) != 0) {
                _exit(127);
            }
            if (pos + 1U >= sizeof(command)) _exit(127);
            command[pos++] = ' ';
            command[pos] = '\0';
            if (kc_wch_shell_arg(
                    command, sizeof(command), &pos, event->path) != 0) {
                _exit(127);
            }

            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(127);
        }
    }
#endif
}

/**
 * Runs one resident watcher from stored registration data.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @return Process exit status.
 */
static int kc_wch_serve(const char *dir, const char *name) {
    kc_wch_registration_t registration;
    kc_wch_options_t options = { 0 };
    kc_wch_dispatch_t dispatch;
    kc_wch_t *watcher = NULL;

    if (kc_wch_read_registration(dir, name, &registration) != 0) {
        return 1;
    }

#ifndef _WIN32
    signal(SIGCHLD, SIG_IGN);
#endif

    options.recursive = registration.recursive;
    if (kc_wch_open(&watcher, registration.path, &options) != KC_WCH_OK) {
        return 1;
    }

    dispatch.command = registration.command;
    if (kc_wch_on(watcher, kc_wch_dispatch_event, &dispatch) != KC_WCH_OK) {
        kc_wch_close(watcher);
        return 1;
    }

#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;) pause();
#endif

    kc_wch_close(watcher);
    return 0;
}

/**
 * Stops and removes one named watcher.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @return 0 on success.
 */
static int kc_wch_delete(const char *dir, const char *name) {
    long pid;

    if (kc_wch_read_pid(dir, name, &pid) == 0) {
        kc_wch_stop_pid(pid);
    }
    kc_wch_remove_state(dir, name);
    return 0;
}

/**
 * Starts or replaces one resident watcher.
 * @param dir Runtime directory.
 * @param registration Registration data.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_update(
    const char *dir,
    const kc_wch_registration_t *registration
) {
    long old_pid;

    if (kc_wch_ensure_dir(dir) != 0) return 1;

    if (kc_wch_read_pid(dir, registration->name, &old_pid) == 0) {
        kc_wch_stop_pid(old_pid);
    }

    if (kc_wch_write_registration(dir, registration) != 0) {
        return 1;
    }

#ifdef _WIN32
    {
        char exe[MAX_PATH];
        char command[MAX_PATH + KC_WCH_NAME_MAX + 64];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;

        if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
        if ((size_t)snprintf(
                command, sizeof(command),
                "\"%s\" --_serve \"%s\"",
                exe, registration->name) >= sizeof(command)) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }

        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);

        if (!CreateProcessA(
                NULL, command, NULL, NULL, FALSE,
                DETACHED_PROCESS | CREATE_NO_WINDOW,
                NULL, NULL, &si, &pi)) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }

        CloseHandle(pi.hThread);
        if (kc_wch_write_pid(
                dir, registration->name, (long)pi.dwProcessId) != 0) {
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
        CloseHandle(pi.hProcess);
    }
#else
    {
        pid_t pid = fork();

        if (pid < 0) {
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }

        if (pid == 0) {
            if (setsid() < 0) _exit(1);
            _exit(kc_wch_serve(dir, registration->name));
        }

        if (kc_wch_write_pid(
                dir, registration->name, (long)pid) != 0) {
            (void)kill(pid, SIGTERM);
            kc_wch_remove_state(dir, registration->name);
            return 1;
        }
    }
#endif

    return 0;
}

/**
 * Prints one registration row.
 * @param dir Runtime directory.
 * @param name Registration name.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_list_one(const char *dir, const char *name) {
    kc_wch_registration_t registration;
    long pid = 0;
    const char *status = "stopped";

    if (kc_wch_read_registration(dir, name, &registration) != 0) {
        return 0;
    }

    if (kc_wch_read_pid(dir, name, &pid) == 0 && kc_wch_pid_alive(pid)) {
        status = "running";
    }

    printf("%s\t%s\t%s\t%s\t%s\n",
        registration.name,
        status,
        registration.recursive ? "recursive" : "direct",
        registration.path,
        registration.command);
    return 0;
}

/**
 * Lists all registered watchers.
 * @param dir Runtime directory.
 * @return 0 on success, 1 on failure.
 */
static int kc_wch_list_all(const char *dir) {
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE find;
    char pattern[KC_WCH_PATH_MAX];

    if ((size_t)snprintf(
            pattern, sizeof(pattern), "%s\\*.meta", dir) >= sizeof(pattern)) {
        return 1;
    }

    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;

    do {
        size_t len = strlen(data.cFileName);

        if (len > 5U && strcmp(data.cFileName + len - 5U, ".meta") == 0) {
            data.cFileName[len - 5U] = '\0';
            (void)kc_wch_list_one(dir, data.cFileName);
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
#else
    DIR *directory = opendir(dir);
    struct dirent *entry;

    if (directory == NULL) return 0;

    while ((entry = readdir(directory)) != NULL) {
        size_t len = strlen(entry->d_name);

        if (len > 5U && strcmp(entry->d_name + len - 5U, ".meta") == 0) {
            char name[KC_WCH_NAME_MAX];

            if (len - 5U >= sizeof(name)) continue;
            memcpy(name, entry->d_name, len - 5U);
            name[len - 5U] = '\0';
            (void)kc_wch_list_one(dir, name);
        }
    }

    closedir(directory);
#endif
    return 0;
}

/**
 * Joins command arguments with spaces.
 * @param out Destination command buffer.
 * @param cap Destination capacity.
 * @param argv Argument vector.
 * @param from First command argument index.
 * @param argc Argument count.
 * @return 0 on success, 1 on overflow.
 */
static int kc_wch_join_args(
    char *out,
    size_t cap,
    char **argv,
    int from,
    int argc
) {
    size_t pos = 0U;
    int i;

    out[0] = '\0';

    for (i = from; i < argc; i++) {
        int n;

        if (pos > 0U) {
            if (pos + 1U >= cap) return 1;
            out[pos++] = ' ';
        }

        n = snprintf(out + pos, cap - pos, "%s", argv[i]);
        if (n < 0 || (size_t)n >= cap - pos) return 1;
        pos += (size_t)n;
    }

    return 0;
}

/**
 * Runs the managed watcher command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    char dir[KC_WCH_PATH_MAX];
    int i = 1;

    if (kc_wch_runtime_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "wch: cannot resolve runtime directory\n");
        return 1;
    }

    if (argc < 2) {
        kc_wch_help();
        return 1;
    }

    if (strcmp(argv[1], "--_serve") == 0) {
        if (argc != 3 || !kc_wch_valid_name(argv[2])) return 1;
        return kc_wch_serve(dir, argv[2]);
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        kc_wch_help();
        return 0;
    }

    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        kc_wch_print_version();
        return 0;
    }

    if (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
        if (argc == 2) return kc_wch_list_all(dir);
        if (argc == 3 && kc_wch_valid_name(argv[2])) {
            return kc_wch_list_one(dir, argv[2]);
        }
        return 1;
    }

    if (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--delete") == 0) {
        if (argc != 3 || !kc_wch_valid_name(argv[2])) return 1;
        return kc_wch_delete(dir, argv[2]);
    }

    if (!kc_wch_valid_name(argv[1])) {
        fprintf(stderr, "wch: invalid watcher name\n");
        return 1;
    }

    if (argc == 3 &&
            (strcmp(argv[2], "-l") == 0 ||
             strcmp(argv[2], "--list") == 0)) {
        return kc_wch_list_one(dir, argv[1]);
    }

    if (argc == 3 &&
            (strcmp(argv[2], "-d") == 0 ||
             strcmp(argv[2], "--delete") == 0)) {
        return kc_wch_delete(dir, argv[1]);
    }

    {
        kc_wch_registration_t registration;
        int recursive = 0;

        memset(&registration, 0, sizeof(registration));
        snprintf(
            registration.name, sizeof(registration.name), "%s", argv[1]
        );

        i = 2;
        if (i < argc &&
                (strcmp(argv[i], "-r") == 0 ||
                 strcmp(argv[i], "--recursive") == 0)) {
            recursive = 1;
            i++;
        }

        if (i >= argc) {
            fprintf(stderr, "wch: missing path\n");
            return 1;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "wch: missing command\n");
            return 1;
        }

        if ((size_t)snprintf(
                registration.path,
                sizeof(registration.path),
                "%s",
                argv[i]) >= sizeof(registration.path)) {
            return 1;
        }
        registration.recursive = recursive;

        if (kc_wch_join_args(
                registration.command,
                sizeof(registration.command),
                argv,
                i + 1,
                argc) != 0) {
            fprintf(stderr, "wch: command too long\n");
            return 1;
        }

        if (kc_wch_update(dir, &registration) != 0) {
            fprintf(stderr, "wch: failed to register watcher\n");
            return 1;
        }
    }

    return 0;
}
