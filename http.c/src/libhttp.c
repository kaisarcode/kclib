/**
 * libhttp.c - HTTP protocol parser and builder.
 * Summary: Shared context and dispatch for HTTP message operations.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700
#endif
#include <signal.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "libhttp.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#define HTTP_HDR_MAX      256
#define HTTP_CHUNK_DEFAULT 8192
#define HTTP_PREFACE_MAX  24

typedef struct {
    char *name;
    char *value;
} http_hdr_t;

typedef struct {
    int            type;
    int            ver_major;
    int            ver_minor;
    char          *method;
    char          *target;
    char          *path;
    char          *query;
    int            status;
    char          *reason;
    http_hdr_t     hdrs[HTTP_HDR_MAX];
    int            nhdr;
    unsigned char *body;
    size_t         body_len;
    http_hdr_t     trailers[HTTP_HDR_MAX];
    int            ntrailer;
} http_msg_t;

typedef struct {
    const unsigned char *data;
    size_t               len;
    size_t               pos;
    unsigned char        pre[HTTP_PREFACE_MAX];
    size_t               pre_n;
    size_t               pre_i;
} http_src_t;

#define HTTP_TYPE_REQUEST  1
#define HTTP_TYPE_RESPONSE 2

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_FLOAT,
    KC_ENV_TYPE_STR
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_HTTP_METHOD",  offsetof(kc_http_options_t, method),  KC_ENV_TYPE_STR },
    { "KC_HTTP_TARGET",  offsetof(kc_http_options_t, target),  KC_ENV_TYPE_STR },
    { "KC_HTTP_VERSION", offsetof(kc_http_options_t, version), KC_ENV_TYPE_STR },
    { "KC_HTTP_CHUNKED", offsetof(kc_http_options_t, chunked), KC_ENV_TYPE_INT },
};
static const int env_config_table_n =
    sizeof(env_config_table) / sizeof(env_config_table[0]);

struct kc_http {
    kc_http_options_t opts;
    volatile sig_atomic_t stop_requested;
    int    op;
    int    all;
    const unsigned char *input;
    size_t               input_len;
    unsigned char *result;
    size_t         result_len;
    char  *method;
    char  *target;
    char  *version;
    int    status;
    char  *reason;
    int    chunked;
    size_t chunk_size;
    char  *hdrs[HTTP_HDR_MAX];
    int    nhdr;
    char  *trailers[HTTP_HDR_MAX];
    int    ntrailer;
};

typedef struct {
    unsigned char *data;
    size_t         len;
    size_t         cap;
} http_buf_t;

/**
 * Initialize a growable buffer to empty.
 * @param b Buffer to initialize.
 * @return None.
 */
static void http_buf_init(http_buf_t *b) {
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

/**
 * Append bytes to a growable buffer.
 * @param b Output buffer.
 * @param p Data to append.
 * @param n Number of bytes.
 * @return 0 on success, -1 on allocation failure.
 */
static int http_buf_write(http_buf_t *b, const void *p, size_t n) {
    size_t nc;
    unsigned char *tmp;

    if (n == 0) return 0;
    if (b->len + n > b->cap) {
        nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->len + n) nc *= 2;
        tmp = (unsigned char *)realloc(b->data, nc);
        if (!tmp) return -1;
        b->data = tmp;
        b->cap  = nc;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

/**
 * Append a formatted string to a growable buffer.
 * @param b Output buffer.
 * @param fmt printf format string.
 * @return 0 on success, -1 on write error.
 */
static int http_buf_printf(http_buf_t *b, const char *fmt, ...) {
    va_list ap;
    char buf[1024];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n < sizeof(buf)) return http_buf_write(b, buf, (size_t)n);
    {
        char *tmp = (char *)malloc((size_t)n + 1U);
        if (!tmp) return -1;
        va_start(ap, fmt);
        vsnprintf(tmp, (size_t)n + 1U, fmt, ap);
        va_end(ap);
        n = http_buf_write(b, tmp, (size_t)n);
        free(tmp);
        return n;
    }
}

/**
 * Duplicate a string, returning a heap-allocated copy.
 * @param s Source string.
 * @return Allocated copy or NULL on failure.
 */
static char *http_strdup(const char *s) {
    size_t n;
    char *out;
    if (!s) return NULL;
    n = strlen(s) + 1;
    out = (char *)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

/**
 * Duplicate a string and convert all bytes to lowercase.
 * @param s Source string.
 * @return Allocated lowercase copy or NULL on failure.
 */
static char *http_strdup_lower(const char *s) {
    char *out = http_strdup(s);
    char *p;
    if (!out) return NULL;
    for (p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
    return out;
}

/**
 * Trim leading and trailing ASCII whitespace in-place.
 * @param s Input string (mutated; original pointer preserved).
 * @return Original pointer s.
 */
static char *http_trim(char *s) {
    char  *start;
    char  *end;
    size_t n;
    if (!s) return s;
    start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == '\0') { s[0] = '\0'; return s; }
    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    n = (size_t)(end - start + 1);
    memmove(s, start, n);
    s[n] = '\0';
    return s;
}

/**
 * Read one byte from a prefixed source.
 * @param src Input source.
 * @return Byte value or EOF.
 */
static int http_src_getc(http_src_t *src) {
    if (src->pre_i < src->pre_n) return src->pre[src->pre_i++];
    if (src->pos < src->len) return src->data[src->pos++];
    return EOF;
}

/**
 * Read bytes from a prefixed source.
 * @param src Input source.
 * @param out Output buffer.
 * @param n Byte count.
 * @return Number of bytes read.
 */
static size_t http_src_read(http_src_t *src, unsigned char *out, size_t n) {
    size_t got = 0;

    while (got < n && src->pre_i < src->pre_n) {
        out[got++] = src->pre[src->pre_i++];
    }
    while (got < n && src->pos < src->len) {
        out[got++] = src->data[src->pos++];
    }
    return got;
}

/**
 * Read one line from a source, stripping the CRLF or LF terminator.
 * @param src Input source.
 * @param out Output string (caller must free).
 * @param out_len Output length.
 * @return 1 on success, 0 on clean EOF with no data, -1 on error.
 */
static int http_read_line(http_src_t *src, char **out, size_t *out_len) {
    char  *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    int    c;

    while ((c = http_src_getc(src)) != EOF) {
        if (c == '\n') break;
        if (c == '\r') continue;
        if (len >= cap) {
            size_t nc = cap ? cap * 2 : 256;
            char  *nb = (char *)realloc(buf, nc + 1);
            if (!nb) { free(buf); return -1; }
            buf = nb;
            cap = nc;
        }
        buf[len++] = (char)c;
    }

    if (c == EOF && len == 0 && !buf) {
        if (out_len) *out_len = 0;
        *out = NULL;
        return 0;
    }

    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) return -1;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    *out = buf;
    return 1;
}

/**
 * Read exactly n bytes from a source.
 * @param src Input source.
 * @param n Byte count.
 * @param out Output buffer (caller must free).
 * @return 0 on success, -1 on error or short read.
 */
static int http_read_bytes(http_src_t *src, size_t n, unsigned char **out) {
    unsigned char *buf;
    size_t got;

    if (n == 0) {
        *out = NULL;
        return 0;
    }
    buf = (unsigned char *)malloc(n + 1);
    if (!buf) return -1;
    got = http_src_read(src, buf, n);
    if (got != n) { free(buf); return -1; }
    buf[n] = '\0';
    *out = buf;
    return 0;
}

/**
 * Free all heap memory owned by an http_msg_t.
 * @param msg Message to free.
 * @return None.
 */
static void http_msg_free(http_msg_t *msg) {
    int i;
    if (!msg) return;
    free(msg->method);
    free(msg->target);
    free(msg->path);
    free(msg->query);
    free(msg->reason);
    for (i = 0; i < msg->nhdr; i++) {
        free(msg->hdrs[i].name);
        free(msg->hdrs[i].value);
    }
    free(msg->body);
    for (i = 0; i < msg->ntrailer; i++) {
        free(msg->trailers[i].name);
        free(msg->trailers[i].value);
    }
    memset(msg, 0, sizeof(*msg));
}

/**
 * Return a standard reason phrase for a status code.
 * @param status HTTP status code.
 * @return Reason phrase string.
 */
static const char *http_default_reason(int status) {
    switch (status) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Content";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Unknown";
    }
}

static int http1_parse_request_line(char *line, http_msg_t *msg);

/**
 * Parse a request line into an http_msg_t.
 * @param line Raw request line text.
 * @param msg Destination message.
 * @return 0 on success, -1 on error.
 */
static int http1_parse_request_line(char *line, http_msg_t *msg) {
    char *sp1, *sp2, *qmark;

    sp1 = strchr(line, ' ');
    if (!sp1) return -1;
    *sp1 = '\0';
    msg->method = http_strdup(line);
    if (!msg->method) return -1;

    sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) {
        msg->ver_major = 0;
        msg->ver_minor = 9;
        msg->target = http_strdup(sp1 + 1);
    } else {
        *sp2 = '\0';
        msg->target = http_strdup(sp1 + 1);
        if (sscanf(sp2 + 1, "HTTP/%d.%d", &msg->ver_major, &msg->ver_minor) != 2) return -1;
    }
    if (!msg->target) return -1;

    qmark = strchr(msg->target, '?');
    if (qmark) {
        msg->path  = (char *)malloc((size_t)(qmark - msg->target) + 1);
        if (!msg->path) return -1;
        memcpy(msg->path, msg->target, (size_t)(qmark - msg->target));
        msg->path[qmark - msg->target] = '\0';
        msg->query = http_strdup(qmark + 1);
        if (!msg->query) return -1;
    } else {
        msg->path = http_strdup(msg->target);
        if (!msg->path) return -1;
    }
    return 0;
}

/**
 * Parse a status line into an http_msg_t.
 * @param line Raw status line text.
 * @param msg Destination message.
 * @return 0 on success, -1 on error.
 */
static int http1_parse_status_line(char *line, http_msg_t *msg) {
    char *sp1, *sp2;

    if (sscanf(line, "HTTP/%d.%d", &msg->ver_major, &msg->ver_minor) != 2) return -1;
    sp1 = strchr(line, ' ');
    if (!sp1) return -1;
    msg->status = (int)strtol(sp1 + 1, &sp2, 10);
    if (sp2 == sp1 + 1) return -1;
    if (sp2 && *sp2 == ' ') {
        msg->reason = http_strdup(sp2 + 1);
        if (!msg->reason) return -1;
    } else {
        msg->reason = http_strdup("");
        if (!msg->reason) return -1;
    }
    return 0;
}

/**
 * Add one parsed header to an http_msg_t.
 * @param line Raw "name: value" header line.
 * @param msg Destination message.
 * @return 0 on success, -1 on error.
 */
static int http1_add_header(const char *line, http_msg_t *msg) {
    const char *colon;
    char       *name, *value;

    if (msg->nhdr >= HTTP_HDR_MAX) return -1;
    colon = strchr(line, ':');
    if (!colon) return -1;

    name = (char *)malloc((size_t)(colon - line) + 1);
    if (!name) return -1;
    memcpy(name, line, (size_t)(colon - line));
    name[colon - line] = '\0';

    value = http_strdup(colon + 1);
    if (!value) { free(name); return -1; }
    value = http_trim(value);

    msg->hdrs[msg->nhdr].name  = http_strdup_lower(name);
    msg->hdrs[msg->nhdr].value = value;
    free(name);
    if (!msg->hdrs[msg->nhdr].name) { free(value); return -1; }
    msg->nhdr++;
    return 0;
}

