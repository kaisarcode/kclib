/**
 * tls.c - TLS stream relay command-line interface.
 * Summary: Relays plaintext through one authenticated TLS client or server.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtls.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>
#include <fcntl.h>
#include <io.h>
typedef SOCKET tls_cli_socket_t;
#define TLS_CLI_INVALID_SOCKET INVALID_SOCKET
#define tls_cli_socket_close closesocket
#else
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int tls_cli_socket_t;
#define TLS_CLI_INVALID_SOCKET (-1)
#define tls_cli_socket_close close
#endif

#define TLS_CLI_BUFFER_SIZE 16384U
#define TLS_CLI_EVENT_SOCKET 1
#define TLS_CLI_EVENT_STDIN 2
#define TLS_CLI_EVENT_STDIN_CLOSED 4

typedef struct {
    int role;
    const char *network_host;
    const char *tls_host;
    const char *bind_host;
    const char *ca_path;
    const char *certificate_path;
    const char *private_key_path;
    unsigned short port;
} tls_cli_options_t;

typedef struct {
    kc_tls_t *tls;
    tls_cli_socket_t socket;
    int io_failed;
} tls_cli_state_t;

/**
 * Prints command usage.
 * @param name Program name.
 * @return void.
 */
static void tls_cli_help(const char *name)
{
    printf("Usage: %s client <host> <port> [options]\n", name);
    printf("       %s server <port> --cert <file> --key <file> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  client  Connect one authenticated TLS client session\n");
    printf("  server  Accept one authenticated TLS server session\n");
    printf("\n");
    printf("Client options:\n");
    printf("  --name <host>  TLS server name for SNI and verification (default: host)\n");
    printf("  --ca <file>    Trusted CA chain in PEM or DER (default: system trust)\n");
    printf("\n");
    printf("Server options:\n");
    printf("  --bind <host>  Local bind host (default: all interfaces)\n");
    printf("  --cert <file>  Server certificate chain in PEM or DER\n");
    printf("  --key <file>   Server private key in PEM or DER\n");
    printf("\n");
    printf("Common options:\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Prints the linked libtls build version.
 * @return void.
 */
static void tls_cli_version(void)
{
    printf("%llu\n", (unsigned long long)kc_tls_version());
}

/**
 * Prints one CLI error.
 * @param message Error message.
 * @return One.
 */
static int tls_cli_error(const char *message)
{
    fprintf(stderr, "tls: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error with a value.
 * @param message Error prefix.
 * @param value Error value.
 * @return One.
 */
static int tls_cli_error_value(const char *message, const char *value)
{
    fprintf(stderr, "tls: %s%s\n", message, value ? value : "");
    return 1;
}

/**
 * Parses one nonzero TCP port.
 * @param text Port text.
 * @param out Parsed port.
 * @return Nonzero on success.
 */
static int tls_cli_parse_port(const char *text, unsigned short *out)
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
static int tls_cli_option_value(
    int argc,
    char **argv,
    int *index,
    const char *option,
    const char **out
)
{
    if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
        fprintf(stderr, "tls: %s requires a value\n", option);
        return 1;
    }
    *index += 1;
    *out = argv[*index];
    return 0;
}

/**
 * Parses the client or server command contract.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param options Parsed options.
 * @return Zero on success.
 */
static int tls_cli_parse(
    int argc,
    char **argv,
    tls_cli_options_t *options
)
{
    const char *positionals[3];
    size_t positional_count;
    int index;

    if (argc < 2 || !options) return tls_cli_error("missing command");
    memset(options, 0, sizeof(*options));
    if (strcmp(argv[1], "client") == 0) {
        options->role = KC_TLS_CLIENT;
    } else if (strcmp(argv[1], "server") == 0) {
        options->role = KC_TLS_SERVER;
    } else {
        return tls_cli_error_value("unknown command: ", argv[1]);
    }

    positional_count = 0U;
    for (index = 2; index < argc; index++) {
        const char *argument;

        argument = argv[index];
        if (strcmp(argument, "--name") == 0) {
            if (options->role != KC_TLS_CLIENT) {
                return tls_cli_error("--name is only valid for client");
            }
            if (tls_cli_option_value(
                    argc,
                    argv,
                    &index,
                    "--name",
                    &options->tls_host
                ) != 0) {
                return 1;
            }
        } else if (strcmp(argument, "--ca") == 0) {
            if (options->role != KC_TLS_CLIENT) {
                return tls_cli_error("--ca is only valid for client");
            }
            if (tls_cli_option_value(
                    argc,
                    argv,
                    &index,
                    "--ca",
                    &options->ca_path
                ) != 0) {
                return 1;
            }
        } else if (strcmp(argument, "--bind") == 0) {
            if (options->role != KC_TLS_SERVER) {
                return tls_cli_error("--bind is only valid for server");
            }
            if (tls_cli_option_value(
                    argc,
                    argv,
                    &index,
                    "--bind",
                    &options->bind_host
                ) != 0) {
                return 1;
            }
        } else if (strcmp(argument, "--cert") == 0) {
            if (options->role != KC_TLS_SERVER) {
                return tls_cli_error("--cert is only valid for server");
            }
            if (tls_cli_option_value(
                    argc,
                    argv,
                    &index,
                    "--cert",
                    &options->certificate_path
                ) != 0) {
                return 1;
            }
        } else if (strcmp(argument, "--key") == 0) {
            if (options->role != KC_TLS_SERVER) {
                return tls_cli_error("--key is only valid for server");
            }
            if (tls_cli_option_value(
                    argc,
                    argv,
                    &index,
                    "--key",
                    &options->private_key_path
                ) != 0) {
                return 1;
            }
        } else if (argument[0] == '-') {
            return tls_cli_error_value("unknown option: ", argument);
        } else {
            if (positional_count >= sizeof(positionals) / sizeof(positionals[0])) {
                return tls_cli_error("too many positional arguments");
            }
            positionals[positional_count++] = argument;
        }
    }

    if (options->role == KC_TLS_CLIENT) {
        if (positional_count != 2U) {
            return tls_cli_error("client requires <host> <port>");
        }
        options->network_host = positionals[0];
        if (!tls_cli_parse_port(positionals[1], &options->port)) {
            return tls_cli_error_value("invalid port: ", positionals[1]);
        }
        if (!options->tls_host) options->tls_host = options->network_host;
        return 0;
    }

    if (positional_count != 1U) {
        return tls_cli_error("server requires <port>");
    }
    if (!tls_cli_parse_port(positionals[0], &options->port)) {
        return tls_cli_error_value("invalid port: ", positionals[0]);
    }
    if (!options->certificate_path || !options->private_key_path) {
        return tls_cli_error("server requires --cert <file> and --key <file>");
    }
    return 0;
}

/**
 * Starts the native socket runtime.
 * @return Zero on success.
 */
static int tls_cli_socket_runtime_start(void)
{
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : 1;
#else
    (void)signal(SIGPIPE, SIG_IGN);
    return 0;
#endif
}

/**
 * Stops the native socket runtime.
 * @return void.
 */
static void tls_cli_socket_runtime_stop(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Formats one TCP service port.
 * @param port TCP port.
 * @param service Destination buffer.
 * @param size Destination size.
 * @return Zero on success.
 */
static int tls_cli_service(
    unsigned short port,
    char *service,
    size_t size
)
{
    int count;

    count = snprintf(service, size, "%u", (unsigned int)port);
    return count >= 0 && (size_t)count < size ? 0 : 1;
}

/**
 * Opens one outbound TCP connection.
 * @param host Destination host.
 * @param port Destination port.
 * @return Connected socket or TLS_CLI_INVALID_SOCKET.
 */
static tls_cli_socket_t tls_cli_connect(
    const char *host,
    unsigned short port
)
{
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    tls_cli_socket_t socket_fd;
    char service[16];

    if (tls_cli_service(port, service, sizeof(service)) != 0) {
        return TLS_CLI_INVALID_SOCKET;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) {
        return TLS_CLI_INVALID_SOCKET;
    }

    socket_fd = TLS_CLI_INVALID_SOCKET;
    for (address = addresses; address; address = address->ai_next) {
        socket_fd = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );
        if (socket_fd == TLS_CLI_INVALID_SOCKET) continue;
        if (connect(socket_fd, address->ai_addr, (int)address->ai_addrlen) == 0) {
            break;
        }
        tls_cli_socket_close(socket_fd);
        socket_fd = TLS_CLI_INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    return socket_fd;
}

/**
 * Opens one TCP listener.
 * @param host Optional bind host.
 * @param port Local port.
 * @return Listening socket or TLS_CLI_INVALID_SOCKET.
 */
static tls_cli_socket_t tls_cli_listen(
    const char *host,
    unsigned short port
)
{
    struct addrinfo hints;
    struct addrinfo *addresses;
    struct addrinfo *address;
    tls_cli_socket_t socket_fd;
    char service[16];

    if (tls_cli_service(port, service, sizeof(service)) != 0) {
        return TLS_CLI_INVALID_SOCKET;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) {
        return TLS_CLI_INVALID_SOCKET;
    }

    socket_fd = TLS_CLI_INVALID_SOCKET;
    for (address = addresses; address; address = address->ai_next) {
        int reuse;

        socket_fd = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );
        if (socket_fd == TLS_CLI_INVALID_SOCKET) continue;
        reuse = 1;
        (void)setsockopt(
            socket_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            (const char *)&reuse,
            (int)sizeof(reuse)
        );
        if (bind(socket_fd, address->ai_addr, (int)address->ai_addrlen) == 0 &&
            listen(socket_fd, 16) == 0) {
            break;
        }
        tls_cli_socket_close(socket_fd);
        socket_fd = TLS_CLI_INVALID_SOCKET;
    }
    freeaddrinfo(addresses);
    return socket_fd;
}

/**
 * Sends all bytes to one TCP socket.
 * @param socket_fd Connected socket.
 * @param data Bytes to send.
 * @param size Byte count.
 * @return Zero on success.
 */
static int tls_cli_socket_send_all(
    tls_cli_socket_t socket_fd,
    const void *data,
    size_t size
)
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
        result = send(socket_fd, (const char *)bytes + offset, (int)part, 0);
        if (result == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return 1;
        }
#else
        result = send(socket_fd, bytes + offset, part, 0);
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
 * Receives one TCP chunk.
 * @param socket_fd Connected socket.
 * @param data Destination buffer.
 * @param size Destination size.
 * @return Received byte count, zero on EOF, or negative on error.
 */
static int tls_cli_socket_receive(
    tls_cli_socket_t socket_fd,
    void *data,
    size_t size
)
{
    for (;;) {
#ifdef _WIN32
        int result;

        result = recv(socket_fd, (char *)data, (int)size, 0);
        if (result == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return -1;
        }
        return result;
#else
        ssize_t result;

        result = recv(socket_fd, data, size, 0);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        return (int)result;
#endif
    }
}

/**
 * Writes all plaintext bytes to standard output.
 * @param data Plaintext bytes.
 * @param size Byte count.
 * @return Zero on success.
 */
static int tls_cli_write_stdout(const void *data, size_t size)
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
 * Reads one plaintext chunk from standard input.
 * @param data Destination buffer.
 * @param size Destination size.
 * @return Byte count, zero on EOF, or negative on error.
 */
static int tls_cli_read_stdin(void *data, size_t size)
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
 * Configures binary standard streams when required.
 * @return Zero on success.
 */
static int tls_cli_prepare_stdio(void)
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
static int tls_cli_windows_stdin_ready(int *closed)
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
 * Waits for socket or standard-input activity.
 * @param socket_fd Connected socket.
 * @param watch_stdin Nonzero while standard input remains open.
 * @return Event flags, or negative on error.
 */
static int tls_cli_wait(
    tls_cli_socket_t socket_fd,
    int watch_stdin
)
{
#ifdef _WIN32
    fd_set readable;
    struct timeval timeout;
    int events;
    int result;

    FD_ZERO(&readable);
    FD_SET(socket_fd, &readable);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    result = select(0, &readable, NULL, NULL, &timeout);
    if (result == SOCKET_ERROR) return -1;
    events = FD_ISSET(socket_fd, &readable) ? TLS_CLI_EVENT_SOCKET : 0;
    if (watch_stdin) {
        int closed;

        if (tls_cli_windows_stdin_ready(&closed)) {
            events |= TLS_CLI_EVENT_STDIN;
        } else if (closed) {
            events |= TLS_CLI_EVENT_STDIN_CLOSED;
        }
    }
    return events;
#else
    fd_set readable;
    int maximum;
    int result;
    int events;

    FD_ZERO(&readable);
    FD_SET(socket_fd, &readable);
    maximum = socket_fd;
    if (watch_stdin) {
        FD_SET(STDIN_FILENO, &readable);
        if (STDIN_FILENO > maximum) maximum = STDIN_FILENO;
    }
    do {
        result = select(maximum + 1, &readable, NULL, NULL, NULL);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    events = FD_ISSET(socket_fd, &readable) ? TLS_CLI_EVENT_SOCKET : 0;
    if (watch_stdin && FD_ISSET(STDIN_FILENO, &readable)) {
        events |= TLS_CLI_EVENT_STDIN;
    }
    return events;
#endif
}

/**
 * Receives authenticated plaintext from libtls.
 * @param data Plaintext bytes.
 * @param data_size Plaintext byte count.
 * @param userdata Relay state.
 * @return void.
 */
static void tls_cli_plaintext(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    tls_cli_state_t *state;

    state = (tls_cli_state_t *)userdata;
    if (tls_cli_write_stdout(data, data_size) != 0) state->io_failed = 1;
}

/**
 * Sends TLS wire bytes to the TCP transport.
 * @param data TLS wire bytes.
 * @param data_size TLS wire byte count.
 * @param userdata Relay state.
 * @return void.
 */
static void tls_cli_ciphertext(
    const void *data,
    size_t data_size,
    void *userdata
)
{
    tls_cli_state_t *state;

    state = (tls_cli_state_t *)userdata;
    if (tls_cli_socket_send_all(state->socket, data, data_size) != 0) {
        state->io_failed = 1;
    }
}

/**
 * Reads an entire certificate or key file.
 * @param path Input path.
 * @param out Owned byte buffer.
 * @param out_size Output byte count.
 * @return Zero on success.
 */
static int tls_cli_read_file(
    const char *path,
    unsigned char **out,
    size_t *out_size
)
{
    FILE *file;
    long length;
    unsigned char *data;

    if (!path || !out || !out_size) return 1;
    *out = NULL;
    *out_size = 0U;
    file = fopen(path, "rb");
    if (!file) return 1;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    length = ftell(file);
    if (length <= 0L || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    data = (unsigned char *)malloc((size_t)length);
    if (!data) {
        fclose(file);
        return 1;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 1;
    }
    fclose(file);
    *out = data;
    *out_size = (size_t)length;
    return 0;
}

/**
 * Prints one libtls failure.
 * @param status Public libtls status.
 * @return One.
 */
static int tls_cli_tls_error(int status)
{
    fprintf(stderr, "tls: %s\n", kc_tls_strerror(status));
    return 1;
}

/**
 * Relays plaintext and TLS wire bytes until authenticated shutdown.
 * @param state Active relay state.
 * @return Zero on clean TLS shutdown.
 */
static int tls_cli_relay(tls_cli_state_t *state)
{
    unsigned char buffer[TLS_CLI_BUFFER_SIZE];
    int stdin_open;

    stdin_open = 1;
    for (;;) {
        int events;

        if (state->io_failed) return tls_cli_error("transport I/O failed");
        events = tls_cli_wait(state->socket, stdin_open);
        if (events < 0) return tls_cli_error("transport wait failed");
        if (events & TLS_CLI_EVENT_STDIN_CLOSED) stdin_open = 0;

        if (events & TLS_CLI_EVENT_SOCKET) {
            int size;
            int status;

            size = tls_cli_socket_receive(
                state->socket,
                buffer,
                sizeof(buffer)
            );
            if (size < 0) return tls_cli_error("TCP receive failed");
            if (size == 0) {
                return tls_cli_error("TCP closed before TLS close_notify");
            }
            status = kc_tls_receive(state->tls, buffer, (size_t)size);
            if (state->io_failed) return tls_cli_error("transport I/O failed");
            if (status == KC_TLS_CLOSED) return 0;
            if (status != KC_TLS_OK) return tls_cli_tls_error(status);
        }

        if (stdin_open && (events & TLS_CLI_EVENT_STDIN)) {
            int size;
            int status;

            size = tls_cli_read_stdin(buffer, sizeof(buffer));
            if (size < 0) return tls_cli_error("standard input read failed");
            if (size == 0) {
                stdin_open = 0;
                continue;
            }
            status = kc_tls_send(state->tls, buffer, (size_t)size);
            if (state->io_failed) return tls_cli_error("transport I/O failed");
            if (status == KC_TLS_CLOSED) return 0;
            if (status != KC_TLS_OK) return tls_cli_tls_error(status);
        }
    }
}

/**
 * Opens one TLS session over an established TCP socket.
 * @param state Relay state.
 * @param options CLI options.
 * @param ca Optional CA bytes.
 * @param ca_size CA byte count.
 * @param certificate Optional certificate bytes.
 * @param certificate_size Certificate byte count.
 * @param private_key Optional private-key bytes.
 * @param private_key_size Private-key byte count.
 * @return Public libtls status.
 */
static int tls_cli_open_tls(
    tls_cli_state_t *state,
    const tls_cli_options_t *options,
    const void *ca,
    size_t ca_size,
    const void *certificate,
    size_t certificate_size,
    const void *private_key,
    size_t private_key_size
)
{
    kc_tls_options_t tls_options;

    memset(&tls_options, 0, sizeof(tls_options));
    tls_options.role = options->role;
    tls_options.host = options->tls_host;
    tls_options.ca = ca;
    tls_options.ca_size = ca_size;
    tls_options.certificate = certificate;
    tls_options.certificate_size = certificate_size;
    tls_options.private_key = private_key;
    tls_options.private_key_size = private_key_size;
    return kc_tls_open(
        &state->tls,
        &tls_options,
        tls_cli_plaintext,
        tls_cli_ciphertext,
        state
    );
}

/**
 * Runs one client TLS relay.
 * @param options Client options.
 * @return Process status.
 */
static int tls_cli_run_client(const tls_cli_options_t *options)
{
    tls_cli_state_t state;
    unsigned char *ca;
    size_t ca_size;
    int status;
    int result;

    memset(&state, 0, sizeof(state));
    state.socket = TLS_CLI_INVALID_SOCKET;
    ca = NULL;
    ca_size = 0U;
    if (options->ca_path &&
        tls_cli_read_file(options->ca_path, &ca, &ca_size) != 0) {
        return tls_cli_error_value("cannot read CA file: ", options->ca_path);
    }
    if (tls_cli_socket_runtime_start() != 0) {
        free(ca);
        return tls_cli_error("cannot initialize socket runtime");
    }
    state.socket = tls_cli_connect(options->network_host, options->port);
    if (state.socket == TLS_CLI_INVALID_SOCKET) {
        tls_cli_socket_runtime_stop();
        free(ca);
        return tls_cli_error("TCP connection failed");
    }
    if (tls_cli_prepare_stdio() != 0) {
        tls_cli_socket_close(state.socket);
        tls_cli_socket_runtime_stop();
        free(ca);
        return tls_cli_error("cannot configure standard streams");
    }

    status = tls_cli_open_tls(
        &state,
        options,
        ca,
        ca_size,
        NULL,
        0U,
        NULL,
        0U
    );
    free(ca);
    if (status != KC_TLS_OK || state.io_failed) {
        if (state.tls) kc_tls_close(state.tls);
        tls_cli_socket_close(state.socket);
        tls_cli_socket_runtime_stop();
        if (status != KC_TLS_OK) return tls_cli_tls_error(status);
        return tls_cli_error("transport I/O failed");
    }

    result = tls_cli_relay(&state);
    kc_tls_close(state.tls);
    tls_cli_socket_close(state.socket);
    tls_cli_socket_runtime_stop();
    return result;
}

/**
 * Runs one server TLS relay.
 * @param options Server options.
 * @return Process status.
 */
static int tls_cli_run_server(const tls_cli_options_t *options)
{
    tls_cli_state_t state;
    tls_cli_socket_t listener;
    unsigned char *certificate;
    unsigned char *private_key;
    size_t certificate_size;
    size_t private_key_size;
    int status;
    int result;

    memset(&state, 0, sizeof(state));
    state.socket = TLS_CLI_INVALID_SOCKET;
    listener = TLS_CLI_INVALID_SOCKET;
    certificate = NULL;
    private_key = NULL;
    certificate_size = 0U;
    private_key_size = 0U;

    if (tls_cli_read_file(
            options->certificate_path,
            &certificate,
            &certificate_size
        ) != 0) {
        return tls_cli_error_value(
            "cannot read certificate file: ",
            options->certificate_path
        );
    }
    if (tls_cli_read_file(
            options->private_key_path,
            &private_key,
            &private_key_size
        ) != 0) {
        free(certificate);
        return tls_cli_error_value(
            "cannot read private key file: ",
            options->private_key_path
        );
    }
    if (tls_cli_socket_runtime_start() != 0) {
        free(certificate);
        free(private_key);
        return tls_cli_error("cannot initialize socket runtime");
    }
    listener = tls_cli_listen(options->bind_host, options->port);
    if (listener == TLS_CLI_INVALID_SOCKET) {
        tls_cli_socket_runtime_stop();
        free(certificate);
        free(private_key);
        return tls_cli_error("TCP listen failed");
    }
    state.socket = accept(listener, NULL, NULL);
    tls_cli_socket_close(listener);
    if (state.socket == TLS_CLI_INVALID_SOCKET) {
        tls_cli_socket_runtime_stop();
        free(certificate);
        free(private_key);
        return tls_cli_error("TCP accept failed");
    }
    if (tls_cli_prepare_stdio() != 0) {
        tls_cli_socket_close(state.socket);
        tls_cli_socket_runtime_stop();
        free(certificate);
        free(private_key);
        return tls_cli_error("cannot configure standard streams");
    }

    status = tls_cli_open_tls(
        &state,
        options,
        NULL,
        0U,
        certificate,
        certificate_size,
        private_key,
        private_key_size
    );
    free(certificate);
    free(private_key);
    if (status != KC_TLS_OK || state.io_failed) {
        if (state.tls) kc_tls_close(state.tls);
        tls_cli_socket_close(state.socket);
        tls_cli_socket_runtime_stop();
        if (status != KC_TLS_OK) return tls_cli_tls_error(status);
        return tls_cli_error("transport I/O failed");
    }

    result = tls_cli_relay(&state);
    kc_tls_close(state.tls);
    tls_cli_socket_close(state.socket);
    tls_cli_socket_runtime_stop();
    return result;
}

/**
 * Runs the tls CLI.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv)
{
    tls_cli_options_t options;
    int index;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-h") == 0 ||
            strcmp(argv[index], "--help") == 0) {
            tls_cli_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[index], "-v") == 0 ||
            strcmp(argv[index], "--version") == 0) {
            tls_cli_version();
            return 0;
        }
    }
    if (tls_cli_parse(argc, argv, &options) != 0) return 1;
    if (options.role == KC_TLS_CLIENT) {
        return tls_cli_run_client(&options);
    }
    return tls_cli_run_server(&options);
}
