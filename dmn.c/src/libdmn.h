/**
 * dmn.h - IPC Daemon Manager
 * Summary: Public API for the dmn library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_DMN_H
#define KC_DMN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_dmn kc_dmn_t;
typedef struct kc_dmn_options kc_dmn_options_t;
typedef struct kc_dmn_conn kc_dmn_conn_t;

#define KC_DMN_OK      0
#define KC_DMN_ERROR  -1
#define KC_DMN_EOF     1

/**
 * Callback type for kc_dmn_list.
 *
 * The callback is invoked synchronously during kc_dmn_list. The callback
 * pointer and userdata pointer are not retained. The key and sock strings
 * are borrowed only during callback invocation; callers must copy key or
 * sock for a longer lifetime.
 *
 * @param key Borrowed daemon key name.
 * @param sock Borrowed socket or pipe path.
 * @param userdata Opaque pointer.
 */
typedef void (*kc_dmn_list_cb)(
    const char *key,
    const char *sock,
    void *userdata
);

/**
 * Allocate options initialized with default values.
 *
 * The returned options object is owned by the caller and must be released
 * with kc_dmn_options_free().
 *
 * @return Owned options object, or NULL on allocation failure.
 */
kc_dmn_options_t *kc_dmn_options_default(void);

/**
 * Set an option.
 *
 * The options object remains owned by the caller. Supported keys and values
 * are copied by the library; passing NULL as the value for "dir" restores
 * automatic runtime-directory resolution.
 *
 * @param opts Options object.
 * @param key Option key; currently "dir".
 * @param value Option value, or NULL to reset "dir".
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_options_set(
    kc_dmn_options_t *opts,
    const char *key,
    const char *value
);

/**
 * Free an options object.
 *
 * @param opts Options object, or NULL.
 * @return None.
 */
void kc_dmn_options_free(kc_dmn_options_t *opts);

/**
 * Initialize a new dmn context.
 *
 * The context copies the options it needs; the caller retains ownership of
 * opts and may free it after this call returns.
 *
 * @param out Pointer to receive the context pointer.
 * @param opts Optional options object, borrowed for the duration of the call.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_open(
    kc_dmn_t **out,
    const kc_dmn_options_t *opts
);

/**
 * Release a dmn context.
 *
 * @param ctx Context pointer.
 * @return None.
 */
void kc_dmn_close(kc_dmn_t *ctx);

/**
 * Return the resolved runtime directory for a dmn context.
 *
 * The returned string is borrowed from ctx and remains valid until ctx is
 * closed or otherwise changed by the library.
 *
 * @param ctx Context pointer.
 * @return Borrowed runtime directory path, or NULL on invalid input.
 */
const char *kc_dmn_path(const kc_dmn_t *ctx);

/**
 * Register or replace a named daemon command.
 *
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param cmd Shell command string for the resident backend.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_update(
    kc_dmn_t *ctx,
    const char *key,
    const char *cmd
);

/**
 * Delete a named daemon.
 *
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_delete(
    kc_dmn_t *ctx,
    const char *key
);

/**
 * List registered daemons.
 *
 * The callback is synchronous. The callback pointer and userdata pointer are
 * not retained. The key and sock arguments are borrowed only during callback
 * invocation; callers must copy key or sock for a longer lifetime.
 *
 * @param ctx Context pointer.
 * @param key Optional daemon key name, or NULL for all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_list(
    kc_dmn_t *ctx,
    const char *key,
    kc_dmn_list_cb cb,
    void *userdata
);

/**
 * Connect to a named daemon.
 *
 * On success, the connection object is owned by the caller and must be
 * released with kc_dmn_disconnect().
 *
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param out Pointer to receive the owned connection object.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_connect(
    kc_dmn_t *ctx,
    const char *key,
    kc_dmn_conn_t **out
);

/**
 * Send data to a connected daemon.
 *
 * @param conn Owned connection object.
 * @param data Data to send.
 * @param data_size Data size.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_send(
    kc_dmn_conn_t *conn,
    const void *data,
    size_t data_size
);

/**
 * Receive owned binary data from a connected daemon.
 *
 * On success, *out_data is owned by the caller and must be released with
 * kc_dmn_free(). The returned size is stored in *out_size.
 *
 * @param conn Owned connection object.
 * @param max_size Maximum number of bytes to receive.
 * @param out_data Pointer to receive owned data.
 * @param out_size Pointer to receive the data size.
 * @return KC_DMN_OK, KC_DMN_EOF, or KC_DMN_ERROR.
 */
int kc_dmn_recv(
    kc_dmn_conn_t *conn,
    size_t max_size,
    void **out_data,
    size_t *out_size
);

/**
 * Disconnect and release a connection object.
 *
 * @param conn Connection object, or NULL.
 * @return None.
 */
void kc_dmn_disconnect(kc_dmn_conn_t *conn);

/**
 * Free memory returned by a dmn API.
 *
 * @param ptr API-owned pointer, or NULL.
 * @return None.
 */
void kc_dmn_free(void *ptr);

/**
 * Send a signal to a managed daemon process.
 *
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param signo Signal number (POSIX) or ignored on Windows.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_signal(
    kc_dmn_t *ctx,
    const char *key,
    int signo
);

/**
 * Return the last error message for a dmn context.
 *
 * The returned string is borrowed from ctx and remains valid until ctx is
 * closed or the library replaces the error.
 *
 * @param ctx Context pointer.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_dmn_get_error(const kc_dmn_t *ctx);

/**
 * Return the build version generated at compile time.
 *
 * @return Unix timestamp for the current build.
 */
uint64_t kc_dmn_version(void);

#ifdef __cplusplus
}
#endif

#endif