/**
 * Find the value of the first header matching a name.
 * @param msg Source message.
 * @param name Lowercase header name.
 * @return Header value pointer or NULL if not found.
 */
static const char *http1_header_value(const http_msg_t *msg, const char *name) {
    int i;
    for (i = 0; i < msg->nhdr; i++) {
        if (strcmp(msg->hdrs[i].name, name) == 0) return msg->hdrs[i].value;
    }
    return NULL;
}

/**
 * Read a chunked body and decode it to plain bytes.
 * @param src Input source.
 * @param msg Destination message (body and trailers are populated).
 * @return 0 on success, -1 on error.
 */
static int http1_read_chunked(http_src_t *src, http_msg_t *msg) {
    unsigned char *body  = NULL;
    size_t         total = 0;

    for (;;) {
        char        *line = NULL;
        size_t       llen = 0;
        size_t       chunk_size;
        char        *end;
        char        *semi;
        unsigned char *chunk_data;
        unsigned char *nbody;
        char        *crlf = NULL;
        size_t       clen = 0;

        if (http_read_line(src, &line, &llen) <= 0) { free(body); free(line); return -1; }
        semi = strchr(line, ';');
        if (semi) *semi = '\0';
        chunk_size = (size_t)strtoul(line, &end, 16);
        free(line);

        if (chunk_size == 0) {
            for (;;) {
                char  *tline = NULL;
                size_t tlen  = 0;
                int    r     = http_read_line(src, &tline, &tlen);
                if (r <= 0) { free(tline); break; }
                if (tlen == 0) { free(tline); break; }
                if (msg->ntrailer < HTTP_HDR_MAX) {
                    const char *colon = strchr(tline, ':');
                    if (colon) {
                        char *tname = (char *)malloc((size_t)(colon - tline) + 1);
                        char *tval  = http_strdup(colon + 1);
                        if (tname && tval) {
                            memcpy(tname, tline, (size_t)(colon - tline));
                            tname[colon - tline] = '\0';
                            msg->trailers[msg->ntrailer].name  = http_strdup_lower(tname);
                            msg->trailers[msg->ntrailer].value = http_trim(tval);
                            msg->ntrailer++;
                        }
                        free(tname);
                    }
                }
                free(tline);
            }
            break;
        }

        chunk_data = (unsigned char *)malloc(chunk_size);
        if (!chunk_data) { free(body); return -1; }
        if (http_src_read(src, chunk_data, chunk_size) != chunk_size) {
            free(chunk_data); free(body); return -1;
        }

        http_read_line(src, &crlf, &clen);
        free(crlf);

        nbody = (unsigned char *)realloc(body, total + chunk_size + 1);
        if (!nbody) { free(chunk_data); free(body); return -1; }
        body = nbody;
        memcpy(body + total, chunk_data, chunk_size);
        total += chunk_size;
        free(chunk_data);
    }

    if (body) body[total] = '\0';
    msg->body     = body;
    msg->body_len = total;
    return 0;
}

/**
 * Parse one complete HTTP/1.x message from a stream.
 * @param src Input source.
 * @param msg Destination message.
 * @return 0 on success, -1 on parse error, 1 on clean EOF.
 */
static int http1_parse(http_src_t *src, http_msg_t *msg) {
    char  *line  = NULL;
    size_t llen  = 0;
    int    r;

    memset(msg, 0, sizeof(*msg));

    r = http_read_line(src, &line, &llen);
    if (r == 0) { free(line); return 1; }
    if (r < 0)  { free(line); return -1; }
    if (llen == 0) { free(line); return -1; }

    if (strncmp(line, "HTTP/", 5) == 0) {
        msg->type = HTTP_TYPE_RESPONSE;
        r = http1_parse_status_line(line, msg);
    } else {
        msg->type = HTTP_TYPE_REQUEST;
        r = http1_parse_request_line(line, msg);
    }
    free(line);
    if (r != 0) return -1;

    {
        int got_blank = 0;
        for (;;) {
            line = NULL; llen = 0;
            r = http_read_line(src, &line, &llen);
            if (r <= 0) { free(line); break; }
            if (llen == 0) { free(line); got_blank = 1; break; }
            http1_add_header(line, msg);
            free(line);
        }
        if (!got_blank) return -1;
    }

    {
        const char *cl  = http1_header_value(msg, "content-length");
        const char *te  = http1_header_value(msg, "transfer-encoding");
        int no_body = 0;

        if (msg->type == HTTP_TYPE_RESPONSE) {
            if (msg->status == 204 || msg->status == 304 ||
                (msg->status >= 100 && msg->status < 200)) no_body = 1;
        } else {
            if (!cl && (!te || strstr(te, "chunked") == NULL)) no_body = 1;
        }

        if (!no_body) {
            if (te && strstr(te, "chunked") != NULL) {
                if (http1_read_chunked(src, msg) != 0) return -1;
            } else if (cl) {
                size_t body_len;
                char  *endp;
                errno    = 0;
                body_len = (size_t)strtoul(cl, &endp, 10);
                if (errno != 0 || endp == cl) return -1;
                if (http_read_bytes(src, body_len, &msg->body) != 0) return -1;
                msg->body_len = body_len;
            }
        }
    }
    return 0;
}

/**
 * Emit the parsed message metadata and body to a buffer.
 * @param b Output buffer.
 * @param msg Source message.
 * @return 0 on success, -1 on write error.
 */
static int http1_emit_msg(http_buf_t *b, const http_msg_t *msg) {
    int i;

    if (msg->type == HTTP_TYPE_REQUEST) {
        if (http_buf_printf(b, "http.type=request\n") != 0) return -1;
        if (msg->ver_minor == 0 && msg->ver_major > 1) { if (http_buf_printf(b, "http.version=%d\n", msg->ver_major) != 0) return -1; }
        else { if (http_buf_printf(b, "http.version=%d.%d\n", msg->ver_major, msg->ver_minor) != 0) return -1; }
        if (http_buf_printf(b, "request.method=%s\n", msg->method ? msg->method : "") != 0) return -1;
        if (http_buf_printf(b, "request.target=%s\n", msg->target  ? msg->target  : "") != 0) return -1;
        if (http_buf_printf(b, "request.path=%s\n",   msg->path    ? msg->path    : "") != 0) return -1;
        if (msg->query) { if (http_buf_printf(b, "request.query=%s\n", msg->query) != 0) return -1; }
    } else {
        if (http_buf_printf(b, "http.type=response\n") != 0) return -1;
        if (msg->ver_minor == 0 && msg->ver_major > 1) { if (http_buf_printf(b, "http.version=%d\n", msg->ver_major) != 0) return -1; }
        else { if (http_buf_printf(b, "http.version=%d.%d\n", msg->ver_major, msg->ver_minor) != 0) return -1; }
        if (http_buf_printf(b, "response.status=%d\n", msg->status) != 0) return -1;
        if (http_buf_printf(b, "response.reason=%s\n", msg->reason ? msg->reason : "") != 0) return -1;
    }

    for (i = 0; i < msg->nhdr; i++) {
        if (http_buf_printf(b, "header.%s=%s\n", msg->hdrs[i].name, msg->hdrs[i].value) != 0) return -1;
    }

    for (i = 0; i < msg->ntrailer; i++) {
        if (http_buf_printf(b, "trailer.%s=%s\n", msg->trailers[i].name, msg->trailers[i].value) != 0) return -1;
    }

    if (http_buf_printf(b, "body.length=%zu\n", msg->body_len) != 0) return -1;
    if (http_buf_printf(b, "\n") != 0) return -1;

    if (msg->body_len > 0) {
        if (http_buf_write(b, msg->body, msg->body_len) != 0) return -1;
    }

    return 0;
}

/**
 * Emit context headers plus auto Content-Length or Transfer-Encoding.
 * @param b Output buffer.
 * @param ctx Build context.
 * @param body_len Body length (ignored in chunked mode).
 * @return 0 on success, -1 on write error.
 */
static int http_emit_headers(http_buf_t *b, const kc_http_t *ctx, size_t body_len) {
    int i;
    for (i = 0; i < ctx->nhdr; i++) {
        if (http_buf_printf(b, "%s\r\n", ctx->hdrs[i]) != 0) return -1;
    }
    if (ctx->chunked) {
        if (http_buf_printf(b, "Transfer-Encoding: chunked\r\n") != 0) return -1;
    } else {
        if (http_buf_printf(b, "Content-Length: %zu\r\n", body_len) != 0) return -1;
    }
    if (http_buf_printf(b, "\r\n") != 0) return -1;
    return 0;
}

/**
 * Emit a buffered body with Content-Length framing.
 * @param b Output buffer.
 * @param data Body bytes.
 * @param size Body size.
 * @return 0 on success, -1 on write error.
 */
static int http_emit_body_cl(http_buf_t *b, const unsigned char *data, size_t size) {
    if (size > 0 && http_buf_write(b, data, size) != 0) return -1;
    return 0;
}

/**
 * Emit body in chunk framing to the buffer, then emit trailers.
 * @param b Output buffer.
 * @param ctx Build context (input contains the body).
 * @return 0 on success, -1 on write error.
 */
static int http_emit_body_chunked(http_buf_t *b, const kc_http_t *ctx) {
    size_t chunk_size = ctx->chunk_size ? ctx->chunk_size : HTTP_CHUNK_DEFAULT;
    size_t offset = 0;
    size_t n;
    int    i;

    while (offset < ctx->input_len) {
        if (ctx->stop_requested) return -1;
        n = ctx->input_len - offset;
        if (n > chunk_size) n = chunk_size;
        if (http_buf_printf(b, "%zx\r\n", n) != 0) return -1;
        if (http_buf_write(b, ctx->input + offset, n) != 0) return -1;
        if (http_buf_printf(b, "\r\n") != 0) return -1;
        offset += n;
    }

    if (http_buf_printf(b, "0\r\n") != 0) return -1;
    for (i = 0; i < ctx->ntrailer; i++) {
        if (http_buf_printf(b, "%s\r\n", ctx->trailers[i]) != 0) return -1;
    }
    if (http_buf_printf(b, "\r\n") != 0) return -1;
    return 0;
}

