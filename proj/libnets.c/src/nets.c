/**
 * nets.c - Persistent outbound network relay command-line interface.
 * Summary: Relays binary standard streams through one TCP or UDP endpoint.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnets.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <conio.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#endif

#define NETS_CLI_BUFFER_SIZE 16384U
#define NETS_CLI_STDIN_NONE 0
#define NETS_CLI_STDIN_READY 1
#define NETS_CLI_STDIN_CLOSED 2

typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
} nets_cli_options_t;

typedef struct {
    atomic_int terminal;
    atomic_int error_status;
    atomic_int io_failed;
} nets_cli_state_t;

static volatile sig_atomic_t nets_cli_stop;

/**
 * Prints command usage.
 * @param name Program name.
 * @return void.
 */
static void nets_cli_help(const char *name)
{
    printf("Usage: %s <host> <port> [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  -t, --transport <type>  tcp or udp (default: tcp)\n");
    printf("  -h, --help              Show this help\n");
    printf("  -v, --version           Show version\n");
}

/**
 * Prints the linked libnets build version.
 * @return void.
 */
static void nets_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_nets_version());
}

/**
 * Prints one CLI error.
 * @param message Error message.
 * @return One.
 */
static int nets_cli_error(const char *message)
{
    fprintf(stderr, "nets: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error with a value.
 * @param message Error prefix.
 * @param value Error value.
 * @return One.
 */
static int nets_cli_error_value(const char *message, const char *value)
{
    fprintf(stderr, "nets: %s%s\n", message, value ? value : "");
    return 1;
}

/**
 * Parses one nonzero network port.
 * @param text Port text.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int nets_cli_parse_port(const char *text, unsigned short *out)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out || text[0] == '-') return 0;
    errno = 0;
    end = NULL;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value == 0UL || value > 65535UL) {
        return 0;
    }
    *out = (unsigned short)value;
    return 1;
}

/**
 * Reads one required option value.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param index Current option index, advanced on success.
 * @param option Option name for diagnostics.
 * @param out Destination value.
 * @return Zero on success.
 */
static int nets_cli_option_value(
    int argc,
    char **argv,
    int *index,
    const char *option,
    const char **out
)
{
    if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
        fprintf(stderr, "nets: %s requires a value\n", option);
        return 1;
    }
    *index += 1;
    *out = argv[*index];
    return 0;
}

/**
 * Parses the outbound endpoint command contract.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param options Parsed options.
 * @return Zero on success.
 */
static int nets_cli_parse(
    int argc,
    char **argv,
    nets_cli_options_t *options
)
{
    const char *positionals[2];
    size_t positional_count;
    int index;

    if (!options) return nets_cli_error("invalid parser state");
    memset(options, 0, sizeof(*options));
    options->protocol = KC_NETS_TCP;
    positional_count = 0U;

    for (index = 1; index < argc; index++) {
        const char *argument;

        argument = argv[index];
        if (
            strcmp(argument, "-t") == 0 ||
            strcmp(argument, "--transport") == 0
        ) {
            const char *value;

            value = NULL;
            if (
                nets_cli_option_value(
                    argc,
                    argv,
                    &index,
                    argument,
                    &value
                ) != 0
            ) {
                return 1;
            }
            if (strcmp(value, "tcp") == 0) {
                options->protocol = KC_NETS_TCP;
            } else if (strcmp(value, "udp") == 0) {
                options->protocol = KC_NETS_UDP;
            } else {
                return nets_cli_error_value("invalid transport: ", value);
            }
        } else if (argument[0] == '-') {
            return nets_cli_error_value("unknown option: ", argument);
        } else {
            if (
                positional_count >=
                sizeof(positionals) / sizeof(positionals[0])
            ) {
                return nets_cli_error("too many positional arguments");
            }
            positionals[positional_count++] = argument;
        }
    }

    if (positional_count != 2U) {
        return nets_cli_error("requires <host> <port>");
    }
    if (!positionals[0][0]) return nets_cli_error("host must not be empty");
    if (!nets_cli_parse_port(positionals[1], &options->port)) {
        return nets_cli_error_value("invalid port: ", positionals[1]);
    }
    options->host = positionals[0];
    return 0;
}

/**
 * Writes all received bytes to standard output.
 * @param data Network bytes.
 * @param size Byte count.
 * @return Zero on success.
 */
static int nets_cli_write_stdout(const void *data, size_t size)
{
    const unsigned char *bytes;
    size_t offset;

    bytes = (const unsigned char *)data;
    offset = 0U;
    while (offset < size) {
        size_t part;
#ifdef _WIN32
        int result;
#else
        ssize_t result;
#endif

        part = size - offset;
        if (part > (size_t)INT_MAX) part = (size_t)INT_MAX;
#ifdef _WIN32
        result = _write(_fileno(stdout), bytes + offset, (unsigned int)part);
        if (result < 0) return 1;
#else
        result = write(STDOUT_FILENO, bytes + offset, part);
        if (result < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
#endif
        if (result <= 0) return 1;
        offset += (size_t)result;
    }
    return 0;
}

/**
 * Reads one binary chunk from standard input.
 * @param data Destination buffer.
 * @param size Destination size.
 * @return Byte count, zero on EOF, or negative on error.
 */
static int nets_cli_read_stdin(void *data, size_t size)
{
    for (;;) {
#ifdef _WIN32
        int result;

        result = _read(_fileno(stdin), data, (unsigned int)size);
        return result < 0 ? -1 : result;
#else
        ssize_t result;

        result = read(STDIN_FILENO, data, size);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        return (int)result;
#endif
    }
}

/**
 * Configures binary standard streams where required.
 * @return Zero on success.
 */
static int nets_cli_prepare_stdio(void)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) return 1;
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
#endif
    return 0;
}

