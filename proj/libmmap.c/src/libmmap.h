/**
 * mmap.h - Persistent binary value.
 * Summary: Public API for one file-backed mmap value.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MMAP_H
#define KC_MMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mmap kc_mmap_t;

#define KC_MMAP_OK         0
#define KC_MMAP_NOT_FOUND  1
#define KC_MMAP_ERROR     -1

/**
 * Opens one file-backed value.
 *
 * A missing file is not an error: the instance opens with no current value.
 *
 * @param out Output pointer for the new instance.
 * @param path Backing file path copied by the instance.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_open(kc_mmap_t **out, const char *path);

/**
 * Reads the current in-memory value.
 *
 * The returned pointer is borrowed from the instance and remains valid until
 * kc_mmap_set(), kc_mmap_del(), or kc_mmap_close().
 *
 * @param map Instance pointer.
 * @param out_data Receives the borrowed value pointer on KC_MMAP_OK.
 * @param out_size Receives the value size on KC_MMAP_OK.
 * @return KC_MMAP_OK when a value exists, KC_MMAP_NOT_FOUND when the current
 *         value is null, or KC_MMAP_ERROR when the instance is invalid.
 */
int kc_mmap_get(
    const kc_mmap_t *map,
    const void **out_data,
    size_t *out_size
);

/**
 * Replaces the current in-memory value without writing the backing file.
 *
 * NULL data with size zero sets the logical value to null. A non-NULL data
 * pointer with size zero represents a real zero-byte value such as "".
 *
 * @param map Instance pointer.
 * @param data Borrowed input bytes, or NULL to set null when size is zero.
 * @param size Input byte length.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_set(kc_mmap_t *map, const void *data, size_t size);

/**
 * Persists the current value to the backing file.
 *
 * A zero-byte value creates or truncates the file to zero bytes. Saving null
 * deletes the backing file and invalidates the instance.
 *
 * @param map Instance pointer.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_save(kc_mmap_t *map);

/**
 * Deletes the backing file and invalidates the instance.
 *
 * This is shorthand for setting null and saving it. Deleting an already
 * missing backing file still succeeds for a valid instance. After this call,
 * get/set/save/del return KC_MMAP_ERROR.
 *
 * @param map Instance pointer.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_del(kc_mmap_t *map);

/**
 * Releases an instance. NULL is safe.
 * @param map Instance pointer.
 * @return None.
 */
void kc_mmap_close(kc_mmap_t *map);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mmap_version(void);

#ifdef __cplusplus
}
#endif

#endif