/**
 * Build one HTTP/1.x request into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http1_build_request(const kc_http_t *ctx, http_buf_t *b) {
    const char    *method  = ctx->method  ? ctx->method  : "GET";
    const char    *target  = ctx->target  ? ctx->target  : "/";
    const char    *version = ctx->version ? ctx->version : "1.1";
    int            rc      = KC_HTTP_OK;

    if (http_buf_printf(b, "%s %s HTTP/%s\r\n", method, target, version) != 0) return KC_HTTP_ERROR;
    if (http_emit_headers(b, ctx, ctx->input_len) != 0) return KC_HTTP_ERROR;

    if (ctx->chunked) {
        if (http_emit_body_chunked(b, ctx) != 0) rc = KC_HTTP_ERROR;
    } else {
        if (http_emit_body_cl(b, ctx->input, ctx->input_len) != 0) rc = KC_HTTP_ERROR;
    }

    return rc;
}

/**
 * Build one HTTP/1.x response into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http1_build_response(const kc_http_t *ctx, http_buf_t *b) {
    const char    *version = ctx->version ? ctx->version : "1.1";
    int            status  = ctx->status  ? ctx->status  : 200;
    const char    *reason  = ctx->reason  ? ctx->reason  : http_default_reason(status);
    int            rc      = KC_HTTP_OK;

    if (http_buf_printf(b, "HTTP/%s %d %s\r\n", version, status, reason) != 0) return KC_HTTP_ERROR;
    if (http_emit_headers(b, ctx, ctx->input_len) != 0) return KC_HTTP_ERROR;

    if (ctx->chunked) {
        if (http_emit_body_chunked(b, ctx) != 0) rc = KC_HTTP_ERROR;
    } else {
        if (http_emit_body_cl(b, ctx->input, ctx->input_len) != 0) rc = KC_HTTP_ERROR;
    }

    return rc;
}

#define HTTP2_FRAME_DATA 0
#define HTTP2_FRAME_HEADERS 1
#define HTTP2_FRAME_RST_STREAM 3
#define HTTP2_FRAME_SETTINGS 4
#define HTTP2_FRAME_GOAWAY 7
#define HTTP2_FRAME_WINDOW_UPDATE 8
#define HTTP2_FRAME_CONTINUATION 9
#define HTTP2_FLAG_END_STREAM 0x01
#define HTTP2_FLAG_END_HEADERS 0x04
#define HTTP2_FLAG_PADDED 0x08
#define HTTP2_FLAG_PRIORITY 0x20
#define HTTP2_STREAM_DEFAULT 1U
#define HTTP2_DYN_DEFAULT 4096U

typedef struct {
    const char *name;
    const char *value;
} http2_static_t;

typedef struct {
    char *name;
    char *value;
    size_t size;
} http2_dyn_ent_t;

typedef struct {
    http2_dyn_ent_t *ents;
    size_t n;
    size_t cap;
    size_t used;
    size_t max;
} http2_dyn_t;

typedef struct {
    unsigned int len;
    unsigned char type;
    unsigned char flags;
    unsigned int stream_id;
} http2_frame_t;

typedef struct {
    unsigned int code;
    int len;
} http2_huff_t;

static const http2_static_t http2_static[] = {
    { NULL, NULL },
    { ":authority", "" },
    { ":method", "GET" },
    { ":method", "POST" },
    { ":path", "/" },
    { ":path", "/index.html" },
    { ":scheme", "http" },
    { ":scheme", "https" },
    { ":status", "200" },
    { ":status", "204" },
    { ":status", "206" },
    { ":status", "304" },
    { ":status", "400" },
    { ":status", "404" },
    { ":status", "500" },
    { "accept-charset", "" },
    { "accept-encoding", "gzip,deflate" },
    { "accept-language", "" },
    { "accept-ranges", "" },
    { "accept", "" },
    { "access-control-allow-origin", "" },
    { "age", "" },
    { "allow", "" },
    { "authorization", "" },
    { "cache-control", "" },
    { "content-disposition", "" },
    { "content-encoding", "" },
    { "content-language", "" },
    { "content-length", "" },
    { "content-location", "" },
    { "content-range", "" },
    { "content-type", "" },
    { "cookie", "" },
    { "date", "" },
    { "etag", "" },
    { "expect", "" },
    { "expires", "" },
    { "from", "" },
    { "host", "" },
    { "if-match", "" },
    { "if-modified-since", "" },
    { "if-none-match", "" },
    { "if-range", "" },
    { "if-unmodified-since", "" },
    { "last-modified", "" },
    { "link", "" },
    { "location", "" },
    { "max-forwards", "" },
    { "proxy-authenticate", "" },
    { "proxy-authorization", "" },
    { "range", "" },
    { "referer", "" },
    { "refresh", "" },
    { "retry-after", "" },
    { "server", "" },
    { "set-cookie", "" },
    { "strict-transport-security", "" },
    { "transfer-encoding", "" },
    { "user-agent", "" },
    { "vary", "" },
    { "via", "" },
    { "www-authenticate", "" }
};

static const http2_huff_t http2_huff[] = {
    { 0x1ff8U, 13 },
    { 0x7fffd8U, 23 },
    { 0xfffffe2U, 28 },
    { 0xfffffe3U, 28 },
    { 0xfffffe4U, 28 },
    { 0xfffffe5U, 28 },
    { 0xfffffe6U, 28 },
    { 0xfffffe7U, 28 },
    { 0xfffffe8U, 28 },
    { 0xffffeaU, 24 },
    { 0x3ffffffcU, 30 },
    { 0xfffffe9U, 28 },
    { 0xfffffeaU, 28 },
    { 0x3ffffffdU, 30 },
    { 0xfffffebU, 28 },
    { 0xfffffecU, 28 },
    { 0xfffffedU, 28 },
    { 0xfffffeeU, 28 },
    { 0xfffffefU, 28 },
    { 0xffffff0U, 28 },
    { 0xffffff1U, 28 },
    { 0xffffff2U, 28 },
    { 0x3ffffffeU, 30 },
    { 0xffffff3U, 28 },
    { 0xffffff4U, 28 },
    { 0xffffff5U, 28 },
    { 0xffffff6U, 28 },
    { 0xffffff7U, 28 },
    { 0xffffff8U, 28 },
    { 0xffffff9U, 28 },
    { 0xffffffaU, 28 },
    { 0xffffffbU, 28 },
    { 0x14U, 6 },
    { 0x3f8U, 10 },
    { 0x3f9U, 10 },
    { 0xffaU, 12 },
    { 0x1ff9U, 13 },
    { 0x15U, 6 },
    { 0xf8U, 8 },
    { 0x7faU, 11 },
    { 0x3faU, 10 },
    { 0x3fbU, 10 },
    { 0xf9U, 8 },
    { 0x7fbU, 11 },
    { 0xfaU, 8 },
    { 0x16U, 6 },
    { 0x17U, 6 },
    { 0x18U, 6 },
    { 0x0U, 5 },
    { 0x1U, 5 },
    { 0x2U, 5 },
    { 0x19U, 6 },
    { 0x1aU, 6 },
    { 0x1bU, 6 },
    { 0x1cU, 6 },
    { 0x1dU, 6 },
    { 0x1eU, 6 },
    { 0x1fU, 6 },
    { 0x5cU, 7 },
    { 0xfbU, 8 },
    { 0x7ffcU, 15 },
    { 0x20U, 6 },
    { 0xffbU, 12 },
    { 0x3fcU, 10 },
    { 0x1ffaU, 13 },
    { 0x21U, 6 },
    { 0x5dU, 7 },
    { 0x5eU, 7 },
    { 0x5fU, 7 },
    { 0x60U, 7 },
    { 0x61U, 7 },
    { 0x62U, 7 },
    { 0x63U, 7 },
    { 0x64U, 7 },
    { 0x65U, 7 },
    { 0x66U, 7 },
    { 0x67U, 7 },
    { 0x68U, 7 },
    { 0x69U, 7 },
    { 0x6aU, 7 },
    { 0x6bU, 7 },
    { 0x6cU, 7 },
    { 0x6dU, 7 },
    { 0x6eU, 7 },
    { 0x6fU, 7 },
    { 0x70U, 7 },
    { 0x71U, 7 },
    { 0x72U, 7 },
    { 0xfcU, 8 },
    { 0x73U, 7 },
    { 0xfdU, 8 },
    { 0x1ffbU, 13 },
    { 0x7fff0U, 19 },
    { 0x1ffcU, 13 },
    { 0x3ffcU, 14 },
    { 0x22U, 6 },
    { 0x7ffdU, 15 },
    { 0x3U, 5 },
    { 0x23U, 6 },
    { 0x4U, 5 },
    { 0x24U, 6 },
    { 0x5U, 5 },
    { 0x25U, 6 },
    { 0x26U, 6 },
    { 0x27U, 6 },
    { 0x6U, 5 },
    { 0x74U, 7 },
    { 0x75U, 7 },
    { 0x28U, 6 },
    { 0x29U, 6 },
    { 0x2aU, 6 },
    { 0x7U, 5 },
    { 0x2bU, 6 },
    { 0x76U, 7 },
    { 0x2cU, 6 },
    { 0x8U, 5 },
    { 0x9U, 5 },
    { 0x2dU, 6 },
    { 0x77U, 7 },
    { 0x78U, 7 },
    { 0x79U, 7 },
    { 0x7aU, 7 },
    { 0x7bU, 7 },
    { 0x7ffeU, 15 },
    { 0x7fcU, 11 },
    { 0x3ffdU, 14 },
    { 0x1ffdU, 13 },
    { 0xffffffcU, 28 },
    { 0xfffe6U, 20 },
    { 0x3fffd2U, 22 },
    { 0xfffe7U, 20 },
    { 0xfffe8U, 20 },
    { 0x3fffd3U, 22 },
    { 0x3fffd4U, 22 },
    { 0x3fffd5U, 22 },
    { 0x7fffd9U, 23 },
    { 0x3fffd6U, 22 },
    { 0x7fffdaU, 23 },
    { 0x7fffdbU, 23 },
    { 0x7fffdcU, 23 },
    { 0x7fffddU, 23 },
    { 0x7fffdeU, 23 },
    { 0xffffebU, 24 },
    { 0x7fffdfU, 23 },
    { 0xffffecU, 24 },
    { 0xffffedU, 24 },
    { 0x3fffd7U, 22 },
    { 0x7fffe0U, 23 },
    { 0xffffeeU, 24 },
    { 0x7fffe1U, 23 },
    { 0x7fffe2U, 23 },
    { 0x7fffe3U, 23 },
    { 0x7fffe4U, 23 },
    { 0x1fffdcU, 21 },
    { 0x3fffd8U, 22 },
    { 0x7fffe5U, 23 },
    { 0x3fffd9U, 22 },
    { 0x7fffe6U, 23 },
    { 0x7fffe7U, 23 },
    { 0xffffefU, 24 },
    { 0x3fffdaU, 22 },
    { 0x1fffddU, 21 },
    { 0xfffe9U, 20 },
    { 0x3fffdbU, 22 },
    { 0x3fffdcU, 22 },
    { 0x7fffe8U, 23 },
    { 0x7fffe9U, 23 },
    { 0x1fffdeU, 21 },
    { 0x7fffeaU, 23 },
    { 0x3fffddU, 22 },
    { 0x3fffdeU, 22 },
    { 0xfffff0U, 24 },
    { 0x1fffdfU, 21 },
    { 0x3fffdfU, 22 },
    { 0x7fffebU, 23 },
    { 0x7fffecU, 23 },
    { 0x1fffe0U, 21 },
    { 0x1fffe1U, 21 },
    { 0x3fffe0U, 22 },
    { 0x1fffe2U, 21 },
    { 0x7fffedU, 23 },
    { 0x3fffe1U, 22 },
    { 0x7fffeeU, 23 },
    { 0x7fffefU, 23 },
    { 0xfffeaU, 20 },
    { 0x3fffe2U, 22 },
    { 0x3fffe3U, 22 },
    { 0x3fffe4U, 22 },
    { 0x7ffff0U, 23 },
    { 0x3fffe5U, 22 },
    { 0x3fffe6U, 22 },
    { 0x7ffff1U, 23 },
    { 0x3ffffe0U, 26 },
    { 0x3ffffe1U, 26 },
    { 0xfffebU, 20 },
    { 0x7fff1U, 19 },
    { 0x3fffe7U, 22 },
    { 0x7ffff2U, 23 },
    { 0x3fffe8U, 22 },
    { 0x1ffffecU, 25 },
    { 0x3ffffe2U, 26 },
    { 0x3ffffe3U, 26 },
    { 0x3ffffe4U, 26 },
    { 0x7ffffdeU, 27 },
    { 0x7ffffdfU, 27 },
    { 0x3ffffe5U, 26 },
    { 0xfffff1U, 24 },
    { 0x1ffffedU, 25 },
    { 0x7fff2U, 19 },
    { 0x1fffe3U, 21 },
    { 0x3ffffe6U, 26 },
    { 0x7ffffe0U, 27 },
    { 0x7ffffe1U, 27 },
    { 0x3ffffe7U, 26 },
    { 0x7ffffe2U, 27 },
    { 0xfffff2U, 24 },
    { 0x1fffe4U, 21 },
    { 0x1fffe5U, 21 },
    { 0x3ffffe8U, 26 },
    { 0x3ffffe9U, 26 },
    { 0xffffffdU, 28 },
    { 0x7ffffe3U, 27 },
    { 0x7ffffe4U, 27 },
    { 0x7ffffe5U, 27 },
    { 0xfffecU, 20 },
    { 0xfffff3U, 24 },
    { 0xfffedU, 20 },
    { 0x1fffe6U, 21 },
    { 0x3fffe9U, 22 },
    { 0x1fffe7U, 21 },
    { 0x1fffe8U, 21 },
    { 0x7ffff3U, 23 },
    { 0x3fffeaU, 22 },
    { 0x3fffebU, 22 },
    { 0x1ffffeeU, 25 },
    { 0x1ffffefU, 25 },
    { 0xfffff4U, 24 },
    { 0xfffff5U, 24 },
    { 0x3ffffeaU, 26 },
    { 0x7ffff4U, 23 },
    { 0x3ffffebU, 26 },
    { 0x7ffffe6U, 27 },
    { 0x3ffffecU, 26 },
    { 0x3ffffedU, 26 },
    { 0x7ffffe7U, 27 },
    { 0x7ffffe8U, 27 },
    { 0x7ffffe9U, 27 },
    { 0x7ffffeaU, 27 },
    { 0x7ffffebU, 27 },
    { 0xffffffeU, 28 },
    { 0x7ffffecU, 27 },
    { 0x7ffffedU, 27 },
    { 0x7ffffeeU, 27 },
    { 0x7ffffefU, 27 },
    { 0x7fffff0U, 27 },
    { 0x3ffffeeU, 26 },
    { 0x3fffffffU, 30 },
};

/**
 * Read an exact byte count from a stream.
 * @param f Input stream.
 * @param out Destination buffer.
 * @param n Byte count.
 * @return 0 on success, -1 on short read or error.
 */
