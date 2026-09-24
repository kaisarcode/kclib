/**
 * http.c - HTTP protocol parser and builder CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libhttp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#define HTTP_CLI_FIELD_MAX 256

typedef struct {
    int all;
    int emitted;
    int failed;
} cli_parse_state_t;

static int cli_stream_read(void *data, size_t size) {
#ifdef _WIN32
    return _read(_fileno(stdin), data, (unsigned int)size);
#else
    ssize_t count = read(fileno(stdin), data, size);
    return count < 0 ? -1 : (int)count;
#endif
}

static void cli_help(const char *name) {
    printf("Usage: %s <command> [options]\n\n", name);
    printf("Commands:\n");
    printf("  parse [--all]                   Parse one HTTP message from stdin\n");
    printf("  build request [options]         Build an HTTP request from stdin body\n");
    printf("  build response [options]        Build an HTTP response from stdin body\n\n");
    printf("Parse options:\n");
    printf("  --all                           Print protocol metadata and all parsed fields\n\n");
    printf("Request options:\n");
    printf("  --method <method>               HTTP method (default: GET)\n");
    printf("  --target <target>               Request target (default: /)\n");
    printf("  --version <version>             HTTP version (default: 1.1)\n");
    printf("  --header <name: value>          Add a header\n");
    printf("  --chunked                       Use chunked transfer encoding\n");
    printf("  --chunk-size <n>                Chunk size (default: 8192)\n\n");
    printf("Response options:\n");
    printf("  --status <code>                 HTTP status (default: 200)\n");
    printf("  --reason <phrase>               Reason phrase (default: derived)\n");
    printf("  --version <version>             HTTP version (default: 1.1)\n");
    printf("  --header <name: value>          Add a header\n");
    printf("  --chunked                       Use chunked transfer encoding\n");
    printf("  --chunk-size <n>                Chunk size (default: 8192)\n");
    printf("  --trailer <name: value>         Add a trailer\n\n");
    printf("Common options:\n");
    printf("  -h, --help                      Show this help\n");
    printf("  -v, --version                   Show version\n");
}

static int cli_split_field(const char *text, kc_http_field_t *field) {
    const char *colon;
    char *name;
    char *value;
    size_t name_len;

    colon = strchr(text, ':');
    if (colon == NULL) return -1;
    name_len = (size_t)(colon - text);
    while (name_len > 0U && (text[name_len - 1U] == ' ' || text[name_len - 1U] == '\t')) name_len--;
    while (*++colon == ' ' || *colon == '\t') {}

    name = (char *)malloc(name_len + 1U);
    if (name == NULL) return -1;
    memcpy(name, text, name_len);
    name[name_len] = '\0';
    value = (char *)malloc(strlen(colon) + 1U);
    if (value == NULL) {
        free(name);
        return -1;
    }
    strcpy(value, colon);
    field->name = name;
    field->value = value;
    return 0;
}

static void cli_free_fields(kc_http_field_t *fields, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        free((void *)fields[i].name);
        free((void *)fields[i].value);
    }
}

static void cli_print_fields(const kc_http_field_t *fields, size_t count, const char *prefix) {
    size_t i;
    for (i = 0; i < count; i++) {
        printf("%s%s=%s\n", prefix, fields[i].name, fields[i].value);
    }
}

static void cli_request_cb(const kc_http_request_t *request, void *userdata) {
    cli_parse_state_t *state = (cli_parse_state_t *)userdata;
    if (state->emitted) return;

    if (state->all) {
        printf("http.type=request\n");
        printf("http.version=%s\n", request->version ? request->version : "");
    }
    printf("request.method=%s\n", request->method ? request->method : "");
    printf("request.target=%s\n", request->target ? request->target : "");
    printf("request.path=%s\n", request->path ? request->path : "");
    printf("request.query=%s\n", request->query ? request->query : "");
    cli_print_fields(request->headers, request->header_count, "header.");
    if (state->all) {
        printf("body.length=%zu\n", request->body_size);
        cli_print_fields(request->trailers, request->trailer_count, "trailer.");
    }
    if (request->body_size != 0U) {
        printf("\n");
        fwrite(request->body, 1, request->body_size, stdout);
    }
    state->emitted = 1;
}

static void cli_response_cb(const kc_http_response_t *response, void *userdata) {
    cli_parse_state_t *state = (cli_parse_state_t *)userdata;
    if (state->emitted) return;

    if (state->all) {
        printf("http.type=response\n");
        printf("http.version=%s\n", response->version ? response->version : "");
    }
    printf("response.status=%d\n", response->status);
    printf("response.reason=%s\n", response->reason ? response->reason : "");
    cli_print_fields(response->headers, response->header_count, "header.");
    if (state->all) {
        printf("body.length=%zu\n", response->body_size);
        cli_print_fields(response->trailers, response->trailer_count, "trailer.");
    }
    if (response->body_size != 0U) {
        printf("\n");
        fwrite(response->body, 1, response->body_size, stdout);
    }
    state->emitted = 1;
}

static void cli_error_cb(int status, void *userdata) {
    cli_parse_state_t *state = (cli_parse_state_t *)userdata;
    state->failed = status;
}

static int cli_parse(int all) {
    unsigned char buf[8192];
    kc_http_parser_t *parser = NULL;
    cli_parse_state_t state;
    int rc;

    memset(&state, 0, sizeof(state));
    state.all = all;

    rc = kc_http_parser_open(&parser, cli_request_cb, cli_response_cb, cli_error_cb, &state);
    if (rc != KC_HTTP_OK) {
        fprintf(stderr, "http: %s\n", kc_http_strerror(rc));
        return 1;
    }

    while (!state.emitted) {
        int n = cli_stream_read(buf, sizeof(buf));
        if (n < 0) {
            state.failed = KC_HTTP_EPARSE;
            break;
        }
        if (n == 0) break;
        rc = kc_http_parser_write(parser, buf, (size_t)n);
        if (rc != KC_HTTP_OK) break;
    }

    kc_http_parser_close(parser);
    if (state.failed != 0 || !state.emitted) {
        fprintf(stderr, "http: %s\n", kc_http_strerror(
            state.failed != 0 ? state.failed : KC_HTTP_EPARSE));
        return 1;
    }
    return 0;
}

static int cli_build(int response_mode, int argc, char **argv, int start) {
    kc_http_field_t headers[HTTP_CLI_FIELD_MAX];
    kc_http_field_t trailers[HTTP_CLI_FIELD_MAX];
    size_t header_count = 0U;
    size_t trailer_count = 0U;
    const char *method = NULL;
    const char *target = NULL;
    const char *version = NULL;
    const char *reason = NULL;
    int status = 0;
    int chunked = 0;
    size_t chunk_size = 0U;
    unsigned char *body = NULL;
    size_t body_size = 0U;
    size_t body_cap = 0U;
    void *wire = NULL;
    size_t wire_size = 0U;
    int i;
    int rc = 1;

    memset(headers, 0, sizeof(headers));
    memset(trailers, 0, sizeof(trailers));

    for (i = start; i < argc; i++) {
        if (!response_mode &&
            strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            method = argv[++i];
        } else if (!response_mode &&
                   strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) {
            version = argv[++i];
        } else if (response_mode &&
                   strcmp(argv[i], "--status") == 0 && i + 1 < argc) {
            status = atoi(argv[++i]);
        } else if (response_mode &&
                   strcmp(argv[i], "--reason") == 0 && i + 1 < argc) {
            reason = argv[++i];
        }
        else if (strcmp(argv[i], "--chunked") == 0) chunked = 1;
        else if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc) chunk_size = (size_t)strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--header") == 0 && i + 1 < argc && header_count < HTTP_CLI_FIELD_MAX) {
            if (cli_split_field(argv[++i], &headers[header_count]) != 0) goto done;
            header_count++;
        } else if (response_mode &&
                   strcmp(argv[i], "--trailer") == 0 &&
                   i + 1 < argc &&
                   trailer_count < HTTP_CLI_FIELD_MAX) {
            if (cli_split_field(argv[++i], &trailers[trailer_count]) != 0) goto done;
            trailer_count++;
        } else {
            fprintf(stderr, "http: invalid option '%s'\n", argv[i]);
            goto done;
        }
    }

    for (;;) {
        unsigned char tmp[8192];
        size_t n = fread(tmp, 1, sizeof(tmp), stdin);
        if (n != 0U) {
            unsigned char *next;
            if (body_size + n < body_size) goto done;
            if (body_size + n > body_cap) {
                size_t cap = body_cap ? body_cap * 2U : 8192U;
                while (cap < body_size + n) cap *= 2U;
                next = (unsigned char *)realloc(body, cap);
                if (next == NULL) goto done;
                body = next;
                body_cap = cap;
            }
            memcpy(body + body_size, tmp, n);
            body_size += n;
        }
        if (n < sizeof(tmp)) {
            if (ferror(stdin)) goto done;
            break;
        }
    }

    if (response_mode) {
        kc_http_response_t response;
        memset(&response, 0, sizeof(response));
        response.version = version;
        response.status = status;
        response.reason = reason;
        response.headers = headers;
        response.header_count = header_count;
        response.body = body;
        response.body_size = body_size;
        response.trailers = trailers;
        response.trailer_count = trailer_count;
        response.chunked = chunked;
        response.chunk_size = chunk_size;
        rc = kc_http_response(&response, &wire, &wire_size);
    } else {
        kc_http_request_t request;
        memset(&request, 0, sizeof(request));
        request.version = version;
        request.method = method;
        request.target = target;
        request.headers = headers;
        request.header_count = header_count;
        request.body = body;
        request.body_size = body_size;
        request.trailers = trailers;
        request.trailer_count = trailer_count;
        request.chunked = chunked;
        request.chunk_size = chunk_size;
        rc = kc_http_request(&request, &wire, &wire_size);
    }

    if (rc != KC_HTTP_OK) {
        fprintf(stderr, "http: %s\n", kc_http_strerror(rc));
        rc = 1;
        goto done;
    }

    if (wire_size != 0U) fwrite(wire, 1, wire_size, stdout);
    rc = 0;

done:
    kc_http_free(wire);
    free(body);
    cli_free_fields(headers, header_count);
    cli_free_fields(trailers, trailer_count);
    return rc;
}

int main(int argc, char **argv) {
    int i = 1;

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    if (argc < 2) {
        fprintf(stderr, "http: missing command\n");
        return 1;
    }
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
        cli_help(argv[0]);
        return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
        printf("http build %llu\n", (unsigned long long)kc_http_version());
        return 0;
    }

    if (strcmp(argv[i], "parse") == 0) {
        int all = 0;
        i++;
        while (i < argc) {
            if (strcmp(argv[i], "--all") == 0) all = 1;
            else {
                fprintf(stderr, "http: invalid option '%s'\n", argv[i]);
                return 1;
            }
            i++;
        }
        return cli_parse(all);
    }

    if (strcmp(argv[i], "build") == 0 && i + 1 < argc) {
        if (strcmp(argv[i + 1], "request") == 0) return cli_build(0, argc, argv, i + 2);
        if (strcmp(argv[i + 1], "response") == 0) return cli_build(1, argc, argv, i + 2);
        fprintf(stderr, "http: unknown build subcommand '%s'\n", argv[i + 1]);
        return 1;
    }

    fprintf(stderr, "http: unknown command '%s'\n", argv[i]);
    return 1;
}
