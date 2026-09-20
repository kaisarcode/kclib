/**
 * libmmap.c - Readonly memory mapping.
 * Summary: Core implementation for the mmap library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmmap.h"

#include <stddef.h>
#include <stdlib.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
#ifndef _WIN32
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

struct kc_mmap {
    void *data;
    size_t size;
    int fd;
    void *priv1;
    void *priv2;
};

/**
 * Initialize a new mmap context and map a file.
 * @param out Output pointer for the new context.
 * @param path File path.
 * @return KC_MMAP_OK on success, or KC_MMAP_ERROR on failure.
 */
int kc_mmap_open(kc_mmap_t **out, const char *path) {
    if (!out) return KC_MMAP_ERROR;
    *out = NULL;
    if (!path) return KC_MMAP_ERROR;

    kc_mmap_t *map = calloc(1, sizeof(kc_mmap_t));
    if (!map) return KC_MMAP_ERROR;
    map->fd = -1;

#ifdef _WIN32
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(map);
        return KC_MMAP_ERROR;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        free(map);
        return KC_MMAP_ERROR;
    }

    if (size.QuadPart == 0) {
        CloseHandle(hFile);
        map->size = 0;
        map->data = NULL;
        *out = map;
        return KC_MMAP_OK;
    }

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        CloseHandle(hFile);
        free(map);
        return KC_MMAP_ERROR;
    }

    void *data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!data) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        free(map);
        return KC_MMAP_ERROR;
    }

    map->priv1 = hFile;
    map->priv2 = hMap;
    map->data = data;
    map->size = (size_t)size.QuadPart;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        free(map);
        return KC_MMAP_ERROR;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        free(map);
        return KC_MMAP_ERROR;
    }

    if (st.st_size == 0) {
        close(fd);
        map->size = 0;
        map->data = NULL;
        *out = map;
        return KC_MMAP_OK;
    }

    void *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        free(map);
        return KC_MMAP_ERROR;
    }

    map->fd = fd;
    map->data = data;
    map->size = st.st_size;
#endif

    *out = map;
    return KC_MMAP_OK;
}

/**
 * Get pointer to mapped data.
 * @param map Map context pointer.
 * @return Pointer to data or NULL.
 */
const void *kc_mmap_data(const kc_mmap_t *map) {
    if (!map) return NULL;
    return map->data;
}

/**
 * Get mapped data size.
 * @param map Map context pointer.
 * @return Size in bytes.
 */
size_t kc_mmap_size(const kc_mmap_t *map) {
    if (!map) return 0;
    return map->size;
}

/**
 * Release a mmap context.
 * @param map Map context pointer.
 * @return None.
 */
void kc_mmap_close(kc_mmap_t *map) {
    if (!map) return;

#ifdef _WIN32
    if (map->data) {
        UnmapViewOfFile(map->data);
    }
    if (map->priv2) {
        CloseHandle((HANDLE)map->priv2);
    }
    if (map->priv1) {
        CloseHandle((HANDLE)map->priv1);
    }
#else
    if (map->data && map->data != MAP_FAILED) {
        munmap(map->data, map->size);
    }
    if (map->fd >= 0) {
        close(map->fd);
    }
#endif

    map->data = NULL;
    map->size = 0;
    map->fd = -1;
#ifdef _WIN32
    map->priv1 = NULL;
    map->priv2 = NULL;
#endif

    free(map);
}

#ifndef KC_MMAP_BUILD_VERSION
#define KC_MMAP_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_mmap_version(void) {
    return (uint64_t)KC_MMAP_BUILD_VERSION;
}
