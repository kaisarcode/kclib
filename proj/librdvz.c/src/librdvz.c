/**
 * librdvz.c - Generic peer rendezvous relationships.
 * Summary: Registry-local peer identities and symmetric direct links.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdvz.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_RDVZ_BUILD_VERSION
#define KC_RDVZ_BUILD_VERSION 0
#endif

#define KC_RDVZ_INITIAL_BUCKETS 16U
#define KC_RDVZ_LOAD_DEN 4U

typedef struct kc_rdvz_edge kc_rdvz_edge_t;

/**
 * One end of a bidirectional peer relationship.
 */
struct kc_rdvz_edge {
    kc_rdvz_peer_t *other;
    kc_rdvz_edge_t *reverse;
    kc_rdvz_edge_t *previous;
    kc_rdvz_edge_t *next;
};

/**
 * One independently owned peer within a registry.
 */
struct kc_rdvz_peer {
    kc_rdvz_t *owner;
    uint64_t serial;
    char id[32];
    kc_rdvz_peer_t *previous;
    kc_rdvz_peer_t *next;
    kc_rdvz_peer_t *hash_next;
    kc_rdvz_edge_t *edge_head;
    kc_rdvz_edge_t *edge_tail;
};

/**
 * A registry with one peer namespace and independent direct links.
 */
struct kc_rdvz {
    kc_rdvz_options_t options;
    kc_rdvz_peer_t **buckets;
    size_t bucket_count;
    size_t peer_count;
    uint64_t next_id;
    kc_rdvz_peer_t *head;
    kc_rdvz_peer_t *tail;
    unsigned busy;
};

/**
 * Maps one peer identifier to a hash table bucket.
 * @param id Numeric peer ID.
 * @param capacity Power-of-two table capacity.
 * @return Bucket index.
 */
static size_t rdvz_bucket(uint64_t id, size_t capacity) {
    uint64_t hash = id;
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    hash *= UINT64_C(0xc4ceb9fe1a85ec53);
    hash ^= hash >> 33;
    return (size_t)(hash & (uint64_t)(capacity - 1U));
}

/**
 * Parses a canonical decimal peer identifier.
 * @param text Text from a caller.
 * @param out Parsed positive ID.
 * @return 1 for a valid ID, otherwise 0.
 */
static int rdvz_parse_id(const char *text, uint64_t *out) {
    uint64_t value = 0U;
    size_t i;

    if (!text || !text[0] || text[0] == '0' || !out) return 0;
    for (i = 0U; text[i]; ++i) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') return 0;
        digit = (unsigned)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) return 0;
        value = value * 10U + digit;
    }
    *out = value;
    return 1;
}

/**
 * Finds an existing peer by its numeric identifier.
 * @param rdvz Registry to search.
 * @param id Numeric peer ID.
 * @return Live peer or NULL.
 */
static kc_rdvz_peer_t *rdvz_find(kc_rdvz_t *rdvz, uint64_t id) {
    kc_rdvz_peer_t *peer;
    size_t index = rdvz_bucket(id, rdvz->bucket_count);

    for (peer = rdvz->buckets[index]; peer; peer = peer->hash_next) {
        if (peer->serial == id) return peer;
    }
    return NULL;
}

/**
 * Rebuilds the peer lookup table at a larger capacity.
 * @param rdvz Registry to grow.
 * @param capacity New power-of-two table capacity.
 * @return Nonzero on success.
 */
static int rdvz_rehash(kc_rdvz_t *rdvz, size_t capacity) {
    kc_rdvz_peer_t **buckets;
    kc_rdvz_peer_t *peer;
    kc_rdvz_peer_t *next;
    size_t i;

    if (capacity > SIZE_MAX / sizeof(*buckets)) return 0;
    buckets = (kc_rdvz_peer_t **)calloc(capacity, sizeof(*buckets));
    if (!buckets) return 0;
    for (i = 0U; i < rdvz->bucket_count; ++i) {
        for (peer = rdvz->buckets[i]; peer; peer = next) {
            size_t index = rdvz_bucket(peer->serial, capacity);
            next = peer->hash_next;
            peer->hash_next = buckets[index];
            buckets[index] = peer;
        }
    }
    free(rdvz->buckets);
    rdvz->buckets = buckets;
    rdvz->bucket_count = capacity;
    return 1;
}

/**
 * Ensures the lookup table can accept one additional peer.
 * @param rdvz Registry to extend.
 * @return Nonzero on success.
 */
static int rdvz_reserve(kc_rdvz_t *rdvz) {
    size_t threshold = rdvz->bucket_count -
        rdvz->bucket_count / KC_RDVZ_LOAD_DEN;

    if (rdvz->peer_count < threshold) return 1;
    if (rdvz->bucket_count > SIZE_MAX / 2U) return 0;
    return rdvz_rehash(rdvz, rdvz->bucket_count * 2U);
}

/**
 * Inserts a constructed peer into the global list and lookup table.
 * @param rdvz Destination registry.
 * @param peer Newly allocated peer.
 * @return None.
 */
