/**
 * ngram.c
 * Summary: Portable CLI wrapper for libngram (POSIX + Windows).
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "libngram.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct {
    const char *command;
} kc_ngram_cli_context_t;

typedef struct {
    char **items;
    int count;
} kc_ngram_arg_list_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} kc_ngram_string_t;

/**
 * Duplicates one string into heap memory.
 * @param text Source string.
 * @return Heap copy, or NULL on failure.
 */
static char *kc_ngram_strdup(const char *text) {
    size_t size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    size = strlen(text) + 1U;
    copy = (char *)malloc(size);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, size);
    return copy;
}

/**
 * Releases all parsed command arguments.
 * @param args Argument list.
 * @return No return value.
 */
static void kc_ngram_free_args(kc_ngram_arg_list_t *args) {
    int i;

    if (args == NULL) {
        return;
    }

    for (i = 0; i < args->count; i++) {
        free(args->items[i]);
    }

    free(args->items);
    args->items = NULL;
    args->count = 0;
}

/**
 * Returns the exact byte length for one chunk span.
 * @param chunk Current chunk.
 * @return Span length in bytes.
 */
static size_t kc_ngram_chunk_length(const kc_ngram_chunk_t *chunk) {
    if (chunk == NULL || chunk->data == NULL) {
        return 0U;
    }

    return chunk->data_size;
}

/**
 * Returns the exact byte pointer for one chunk span.
 * @param chunk Current chunk.
 * @return Pointer to the first byte of the span, or NULL on invalid input.
 */
static const char *kc_ngram_chunk_data(const kc_ngram_chunk_t *chunk) {
    if (chunk == NULL) {
        return NULL;
    }

    return chunk->data;
}

/**
 * Releases one dynamic string buffer.
 * @param string Buffer to release.
 * @return No return value.
 */
static void kc_ngram_string_free(kc_ngram_string_t *string) {
    if (string == NULL) {
        return;
    }

    free(string->data);
    string->data = NULL;
    string->length = 0U;
    string->capacity = 0U;
}

