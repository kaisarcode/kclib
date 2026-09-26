/**
 * nets.c - Network sender.
 * Summary: Command line interface for sending stdin to TCP, UDP, or TLS.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "libnets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define KC_NETS_STDIN_FD 0
#else
#include <pthread.h>
#include <unistd.h>
#define KC_NETS_STDIN_FD STDIN_FILENO
#endif

typedef struct {
    int done;
    int status;
#ifdef _WIN32
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE condition;
#else
    pthread_mutex_t lock;
    pthread_cond_t condition;
#endif
} nets_cli_result_t;

/**
 * Parses a host, host:port, bracketed IPv6, or URL-shaped target.
 * URL schemes select transport defaults only.
 * @param text      Input target text.
 * @param host      Output host buffer.
 * @param host_cap  Output host capacity.
 * @param port      Output port pointer.
 * @param proto     Output protocol pointer.
 * @return 0 on success, or 1 on failure.
 */
static int nets_parse_target(
    const char *text,
    char *host,
    size_t host_cap,
    unsigned short *port,
    int *proto
) {
    const char *authority;
    const char *authority_end;
    const char *scheme_end;
    const char *host_begin;
    const char *host_end;
    const char *port_begin;
    char *end;
    unsigned long value;
    size_t n;

    if (!text || !text[0] || !host || host_cap == 0 || !port || !proto) return 1;

    authority = text;
    authority_end = text + strlen(text);
    *port = 80;

    scheme_end = strstr(text, "://");
    if (scheme_end) {
        n = (size_t)(scheme_end - text);
        if (n == 4 && strncmp(text, "http", 4) == 0) {
            *port = 80;
            *proto = KC_NETS_TCP;
        } else if (n == 5 && strncmp(text, "https", 5) == 0) {
            *port = 443;
            *proto = KC_NETS_TLS;
        } else if (n == 3 && strncmp(text, "tcp", 3) == 0) {
            *port = 80;
            *proto = KC_NETS_TCP;
        } else if (n == 3 && strncmp(text, "udp", 3) == 0) {
            *port = 80;
            *proto = KC_NETS_UDP;
        } else {
            return 1;
        }
        authority = scheme_end + 3;
        authority_end = authority + strcspn(authority, "/?#");
    }

    if (authority == authority_end) return 1;
    if (memchr(authority, '@', (size_t)(authority_end - authority)) != NULL) return 1;

    port_begin = NULL;
    if (*authority == '[') {
        host_begin = authority + 1;
        host_end = memchr(host_begin, ']', (size_t)(authority_end - host_begin));
        if (!host_end || host_end == host_begin) return 1;
        if (host_end + 1 < authority_end) {
            if (host_end[1] != ':') return 1;
            port_begin = host_end + 2;
            if (port_begin == authority_end) return 1;
        } else if (host_end + 1 != authority_end) {
            return 1;
        }
    } else {
        const char *colon;
        const char *cursor;
        int colon_count;

        colon = NULL;
        colon_count = 0;
        for (cursor = authority; cursor < authority_end; cursor++) {
            if (*cursor == ':') {
                colon = cursor;
                colon_count++;
            }
        }
        if (colon_count > 1) return 1;
        host_begin = authority;
        host_end = colon ? colon : authority_end;
        if (colon) {
            port_begin = colon + 1;
            if (port_begin == authority_end) return 1;
        }
    }

    n = (size_t)(host_end - host_begin);
    if (n == 0 || n >= host_cap) return 1;
    memcpy(host, host_begin, n);
    host[n] = '\0';

    if (port_begin) {
        char port_text[6];
        size_t port_len;

        port_len = (size_t)(authority_end - port_begin);
        if (port_len == 0 || port_len >= sizeof(port_text)) return 1;
        memcpy(port_text, port_begin, port_len);
        port_text[port_len] = '\0';
        value = strtoul(port_text, &end, 10);
        if (*end != '\0' || value == 0 || value > 65535) return 1;
        *port = (unsigned short)value;
    }

    return 0;
}

