/**
 * libmmap.c - Persistent binary value.
 * Summary: Core implementation for one file-backed mmap value.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmmap.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

struct kc_mmap {
    char *path;
    void *data;
    size_t size;
    int has_value;
    int mapped;
    int dirty;
    int valid;
#ifdef _WIN32
    HANDLE file_handle;
    HANDLE map_handle;
#else
    int fd;
#endif
};

#ifndef KC_MMAP_BUILD_VERSION
#define KC_MMAP_BUILD_VERSION 0
#endif

/**
 * Duplicates one null-terminated string.
 * @param text Source string.
 * @return Owned copy, or NULL on allocation failure.
 */
static char *kc_mmap_dup(const char *text) {
    size_t len;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    len = strlen(text);
    copy = (char *)malloc(len + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, len + 1U);
    return copy;
}

/**
 * Releases the current data representation without changing logical state.
 * @param map Instance pointer.
 * @return None.
 */
static void kc_mmap_release_data(kc_mmap_t *map) {
    if (map == NULL) {
        return;
    }

#ifdef _WIN32
    if (map->mapped) {
        if (map->data != NULL) {
            UnmapViewOfFile(map->data);
        }
        if (map->map_handle != NULL) {
            CloseHandle(map->map_handle);
        }
        if (map->file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(map->file_handle);
        }
        map->map_handle = NULL;
        map->file_handle = INVALID_HANDLE_VALUE;
    } else {
        free(map->data);
    }
#else
    if (map->mapped) {
        if (map->data != NULL && map->size > 0U) {
            munmap(map->data, map->size);
        }
        if (map->fd >= 0) {
            close(map->fd);
        }
        map->fd = -1;
    } else {
        free(map->data);
    }
#endif

    map->data = NULL;
    map->size = 0U;
    map->mapped = 0;
}

/**
 * Initializes platform-specific resource fields.
 * @param map Instance pointer.
 * @return None.
 */
static void kc_mmap_init_native(kc_mmap_t *map) {
#ifdef _WIN32
    map->file_handle = INVALID_HANDLE_VALUE;
    map->map_handle = NULL;
#else
    map->fd = -1;
#endif
}

/**
 * Opens the backing file as the initial read-only mapped value.
 * @param map Instance pointer.
 * @return KC_MMAP_OK when opened or absent, otherwise KC_MMAP_ERROR.
 */
static int kc_mmap_load(kc_mmap_t *map) {
#ifdef _WIN32
    LARGE_INTEGER file_size;
    HANDLE file_handle;
    HANDLE map_handle;
    void *data;
    DWORD error;

    file_handle = CreateFileA(
        map->path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file_handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            map->has_value = 0;
            return KC_MMAP_OK;
        }
        return KC_MMAP_ERROR;
    }

    if (!GetFileSizeEx(file_handle, &file_size) || file_size.QuadPart < 0) {
        CloseHandle(file_handle);
        return KC_MMAP_ERROR;
    }

    map->has_value = 1;
    if (file_size.QuadPart == 0) {
        CloseHandle(file_handle);
        return KC_MMAP_OK;
    }

    if ((uint64_t)file_size.QuadPart > (uint64_t)SIZE_MAX) {
        CloseHandle(file_handle);
        return KC_MMAP_ERROR;
    }

    map_handle = CreateFileMappingA(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (map_handle == NULL) {
        CloseHandle(file_handle);
        return KC_MMAP_ERROR;
    }

    data = MapViewOfFile(map_handle, FILE_MAP_READ, 0, 0, 0);
    if (data == NULL) {
        CloseHandle(map_handle);
        CloseHandle(file_handle);
        return KC_MMAP_ERROR;
    }

    map->file_handle = file_handle;
    map->map_handle = map_handle;
    map->data = data;
    map->size = (size_t)file_size.QuadPart;
    map->mapped = 1;
#else
    struct stat st;
    void *data;
    int fd;

    fd = open(map->path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            map->has_value = 0;
            return KC_MMAP_OK;
        }
        return KC_MMAP_ERROR;
    }

    if (fstat(fd, &st) < 0 || st.st_size < 0) {
        close(fd);
        return KC_MMAP_ERROR;
    }

    map->has_value = 1;
    if (st.st_size == 0) {
        close(fd);
        return KC_MMAP_OK;
    }

    if ((uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
        close(fd);
        return KC_MMAP_ERROR;
    }

    data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return KC_MMAP_ERROR;
    }

    map->fd = fd;
    map->data = data;
    map->size = (size_t)st.st_size;
    map->mapped = 1;