/**
 * Ensures one dynamic string buffer can hold the required size.
 * @param string Buffer to grow.
 * @param required Required size including trailing NUL.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_string_reserve(
    kc_ngram_string_t *string,
    size_t required
) {
    char *next_data;
    size_t next_capacity;

    if (string == NULL) {
        return -1;
    }

    if (required <= string->capacity) {
        return 0;
    }

    next_capacity = string->capacity > 0U ? string->capacity : 4096U;
    while (next_capacity < required) {
        next_capacity *= 2U;
    }

    next_data = (char *)realloc(string->data, next_capacity);
    if (next_data == NULL) {
        return -1;
    }

    string->data = next_data;
    string->capacity = next_capacity;
    return 0;
}

/**
 * Appends one byte into a dynamic token buffer.
 * @param buffer Destination token buffer.
 * @param length Current used length.
 * @param capacity Current allocated capacity.
 * @param ch Byte to append.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_char(
    char **buffer,
    size_t *length,
    size_t *capacity,
    char ch
) {
    char *next_buffer;
    size_t next_capacity;

    if (buffer == NULL || length == NULL || capacity == NULL) {
        return -1;
    }

    if (*length + 1U >= *capacity) {
        next_capacity = *capacity ? (*capacity * 2U) : 32U;
        next_buffer = (char *)realloc(*buffer, next_capacity);
        if (next_buffer == NULL) {
            return -1;
        }

        *buffer = next_buffer;
        *capacity = next_capacity;
    }

    (*buffer)[*length] = ch;
    (*length)++;
    return 0;
}

/**
 * Appends one parsed argument into the command list.
 * @param args Destination argument list.
 * @param text Argument text.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_push_arg(kc_ngram_arg_list_t *args, const char *text) {
    char **next_items;
    char *copy;

    if (args == NULL || text == NULL || *text == '\0') {
        return -1;
    }

    next_items = (char **)realloc(
        args->items,
        (size_t)(args->count + 1) * sizeof(char *)
    );
    if (next_items == NULL) {
        return -1;
    }

    copy = kc_ngram_strdup(text);
    if (copy == NULL) {
        return -1;
    }

    args->items = next_items;
    args->items[args->count] = copy;
    args->count++;
    return 0;
}

/**
 * Parses one command string into executable arguments.
 * @param command Raw command string.
 * @param args Destination argument list.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_parse_command(
    const char *command,
    kc_ngram_arg_list_t *args
) {
    char *token;
    size_t length;
    size_t capacity;
    int in_single;
    int in_double;
    int had_text;
    const char *cursor;

    if (command == NULL || args == NULL) {
        return -1;
    }

    args->items = NULL;
    args->count = 0;

    token = NULL;
    length = 0U;
    capacity = 0U;
    in_single = 0;
    in_double = 0;
    had_text = 0;
    cursor = command;

    while (*cursor != '\0') {
        char ch;

        ch = *cursor;

        if (!in_single && ch == '\\') {
            cursor++;
            if (*cursor == '\0') {
                free(token);
                kc_ngram_free_args(args);
                return -1;
            }

            if (kc_ngram_push_char(&token, &length, &capacity, *cursor) != 0) {
                free(token);
                kc_ngram_free_args(args);
                return -1;
            }

            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_double && ch == '\'') {
            in_single = !in_single;
            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_single && ch == '"') {
            in_double = !in_double;
            had_text = 1;
            cursor++;
            continue;
        }

        if (!in_single && !in_double) {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                if (had_text) {
                    if (kc_ngram_push_char(&token, &length, &capacity, '\0') != 0) {
                        free(token);
                        kc_ngram_free_args(args);
                        return -1;
                    }

                    length--;
                    if (kc_ngram_push_arg(args, token) != 0) {
                        free(token);
                        kc_ngram_free_args(args);
                        return -1;
                    }

                    length = 0U;
                    had_text = 0;
                }

                cursor++;
                continue;
            }
        }

        if (kc_ngram_push_char(&token, &length, &capacity, ch) != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }

        had_text = 1;
        cursor++;
    }

    if (in_single || in_double) {
        free(token);
        kc_ngram_free_args(args);
        return -1;
    }

    if (had_text) {
        if (kc_ngram_push_char(&token, &length, &capacity, '\0') != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }

        length--;
        if (kc_ngram_push_arg(args, token) != 0) {
            free(token);
            kc_ngram_free_args(args);
            return -1;
        }
    }

    free(token);

    if (args->count == 0) {
        kc_ngram_free_args(args);
        return -1;
    }

    return 0;
}

#ifdef _WIN32

/**
 * Builds one Windows command line string from parsed arguments.
 * @param args Parsed argument list.
 * @param out_command_line Destination command line buffer.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_build_windows_command_line(
    const kc_ngram_arg_list_t *args,
    char **out_command_line
) {
    char *command_line;
    size_t length;
    size_t capacity;
    int i;

    if (args == NULL || out_command_line == NULL || args->count < 1) {
        return -1;
    }

    command_line = NULL;
    length = 0U;
    capacity = 0U;

    for (i = 0; i < args->count; i++) {
        const char *arg;
        size_t j;
        int needs_quotes;

        arg = args->items[i];
        if (arg == NULL) {
            free(command_line);
            return -1;
        }

        if (i > 0) {
            if (kc_ngram_push_char(&command_line, &length, &capacity, ' ') != 0) {
                free(command_line);
                return -1;
            }
        }

        needs_quotes = (*arg == '\0');
        for (j = 0U; arg[j] != '\0'; j++) {
            if (arg[j] == ' ' || arg[j] == '\t' || arg[j] == '"') {
                needs_quotes = 1;
                break;
            }
        }

        if (!needs_quotes) {
            for (j = 0U; arg[j] != '\0'; j++) {
                if (kc_ngram_push_char(&command_line, &length, &capacity, arg[j]) != 0) {
                    free(command_line);
                    return -1;
                }
            }

            continue;
        }

        if (kc_ngram_push_char(&command_line, &length, &capacity, '"') != 0) {
            free(command_line);
            return -1;
        }

        for (j = 0U; ; ) {
            size_t slash_count;
            char ch;

            slash_count = 0U;
            while (arg[j] == '\\') {
                slash_count++;
                j++;
            }

            ch = arg[j];
            if (ch == '\0') {
                while (slash_count > 0U) {
                    if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                        free(command_line);
                        return -1;
                    }

                    if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                        free(command_line);
                        return -1;
                    }

                    slash_count--;
                }

                break;
            }

            if (ch == '"') {
                while (slash_count > 0U) {
                    if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                        free(command_line);
                        return -1;
                    }

                    if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                        free(command_line);
                        return -1;
                    }

                    slash_count--;
                }

                if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                    free(command_line);
                    return -1;
                }

                if (kc_ngram_push_char(&command_line, &length, &capacity, '"') != 0) {
                    free(command_line);
                    return -1;
                }

                j++;
                continue;
            }

            while (slash_count > 0U) {
                if (kc_ngram_push_char(&command_line, &length, &capacity, '\\') != 0) {
                    free(command_line);
                    return -1;
                }

                slash_count--;
            }

            if (kc_ngram_push_char(&command_line, &length, &capacity, ch) != 0) {
                free(command_line);
                return -1;
            }

            j++;
        }

        if (kc_ngram_push_char(&command_line, &length, &capacity, '"') != 0) {
            free(command_line);
            return -1;
        }
    }

    if (kc_ngram_push_char(&command_line, &length, &capacity, '\0') != 0) {
        free(command_line);
        return -1;
    }

    *out_command_line = command_line;
    return 0;
}

#endif

/**
 * Reads text from standard input into the provided buffer.
 * @param out_text Destination pointer for the allocated text.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_read_stdin(char **out_text) {
    kc_ngram_string_t input;
    char chunk[4096];
    size_t n;

    if (out_text == NULL) {
        return -1;
    }

    input.data = NULL;
    input.length = 0U;
    input.capacity = 0U;

    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0U) {
        if (
            kc_ngram_string_reserve(
                &input,
                input.length + n + 1U
            ) != 0
        ) {
            kc_ngram_string_free(&input);
            return -1;
        }

        memcpy(input.data + input.length, chunk, n);
        input.length += n;
    }

    if (ferror(stdin)) {
        kc_ngram_string_free(&input);
        return -1;
    }

    if (input.length == 0U) {
        kc_ngram_string_free(&input);
        *out_text = NULL;
        return 0;
    }

    input.data[input.length] = '\0';
    *out_text = input.data;
    return 0;
}

/**
 * Parses one size_t CLI value.
 * @param text Input text.
 * @param out Output size_t pointer.
 * @return 1 on success, or 0 on failure.
 */
