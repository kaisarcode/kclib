/**
 * rstrm.c - Reliable stream command-line interface.
 * Summary: Relays binary standard streams over a reliable UDP-backed stream.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "librstrm.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET rstrm_cli_socket_t;
typedef int rstrm_cli_socklen_t;
#define RSTRM_CLI_INVALID_SOCKET INVALID_SOCKET
#define rstrm_cli_socket_close closesocket
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int rstrm_cli_socket_t;
typedef socklen_t rstrm_cli_socklen_t;
#define RSTRM_CLI_INVALID_SOCKET (-1)
#define rstrm_cli_socket_close close
#endif

#define RSTRM_CLI_BUFFER_SIZE 16384U
#define RSTRM_CLI_DATAGRAM_SIZE 65536U
#define RSTRM_CLI_STDIN_NONE 0
#define RSTRM_CLI_STDIN_READY 1
#define RSTRM_CLI_STDIN_CLOSED 2

typedef struct {
    const char *peer_host;
    unsigned short peer_port;
    const char *host;
    unsigned short port;
    size_t mtu;
} rstrm_cli_options_t;

typedef struct {
    rstrm_cli_socket_t socket_fd;
    struct sockaddr_storage peer_address;
    rstrm_cli_socklen_t peer_address_size;
    atomic_int remote_eof;
    atomic_int can_send;
    atomic_int error_status;
    atomic_int io_failed;
} rstrm_cli_state_t;

static volatile sig_atomic_t rstrm_cli_stop;

/**
 * Prints command usage.
 * @param name Program name.
 * @return void.
 */
static void rstrm_cli_help(const char *name)
{
    printf("Usage: %s <peer-host> <peer-port> [options]\n", name);
    printf("\n");
    printf("Options:\n");
    printf("  --host <host>       Local UDP bind host\n");
    printf("  -p, --port <port>   Local UDP port (default: 0)\n");
    printf("  --mtu <bytes>       Datagram MTU (default: library)\n");
    printf("  -h, --help          Show this help\n");
    printf("  -v, --version       Show version\n");
}

/**
 * Prints the linked librstrm build version.
 * @return void.
 */
static void rstrm_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_rstrm_version());
}

/**
 * Prints one CLI error.
 * @param message Error message.
 * @return One.
 */
