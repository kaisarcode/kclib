/**
 * libdmn.h - Local daemon manager
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
typedef struct kc_dmn_stream kc_dmn_stream_t;

typedef struct {
    const char *cmd;
    const char *dir;
    const void *eot;
    size_t eot_size;
} kc_dmn_options_t;

typedef struct {
    const char *name;
    const char *endpoint;
} kc_dmn_entry_t;

typedef void (*kc_dmn_handler_t)(
    const void *data,
    size_t size,
    void *userdata
);

#define KC_DMN_OK          0
#define KC_DMN_NOT_FOUND   1
#define KC_DMN_EOF         2
#define KC_DMN_ERROR      -1

/**
 * Create or replace a named daemon.
 * options->cmd is required. NULL dir uses the default runtime directory.
 * NULL eot with eot_size 0 selects the default EOT byte 4.
 */
int kc_dmn_create(
    const char *name,
    const kc_dmn_options_t *options
);

/** Open a local handle bound to one daemon name. */
int kc_dmn_open(
    kc_dmn_t **out,
    const char *name
);

/**
 * List daemons. NULL dir uses the default runtime directory.
 * Returned entries share one allocation released with kc_dmn_free().
 */
int kc_dmn_list(
    const char *dir,
    kc_dmn_entry_t **out_entries,
    size_t *out_count
);

/** Delete a named daemon. NULL dir uses the default runtime directory. */
int kc_dmn_delete(
    const char *name,
    const char *dir
);

/** Replace the command of an existing daemon. */
int kc_dmn_set_cmd(
    kc_dmn_t *dmn,
    const char *cmd
);

/** Return the borrowed configured command. */
const char *kc_dmn_get_cmd(
    const kc_dmn_t *dmn
);

/** Change the runtime directory targeted by this handle. */
int kc_dmn_set_dir(
    kc_dmn_t *dmn,
    const char *dir
);

/** Return the borrowed runtime directory targeted by this handle. */
const char *kc_dmn_get_dir(
    const kc_dmn_t *dmn
);

/** Replace the daemon EOT marker; NULL,0 restores byte 4. */
int kc_dmn_set_eot(
    kc_dmn_t *dmn,
    const void *eot,
    size_t eot_size
);

/** Return the borrowed EOT bytes and optionally their size. */
const void *kc_dmn_get_eot(
    const kc_dmn_t *dmn,
    size_t *out_size
);

/** Register or clear a handler for the "data" event. */
int kc_dmn_on(
    kc_dmn_t *dmn,
    const char *event,
    kc_dmn_handler_t handler,
    void *userdata
);

/**
 * Perform one complete data exchange using the configured EOT.
 * The returned response excludes EOT and is released with kc_dmn_free().
 */
int kc_dmn_send_data(
    kc_dmn_t *dmn,
    const void *data,
    size_t data_size,
    void **out_data,
    size_t *out_size
);

/** Send a platform signal to the opened daemon. */
int kc_dmn_send_signal(
    kc_dmn_t *dmn,
    int signal
);

/** Open a raw byte stream to the opened daemon. */
int kc_dmn_stream(
    kc_dmn_t *dmn,
    kc_dmn_stream_t **out
);

/** Write raw bytes without adding EOT. */
int kc_dmn_stream_write(
    kc_dmn_stream_t *stream,
    const void *data,
    size_t size
);

/** Read raw bytes; returns KC_DMN_EOF when the stream ends. */
int kc_dmn_stream_read(
    kc_dmn_stream_t *stream,
    void *data,
    size_t capacity,
    size_t *out_size
);

/** Close and release a raw stream. */
void kc_dmn_stream_close(
    kc_dmn_stream_t *stream
);

/** Release memory returned by dmn. */
void kc_dmn_free(void *ptr);
/** Close and release a local daemon handle without deleting the daemon. */
void kc_dmn_close(kc_dmn_t *dmn);
/** Return the generated build version. */
uint64_t kc_dmn_version(void);

#ifdef __cplusplus
}
#endif

#endif
