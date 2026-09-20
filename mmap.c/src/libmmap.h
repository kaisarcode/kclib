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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_mmap kc_mmap_t;

#define KC_MMAP_OK      0
#define KC_MMAP_ERROR  -1

/**
 * Initialize a new mmap context and map a file.
 * @param out Output pointer for the new context.
 * @param path File path.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_open(kc_mmap_t **out, const char *path);

/**
 * Get pointer to mapped data.
 * @param map Map context pointer.
 * @return Context-owned, borrowed read-only data; do not free or write through
 *         this pointer. It is valid only while this exact context remains open
 *         and becomes invalid immediately after kc_mmap_close(). A successfully
 *         opened empty file returns NULL. kc_mmap_size() is authoritative; this
 *         function does not copy data.
 */
const void *kc_mmap_data(const kc_mmap_t *map);

/**
 * Get mapped data size.
 * @param map Map context pointer.
 * @return Byte length of the borrowed mapping.
 */
size_t kc_mmap_size(const kc_mmap_t *map);

/**
 * Release a mmap context.
 * @param map Map context pointer; NULL is safe.
 * @return None. This call invalidates map and all borrowed data pointers.
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
