/**
 * pow.c - Proof-of-work command-line interface.
 * Summary: Creates, solves, and verifies portable libpow entities.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libpow.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * Prints the CLI usage message.
 * @param name Program name.
 * @return None.
 */
static void pow_cli_usage(const char *name)
{
    printf("Usage: %s <challenge|solve|verify> [arguments] [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -i, --input <file>      Read command input from file\n");
    printf("  -c, --challenge <file>  Read verify challenge from file\n");
    printf("  -b, --backend <name>    Select challenge backend: auto, sha256, hashwx\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Checks if stdin is a TTY.
 * @return 1 if stdin is a TTY, 0 otherwise.
 */
static int pow_cli_stdin_is_tty(void)
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
 * Reads all data from a stream into a buffer.
 * @param stream Input stream.
 * @param out Output buffer pointer.
 * @param out_size Output buffer size.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_read_stream(FILE *stream, void **out, size_t *out_size)
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
 * Reads a file into a buffer.
 * @param path File path.
 * @param out Output buffer pointer.
 * @param out_size Output buffer size.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_read_file(const char *path, void **out, size_t *out_size)
{
    FILE *file;
    int status;

    if (!path) return 1;
    file = fopen(path, "rb");
    if (!file) return 1;
    status = pow_cli_read_stream(file, out, out_size);
    fclose(file);
    return status;
}

/**
 * Reads input from stdin or a file.
 * @param path Optional file path (used if stdin is a TTY).
 * @param out Output buffer pointer.
 * @param out_size Output buffer size.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_read_input(const char *path, void **out, size_t *out_size)
{
    if (!pow_cli_stdin_is_tty()) {
#ifdef _WIN32
        if (_setmode(_fileno(stdin), _O_BINARY) == -1) return 1;
#endif
        if (pow_cli_read_stream(stdin, out, out_size)) return 1;
        if (*out_size > 0 || !path) return 0;
        free(*out);
        *out = NULL;
    }
    if (path) return pow_cli_read_file(path, out, out_size);
    return 1;
}

/**
 * Writes binary data to stdout.
 * @param data Data to write.
 * @param size Data size.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_write_stdout(const void *data, size_t size)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    if (size > 0 && fwrite(data, 1, size, stdout) != size) return 1;
    return fflush(stdout) == 0 ? 0 : 1;
}

/**
 * Parses an effort value from a string.
 * @param text Input string.
 * @param out Output effort value.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_parse_effort(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (!text || !text[0] || !out || text[0] == '-') return 1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0) return 1;
    *out = (uint64_t)value;
    return 0;
}

/**
 * Executes the challenge command.
 * @param effort_text Effort string to parse.
 * @param backend_name Optional requested backend name.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_challenge(const char *effort_text, const char *backend_name)
{
    kc_pow_backend_t backend;
    uint64_t effort;
    void *challenge = NULL;
    size_t challenge_size = 0;
    int status = 1;

    if (pow_cli_parse_effort(effort_text, &effort)) {
        fprintf(stderr, "pow: invalid effort\n");
        return 1;
    }
    if (!backend_name) {
        status = kc_pow_challenge(effort, &challenge, &challenge_size);
    } else {
        backend = kc_pow_backend_from_name(backend_name);
        if (backend == KC_POW_BACKEND_INVALID) {
            fprintf(stderr, "pow: invalid backend: %s\n", backend_name);
            return 1;
        }
        if (!kc_pow_backend_available(backend)) {
            fprintf(stderr, "pow: backend unavailable: %s\n", backend_name);
            return 1;
        }
        status = kc_pow_challenge_with_backend(backend, effort, &challenge,
            &challenge_size);
    }
    if (status != KC_POW_OK) {
        fprintf(stderr, "pow: challenge failed\n");
        goto done;
    }
    status = pow_cli_write_stdout(challenge, challenge_size);
    if (status) fprintf(stderr, "pow: cannot write challenge\n");

done:
    kc_pow_free(challenge);
    return status;
}

/**
 * Executes the solve command.
 * @param input_path Optional challenge input file path.
 * @return 0 on success, 1 on failure.
 */
static int pow_cli_solve(const char *input_path)
{
    void *challenge = NULL;
    size_t challenge_size = 0;
    void *solution = NULL;
    size_t solution_size = 0;
    int status = 1;

    if (pow_cli_read_input(input_path, &challenge, &challenge_size)) {
        fprintf(stderr, "pow: cannot read challenge\n");
        return 1;
    }
    if (kc_pow_solve(challenge, challenge_size,
        &solution, &solution_size) != KC_POW_OK)
    {
        fprintf(stderr, "pow: solve failed\n");
        goto done;
    }
    status = pow_cli_write_stdout(solution, solution_size);
    if (status) fprintf(stderr, "pow: cannot write solution\n");

done:
    free(challenge);
    kc_pow_free(solution);
    return status;
}

/**
 * Executes the verify command.
 * @param challenge_path Challenge file path.
 * @param input_path Optional solution input file path.
 * @return 0 on valid, 1 on invalid or error.
 */
static int pow_cli_verify(const char *challenge_path, const char *input_path)
{
    void *challenge = NULL;
    size_t challenge_size = 0;
    void *solution = NULL;
    size_t solution_size = 0;
    int rc;

    if (!challenge_path) {
        fprintf(stderr, "pow: verify requires --challenge <file>\n");
        return 1;
    }
    if (pow_cli_read_file(challenge_path, &challenge, &challenge_size)) {
        fprintf(stderr, "pow: cannot read challenge\n");
        return 1;
    }
    if (pow_cli_read_input(input_path, &solution, &solution_size)) {
        fprintf(stderr, "pow: cannot read solution\n");
        free(challenge);
        return 1;
    }

    rc = kc_pow_verify(challenge, challenge_size, solution, solution_size);
    free(solution);
    free(challenge);
    if (rc == KC_POW_OK) return 0;
    if (rc == KC_POW_INVALID) {
        fprintf(stderr, "pow: invalid solution\n");
        return 1;
    }
    fprintf(stderr, "pow: verify failed\n");
    return 1;
}

/**
 * Main entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char **argv)
{
    const char *command = NULL;
    const char *input_path = NULL;
    const char *challenge_path = NULL;
    const char *backend_name = NULL;
    const char *positionals[2];
    int positional_count = 0;

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0))
    {
        pow_cli_usage(argv[0]);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "-v") == 0 ||
        strcmp(argv[1], "--version") == 0))
    {
        printf("%llu\n", (unsigned long long)kc_pow_version());
        return 0;
    }
    if (argc < 2) {
        pow_cli_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (++i >= argc || !argv[i][0] || input_path) {
                fprintf(stderr, "pow: --input requires one file\n");
                return 1;
            }
            input_path = argv[i];
        } else if (strcmp(argv[i], "-c") == 0 ||
            strcmp(argv[i], "--challenge") == 0)
        {
            if (++i >= argc || !argv[i][0] || challenge_path) {
                fprintf(stderr, "pow: --challenge requires one file\n");
                return 1;
            }
            challenge_path = argv[i];
        } else if (strcmp(argv[i], "-b") == 0 ||
            strcmp(argv[i], "--backend") == 0)
        {
            if (++i >= argc || !argv[i][0] || backend_name) {
                fprintf(stderr, "pow: --backend requires one name\n");
                return 1;
            }
            backend_name = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "--version") == 0)
        {
            fprintf(stderr, "pow: -h/--help and -v/--version must be used alone\n");
            return 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "pow: unknown option: %s\n", argv[i]);
            return 1;
        } else if (!command) {
            command = argv[i];
        } else if (positional_count < 2) {
            positionals[positional_count++] = argv[i];
        } else {
            fprintf(stderr, "pow: too many arguments\n");
            return 1;
        }
    }

    if (!command) {
        fprintf(stderr, "pow: missing command\n");
        return 1;
    }
    if (backend_name && strcmp(command, "challenge") != 0) {
        fprintf(stderr, "pow: --backend is only valid for challenge\n");
        return 1;
    }
    if (strcmp(command, "challenge") == 0) {
        if (positional_count != 1 || input_path || challenge_path) {
            fprintf(stderr, "pow: challenge requires one effort\n");
            return 1;
        }
        return pow_cli_challenge(positionals[0], backend_name);
    }
    if (strcmp(command, "solve") == 0) {
        if (positional_count != 0 || challenge_path) {
            fprintf(stderr, "pow: solve takes no arguments\n");
            return 1;
        }
        return pow_cli_solve(input_path);
    }
    if (strcmp(command, "verify") == 0) {
        if (positional_count != 0) {
            fprintf(stderr, "pow: verify takes no positional arguments\n");
            return 1;
        }
        return pow_cli_verify(challenge_path, input_path);
    }

    fprintf(stderr, "pow: unknown command '%s'\n", command);
    return 1;
}
