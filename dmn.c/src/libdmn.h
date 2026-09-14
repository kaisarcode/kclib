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

#define KC_DMN_OK      0
#define KC_DMN_ERROR  -1
#define KC_DMN_ESTOP  -3

/**
 * Daemon Manager options.
 * @param dir Optional runtime directory override.
 */
typedef struct kc_dmn_options {
    char *dir;
} kc_dmn_options_t;

/**
 * Callback type for kc_dmn_list.
 * @param key Daemon key name.
 * @param sock Socket path.
 * @param userdata Opaque pointer.
 */
typedef void (*kc_dmn_list_cb)(const char *key, const char *sock, void *userdata);

/**
 * Initialize a new dmn context.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_open(kc_dmn_t **out, const kc_dmn_options_t *opts);

/**
 * Release a dmn context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_dmn_close(kc_dmn_t *ctx);

/**
 * Request stop for a specific dmn context.
 * @param ctx Context pointer.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_stop(kc_dmn_t *ctx);

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_dmn_options_t kc_dmn_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_dmn_options_load_env(kc_dmn_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_dmn_options_free(kc_dmn_options_t *opts);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_dmn_version(void);

/**
 * Return the resolved runtime directory for a dmn context.
 * @param ctx Context pointer.
 * @return Runtime directory path, or NULL on invalid input.
 */
const char *kc_dmn_path(kc_dmn_t *ctx);

/**
 * Register or replace a named daemon command.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param cmd Shell command string for the resident backend.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_update(kc_dmn_t *ctx, const char *key, const char *cmd);

/**
 * Delete a named daemon.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_delete(kc_dmn_t *ctx, const char *key);

/**
 * List registered daemons.
 * Calls cb(key, sock, userdata) per entry.
 * @param ctx Context pointer.
 * @param key Optional daemon key name, or NULL for all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_list(kc_dmn_t *ctx, const char *key, kc_dmn_list_cb cb, void *userdata);

/**
 * Relay stdin/stdout through a named daemon.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_relay(kc_dmn_t *ctx, const char *key);

/**
 * Connect to a named daemon for relay.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param out_handle Pointer to receive the handle.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_connect(kc_dmn_t *ctx, const char *key, int *out_handle);

/**
 * Send data to a connected daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @param data Data to send.
 * @param len Data length.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_send(int handle, const void *data, size_t len);

/**
 * Receive data from a connected daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @param buf Output buffer.
 * @param cap Output buffer capacity.
 * @return Number of bytes received, or -1 on failure.
 */
int kc_dmn_recv(int handle, void *buf, size_t cap);

/**
 * Disconnect from a daemon.
 * @param handle Connection handle from kc_dmn_connect.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_disconnect(int handle);

/**
 * Send a signal to a managed daemon process.
 * @param ctx Context pointer.
 * @param key Daemon key name.
 * @param signo Signal number (POSIX) or ignored on Windows.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_signal(kc_dmn_t *ctx, const char *key, int signo);

#ifdef _WIN32
/**
 * Serve one Windows named pipe daemon process.
 * @param pipename Named Pipe path.
 * @param cmd Command string.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_serve(const char *pipename, const char *cmd);
#endif

#ifdef __cplusplus
}
#endif

#endif
