/**
 * librdv.c - Generic rendezvous registry.
 * Summary: Named publications with independent ordered seekers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdv.h"

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

#ifndef KC_RDV_BUILD_VERSION
#define KC_RDV_BUILD_VERSION 0
#endif

#define KC_RDV_INITIAL_BUCKETS 16U
#define KC_RDV_LOAD_DEN 4U

typedef struct kc_rdv_point_node kc_rdv_point_node_t;
typedef struct kc_rdv_seeker_node kc_rdv_seeker_node_t;

struct kc_rdv_seeker_node {
    kc_rdv_seeker_t view;
    kc_rdv_t *owner;
    kc_rdv_point_node_t *point;
    kc_rdv_seeker_node_t *list_previous;
    kc_rdv_seeker_node_t *list_next;
    kc_rdv_seeker_node_t *expiry_previous;
    kc_rdv_seeker_node_t *expiry_next;
    uint64_t expires_at_ms;
    void *owned_data;
};

struct kc_rdv_point_node {
    kc_rdv_point_t view;
    kc_rdv_publication_t publication;
    uint64_t hash;
    kc_rdv_point_node_t *hash_previous;
    kc_rdv_point_node_t *hash_next;
    kc_rdv_point_node_t *expiry_previous;
    kc_rdv_point_node_t *expiry_next;
    uint64_t publication_expires_at_ms;
    kc_rdv_seeker_node_t *seeker_head;
    kc_rdv_seeker_node_t *seeker_tail;
    char *owned_id;
    void *publication_data;
};

struct kc_rdv {
    kc_rdv_options_t options;
    kc_rdv_point_node_t **buckets;
    size_t bucket_count;
    size_t point_count;
    size_t publication_count;
    size_t seeker_count;
    kc_rdv_point_node_t *publication_expiry_head;
    kc_rdv_point_node_t *publication_expiry_tail;
    kc_rdv_seeker_node_t *seeker_expiry_head;
    kc_rdv_seeker_node_t *seeker_expiry_tail;
};

/**
 * Get current time in milliseconds.
 * @return Current time in milliseconds.
 */
