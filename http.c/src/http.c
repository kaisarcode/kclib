/**
 * http.c - HTTP protocol parser and builder.
 * Summary: Command line interface for parsing and building HTTP messages.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libhttp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_CLI_HDR_MAX 256

/**
 * Print command usage to stdout.
 * @param name Program executable name.
 * @return None.
 */
static void kc_http_help(const char *name) {
    printf("Usage: %s <command> [options]\n", name);
    printf("\n");
    printf("Commands:\n");
    printf("  parse [--all]                   Parse one (or all) HTTP messages from stdin\n");
    printf("  build request  [options]        Build an HTTP request from stdin body\n");
    printf("  build response [options]        Build an HTTP response from stdin body\n");
    printf("\n");
    printf("Parse options:\n");
    printf("  --all                           Parse all messages until EOF\n");
    printf("\n");
    printf("Build request options:\n");
    printf("  --method <method>               HTTP method (default: GET)\n");
    printf("  --target <target>               Request target (default: /)\n");
    printf("  --version <version>             HTTP version (default: 1.1)\n");
    printf("  --header <name: value>          Add a request header\n");
    printf("  --chunked                       Use chunked transfer encoding\n");
    printf("  --chunk-size <n>                Chunk size in bytes (default: 8192)\n");
    printf("\n");
    printf("Build response options:\n");
    printf("  --status <code>                 HTTP status code (default: 200)\n");
    printf("  --reason <phrase>               Reason phrase (default: derived from status)\n");
    printf("  --version <version>             HTTP version (default: 1.1)\n");
    printf("  --header <name: value>          Add a response header\n");
    printf("  --chunked                       Use chunked transfer encoding\n");
    printf("  --chunk-size <n>                Chunk size in bytes (default: 8192)\n");
    printf("  --trailer <name: value>         Add a trailer (chunked only)\n");
    printf("\n");
    printf("Common options:\n");
    printf("  -h, --help                      Show this help\n");
    printf("  -v, --version                   Show version\n");
}

/**
 * Print version information to stdout.
 * @return None.
 */
static void kc_http_print_version(void) {
    printf("http build %llu\n", (unsigned long long)kc_http_version());
}

/**
 * Read all of stdin into a malloc'd buffer.
 * @param out_data Receives malloc'd buffer.
 * @param out_size Receives data size.
 * @return 0 on success, -1 on failure.
 */
static int kc_http_read_stdin(unsigned char **out_data, size_t *out_size) {
    unsigned char *data = NULL;
    size_t used = 0;
    size_t cap = 0;
    unsigned char buf[8192];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (used + n > cap) {
            size_t nc = cap ? cap * 2 : 8192;
            unsigned char *tmp;
            while (nc < used + n) nc *= 2;
            tmp = (unsigned char *)realloc(data, nc);
            if (!tmp) { free(data); return -1; }
            data = tmp;
            cap = nc;
        }
        memcpy(data + used, buf, n);
        used += n;
    }
    if (ferror(stdin)) { free(data); return -1; }
    if (data == NULL) {
        data = (unsigned char *)malloc(1);
        if (data == NULL) return -1;
    }
    *out_data = data;
    *out_size = used;
    return 0;
}

/**
 * Execute the parse operation and write normalized output to stdout.
 * @param all Parse all messages until EOF.
 * @param body Input bytes.
 * @param body_len Input byte count.
 * @return Process exit code.
 */
static int cli_do_parse(int all, const void *body, size_t body_len) {
    kc_http_options_t opts;
    kc_http_t *ctx = NULL;
    unsigned char *out = NULL;
    size_t out_len = 0;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        fprintf(stderr, "http: out of memory\n");
        return 1;
    }
    kc_http_options_free(&opts);

    kc_http_set_op(ctx, KC_HTTP_OP_PARSE);
    if (all) kc_http_set_all(ctx, 1);
    kc_http_set_input(ctx, body, body_len);

    if (kc_http_exec(ctx) != KC_HTTP_OK) {
        kc_http_close(ctx);
        fprintf(stderr, "http: parse failed\n");
        return 1;
    }

    kc_http_get_output(ctx, &out, &out_len);
    if (out != NULL && out_len > 0) {
        fwrite(out, 1, out_len, stdout);
    }
    kc_http_close(ctx);
    return 0;
}