static int http2_read_exact(http_src_t *src, unsigned char *out, size_t n) {
    return http_src_read(src, out, n) == n ? 0 : -1;
}

/**
 * Append bytes to a heap buffer.
 * @param data Buffer pointer.
 * @param len Current length pointer.
 * @param add Bytes to append.
 * @param add_len Added length.
 * @return 0 on success, -1 on allocation failure.
 */
static int http2_append(unsigned char **data, size_t *len, const unsigned char *add, size_t add_len) {
    unsigned char *tmp;

    if (add_len == 0) return 0;
    tmp = (unsigned char *)realloc(*data, *len + add_len + 1);
    if (!tmp) return -1;
    *data = tmp;
    memcpy(*data + *len, add, add_len);
    *len += add_len;
    (*data)[*len] = '\0';
    return 0;
}

/**
 * Read an HTTP/2 frame header.
 * @param f Input stream.
 * @param frame Destination frame metadata.
 * @return 0 on success, 1 on clean EOF, -1 on error.
 */
static int http2_read_frame(http_src_t *src, http2_frame_t *frame) {
    unsigned char h[9];
    size_t got;

    got = http_src_read(src, h, sizeof(h));
    if (got == 0) return 1;
    if (got != sizeof(h)) return -1;
    frame->len = ((unsigned int)h[0] << 16) | ((unsigned int)h[1] << 8) | h[2];
    frame->type = h[3];
    frame->flags = h[4];
    frame->stream_id = ((unsigned int)(h[5] & 0x7f) << 24) |
        ((unsigned int)h[6] << 16) | ((unsigned int)h[7] << 8) | h[8];
    return 0;
}

/**
 * Decode an HPACK integer from a header block.
 * @param p Input cursor pointer.
 * @param end End pointer.
 * @param prefix Prefix bit count.
 * @param out Decoded integer.
 * @return 0 on success, -1 on malformed input.
 */
static int hpack_int(const unsigned char **p, const unsigned char *end, int prefix, unsigned int *out) {
    unsigned int mask;
    unsigned int value;
    unsigned int shift;

    if (*p >= end) return -1;
    mask = (1U << prefix) - 1U;
    value = **p & mask;
    (*p)++;
    if (value < mask) {
        *out = value;
        return 0;
    }
    shift = 0;
    for (;;) {
        unsigned char b;
        if (*p >= end || shift > 28) return -1;
        b = **p;
        (*p)++;
        value += (unsigned int)(b & 0x7fU) << shift;
        if ((b & 0x80U) == 0) break;
        shift += 7;
    }
    *out = value;
    return 0;
}

/**
 * Encode an HPACK integer to a byte buffer.
 * @param out Output bytes.
 * @param len Output length pointer.
 * @param first First byte prefix flags.
 * @param prefix Prefix bit count.
 * @param value Integer value.
 * @return None.
 */
static void hpack_emit_int(unsigned char *out, size_t *len, unsigned char first, int prefix, unsigned int value) {
    unsigned int max = (1U << prefix) - 1U;

    if (value < max) {
        out[(*len)++] = (unsigned char)(first | value);
        return;
    }
    out[(*len)++] = (unsigned char)(first | max);
    value -= max;
    while (value >= 128U) {
        out[(*len)++] = (unsigned char)((value & 0x7fU) | 0x80U);
        value >>= 7;
    }
    out[(*len)++] = (unsigned char)value;
}

/**
 * Decode an HPACK Huffman byte string.
 * @param data Encoded bytes.
 * @param len Encoded byte count.
 * @param out Output string.
 * @return 0 on success, -1 on malformed input.
 */
static int hpack_huffman(const unsigned char *data, size_t len, char **out) {
    unsigned int code = 0;
    int          bits = 0;
    size_t       cap = len * 8U + 1U;
    size_t       used = 0;
    size_t       i;

    *out = (char *)malloc(cap);
    if (!*out) return -1;
    for (i = 0; i < len; i++) {
        int bit;
        for (bit = 7; bit >= 0; bit--) {
            int sym;
            code = (code << 1) | ((data[i] >> bit) & 1U);
            bits++;
            for (sym = 0; sym < 257; sym++) {
                if (http2_huff[sym].len == bits && http2_huff[sym].code == code) {
                    if (sym == 256) { free(*out); *out = NULL; return -1; }
                    (*out)[used++] = (char)sym;
                    code = 0;
                    bits = 0;
                    break;
                }
            }
        }
    }
    if (bits > 7 || (bits > 0 && code != ((1U << bits) - 1U))) {
        free(*out);
        *out = NULL;
        return -1;
    }
    (*out)[used] = '\0';
    return 0;
}

/**
 * Decode an HPACK string.
 * @param p Input cursor pointer.
 * @param end End pointer.
 * @param out Output string.
 * @return 0 on success, -1 on malformed input.
 */
static int hpack_string(const unsigned char **p, const unsigned char *end, char **out) {
    unsigned int len;
    int huff;

    if (*p >= end) return -1;
    huff = (**p & 0x80U) != 0;
    if (hpack_int(p, end, 7, &len) != 0) return -1;
    if ((size_t)(end - *p) < len) return -1;
    if (huff) {
        if (hpack_huffman(*p, len, out) != 0) return -1;
        *p += len;
        return 0;
    }
    *out = (char *)malloc((size_t)len + 1U);
    if (!*out) return -1;
    memcpy(*out, *p, len);
    (*out)[len] = '\0';
    *p += len;
    return 0;
}

/**
 * Initialize an HPACK dynamic table.
 * @param dyn Dynamic table.
 * @return None.
 */
static void hpack_dyn_init(http2_dyn_t *dyn) {
    memset(dyn, 0, sizeof(*dyn));
    dyn->max = HTTP2_DYN_DEFAULT;
}

/**
 * Release an HPACK dynamic table.
 * @param dyn Dynamic table.
 * @return None.
 */
static void hpack_dyn_free(http2_dyn_t *dyn) {
    size_t i;

    for (i = 0; i < dyn->n; i++) {
        free(dyn->ents[i].name);
        free(dyn->ents[i].value);
    }
    free(dyn->ents);
    memset(dyn, 0, sizeof(*dyn));
}

/**
 * Evict dynamic HPACK entries until the table fits.
 * @param dyn Dynamic table.
 * @return None.
 */
static void hpack_dyn_evict(http2_dyn_t *dyn) {
    while (dyn->n > 0 && dyn->used > dyn->max) {
        size_t last = dyn->n - 1;
        dyn->used -= dyn->ents[last].size;
        free(dyn->ents[last].name);
        free(dyn->ents[last].value);
        dyn->n--;
    }
}

/**
 * Insert one dynamic HPACK entry.
 * @param dyn Dynamic table.
 * @param name Header name.
 * @param value Header value.
 * @return 0 on success, -1 on allocation failure.
 */
static int hpack_dyn_add(http2_dyn_t *dyn, const char *name, const char *value) {
    http2_dyn_ent_t ent;
    http2_dyn_ent_t *tmp;

    ent.name = http_strdup(name);
    ent.value = http_strdup(value);
    if (!ent.name || !ent.value) {
        free(ent.name);
        free(ent.value);
        return -1;
    }
    ent.size = strlen(name) + strlen(value) + 32U;
    if (ent.size > dyn->max) {
        free(ent.name);
        free(ent.value);
        dyn->used = 0;
        dyn->n = 0;
        return 0;
    }
    if (dyn->n == dyn->cap) {
        size_t cap = dyn->cap ? dyn->cap * 2U : 16U;
        tmp = (http2_dyn_ent_t *)realloc(dyn->ents, cap * sizeof(*dyn->ents));
        if (!tmp) {
            free(ent.name);
            free(ent.value);
            return -1;
        }
        dyn->ents = tmp;
        dyn->cap = cap;
    }
    memmove(dyn->ents + 1, dyn->ents, dyn->n * sizeof(*dyn->ents));
    dyn->ents[0] = ent;
    dyn->n++;
    dyn->used += ent.size;
    hpack_dyn_evict(dyn);
    return 0;
}

/**
 * Resolve an HPACK index to a name and value.
 * @param dyn Dynamic table.
 * @param index HPACK index.
 * @param name Output name pointer.
 * @param value Output value pointer.
 * @return 0 on success, -1 on unknown index.
 */
