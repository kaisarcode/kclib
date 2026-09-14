/**
 * netl.h - Network listener.
 * Summary: Public API for registering and running named network listeners.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_NETL_H
#define KC_NETL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_netl kc_netl_t;

#define KC_NETL_OK     0
#define KC_NETL_ERROR -1
#define KC_NETL_ENET  -2
#define KC_NETL_ESTOP -3

typedef struct {
    int reserved;
} kc_netl_options_t;

typedef void (*kc_netl_list_cb)(const char *key, const char *addrport, void *userdata);

typedef int (*kc_netl_data_fn)(const char *data, size_t len, void *user);

#define KC_NETL_TCP 1
#define KC_NETL_UDP 2

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_netl_options_t kc_netl_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_netl_options_load_env(kc_netl_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_netl_options_free(kc_netl_options_t *opts);

/**
 * Request stop for a specific netl context.
 * @param ctx Context pointer.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_request_stop(kc_netl_t *ctx);

/**
 * Opens a netl context, resolving the metadata directory.
 * @param out Pointer to receive the context pointer.
 * @param opts Options.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_open(kc_netl_t **out, const kc_netl_options_t *opts);

/**
 * Releases a netl context.
 * @param ctx Context to close and free.
 * @return KC_NETL_OK on success, or KC_NETL_ERROR on failure.
 */
int kc_netl_close(kc_netl_t *ctx);

/**
 * Returns the resolved metadata directory path.
 * @param ctx Open context.
 * @return Pointer to the internal path string.
 */
const char *kc_netl_path(kc_netl_t *ctx);

/**
 * Registers or replaces a named network listener.
 * @param ctx   Open context.
 * @param key   Registration name.
 * @param host  Bind host or IP.
 * @param port  Bind port.
 * @param proto KC_NETL_TCP or KC_NETL_UDP.
 * @param cmd   Ignored (kept for compatibility).
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_update(kc_netl_t *ctx, const char *key, const char *host, unsigned short port, int proto, const char *cmd);

/**
 * Starts a registered listener. Blocking on success, never returns.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_ERROR on failure.
 */
int kc_netl_exec(kc_netl_t *ctx, const char *key);

/**
 * Lists all registrations or one named registration.
 * Calls cb(key, addrport, userdata) per entry.
 * @param ctx Open context.
 * @param key Registration name, or NULL to list all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_list(kc_netl_t *ctx, const char *key, kc_netl_list_cb cb, void *userdata);

/**
 * Removes a named registration.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_delete(kc_netl_t *ctx, const char *key);

/**
 * Stops a registered listener by signaling its stored PID.
 * @param ctx Open context.
 * @param key Registration name.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_stop(kc_netl_t *ctx, const char *key);

/**
 * Updates the stored PID for a registration.
 * @param ctx Open context.
 * @param key Registration name.
 * @param pid Process ID.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_set_pid(kc_netl_t *ctx, const char *key, long pid);

/**
 * Starts a non-blocking listener for a registered key.
 * When data arrives, the callback is invoked with the data.
 * @param ctx Open context.
 * @param key Registration name.
 * @param callback Function called when data arrives.
 * @param userdata Opaque pointer passed to callback.
 * @return Non-negative handle on success, or negative error code.
 */
int kc_netl_listen(kc_netl_t *ctx, const char *key, kc_netl_data_fn callback, void *userdata);

/**
 * Sends data through an active listener socket.
 * @param ctx Open context.
 * @param handle Listener handle from kc_netl_listen.
 * @param buf Data buffer.
 * @param len Number of bytes to send.
 * @return Number of bytes sent, or negative error code.
 */
int kc_netl_send(kc_netl_t *ctx, int handle, const char *buf, size_t len);

/**
 * Closes an active listener and releases its resources.
 * @param ctx Open context.
 * @param handle Listener handle from kc_netl_listen.
 * @return KC_NETL_OK on success, KC_NETL_ERROR on failure.
 */
int kc_netl_close_listener(kc_netl_t *ctx, int handle);

/**
 * Binds to an address and serves incoming connections in a blocking loop.
 * TCP: forks one handler per connection with stdin and stdout on the socket.
 * UDP: forks one handler per datagram with stdin piped from the packet.
 * Never returns on success.
 * @param host  Bind host or IP. NULL or empty string binds all interfaces.
 * @param port  Bind port.
 * @param proto KC_NETL_TCP or KC_NETL_UDP.
 * @param cmd   Shell command executed per connection or datagram.
 * @return Negative error code on setup failure.
 */
int kc_netl_serve(const char *host, unsigned short port, int proto, const char *cmd);

/**
 * Returns a static string for a netl error code.
 * @param code Error code.
 * @return Static string.
 */
const char *kc_netl_strerror(int code);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_netl_version(void);

#ifdef __cplusplus
}
#endif

#endif
