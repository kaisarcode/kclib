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
#include "libhttp.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_HTTP_ERROR      (-1)

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

typedef struct kc_http {
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
    char   error[256];
} kc_http_t;

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
 * Set or clear the most recent contextual error.
 * @param ctx Context pointer.
 * @param message Error message, or NULL to clear it.
 * @return None.
 */
static void http_set_error(kc_http_t *ctx, const char *message) {
    if (!ctx) return;
    if (!message) {
        ctx->error[0] = '\0';
        return;
    }
    snprintf(ctx->error, sizeof(ctx->error), "%s", message);
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
 * Return whether a field name is a non-empty HTTP token.
 * @param name Field name to validate.
 * @return Non-zero when name is valid, zero otherwise.
 */
static int http_field_name_valid(const char *name) {
    const unsigned char *p;

    if (!name || !*name) return 0;
    for (p = (const unsigned char *)name; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') ||
            (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9')) continue;
        switch (*p) {
            case '!': case '#': case '$': case '%': case '&': case '\'':
            case '*': case '+': case '-': case '.': case '^': case '_':
            case '`': case '|': case '~':
                break;
            default:
                return 0;
        }
    }
    return 1;
}

/**
 * Return whether a field value has only HTTP field-value bytes.
 * @param value Field value to validate.
 * @return Non-zero when value is valid, zero otherwise.
 */
static int http_field_value_valid(const char *value) {
    const unsigned char *p;

    if (!value) return 0;
    for (p = (const unsigned char *)value; *p; p++) {
        if (*p == '\t' || (*p >= 0x20 && *p != 0x7f)) continue;
        return 0;
    }
    return 1;
}

/**
 * Add one validated field to an ordered builder field list.
 * @param ctx Context pointer.
 * @param fields Destination field list.
 * @param count Destination field count.
 * @param name Field name.
 * @param value Field value.
 * @param kind Field kind for contextual errors.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
static int http_add_field(kc_http_t *ctx, char **fields, int *count,
const char *name, const char *value,
const char *kind) {
    char   *field;
    size_t  name_len;
    size_t  value_len;

    if (!name || !value) {
        http_set_error(ctx, "HTTP field name and value are required");
        return KC_HTTP_ERROR;
    }
    if (*count >= HTTP_HDR_MAX) {
        http_set_error(ctx, kind);
        return KC_HTTP_ERROR;
    }
    if (!http_field_name_valid(name)) {
        http_set_error(ctx, "invalid HTTP field name");
        return KC_HTTP_ERROR;
    }
    if (!http_field_value_valid(value)) {
        http_set_error(ctx, "invalid HTTP field value");
        return KC_HTTP_ERROR;
    }

    name_len = strlen(name);
    value_len = strlen(value);
    if (value_len > (size_t)-1 - 3U ||
        name_len > (size_t)-1 - value_len - 3U) {
        http_set_error(ctx, "HTTP field is too large");
        return KC_HTTP_ERROR;
    }
    field = (char *)malloc(name_len + value_len + 3U);
    if (!field) {
        http_set_error(ctx, "HTTP field allocation failed");
        return KC_HTTP_ERROR;
    }
    memcpy(field, name, name_len);
    memcpy(field + name_len, ": ", 2);
    memcpy(field + name_len + 2, value, value_len + 1);
    fields[*count] = field;
    (*count)++;
    http_set_error(ctx, NULL);
    return KC_HTTP_OK;
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
 * Read and validate the Content-Length fields in a message.
 * @param msg Source message.
 * @param body_len Receives the declared body length.
 * @return One when present, zero when absent, or -1 when malformed.
 */
static int http1_content_length(const http_msg_t *msg, size_t *body_len) {
    int    i;
    int    found = 0;
    size_t length = 0;

    for (i = 0; i < msg->nhdr; i++) {
        const char    *value;
        const char    *p;
        char          *endp;
        unsigned long  parsed;

        if (strcmp(msg->hdrs[i].name, "content-length") != 0) continue;
        value = msg->hdrs[i].value;
        if (!*value) return -1;
        for (p = value; *p; p++) {
            if (!isdigit((unsigned char)*p)) return -1;
        }
        errno = 0;
        parsed = strtoul(value, &endp, 10);
        if (errno != 0 || *endp != '\0' || (unsigned long)(size_t)parsed != parsed) return -1;
        if (found && length != (size_t)parsed) return -1;
        length = (size_t)parsed;
        found = 1;
    }
    if (found) *body_len = length;
    return found;
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
        errno = 0;
        chunk_size = (size_t)strtoul(line, &end, 16);
        if (errno != 0 || end == line || *end != '\0') { free(line); free(body); return -1; }
        free(line);

        if (chunk_size == 0) {
            for (;;) {
                char  *tline = NULL;
                size_t tlen  = 0;
                int    r     = http_read_line(src, &tline, &tlen);
                const char *colon;
                char       *tname;
                char       *tvalue;

                if (r <= 0) { free(tline); free(body); return -1; }
                if (tlen == 0) { free(tline); break; }
                if (msg->ntrailer >= HTTP_HDR_MAX) {
                    free(tline); free(body); return -1;
                }
                colon = strchr(tline, ':');
                if (!colon) { free(tline); free(body); return -1; }
                tname = (char *)malloc((size_t)(colon - tline) + 1);
                tvalue = http_strdup(colon + 1);
                if (!tname || !tvalue) {
                    free(tname); free(tvalue); free(tline); free(body); return -1;
                }
                memcpy(tname, tline, (size_t)(colon - tline));
                tname[colon - tline] = '\0';
                msg->trailers[msg->ntrailer].name = http_strdup_lower(tname);
                msg->trailers[msg->ntrailer].value = http_trim(tvalue);
                free(tname);
                if (!msg->trailers[msg->ntrailer].name) {
                    free(tvalue); free(tline); free(body); return -1;
                }
                msg->ntrailer++;
                free(tline);
            }
            break;
        }

        chunk_data = (unsigned char *)malloc(chunk_size);
        if (!chunk_data) { free(body); return -1; }
        if (http_src_read(src, chunk_data, chunk_size) != chunk_size) {
            free(chunk_data); free(body); return -1;
        }

        if (http_read_line(src, &crlf, &clen) <= 0 || clen != 0) {
            free(crlf); free(chunk_data); free(body); return -1;
        }
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
            if (http1_add_header(line, msg) != 0) {
                free(line);
                return -1;
            }
            free(line);
        }
        if (!got_blank) return -1;
    }

    {
        const char *te = http1_header_value(msg, "transfer-encoding");
        size_t      body_len;
        int         has_cl;
        int         no_body = 0;

        has_cl = http1_content_length(msg, &body_len);
        if (has_cl < 0) return -1;
        if (has_cl && te && strstr(te, "chunked") != NULL) return -1;

        if (msg->type == HTTP_TYPE_RESPONSE) {
            if (msg->status == 204 || msg->status == 304 ||
                (msg->status >= 100 && msg->status < 200)) no_body = 1;
        } else {
            if (!has_cl && (!te || strstr(te, "chunked") == NULL)) no_body = 1;
        }

        if (!no_body) {
            if (te && strstr(te, "chunked") != NULL) {
                if (http1_read_chunked(src, msg) != 0) return -1;
            } else if (has_cl) {
                if (http_read_bytes(src, body_len, &msg->body) != 0) return -1;
                msg->body_len = body_len;
            }
        }
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
 * @param ctx Build context.
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @return 0 on success, -1 on write error.
 */
static int http_emit_body_chunked(http_buf_t *b, const kc_http_t *ctx,
const unsigned char *body, size_t body_size) {
    size_t chunk_size = ctx->chunk_size ? ctx->chunk_size : HTTP_CHUNK_DEFAULT;
    size_t offset = 0;
    size_t n;
    int    i;

    while (offset < body_size) {
        n = body_size - offset;
        if (n > chunk_size) n = chunk_size;
        if (http_buf_printf(b, "%zx\r\n", n) != 0) return -1;
        if (http_buf_write(b, body + offset, n) != 0) return -1;
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
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http1_build_request(const kc_http_t *ctx, const unsigned char *body,
size_t body_size, http_buf_t *b) {
    const char    *method  = ctx->method  ? ctx->method  : "GET";
    const char    *target  = ctx->target  ? ctx->target  : "/";
    const char    *version = ctx->version ? ctx->version : "1.1";
    int            rc      = KC_HTTP_OK;

    if (http_buf_printf(b, "%s %s HTTP/%s\r\n", method, target, version) != 0) return KC_HTTP_ERROR;
    if (http_emit_headers(b, ctx, body_size) != 0) return KC_HTTP_ERROR;

    if (ctx->chunked) {
        if (http_emit_body_chunked(b, ctx, body, body_size) != 0) rc = KC_HTTP_ERROR;
    } else {
        if (http_emit_body_cl(b, body, body_size) != 0) rc = KC_HTTP_ERROR;
    }

    return rc;
}

/**
 * Build one HTTP/1.x response into a buffer.
 * @param ctx Build context.
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http1_build_response(const kc_http_t *ctx, const unsigned char *body,
                                size_t body_size, http_buf_t *b) {
    const char    *version = ctx->version ? ctx->version : "1.1";
    int            status  = ctx->status  ? ctx->status  : 200;
    const char    *reason  = ctx->reason  ? ctx->reason  : http_default_reason(status);
    int            rc      = KC_HTTP_OK;

    if (http_buf_printf(b, "HTTP/%s %d %s\r\n", version, status, reason) != 0) return KC_HTTP_ERROR;
    if (http_emit_headers(b, ctx, body_size) != 0) return KC_HTTP_ERROR;

    if (ctx->chunked) {
        if (http_emit_body_chunked(b, ctx, body, body_size) != 0) rc = KC_HTTP_ERROR;
    } else {
        if (http_emit_body_cl(b, body, body_size) != 0) rc = KC_HTTP_ERROR;
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
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http2_build_request(const kc_http_t *ctx, const unsigned char *body,
size_t body_size, http_buf_t *b) {
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
    if (http2_write_frame(b, HTTP2_FRAME_HEADERS, HTTP2_FLAG_END_HEADERS | (body_size == 0 ? HTTP2_FLAG_END_STREAM : 0), HTTP2_STREAM_DEFAULT, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (body_size > 0 && http2_write_frame(b, HTTP2_FRAME_DATA, HTTP2_FLAG_END_STREAM, HTTP2_STREAM_DEFAULT, body, body_size) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP/2 response into a buffer.
 * @param ctx Build context.
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http2_build_response(const kc_http_t *ctx, const unsigned char *body,
                                size_t body_size, http_buf_t *b) {
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
    if (http2_write_frame(b, HTTP2_FRAME_HEADERS, HTTP2_FLAG_END_HEADERS | (body_size == 0 ? HTTP2_FLAG_END_STREAM : 0), HTTP2_STREAM_DEFAULT, block, block_len) != KC_HTTP_OK) return KC_HTTP_ERROR;
    if (body_size > 0 && http2_write_frame(b, HTTP2_FRAME_DATA, HTTP2_FLAG_END_STREAM, HTTP2_STREAM_DEFAULT, body, body_size) != KC_HTTP_OK) return KC_HTTP_ERROR;
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
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_build_request(const kc_http_t *ctx, const unsigned char *body,
size_t body_size, http_buf_t *b) {
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
    if (body_size > 0 && http3_write_frame(b, HTTP3_FRAME_DATA, body, body_size) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP/3 response into a buffer.
 * @param ctx Build context.
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http3_build_response(const kc_http_t *ctx, const unsigned char *body,
                                size_t body_size, http_buf_t *b) {
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
    if (body_size > 0 && http3_write_frame(b, HTTP3_FRAME_DATA, body, body_size) != KC_HTTP_OK) return KC_HTTP_ERROR;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP request or response using the configured protocol version.
 * @param ctx Build context.
 * @param response Non-zero to build a response.
 * @param body Borrowed body bytes.
 * @param body_size Number of body bytes.
 * @param b Output buffer.
 * @return KC_HTTP_OK on success, KC_HTTP_ERROR on failure.
 */
static int http_build(const kc_http_t *ctx, int response,
const unsigned char *body, size_t body_size,
http_buf_t *b) {
    const char *version = ctx->version ? ctx->version : "1.1";

    if (strcmp(version, "2") == 0) {
        return response ? http2_build_response(ctx, body, body_size, b)
                        : http2_build_request(ctx, body, body_size, b);
    }
    if (strcmp(version, "3") == 0) {
        return response ? http3_build_response(ctx, body, body_size, b)
                        : http3_build_request(ctx, body, body_size, b);
    }
    return response ? http1_build_response(ctx, body, body_size, b)
                    : http1_build_request(ctx, body, body_size, b);
}

/**
 * Release only heap-owned field strings in an internal build context.
 */
static void http_build_fields_free(kc_http_t *ctx) {
    int i;
    for (i = 0; i < ctx->nhdr; i++) free(ctx->hdrs[i]);
    for (i = 0; i < ctx->ntrailer; i++) free(ctx->trailers[i]);
    ctx->nhdr = 0;
    ctx->ntrailer = 0;
}

/**
 * Copy public fields into the internal builder representation.
 */
static int http_build_fields(
    kc_http_t *ctx,
    const kc_http_field_t *fields,
    size_t count,
    int trailer
) {
    size_t i;

    if (count > HTTP_HDR_MAX) return KC_HTTP_EINVAL;
    if (count != 0U && fields == NULL) return KC_HTTP_EINVAL;

    for (i = 0; i < count; i++) {
        int rc;
        if (trailer) {
            rc = http_add_field(
                ctx,
                ctx->trailers,
                &ctx->ntrailer,
                fields[i].name,
                fields[i].value,
                "trailer limit exceeded"
            );
        } else {
            rc = http_add_field(
                ctx,
                ctx->hdrs,
                &ctx->nhdr,
                fields[i].name,
                fields[i].value,
                "header limit exceeded"
            );
        }
        if (rc != KC_HTTP_OK) return KC_HTTP_EINVAL;
    }
    return KC_HTTP_OK;
}

struct kc_http_parser {
    unsigned char *data;
    size_t len;
    size_t cap;
    kc_http_request_fn request_handler;
    kc_http_response_fn response_handler;
    kc_http_error_fn error_handler;
    void *userdata;
    int failed;
};

/**
 * Grow and append bytes to a parser buffer.
 */
static int http_parser_append(
    kc_http_parser_t *parser,
    const void *data,
    size_t size
) {
    unsigned char *next;
    size_t cap;

    if (size == 0U) return KC_HTTP_OK;
    if (parser->len + size < parser->len) return KC_HTTP_ENOMEM;

    if (parser->len + size > parser->cap) {
        cap = parser->cap != 0U ? parser->cap : 4096U;
        while (cap < parser->len + size) {
            if (cap > ((size_t)-1) / 2U) {
                cap = parser->len + size;
                break;
            }
            cap *= 2U;
        }
        next = (unsigned char *)realloc(parser->data, cap);
        if (next == NULL) return KC_HTTP_ENOMEM;
        parser->data = next;
        parser->cap = cap;
    }

    memcpy(parser->data + parser->len, data, size);
    parser->len += size;
    return KC_HTTP_OK;
}

/**
 * Publish one parsed message through the matching borrowed callback.
 */
static void http_parser_emit(
    kc_http_parser_t *parser,
    const http_msg_t *msg
) {
    kc_http_field_t headers[HTTP_HDR_MAX];
    kc_http_field_t trailers[HTTP_HDR_MAX];
    char version[32];
    int i;

    for (i = 0; i < msg->nhdr; i++) {
        headers[i].name = msg->hdrs[i].name;
        headers[i].value = msg->hdrs[i].value;
    }
    for (i = 0; i < msg->ntrailer; i++) {
        trailers[i].name = msg->trailers[i].name;
        trailers[i].value = msg->trailers[i].value;
    }

    if (msg->ver_major >= 2 && msg->ver_minor == 0) {
        snprintf(version, sizeof(version), "%d", msg->ver_major);
    } else {
        snprintf(version, sizeof(version), "%d.%d", msg->ver_major, msg->ver_minor);
    }

    if (msg->type == HTTP_TYPE_REQUEST && parser->request_handler != NULL) {
        kc_http_request_t request;
        memset(&request, 0, sizeof(request));
        request.version = version;
        request.method = msg->method;
        request.target = msg->target;
        request.path = msg->path;
        request.query = msg->query;
        request.headers = headers;
        request.header_count = (size_t)msg->nhdr;
        request.body = msg->body;
        request.body_size = msg->body_len;
        request.trailers = trailers;
        request.trailer_count = (size_t)msg->ntrailer;
        for (i = 0; i < msg->nhdr; i++) {
            if (strcmp(msg->hdrs[i].name, "transfer-encoding") == 0 &&
                strstr(msg->hdrs[i].value, "chunked") != NULL) {
                request.chunked = 1;
                break;
            }
        }
        parser->request_handler(&request, parser->userdata);
    } else if (msg->type == HTTP_TYPE_RESPONSE && parser->response_handler != NULL) {
        kc_http_response_t response;
        memset(&response, 0, sizeof(response));
        response.version = version;
        response.status = msg->status;
        response.reason = msg->reason;
        response.headers = headers;
        response.header_count = (size_t)msg->nhdr;
        response.body = msg->body;
        response.body_size = msg->body_len;
        response.trailers = trailers;
        response.trailer_count = (size_t)msg->ntrailer;
        for (i = 0; i < msg->nhdr; i++) {
            if (strcmp(msg->hdrs[i].name, "transfer-encoding") == 0 &&
                strstr(msg->hdrs[i].value, "chunked") != NULL) {
                response.chunked = 1;
                break;
            }
        }
        parser->response_handler(&response, parser->userdata);
    }
}

/**
 * Compare an ASCII field name with a lowercase literal.
 */
static int http_name_equal(
    const unsigned char *name,
    size_t name_len,
    const char *lower
) {
    size_t i;
    size_t lower_len = strlen(lower);
    if (name_len != lower_len) return 0;
    for (i = 0; i < name_len; i++) {
        unsigned char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if (c != (unsigned char)lower[i]) return 0;
    }
    return 1;
}

/**
 * Return whether an ASCII field value contains token "chunked".
 */
static int http_value_has_chunked(
    const unsigned char *value,
    size_t value_len
) {
    static const char token[] = "chunked";
    size_t i;

    if (value_len < sizeof(token) - 1U) return 0;
    for (i = 0; i + sizeof(token) - 1U <= value_len; i++) {
        size_t j;
        for (j = 0; j < sizeof(token) - 1U; j++) {
            unsigned char c = value[i + j];
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            if (c != (unsigned char)token[j]) break;
        }
        if (j == sizeof(token) - 1U) return 1;
    }
    return 0;
}

/**
 * Find the end of the HTTP/1 header block.
 *
 * Returns 1 when found and stores the first body offset, zero when incomplete.
 */
static int http1_headers_end(
    const unsigned char *data,
    size_t size,
    size_t *out
) {
    size_t i;

    for (i = 0; i + 3U < size; i++) {
        if (data[i] == '\r' && data[i + 1U] == '\n' &&
            data[i + 2U] == '\r' && data[i + 3U] == '\n') {
            *out = i + 4U;
            return 1;
        }
    }
    for (i = 0; i + 1U < size; i++) {
        if (data[i] == '\n' && data[i + 1U] == '\n') {
            *out = i + 2U;
            return 1;
        }
    }
    return 0;
}

/**
 * Read one unsigned decimal Content-Length value.
 */
static int http_decimal_size(
    const unsigned char *value,
    size_t value_len,
    size_t *out
) {
    size_t i = 0U;
    size_t n = 0U;

    while (i < value_len && (value[i] == ' ' || value[i] == '\t')) i++;
    if (i == value_len) return -1;

    for (; i < value_len; i++) {
        unsigned char c = value[i];
        if (c == ' ' || c == '\t') {
            while (i < value_len && (value[i] == ' ' || value[i] == '\t')) i++;
            if (i != value_len) return -1;
            break;
        }
        if (c < '0' || c > '9') return -1;
        if (n > ((size_t)-1 - (size_t)(c - '0')) / 10U) return -1;
        n = n * 10U + (size_t)(c - '0');
    }

    *out = n;
    return 0;
}

/**
 * Scan chunked HTTP/1 framing.
 *
 * Returns 1 and the consumed size for a complete body, zero when incomplete,
 * or -1 for malformed framing.
 */
static int http1_chunked_size(
    const unsigned char *data,
    size_t size,
    size_t body_offset,
    size_t *out
) {
    size_t pos = body_offset;

    for (;;) {
        size_t line_end;
        size_t i;
        size_t chunk = 0U;
        int digits = 0;

        line_end = pos;
        while (line_end < size && data[line_end] != '\n') line_end++;
        if (line_end == size) return 0;

        i = pos;
        while (i < line_end && data[i] != ';' && data[i] != '\r') {
            unsigned char c = data[i];
            unsigned int v;
            if (c >= '0' && c <= '9') v = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') v = 10U + (unsigned int)(c - 'a');
            else if (c >= 'A' && c <= 'F') v = 10U + (unsigned int)(c - 'A');
            else return -1;
            if (chunk > (((size_t)-1) - v) / 16U) return -1;
            chunk = chunk * 16U + v;
            digits = 1;
            i++;
        }
        if (!digits) return -1;
        pos = line_end + 1U;

        if (chunk == 0U) {
            for (;;) {
                size_t trailer_end = pos;
                while (trailer_end < size && data[trailer_end] != '\n') trailer_end++;
                if (trailer_end == size) return 0;
                if (trailer_end == pos ||
                    (trailer_end == pos + 1U && data[pos] == '\r')) {
                    *out = trailer_end + 1U;
                    return 1;
                }
                pos = trailer_end + 1U;
            }
        }

        if (chunk > size - pos) return 0;
        pos += chunk;
        if (pos == size) return 0;
        if (data[pos] == '\r') {
            if (pos + 1U >= size) return 0;
            if (data[pos + 1U] != '\n') return -1;
            pos += 2U;
        } else if (data[pos] == '\n') {
            pos += 1U;
        } else {
            return -1;
        }
    }
}

/**
 * Determine whether the first buffered HTTP/1 message is complete.
 *
 * Returns 1 with its wire size, zero when more bytes are required, or -1 for
 * framing that is already known to be invalid.
 */
static int http1_message_size(
    const unsigned char *data,
    size_t size,
    size_t *out
) {
    size_t header_end;
    size_t pos;
    size_t content_length = 0U;
    int have_content_length = 0;
    int chunked = 0;

    if (!http1_headers_end(data, size, &header_end)) return 0;

    pos = 0U;
    while (pos < header_end && data[pos] != '\n') pos++;
    if (pos >= header_end) return -1;
    pos++;

    while (pos < header_end) {
        size_t line_end = pos;
        size_t content_end;
        size_t colon;
        size_t value_start;

        while (line_end < header_end && data[line_end] != '\n') line_end++;
        content_end = line_end;
        if (content_end > pos && data[content_end - 1U] == '\r') content_end--;

        if (content_end == pos) break;

        colon = pos;
        while (colon < content_end && data[colon] != ':') colon++;
        if (colon == content_end) return -1;

        value_start = colon + 1U;
        while (value_start < content_end &&
               (data[value_start] == ' ' || data[value_start] == '\t')) {
            value_start++;
        }

        if (http_name_equal(data + pos, colon - pos, "content-length")) {
            size_t parsed;
            if (http_decimal_size(
                    data + value_start,
                    content_end - value_start,
                    &parsed) != 0) {
                return -1;
            }
            if (have_content_length && parsed != content_length) return -1;
            content_length = parsed;
            have_content_length = 1;
        } else if (http_name_equal(
                       data + pos,
                       colon - pos,
                       "transfer-encoding")) {
            if (http_value_has_chunked(
                    data + value_start,
                    content_end - value_start)) {
                chunked = 1;
            }
        }

        pos = line_end + 1U;
    }

    if (chunked && have_content_length) return -1;

    if (chunked) {
        return http1_chunked_size(data, size, header_end, out);
    }

    if (have_content_length) {
        if (content_length > size - header_end) return 0;
        *out = header_end + content_length;
        return 1;
    }

    *out = header_end;
    return 1;
}

/**
 * Parse one complete HTTP/1 message from exactly one wire slice.
 */
static int http_parser_parse_http1(
    kc_http_parser_t *parser,
    const unsigned char *data,
    size_t size
) {
    http_src_t src;
    http_msg_t msg;
    int rc;

    memset(&src, 0, sizeof(src));
    memset(&msg, 0, sizeof(msg));
    src.data = data;
    src.len = size;

    rc = http1_parse(&src, &msg);
    if (rc != 0) {
        http_msg_free(&msg);
        return KC_HTTP_EPARSE;
    }

    http_parser_emit(parser, &msg);
    http_msg_free(&msg);
    return KC_HTTP_OK;
}

/**
 * Try parsing a buffered HTTP/2 or HTTP/3 message.
 *
 * A non-zero parser result is treated as "need more bytes" because those
 * frame parsers historically do not distinguish truncation from malformed
 * framing. Complete malformed input will still be rejected when the stream
 * closes.
 */
static int http_parser_try_binary(kc_http_parser_t *parser) {
    static const unsigned char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    http_src_t src;
    http_msg_t msg;
    int rc;

    memset(&src, 0, sizeof(src));
    memset(&msg, 0, sizeof(msg));

    if (parser->len >= sizeof(preface) - 1U &&
        memcmp(parser->data, preface, sizeof(preface) - 1U) == 0) {
        src.data = parser->data + sizeof(preface) - 1U;
        src.len = parser->len - (sizeof(preface) - 1U);
        rc = http2_parse(&src, &msg);
    } else {
        src.data = parser->data;
        src.len = parser->len;
        rc = http3_parse(&src, &msg);
    }

    if (rc != 0) {
        http_msg_free(&msg);
        return 0;
    }

    http_parser_emit(parser, &msg);
    http_msg_free(&msg);
    parser->len = 0U;
    return 1;
}

/**
 * Create an incremental HTTP parser for one byte stream.
 */
int kc_http_parser_open(
    kc_http_parser_t **out,
    kc_http_request_fn request_handler,
    kc_http_response_fn response_handler,
    kc_http_error_fn error_handler,
    void *userdata
) {
    kc_http_parser_t *parser;

    if (out == NULL) return KC_HTTP_EINVAL;
    *out = NULL;
    if (request_handler == NULL && response_handler == NULL) {
        return KC_HTTP_EINVAL;
    }

    parser = (kc_http_parser_t *)calloc(1, sizeof(*parser));
    if (parser == NULL) return KC_HTTP_ENOMEM;

    parser->request_handler = request_handler;
    parser->response_handler = response_handler;
    parser->error_handler = error_handler;
    parser->userdata = userdata;
    *out = parser;
    return KC_HTTP_OK;
}

/**
 * Feed bytes from one HTTP byte stream.
 */
int kc_http_parser_write(
    kc_http_parser_t *parser,
    const void *data,
    size_t data_size
) {
    int rc;

    if (parser == NULL || (data == NULL && data_size != 0U)) {
        return KC_HTTP_EINVAL;
    }
    if (parser->failed) return KC_HTTP_EPARSE;

    rc = http_parser_append(parser, data, data_size);
    if (rc != KC_HTTP_OK) {
        parser->failed = 1;
        if (parser->error_handler != NULL) {
            parser->error_handler(rc, parser->userdata);
        }
        return rc;
    }

    for (;;) {
        size_t message_size;
        int complete;

        if (parser->len == 0U) return KC_HTTP_OK;

        if (parser->len < 24U &&
            memcmp(
                parser->data,
                "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n",
                parser->len) == 0) {
            return KC_HTTP_OK;
        }

        if ((parser->len >= 1U && parser->data[0] < 0x20U) ||
            (parser->len >= 24U &&
             memcmp(parser->data, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24U) == 0)) {
            (void)http_parser_try_binary(parser);
            return KC_HTTP_OK;
        }

        complete = http1_message_size(parser->data, parser->len, &message_size);
        if (complete == 0) return KC_HTTP_OK;
        if (complete < 0) {
            parser->failed = 1;
            if (parser->error_handler != NULL) {
                parser->error_handler(KC_HTTP_EPARSE, parser->userdata);
            }
            return KC_HTTP_EPARSE;
        }

        rc = http_parser_parse_http1(parser, parser->data, message_size);
        if (rc != KC_HTTP_OK) {
            parser->failed = 1;
            if (parser->error_handler != NULL) {
                parser->error_handler(rc, parser->userdata);
            }
            return rc;
        }

        parser->len -= message_size;
        if (parser->len != 0U) {
            memmove(parser->data, parser->data + message_size, parser->len);
        }
    }
}

/**
 * Finalize and release one parser.
 */
void kc_http_parser_close(kc_http_parser_t *parser) {
    if (parser == NULL) return;
    if (!parser->failed && parser->len != 0U && parser->error_handler != NULL) {
        parser->error_handler(KC_HTTP_EPARSE, parser->userdata);
    }
    free(parser->data);
    free(parser);
}

/**
 * Build one HTTP request into allocated wire bytes.
 */
int kc_http_request(
    const kc_http_request_t *request,
    void **out_data,
    size_t *out_size
) {
    kc_http_t ctx;
    http_buf_t buf;
    const void *body = NULL;
    size_t body_size = 0U;
    int rc;

    if (out_data != NULL) *out_data = NULL;
    if (out_size != NULL) *out_size = 0U;
    if (out_data == NULL || out_size == NULL) return KC_HTTP_EINVAL;

    memset(&ctx, 0, sizeof(ctx));
    ctx.method = (char *)(request != NULL && request->method != NULL
        ? request->method : "GET");
    ctx.target = (char *)(request != NULL && request->target != NULL
        ? request->target : "/");
    ctx.version = (char *)(request != NULL && request->version != NULL
        ? request->version : "1.1");
    ctx.chunk_size = request != NULL && request->chunk_size != 0U
        ? request->chunk_size : HTTP_CHUNK_DEFAULT;

    if (request != NULL) {
        if (request->body == NULL && request->body_size != 0U) {
            return KC_HTTP_EINVAL;
        }
        body = request->body;
        body_size = request->body_size;
        ctx.chunked = request->chunked ? 1 : 0;

        rc = http_build_fields(
            &ctx,
            request->headers,
            request->header_count,
            0
        );
        if (rc != KC_HTTP_OK) {
            http_build_fields_free(&ctx);
            return rc;
        }
        rc = http_build_fields(
            &ctx,
            request->trailers,
            request->trailer_count,
            1
        );
        if (rc != KC_HTTP_OK) {
            http_build_fields_free(&ctx);
            return rc;
        }
    }

    http_buf_init(&buf);
    rc = http_build(
        &ctx,
        0,
        (const unsigned char *)body,
        body_size,
        &buf
    );
    http_build_fields_free(&ctx);

    if (rc != KC_HTTP_OK) {
        free(buf.data);
        return KC_HTTP_EINVAL;
    }

    *out_data = buf.data;
    *out_size = buf.len;
    return KC_HTTP_OK;
}

/**
 * Build one HTTP response into allocated wire bytes.
 */
int kc_http_response(
    const kc_http_response_t *response,
    void **out_data,
    size_t *out_size
) {
    kc_http_t ctx;
    http_buf_t buf;
    const void *body = NULL;
    size_t body_size = 0U;
    int rc;

    if (out_data != NULL) *out_data = NULL;
    if (out_size != NULL) *out_size = 0U;
    if (out_data == NULL || out_size == NULL) return KC_HTTP_EINVAL;

    memset(&ctx, 0, sizeof(ctx));
    ctx.version = (char *)(response != NULL && response->version != NULL
        ? response->version : "1.1");
    ctx.status = response != NULL && response->status != 0
        ? response->status : 200;
    ctx.reason = (char *)(response != NULL ? response->reason : NULL);
    ctx.chunk_size = response != NULL && response->chunk_size != 0U
        ? response->chunk_size : HTTP_CHUNK_DEFAULT;

    if (ctx.status < 100 || ctx.status > 599) return KC_HTTP_EINVAL;

    if (response != NULL) {
        if (response->body == NULL && response->body_size != 0U) {
            return KC_HTTP_EINVAL;
        }
        body = response->body;
        body_size = response->body_size;
        ctx.chunked = response->chunked ? 1 : 0;

        rc = http_build_fields(
            &ctx,
            response->headers,
            response->header_count,
            0
        );
        if (rc != KC_HTTP_OK) {
            http_build_fields_free(&ctx);
            return rc;
        }
        rc = http_build_fields(
            &ctx,
            response->trailers,
            response->trailer_count,
            1
        );
        if (rc != KC_HTTP_OK) {
            http_build_fields_free(&ctx);
            return rc;
        }
    }

    http_buf_init(&buf);
    rc = http_build(
        &ctx,
        1,
        (const unsigned char *)body,
        body_size,
        &buf
    );
    http_build_fields_free(&ctx);

    if (rc != KC_HTTP_OK) {
        free(buf.data);
        return KC_HTTP_EINVAL;
    }

    *out_data = buf.data;
    *out_size = buf.len;
    return KC_HTTP_OK;
}

/**
 * Release caller-owned output memory.
 */
void kc_http_free(void *ptr) {
    free(ptr);
}

/**
 * Return a static message for a public status code.
 */
const char *kc_http_strerror(int status) {
    switch (status) {
        case KC_HTTP_OK: return "ok";
        case KC_HTTP_EINVAL: return "invalid argument";
        case KC_HTTP_EPARSE: return "HTTP parse error";
        case KC_HTTP_ENOMEM: return "out of memory";
        default: return "unknown error";
    }
}

#ifndef KC_HTTP_BUILD_VERSION
#define KC_HTTP_BUILD_VERSION 0
#endif

/**
 * Return the build version generated at compile time.
 */
uint64_t kc_http_version(void) {
    return (uint64_t)KC_HTTP_BUILD_VERSION;
}
