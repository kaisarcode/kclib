/**
 * pact.c - Scoped trust command-line interface.
 * Summary: Establishes trust relationships and protects binary messages.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libpact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define PACT_CLI_PATH_SIZE 4096

/**
 * Prints public CLI usage.
 * @param name Program executable name.
 * @return None.
 */
static void pact_cli_usage(const char *name)
{
    printf("Usage: %s <command> [arguments] [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  invite                    Create a one-use invitation\n");
    printf("  join <code>               Join an invitation\n");
    printf("  confirm <confirmation>    Confirm a joined invitation\n");
    printf("  seal <uid> [message]      Protect data for a remote UID\n");
    printf("  unseal <uid> [data]       Restore data addressed to a local UID\n");
    printf("  -l [uid]                  List relationships and invitations\n");
    printf("  <uid> -d                  Delete a relationship or invitation\n");
    printf("\n");
    printf("Options:\n");
    printf("  -l, --list                List relationships and invitations\n");
    printf("  -d, --delete              Delete a relationship or invitation\n");
    printf("  -i, --input <file>        Read seal/unseal input from file\n");
    printf("  -h, --help                Show this help\n");
    printf("  -v, --version             Show version\n");
}

/**
 * Reports whether standard input is attached to an interactive terminal.
 * @return Nonzero when stdin is interactive.
 */
static int pact_cli_stdin_is_tty(void)
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (handle == INVALID_HANDLE_VALUE || handle == NULL) return 0;
    return GetConsoleMode(handle, &mode) != 0;
#else
    return isatty(STDIN_FILENO);
#endif
}

/**
 * Reads all bytes from one stream.
 * @param stream Input stream.
 * @param out Receives allocated data.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_read_stream(FILE *stream, void **out, size_t *out_size)
{
    unsigned char *buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    unsigned char chunk[4096];
    size_t count;

    if (!stream || !out || !out_size) return 1;
    *out = NULL;
    *out_size = 0;

    while ((count = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        size_t required;
        unsigned char *next;

        if (count > SIZE_MAX - size) {
            free(buffer);
            return 1;
        }
        required = size + count;
        if (required > capacity) {
            size_t next_capacity = capacity ? capacity : 4096;

            while (next_capacity < required) {
                if (next_capacity > SIZE_MAX / 2) {
                    next_capacity = required;
                    break;
                }
                next_capacity *= 2;
            }
            next = (unsigned char *)realloc(buffer, next_capacity);
            if (!next) {
                free(buffer);
                return 1;
            }
            buffer = next;
            capacity = next_capacity;
        }
        memcpy(buffer + size, chunk, count);
        size += count;
    }

    if (ferror(stream)) {
        free(buffer);
        return 1;
    }

    if (!buffer) {
        buffer = (unsigned char *)malloc(1);
        if (!buffer) return 1;
    }

    *out = buffer;
    *out_size = size;
    return 0;
}

/**
 * Reads all bytes from one file.
 * @param path Input file path.
 * @param out Receives allocated data.
 * @param out_size Receives byte count.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_read_file(
    const char *path,
    void **out,
    size_t *out_size
)
{
    FILE *file;
    int status;

    if (!path) return 1;
    file = fopen(path, "rb");
    if (!file) return 1;
    status = pact_cli_read_stream(file, out, out_size);
    fclose(file);
    return status;
}

/**
 * Writes exact binary output to stdout.
 * @param data Source bytes.
 * @param size Byte count.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_write_stdout(const void *data, size_t size)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    if (size > 0 && fwrite(data, 1, size, stdout) != size) return 1;
    return fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Resolves seal/unseal input with argument, stdin, then file precedence.
 * @param direct Optional direct argument.
 * @param input_path Optional input file.
 * @param out Receives allocated data when needed.
 * @param out_size Receives byte count.
 * @param direct_data Receives direct pointer when direct input wins.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_resolve_input(
    const char *direct,
    const char *input_path,
    void **out,
    size_t *out_size,
    const void **direct_data
)
{
    *out = NULL;
    *out_size = 0;
    *direct_data = NULL;

    if (direct) {
        *direct_data = direct;
        *out_size = strlen(direct);
        return 0;
    }

    if (!pact_cli_stdin_is_tty()) {
#ifdef _WIN32
        if (_setmode(_fileno(stdin), _O_BINARY) == -1) return 1;
#endif
        if (pact_cli_read_stream(stdin, out, out_size)) return 1;
        if (*out_size > 0 || !input_path) return 0;
        free(*out);
        *out = NULL;
    }

    if (input_path) return pact_cli_read_file(input_path, out, out_size);

    *out = malloc(1);
    return *out ? 0 : 1;
}

/**
 * Resolves the persistent trust-state directory.
 * @param out Destination path buffer.
 * @param capacity Destination capacity.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_resolve_dir(char *out, size_t capacity)
{
    const char *override = getenv("KC_PACT_DIR");

    if (!out || capacity == 0) return 1;
    if (override && override[0]) {
        return (size_t)snprintf(out, capacity, "%s", override) < capacity ? 0 : 1;
    }

#ifdef _WIN32
    {
        const char *base = getenv("LOCALAPPDATA");

        if (!base || !base[0]) base = getenv("APPDATA");
        if (!base || !base[0]) return 1;
        return (size_t)snprintf(out, capacity,
            "%s\\kaisarcode\\pact.c", base) < capacity ? 0 : 1;
    }
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *home = getenv("HOME");

        if (xdg && xdg[0]) {
            return (size_t)snprintf(out, capacity,
                "%s/kaisarcode/pact.c", xdg) < capacity ? 0 : 1;
        }
        if (!home || !home[0]) return 1;
        return (size_t)snprintf(out, capacity,
            "%s/.local/share/kaisarcode/pact.c", home) < capacity ? 0 : 1;
    }
#endif
}

/**
 * Joins one state-directory path.
 * @param out Destination path.
 * @param capacity Destination capacity.
 * @param dir Base directory.
 * @param child Child component.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_path_join(
    char *out,
    size_t capacity,
    const char *dir,
    const char *child
)
{
#ifdef _WIN32
    return (size_t)snprintf(out, capacity, "%s\\%s", dir, child) < capacity ? 0 : 1;
#else
    return (size_t)snprintf(out, capacity, "%s/%s", dir, child) < capacity ? 0 : 1;
#endif
}

/**
 * Checks whether one filename is a canonical UUID.
 * @param uid Candidate UID.
 * @return Nonzero when valid.
 */