static int hpack_lookup(http2_dyn_t *dyn, unsigned int index, const char **name, const char **value) {
    unsigned int static_n = (unsigned int)(sizeof(http2_static) / sizeof(http2_static[0])) - 1U;

    if (index > 0 && index <= static_n) {
        *name = http2_static[index].name;
        *value = http2_static[index].value;
        return 0;
    }
    index -= static_n + 1U;
    if (index < dyn->n) {
        *name = dyn->ents[index].name;
        *value = dyn->ents[index].value;
        return 0;
    }
    return -1;
}

/**
 * Add one decoded header to a message.
 * @param msg Destination message.
 * @param name Header name.
 * @param value Header value.
 * @return 0 on success, -1 on allocation failure.
 */
static int http2_msg_add(http_msg_t *msg, const char *name, const char *value) {
    char *target;
    char *qmark;

    if (strcmp(name, ":method") == 0) {
        msg->type = HTTP_TYPE_REQUEST;
        msg->method = http_strdup(value);
        return msg->method ? 0 : -1;
    }
    if (strcmp(name, ":path") == 0) {
        msg->target = http_strdup(value);
        if (!msg->target) return -1;
        target = msg->target;
        qmark = strchr(target, '?');
        if (qmark) {
            msg->path = (char *)malloc((size_t)(qmark - target) + 1U);
            if (!msg->path) return -1;
            memcpy(msg->path, target, (size_t)(qmark - target));
            msg->path[qmark - target] = '\0';
            msg->query = http_strdup(qmark + 1);
            return msg->query ? 0 : -1;
        }
        msg->path = http_strdup(target);
        return msg->path ? 0 : -1;
    }
    if (strcmp(name, ":status") == 0) {
        msg->type = HTTP_TYPE_RESPONSE;
        msg->status = atoi(value);
        msg->reason = http_strdup(http_default_reason(msg->status));
        return msg->reason ? 0 : -1;
    }
    if (strcmp(name, ":scheme") == 0) return 0;
    if (strcmp(name, ":authority") == 0) name = "host";
    if (msg->nhdr >= HTTP_HDR_MAX) return -1;
    msg->hdrs[msg->nhdr].name = http_strdup_lower(name);
    msg->hdrs[msg->nhdr].value = http_strdup(value);
    if (!msg->hdrs[msg->nhdr].name || !msg->hdrs[msg->nhdr].value) return -1;
    msg->nhdr++;
    return 0;
}

/**
 * Decode a complete HPACK header block.
 * @param msg Destination message.
 * @param dyn Dynamic HPACK table.
 * @param block Header block bytes.
 * @param len Header block length.
 * @return 0 on success, -1 on malformed input.
 */
static int hpack_decode(http_msg_t *msg, http2_dyn_t *dyn, const unsigned char *block, size_t len) {
    const unsigned char *p = block;
    const unsigned char *end = block + len;

    while (p < end) {
        unsigned char b = *p;
        unsigned int index;
        const char *name;
        const char *value;
        char *own_name = NULL;
        char *own_value = NULL;
        int indexed = 0;

        if ((b & 0x80U) != 0) {
            if (hpack_int(&p, end, 7, &index) != 0) return -1;
            if (hpack_lookup(dyn, index, &name, &value) != 0) return -1;
            if (http2_msg_add(msg, name, value) != 0) return -1;
            continue;
        }
        if ((b & 0x40U) != 0) {
            indexed = 1;
            if (hpack_int(&p, end, 6, &index) != 0) return -1;
        } else if ((b & 0xf0U) == 0x10U) {
            if (hpack_int(&p, end, 4, &index) != 0) return -1;
        } else if ((b & 0xf0U) == 0x00U) {
            if (hpack_int(&p, end, 4, &index) != 0) return -1;
        } else if ((b & 0xe0U) == 0x20U) {
            if (hpack_int(&p, end, 5, &index) != 0) return -1;
            dyn->max = index;
            hpack_dyn_evict(dyn);
            continue;
        } else {
            return -1;
        }
        if (index == 0) {
            if (hpack_string(&p, end, &own_name) != 0) return -1;
            name = own_name;
        } else if (hpack_lookup(dyn, index, &name, &value) != 0) {
            free(own_name);
            return -1;
        }
        if (hpack_string(&p, end, &own_value) != 0) {
            free(own_name);
            return -1;
        }
        if (http2_msg_add(msg, name, own_value) != 0) {
            free(own_name);
            free(own_value);
            return -1;
        }
        if (indexed && hpack_dyn_add(dyn, name, own_value) != 0) {
            free(own_name);
            free(own_value);
            return -1;
        }
        free(own_name);
        free(own_value);
    }
    return 0;
}

/**
 * Decode a HEADERS payload fragment.
 * @param frame Frame metadata.
 * @param payload Frame payload.
 * @param payload_len Payload length.
 * @param block Header block buffer.
 * @param block_len Header block length pointer.
 * @return 0 on success, -1 on malformed input.
 */
static int http2_headers_fragment(http2_frame_t *frame, unsigned char *payload, size_t payload_len, unsigned char **block, size_t *block_len) {
    size_t off = 0;
    size_t pad = 0;

    if ((frame->flags & HTTP2_FLAG_PADDED) != 0) {
        if (payload_len == 0) return -1;
        pad = payload[0];
        off = 1;
    }
    if ((frame->flags & HTTP2_FLAG_PRIORITY) != 0) {
        if (payload_len < off + 5U) return -1;
        off += 5U;
    }
    if (pad > payload_len - off) return -1;
    return http2_append(block, block_len, payload + off, payload_len - off - pad);
}

/**
 * Parse one HTTP/2 message from a stream after the client preface.
 * @param f Input stream.
 * @param msg Destination message.
 * @return 0 on success, -1 on parse error, 1 on clean EOF.
 */
static int http2_parse(http_src_t *src, http_msg_t *msg) {
    http2_dyn_t dyn;
    unsigned char *block = NULL;
    size_t block_len = 0;
    unsigned int stream_id = 0;
    int saw_headers = 0;
    int in_headers = 0;
    int done = 0;

    memset(msg, 0, sizeof(*msg));
    msg->ver_major = 2;
    msg->ver_minor = 0;
    hpack_dyn_init(&dyn);
    while (!done) {
        http2_frame_t frame;
        unsigned char *payload = NULL;
        int r = http2_read_frame(src, &frame);
        if (r == 1) break;
        if (r != 0) { hpack_dyn_free(&dyn); free(block); return -1; }
        if (frame.len > 0) {
            payload = (unsigned char *)malloc(frame.len);
            if (!payload) { hpack_dyn_free(&dyn); free(block); return -1; }
            if (http2_read_exact(src, payload, frame.len) != 0) {
                free(payload);
                hpack_dyn_free(&dyn);
                free(block);
                return -1;
            }
        }
        if (frame.type == HTTP2_FRAME_SETTINGS) {
            if (frame.stream_id != 0 || frame.len % 6U != 0) r = -1;
        } else if (frame.type == HTTP2_FRAME_HEADERS) {
            if (frame.stream_id == 0) r = -1;
            else if (in_headers) r = -1;
            else if (!saw_headers) {
                stream_id = frame.stream_id;
                saw_headers = 1;
                in_headers = (frame.flags & HTTP2_FLAG_END_HEADERS) == 0;
                r = http2_headers_fragment(&frame, payload, frame.len, &block, &block_len);
                if (r == 0 && !in_headers) {
                    r = hpack_decode(msg, &dyn, block, block_len);
                    free(block);
                    block = NULL;
                    block_len = 0;
                }
                if ((frame.flags & HTTP2_FLAG_END_STREAM) != 0) done = 1;
            } else if (frame.stream_id == stream_id) {
                r = http2_headers_fragment(&frame, payload, frame.len, &block, &block_len);
                if (r == 0 && (frame.flags & HTTP2_FLAG_END_HEADERS) != 0) {
                    free(block);
                    block = NULL;
                    block_len = 0;
                }
            }
        } else if (frame.type == HTTP2_FRAME_CONTINUATION) {
            if (!in_headers || frame.stream_id != stream_id) r = -1;
            if (r == 0) r = http2_append(&block, &block_len, payload, frame.len);
            if (r == 0 && (frame.flags & HTTP2_FLAG_END_HEADERS) != 0) {
                r = hpack_decode(msg, &dyn, block, block_len);
                free(block);
                block = NULL;
                block_len = 0;
                in_headers = 0;
            }
        } else if (frame.type == HTTP2_FRAME_DATA && frame.stream_id == stream_id) {
            size_t off = 0;
            size_t pad = 0;
            if ((frame.flags & HTTP2_FLAG_PADDED) != 0) {
                if (frame.len == 0) r = -1;
                else { pad = payload[0]; off = 1; }
            }
            if (r == 0 && pad <= frame.len - off) r = http2_append(&msg->body, &msg->body_len, payload + off, frame.len - off - pad);
            else r = -1;
            if ((frame.flags & HTTP2_FLAG_END_STREAM) != 0) done = 1;
        } else if (frame.type == HTTP2_FRAME_RST_STREAM) {
            if (frame.stream_id == 0 || frame.len != 4U) r = -1;
            else if (frame.stream_id == stream_id) r = -1;
        } else if (frame.type == HTTP2_FRAME_WINDOW_UPDATE) {
            if (frame.len != 4U) r = -1;
        } else if (frame.type == HTTP2_FRAME_GOAWAY) {
            if (frame.stream_id != 0 || frame.len < 8U) r = -1;
            else if (saw_headers && msg->type != 0) done = 1;
        }
        free(payload);
        if (r != 0) { hpack_dyn_free(&dyn); free(block); return -1; }
    }
    hpack_dyn_free(&dyn);
    free(block);
    if (!saw_headers || msg->type == 0) return 1;
    if (msg->type == HTTP_TYPE_REQUEST && !msg->path) msg->path = http_strdup(msg->target ? msg->target : "");
    return 0;
}

/**
 * Emit an HTTP/2 frame to a buffer.
 * @param type Frame type.
 * @param flags Frame flags.
 * @param stream_id Stream id.
 * @param payload Frame payload.
 * @param len Payload length.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on write failure.
 */