static int kc_ngram_parse_sizet(const char *text, size_t *out) {
    const unsigned char *cursor;
    unsigned long long value;
    char *end;

    if (text == NULL || out == NULL) {
        return 0;
    }

    cursor = (const unsigned char *)text;
    while (
        *cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
        *cursor == '\r' || *cursor == '\v' || *cursor == '\f'
    ) {
        cursor++;
    }

    if (*cursor == '-') {
        return 0;
    }

    errno = 0;
    value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    if (value > (unsigned long long)SIZE_MAX) {
        return 0;
    }

    *out = (size_t)value;
    return 1;
}

/**
 * Prints compact command help.
 * @return No return value.
 */
static void kc_ngram_help(void) {
    printf("Usage:\n");
    printf("  ngram [options] [text]\n\n");
    printf("Options:\n");
    printf("  --max, -max <n>     Maximum tokens per block\n");
    printf("  --min, -min <n>     Minimum tokens per block\n");
    printf("  --sep, -sep      Custom separator characters\n");
    printf("  --cmd, -cmd <cmd>   Execute command for each chunk\n");
    printf("  -h, --help          Show help\n");
    printf("  -v, --version       Show version\n");
}

/**
 * Prints usage error.
 * @param message Error text.
 * @return Exit code.
 */
static int kc_ngram_fail_usage(const char *message) {
    fprintf(stderr, "ngram: %s\n\n", message);
    kc_ngram_help();
    return 1;
}

#ifndef _WIN32

/**
 * Writes all bytes to one file descriptor.
 * @param fd Destination descriptor.
 * @param text Source buffer.
 * @param size Number of bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(int fd, const char *text, size_t size) {
    while (size > 0U) {
        ssize_t written;

        written = write(fd, text, size);

        if (written <= 0) {
            return -1;
        }

        text += (size_t)written;
        size -= (size_t)written;
    }

    return 0;
}

/**
 * Executes one command on POSIX systems.
 * @param command Command string.
 * @param chunk Current chunk.
 * @return 1 to close span, 0 to keep open, -1 on error.
 */