#endif

    return KC_MMAP_OK;
}

int kc_mmap_open(kc_mmap_t **out, const char *path) {
    kc_mmap_t *map;

    if (out == NULL) {
        return KC_MMAP_ERROR;
    }
    *out = NULL;

    if (path == NULL || path[0] == '\0') {
        return KC_MMAP_ERROR;
    }

    map = (kc_mmap_t *)calloc(1U, sizeof(kc_mmap_t));
    if (map == NULL) {
        return KC_MMAP_ERROR;
    }

    kc_mmap_init_native(map);
    map->path = kc_mmap_dup(path);
    if (map->path == NULL) {
        free(map);
        return KC_MMAP_ERROR;
    }

    map->valid = 1;
    if (kc_mmap_load(map) != KC_MMAP_OK) {
        free(map->path);
        free(map);
        return KC_MMAP_ERROR;
    }

    *out = map;
    return KC_MMAP_OK;
}

int kc_mmap_get(
    const kc_mmap_t *map,
    const void **out_data,
    size_t *out_size
) {
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_size != NULL) {
        *out_size = 0U;
    }

    if (map == NULL || !map->valid || out_data == NULL || out_size == NULL) {
        return KC_MMAP_ERROR;
    }

    if (!map->has_value) {
        return KC_MMAP_NOT_FOUND;
    }

    *out_data = map->data;
    *out_size = map->size;
    return KC_MMAP_OK;
}

int kc_mmap_set(kc_mmap_t *map, const void *data, size_t size) {
    void *copy = NULL;

    if (map == NULL || !map->valid || (data == NULL && size > 0U)) {
        return KC_MMAP_ERROR;
    }

    if (data != NULL && size > 0U) {
        copy = malloc(size);
        if (copy == NULL) {
            return KC_MMAP_ERROR;
        }
        memcpy(copy, data, size);
    }

    kc_mmap_release_data(map);
    map->data = copy;
    map->size = size;
    map->has_value = data != NULL ? 1 : 0;
    map->dirty = 1;
    return KC_MMAP_OK;
}

int kc_mmap_save(kc_mmap_t *map) {
    FILE *file;

    if (map == NULL || !map->valid) {
        return KC_MMAP_ERROR;
    }

    if (!map->dirty) {
        return KC_MMAP_OK;
    }

    if (!map->has_value) {
        if (remove(map->path) != 0 && errno != ENOENT) {
            return KC_MMAP_ERROR;
        }
        map->dirty = 0;
        map->valid = 0;
        return KC_MMAP_OK;
    }

    file = fopen(map->path, "wb");
    if (file == NULL) {
        return KC_MMAP_ERROR;
    }

    if (map->size > 0U && fwrite(map->data, 1U, map->size, file) != map->size) {
        fclose(file);
        return KC_MMAP_ERROR;
    }

    if (fclose(file) != 0) {
        return KC_MMAP_ERROR;
    }

    map->dirty = 0;
    return KC_MMAP_OK;
}

int kc_mmap_del(kc_mmap_t *map) {
    if (map == NULL || !map->valid) {
        return KC_MMAP_ERROR;
    }

    if (kc_mmap_set(map, NULL, 0U) != KC_MMAP_OK) {
        return KC_MMAP_ERROR;
    }

    return kc_mmap_save(map);
}

void kc_mmap_close(kc_mmap_t *map) {
    if (map == NULL) {
        return;
    }

    kc_mmap_release_data(map);
    free(map->path);
    free(map);
}

uint64_t kc_mmap_version(void) {
    return (uint64_t)KC_MMAP_BUILD_VERSION;
}
