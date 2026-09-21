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

/**
 * Initialize a new HTTP context.
 * @param out Receives the new context.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_open(kc_http_t **out);

/**
 * Release an HTTP context and all owned configuration.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_http_close(kc_http_t *ctx);

/**
 * Set the HTTP method for request builds.
 * @param ctx Context pointer.
 * @param method Method string.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_method(kc_http_t *ctx, const char *method);

/**
 * Set the request target for request builds.
 * @param ctx Context pointer.
 * @param target Request target.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_target(kc_http_t *ctx, const char *target);

/**
 * Set the HTTP version for builds.
 * @param ctx Context pointer.
 * @param version Version string.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_version(kc_http_t *ctx, const char *version);

/**
 * Set the response status code for response builds.
 * @param ctx Context pointer.
 * @param status HTTP status code.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_status(kc_http_t *ctx, int status);

/**
 * Set the response reason phrase for response builds.
 * @param ctx Context pointer.
 * @param reason Reason phrase.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_reason(kc_http_t *ctx, const char *reason);

/**
 * Set whether builds use chunked transfer encoding.
 * @param ctx Context pointer.
 * @param chunked Non-zero to use chunked encoding.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_chunked(kc_http_t *ctx, int chunked);

/**
 * Set the chunk size for chunked builds.
 * @param ctx Context pointer.
 * @param chunk_size Chunk size in bytes.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_set_chunk_size(kc_http_t *ctx, size_t chunk_size);

/**
 * Add a header for builds.
 * @param ctx Context pointer.
 * @param name Header name.
 * @param value Header value.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_add_header(kc_http_t *ctx, const char *name, const char *value);

/**
 * Add a trailer for chunked builds.
 * @param ctx Context pointer.
 * @param name Trailer name.
 * @param value Trailer value.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_add_trailer(kc_http_t *ctx, const char *name, const char *value);

/**
 * Parse HTTP wire data into caller-owned normalized output.
 * @param ctx Context pointer.
 * @param data Input wire bytes.
 * @param data_size Input size in bytes.
 * @param all Non-zero to parse all complete messages.
 * @param out_data Receives allocated normalized data, or NULL on failure.
 * @param out_size Receives output size, or zero on failure.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_parse(kc_http_t *ctx, const void *data, size_t data_size, int all,
void **out_data, size_t *out_size);

/**
 * Build a request into caller-owned wire output.
 * @param ctx Context pointer.
 * @param body Body bytes.
 * @param body_size Body size in bytes.
 * @param out_data Receives allocated wire data, or NULL on failure.
 * @param out_size Receives output size, or zero on failure.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_build_request(kc_http_t *ctx, const void *body, size_t body_size,
void **out_data, size_t *out_size);

/**
 * Build a response into caller-owned wire output.
 * @param ctx Context pointer.
 * @param body Body bytes.
 * @param body_size Body size in bytes.
 * @param out_data Receives allocated wire data, or NULL on failure.
 * @param out_size Receives output size, or zero on failure.
 * @return KC_HTTP_OK on success, or KC_HTTP_ERROR on failure.
 */
int kc_http_build_response(kc_http_t *ctx, const void *body, size_t body_size,
void **out_data, size_t *out_size);

/**
 * Free memory returned through an output parameter.
 * @param ptr Output pointer to release.
 * @return None.
 */
void kc_http_free(void *ptr);

/**
 * Get the most recent contextual error message.
 * @param ctx Context pointer.
 * @return Error message, or NULL when no error is available.
 */
const char *kc_http_get_error(const kc_http_t *ctx);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_http_version(void);

#ifdef __cplusplus
}
#endif

#endif