static void rdvz_insert(kc_rdvz_t *rdvz, kc_rdvz_peer_t *peer) {
    size_t index = rdvz_bucket(peer->serial, rdvz->bucket_count);

    peer->hash_next = rdvz->buckets[index];
    rdvz->buckets[index] = peer;
    peer->previous = rdvz->tail;
    if (rdvz->tail) rdvz->tail->next = peer;
    else rdvz->head = peer;
    rdvz->tail = peer;
    rdvz->peer_count++;
}

/**
 * Removes a peer from the registry's global list and lookup table.
 * @param rdvz Owner registry.
 * @param peer Live peer without direct links.
 * @return None.
 */
static void rdvz_erase(kc_rdvz_t *rdvz, kc_rdvz_peer_t *peer) {
    kc_rdvz_peer_t **slot;
    size_t index = rdvz_bucket(peer->serial, rdvz->bucket_count);

    slot = &rdvz->buckets[index];
    while (*slot && *slot != peer) slot = &(*slot)->hash_next;
    if (*slot) *slot = peer->hash_next;

    if (peer->previous) peer->previous->next = peer->next;
    else rdvz->head = peer->next;
    if (peer->next) peer->next->previous = peer->previous;
    else rdvz->tail = peer->previous;
    rdvz->peer_count--;
}

/**
 * Attaches one side of a direct link to a peer.
 * @param peer Receiving peer.
 * @param edge Link endpoint.
 * @return None.
 */
static void rdvz_edge_append(kc_rdvz_peer_t *peer, kc_rdvz_edge_t *edge) {
    edge->previous = peer->edge_tail;
    if (peer->edge_tail) peer->edge_tail->next = edge;
    else peer->edge_head = edge;
    peer->edge_tail = edge;
}

/**
 * Detaches one side of a direct link from a peer.
 * @param peer Owner of that link endpoint.
 * @param edge Endpoint to detach.
 * @return None.
 */
static void rdvz_edge_remove(kc_rdvz_peer_t *peer, kc_rdvz_edge_t *edge) {
    if (edge->previous) edge->previous->next = edge->next;
    else peer->edge_head = edge->next;
    if (edge->next) edge->next->previous = edge->previous;
    else peer->edge_tail = edge->previous;
}

/**
 * Releases a peer and its direct links, optionally emitting notifications.
 * @param rdvz Owner registry, already protected from callback mutations.
 * @param peer Peer being disconnected.
 * @param notify Nonzero to emit one event per removed link.
 * @return None.
 */
static void rdvz_drop_peer(kc_rdvz_t *rdvz, kc_rdvz_peer_t *peer, int notify) {
    while (peer->edge_head) {
        kc_rdvz_edge_t *edge = peer->edge_head;
        kc_rdvz_peer_t *other = edge->other;

        rdvz_edge_remove(peer, edge);
        rdvz_edge_remove(other, edge->reverse);
        free(edge->reverse);
        free(edge);
        if (notify && rdvz->options.on_disconnect) {
            rdvz->options.on_disconnect(peer, other, rdvz->options.userdata);
        }
    }
    rdvz_erase(rdvz, peer);
    peer->owner = NULL;
    free(peer);
}

/**
 * Opens an independent peer rendezvous registry.
 * @param out Destination for the registry.
 * @param options Optional link notifications.
 * @return Public status code.
 */