static int rstrm_cli_error(const char *message)
{
    fprintf(stderr, "rstrm: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error with a value.
 * @param message Error prefix.
 * @param value Error value.
 * @return One.
 */
static int rstrm_cli_error_value(const char *message, const char *value)
{
    fprintf(stderr, "rstrm: %s%s\n", message, value ? value : "");
    return 1;
}

/**
 * Parses one UDP port.
 * @param text Port text.
 * @param allow_zero Whether zero is accepted.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int rstrm_cli_parse_port(
    const char *text,
    int allow_zero,
    unsigned short *out
)
{
    unsigned long value;
    char *end;

    if (!text || !text[0] || !out || text[0] == '-') return 0;
    errno = 0;
    end = NULL;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value > 65535UL) return 0;
    if (!allow_zero && value == 0UL) return 0;
    *out = (unsigned short)value;
    return 1;
}

/**
 * Parses one size value.
 * @param text Size text.
 * @param out Parsed size.
 * @return Nonzero on success.
 */
static int rstrm_cli_parse_size(const char *text, size_t *out)
{
    unsigned long long value;
    char *end;

    if (!text || !text[0] || !out || text[0] == '-') return 0;
    errno = 0;
    end = NULL;
    value = strtoull(text, &end, 10);
    if (errno || !end || *end != '\0' || value > (unsigned long long)SIZE_MAX) {
        return 0;
    }
    *out = (size_t)value;
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
static int rstrm_cli_option_value(
    int argc,
    char **argv,
    int *index,
    const char *option,
    const char **out
)
{
    if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
        fprintf(stderr, "rstrm: %s requires a value\n", option);
        return 1;
    }
    *index += 1;
    *out = argv[*index];
    return 0;
}

/**
 * Parses the explicit-peer relay command contract.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param options Parsed options.
 * @return Zero on success.
 */
static int rstrm_cli_parse(
    int argc,
    char **argv,
    rstrm_cli_options_t *options
)
{
    const char *positionals[2];
    size_t positional_count;
    int index;

    if (!options) return rstrm_cli_error("invalid parser state");
    memset(options, 0, sizeof(*options));
    positional_count = 0U;

    for (index = 1; index < argc; index++) {
        const char *argument;

        argument = argv[index];
        if (strcmp(argument, "--host") == 0) {
            const char *value;

            value = NULL;
            if (
                rstrm_cli_option_value(
                    argc,
                    argv,
                    &index,
                    argument,
                    &value
                ) != 0
            ) {
                return 1;
            }
            options->host = value;
        } else if (
            strcmp(argument, "-p") == 0 ||
            strcmp(argument, "--port") == 0
        ) {
            const char *value;

            value = NULL;
            if (
                rstrm_cli_option_value(
                    argc,
                    argv,
                    &index,
                    argument,
                    &value
                ) != 0
            ) {
                return 1;
            }
            if (!rstrm_cli_parse_port(value, 1, &options->port)) {
                return rstrm_cli_error_value("invalid local port: ", value);
            }
        } else if (strcmp(argument, "--mtu") == 0) {
            const char *value;

            value = NULL;
            if (
                rstrm_cli_option_value(
                    argc,
                    argv,
                    &index,
                    argument,
                    &value
                ) != 0
            ) {
                return 1;
            }
            if (!rstrm_cli_parse_size(value, &options->mtu)) {
                return rstrm_cli_error_value("invalid MTU: ", value);
            }
        } else if (
            strcmp(argument, "-h") == 0 ||
            strcmp(argument, "--help") == 0 ||
            strcmp(argument, "-v") == 0 ||
            strcmp(argument, "--version") == 0
        ) {
            return rstrm_cli_error(
                "-h/--help and -v/--version must be used alone"
            );
        } else if (argument[0] == '-') {
            return rstrm_cli_error_value("unknown option: ", argument);
        } else {
            if (
                positional_count >=
                sizeof(positionals) / sizeof(positionals[0])
            ) {
                return rstrm_cli_error("too many positional arguments");
            }
            positionals[positional_count++] = argument;
        }
    }

    if (positional_count != 2U) {
        return rstrm_cli_error("requires <peer-host> <peer-port>");
    }
    if (!positionals[0][0]) {
        return rstrm_cli_error("peer host must not be empty");
    }
    if (!rstrm_cli_parse_port(positionals[1], 0, &options->peer_port)) {
        return rstrm_cli_error_value("invalid peer port: ", positionals[1]);
    }
    options->peer_host = positionals[0];
    return 0;
}

/**
 * Starts the socket runtime where required.
 * @return Zero on success.
 */
static int rstrm_cli_socket_runtime_start(void)
{
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    return 0;
#endif
}

/**
 * Stops the socket runtime where required.
 * @return void.
 */
static void rstrm_cli_socket_runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Opens one bound UDP socket and stores the explicit peer endpoint.
 * @param options Parsed CLI options.
 * @param state Relay state.
 * @return Zero on success.
 */
static int rstrm_cli_socket_open(
    const rstrm_cli_options_t *options,
    rstrm_cli_state_t *state
)
{
    struct addrinfo peer_hints;
    struct addrinfo *peer_list;
    struct addrinfo *peer;
    char peer_port[16];
    char local_port[16];
    int peer_result;

    memset(&peer_hints, 0, sizeof(peer_hints));
    peer_hints.ai_family = AF_UNSPEC;
    peer_hints.ai_socktype = SOCK_DGRAM;
    peer_hints.ai_protocol = IPPROTO_UDP;
    peer_hints.ai_flags = AI_NUMERICSERV;
    snprintf(peer_port, sizeof(peer_port), "%u", (unsigned)options->peer_port);
    snprintf(local_port, sizeof(local_port), "%u", (unsigned)options->port);
    peer_list = NULL;
    peer_result = getaddrinfo(
        options->peer_host,
        peer_port,
        &peer_hints,
        &peer_list
    );
    if (peer_result != 0 || !peer_list) return 1;

    state->socket_fd = RSTRM_CLI_INVALID_SOCKET;
    for (peer = peer_list; peer; peer = peer->ai_next) {
        struct addrinfo local_hints;
        struct addrinfo *local_list;
        struct addrinfo *local;
        int local_result;

        if (peer->ai_family != AF_INET && peer->ai_family != AF_INET6) continue;
        memset(&local_hints, 0, sizeof(local_hints));
        local_hints.ai_family = peer->ai_family;
        local_hints.ai_socktype = SOCK_DGRAM;
        local_hints.ai_protocol = IPPROTO_UDP;
        local_hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
        local_list = NULL;
        local_result = getaddrinfo(
            options->host,
            local_port,
            &local_hints,
            &local_list
        );
        if (local_result != 0 || !local_list) continue;

        for (local = local_list; local; local = local->ai_next) {
            rstrm_cli_socket_t socket_fd;

            socket_fd = socket(
                peer->ai_family,
                peer->ai_socktype,
                peer->ai_protocol
            );
            if (socket_fd == RSTRM_CLI_INVALID_SOCKET) continue;
#ifndef _WIN32
            if (socket_fd >= FD_SETSIZE) {
                rstrm_cli_socket_close(socket_fd);
                continue;
            }
#endif
            if (
                bind(
                    socket_fd,
                    local->ai_addr,
                    (rstrm_cli_socklen_t)local->ai_addrlen
                ) == 0
            ) {
                state->socket_fd = socket_fd;
                memset(&state->peer_address, 0, sizeof(state->peer_address));
                memcpy(
                    &state->peer_address,
                    peer->ai_addr,
                    peer->ai_addrlen
                );
                state->peer_address_size =
                    (rstrm_cli_socklen_t)peer->ai_addrlen;
                break;
            }
            rstrm_cli_socket_close(socket_fd);
        }
        freeaddrinfo(local_list);
        if (state->socket_fd != RSTRM_CLI_INVALID_SOCKET) break;
    }
    freeaddrinfo(peer_list);
    return state->socket_fd == RSTRM_CLI_INVALID_SOCKET ? 1 : 0;
}

/**
 * Checks whether one datagram source matches the configured peer.
 * @param source Datagram source.
 * @param peer Configured peer.
 * @return Nonzero when the endpoints match.
 */
static int rstrm_cli_peer_matches(
    const struct sockaddr_storage *source,
    const struct sockaddr_storage *peer
)
{
    if (source->ss_family != peer->ss_family) return 0;
    if (source->ss_family == AF_INET) {
        const struct sockaddr_in *source4;
        const struct sockaddr_in *peer4;

        source4 = (const struct sockaddr_in *)source;
        peer4 = (const struct sockaddr_in *)peer;
        return source4->sin_port == peer4->sin_port &&
            source4->sin_addr.s_addr == peer4->sin_addr.s_addr;
    }
    if (source->ss_family == AF_INET6) {
        const struct sockaddr_in6 *source6;
        const struct sockaddr_in6 *peer6;

        source6 = (const struct sockaddr_in6 *)source;
        peer6 = (const struct sockaddr_in6 *)peer;
        return source6->sin6_port == peer6->sin6_port &&
            source6->sin6_scope_id == peer6->sin6_scope_id &&
            memcmp(
                &source6->sin6_addr,
                &peer6->sin6_addr,
                sizeof(source6->sin6_addr)
            ) == 0;
    }
    return 0;
}

/**
 * Writes all received bytes to standard output.
 * @param data Stream bytes.
 * @param size Byte count.
 * @return Zero on success.
 */
static int rstrm_cli_write_stdout(const void *data, size_t size)
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
static int rstrm_cli_read_stdin(void *data, size_t size)
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
            if (errno == EINTR) {
                if (rstrm_cli_stop) return 0;
                continue;
            }
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
static int rstrm_cli_prepare_stdio(void)
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
static int rstrm_cli_windows_stdin_ready(int *closed)
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
 * Waits briefly for UDP or standard-input activity.
 * @param socket_fd Bound UDP socket.
 * @param watch_stdin Whether standard input should be watched.
 * @param socket_ready Set when one UDP datagram can be read.
 * @param stdin_state Set to one RSTRM_CLI_STDIN_* state.
 * @return Zero on success.
 */
static int rstrm_cli_wait(
    rstrm_cli_socket_t socket_fd,
    int watch_stdin,
    int *socket_ready,
    int *stdin_state
)
{
    fd_set readable;
    struct timeval timeout;
    int result;

    *socket_ready = 0;
    *stdin_state = RSTRM_CLI_STDIN_NONE;
#ifdef _WIN32
    if (watch_stdin) {
        int closed;

        if (rstrm_cli_windows_stdin_ready(&closed)) {
            *stdin_state = RSTRM_CLI_STDIN_READY;
        } else if (closed) {
            *stdin_state = RSTRM_CLI_STDIN_CLOSED;
        }
    }
    timeout.tv_sec = 0;
    timeout.tv_usec = *stdin_state == RSTRM_CLI_STDIN_NONE ? 100000 : 0;
    FD_ZERO(&readable);
    FD_SET(socket_fd, &readable);
    result = select(0, &readable, NULL, NULL, &timeout);
    if (result == SOCKET_ERROR) {
        if (rstrm_cli_stop && WSAGetLastError() == WSAEINTR) return 0;
        return 1;
    }
#else
    int max_fd;

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    FD_ZERO(&readable);
    FD_SET(socket_fd, &readable);
    max_fd = socket_fd;
    if (watch_stdin) {
        FD_SET(STDIN_FILENO, &readable);
        if (STDIN_FILENO > max_fd) max_fd = STDIN_FILENO;
    }
    result = select(max_fd + 1, &readable, NULL, NULL, &timeout);
    if (result < 0) {
        if (errno == EINTR) return 0;
        return 1;
    }
    if (watch_stdin && FD_ISSET(STDIN_FILENO, &readable)) {
        *stdin_state = RSTRM_CLI_STDIN_READY;
    }
#endif
    if (result > 0 && FD_ISSET(socket_fd, &readable)) *socket_ready = 1;
    return 0;
}

/**
 * Handles process interruption.
 * @param signal_number Signal number.
 * @return void.
 */
static void rstrm_cli_signal(int signal_number)
{
    (void)signal_number;
    rstrm_cli_stop = 1;
}

/**
 * Writes one outgoing reliable-stream datagram to UDP.
 * @param data Datagram bytes.
 * @param data_size Datagram byte count.
 * @param userdata Relay state.
 * @return Zero when the datagram was sent.
 */
static int rstrm_cli_datagram(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    rstrm_cli_state_t *state;
#ifdef _WIN32
    int result;
#else
    ssize_t result;
#endif

    state = (rstrm_cli_state_t *)userdata;
#ifdef _WIN32
    if (data_size > (size_t)INT_MAX) return 1;
    result = sendto(
        state->socket_fd,
        (const char *)data,
        (int)data_size,
        0,
        (const struct sockaddr *)&state->peer_address,
        state->peer_address_size
    );
#else
    do {
        result = sendto(
            state->socket_fd,
            data,
            data_size,
            0,
            (const struct sockaddr *)&state->peer_address,
            state->peer_address_size
        );
    } while (result < 0 && errno == EINTR && !rstrm_cli_stop);
#endif
    return result >= 0 && (size_t)result == data_size ? 0 : 1;
}

/**
 * Writes ordered stream bytes to standard output.
 * @param data Stream bytes.
 * @param data_size Byte count.
 * @param userdata Relay state.
 * @return void.
 */
static void rstrm_cli_receive(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    rstrm_cli_state_t *state;

    state = (rstrm_cli_state_t *)userdata;
    if (atomic_load(&state->io_failed)) return;
    if (rstrm_cli_write_stdout(data, data_size) != 0) {
        atomic_store(&state->io_failed, 1);
    }
}

/**
 * Marks the peer write side as gracefully closed.
 * @param userdata Relay state.
 * @return void.
 */
static void rstrm_cli_eof(void *userdata)
{
    rstrm_cli_state_t *state;

    state = (rstrm_cli_state_t *)userdata;
    atomic_store(&state->remote_eof, 1);
}

/**
 * Marks reliable-stream send capacity as available.
 * @param userdata Relay state.
 * @return void.
 */
static void rstrm_cli_drain(void *userdata)
{
    rstrm_cli_state_t *state;

    state = (rstrm_cli_state_t *)userdata;
    atomic_store(&state->can_send, 1);
}

/**
 * Stores one asynchronous reliable-stream failure.
 * @param status Public reliable-stream status.
 * @param userdata Relay state.
 * @return void.
 */
static void rstrm_cli_stream_error(int status, void *userdata)
{
    rstrm_cli_state_t *state;

    state = (rstrm_cli_state_t *)userdata;
    atomic_store(&state->error_status, status);
}

/**
 * Supplies one received peer datagram to librstrm.
 * @param stream Open reliable stream.
 * @param state Relay state.
 * @param stream_status Set to an unexpected library status.
 * @return Zero on socket success.
 */
static int rstrm_cli_input_one(
    kc_rstrm_t *stream,
    rstrm_cli_state_t *state,
    int *stream_status
)
{
    unsigned char data[RSTRM_CLI_DATAGRAM_SIZE];
    struct sockaddr_storage source;
    rstrm_cli_socklen_t source_size;
#ifdef _WIN32
    int result;
#else
    ssize_t result;
#endif

    memset(&source, 0, sizeof(source));
    source_size = (rstrm_cli_socklen_t)sizeof(source);
#ifdef _WIN32
    result = recvfrom(
        state->socket_fd,
        (char *)data,
        (int)sizeof(data),
        0,
        (struct sockaddr *)&source,
        &source_size
    );
    if (result == SOCKET_ERROR) return 1;
#else
    do {
        result = recvfrom(
            state->socket_fd,
            data,
            sizeof(data),
            0,
            (struct sockaddr *)&source,
            &source_size
        );
    } while (result < 0 && errno == EINTR && !rstrm_cli_stop);
    if (result < 0) {
        if (errno == EINTR && rstrm_cli_stop) return 0;
        return 1;
    }
#endif
    if (!rstrm_cli_peer_matches(&source, &state->peer_address)) return 0;
    *stream_status = kc_rstrm_input(stream, data, (size_t)result);
    if (*stream_status == KC_RSTRM_EFULL) *stream_status = KC_RSTRM_OK;
    return 0;
}

/**
 * Sends one retained stdin chunk when stream capacity is available.
 * @param stream Open reliable stream.
 * @param state Relay state.
 * @param data Retained stdin bytes.
 * @param data_size Retained byte count, cleared after acceptance.
 * @return KC_RSTRM_OK or an unexpected library status.
 */
static int rstrm_cli_send_pending(
    kc_rstrm_t *stream,
    rstrm_cli_state_t *state,
    const void *data,
    size_t *data_size
)
{
    int status;

    if (*data_size == 0U || !atomic_load(&state->can_send)) {
        return KC_RSTRM_OK;
    }
    atomic_store(&state->can_send, 0);
    status = kc_rstrm_send(stream, data, *data_size);
    if (status == KC_RSTRM_OK) {
        *data_size = 0U;
        atomic_store(&state->can_send, 1);
        return KC_RSTRM_OK;
    }
    if (status == KC_RSTRM_EFULL) return KC_RSTRM_OK;
    return status;
}

/**
 * Runs one full-duplex explicit-peer relay.
 * @param options Parsed command options.
 * @return Process exit status.
 */
static int rstrm_cli_run(const rstrm_cli_options_t *options)
{
    unsigned char input[RSTRM_CLI_BUFFER_SIZE];
    rstrm_cli_state_t state;
    kc_rstrm_options_t stream_options;
    kc_rstrm_t *stream;
    size_t pending_size;
    int local_shutdown;
    int exit_status;
    int status;

    memset(&state, 0, sizeof(state));
    state.socket_fd = RSTRM_CLI_INVALID_SOCKET;
    atomic_init(&state.remote_eof, 0);
    atomic_init(&state.can_send, 1);
    atomic_init(&state.error_status, KC_RSTRM_OK);
    atomic_init(&state.io_failed, 0);
    if (rstrm_cli_socket_open(options, &state) != 0) {
        return rstrm_cli_error("cannot open UDP path");
    }

    memset(&stream_options, 0, sizeof(stream_options));
    stream_options.mtu = options->mtu;
    stream = NULL;
    status = kc_rstrm_open(
        &stream,
        &stream_options,
        rstrm_cli_receive,
        rstrm_cli_datagram,
        rstrm_cli_eof,
        rstrm_cli_drain,
        rstrm_cli_stream_error,
        &state
    );
    if (status != KC_RSTRM_OK) {
        rstrm_cli_socket_close(state.socket_fd);
        return rstrm_cli_error_value("open failed: ", kc_rstrm_strerror(status));
    }

    pending_size = 0U;
    local_shutdown = 0;
    exit_status = 0;
    for (;;) {
        int socket_ready;
        int stdin_state;
        int stream_status;
        int watch_stdin;

        if (rstrm_cli_stop) break;
        if (atomic_load(&state.io_failed)) {
            exit_status = rstrm_cli_error("standard output write failed");
            break;
        }
        stream_status = atomic_load(&state.error_status);
        if (stream_status != KC_RSTRM_OK) {
            exit_status = rstrm_cli_error_value(
                "stream error: ",
                kc_rstrm_strerror(stream_status)
            );
            break;
        }
        if (local_shutdown && atomic_load(&state.remote_eof) &&
            kc_rstrm_shutdown_complete(stream))
        {
            break;
        }

        status = rstrm_cli_send_pending(
            stream,
            &state,
            input,
            &pending_size
        );
        if (status != KC_RSTRM_OK) {
            exit_status = rstrm_cli_error_value(
                "send failed: ",
                kc_rstrm_strerror(status)
            );
            break;
        }

        watch_stdin = !local_shutdown && pending_size == 0U &&
            atomic_load(&state.can_send);
        if (
            rstrm_cli_wait(
                state.socket_fd,
                watch_stdin,
                &socket_ready,
                &stdin_state
            ) != 0
        ) {
            exit_status = rstrm_cli_error("I/O wait failed");
            break;
        }

        if (socket_ready) {
            stream_status = KC_RSTRM_OK;
            if (rstrm_cli_input_one(stream, &state, &stream_status) != 0) {
                exit_status = rstrm_cli_error("UDP receive failed");
                break;
            }
            if (stream_status != KC_RSTRM_OK) {
                exit_status = rstrm_cli_error_value(
                    "input failed: ",
                    kc_rstrm_strerror(stream_status)
                );
                break;
            }
        }

        if (stdin_state == RSTRM_CLI_STDIN_CLOSED) {
            status = kc_rstrm_shutdown(stream);
            if (status != KC_RSTRM_OK) {
                exit_status = rstrm_cli_error_value(
                    "shutdown failed: ",
                    kc_rstrm_strerror(status)
                );
                break;
            }
            local_shutdown = 1;
        } else if (stdin_state == RSTRM_CLI_STDIN_READY) {
            int count;

            count = rstrm_cli_read_stdin(input, sizeof(input));
            if (count < 0) {
                exit_status = rstrm_cli_error("standard input read failed");
                break;
            }
            if (count == 0) {
                status = kc_rstrm_shutdown(stream);
                if (status != KC_RSTRM_OK) {
                    exit_status = rstrm_cli_error_value(
                        "shutdown failed: ",
                        kc_rstrm_strerror(status)
                    );
                    break;
                }
                local_shutdown = 1;
            } else {
                pending_size = (size_t)count;
                status = rstrm_cli_send_pending(
                    stream,
                    &state,
                    input,
                    &pending_size
                );
                if (status != KC_RSTRM_OK) {
                    exit_status = rstrm_cli_error_value(
                        "send failed: ",
                        kc_rstrm_strerror(status)
                    );
                    break;
                }
            }
        }
    }

    kc_rstrm_close(stream);
    rstrm_cli_socket_close(state.socket_fd);
    return exit_status;
}

/**
 * Runs metadata handling or one reliable-stream relay.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv)
{
    rstrm_cli_options_t options;
    int result;

    if (
        argc == 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    ) {
        rstrm_cli_help(argv[0]);
        return 0;
    }
    if (
        argc == 2 &&
        (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)
    ) {
        rstrm_cli_version();
        return 0;
    }
    if (rstrm_cli_parse(argc, argv, &options) != 0) return 1;
    if (rstrm_cli_prepare_stdio() != 0) {
        return rstrm_cli_error("standard stream setup failed");
    }
    if (rstrm_cli_socket_runtime_start() != 0) {
        return rstrm_cli_error("socket runtime setup failed");
    }

    rstrm_cli_stop = 0;
    (void)signal(SIGINT, rstrm_cli_signal);
    (void)signal(SIGTERM, rstrm_cli_signal);
    result = rstrm_cli_run(&options);
    rstrm_cli_socket_runtime_stop();
    return result;
}