static int kc_ngram_run_command(
    const char *command,
    const kc_ngram_chunk_t *chunk
) {
    int stdin_pipe[2];
    int stdout_pipe[2];
    int error_pipe[2];
    pid_t pid;
    int status;
    kc_ngram_arg_list_t args;
    char output_buffer[256];
    ssize_t read_count;
    ssize_t error_read;
    unsigned char error_marker;
    int has_stdout;
    int i;

    if (command == NULL || chunk == NULL) {
        return 0;
    }

    if (kc_ngram_parse_command(command, &args) != 0) {
        return -1;
    }

    if (pipe(stdin_pipe) != 0) {
        kc_ngram_free_args(&args);
        return -1;
    }

    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (pipe(error_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC) < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(error_pipe[0]);
        close(error_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(error_pipe[0]);
        close(error_pipe[1]);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (pid == 0) {
        char **argv;
        unsigned char marker = 1U;

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(error_pipe[0]);

        argv = (char **)calloc((size_t)args.count + 1U, sizeof(char *));
        if (argv == NULL) {
            (void)write(error_pipe[1], &marker, 1);
            _exit(127);
        }

        for (i = 0; i < args.count; i++) {
            argv[i] = args.items[i];
        }

        execvp(argv[0], argv);
        (void)write(error_pipe[1], &marker, 1);
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(error_pipe[1]);

    if (
        kc_ngram_write_all(
            stdin_pipe[1],
            kc_ngram_chunk_data(chunk),
            kc_ngram_chunk_length(chunk)
        ) != 0
    ) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(error_pipe[0]);
        waitpid(pid, NULL, 0);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (kc_ngram_write_all(stdin_pipe[1], "\n", 1U) != 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(error_pipe[0]);
        waitpid(pid, NULL, 0);
        kc_ngram_free_args(&args);
        return -1;
    }

    close(stdin_pipe[1]);

    has_stdout = 0;
    while ((read_count = read(stdout_pipe[0], output_buffer, sizeof(output_buffer))) > 0) {
        has_stdout = 1;
    }

    close(stdout_pipe[0]);

    if (waitpid(pid, &status, 0) < 0) {
        close(error_pipe[0]);
        kc_ngram_free_args(&args);
        return -1;
    }

    error_read = read(error_pipe[0], &error_marker, 1);
    close(error_pipe[0]);

    kc_ngram_free_args(&args);

    if (error_read < 0) {
        return -1;
    }

    if (error_read == 1) {
        return -1;
    }

    if (read_count < 0) {
        return -1;
    }

    if (has_stdout) {
        return 1;
    }

    return 0;
}

#else

/**
 * Writes all bytes to one Windows handle.
 * @param handle Destination handle.
 * @param text Source buffer.
 * @param size Number of bytes.
 * @return 0 on success, or -1 on failure.
 */
static int kc_ngram_write_all(
    HANDLE handle,
    const char *text,
    size_t size
) {
    while (size > 0U) {
        DWORD written;
        DWORD part;

        part = size > 0x7fffffffU ? 0x7fffffffU : (DWORD)size;

        if (!WriteFile(handle, text, part, &written, NULL)) {
            return -1;
        }

        if (written == 0U) {
            return -1;
        }

        text += written;
        size -= written;
    }

    return 0;
}

/**
 * Executes one command on Windows systems.
 * @param command Command string.
 * @param chunk Current chunk.
 * @return 1 to close span, 0 to keep open, -1 on error.
 */
static int kc_ngram_run_command(
    const char *command,
    const kc_ngram_chunk_t *chunk
) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE stdin_read;
    HANDLE stdin_write;
    HANDLE stdout_read;
    HANDLE stdout_write;
    kc_ngram_arg_list_t args;
    char *command_line;
    char output_buffer[256];
    DWORD bytes_read;
    DWORD error_code;
    int has_stdout;

    if (command == NULL || chunk == NULL) {
        return 0;
    }

    if (kc_ngram_parse_command(command, &args) != 0) {
        return -1;
    }

    if (kc_ngram_build_windows_command_line(&args, &command_line) != 0) {
        kc_ngram_free_args(&args);
        return -1;
    }

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (
        !CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi
        )
    ) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);

    if (
        kc_ngram_write_all(
            stdin_write,
            kc_ngram_chunk_data(chunk),
            kc_ngram_chunk_length(chunk)
        ) != 0
    ) {
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    if (kc_ngram_write_all(stdin_write, "\n", 1U) != 0) {
        CloseHandle(stdin_write);
        CloseHandle(stdout_read);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        free(command_line);
        kc_ngram_free_args(&args);
        return -1;
    }

    CloseHandle(stdin_write);

    has_stdout = 0;
    for (;;) {
        if (!ReadFile(stdout_read, output_buffer, sizeof(output_buffer), &bytes_read, NULL)) {
            error_code = GetLastError();
            if (error_code == ERROR_BROKEN_PIPE) {
                break;
            }

            CloseHandle(stdout_read);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            free(command_line);
            kc_ngram_free_args(&args);
            return -1;
        }

        if (bytes_read == 0U) {
            break;
        }

        has_stdout = 1;
    }

    CloseHandle(stdout_read);

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(command_line);
    kc_ngram_free_args(&args);

    if (has_stdout) {
        return 1;
    }

    return 0;
}

#endif

/**
 * Visitor for CLI that prints chunk and optionally runs command.
 * @param chunk Current chunk.
 * @param context CLI context with command.
 * @return Visitor decision: 0 continue, 1 close span, -1 abort.
 */
static int kc_ngram_cli_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    kc_ngram_cli_context_t *cli = (kc_ngram_cli_context_t *)context;
    size_t chunk_len;
    int cmd_result;

    if (chunk == NULL || cli == NULL) {
        return -1;
    }

    chunk_len = kc_ngram_chunk_length(chunk);
    if (chunk_len > 0) {
        if (fwrite(kc_ngram_chunk_data(chunk), 1, chunk_len, stdout) != chunk_len) {
            return -1;
        }
    }
    if (fputc('\n', stdout) == EOF) {
        return -1;
    }

    if (cli->command == NULL || *cli->command == '\0') {
        return 0;
    }

    cmd_result = kc_ngram_run_command(cli->command, chunk);
    if (cmd_result < 0) {
        return -1;
    }
    if (cmd_result == 1) {
        return 1;
    }

    return 0;
}