int kc_rdvz_open(kc_rdvz_t **out, const kc_rdvz_options_t *options) {
    kc_rdvz_t *rdvz;

    if (!out) return KC_RDVZ_EINVAL;
    *out = NULL;
    rdvz = (kc_rdvz_t *)calloc(1U, sizeof(*rdvz));
    if (!rdvz) return KC_RDVZ_ENOMEM;
    rdvz->bucket_count = KC_RDVZ_INITIAL_BUCKETS;
    rdvz->buckets = (kc_rdvz_peer_t **)calloc(rdvz->bucket_count,
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
 * Creates a peer, optionally linking it directly to an existing peer.
 * @param rdvz Registry.
 * @param target_id Optional existing peer ID.
 * @param out Destination for new peer.
 * @return Public status code.
 */
int kc_rdvz_connect(kc_rdvz_t *rdvz, const char *target_id,
    kc_rdvz_peer_t **out)
{
    kc_rdvz_peer_t *target = NULL;
    kc_rdvz_peer_t *peer;
    kc_rdvz_edge_t *edge = NULL;
    kc_rdvz_edge_t *reverse = NULL;
    uint64_t target_serial;

    if (!out) return KC_RDVZ_EINVAL;
    *out = NULL;
    if (!rdvz) return KC_RDVZ_EINVAL;
    if (rdvz->busy) return KC_RDVZ_EBUSY;
    if (target_id) {
        if (!rdvz_parse_id(target_id, &target_serial))
            return KC_RDVZ_EINVAL;
        target = rdvz_find(rdvz, target_serial);
        if (!target) return KC_RDVZ_ENOENT;
    }
    if (rdvz->next_id == UINT64_MAX) return KC_RDVZ_ELIMIT;
    peer = (kc_rdvz_peer_t *)calloc(1U, sizeof(*peer));
    if (!peer) return KC_RDVZ_ENOMEM;
    if (target) {
        edge = (kc_rdvz_edge_t *)calloc(1U, sizeof(*edge));
        reverse = (kc_rdvz_edge_t *)calloc(1U, sizeof(*reverse));
        if (!edge || !reverse) {
            free(edge);
            free(reverse);
            free(peer);
            return KC_RDVZ_ENOMEM;
        }
    }
    if (!rdvz_reserve(rdvz)) {
        free(edge);
        free(reverse);
        free(peer);
        return KC_RDVZ_ENOMEM;
    }

    peer->owner = rdvz;
    peer->serial = ++rdvz->next_id;
    (void)snprintf(peer->id, sizeof(peer->id), "%" PRIu64, peer->serial);
    rdvz_insert(rdvz, peer);
    if (target) {
        edge->other = target;
        reverse->other = peer;
        edge->reverse = reverse;
        reverse->reverse = edge;
        rdvz_edge_append(peer, edge);
        rdvz_edge_append(target, reverse);
    }
    *out = peer;
    if (target && rdvz->options.on_connect) {
        rdvz->busy++;
        rdvz->options.on_connect(peer, target, rdvz->options.userdata);
        rdvz->busy--;
    }
    return KC_RDVZ_OK;
}

/**
 * Exposes a peer's registry-unique identifier.
 * @param peer Live peer.
 * @return Borrowed canonical decimal identifier.
 */
const char *kc_rdvz_peer_id(const kc_rdvz_peer_t *peer) {
    return peer ? peer->id : NULL;
}

/**
 * Lists all live peers in connection order.
 * @param rdvz Registry.
 * @param visit Required callback for each peer.
 * @param userdata Visitor context.
 * @return Public status code.
 */
int kc_rdvz_list(kc_rdvz_t *rdvz, kc_rdvz_visit_fn visit, void *userdata) {
    kc_rdvz_peer_t *peer;

    if (!rdvz || !visit) return KC_RDVZ_EINVAL;
    rdvz->busy++;
    for (peer = rdvz->head; peer; peer = peer->next)
        visit(peer, userdata);
    rdvz->busy--;
    return KC_RDVZ_OK;
}

/**
 * Lists the direct neighbors of a live peer.
 * @param peer Source peer.
 * @param visit Required callback.
 * @param userdata Visitor context.
 * @return Public status code.
 */
int kc_rdvz_peer_list(const kc_rdvz_peer_t *peer,
    kc_rdvz_visit_fn visit, void *userdata)
{
    kc_rdvz_edge_t *edge;
    kc_rdvz_t *rdvz;

    if (!peer || !visit || !peer->owner) return KC_RDVZ_EINVAL;
    rdvz = peer->owner;
    rdvz->busy++;
    for (edge = peer->edge_head; edge; edge = edge->next)
        visit(edge->other, userdata);
    rdvz->busy--;
    return KC_RDVZ_OK;
}

/**
 * Removes one peer and all its direct links.
 * @param peer Live peer.
 * @return Public status code.
 */
int kc_rdvz_disconnect(kc_rdvz_peer_t *peer) {
    kc_rdvz_t *rdvz;

    if (!peer || !peer->owner) return KC_RDVZ_EINVAL;
    rdvz = peer->owner;
    if (rdvz->busy) return KC_RDVZ_EBUSY;
    rdvz->busy++;
    rdvz_drop_peer(rdvz, peer, 1);
    rdvz->busy--;
    return KC_RDVZ_OK;
}

/**
 * Releases a registry and its remaining peer relationships.
 * @param rdvz Registry, or NULL.
 * @return Public status code.
 */
int kc_rdvz_close(kc_rdvz_t *rdvz) {
    if (!rdvz) return KC_RDVZ_OK;
    if (rdvz->busy) return KC_RDVZ_EBUSY;
    rdvz->busy++;
    while (rdvz->head) rdvz_drop_peer(rdvz, rdvz->head, 0);
    free(rdvz->buckets);
    free(rdvz);
    return KC_RDVZ_OK;
}

/**
 * Explains a public status code.
 * @param status Status code.
 * @return Stable static description.
 */
const char *kc_rdvz_strerror(int status) {
    switch (status) {
        case KC_RDVZ_OK: return "success";
        case KC_RDVZ_ENOENT: return "peer not found";
        case KC_RDVZ_EINVAL: return "invalid argument";
        case KC_RDVZ_ENOMEM: return "out of memory";
        case KC_RDVZ_EBUSY: return "registry busy";
        case KC_RDVZ_ELIMIT: return "peer ID limit reached";
        default: return "unknown error";
    }
}

/**
 * Reports the build version.
 * @return Build version.
 */
uint64_t kc_rdvz_version(void) {
    return (uint64_t)KC_RDVZ_BUILD_VERSION;
}
