/**
 * netl.c - Incoming network listener CLI.
 * Summary: Dispatches TCP connections or UDP datagrams to local commands.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libnetl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifndef _WIN32
#include <time.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define NETL_CLI_COMMAND_SIZE 4096

/**
 * Open the listener with the CLI accepted-peer callback.
 * @return KC_NETL_OK on success, otherwise a negative status.
 */
int kc_netl_cli_open(
    kc_netl_t **out,
    const kc_netl_options_t *options,
    kc_netl_handler_t handler,
    kc_netl_error_handler_t error_handler,
    void *userdata,
    void (*accept_handler)(kc_netl_peer_t *peer, void *userdata),
    void *accept_userdata
);

/**
 * Transfer an accepted TCP socket to the CLI dispatcher.
 * @return Native socket value, or -1 on failure.
 */
intptr_t kc_netl_cli_take_peer(kc_netl_peer_t *peer);

/**
 * Print command usage.
 * @param name Program executable name.
 * @return None.
 */
static void cli_help(const char *name) {
    printf("Usage:\n");
    printf("  %s <addr>[:port] [--tcp|--udp] <command>\n\n", name);
    printf("Options:\n");
    printf("  --tcp          Listen for TCP connections (default)\n");
    printf("  --udp          Listen for UDP datagrams\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Parse host and optional port.
 * @param text Input address text.
 * @param host Destination host buffer.
 * @param host_cap Host buffer capacity.
 * @param port Destination port.
 * @return Zero on success, otherwise nonzero.
 */
static int cli_parse_address(
    const char *text,
    char *host,
    size_t host_cap,
    unsigned short *port
) {
    const char *colon;
    char *end;
    unsigned long value;
    size_t host_len;

    if (
        text == NULL ||
        text[0] == '\0' ||
        host == NULL ||
        host_cap == 0U ||
        port == NULL
    ) {
        return 1;
    }

    colon = strrchr(text, ':');
    if (colon == NULL) {
        host_len = strlen(text);
        if (host_len >= host_cap) return 1;
        memcpy(host, text, host_len + 1U);
        *port = 80U;
        return 0;
    }

    if (colon == text || colon[1] == '\0') return 1;
    host_len = (size_t)(colon - text);
    if (host_len >= host_cap) return 1;
    memcpy(host, text, host_len);
    host[host_len] = '\0';

    value = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || value == 0UL || value > 65535UL) return 1;
    *port = (unsigned short)value;
    return 0;
}

/**
 * Join command-line command arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param start First command argument.
 * @param out Destination command buffer.
 * @param out_cap Destination capacity.
 * @return Zero on success, otherwise nonzero.
 */
static int cli_join_command(
    int argc,
    char **argv,
    int start,
    char *out,
    size_t out_cap
) {
    size_t used = 0U;
    int i;

    if (start >= argc || out_cap == 0U) return 1;
    out[0] = '\0';

    for (i = start; i < argc; i++) {
        size_t len = strlen(argv[i]);

        if (used != 0U) {
            if (used + 1U >= out_cap) return 1;
            out[used++] = ' ';
        }
        if (len >= out_cap - used) return 1;
        memcpy(out + used, argv[i], len);
        used += len;
        out[used] = '\0';
    }

    return used == 0U ? 1 : 0;
}

#ifndef _WIN32
/**
 * Reap completed command processes.
 * @param signal_number Signal number.
 * @return None.
 */
static void cli_reap(int signal_number) {
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

/**
 * Dispatch one accepted TCP socket to a shell command.
 * @param native_socket Native accepted socket.
 * @param command Shell command.
 * @return Zero when launched, otherwise nonzero.
 */
static int cli_dispatch_tcp(
    intptr_t native_socket,
    const char *command
) {
    int fd = (int)native_socket;
    pid_t pid = fork();

    if (pid < 0) {
        close(fd);
        return 1;
    }
    if (pid == 0) {
        if (dup2(fd, STDIN_FILENO) < 0 ||
            dup2(fd, STDOUT_FILENO) < 0) {
            _exit(1);
        }
        if (fd > STDERR_FILENO) close(fd);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(1);
    }

    close(fd);
    return 0;
}

/**
 * Dispatch one UDP datagram to a shell command.
 * @param command Shell command.
 * @param data Datagram bytes.
 * @param data_size Datagram size.
 * @return Zero when launched, otherwise nonzero.
 */
static int cli_dispatch_udp(
    const char *command,
    const void *data,
    size_t data_size
) {
    pid_t worker = fork();

    if (worker < 0) return 1;
    if (worker == 0) {
        int pipefd[2];
        pid_t child;
        size_t offset = 0U;

        if (pipe(pipefd) != 0) _exit(1);
        child = fork();
        if (child < 0) _exit(1);
        if (child == 0) {
            close(pipefd[1]);
            if (dup2(pipefd[0], STDIN_FILENO) < 0) _exit(1);
            if (pipefd[0] > STDERR_FILENO) close(pipefd[0]);
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
            _exit(1);
        }

        close(pipefd[0]);
        while (offset < data_size) {
            ssize_t written = write(
                pipefd[1],
                (const unsigned char *)data + offset,
                data_size - offset
            );
            if (written <= 0) break;
            offset += (size_t)written;
        }
        close(pipefd[1]);
        (void)waitpid(child, NULL, 0);
        _exit(offset == data_size ? 0 : 1);
    }

    return 0;
}
#else
typedef struct {
    SOCKET socket;
    char command[NETL_CLI_COMMAND_SIZE];
} cli_tcp_worker_t;

typedef struct {
    SOCKET socket;
    HANDLE pipe;
} cli_socket_pipe_t;

/**
 * Relay socket bytes into child stdin.
 * @param arg Relay state.
 * @return Thread result.
 */
static DWORD WINAPI cli_socket_to_pipe(LPVOID arg) {
    cli_socket_pipe_t *relay = (cli_socket_pipe_t *)arg;
    char buffer[8192];
    int received;
    DWORD written;

    while ((received = recv(
        relay->socket,
        buffer,
        (int)sizeof(buffer),
        0
    )) > 0) {
        if (!WriteFile(
                relay->pipe,
                buffer,
                (DWORD)received,
                &written,
                NULL
            )) {
            break;
        }
    }

    CloseHandle(relay->pipe);
    return 0;
}

/**
 * Relay child stdout bytes into the socket.
 * @param arg Relay state.
 * @return Thread result.
 */
static DWORD WINAPI cli_pipe_to_socket(LPVOID arg) {
    cli_socket_pipe_t *relay = (cli_socket_pipe_t *)arg;
    char buffer[8192];
    DWORD received;

    while (ReadFile(
        relay->pipe,
        buffer,
        (DWORD)sizeof(buffer),
        &received,
        NULL
    ) && received != 0U) {
        size_t offset = 0U;

        while (offset < (size_t)received) {
            int sent = send(
                relay->socket,
                buffer + offset,
                (int)((size_t)received - offset),
                0
            );
            if (sent <= 0) {
                CloseHandle(relay->pipe);
                return 0;
            }
            offset += (size_t)sent;
        }
    }

    CloseHandle(relay->pipe);
    return 0;
}

/**
 * Run one command for one accepted TCP socket.
 * @param arg Worker state.
 * @return Thread result.
 */
static DWORD WINAPI cli_tcp_worker(LPVOID arg) {
    cli_tcp_worker_t *worker = (cli_tcp_worker_t *)arg;
    SECURITY_ATTRIBUTES attributes = {
        sizeof(SECURITY_ATTRIBUTES),
        NULL,
        TRUE
    };
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE input_read = NULL;
    HANDLE input_write = NULL;
    HANDLE output_read = NULL;
    HANDLE output_write = NULL;
    HANDLE input_thread = NULL;
    HANDLE output_thread = NULL;
    cli_socket_pipe_t input_relay;
    cli_socket_pipe_t output_relay;
    char command[NETL_CLI_COMMAND_SIZE];

    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);

    if (!CreatePipe(
            &input_read,
            &input_write,
            &attributes,
            0
        )) {
        goto done;
    }
    if (!CreatePipe(
            &output_read,
            &output_write,
            &attributes,
            0
        )) {
        goto done;
    }

    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = output_write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    snprintf(command, sizeof(command), "%s", worker->command);
    if (!CreateProcessA(
            NULL,
            command,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &process
        )) {
        goto done;
    }

    CloseHandle(input_read);
    input_read = NULL;
    CloseHandle(output_write);
    output_write = NULL;

    input_relay.socket = worker->socket;
    input_relay.pipe = input_write;
    output_relay.socket = worker->socket;
    output_relay.pipe = output_read;

    input_thread = CreateThread(
        NULL,
        0,
        cli_socket_to_pipe,
        &input_relay,
        0,
        NULL
    );
    output_thread = CreateThread(
        NULL,
        0,
        cli_pipe_to_socket,
        &output_relay,
        0,
        NULL
    );

    WaitForSingleObject(process.hProcess, INFINITE);
    shutdown(worker->socket, SD_RECEIVE);
    if (input_thread != NULL) {
        WaitForSingleObject(input_thread, INFINITE);
        CloseHandle(input_thread);
        input_write = NULL;
    }
    if (output_thread != NULL) {
        WaitForSingleObject(output_thread, INFINITE);
        CloseHandle(output_thread);
        output_read = NULL;
    }

done:
    if (process.hProcess != NULL) CloseHandle(process.hProcess);
    if (process.hThread != NULL) CloseHandle(process.hThread);
    if (input_read != NULL) CloseHandle(input_read);
    if (input_write != NULL) CloseHandle(input_write);
    if (output_read != NULL) CloseHandle(output_read);
    if (output_write != NULL) CloseHandle(output_write);
    closesocket(worker->socket);
    free(worker);
    return 0;
}

/**
 * Dispatch one accepted TCP socket to a command worker.
 * @param native_socket Native accepted socket.
 * @param command Command line.
 * @return Zero when launched, otherwise nonzero.
 */
static int cli_dispatch_tcp(
    intptr_t native_socket,
    const char *command
) {
    cli_tcp_worker_t *worker;
    HANDLE thread;

    worker = (cli_tcp_worker_t *)calloc(1, sizeof(*worker));
    if (worker == NULL) {
        closesocket((SOCKET)native_socket);
        return 1;
    }

    worker->socket = (SOCKET)native_socket;
    snprintf(worker->command, sizeof(worker->command), "%s", command);
    thread = CreateThread(NULL, 0, cli_tcp_worker, worker, 0, NULL);
    if (thread == NULL) {
        closesocket(worker->socket);
        free(worker);
        return 1;
    }

    CloseHandle(thread);
    return 0;
}

typedef struct {
    char command[NETL_CLI_COMMAND_SIZE];
    unsigned char *data;
    size_t data_size;
} cli_udp_worker_t;

/**
 * Run one UDP command without blocking the listener loop.
 * @param arg Worker state.
 * @return Thread result.
 */
static DWORD WINAPI cli_udp_worker(LPVOID arg) {
    cli_udp_worker_t *worker = (cli_udp_worker_t *)arg;
    SECURITY_ATTRIBUTES attributes = {
        sizeof(SECURITY_ATTRIBUTES),
        NULL,
        TRUE
    };
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    HANDLE input_read = NULL;
    HANDLE input_write = NULL;
    char command_line[NETL_CLI_COMMAND_SIZE];
    DWORD written = 0U;

    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));

    if (!CreatePipe(
            &input_read,
            &input_write,
            &attributes,
            0
        )) {
        goto done;
    }

    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    snprintf(command_line, sizeof(command_line), "%s", worker->command);

    if (!CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &process
        )) {
        goto done;
    }

    CloseHandle(input_read);
    input_read = NULL;
    if (worker->data_size != 0U) {
        (void)WriteFile(
            input_write,
            worker->data,
            (DWORD)worker->data_size,
            &written,
            NULL
        );
    }
    CloseHandle(input_write);
    input_write = NULL;
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);