/**
 * Reads all available bytes from a file descriptor.
 * @param fd Source file descriptor.
 * @param out_data Receives owned bytes.
 * @param out_size Receives byte count.
 * @return 0 on success, or 1 on failure.
 */
static int nets_read_fd(int fd, char **out_data, size_t *out_size) {
    char *data;
    size_t used;
    size_t capacity;
    char buffer[8192];
#ifdef _WIN32
    int count;
#else
    ssize_t count;
#endif

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0U;
    if (!out_data || !out_size) return 1;

    data = NULL;
    used = 0U;
    capacity = 0U;

    for (;;) {
#ifdef _WIN32
        count = _read(fd, buffer, (unsigned)sizeof(buffer));
#else
        count = read(fd, buffer, sizeof(buffer));
#endif
        if (count < 0) {
            free(data);
            return 1;
        }
        if (count == 0) break;
        if (used + (size_t)count > capacity) {
            size_t next_capacity;
            char *next;

            next_capacity = capacity ? capacity * 2U : 8192U;
            while (next_capacity < used + (size_t)count) next_capacity *= 2U;
            next = (char *)realloc(data, next_capacity);
            if (!next) {
                free(data);
                return 1;
            }
            data = next;
            capacity = next_capacity;
        }
        memcpy(data + used, buffer, (size_t)count);
        used += (size_t)count;
    }

    if (!data) {
        data = (char *)malloc(1U);
        if (!data) return 1;
    }
    data[used] = '\0';
    *out_data = data;
    *out_size = used;
    return 0;
}

/**
 * Initialize callback completion state.
 * @param result Completion state.
 * @return 0 on success, 1 on failure.
 */
static int nets_result_init(nets_cli_result_t *result) {
    memset(result, 0, sizeof(*result));
#ifdef _WIN32
    InitializeCriticalSection(&result->lock);
    InitializeConditionVariable(&result->condition);
    return 0;
#else
    if (pthread_mutex_init(&result->lock, NULL) != 0) return 1;
    if (pthread_cond_init(&result->condition, NULL) != 0) {
        pthread_mutex_destroy(&result->lock);
        return 1;
    }
    return 0;
#endif
}

/**
 * Destroy callback completion state.
 * @param result Completion state.
 * @return None.
 */
static void nets_result_destroy(nets_cli_result_t *result) {
#ifdef _WIN32
    DeleteCriticalSection(&result->lock);
#else
    pthread_cond_destroy(&result->condition);
    pthread_mutex_destroy(&result->lock);
#endif
}

/**
 * Receive one terminal transfer result.
 * @param status Transfer status.
 * @param data Borrowed response bytes.
 * @param size Response size.
 * @param userdata Completion state.
 * @return None.
 */
static void nets_result_handler(
    int status,
    const void *data,
    size_t size,
    void *userdata
) {
    nets_cli_result_t *result;

    result = (nets_cli_result_t *)userdata;
    if (status == KC_NETS_OK && data != NULL && size > 0U) {
        fwrite(data, 1, size, stdout);
    }

#ifdef _WIN32
    EnterCriticalSection(&result->lock);
    result->status = status;
    result->done = 1;
    WakeConditionVariable(&result->condition);
    LeaveCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
    result->status = status;
    result->done = 1;
    pthread_cond_signal(&result->condition);
    pthread_mutex_unlock(&result->lock);
#endif
}

/**
 * Wait for transfer completion.
 * @param result Completion state.
 * @return Terminal transfer status.
 */
static int nets_result_wait(nets_cli_result_t *result) {
    int status;

#ifdef _WIN32
    EnterCriticalSection(&result->lock);
    while (!result->done) {
        SleepConditionVariableCS(&result->condition, &result->lock, INFINITE);
    }
    status = result->status;
    LeaveCriticalSection(&result->lock);
#else
    pthread_mutex_lock(&result->lock);
    while (!result->done) {
        pthread_cond_wait(&result->condition, &result->lock);
    }
    status = result->status;
    pthread_mutex_unlock(&result->lock);
#endif
    return status;
}