/**
 * Execute a build operation and write wire bytes to stdout.
 * @param response Non-zero for response build, zero for request build.
 * @param method Method string, or NULL for default.
 * @param target Target string, or NULL for default.
 * @param version Version string, or NULL for default.
 * @param status Status code, or 0 for default.
 * @param reason Reason phrase, or NULL for default.
 * @param chunked Non-zero to use chunked encoding.
 * @param chunk_size Chunk size in bytes, or 0 for default.
 * @param hdrs Header list.
 * @param nhdr Header count.
 * @param trl Trailer list.
 * @param ntrl Trailer count.
 * @param body Input bytes.
 * @param body_len Input byte count.
 * @return Process exit code.
 */
static int cli_do_build(int response, const char *method,
    const char *target, const char *version, int status, const char *reason,
    int chunked, unsigned long chunk_size, const char **hdrs, int nhdr,
    const char **trl, int ntrl, const void *body, size_t body_len) {
    kc_http_options_t opts;
    kc_http_t *ctx = NULL;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int i;

    opts = kc_http_options_default();
    if (kc_http_open(&ctx, &opts) != KC_HTTP_OK) {
        kc_http_options_free(&opts);
        fprintf(stderr, "http: out of memory\n");
        return 1;
    }
    kc_http_options_free(&opts);

    kc_http_set_op(ctx, response ? KC_HTTP_OP_BUILD_RESPONSE : KC_HTTP_OP_BUILD_REQUEST);
    if (method)  kc_http_set_method(ctx, method);
    if (target)  kc_http_set_target(ctx, target);
    if (version) kc_http_set_version(ctx, version);
    if (status > 0) kc_http_set_status(ctx, status);
    if (reason)  kc_http_set_reason(ctx, reason);
    if (chunked) kc_http_set_chunked(ctx, 1);
    if (chunk_size > 0) kc_http_set_chunk_size(ctx, (size_t)chunk_size);
    for (i = 0; i < nhdr; i++) {
        if (kc_http_add_header(ctx, hdrs[i]) != KC_HTTP_OK) {
            kc_http_close(ctx);
            fprintf(stderr, "http: too many headers\n");
            return 1;
        }
    }
    for (i = 0; i < ntrl; i++) {
        if (kc_http_add_trailer(ctx, trl[i]) != KC_HTTP_OK) {
            kc_http_close(ctx);
            fprintf(stderr, "http: too many trailers\n");
            return 1;
        }
    }
    kc_http_set_input(ctx, body, body_len);

    if (kc_http_exec(ctx) != KC_HTTP_OK) {
        kc_http_close(ctx);
        fprintf(stderr, "http: %s failed\n",
            response ? "build response" : "build request");
        return 1;
    }

    kc_http_get_output(ctx, &out, &out_len);
    if (out != NULL && out_len > 0) {
        fwrite(out, 1, out_len, stdout);
    }
    kc_http_close(ctx);
    return 0;
}