done:
    if (input_read != NULL) CloseHandle(input_read);
    if (input_write != NULL) CloseHandle(input_write);
    free(worker->data);
    free(worker);
    return 0;
}

/**
 * Dispatch one UDP datagram to an asynchronous command worker.
 * @param command Command line.
 * @param data Datagram bytes.
 * @param data_size Datagram size.
 * @return Zero when launched, otherwise nonzero.
 */
static int cli_dispatch_udp(
    const char *command,
    const void *data,
    size_t data_size
) {
    cli_udp_worker_t *worker;
    HANDLE thread;

    worker = (cli_udp_worker_t *)calloc(1, sizeof(*worker));
    if (worker == NULL) return 1;
    snprintf(worker->command, sizeof(worker->command), "%s", command);
    worker->data_size = data_size;

    if (data_size != 0U) {
        worker->data = (unsigned char *)malloc(data_size);
        if (worker->data == NULL) {
            free(worker);
            return 1;
        }
        memcpy(worker->data, data, data_size);
    }

    thread = CreateThread(NULL, 0, cli_udp_worker, worker, 0, NULL);
    if (thread == NULL) {
        free(worker->data);
        free(worker);
        return 1;
    }

    CloseHandle(thread);
    return 0;
}
#endif

typedef struct {
    const char *command;
    atomic_int failed;
} cli_listener_state_t;