static uint64_t rdv_now_ms(void) {
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
static uint64_t rdv_expiry(uint64_t now, uint64_t ttl) {
    if (ttl == 0U) return 0U;
    if (UINT64_MAX - now < ttl) return UINT64_MAX;
    return now + ttl;
}

/**
 * Compute FNV-1a hash of a string identifier.
 * @param id NUL-terminated identifier string.
 * @return 64-bit hash value.
 */
static uint64_t rdv_hash_id(const char *id) {
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
static void *rdv_copy_data(const void *data, size_t size) {
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
static char *rdv_copy_id(const char *id) {
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
static kc_rdv_seeker_node_t *rdv_seeker_node(const kc_rdv_seeker_t *seeker) {
    return (kc_rdv_seeker_node_t *)(uintptr_t)seeker;
}

/**
 * Find a point node by identifier and hash.
 * @param rdv Registry.
 * @param id Identifier string.
 * @param hash Precomputed hash of id.
 * @return Matching point node, or NULL if not found.
 */
static kc_rdv_point_node_t *rdv_find(kc_rdv_t *rdv, const char *id,
    uint64_t hash)
{
    kc_rdv_point_node_t *point;
    size_t bucket;

    bucket = (size_t)(hash & (uint64_t)(rdv->bucket_count - 1U));
    for (point = rdv->buckets[bucket]; point; point = point->hash_next) {
        if (point->hash == hash && strcmp(point->owned_id, id) == 0)
            return point;
    }
    return NULL;
}

/**
 * Rehash the registry's bucket array to a new capacity.
 * @param rdv Registry.
 * @param capacity New bucket capacity (must be power of two).
 * @return 1 on success, 0 on allocation failure.
 */
static int rdv_rehash(kc_rdv_t *rdv, size_t capacity) {
    kc_rdv_point_node_t **buckets;
    kc_rdv_point_node_t *point;
    kc_rdv_point_node_t *next;
    size_t i;
    size_t bucket;

    if (capacity > SIZE_MAX / sizeof(*buckets)) return 0;
    buckets = (kc_rdv_point_node_t **)calloc(capacity, sizeof(*buckets));
    if (!buckets) return 0;

    for (i = 0U; i < rdv->bucket_count; i++) {
        point = rdv->buckets[i];
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

    free(rdv->buckets);
    rdv->buckets = buckets;
    rdv->bucket_count = capacity;
    return 1;
}

/**
 * Reserve space for a new point, rehashing if needed.
 * @param rdv Registry.
 * @return 1 on success, 0 on rehash failure.
 */
static int rdv_reserve_point(kc_rdv_t *rdv) {
    size_t capacity;
    size_t threshold;

    threshold = rdv->bucket_count - rdv->bucket_count / KC_RDV_LOAD_DEN;
    if (rdv->point_count < threshold) return 1;
    if (rdv->bucket_count > SIZE_MAX / 2U) return 0;
    capacity = rdv->bucket_count * 2U;
    return rdv_rehash(rdv, capacity);
}

/**
 * Insert a point into the hash table.
 * @param rdv Registry.
 * @param point Point node to insert.
 * @return void
 */
static void rdv_hash_insert(kc_rdv_t *rdv, kc_rdv_point_node_t *point) {
    size_t bucket;

    bucket = (size_t)(point->hash & (uint64_t)(rdv->bucket_count - 1U));
    point->hash_previous = NULL;
    point->hash_next = rdv->buckets[bucket];
    if (point->hash_next) point->hash_next->hash_previous = point;
    rdv->buckets[bucket] = point;
    rdv->point_count++;
}

/**
 * Remove a point from the hash table.
 * @param rdv Registry.
 * @param point Point node to remove.
 * @return void
 */
static void rdv_hash_remove(kc_rdv_t *rdv, kc_rdv_point_node_t *point) {
    size_t bucket;

    bucket = (size_t)(point->hash & (uint64_t)(rdv->bucket_count - 1U));
    if (point->hash_previous)
        point->hash_previous->hash_next = point->hash_next;
    else
        rdv->buckets[bucket] = point->hash_next;
    if (point->hash_next)
        point->hash_next->hash_previous = point->hash_previous;
    point->hash_previous = NULL;
    point->hash_next = NULL;
    rdv->point_count--;
}

/**
 * Create a new point node and insert into hash table.
 * @param rdv Registry.
 * @param id Identifier string.
 * @param hash Precomputed hash of id.
 * @return New point node, or NULL on allocation failure.
 */
static kc_rdv_point_node_t *rdv_point_create(kc_rdv_t *rdv, const char *id,
    uint64_t hash)
{
    kc_rdv_point_node_t *point;

    if (!rdv_reserve_point(rdv)) return NULL;
    point = (kc_rdv_point_node_t *)calloc(1U, sizeof(*point));
    if (!point) return NULL;
    point->owned_id = rdv_copy_id(id);
    if (!point->owned_id) {
        free(point);
        return NULL;
    }
    point->hash = hash;
    point->view.id = point->owned_id;
    rdv_hash_insert(rdv, point);
    return point;
}

/**
 * Destroy an empty point node (no publication, no seekers).
 * @param rdv Registry.
 * @param point Point node to destroy.
 * @return void
 */
static void rdv_point_destroy_empty(kc_rdv_t *rdv,
    kc_rdv_point_node_t *point)
{
    rdv_hash_remove(rdv, point);
    free(point->owned_id);
    free(point);
}

/**
 * Append a point to the publication expiry queue.
 * @param rdv Registry.
 * @param point Point with publication to add to expiry queue.
 * @return void
 */
static void rdv_publication_expiry_append(kc_rdv_t *rdv,
    kc_rdv_point_node_t *point)
{
    point->expiry_previous = rdv->publication_expiry_tail;
    point->expiry_next = NULL;
    if (rdv->publication_expiry_tail)
        rdv->publication_expiry_tail->expiry_next = point;
    else
        rdv->publication_expiry_head = point;
    rdv->publication_expiry_tail = point;
}

/**
 * Remove a point from the publication expiry queue.
 * @param rdv Registry.
 * @param point Point to remove from expiry queue.
 * @return void
 */
static void rdv_publication_expiry_remove(kc_rdv_t *rdv,
    kc_rdv_point_node_t *point)
{
    if (point->expiry_previous)
        point->expiry_previous->expiry_next = point->expiry_next;
    else if (rdv->publication_expiry_head == point)
        rdv->publication_expiry_head = point->expiry_next;
    if (point->expiry_next)
        point->expiry_next->expiry_previous = point->expiry_previous;
    else if (rdv->publication_expiry_tail == point)
        rdv->publication_expiry_tail = point->expiry_previous;
    point->expiry_previous = NULL;
    point->expiry_next = NULL;
}

/**
 * Append a seeker to the seeker expiry queue.
 * @param rdv Registry.
 * @param seeker Seeker node to add to expiry queue.
 * @return void
 */
static void rdv_seeker_expiry_append(kc_rdv_t *rdv,
    kc_rdv_seeker_node_t *seeker)
{
    seeker->expiry_previous = rdv->seeker_expiry_tail;
    seeker->expiry_next = NULL;
    if (rdv->seeker_expiry_tail)
        rdv->seeker_expiry_tail->expiry_next = seeker;
    else
        rdv->seeker_expiry_head = seeker;
    rdv->seeker_expiry_tail = seeker;
}

/**
 * Remove a seeker from the seeker expiry queue.
 * @param rdv Registry.
 * @param seeker Seeker node to remove from expiry queue.
 * @return void
 */
static void rdv_seeker_expiry_remove(kc_rdv_t *rdv,
    kc_rdv_seeker_node_t *seeker)
{
    if (seeker->expiry_previous)
        seeker->expiry_previous->expiry_next = seeker->expiry_next;
    else if (rdv->seeker_expiry_head == seeker)
        rdv->seeker_expiry_head = seeker->expiry_next;
    if (seeker->expiry_next)
        seeker->expiry_next->expiry_previous = seeker->expiry_previous;
    else if (rdv->seeker_expiry_tail == seeker)
        rdv->seeker_expiry_tail = seeker->expiry_previous;
    seeker->expiry_previous = NULL;
    seeker->expiry_next = NULL;
}

/**
 * Append a seeker to a point's seeker list.
 * @param point Point node.
 * @param seeker Seeker node to append.
 * @return void
 */
static void rdv_seeker_list_append(kc_rdv_point_node_t *point,
    kc_rdv_seeker_node_t *seeker)
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
 * @param rdv Registry.
 * @param seeker Seeker node to remove and free.
 * @return void
 */
static void rdv_seeker_remove_node(kc_rdv_t *rdv,
    kc_rdv_seeker_node_t *seeker)
{
    kc_rdv_point_node_t *point;

    point = seeker->point;
    if (rdv->options.seeker_ttl_ms != 0U)
        rdv_seeker_expiry_remove(rdv, seeker);

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
    rdv->seeker_count--;
    seeker->owner = NULL;
    seeker->point = NULL;
    free(seeker->owned_data);
    free(seeker);

    if (!point->view.publication && point->view.seeker_count == 0U)
        rdv_point_destroy_empty(rdv, point);
}

/**
 * Remove publication from a point, freeing its data.
 * @param rdv Registry.
 * @param point Point node with publication to remove.
 * @return void
 */
static void rdv_unpublish_node(kc_rdv_t *rdv, kc_rdv_point_node_t *point) {
    if (!point->view.publication) return;
    if (rdv->options.publication_ttl_ms != 0U)
        rdv_publication_expiry_remove(rdv, point);
    free(point->publication_data);
    point->publication_data = NULL;
    point->publication.data = NULL;
    point->publication.data_size = 0U;
    point->publication_expires_at_ms = 0U;
    point->view.publication = NULL;
    rdv->publication_count--;
    if (point->view.seeker_count == 0U)
        rdv_point_destroy_empty(rdv, point);
}

/**
 * Open a new rendezvous registry.
 * @param out Output pointer for the new registry.
 * @param options Configuration options, or NULL for defaults.
 * @return KC_RDV_OK on success, negative status code on failure.
 */
int kc_rdv_open(kc_rdv_t **out, const kc_rdv_options_t *options) {
    kc_rdv_t *rdv;

    if (!out) return KC_RDV_EINVAL;
    *out = NULL;
    rdv = (kc_rdv_t *)calloc(1U, sizeof(*rdv));
    if (!rdv) return KC_RDV_ENOMEM;
    rdv->bucket_count = KC_RDV_INITIAL_BUCKETS;
    rdv->buckets = (kc_rdv_point_node_t **)calloc(rdv->bucket_count,
        sizeof(*rdv->buckets));
    if (!rdv->buckets) {
        free(rdv);
        return KC_RDV_ENOMEM;
    }
    if (options) rdv->options = *options;
    *out = rdv;
    return KC_RDV_OK;
}

const kc_rdv_point_t *kc_rdv_get(kc_rdv_t *rdv, const char *id) {
    kc_rdv_point_node_t *point;
    uint64_t hash;

    if (!rdv || !id) return NULL;
    hash = rdv_hash_id(id);
    point = rdv_find(rdv, id, hash);
    return point ? &point->view : NULL;
}

int kc_rdv_publish(kc_rdv_t *rdv, const char *id,
    const void *data, size_t data_size, const kc_rdv_point_t **out)
{
    kc_rdv_point_node_t *point;
    void *copy;
    uint64_t hash;

    if (out) *out = NULL;
    if (!rdv || !id || (data_size != 0U && !data)) return KC_RDV_EINVAL;
    hash = rdv_hash_id(id);
    point = rdv_find(rdv, id, hash);
    if (point && point->view.publication) return KC_RDV_EEXIST;
    if (rdv->options.max_publications != 0U &&
        rdv->publication_count >= rdv->options.max_publications)
        return KC_RDV_ELIMIT;

    copy = rdv_copy_data(data, data_size);
    if (data_size != 0U && !copy) return KC_RDV_ENOMEM;
    if (!point) {
        point = rdv_point_create(rdv, id, hash);
        if (!point) {
            free(copy);
            return KC_RDV_ENOMEM;
        }
    }

    point->publication_data = copy;
    point->publication.data = copy;
    point->publication.data_size = data_size;
    point->view.publication = &point->publication;
    rdv->publication_count++;
    if (rdv->options.publication_ttl_ms != 0U) {
        point->publication_expires_at_ms = rdv_expiry(rdv_now_ms(),
            rdv->options.publication_ttl_ms);
        rdv_publication_expiry_append(rdv, point);
    }
    if (out) *out = &point->view;
    return KC_RDV_OK;
}

int kc_rdv_unpublish(kc_rdv_t *rdv, const char *id) {
    kc_rdv_point_node_t *point;
    uint64_t hash;

    if (!rdv || !id) return KC_RDV_EINVAL;
    hash = rdv_hash_id(id);
    point = rdv_find(rdv, id, hash);
    if (!point || !point->view.publication) return KC_RDV_ENOENT;
    rdv_unpublish_node(rdv, point);
    return KC_RDV_OK;
}

int kc_rdv_seek(kc_rdv_t *rdv, const char *id,
    const void *data, size_t data_size, const kc_rdv_seeker_t **out)
{
    kc_rdv_point_node_t *point;
    kc_rdv_seeker_node_t *seeker;
    void *copy;
    uint64_t hash;

    if (!out) return KC_RDV_EINVAL;
    *out = NULL;
    if (!rdv || !id || (data_size != 0U && !data)) return KC_RDV_EINVAL;
    if (rdv->options.max_seekers != 0U &&
        rdv->seeker_count >= rdv->options.max_seekers)
        return KC_RDV_ELIMIT;

    hash = rdv_hash_id(id);
    point = rdv_find(rdv, id, hash);
    if (point && rdv->options.max_seekers_per_point != 0U &&
        point->view.seeker_count >= rdv->options.max_seekers_per_point)
        return KC_RDV_ELIMIT;

    copy = rdv_copy_data(data, data_size);
    if (data_size != 0U && !copy) return KC_RDV_ENOMEM;
    seeker = (kc_rdv_seeker_node_t *)calloc(1U, sizeof(*seeker));
    if (!seeker) {
        free(copy);
        return KC_RDV_ENOMEM;
    }
    if (!point) {
        point = rdv_point_create(rdv, id, hash);
        if (!point) {
            free(copy);
            free(seeker);
            return KC_RDV_ENOMEM;
        }
    }

    seeker->owner = rdv;
    seeker->point = point;
    seeker->owned_data = copy;
    seeker->view.point = &point->view;
    seeker->view.data = copy;
    seeker->view.data_size = data_size;
    rdv_seeker_list_append(point, seeker);
    rdv->seeker_count++;
    if (rdv->options.seeker_ttl_ms != 0U) {
        seeker->expires_at_ms = rdv_expiry(rdv_now_ms(),
            rdv->options.seeker_ttl_ms);
        rdv_seeker_expiry_append(rdv, seeker);
    }
    *out = &seeker->view;
    return KC_RDV_OK;
}

int kc_rdv_remove(kc_rdv_t *rdv, const kc_rdv_seeker_t *seeker) {
    kc_rdv_seeker_node_t *node;

    if (!rdv || !seeker) return KC_RDV_EINVAL;
    node = rdv_seeker_node(seeker);
    if (node->owner != rdv || !node->point) return KC_RDV_EINVAL;
    rdv_seeker_remove_node(rdv, node);
    return KC_RDV_OK;
}

size_t kc_rdv_prune(kc_rdv_t *rdv) {
    kc_rdv_point_node_t *point;
    kc_rdv_seeker_node_t *seeker;
    uint64_t now;
    size_t removed;

    if (!rdv) return 0U;
    now = rdv_now_ms();
    removed = 0U;

    while ((point = rdv->publication_expiry_head) != NULL &&
        point->publication_expires_at_ms <= now)
    {
        rdv_unpublish_node(rdv, point);
        removed++;
    }
    while ((seeker = rdv->seeker_expiry_head) != NULL &&
        seeker->expires_at_ms <= now)
    {
        rdv_seeker_remove_node(rdv, seeker);
        removed++;
    }
    return removed;
}

void kc_rdv_close(kc_rdv_t *rdv) {
    kc_rdv_point_node_t *point;
    kc_rdv_point_node_t *next_point;
    kc_rdv_seeker_node_t *seeker;
    kc_rdv_seeker_node_t *next_seeker;
    size_t i;

    if (!rdv) return;
    for (i = 0U; i < rdv->bucket_count; i++) {
        point = rdv->buckets[i];
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
    free(rdv->buckets);
    free(rdv);
}

const char *kc_rdv_strerror(int status) {
    switch (status) {
        case KC_RDV_OK: return "ok";
        case KC_RDV_ENOENT: return "not found";
        case KC_RDV_EEXIST: return "already published";
        case KC_RDV_EINVAL: return "invalid argument";
        case KC_RDV_ENOMEM: return "out of memory";
        case KC_RDV_ELIMIT: return "storage limit reached";
        default: return "unknown rendezvous error";
    }
}

uint64_t kc_rdv_version(void) {
    return (uint64_t)KC_RDV_BUILD_VERSION;
}