static int http2_write_frame(http_buf_t *b, unsigned char type, unsigned char flags, unsigned int stream_id, const unsigned char *payload, size_t len) {
    unsigned char h[9];

    if (len > 0xffffffU) return KC_HTTP_ERROR;
    h[0] = (unsigned char)((len >> 16) & 0xffU);
    h[1] = (unsigned char)((len >> 8) & 0xffU);
    h[2] = (unsigned char)(len & 0xffU);
    h[3] = type;
    h[4] = flags;
    h[5] = (unsigned char)((stream_id >> 24) & 0x7fU);
    h[6] = (unsigned char)((stream_id >> 16) & 0xffU);
    h[7] = (unsigned char)((stream_id >> 8) & 0xffU);
    h[8] = (unsigned char)(stream_id & 0xffU);
    if (http_buf_write(b, h, sizeof(h)) != 0) return KC_HTTP_ERROR;
    if (len > 0 && http_buf_write(b, payload, len) != 0) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Emit one raw HPACK string.
 * @param out Output buffer.
 * @param len Output length pointer.
 * @param s String value.
 * @return None.
 */
static void hpack_emit_string(unsigned char *out, size_t *len, const char *s) {
    size_t n = strlen(s);

    hpack_emit_int(out, len, 0, 7, (unsigned int)n);
    memcpy(out + *len, s, n);
    *len += n;
}

/**
 * Emit one HPACK literal header using an indexed name.
 * @param out Output buffer.
 * @param len Output length pointer.
 * @param name_index HPACK static name index.
 * @param value Header value.
 * @return None.
 */
static void hpack_emit_lit_indexed(unsigned char *out, size_t *len, unsigned int name_index, const char *value) {
    hpack_emit_int(out, len, 0x40U, 6, name_index);
    hpack_emit_string(out, len, value);
}

/**
 * Emit one HPACK literal header with a raw name.
 * @param out Output buffer.
 * @param len Output length pointer.
 * @param name Header name.
 * @param value Header value.
 * @return None.
 */
static void hpack_emit_lit_raw(unsigned char *out, size_t *len, const char *name, const char *value) {
    hpack_emit_int(out, len, 0x40U, 6, 0);
    hpack_emit_string(out, len, name);
    hpack_emit_string(out, len, value);
}

/**
 * Split one configured header into name and value.
 * @param header Header text.
 * @param name Output name.
 * @param value Output value.
 * @return 0 on success, -1 on malformed input.
 */
static int http2_split_header(const char *header, char **name, char **value) {
    const char *colon = strchr(header, ':');

    if (!colon) return -1;
    *name = (char *)malloc((size_t)(colon - header) + 1U);
    if (!*name) return -1;
    memcpy(*name, header, (size_t)(colon - header));
    (*name)[colon - header] = '\0';
    *value = http_strdup(colon + 1);
    if (!*value) { free(*name); return -1; }
    http_trim(*value);
    return 0;
}

/**
 * Return a static table index for an exact name and value.
 * @param name Header name.
 * @param value Header value.
 * @return HPACK index, or 0 if not found.
 */
static unsigned int hpack_exact_index(const char *name, const char *value) {
    unsigned int i;

    for (i = 1; i < sizeof(http2_static) / sizeof(http2_static[0]); i++) {
        if (strcmp(http2_static[i].name, name) == 0 && strcmp(http2_static[i].value, value) == 0) return i;
    }
    return 0;
}

/**
 * Return a static table index for a name.
 * @param name Header name.
 * @return HPACK name index, or 0 if not found.
 */
static unsigned int hpack_name_index(const char *name) {
    unsigned int i;

    for (i = 1; i < sizeof(http2_static) / sizeof(http2_static[0]); i++) {
        if (strcmp(http2_static[i].name, name) == 0) return i;
    }
    return 0;
}

/**
 * Emit one regular configured header into an HPACK block.
 * @param out Output block.
 * @param len Output length pointer.
 * @param header Configured header string.
 * @return 0 on success, -1 on malformed input.
 */
static int hpack_emit_config_header(unsigned char *out, size_t *len, const char *header) {
    char *name;
    char *lower;
    char *value;
    unsigned int index;

    if (http2_split_header(header, &name, &value) != 0) return -1;
    lower = http_strdup_lower(name);
    free(name);
    if (!lower) { free(value); return -1; }
    if (strcmp(lower, "host") == 0) {
        hpack_emit_lit_indexed(out, len, 1, value);
    } else {
        index = hpack_name_index(lower);
        if (index != 0) hpack_emit_lit_indexed(out, len, index, value);
        else hpack_emit_lit_raw(out, len, lower, value);
    }
    free(lower);
    free(value);
    return 0;
}

/**
 * Build one HTTP/2 request into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http2_build_request(const kc_http_t *ctx, http_buf_t *b) {
    static const unsigned char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    const char *method = ctx->method ? ctx->method : "GET";
    const char *target = ctx->target ? ctx->target : "/";
    unsigned char block[65536];
    size_t block_len = 0;
    int i;

    if (http_buf_write(b, preface, sizeof(preface) - 1U) != 0) return KC_HTTP_ERROR;
    if (http2_write_frame(b, HTTP2_FRAME_SETTINGS, 0, 0, NULL, 0) != KC_HTTP_OK) return KC_HTTP_ERROR;
    i = (int)hpack_exact_index(":method", method);
    if (i) block[block_len++] = (unsigned char)(0x80U | i);
    else hpack_emit_lit_indexed(block, &block_len, 2, method);
    i = (int)hpack_exact_index(":path", target);
    if (i) block[block_len++] = (unsigned char)(0x80U | i);
    else hpack_emit_lit_indexed(block, &block_len, 4, target);
    block[block_len++] = 0x87U;
    for (i = 0; i < ctx->nhdr; i++) {
        if (hpack_emit_config_header(block, &block_len, ctx->hdrs[i]) != 0) return KC_HTTP_ERROR;
    }
    if (http2_write_frame(b, HTTP2_FRAME_HEADERS, HTTP2_FLAG_END_HEADERS | (ctx->input_len == 0 ? HTTP2_FLAG_END_STREAM : 0), HTTP2_STREAM_DEFAULT, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (ctx->input_len > 0 && http2_write_frame(b, HTTP2_FRAME_DATA, HTTP2_FLAG_END_STREAM, HTTP2_STREAM_DEFAULT, ctx->input, ctx->input_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP/2 response into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http2_build_response(const kc_http_t *ctx, http_buf_t *b) {
    static const unsigned char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    unsigned char block[65536];
    char status_buf[16];
    size_t block_len = 0;
    unsigned int index;
    int i;

    if (http_buf_write(b, preface, sizeof(preface) - 1U) != 0) return KC_HTTP_ERROR;
    if (http2_write_frame(b, HTTP2_FRAME_SETTINGS, 0, 0, NULL, 0) != KC_HTTP_OK) return KC_HTTP_ERROR;
    snprintf(status_buf, sizeof(status_buf), "%d", ctx->status ? ctx->status : 200);
    index = hpack_exact_index(":status", status_buf);
    if (index) block[block_len++] = (unsigned char)(0x80U | index);
    else hpack_emit_lit_indexed(block, &block_len, 8, status_buf);
    for (i = 0; i < ctx->nhdr; i++) {
        if (hpack_emit_config_header(block, &block_len, ctx->hdrs[i]) != 0) return KC_HTTP_ERROR;
    }
    if (http2_write_frame(b, HTTP2_FRAME_HEADERS, HTTP2_FLAG_END_HEADERS | (ctx->input_len == 0 ? HTTP2_FLAG_END_STREAM : 0), HTTP2_STREAM_DEFAULT, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (ctx->input_len > 0 && http2_write_frame(b, HTTP2_FRAME_DATA, HTTP2_FLAG_END_STREAM, HTTP2_STREAM_DEFAULT, ctx->input, ctx->input_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

#define HTTP3_FRAME_DATA 0x00U
#define HTTP3_FRAME_HEADERS 0x01U
#define HTTP3_FRAME_SETTINGS 0x04U

typedef struct {
    const char *name;
    const char *value;
} http3_static_t;

static const http3_static_t http3_static[] = {
    { ":authority", "" },
    { ":path", "/" },
    { "age", "0" },
    { "content-disposition", "" },
    { "content-length", "0" },
    { "cookie", "" },
    { "date", "" },
    { "etag", "" },
    { "if-modified-since", "" },
    { "if-none-match", "" },
    { "last-modified", "" },
    { "link", "" },
    { "location", "" },
    { "referer", "" },
    { "set-cookie", "" },
    { ":method", "CONNECT" },
    { ":method", "DELETE" },
    { ":method", "GET" },
    { ":method", "HEAD" },
    { ":method", "OPTIONS" },
    { ":method", "POST" },
    { ":method", "PUT" },
    { ":scheme", "http" },
    { ":scheme", "https" },
    { ":status", "103" },
    { ":status", "200" },
    { ":status", "304" },
    { ":status", "404" },
    { ":status", "503" },
    { "accept", "*/*" },
    { "accept", "application/dns-message" },
    { "accept-encoding", "gzip, deflate, br" },
    { "accept-ranges", "bytes" },
    { "access-control-allow-headers", "cache-control" },
    { "access-control-allow-headers", "content-type" },
    { "access-control-allow-origin", "*" },
    { "cache-control", "max-age=0" },
    { "cache-control", "max-age=2592000" },
    { "cache-control", "max-age=604800" },
    { "cache-control", "no-cache" },
    { "cache-control", "no-store" },
    { "cache-control", "public, max-age=31536000" },
    { "content-encoding", "br" },
    { "content-encoding", "gzip" },
    { "content-type", "application/dns-message" },
    { "content-type", "application/javascript" },
    { "content-type", "application/json" },
    { "content-type", "application/x-www-form-urlencoded" },
    { "content-type", "image/gif" },
    { "content-type", "image/jpeg" },
    { "content-type", "image/png" },
    { "content-type", "text/css" },
    { "content-type", "text/html; charset=utf-8" },
    { "content-type", "text/plain" },
    { "content-type", "text/plain;charset=utf-8" },
    { "range", "bytes=0-" },
    { "strict-transport-security", "max-age=31536000" },
    { "strict-transport-security", "max-age=31536000; includesubdomains" },
    { "strict-transport-security", "max-age=31536000; includesubdomains; preload" },
    { "vary", "accept-encoding" },
    { "vary", "origin" },
    { "x-content-type-options", "nosniff" },
    { "x-xss-protection", "1; mode=block" },
    { ":status", "100" },
    { ":status", "204" },
    { ":status", "206" },
    { ":status", "302" },
    { ":status", "400" },
    { ":status", "403" },
    { ":status", "421" },
    { ":status", "425" },
    { ":status", "500" },
    { "accept-language", "" },
    { "access-control-allow-credentials", "FALSE" },
    { "access-control-allow-credentials", "TRUE" },
    { "access-control-allow-headers", "*" },
    { "access-control-allow-methods", "get" },
    { "access-control-allow-methods", "get, post, options" },
    { "access-control-allow-methods", "options" },
    { "access-control-expose-headers", "content-length" },
    { "access-control-request-headers", "content-type" },
    { "access-control-request-method", "get" },
    { "access-control-request-method", "post" },
    { "alt-svc", "clear" },
    { "authorization", "" },
    { "content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'" },
    { "early-data", "1" },
    { "expect-ct", "" },
    { "forwarded", "" },
    { "if-range", "" },
    { "origin", "" },
    { "purpose", "prefetch" },
    { "server", "" },
    { "timing-allow-origin", "*" },
    { "upgrade-insecure-requests", "1" },
    { "user-agent", "" },
    { "x-forwarded-for", "" },
    { "x-frame-options", "deny" },
    { "x-frame-options", "sameorigin" }
};

/**
 * Read one QUIC variable-length integer.
 * @param src Input source.
 * @param out Output integer.
 * @return 0 on success, -1 on malformed input.
 */
static int http3_varint_read(http_src_t *src, unsigned long long *out) {
    unsigned char b[8];
    size_t len;
    size_t i;

    if (http_src_read(src, b, 1) != 1) return -1;
    len = (size_t)1U << (b[0] >> 6);
    b[0] &= 0x3fU;
    if (len > 1 && http_src_read(src, b + 1, len - 1U) != len - 1U) return -1;
    *out = 0;
    for (i = 0; i < len; i++) *out = (*out << 8) | b[i];
    return 0;
}