/**
 * Dispatch one received UDP datagram.
 * @return None.
 */
static void cli_on_input(
    const kc_netl_input_t *input,
    void *userdata
) {
    cli_listener_state_t *state = (cli_listener_state_t *)userdata;

    if (input->protocol != KC_NETL_UDP) return;
    if (cli_dispatch_udp(state->command, input->data, input->data_size) != 0) {
        fprintf(stderr, "netl: failed to dispatch datagram\n");
    }
}

/**
 * Dispatch one accepted TCP peer through the CLI socket handoff.
 * @return None.
 */
static void cli_on_accept(
    kc_netl_peer_t *peer,
    void *userdata
) {
    cli_listener_state_t *state = (cli_listener_state_t *)userdata;
    intptr_t native_socket = kc_netl_cli_take_peer(peer);

    if (
        native_socket == (intptr_t)-1 ||
        cli_dispatch_tcp(native_socket, state->command) != 0
    ) {
        fprintf(stderr, "netl: failed to dispatch connection\n");
    }
}

/**
 * Record a terminal listener failure.
 * @return None.
 */
static void cli_on_error(int status, void *userdata) {
    cli_listener_state_t *state = (cli_listener_state_t *)userdata;

    fprintf(stderr, "netl: %s\n", kc_netl_strerror(status));
    atomic_store(&state->failed, 1);
}