#ifdef _WIN32
/**
 * Checks whether Windows standard input can be read without blocking.
 * @param closed Set when redirected standard input reached EOF.
 * @return Nonzero when input is ready.
 */
static int nets_cli_windows_stdin_ready(int *closed)
{
    HANDLE handle;
    DWORD type;

    *closed = 0;
    handle = (HANDLE)_get_osfhandle(_fileno(stdin));
    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        *closed = 1;
        return 0;
    }
    type = GetFileType(handle);
    if (type == FILE_TYPE_PIPE) {
        DWORD available;

        available = 0;
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
            DWORD error;

            error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) {
                *closed = 1;
            }
            return 0;
        }
        return available != 0U;
    }
    if (type == FILE_TYPE_DISK) return 1;
    if (type == FILE_TYPE_CHAR) {
        DWORD mode;

        if (!GetConsoleMode(handle, &mode)) return 1;
        return _kbhit() != 0;
    }
    return 0;
}
#endif

/**
 * Waits briefly for standard-input activity.
 * @return NETS_CLI_STDIN_* state, or negative on failure.
 */
static int nets_cli_wait_stdin(void)
{
#ifdef _WIN32
    int closed;

    if (nets_cli_windows_stdin_ready(&closed)) return NETS_CLI_STDIN_READY;
    if (closed) return NETS_CLI_STDIN_CLOSED;
    Sleep(100U);
    return NETS_CLI_STDIN_NONE;
#else
    fd_set readable;
    struct timeval timeout;
    int result;

    for (;;) {
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        result = select(
            STDIN_FILENO + 1,
            &readable,
            NULL,
            NULL,
            &timeout
        );
        if (result >= 0) break;
        if (errno != EINTR) return -1;
        if (nets_cli_stop) return NETS_CLI_STDIN_NONE;
    }
    if (result == 0) return NETS_CLI_STDIN_NONE;
    return FD_ISSET(STDIN_FILENO, &readable)
        ? NETS_CLI_STDIN_READY
        : NETS_CLI_STDIN_NONE;
#endif
}

/**
 * Waits briefly while standard input is closed.
 * @return void.
 */