static int pact_cli_uid_valid(const char *uid)
{
    size_t i;

    if (!uid || strlen(uid) != KC_PACT_UID_SIZE) return 0;
    for (i = 0; i < KC_PACT_UID_SIZE; i++) {
        unsigned char ch = (unsigned char)uid[i];
        int hyphen = i == 8U || i == 13U || i == 18U || i == 23U;

        if (hyphen) {
            if (ch != '-') return 0;
        } else if (!((ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f')))
        {
            return 0;
        }
    }
    return 1;
}

#ifdef _WIN32
/**
 * Converts a Windows file timestamp to Unix seconds.
 * @param value Windows FILETIME value.
 * @return Unix timestamp in seconds.
 */
static unsigned long long pact_cli_file_time(FILETIME value)
{
    ULARGE_INTEGER time;
    const unsigned long long epoch = 116444736000000000ULL;

    time.LowPart = value.dwLowDateTime;
    time.HighPart = value.dwHighDateTime;
    if (time.QuadPart < epoch) return 0;
    return (time.QuadPart - epoch) / 10000000ULL;
}
#endif

/**
 * Prints one list row from a persisted state record.
 * @param uid Remote UID.
 * @param status State label.
 * @param created Record creation timestamp.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_print_entry(
    const char *uid,
    const char *status,
    unsigned long long created
)
{
    return printf("%s\t%s\t%llu\n", uid, status, created) < 0 ? 1 : 0;
}

#ifdef _WIN32
/**
 * Lists one Windows state directory.
 * @param root Trust-state root.
 * @param kind State directory name.
 * @param status State label.
 * @param filter Optional UID filter.
 * @param found Receives whether a matching row was found.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_list_kind(
    const char *root,
    const char *kind,
    const char *status,
    const char *filter,
    int *found
)
{
    char dir[PACT_CLI_PATH_SIZE];
    char pattern[PACT_CLI_PATH_SIZE];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    if (pact_cli_path_join(dir, sizeof(dir), root, kind) ||
        (size_t)snprintf(pattern, sizeof(pattern), "%s\\*", dir) >= sizeof(pattern))
    {
        return 1;
    }

    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : 1;
    }

    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            pact_cli_uid_valid(data.cFileName) &&
            (!filter || strcmp(filter, data.cFileName) == 0))
        {
            if (pact_cli_print_entry(data.cFileName, status,
                pact_cli_file_time(data.ftCreationTime)))
            {
                FindClose(handle);
                return 1;
            }
            *found = 1;
        }
    } while (FindNextFileA(handle, &data));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        FindClose(handle);
        return 1;
    }
    return FindClose(handle) ? 0 : 1;
}
#else
/**
 * Lists one POSIX state directory.
 * @param root Trust-state root.
 * @param kind State directory name.
 * @param status State label.
 * @param filter Optional UID filter.
 * @param found Receives whether a matching row was found.
 * @return Zero on success, nonzero on failure.
 */
static int pact_cli_list_kind(
    const char *root,
    const char *kind,
    const char *status,
    const char *filter,
    int *found
)
{
    char dir[PACT_CLI_PATH_SIZE];
    DIR *stream;
    struct dirent *entry;

    if (pact_cli_path_join(dir, sizeof(dir), root, kind)) return 1;
    stream = opendir(dir);
    if (!stream) return 1;

    while ((entry = readdir(stream)) != NULL) {
        char path[PACT_CLI_PATH_SIZE];
        struct stat info;

        if (!pact_cli_uid_valid(entry->d_name) ||
            (filter && strcmp(filter, entry->d_name) != 0))
        {
            continue;
        }
        if (pact_cli_path_join(path, sizeof(path), dir, entry->d_name) ||
            stat(path, &info) != 0 || !S_ISREG(info.st_mode))
        {
            closedir(stream);
            return 1;
        }
        if (pact_cli_print_entry(entry->d_name, status,
            (unsigned long long)info.st_mtime))
        {
            closedir(stream);
            return 1;
        }
        *found = 1;
    }

    return closedir(stream) == 0 ? 0 : 1;
}
#endif

/**
 * Lists persisted remote UIDs.
 * @param filter Optional remote UID filter.
 * @return Process exit code.
 */
static int pact_cli_list(const char *filter)
{
    char root[PACT_CLI_PATH_SIZE];
    int found = 0;

    if (filter && !pact_cli_uid_valid(filter)) {
        fprintf(stderr, "pact: invalid uid\n");
        return 1;
    }
    if (pact_cli_resolve_dir(root, sizeof(root)) ||
        pact_cli_list_kind(root, "pending", "pending", filter, &found) ||
        pact_cli_list_kind(root, "peers", "confirmed", filter, &found))
    {
        fprintf(stderr, "pact: cannot list local state\n");
        return 1;
    }
    if (fflush(stdout) != 0) return 1;
    if (filter && !found) {
        fprintf(stderr, "pact: uid not found\n");
        return 1;
    }
    return 0;
}

/**
 * Executes invite.
 * @param trust Trust context.
 * @return Process exit code.
 */
static int pact_cli_invite(kc_pact_t *trust)
{
    char *uid = NULL;
    char *code = NULL;
    int status = 1;

    if (kc_pact_invite(trust, &uid, &code) != KC_PACT_OK) {
        fprintf(stderr, "pact: invite failed\n");
        goto done;
    }
    if (printf("%s\t%s\n", uid, code) < 0 || fflush(stdout) != 0) goto done;
    status = 0;

done:
    kc_pact_free(uid);
    kc_pact_free(code);
    return status;
}

/**
 * Executes join.
 * @param trust Trust context.
 * @param code Invitation code.
 * @return Process exit code.
 */
static int pact_cli_join(kc_pact_t *trust, const char *code)
{
    char *uid = NULL;
    char *confirmation = NULL;
    int status = 1;

    if (kc_pact_join(trust, code, &uid, &confirmation) != KC_PACT_OK) {
        fprintf(stderr, "pact: join failed\n");
        goto done;
    }
    if (printf("%s\t%s\n", uid, confirmation) < 0 || fflush(stdout) != 0) {
        goto done;
    }
    status = 0;

done:
    kc_pact_free(uid);
    kc_pact_free(confirmation);
    return status;
}

/**
 * Executes confirm.
 * @param trust Trust context.
 * @param confirmation Confirmation text.
 * @return Process exit code.
 */
static int pact_cli_confirm(kc_pact_t *trust, const char *confirmation)
{
    char *uid = NULL;
    int status = 1;

    if (kc_pact_confirm(trust, confirmation, &uid) != KC_PACT_OK) {
        fprintf(stderr, "pact: confirm failed\n");
        goto done;
    }
    if (printf("%s\n", uid) < 0 || fflush(stdout) != 0) goto done;
    status = 0;

done:
    kc_pact_free(uid);
    return status;
}

/**
 * Executes seal or unseal.
 * @param trust Trust context.
 * @param unseal Nonzero for unseal.
 * @param uid Destination UID.
 * @param direct Optional direct input.
 * @param input_path Optional file input.
 * @return Process exit code.
 */
static int pact_cli_transform(
    kc_pact_t *trust,
    int unseal,
    const char *uid,
    const char *direct,
    const char *input_path
)
{
    void *owned_input = NULL;
    size_t input_size = 0;
    const void *direct_data = NULL;
    const void *input;
    void *output = NULL;
    size_t output_size = 0;
    int rc;
    int status = 1;

    if (pact_cli_resolve_input(direct, input_path, &owned_input,
        &input_size, &direct_data))
    {
        fprintf(stderr, "pact: cannot read input\n");
        return 1;
    }
    input = direct_data ? direct_data : owned_input;

    if (unseal) {
        rc = kc_pact_unseal(trust, uid, input, input_size,
            &output, &output_size);
    } else {
        rc = kc_pact_seal(trust, uid, input, input_size,
            &output, &output_size);
    }
    if (rc != KC_PACT_OK) {
        fprintf(stderr, "pact: %s failed\n", unseal ? "unseal" : "seal");
        goto done;
    }

    status = pact_cli_write_stdout(output, output_size);

done:
    free(owned_input);
    kc_pact_free(output);
    return status;
}

/**
 * Runs the pact CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv)
{
    kc_pact_t *trust = NULL;
    const char *input_path = NULL;
    const char *positionals[4];
    int positional_count = 0;
    int list = 0;
    int delete_entry = 0;
    int status = 1;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        pact_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_pact_version());
        return 0;
    }
    if (argc < 2) {
        pact_cli_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            if (list) {
                fprintf(stderr, "pact: list specified more than once\n");
                return 1;
            }
            list = 1;
        } else if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--delete") == 0)
        {
            if (delete_entry) {
                fprintf(stderr, "pact: delete specified more than once\n");
                return 1;
            }
            delete_entry = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "pact: --input requires a file\n");
                return 1;
            }
            if (input_path) {
                fprintf(stderr, "pact: only one input file is accepted\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "pact: -h/--help and -v/--version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "pact: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (positional_count < 4) {
            positionals[positional_count++] = argv[i];
        } else {
            fprintf(stderr, "pact: too many arguments\n");
            return 1;
        }
    }

    if (list && delete_entry) {
        fprintf(stderr, "pact: list and delete cannot be combined\n");
        return 1;
    }
    if ((list || delete_entry) && input_path) {
        fprintf(stderr, "pact: --input is only valid with seal or unseal\n");
        return 1;
    }

    if (kc_pact_init(&trust) != KC_PACT_OK) {
        fprintf(stderr, "pact: cannot initialize local state\n");
        return 1;
    }

    if (list) {
        if (positional_count > 1) {
            fprintf(stderr, "pact: list accepts at most one uid\n");
        } else {
            status = pact_cli_list(positional_count == 1 ? positionals[0] : NULL);
        }
    } else if (delete_entry) {
        if (positional_count != 1) {
            fprintf(stderr, "pact: delete requires one uid\n");
        } else if (kc_pact_revoke(trust, positionals[0]) != KC_PACT_OK) {
            fprintf(stderr, "pact: delete failed\n");
        } else {
            status = 0;
        }
    } else if (positional_count == 0) {
        fprintf(stderr, "pact: missing command\n");
    } else if (strcmp(positionals[0], "invite") == 0) {
        if (positional_count != 1 || input_path) {
            fprintf(stderr, "pact: invite takes no arguments\n");
        } else {
            status = pact_cli_invite(trust);
        }
    } else if (strcmp(positionals[0], "join") == 0) {
        if (positional_count != 2 || input_path) {
            fprintf(stderr, "pact: join requires one invitation code\n");
        } else {
            status = pact_cli_join(trust, positionals[1]);
        }
    } else if (strcmp(positionals[0], "confirm") == 0) {
        if (positional_count != 2 || input_path) {
            fprintf(stderr, "pact: confirm requires one confirmation\n");
        } else {
            status = pact_cli_confirm(trust, positionals[1]);
        }
    } else if (strcmp(positionals[0], "seal") == 0 ||
        strcmp(positionals[0], "unseal") == 0)
    {
        if (positional_count < 2 || positional_count > 3) {
            fprintf(stderr, "pact: %s requires <uid> and optional data\n",
                positionals[0]);
        } else {
            status = pact_cli_transform(
                trust,
                strcmp(positionals[0], "unseal") == 0,
                positionals[1],
                positional_count == 3 ? positionals[2] : NULL,
                input_path
            );
        }
    } else {
        fprintf(stderr, "pact: unknown command '%s'\n", positionals[0]);
    }

    kc_pact_close(trust);
    return status;
}