/**
 * Sleep briefly while the listener worker owns network dispatch.
 * @return None.
 */
static void cli_wait_tick(void) {
#ifdef _WIN32
    Sleep(100);
#else
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Run the foreground command-dispatch listener.
 * @return Process status.
 */
static int cli_run(
    const char *host,
    unsigned short port,
    int protocol,
    const char *command
) {
    kc_netl_options_t options;
    kc_netl_t *listener = NULL;
    cli_listener_state_t state;
    int rc;

    memset(&options, 0, sizeof(options));
    options.host = host;
    options.port = port;
    options.protocol = protocol;

    state.command = command;
    atomic_init(&state.failed, 0);

#ifndef _WIN32
    signal(SIGCHLD, cli_reap);
#endif

    rc = kc_netl_cli_open(
        &listener,
        &options,
        cli_on_input,
        cli_on_error,
        &state,
        protocol == KC_NETL_TCP ? cli_on_accept : NULL,
        &state
    );
    if (rc != KC_NETL_OK) {
        fprintf(stderr, "netl: %s\n", kc_netl_strerror(rc));
        return 1;
    }

    while (!atomic_load(&state.failed)) {
        cli_wait_tick();
    }

    kc_netl_close(listener);
    return 1;
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    char host[256];
    char command[NETL_CLI_COMMAND_SIZE];
    unsigned short port;
    int protocol = KC_NETL_TCP;
    int command_start;
    int i;

    if (
        argc == 2 &&
        (
            strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0
        )
    ) {
        cli_help(argv[0]);
        return 0;
    }
    if (
        argc == 2 &&
        (
            strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0
        )
    ) {
        printf(
            "netl build %llu\n",
            (unsigned long long)kc_netl_version()
        );
        return 0;
    }
    if (argc < 3) {
        cli_help(argv[0]);
        return 1;
    }
    if (cli_parse_address(
            argv[1],
            host,
            sizeof(host),
            &port
        ) != 0) {
        fprintf(stderr, "netl: invalid address '%s'\n", argv[1]);
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            protocol = KC_NETL_TCP;
        } else if (strcmp(argv[i], "--udp") == 0) {
            protocol = KC_NETL_UDP;
        } else {
            break;
        }
    }
    command_start = i;

    if (cli_join_command(
            argc,
            argv,
            command_start,
            command,
            sizeof(command)
        ) != 0) {
        fprintf(stderr, "netl: missing or oversized command\n");
        return 1;
    }

    return cli_run(host, port, protocol, command);
}
