/**
 * libhttp.h - HTTP protocol parser and builder.
 * Summary: Incrementally parses HTTP streams and builds HTTP requests and responses.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_HTTP_H
#define KC_HTTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_HTTP_OK       0
#define KC_HTTP_EINVAL  -1
#define KC_HTTP_EPARSE  -2
#define KC_HTTP_ENOMEM  -3

typedef struct kc_http_parser kc_http_parser_t;

typedef struct {
    const char *name;
    const char *value;
} kc_http_field_t;

typedef struct {
    const char *version;
    const char *method;
    const char *target;
    const char *path;
    const char *query;
    const kc_http_field_t *headers;
    size_t header_count;
    const void *body;
    size_t body_size;
    const kc_http_field_t *trailers;
    size_t trailer_count;
    int chunked;
    size_t chunk_size;
} kc_http_request_t;

typedef struct {
    const char *version;
    int status;
    const char *reason;
    const kc_http_field_t *headers;
    size_t header_count;
    const void *body;
    size_t body_size;
    const kc_http_field_t *trailers;
    size_t trailer_count;
    int chunked;
    size_t chunk_size;
} kc_http_response_t;

typedef struct {
    const char *version;
    const char *method;
    const char *target;
    const kc_http_field_t *headers;
    size_t header_count;
    const void *body;
    size_t body_size;
    const kc_http_field_t *trailers;
    size_t trailer_count;
    int chunked;
    const size_t *chunk_size;
} kc_http_request_build_t;

typedef struct {
    const char *version;
    const int *status;
    const char *reason;
    const kc_http_field_t *headers;
    size_t header_count;
    const void *body;
    size_t body_size;
    const kc_http_field_t *trailers;
    size_t trailer_count;
    int chunked;
    const size_t *chunk_size;
} kc_http_response_build_t;

typedef void (*kc_http_request_fn)(
    const kc_http_request_build_t *request,
    void *userdata
);

typedef void (*kc_http_response_fn)(
    const kc_http_response_build_t *response,
    void *userdata
);

typedef void (*kc_http_error_fn)(
    int status,
    void *userdata
);

/**
 * Create an incremental HTTP parser for one byte stream.
 *
 * Parsed requests and responses are borrowed and remain valid only for the
 * duration of their callback. One write may emit zero, one, or multiple
 * complete messages.
 * @return Function result.
 */
int kc_http_parser_open(
    kc_http_parser_t **out,
    kc_http_request_fn request_handler,
    kc_http_response_fn response_handler,
    kc_http_error_fn error_handler,
    void *userdata
);

/**
 * Feed bytes from the parser's stream.
 * @return Function result.
 */
int kc_http_parser_write(
    kc_http_parser_t *parser,
    const void *data,
    size_t data_size
);

/**
 * Finalize and release a parser.
 *
 * If buffered bytes form an incomplete message, the error callback is invoked
 * with KC_HTTP_EPARSE before the parser is released.
 * @return None.
 */
void kc_http_parser_close(kc_http_parser_t *parser);

/**
 * Build one HTTP request into newly allocated wire bytes.
 *
 * NULL method, target, and version select GET, /, and HTTP/1.1 respectively.
 * NULL chunk_size selects the internal chunk-size default. A non-NULL
 * chunk_size is explicit and must be greater than zero.
 * The caller releases the returned buffer with kc_http_free().
 * @return Function result.
 */
int kc_http_request(
    const kc_http_request_t *request,
    void **out_data,
    size_t *out_size
);

/**
 * Build one HTTP response into newly allocated wire bytes.
 *
 * NULL status selects 200. NULL version selects HTTP/1.1 and NULL reason
 * derives the standard reason phrase. NULL chunk_size selects the internal
 * chunk-size default. Non-NULL status and chunk_size values are explicit.
 * The caller releases the returned buffer with kc_http_free().
 * @return Function result.
 */
int kc_http_response(
    const kc_http_response_t *response,
    void **out_data,
    size_t *out_size
);

/**
 * Release memory returned by kc_http_request() or kc_http_response().
 * @return None.
 */
void kc_http_free(void *ptr);

/**
 * Return a static string for a public status code.
 * @return Static error string.
 */
const char *kc_http_strerror(int status);

/**
 * Return the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_http_version(void);

#ifdef __cplusplus
}
#endif

#endif