/**
 * Write one QUIC variable-length integer.
 * @param value Integer value.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_varint_write(http_buf_t *b, unsigned long long value) {
    unsigned char buf[8];
    size_t len;
    size_t i;

    if (value < 64ULL) {
        buf[0] = (unsigned char)value;
        len = 1;
    } else if (value < 16384ULL) {
        buf[0] = (unsigned char)(0x40U | ((value >> 8) & 0x3fU));
        buf[1] = (unsigned char)(value & 0xffU);
        len = 2;
    } else if (value < 1073741824ULL) {
        buf[0] = (unsigned char)(0x80U | ((value >> 24) & 0x3fU));
        buf[1] = (unsigned char)((value >> 16) & 0xffU);
        buf[2] = (unsigned char)((value >> 8) & 0xffU);
        buf[3] = (unsigned char)(value & 0xffU);
        len = 4;
    } else if (value < 4611686018427387904ULL) {
        buf[0] = (unsigned char)(0xc0U | ((value >> 56) & 0x3fU));
        for (i = 1; i < 8; i++) buf[i] = (unsigned char)((value >> ((7U - i) * 8U)) & 0xffU);
        len = 8;
    } else {
        return KC_HTTP_ERROR;
    }
    return http_buf_write(b, buf, len) == 0 ? KC_HTTP_OK : KC_HTTP_ERROR;
}

/**
 * Resolve a QPACK static table index.
 * @param index Static table index.
 * @param name Output name pointer.
 * @param value Output value pointer.
 * @return 0 on success, -1 on unknown index.
 */
static int qpack_lookup(unsigned int index, const char **name, const char **value) {
    if (index >= sizeof(http3_static) / sizeof(http3_static[0])) return -1;
    *name = http3_static[index].name;
    *value = http3_static[index].value;
    return 0;
}

/**
 * Add one decoded QPACK field to a message.
 * @param msg Destination message.
 * @param name Header name.
 * @param value Header value.
 * @return 0 on success, -1 on allocation failure.
 */
static int http3_msg_add(http_msg_t *msg, const char *name, const char *value) {
    char *qmark;

    if (strcmp(name, ":method") == 0) {
        msg->type = HTTP_TYPE_REQUEST;
        msg->method = http_strdup(value);
        return msg->method ? 0 : -1;
    }
    if (strcmp(name, ":path") == 0) {
        msg->target = http_strdup(value);
        if (!msg->target) return -1;
        qmark = strchr(msg->target, '?');
        if (qmark) {
            msg->path = (char *)malloc((size_t)(qmark - msg->target) + 1U);
            if (!msg->path) return -1;
            memcpy(msg->path, msg->target, (size_t)(qmark - msg->target));
            msg->path[qmark - msg->target] = '\0';
            msg->query = http_strdup(qmark + 1);
            return msg->query ? 0 : -1;
        }
        msg->path = http_strdup(msg->target);
        return msg->path ? 0 : -1;
    }
    if (strcmp(name, ":status") == 0) {
        msg->type = HTTP_TYPE_RESPONSE;
        msg->status = atoi(value);
        msg->reason = http_strdup(http_default_reason(msg->status));
        return msg->reason ? 0 : -1;
    }
    if (strcmp(name, ":scheme") == 0) return 0;
    if (strcmp(name, ":authority") == 0) name = "host";
    if (msg->nhdr >= HTTP_HDR_MAX) return -1;
    msg->hdrs[msg->nhdr].name = http_strdup_lower(name);
    msg->hdrs[msg->nhdr].value = http_strdup(value);
    if (!msg->hdrs[msg->nhdr].name || !msg->hdrs[msg->nhdr].value) return -1;
    msg->nhdr++;
    return 0;
}

/**
 * Decode a QPACK field block.
 * @param msg Destination message.
 * @param block Encoded field block.
 * @param len Field block length.
 * @return 0 on success, -1 on malformed input.
 */
static int qpack_decode(http_msg_t *msg, const unsigned char *block, size_t len) {
    const unsigned char *p = block;
    const unsigned char *end = block + len;
    unsigned int dummy;

    if (hpack_int(&p, end, 8, &dummy) != 0) return -1;
    if (hpack_int(&p, end, 7, &dummy) != 0) return -1;
    while (p < end) {
        const char *name;
        const char *value;
        char *own_value = NULL;
        unsigned int index;
        unsigned char b = *p;

        if ((b & 0xc0U) == 0xc0U) {
            if (hpack_int(&p, end, 6, &index) != 0) return -1;
            if (qpack_lookup(index, &name, &value) != 0) return -1;
            if (http3_msg_add(msg, name, value) != 0) return -1;
        } else if ((b & 0xf0U) == 0x50U) {
            if (hpack_int(&p, end, 4, &index) != 0) return -1;
            if (qpack_lookup(index, &name, &value) != 0) return -1;
            if (hpack_string(&p, end, &own_value) != 0) return -1;
            if (http3_msg_add(msg, name, own_value) != 0) { free(own_value); return -1; }
            free(own_value);
        } else {
            return -1;
        }
    }
    return 0;
}

/**
 * Parse one HTTP/3 message from a stream.
 * @param src Input source.
 * @param msg Destination message.
 * @return 0 on success, -1 on parse error, 1 on clean EOF.
 */
static int http3_parse(http_src_t *src, http_msg_t *msg) {
    int saw_headers = 0;
    int done = 0;

    memset(msg, 0, sizeof(*msg));
    msg->ver_major = 3;
    msg->ver_minor = 0;
    while (!done) {
        unsigned long long type;
        unsigned long long len;
        unsigned char *payload = NULL;
        if (http3_varint_read(src, &type) != 0) break;
        if (http3_varint_read(src, &len) != 0 || len > (unsigned long long)((size_t)-1)) return -1;
        if (len > 0) {
            payload = (unsigned char *)malloc((size_t)len);
            if (!payload) return -1;
            if (http_src_read(src, payload, (size_t)len) != (size_t)len) { free(payload); return -1; }
        }
        if (type == HTTP3_FRAME_HEADERS) {
            if (qpack_decode(msg, payload, (size_t)len) != 0) { free(payload); return -1; }
            saw_headers = 1;
        } else if (type == HTTP3_FRAME_DATA) {
            if (http2_append(&msg->body, &msg->body_len, payload, (size_t)len) != 0) { free(payload); return -1; }
            done = 1;
        } else if (type == HTTP3_FRAME_SETTINGS) {
        } else if (saw_headers) {
            done = 1;
        }
        free(payload);
    }
    if (!saw_headers || msg->type == 0) return 1;
    if (msg->type == HTTP_TYPE_REQUEST && !msg->path) msg->path = http_strdup(msg->target ? msg->target : "");
    return 0;
}

/**
 * Find an exact QPACK static entry.
 * @param name Header name.
 * @param value Header value.
 * @return Static index, or -1 if not found.
 */
static int qpack_exact_index(const char *name, const char *value) {
    size_t i;

    for (i = 0; i < sizeof(http3_static) / sizeof(http3_static[0]); i++) {
        if (strcmp(http3_static[i].name, name) == 0 && strcmp(http3_static[i].value, value) == 0) return (int)i;
    }
    return -1;
}

/**
 * Find a QPACK static name entry.
 * @param name Header name.
 * @return Static index, or -1 if not found.
 */
