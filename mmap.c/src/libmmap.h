/**
 * mmap.h - Readonly memory mapping.
 * Summary: Public API for the mmap library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MMAP_H
#define KC_MMAP_H

#include <stddef.h>
#include <stdint.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mmap kc_mmap_t;

#define KC_MMAP_OK      0
#define KC_MMAP_ERROR  -1
#define KC_MMAP_ESTOP  -3

typedef struct {
    int reserved;
} kc_mmap_options_t;

/**
 * Initialize default mmap options.
 * @return Default-initialized options.
 */
kc_mmap_options_t kc_mmap_options_default(void);

/**
 * Load mmap options from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_mmap_options_load_env(kc_mmap_options_t *opts);

/**
 * Free mmap options.
 * @param opts Options to free.
 * @return None.
 */
void kc_mmap_options_free(kc_mmap_options_t *opts);

/**
 * Initialize a new mmap context and map a file.
 * @param out Output pointer for the new context.
 * @param path File path.
 * @param opts Options, or NULL for defaults.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_open(kc_mmap_t **out, const char *path, const kc_mmap_options_t *opts);

/**
 * Get pointer to mapped data.
 * @param map Map context pointer.
 * @return Pointer to data or NULL.
 */
const void *kc_mmap_data(const kc_mmap_t *map);

/**
 * Get mapped data size.
 * @param map Map context pointer.
 * @return Size in bytes.
 */
size_t kc_mmap_size(const kc_mmap_t *map);

/**
 * Release a mmap context.
 * @param mf Map context pointer.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_close(kc_mmap_t *mf);

/**
 * Request stop for a specific mmap context.
 * @param mf Map context.
 * @return KC_MMAP_OK on success, KC_MMAP_ERROR on failure.
 */
int kc_mmap_stop(kc_mmap_t *mf);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mmap_version(void);

#ifdef __cplusplus
}
#endif

#endif
