/**
 * librdvz.c - Generic rendezvous registry.
 * Summary: Named publications with independent ordered seekers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdvz.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#ifndef KC_RDVZ_BUILD_VERSION
#define KC_RDVZ_BUILD_VERSION 0
#endif

#define KC_RDVZ_INITIAL_BUCKETS 16U
#define KC_RDVZ_LOAD_DEN 4U

typedef struct kc_rdvz_point_node kc_rdvz_point_node_t;
typedef struct kc_rdvz_seeker_node kc_rdvz_seeker_node_t;

struct kc_rdvz_seeker_node {
    kc_rdvz_seeker_t view;
    kc_rdvz_t *owner;
    kc_rdvz_point_node_t *point;
    kc_rdvz_seeker_node_t *list_previous;
    kc_rdvz_seeker_node_t *list_next;
    kc_rdvz_seeker_node_t *expiry_previous;
    kc_rdvz_seeker_node_t *expiry_next;
    uint64_t expires_at_ms;
    void *owned_data;
};

struct kc_rdvz_point_node {
    kc_rdvz_point_t view;
    kc_rdvz_publication_t publication;
    uint64_t hash;
    kc_rdvz_point_node_t *hash_previous;
    kc_rdvz_point_node_t *hash_next;
    kc_rdvz_point_node_t *expiry_previous;
    kc_rdvz_point_node_t *expiry_next;
    uint64_t publication_expires_at_ms;
    kc_rdvz_seeker_node_t *seeker_head;
    kc_rdvz_seeker_node_t *seeker_tail;
    char *owned_id;
    void *publication_data;
};

struct kc_rdvz {
    kc_rdvz_options_t options;
    kc_rdvz_point_node_t **buckets;
    size_t bucket_count;
    size_t point_count;
    size_t publication_count;
    size_t seeker_count;
    kc_rdvz_point_node_t *publication_expiry_head;
    kc_rdvz_point_node_t *publication_expiry_tail;
    kc_rdvz_seeker_node_t *seeker_expiry_head;
    kc_rdvz_seeker_node_t *seeker_expiry_tail;
};

/**
 * Get current time in milliseconds.
 * @return Current time in milliseconds.
 */
static uint64_t rdvz_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#elif defined(__EMSCRIPTEN__)
    return (uint64_t)emscripten_get_now();
#else
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
    }
#endif
#if !defined(__ANDROID__)
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
    }
#endif
    return (uint64_t)time(NULL) * 1000U;
#endif
}

/**
 * Compute expiry timestamp with saturation arithmetic.
 * @param now Current time in milliseconds.
 * @param ttl Time-to-live in milliseconds, or 0 for no expiry.
 * @return Expiry timestamp, 0 if no expiry, UINT64_MAX on overflow.
 */
static uint64_t rdvz_expiry(uint64_t now, uint64_t ttl) {
    if (ttl == 0U) return 0U;
    if (UINT64_MAX - now < ttl) return UINT64_MAX;
    return now + ttl;
}

/**
 * Compute FNV-1a hash of a string identifier.
 * @param id NUL-terminated identifier string.
 * @return 64-bit hash value.
 */