/**
 * Prints command usage information.
 * @param name Program executable name.
 * @return None.
 */
static void kc_nets_help(const char *name) {
    printf("Usage: %s <target> [--tcp|--udp|--tls]\n", name);
    printf("\n");
    printf("Target:\n");
    printf("  host[:port] or http://, https://, tcp://, udp:// URL\n");
    printf("\n");
    printf("Options:\n");
    printf("  --tcp          Use TCP (default)\n");
    printf("  --udp          Use UDP\n");
    printf("  --tls          Use TLS over TCP\n");
    printf("  -h, --help     Show this help\n");
    printf("  -v, --version  Show version\n");
}

/**
 * Print command version information.
 * @return None.
 */
static void kc_nets_cli_version(void) {
    printf("nets build %llu\n", (unsigned long long)kc_nets_version());
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    char *input;
    size_t input_size;
    char host[512];
    unsigned short port;
    int protocol;
    const char *protocol_flag;
    const char *target;
    kc_nets_t *transfer;
    nets_cli_result_t result;
    int rc;
    int i;

    input = NULL;
    input_size = 0U;
    port = 80;
    protocol = KC_NETS_TCP;
    protocol_flag = "tcp";
    target = NULL;
    transfer = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            kc_nets_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            kc_nets_cli_version();
            return 0;
        } else if (strcmp(argv[i], "--tcp") == 0) {
            protocol_flag = "tcp";
        } else if (strcmp(argv[i], "--udp") == 0) {
            protocol_flag = "udp";
        } else if (strcmp(argv[i], "--tls") == 0) {
            protocol_flag = "tls";
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "nets: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (!target) {
            target = argv[i];
        } else {
            fprintf(stderr, "nets: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!target) {
        kc_nets_help(argv[0]);
        return 1;
    }

    if (nets_read_fd(KC_NETS_STDIN_FD, &input, &input_size) != 0) {
        fprintf(stderr, "nets: failed to read stdin\n");
        return 1;
    }

    if (target[0] == '\0') {
        fprintf(stderr, "nets: missing \"target\"\n");
        free(input);
        return 1;
    }

    if (strcmp(protocol_flag, "tls") == 0) {
        if (!kc_nets_tls_available()) {
            fprintf(stderr, "nets: TLS not available (compiled without OpenSSL)\n");
            free(input);
            return 1;
        }
        protocol = KC_NETS_TLS;
    } else if (strcmp(protocol_flag, "udp") == 0) {
        protocol = KC_NETS_UDP;
    }

    if (nets_parse_target(target, host, sizeof(host), &port, &protocol) != 0) {
        fprintf(stderr, "nets: invalid target\n");
        free(input);
        return 1;
    }

    if (protocol == KC_NETS_TLS && !kc_nets_tls_available()) {
        fprintf(stderr, "nets: TLS not available (compiled without OpenSSL)\n");
        free(input);
        return 1;
    }

    if (nets_result_init(&result) != 0) {
        fprintf(stderr, "nets: out of memory\n");
        free(input);
        return 1;
    }

    rc = kc_nets_send(
        &transfer,
        host,
        port,
        protocol,
        input,
        input_size,
        nets_result_handler,
        &result
    );
    free(input);

    if (rc != KC_NETS_OK) {
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(rc));
        nets_result_destroy(&result);
        return 1;
    }

    rc = nets_result_wait(&result);
    kc_nets_close(transfer);
    nets_result_destroy(&result);

    if (rc != KC_NETS_OK) {
        fprintf(stderr, "nets: %s\n", kc_nets_strerror(rc));
        return 1;
    }
    return 0;
}