/**
 * Prints version via library API.
 * @return Exit status.
 */
static int kc_ngram_cli_version(void) {
    printf("ngram build %llu\n", (unsigned long long)kc_ngram_version());
    return 0;
}

/**
 * Standalone entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char **argv) {
    kc_ngram_options_t options = {0};
    kc_ngram_cli_context_t context;
    const char *text;
    char *stdin_text;
    size_t max_tokens;
    size_t min_tokens;
    int result;
    int i;

    context.command = NULL;
    text = NULL;
    stdin_text = NULL;
    max_tokens = 0U;
    min_tokens = 0U;
    result = 0;

    for (i = 1; i < argc; i++) {
        if (
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0
        ) {
            kc_ngram_help();
            result = 0;
            goto cleanup;
        }

        if (
            strcmp(argv[i], "--version") == 0 ||
            strcmp(argv[i], "-v") == 0
        ) {
            result = kc_ngram_cli_version();
            goto cleanup;
        }

        if (
            strcmp(argv[i], "--max") == 0 ||
            strcmp(argv[i], "-max") == 0
        ) {
            if (i + 1 >= argc) {
                result = kc_ngram_fail_usage("Missing value for --max.");
                goto cleanup;
            }

            if (!kc_ngram_parse_sizet(argv[i + 1], &max_tokens)) {
                result = kc_ngram_fail_usage("Invalid value for --max.");
                goto cleanup;
            }

            options.max_tokens = &max_tokens;
            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--min") == 0 ||
            strcmp(argv[i], "-min") == 0
        ) {
            if (i + 1 >= argc) {
                result = kc_ngram_fail_usage("Missing value for --min.");
                goto cleanup;
            }

            if (
                !kc_ngram_parse_sizet(argv[i + 1], &min_tokens) ||
                min_tokens == 0U
            ) {
                result = kc_ngram_fail_usage("Invalid value for --min.");
                goto cleanup;
            }

            options.min_tokens = &min_tokens;
            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--sep") == 0 ||
            strcmp(argv[i], "-sep") == 0
        ) {
            if (i + 1 >= argc) {
                result = kc_ngram_fail_usage("Missing value for --sep.");
                goto cleanup;
            }

            options.separators = argv[i + 1];
            i++;
            continue;
        }

        if (
            strcmp(argv[i], "--cmd") == 0 ||
            strcmp(argv[i], "-cmd") == 0
        ) {
            if (i + 1 >= argc) {
                result = kc_ngram_fail_usage("Missing value for --cmd.");
                goto cleanup;
            }

            context.command = argv[i + 1];
            i++;
            continue;
        }

        if (argv[i][0] == '-') {
            result = kc_ngram_fail_usage("Unknown argument.");
            goto cleanup;
        }

        if (text != NULL) {
            result = kc_ngram_fail_usage("Too many positional arguments.");
            goto cleanup;
        }

        text = argv[i];
    }

    if (text == NULL) {
        if (kc_ngram_read_stdin(&stdin_text) != 0) {
            result = 1;
            goto cleanup;
        }

        text = stdin_text;
    }

    if (text == NULL || *text == '\0') {
        goto cleanup;
    }

    result = kc_ngram_traverse(
        text,
        &options,
        kc_ngram_cli_visitor,
        &context
    );
    if (result < 0) {
        result = 1;
    } else {
        result = 0;
    }

cleanup:
    free(stdin_text);
    return result;
}