static uint64_t rdvz_hash_id(const char *id) {
    const unsigned char *p;
    uint64_t hash;

    hash = UINT64_C(1469598103934665603);
    for (p = (const unsigned char *)id; *p != 0U; p++) {
        hash ^= (uint64_t)*p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/**
 * Copy data bytes with malloc.
 * @param data Source data pointer, or NULL when size is zero.
 * @param size Number of bytes to copy.
 * @return Newly allocated copy, or NULL on allocation failure or zero size.
 */
static void *rdvz_copy_data(const void *data, size_t size) {
    void *copy;

    if (size == 0U) return NULL;
    copy = malloc(size);
    if (!copy) return NULL;
    memcpy(copy, data, size);
    return copy;
}

/**
 * Copy a NUL-terminated identifier string with malloc.
 * @param id Source identifier string.
 * @return Newly allocated copy, or NULL on allocation failure.
 */
static char *rdvz_copy_id(const char *id) {
    size_t len;
    char *copy;

    len = strlen(id);
    if (len == SIZE_MAX) return NULL;
    copy = (char *)malloc(len + 1U);
    if (!copy) return NULL;
    memcpy(copy, id, len + 1U);
    return copy;
}

/**
 * Convert seeker view to internal node pointer.
 * @param seeker Borrowed seeker view.
 * @return Internal seeker node pointer.
 */
static kc_rdvz_seeker_node_t *rdvz_seeker_node(const kc_rdvz_seeker_t *seeker) {
    return (kc_rdvz_seeker_node_t *)(uintptr_t)seeker;
}

/**
 * Find a point node by identifier and hash.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @param hash Precomputed hash of id.
 * @return Matching point node, or NULL if not found.
 */
static kc_rdvz_point_node_t *rdvz_find(kc_rdvz_t *rdvz, const char *id,
    uint64_t hash)
{
    kc_rdvz_point_node_t *point;
    size_t bucket;

    bucket = (size_t)(hash & (uint64_t)(rdvz->bucket_count - 1U));
    for (point = rdvz->buckets[bucket]; point; point = point->hash_next) {
        if (point->hash == hash && strcmp(point->owned_id, id) == 0)
            return point;
    }
    return NULL;
}

/**
 * Rehash the registry's bucket array to a new capacity.
 * @param rdvz Registry.
 * @param capacity New bucket capacity (must be power of two).
 * @return 1 on success, 0 on allocation failure.
 */
static int rdvz_rehash(kc_rdvz_t *rdvz, size_t capacity) {
    kc_rdvz_point_node_t **buckets;
    kc_rdvz_point_node_t *point;
    kc_rdvz_point_node_t *next;
    size_t i;
    size_t bucket;

    if (capacity > SIZE_MAX / sizeof(*buckets)) return 0;
    buckets = (kc_rdvz_point_node_t **)calloc(capacity, sizeof(*buckets));
    if (!buckets) return 0;

    for (i = 0U; i < rdvz->bucket_count; i++) {
        point = rdvz->buckets[i];
        while (point) {
            next = point->hash_next;
            point->hash_previous = NULL;
            bucket = (size_t)(point->hash & (uint64_t)(capacity - 1U));
            point->hash_next = buckets[bucket];
            if (point->hash_next) point->hash_next->hash_previous = point;
            buckets[bucket] = point;
            point = next;
        }
    }

    free(rdvz->buckets);
    rdvz->buckets = buckets;
    rdvz->bucket_count = capacity;
    return 1;
}

/**
 * Reserve space for a new point, rehashing if needed.
 * @param rdvz Registry.
 * @return 1 on success, 0 on rehash failure.
 */
static int rdvz_reserve_point(kc_rdvz_t *rdvz) {
    size_t capacity;
    size_t threshold;

    threshold = rdvz->bucket_count - rdvz->bucket_count / KC_RDVZ_LOAD_DEN;
    if (rdvz->point_count < threshold) return 1;
    if (rdvz->bucket_count > SIZE_MAX / 2U) return 0;
    capacity = rdvz->bucket_count * 2U;
    return rdvz_rehash(rdvz, capacity);
}

/**
 * Insert a point into the hash table.
 * @param rdvz Registry.
 * @param point Point node to insert.
 * @return void
 */
static void rdvz_hash_insert(kc_rdvz_t *rdvz, kc_rdvz_point_node_t *point) {
    size_t bucket;

    bucket = (size_t)(point->hash & (uint64_t)(rdvz->bucket_count - 1U));
    point->hash_previous = NULL;
    point->hash_next = rdvz->buckets[bucket];
    if (point->hash_next) point->hash_next->hash_previous = point;
    rdvz->buckets[bucket] = point;
    rdvz->point_count++;
}

/**
 * Remove a point from the hash table.
 * @param rdvz Registry.
 * @param point Point node to remove.
 * @return void
 */
static void rdvz_hash_remove(kc_rdvz_t *rdvz, kc_rdvz_point_node_t *point) {
    size_t bucket;

    bucket = (size_t)(point->hash & (uint64_t)(rdvz->bucket_count - 1U));
    if (point->hash_previous)
        point->hash_previous->hash_next = point->hash_next;
    else
        rdvz->buckets[bucket] = point->hash_next;
    if (point->hash_next)
        point->hash_next->hash_previous = point->hash_previous;
    point->hash_previous = NULL;
    point->hash_next = NULL;
    rdvz->point_count--;
}

/**
 * Create a new point node and insert into hash table.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @param hash Precomputed hash of id.
 * @return New point node, or NULL on allocation failure.
 */
static kc_rdvz_point_node_t *rdvz_point_create(kc_rdvz_t *rdvz, const char *id,
    uint64_t hash)
{
    kc_rdvz_point_node_t *point;

    if (!rdvz_reserve_point(rdvz)) return NULL;
    point = (kc_rdvz_point_node_t *)calloc(1U, sizeof(*point));
    if (!point) return NULL;
    point->owned_id = rdvz_copy_id(id);
    if (!point->owned_id) {
        free(point);
        return NULL;
    }
    point->hash = hash;
    point->view.id = point->owned_id;
    rdvz_hash_insert(rdvz, point);
    return point;
}

/**
 * Destroy an empty point node (no publication, no seekers).
 * @param rdvz Registry.
 * @param point Point node to destroy.
 * @return void
 */
static void rdvz_point_destroy_empty(kc_rdvz_t *rdvz,
    kc_rdvz_point_node_t *point)
{
    rdvz_hash_remove(rdvz, point);
    free(point->owned_id);
    free(point);
}

/**
 * Append a point to the publication expiry queue.
 * @param rdvz Registry.
 * @param point Point with publication to add to expiry queue.
 * @return void
 */
static void rdvz_publication_expiry_append(kc_rdvz_t *rdvz,
    kc_rdvz_point_node_t *point)
{
    point->expiry_previous = rdvz->publication_expiry_tail;
    point->expiry_next = NULL;
    if (rdvz->publication_expiry_tail)
        rdvz->publication_expiry_tail->expiry_next = point;
    else
        rdvz->publication_expiry_head = point;
    rdvz->publication_expiry_tail = point;
}

/**
 * Remove a point from the publication expiry queue.
 * @param rdvz Registry.
 * @param point Point to remove from expiry queue.
 * @return void
 */
static void rdvz_publication_expiry_remove(kc_rdvz_t *rdvz,
    kc_rdvz_point_node_t *point)
{
    if (point->expiry_previous)
        point->expiry_previous->expiry_next = point->expiry_next;
    else if (rdvz->publication_expiry_head == point)
        rdvz->publication_expiry_head = point->expiry_next;
    if (point->expiry_next)
        point->expiry_next->expiry_previous = point->expiry_previous;
    else if (rdvz->publication_expiry_tail == point)
        rdvz->publication_expiry_tail = point->expiry_previous;
    point->expiry_previous = NULL;
    point->expiry_next = NULL;
}

/**
 * Append a seeker to the seeker expiry queue.
 * @param rdvz Registry.
 * @param seeker Seeker node to add to expiry queue.
 * @return void
 */
static void rdvz_seeker_expiry_append(kc_rdvz_t *rdvz,
    kc_rdvz_seeker_node_t *seeker)
{
    seeker->expiry_previous = rdvz->seeker_expiry_tail;
    seeker->expiry_next = NULL;
    if (rdvz->seeker_expiry_tail)
        rdvz->seeker_expiry_tail->expiry_next = seeker;
    else
        rdvz->seeker_expiry_head = seeker;
    rdvz->seeker_expiry_tail = seeker;
}

/**
 * Remove a seeker from the seeker expiry queue.
 * @param rdvz Registry.
 * @param seeker Seeker node to remove from expiry queue.
 * @return void
 */
static void rdvz_seeker_expiry_remove(kc_rdvz_t *rdvz,
    kc_rdvz_seeker_node_t *seeker)
{
    if (seeker->expiry_previous)
        seeker->expiry_previous->expiry_next = seeker->expiry_next;
    else if (rdvz->seeker_expiry_head == seeker)
        rdvz->seeker_expiry_head = seeker->expiry_next;
    if (seeker->expiry_next)
        seeker->expiry_next->expiry_previous = seeker->expiry_previous;
    else if (rdvz->seeker_expiry_tail == seeker)
        rdvz->seeker_expiry_tail = seeker->expiry_previous;
    seeker->expiry_previous = NULL;
    seeker->expiry_next = NULL;
}

/**
 * Append a seeker to a point's seeker list.
 * @param point Point node.
 * @param seeker Seeker node to append.
 * @return void
 */
static void rdvz_seeker_list_append(kc_rdvz_point_node_t *point,
    kc_rdvz_seeker_node_t *seeker)
{
    seeker->list_previous = point->seeker_tail;
    seeker->list_next = NULL;
    seeker->view.previous = point->seeker_tail ? &point->seeker_tail->view : NULL;
    seeker->view.next = NULL;
    if (point->seeker_tail) {
        point->seeker_tail->list_next = seeker;
        point->seeker_tail->view.next = &seeker->view;
    } else {
        point->seeker_head = seeker;
        point->view.head = &seeker->view;
    }
    point->seeker_tail = seeker;
    point->view.tail = &seeker->view;
    point->view.seeker_count++;
}

/**
 * Remove a seeker node from its point and expiry queue, and free its data.
 * @param rdvz Registry.
 * @param seeker Seeker node to remove and free.
 * @return void
 */
static void rdvz_seeker_remove_node(kc_rdvz_t *rdvz,
    kc_rdvz_seeker_node_t *seeker)
{
    kc_rdvz_point_node_t *point;

    point = seeker->point;
    if (rdvz->options.seeker_ttl_ms != 0U)
        rdvz_seeker_expiry_remove(rdvz, seeker);

    if (seeker->list_previous) {
        seeker->list_previous->list_next = seeker->list_next;
        seeker->list_previous->view.next = seeker->list_next
            ? &seeker->list_next->view : NULL;
    } else {
        point->seeker_head = seeker->list_next;
        point->view.head = seeker->list_next ? &seeker->list_next->view : NULL;
    }
    if (seeker->list_next) {
        seeker->list_next->list_previous = seeker->list_previous;
        seeker->list_next->view.previous = seeker->list_previous
            ? &seeker->list_previous->view : NULL;
    } else {
        point->seeker_tail = seeker->list_previous;
        point->view.tail = seeker->list_previous
            ? &seeker->list_previous->view : NULL;
    }
    point->view.seeker_count--;
    rdvz->seeker_count--;
    seeker->owner = NULL;
    seeker->point = NULL;
    free(seeker->owned_data);
    free(seeker);

    if (!point->view.publication && point->view.seeker_count == 0U)
        rdvz_point_destroy_empty(rdvz, point);
}

/**
 * Remove publication from a point, freeing its data.
 * @param rdvz Registry.
 * @param point Point node with publication to remove.
 * @return void
 */
static void rdvz_unpublish_node(kc_rdvz_t *rdvz, kc_rdvz_point_node_t *point) {
    if (!point->view.publication) return;
    if (rdvz->options.publication_ttl_ms != 0U)
        rdvz_publication_expiry_remove(rdvz, point);
    free(point->publication_data);
    point->publication_data = NULL;
    point->publication.data = NULL;
    point->publication.data_size = 0U;
    point->publication_expires_at_ms = 0U;
    point->view.publication = NULL;
    rdvz->publication_count--;
    if (point->view.seeker_count == 0U)
        rdvz_point_destroy_empty(rdvz, point);
}

/**
 * Open a new rendezvous registry.
 * @param out Output pointer for the new registry.
 * @param options Configuration options, or NULL for defaults.
 * @return KC_RDVZ_OK on success, negative status code on failure.
 */
int kc_rdvz_open(kc_rdvz_t **out, const kc_rdvz_options_t *options) {
    kc_rdvz_t *rdvz;

    if (!out) return KC_RDVZ_EINVAL;
    *out = NULL;
    rdvz = (kc_rdvz_t *)calloc(1U, sizeof(*rdvz));
    if (!rdvz) return KC_RDVZ_ENOMEM;
    rdvz->bucket_count = KC_RDVZ_INITIAL_BUCKETS;
    rdvz->buckets = (kc_rdvz_point_node_t **)calloc(rdvz->bucket_count,
        sizeof(*rdvz->buckets));
    if (!rdvz->buckets) {
        free(rdvz);
        return KC_RDVZ_ENOMEM;
    }
    if (options) rdvz->options = *options;
    *out = rdvz;
    return KC_RDVZ_OK;
}

/**
 * Get a rendezvous point by identifier.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @return Point view, or NULL if not found.
 */
const kc_rdvz_point_t *kc_rdvz_get(kc_rdvz_t *rdvz, const char *id) {
    kc_rdvz_point_node_t *point;
    uint64_t hash;

    if (!rdvz || !id) return NULL;
    hash = rdvz_hash_id(id);
    point = rdvz_find(rdvz, id, hash);
    return point ? &point->view : NULL;
}

/**
 * Publish data at an identifier.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @param data Data to publish, or NULL when data_size is zero.
 * @param data_size Data byte count.
 * @param out Optional output for the point view.
 * @return KC_RDVZ_OK on success, negative status code on failure.
 */
int kc_rdvz_publish(kc_rdvz_t *rdvz, const char *id,
    const void *data, size_t data_size, const kc_rdvz_point_t **out)
{
    kc_rdvz_point_node_t *point;
    void *copy;
    uint64_t hash;

    if (out) *out = NULL;
    if (!rdvz || !id || (data_size != 0U && !data)) return KC_RDVZ_EINVAL;
    hash = rdvz_hash_id(id);
    point = rdvz_find(rdvz, id, hash);
    if (point && point->view.publication) return KC_RDVZ_EEXIST;
    if (rdvz->options.max_publications != 0U &&
        rdvz->publication_count >= rdvz->options.max_publications)
        return KC_RDVZ_ELIMIT;

    copy = rdvz_copy_data(data, data_size);
    if (data_size != 0U && !copy) return KC_RDVZ_ENOMEM;
    if (!point) {
        point = rdvz_point_create(rdvz, id, hash);
        if (!point) {
            free(copy);
            return KC_RDVZ_ENOMEM;
        }
    }

    point->publication_data = copy;
    point->publication.data = copy;
    point->publication.data_size = data_size;
    point->view.publication = &point->publication;
    rdvz->publication_count++;
    if (rdvz->options.publication_ttl_ms != 0U) {
        point->publication_expires_at_ms = rdvz_expiry(rdvz_now_ms(),
            rdvz->options.publication_ttl_ms);
        rdvz_publication_expiry_append(rdvz, point);
    }
    if (out) *out = &point->view;
    return KC_RDVZ_OK;
}

/**
 * Remove publication at an identifier.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @return KC_RDVZ_OK on success, negative status code on failure.
 */
int kc_rdvz_unpublish(kc_rdvz_t *rdvz, const char *id) {
    kc_rdvz_point_node_t *point;
    uint64_t hash;

    if (!rdvz || !id) return KC_RDVZ_EINVAL;
    hash = rdvz_hash_id(id);
    point = rdvz_find(rdvz, id, hash);
    if (!point || !point->view.publication) return KC_RDVZ_ENOENT;
    rdvz_unpublish_node(rdvz, point);
    return KC_RDVZ_OK;
}

/**
 * Add a seeker at an identifier.
 * @param rdvz Registry.
 * @param id Identifier string.
 * @param data Seeker data, or NULL when data_size is zero.
 * @param data_size Seeker data byte count.
 * @param out Output for the seeker view.
 * @return KC_RDVZ_OK on success, negative status code on failure.
 */
int kc_rdvz_seek(kc_rdvz_t *rdvz, const char *id,
    const void *data, size_t data_size, const kc_rdvz_seeker_t **out)
{
    kc_rdvz_point_node_t *point;
    kc_rdvz_seeker_node_t *seeker;
    void *copy;
    uint64_t hash;

    if (!out) return KC_RDVZ_EINVAL;
    *out = NULL;
    if (!rdvz || !id || (data_size != 0U && !data)) return KC_RDVZ_EINVAL;
    if (rdvz->options.max_seekers != 0U &&
        rdvz->seeker_count >= rdvz->options.max_seekers)
        return KC_RDVZ_ELIMIT;

    hash = rdvz_hash_id(id);
    point = rdvz_find(rdvz, id, hash);
    if (point && rdvz->options.max_seekers_per_point != 0U &&
        point->view.seeker_count >= rdvz->options.max_seekers_per_point)
        return KC_RDVZ_ELIMIT;

    copy = rdvz_copy_data(data, data_size);
    if (data_size != 0U && !copy) return KC_RDVZ_ENOMEM;
    seeker = (kc_rdvz_seeker_node_t *)calloc(1U, sizeof(*seeker));
    if (!seeker) {
        free(copy);
        return KC_RDVZ_ENOMEM;
    }
    if (!point) {
        point = rdvz_point_create(rdvz, id, hash);
        if (!point) {
            free(copy);
            free(seeker);
            return KC_RDVZ_ENOMEM;
        }
    }

    seeker->owner = rdvz;
    seeker->point = point;
    seeker->owned_data = copy;
    seeker->view.point = &point->view;
    seeker->view.data = copy;
    seeker->view.data_size = data_size;
    rdvz_seeker_list_append(point, seeker);
    rdvz->seeker_count++;
    if (rdvz->options.seeker_ttl_ms != 0U) {
        seeker->expires_at_ms = rdvz_expiry(rdvz_now_ms(),
            rdvz->options.seeker_ttl_ms);
        rdvz_seeker_expiry_append(rdvz, seeker);
    }
    *out = &seeker->view;
    return KC_RDVZ_OK;
}

/**
 * Remove a seeker by its view pointer.
 * @param rdvz Registry that owns the seeker.
 * @param seeker Borrowed seeker view from kc_rdvz_seek() or point traversal.
 * @return KC_RDVZ_OK on success, KC_RDVZ_EINVAL on invalid seeker.
 */
int kc_rdvz_remove(kc_rdvz_t *rdvz, const kc_rdvz_seeker_t *seeker) {
    kc_rdvz_seeker_node_t *node;

    if (!rdvz || !seeker) return KC_RDVZ_EINVAL;
    node = rdvz_seeker_node(seeker);
    if (node->owner != rdvz || !node->point) return KC_RDVZ_EINVAL;
    rdvz_seeker_remove_node(rdvz, node);
    return KC_RDVZ_OK;
}

/**
 * Apply configured TTLs and remove expired publications and seekers.
 * @param rdvz Registry.
 * @return Number of publications plus seekers removed.
 */
size_t kc_rdvz_prune(kc_rdvz_t *rdvz) {
    kc_rdvz_point_node_t *point;
    kc_rdvz_seeker_node_t *seeker;
    uint64_t now;
    size_t removed;

    if (!rdvz) return 0U;
    now = rdvz_now_ms();
    removed = 0U;

    while ((point = rdvz->publication_expiry_head) != NULL &&
        point->publication_expires_at_ms <= now)
    {
        rdvz_unpublish_node(rdvz, point);
        removed++;
    }
    while ((seeker = rdvz->seeker_expiry_head) != NULL &&
        seeker->expires_at_ms <= now)
    {
        rdvz_seeker_remove_node(rdvz, seeker);
        removed++;
    }
    return removed;
}

/**
 * Close a rendezvous registry and free all resources.
 * @param rdvz Registry to close. NULL is a safe no-op.
 * @return void
 */
void kc_rdvz_close(kc_rdvz_t *rdvz) {
    kc_rdvz_point_node_t *point;
    kc_rdvz_point_node_t *next_point;
    kc_rdvz_seeker_node_t *seeker;
    kc_rdvz_seeker_node_t *next_seeker;
    size_t i;

    if (!rdvz) return;
    for (i = 0U; i < rdvz->bucket_count; i++) {
        point = rdvz->buckets[i];
        while (point) {
            next_point = point->hash_next;
            seeker = point->seeker_head;
            while (seeker) {
                next_seeker = seeker->list_next;
                free(seeker->owned_data);
                free(seeker);
                seeker = next_seeker;
            }
            free(point->publication_data);
            free(point->owned_id);
            free(point);
            point = next_point;
        }
    }
    free(rdvz->buckets);
    free(rdvz);
}

/**
 * Get a textual description for a status code.
 * @param status Status code.
 * @return Static NUL-terminated description string.
 */
const char *kc_rdvz_strerror(int status) {
    switch (status) {
        case KC_RDVZ_OK: return "ok";
        case KC_RDVZ_ENOENT: return "not found";
        case KC_RDVZ_EEXIST: return "already published";
        case KC_RDVZ_EINVAL: return "invalid argument";
        case KC_RDVZ_ENOMEM: return "out of memory";
        case KC_RDVZ_ELIMIT: return "storage limit reached";
        default: return "unknown rendezvous error";
    }
}

/**
 * Get the build version.
 * @return Unix timestamp of the build.
 */
uint64_t kc_rdvz_version(void) {
    return (uint64_t)KC_RDVZ_BUILD_VERSION;
}