/**
 * Execute the command line interface.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char **argv) {
    int i = 1;
    const char *cmd = NULL;
    const char *sub = NULL;

    if (i < argc && (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)) {
        kc_http_help(argv[0]);
        return 0;
    }
    if (i < argc && (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)) {
        kc_http_print_version();
        return 0;
    }

    if (i >= argc) {
        fprintf(stderr, "http: missing command\n");
        kc_http_help(argv[0]);
        return 1;
    }

    cmd = argv[i++];

    if (strcmp(cmd, "parse") == 0) {
        unsigned char *stdin_data = NULL;
        size_t stdin_size = 0;
        int all = 0;
        int fail = 0;
        int rc;

        while (i < argc && !fail) {
            if (strcmp(argv[i], "--all") == 0) {
                all = 1;
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                kc_http_help(argv[0]);
                return 0;
            } else {
                fprintf(stderr, "http: unknown option '%s'\n", argv[i]);
                fail = 1;
            }
            i++;
        }

        if (!fail && kc_http_read_stdin(&stdin_data, &stdin_size) != 0) {
            fprintf(stderr, "http: out of memory\n");
            fail = 1;
        }
        if (fail) {
            free(stdin_data);
            return 1;
        }

        rc = cli_do_parse(all, stdin_data, stdin_size);
        free(stdin_data);
        return rc;

    } else if (strcmp(cmd, "build") == 0) {
        const char *method = NULL;
        const char *target = NULL;
        const char *version = NULL;
        const char *reason = NULL;
        const char *hdrs[HTTP_CLI_HDR_MAX];
        const char *trl[HTTP_CLI_HDR_MAX];
        int nhdr = 0;
        int ntrl = 0;
        int status = 0;
        int chunked = 0;
        unsigned long chunk_size = 0;
        unsigned char *stdin_data = NULL;
        size_t stdin_size = 0;
        int response = 0;
        int fail = 0;
        int rc;

        if (i >= argc) {
            fprintf(stderr, "http: 'build' requires a subcommand: request or response\n");
            return 1;
        }
        sub = argv[i++];

        if (strcmp(sub, "request") == 0) {
            response = 0;
        } else if (strcmp(sub, "response") == 0) {
            response = 1;
        } else {
            fprintf(stderr, "http: unknown build subcommand '%s'\n", sub);
            return 1;
        }

        while (i < argc && !fail) {
            if (strcmp(argv[i], "--method") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --method\n");
                    fail = 1;
                } else {
                    method = argv[i];
                }
            } else if (strcmp(argv[i], "--target") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --target\n");
                    fail = 1;
                } else {
                    target = argv[i];
                }
            } else if (strcmp(argv[i], "--version") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --version\n");
                    fail = 1;
                } else {
                    version = argv[i];
                }
            } else if (strcmp(argv[i], "--status") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --status\n");
                    fail = 1;
                } else {
                    status = (int)strtol(argv[i], NULL, 10);
                }
            } else if (strcmp(argv[i], "--reason") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --reason\n");
                    fail = 1;
                } else {
                    reason = argv[i];
                }
            } else if (strcmp(argv[i], "--header") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --header\n");
                    fail = 1;
                } else if (nhdr >= HTTP_CLI_HDR_MAX) {
                    fprintf(stderr, "http: too many headers\n");
                    fail = 1;
                } else {
                    hdrs[nhdr++] = argv[i];
                }
            } else if (strcmp(argv[i], "--trailer") == 0) {
                if (!response) {
                    fprintf(stderr, "http: trailers only supported for build response\n");
                    fail = 1;
                } else if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --trailer\n");
                    fail = 1;
                } else if (ntrl >= HTTP_CLI_HDR_MAX) {
                    fprintf(stderr, "http: too many trailers\n");
                    fail = 1;
                } else {
                    trl[ntrl++] = argv[i];
                }
            } else if (strcmp(argv[i], "--chunked") == 0) {
                chunked = 1;
            } else if (strcmp(argv[i], "--chunk-size") == 0) {
                if (++i >= argc) {
                    fprintf(stderr, "http: missing value for --chunk-size\n");
                    fail = 1;
                } else {
                    chunk_size = strtoul(argv[i], NULL, 10);
                }
            } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                kc_http_help(argv[0]);
                return 0;
            } else {
                fprintf(stderr, "http: unknown option '%s'\n", argv[i]);
                fail = 1;
            }
            i++;
        }

        if (!fail && kc_http_read_stdin(&stdin_data, &stdin_size) != 0) {
            fprintf(stderr, "http: out of memory\n");
            fail = 1;
        }
        if (fail) {
            free(stdin_data);
            return 1;
        }

        rc = cli_do_build(response, method, target, version, status,
            reason, chunked, chunk_size, hdrs, nhdr, trl, ntrl,
            stdin_data, stdin_size);
        free(stdin_data);
        return rc;

    } else {
        fprintf(stderr, "http: unknown command '%s'\n", cmd);
        return 1;
    }
}
