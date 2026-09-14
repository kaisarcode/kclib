/**
 * http.h - HTTP protocol parser and builder.
 * Summary: Public API for parsing and building HTTP messages.
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

typedef struct kc_http kc_http_t;

#define KC_HTTP_OK    0
#define KC_HTTP_ERROR -1
#define KC_HTTP_ESTOP (-3)

#define KC_HTTP_OP_PARSE          1
#define KC_HTTP_OP_BUILD_REQUEST  2
#define KC_HTTP_OP_BUILD_RESPONSE 3

/**
 * HTTP options.
 * @param method HTTP method.
 * @param target Request target.
 * @param version HTTP version.
 * @param chunked Use chunked encoding.
 * @param chunk_size Chunk size for chunked encoding.
 */
typedef struct kc_http_options {
    char *method;
    char *target;
    char *version;
    int chunked;
    size_t chunk_size;
} kc_http_options_t;

/**
 * Initialize a new http context.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_open(kc_http_t **out, const kc_http_options_t *opts);

/**
 * Execute the configured http operation.
 * @param ctx Context pointer.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_exec(kc_http_t *ctx);

/**
 * Set the input buffer for the next exec operation.
 * @param ctx Context pointer.
 * @param data Input buffer pointer.
 * @param len Input buffer size in bytes.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_set_input(kc_http_t *ctx, const void *data, size_t len);

/**
 * Get the output buffer from the last exec operation.
 * @param ctx Context pointer.
 * @param out Receives pointer to output buffer (caller does not own).
 * @param out_len Receives output buffer size in bytes.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_get_output(kc_http_t *ctx, unsigned char **out, size_t *out_len);

/**
 * Release a http context and all owned memory.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_http_close(kc_http_t *ctx);

/**
 * Request a context to stop at the next opportunity.
 * @param ctx HTTP context.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if ctx is NULL.
 */
int kc_http_stop(kc_http_t *ctx);

/**
 * Set the operation to perform.
 * @param ctx Context pointer.
 * @param op KC_HTTP_OP_PARSE, BUILD_REQUEST, or BUILD_RESPONSE.
 * @return None.
 */
void kc_http_set_op(kc_http_t *ctx, int op);

/**
 * Enable multi-message parse mode.
 * @param ctx Context pointer.
 * @param all Non-zero to parse all messages until EOF.
 * @return None.
 */
void kc_http_set_all(kc_http_t *ctx, int all);

/**
 * Set the HTTP method for build request.
 * @param ctx Context pointer.
 * @param method Method string (e.g. "GET", "POST").
 * @return None.
 */
void kc_http_set_method(kc_http_t *ctx, const char *method);

/**
 * Set the request target for build request.
 * @param ctx Context pointer.
 * @param target Target string (e.g. "/api?a=1").
 * @return None.
 */
void kc_http_set_target(kc_http_t *ctx, const char *target);

/**
 * Set the HTTP version for build operations.
 * @param ctx Context pointer.
 * @param version Version string (e.g. "1.1").
 * @return None.
 */
void kc_http_set_version(kc_http_t *ctx, const char *version);

/**
 * Set the response status code for build response.
 * @param ctx Context pointer.
 * @param status HTTP status code.
 * @return None.
 */
void kc_http_set_status(kc_http_t *ctx, int status);

/**
 * Set the response reason phrase for build response.
 * @param ctx Context pointer.
 * @param reason Reason string (e.g. "OK").
 * @return None.
 */
void kc_http_set_reason(kc_http_t *ctx, const char *reason);

/**
 * Enable chunked transfer encoding for build operations.
 * @param ctx Context pointer.
 * @param chunked Non-zero to use chunked encoding.
 * @return None.
 */
void kc_http_set_chunked(kc_http_t *ctx, int chunked);

/**
 * Set the chunk size for chunked build operations.
 * @param ctx Context pointer.
 * @param size Chunk size in bytes.
 * @return None.
 */
void kc_http_set_chunk_size(kc_http_t *ctx, size_t size);

/**
 * Add a header for build operations.
 * @param ctx Context pointer.
 * @param header Header string in "name: value" format.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if limit exceeded.
 */
int kc_http_add_header(kc_http_t *ctx, const char *header);

/**
 * Add a trailer for chunked build operations.
 * @param ctx Context pointer.
 * @param trailer Trailer string in "name: value" format.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR if limit exceeded.
 */
int kc_http_add_trailer(kc_http_t *ctx, const char *trailer);

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_http_options_t kc_http_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_http_options_load_env(kc_http_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_http_options_free(kc_http_options_t *opts);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_http_version(void);

#ifdef __cplusplus
}
#endif

#endif
