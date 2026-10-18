/**
 * http.c - HTTP parser and builder command-line interface.
 * Summary: Parses HTTP bytes and builds request or response wire messages.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: GNU Lesser General Public License v3.0
 */

#include "libhttp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int http_cli_failed;

/**
 * Prints CLI usage.
 * @param name Program name.
 * @return None.
 */
static void http_cli_help(const char *name) {
    printf("Usage: %s parse < input.http\n", name);
    printf("       %s request [method] [target] [body]\n", name);
    printf("       %s response [status] [body]\n", name);
    printf("\nCommands:\n");
    printf("  parse     Read and describe one HTTP request or response from stdin\n");
    printf("  request   Build one HTTP/1.1 request\n");
    printf("  response  Build one HTTP/1.1 response\n");
}

/**
 * Records a parser error.
 * @param status HTTP status.
 * @param userdata Unused callback state.
 * @return None.
 */
static void http_cli_error(int status, void *userdata) {
    (void)userdata;
    fprintf(stderr, "http: %s\n", kc_http_strerror(status));
    http_cli_failed = 1;
}

/**
 * Prints one parsed request.
 * @param request Parsed request.
 * @param userdata Unused callback state.
 * @return None.
 */
static void http_cli_request(const kc_http_request_t *request, void *userdata) {
    (void)userdata;
    printf("request\t%s\t%s\t%zu\n", request->method, request->target,
        request->body_size);
}

/**
 * Prints one parsed response.
 * @param response Parsed response.
 * @param userdata Unused callback state.
 * @return None.
 */
static void http_cli_response(const kc_http_response_t *response, void *userdata) {
    (void)userdata;
    printf("response\t%d\t%s\t%zu\n", response->status, response->reason,
        response->body_size);
}

/**
 * Parses HTTP bytes from standard input.
 * @return Process status.
 */
static int http_cli_parse(void) {
    kc_http_parser_t *parser = NULL;
    unsigned char buffer[4096];
    size_t count;

    if (kc_http_parser_open(&parser, http_cli_request, http_cli_response,
            http_cli_error, NULL) != KC_HTTP_OK) return 1;
    http_cli_failed = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), stdin)) != 0U) {
        if (kc_http_parser_write(parser, buffer, count) != KC_HTTP_OK) {
            http_cli_failed = 1;
            break;
        }
    }
    if (ferror(stdin)) http_cli_failed = 1;
    kc_http_parser_close(parser);
    return http_cli_failed;
}

/**
 * Builds a request from command arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
static int http_cli_request_build(int argc, char **argv) {
    kc_http_request_build_t request;
    void *data = NULL;
    size_t size = 0U;
    int status;

    memset(&request, 0, sizeof(request));
    if (argc > 2) request.method = argv[2];
    if (argc > 3) request.target = argv[3];
    if (argc > 4) {
        request.body = argv[4];
        request.body_size = strlen(argv[4]);
    }
    status = kc_http_request(&request, &data, &size);
    if (status != KC_HTTP_OK) {
        fprintf(stderr, "http: %s\n", kc_http_strerror(status));
        return 1;
    }
    if (fwrite(data, 1, size, stdout) != size) status = KC_HTTP_EPARSE;
    kc_http_free(data);
    return status != KC_HTTP_OK;
}

/**
 * Builds a response from command arguments.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
static int http_cli_response_build(int argc, char **argv) {
    kc_http_response_build_t response;
    int code = 200;
    void *data = NULL;
    size_t size = 0U;
    int status;

    memset(&response, 0, sizeof(response));
    if (argc > 2) code = atoi(argv[2]);
    if (code < 100 || code > 999) return 1;
    response.status = &code;
    if (argc > 3) {
        response.body = argv[3];
        response.body_size = strlen(argv[3]);
    }
    status = kc_http_response(&response, &data, &size);
    if (status != KC_HTTP_OK) return 1;
    if (fwrite(data, 1, size, stdout) != size) status = KC_HTTP_EPARSE;
    kc_http_free(data);
    return status != KC_HTTP_OK;
}

/**
 * Runs the HTTP command-line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        http_cli_help(argv[0]);
        return 0;
    }
    if (argc == 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("%llu\n", (unsigned long long)kc_http_version());
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "parse")) return http_cli_parse();
    if (argc >= 2 && !strcmp(argv[1], "request")) return http_cli_request_build(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "response")) return http_cli_response_build(argc, argv);
    http_cli_help(argv[0]);
    return 1;
}