static void nets_cli_idle(void)
{
#ifdef _WIN32
    Sleep(100U);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Handles process interruption.
 * @param signal_number Signal number.
 * @return void.
 */
static void nets_cli_signal(int signal_number)
{
    (void)signal_number;
    nets_cli_stop = 1;
}

/**
 * Writes one incoming network chunk to standard output.
 * @param data Received bytes.
 * @param data_size Byte count.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_receive(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    if (nets_cli_write_stdout(data, data_size) != 0) {
        atomic_store(&state->io_failed, 1);
        atomic_store(&state->terminal, 1);
    }
}

/**
 * Marks one TCP remote disconnect as terminal.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_disconnect(void *userdata)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    atomic_store(&state->terminal, 1);
}

/**
 * Stores one asynchronous network failure.
 * @param status Public network status.
 * @param userdata Relay state.
 * @return void.
 */
static void nets_cli_network_error(int status, void *userdata)
{
    nets_cli_state_t *state;

    state = (nets_cli_state_t *)userdata;
    atomic_store(&state->error_status, status);
    atomic_store(&state->terminal, 1);
}

/**
 * Runs one persistent endpoint relay.
 * @param options Parsed command options.
 * @return Process exit status.
 */
static int nets_cli_run(const nets_cli_options_t *options)
{
    unsigned char buffer[NETS_CLI_BUFFER_SIZE];
    kc_nets_options_t network_options;
    kc_nets_t *network;
    nets_cli_state_t state;
    int stdin_open;
    int local_failure;
    int status;

    memset(&state, 0, sizeof(state));
    atomic_init(&state.terminal, 0);
    atomic_init(&state.error_status, KC_NETS_OK);
    atomic_init(&state.io_failed, 0);
    memset(&network_options, 0, sizeof(network_options));
    network_options.host = options->host;
    network_options.port = options->port;
    network_options.protocol = options->protocol;
    network = NULL;

    status = kc_nets_open(
        &network,
        &network_options,
        nets_cli_receive,
        nets_cli_disconnect,
        nets_cli_network_error,
        &state
    );
    if (status != KC_NETS_OK) {
        return nets_cli_error_value("open failed: ", kc_nets_strerror(status));
    }

    stdin_open = 1;
    local_failure = 0;
    while (!nets_cli_stop && !atomic_load(&state.terminal)) {
        if (stdin_open) {
            int event;

            event = nets_cli_wait_stdin();
            if (event < 0) {
                local_failure = nets_cli_error("standard input wait failed");
                break;
            }
            if (event == NETS_CLI_STDIN_CLOSED) {
                stdin_open = 0;
                continue;
            }
            if (event == NETS_CLI_STDIN_READY) {
                int size;

                size = nets_cli_read_stdin(buffer, sizeof(buffer));
                if (size < 0) {
                    local_failure = nets_cli_error("standard input read failed");
                    break;
                }
                if (size == 0) {
                    stdin_open = 0;
                    continue;
                }
                status = kc_nets_send(network, buffer, (size_t)size);
                if (status != KC_NETS_OK) {
                    if (
                        status == KC_NETS_ECLOSED &&
                        atomic_load(&state.terminal)
                    ) {
                        break;
                    }
                    local_failure = nets_cli_error_value(
                        "send failed: ",
                        kc_nets_strerror(status)
                    );
                    break;
                }
            }
        } else {
            nets_cli_idle();
        }
    }

    kc_nets_close(network);
    if (local_failure != 0) return local_failure;
    if (atomic_load(&state.io_failed)) {
        return nets_cli_error("standard output write failed");
    }
    status = atomic_load(&state.error_status);
    if (status != KC_NETS_OK) {
        return nets_cli_error_value("network error: ", kc_nets_strerror(status));
    }
    return 0;
}

/**
 * Runs metadata handling or one endpoint relay.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    nets_cli_options_t options;
    int index;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
            nets_cli_help(argv[0]);
            return 0;
        }
        if (
            strcmp(argv[index], "-v") == 0 ||
            strcmp(argv[index], "--version") == 0
        ) {
            nets_cli_version();
            return 0;
        }
    }

    if (nets_cli_parse(argc, argv, &options) != 0) return 1;
    if (nets_cli_prepare_stdio() != 0) {
        return nets_cli_error("standard stream setup failed");
    }

    nets_cli_stop = 0;
    (void)signal(SIGINT, nets_cli_signal);
    (void)signal(SIGTERM, nets_cli_signal);
    return nets_cli_run(&options);
}