static int qpack_name_index(const char *name) {
    size_t i;

    for (i = 0; i < sizeof(http3_static) / sizeof(http3_static[0]); i++) {
        if (strcmp(http3_static[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/**
 * Emit a QPACK indexed static field.
 * @param out Output block.
 * @param len Output length pointer.
 * @param index Static index.
 * @return None.
 */
static void qpack_emit_index(unsigned char *out, size_t *len, unsigned int index) {
    hpack_emit_int(out, len, 0xc0U, 6, index);
}

/**
 * Emit a QPACK literal field with a static name.
 * @param out Output block.
 * @param len Output length pointer.
 * @param index Static name index.
 * @param value Field value.
 * @return None.
 */
static void qpack_emit_lit_static(unsigned char *out, size_t *len, unsigned int index, const char *value) {
    hpack_emit_int(out, len, 0x50U, 4, index);
    hpack_emit_string(out, len, value);
}

/**
 * Emit one configured header into a QPACK block.
 * @param out Output block.
 * @param len Output length pointer.
 * @param header Configured header string.
 * @return 0 on success, -1 on malformed input.
 */
static int qpack_emit_config_header(unsigned char *out, size_t *len, const char *header) {
    char *name;
    char *lower;
    char *value;
    int index;

    if (http2_split_header(header, &name, &value) != 0) return -1;
    lower = http_strdup_lower(name);
    free(name);
    if (!lower) { free(value); return -1; }
    if (strcmp(lower, "host") == 0) index = 0;
    else index = qpack_name_index(lower);
    if (index < 0) { free(lower); free(value); return -1; }
    qpack_emit_lit_static(out, len, (unsigned int)index, value);
    free(lower);
    free(value);
    return 0;
}

/**
 * Write one HTTP/3 frame.
 * @param type Frame type.
 * @param payload Frame payload.
 * @param len Payload length.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_write_frame(http_buf_t *b, unsigned long long type, const unsigned char *payload, size_t len) {
    if (http3_varint_write(b, type) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (http3_varint_write(b, (unsigned long long)len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (len > 0 && http_buf_write(b, payload, len) != 0) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP/3 request into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_build_request(const kc_http_t *ctx, http_buf_t *b) {
    const char *method = ctx->method ? ctx->method : "GET";
    const char *target = ctx->target ? ctx->target : "/";
    unsigned char block[65536];
    size_t block_len = 0;
    int index;
    int i;

    block[block_len++] = 0x00U;
    block[block_len++] = 0x00U;
    index = qpack_exact_index(":method", method);
    if (index >= 0) qpack_emit_index(block, &block_len, (unsigned int)index);
    else qpack_emit_lit_static(block, &block_len, 17U, method);
    index = qpack_exact_index(":path", target);
    if (index >= 0) qpack_emit_index(block, &block_len, (unsigned int)index);
    else qpack_emit_lit_static(block, &block_len, 1U, target);
    qpack_emit_index(block, &block_len, 23U);
    for (i = 0; i < ctx->nhdr; i++) {
        if (qpack_emit_config_header(block, &block_len, ctx->hdrs[i]) != 0) return KC_HTTP_ERROR;
    }
    if (http3_write_frame(b, HTTP3_FRAME_HEADERS, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (ctx->input_len > 0 && http3_write_frame(b, HTTP3_FRAME_DATA, ctx->input, ctx->input_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP/3 response into a buffer.
 * @param ctx Build context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_build_response(const kc_http_t *ctx, http_buf_t *b) {
    unsigned char block[65536];
    char status_buf[16];
    size_t block_len = 0;
    int index;
    int i;

    snprintf(status_buf, sizeof(status_buf), "%d", ctx->status ? ctx->status : 200);
    block[block_len++] = 0x00U;
    block[block_len++] = 0x00U;
    index = qpack_exact_index(":status", status_buf);
    if (index >= 0) qpack_emit_index(block, &block_len, (unsigned int)index);
    else qpack_emit_lit_static(block, &block_len, 25U, status_buf);
    for (i = 0; i < ctx->nhdr; i++) {
        if (qpack_emit_config_header(block, &block_len, ctx->hdrs[i]) != 0) return KC_HTTP_ERROR;
    }
    if (http3_write_frame(b, HTTP3_FRAME_HEADERS, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (ctx->input_len > 0 && http3_write_frame(b, HTTP3_FRAME_DATA, ctx->input, ctx->input_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Initialize a new http context.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_open(kc_http_t **out, const kc_http_options_t *opts) {
    kc_http_t *ctx;

    if (!out || !opts) return KC_HTTP_ERROR;

    ctx = (kc_http_t *)calloc(1, sizeof(kc_http_t));
    if (!ctx) return KC_HTTP_ERROR;

    ctx->opts = *opts;
    ctx->opts.method = opts->method ? http_strdup(opts->method) : NULL;
    ctx->opts.target = opts->target ? http_strdup(opts->target) : NULL;
    ctx->opts.version = opts->version ? http_strdup(opts->version) : NULL;

    ctx->chunk_size = opts->chunk_size > 0 ? opts->chunk_size : HTTP_CHUNK_DEFAULT;
    
    if (ctx->opts.method) kc_http_set_method(ctx, ctx->opts.method);
    if (ctx->opts.target) kc_http_set_target(ctx, ctx->opts.target);
    if (ctx->opts.version) kc_http_set_version(ctx, ctx->opts.version);
    if (ctx->opts.chunked) kc_http_set_chunked(ctx, ctx->opts.chunked);

    *out = ctx;
    return KC_HTTP_OK;
}

/**
 * Parse one or all HTTP/1.x messages from a stream.
 * @param ctx Parse context.
 * @param src Input source.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http_parse_http1_stream(const kc_http_t *ctx, http_src_t *src, http_buf_t *b) {
    http_msg_t msg;
    int        first = 1;

    for (;;) {
        if (ctx->stop_requested) return KC_HTTP_ESTOP;
        int r = http1_parse(src, &msg);
        if (r == 1) {
            http_msg_free(&msg);
            if (first) return KC_HTTP_ERROR;
            break;
        }
        if (r != 0) {
            http_msg_free(&msg);
            return KC_HTTP_ERROR;
        }
        if (!first && ctx->all) {
            if (http_buf_printf(b, "\n\n") != 0) { http_msg_free(&msg); return KC_HTTP_ERROR; }
        }
        if (http1_emit_msg(b, &msg) != 0) {
            http_msg_free(&msg);
            return KC_HTTP_ERROR;
        }
        http_msg_free(&msg);
        first = 0;
        if (!ctx->all) break;
    }
    return KC_HTTP_OK;
}

/**
 * Detect the HTTP protocol version and parse input.
 * @param ctx Parse context.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http_do_parse(const kc_http_t *ctx, http_buf_t *b) {
    static const unsigned char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    http_src_t src;
    size_t     first_n;
    int        rc;

    memset(&src, 0, sizeof(src));
    src.data = ctx->input;
    src.len  = ctx->input_len;
    first_n = (src.len < sizeof(preface) - 1) ? src.len : sizeof(preface) - 1;
    memcpy(src.pre, src.data, first_n);
    src.data += first_n;
    src.len  -= first_n;
    src.pre_n = first_n;
    src.pos = 0;
    if (first_n == sizeof(preface) - 1 && memcmp(src.pre, preface, sizeof(preface) - 1) == 0) {
        http_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        src.pre_i = src.pre_n;
        rc = http2_parse(&src, &msg);
        if (rc != 0) {
            http_msg_free(&msg);
            return KC_HTTP_ERROR;
        }
        rc = http1_emit_msg(b, &msg);
        http_msg_free(&msg);
        return rc == 0 ? KC_HTTP_OK : KC_HTTP_ERROR;
    }
    if (first_n > 0 && src.pre[0] < 0x20) {
        http_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        rc = http3_parse(&src, &msg);
        if (rc == 0) rc = http1_emit_msg(b, &msg);
        http_msg_free(&msg);
    } else {
        rc = http_parse_http1_stream(ctx, &src, b);
    }
    return rc == 0 ? KC_HTTP_OK : KC_HTTP_ERROR;
}

/**
 * Execute the configured http operation.
 * @param ctx Context pointer.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_exec(kc_http_t *ctx) {
    const char *version;
    http_buf_t buf;
    int rc;

    if (!ctx) return KC_HTTP_ERROR;
    version = ctx->version ? ctx->version : "1.1";
    http_buf_init(&buf);
    switch (ctx->op) {
        case KC_HTTP_OP_PARSE:
            rc = http_do_parse(ctx, &buf);
            break;
        case KC_HTTP_OP_BUILD_REQUEST:
            if (strcmp(version, "2") == 0) rc = http2_build_request(ctx, &buf);
            else if (strcmp(version, "3") == 0) rc = http3_build_request(ctx, &buf);
            else rc = http1_build_request(ctx, &buf);
            break;
        case KC_HTTP_OP_BUILD_RESPONSE:
            if (strcmp(version, "2") == 0) rc = http2_build_response(ctx, &buf);
            else if (strcmp(version, "3") == 0) rc = http3_build_response(ctx, &buf);
            else rc = http1_build_response(ctx, &buf);
            break;
        default:
            free(buf.data);
            return KC_HTTP_ERROR;
    }
    if (rc != KC_HTTP_OK) { free(buf.data); return rc; }
    free(ctx->result);
    ctx->result = buf.data;
    ctx->result_len = buf.len;
    return KC_HTTP_OK;
}

/**
 * Set the input buffer for the next exec operation.
 * @param ctx Context pointer.
 * @param data Input buffer pointer.
 * @param len Input buffer size in bytes.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_set_input(kc_http_t *ctx, const void *data, size_t len) {
    if (!ctx) return KC_HTTP_ERROR;
    ctx->input = (const unsigned char *)data;
    ctx->input_len = len;
    return KC_HTTP_OK;
}

/**
 * Get the output buffer from the last exec operation.
 * @param ctx Context pointer.
 * @param out Receives pointer to output buffer (caller does not own).
 * @param out_len Receives output buffer size in bytes.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_get_output(kc_http_t *ctx, unsigned char **out, size_t *out_len) {
    if (!ctx) return KC_HTTP_ERROR;
    if (out) *out = ctx->result;
    if (out_len) *out_len = ctx->result_len;
    return KC_HTTP_OK;
}

/**
 * Release a http context and all owned memory.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_http_close(kc_http_t *ctx) {
    int i;
    if (!ctx) return;
    free(ctx->result);
    free(ctx->method);
    free(ctx->target);
    free(ctx->version);
    free(ctx->reason);
    for (i = 0; i < ctx->nhdr; i++)      free(ctx->hdrs[i]);
    for (i = 0; i < ctx->ntrailer; i++)  free(ctx->trailers[i]);
    kc_http_options_free(&ctx->opts);
    free(ctx);
}

/**
 * Set the operation to perform.
 * @param ctx Context pointer.
 * @param op Operation constant.
 * @return None.
 */
void kc_http_set_op(kc_http_t *ctx, int op) {
    if (ctx) ctx->op = op;
}

/**
 * Enable multi-message parse mode.
 * @param ctx Context pointer.
 * @param all Non-zero to parse all messages until EOF.
 * @return None.
 */
void kc_http_set_all(kc_http_t *ctx, int all) {
    if (ctx) ctx->all = all;
}

/**
 * Set the HTTP method for build request.
 * @param ctx Context pointer.
 * @param method Method string.
 * @return None.
 */
void kc_http_set_method(kc_http_t *ctx, const char *method) {
    if (!ctx) return;
    free(ctx->method);
    ctx->method = http_strdup(method);
}

/**
 * Set the request target for build request.
 * @param ctx Context pointer.
 * @param target Target string.
 * @return None.
 */
void kc_http_set_target(kc_http_t *ctx, const char *target) {
    if (!ctx) return;
    free(ctx->target);
    ctx->target = http_strdup(target);
}

/**
 * Set the HTTP version for build operations.
 * @param ctx Context pointer.
 * @param version Version string.
 * @return None.
 */
void kc_http_set_version(kc_http_t *ctx, const char *version) {
    if (!ctx) return;
    free(ctx->version);
    ctx->version = http_strdup(version);
}

/**
 * Set the response status code.
 * @param ctx Context pointer.
 * @param status HTTP status code.
 * @return None.
 */
void kc_http_set_status(kc_http_t *ctx, int status) {
    if (ctx) ctx->status = status;
}

/**
 * Set the response reason phrase.
 * @param ctx Context pointer.
 * @param reason Reason string.
 * @return None.
 */
void kc_http_set_reason(kc_http_t *ctx, const char *reason) {
    if (!ctx) return;
    free(ctx->reason);
    ctx->reason = http_strdup(reason);
}

/**
 * Enable chunked transfer encoding for build operations.
 * @param ctx Context pointer.
 * @param chunked Non-zero to use chunked encoding.
 * @return None.
 */
void kc_http_set_chunked(kc_http_t *ctx, int chunked) {
    if (ctx) ctx->chunked = chunked;
}

/**
 * Set the chunk size for chunked build operations.
 * @param ctx Context pointer.
 * @param size Chunk size in bytes.
 * @return None.
 */
void kc_http_set_chunk_size(kc_http_t *ctx, size_t size) {
    if (ctx && size > 0) ctx->chunk_size = size;
}

/**
 * Add a header for build operations.
 * @param ctx Context pointer.
 * @param header Header string in "name: value" format.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if limit exceeded.
 */
int kc_http_add_header(kc_http_t *ctx, const char *header) {
    if (!ctx || ctx->nhdr >= HTTP_HDR_MAX) return KC_HTTP_ERROR;
    ctx->hdrs[ctx->nhdr] = http_strdup(header);
    if (!ctx->hdrs[ctx->nhdr]) return KC_HTTP_ERROR;
    ctx->nhdr++;
    return KC_HTTP_OK;
}

/**
 * Add a trailer for chunked build operations.
 * @param ctx Context pointer.
 * @param trailer Trailer string in "name: value" format.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if limit exceeded.
 */
int kc_http_add_trailer(kc_http_t *ctx, const char *trailer) {
    if (!ctx || ctx->ntrailer >= HTTP_HDR_MAX) return KC_HTTP_ERROR;
    ctx->trailers[ctx->ntrailer] = http_strdup(trailer);
    if (!ctx->trailers[ctx->ntrailer]) return KC_HTTP_ERROR;
    ctx->ntrailer++;
    return KC_HTTP_OK;
}

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_http_options_t kc_http_options_default(void) {
    kc_http_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.chunk_size = HTTP_CHUNK_DEFAULT;
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_http_options_load_env(kc_http_options_t *opts) {
    int i;
    if (!opts) return;

    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;

        if (!val) continue;

        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_FLOAT: {
                float v = strtof(val, &end);
                if (end != val && *end == '\0') {
                    *(float *)((char *)opts + env_config_table[i].offset) = v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                char **p = (char **)((char *)opts + env_config_table[i].offset);
                free(*p);
                *p = http_strdup(val);
                break;
            }
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_http_options_free(kc_http_options_t *opts) {
    if (!opts) return;
    free(opts->method);
    free(opts->target);
    free(opts->version);
    opts->method = NULL;
    opts->target = NULL;
    opts->version = NULL;
}

/**
 * Requests a context to stop at the next opportunity.
 * @param ctx HTTP context.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_stop(kc_http_t *ctx) {
    if (!ctx) return KC_HTTP_ERROR;
    ctx->stop_requested = 1;
    return KC_HTTP_OK;
}

#ifndef KC_HTTP_BUILD_VERSION
#define KC_HTTP_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_http_version(void) {
    return (uint64_t)KC_HTTP_BUILD_VERSION;
}
