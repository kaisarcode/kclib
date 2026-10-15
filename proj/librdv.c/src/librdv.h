/**
 * librdv.h - Generic rendezvous registry.
 * Summary: Public API for named publications and independent seekers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_RDV_H
#define KC_RDV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_rdv kc_rdv_t;
typedef struct kc_rdv_point kc_rdv_point_t;
typedef struct kc_rdv_seeker kc_rdv_seeker_t;

#define KC_RDV_OK 0
#define KC_RDV_ENOENT -1
#define KC_RDV_EEXIST -2
#define KC_RDV_EINVAL -3
#define KC_RDV_ENOMEM -4
#define KC_RDV_ELIMIT -5

/**
 * Configures storage limits and expiry policy.
 *
 * A zero count limit means unlimited. A zero TTL disables expiry for that
 * object type. Expiry is applied only when kc_rdv_prune() is called.
 */
typedef struct {
    size_t max_publications;
    size_t max_seekers;
    size_t max_seekers_per_point;
    uint64_t publication_ttl_ms;
    uint64_t seeker_ttl_ms;
} kc_rdv_options_t;

/**
 * Read-only view of one published value.
 */
typedef struct {
    const void *data;
    size_t data_size;
} kc_rdv_publication_t;

/**
 * Read-only view of one seeker in a rendezvous point.
 *
 * Seekers remain in insertion order until removed or pruned. previous and next
 * are traversal links only; the library owns the list structure.
 */
struct kc_rdv_seeker {
    const kc_rdv_point_t *point;
    const void *data;
    size_t data_size;
    const kc_rdv_seeker_t *previous;
    const kc_rdv_seeker_t *next;
};

/**
 * Read-only view of one named rendezvous point.
 *
 * A point may contain seekers with no publication, a publication with no
 * seekers, or both.
 */
struct kc_rdv_point {
    const char *id;
    const kc_rdv_publication_t *publication;
    const kc_rdv_seeker_t *head;
    const kc_rdv_seeker_t *tail;
    size_t seeker_count;
};

/**
 * Creates an independent rendezvous registry.
 * @param out Destination registry pointer.
 * @param options Optional storage policy. NULL selects unlimited storage and no
 *                expiry.
 * @return KC_RDV_OK or a negative status code.
 */
int kc_rdv_open(kc_rdv_t **out, const kc_rdv_options_t *options);

/**
 * Returns the current point for one identifier.
 * @param rdv Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @return Borrowed point view, or NULL when no publication or seeker exists.
 */
const kc_rdv_point_t *kc_rdv_get(kc_rdv_t *rdv, const char *id);

/**
 * Publishes one value at an identifier.
 *
 * Publishing does not modify existing seekers. Only one publication may exist
 * at a point; publishing an already-published identifier returns KC_RDV_EEXIST.
 * The data bytes are copied by the library.
 *
 * @param rdv Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @param data Publication bytes, or NULL when data_size is zero.
 * @param data_size Publication byte count.
 * @param out Optional destination for the resulting borrowed point view.
 * @return KC_RDV_OK or a negative status code.
 */
int kc_rdv_publish(kc_rdv_t *rdv, const char *id,
    const void *data, size_t data_size, const kc_rdv_point_t **out);

/**
 * Removes only the publication at an identifier.
 *
 * Existing seekers remain attached to the rendezvous point and may later meet
 * a new publication of the same identifier.
 *
 * @param rdv Open registry.
 * @param id Published identifier.
 * @return KC_RDV_OK, KC_RDV_ENOENT, or KC_RDV_EINVAL.
 */
int kc_rdv_unpublish(kc_rdv_t *rdv, const char *id);

/**
 * Adds one seeker to an identifier.
 *
 * Seeking succeeds even when no publication currently exists. The data bytes
 * are copied by the library. The returned seeker exposes its point, so a caller
 * can inspect point->publication immediately when present.
 *
 * @param rdv Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @param data Seeker bytes, or NULL when data_size is zero.
 * @param data_size Seeker byte count.
 * @param out Destination for the borrowed seeker view.
 * @return KC_RDV_OK or a negative status code.
 */
int kc_rdv_seek(kc_rdv_t *rdv, const char *id,
    const void *data, size_t data_size, const kc_rdv_seeker_t **out);

/**
 * Removes one seeker without affecting the publication or other seekers.
 * @param rdv Open registry that owns seeker.
 * @param seeker Borrowed seeker returned by kc_rdv_seek() or point traversal.
 * @return KC_RDV_OK or KC_RDV_EINVAL.
 */
int kc_rdv_remove(kc_rdv_t *rdv, const kc_rdv_seeker_t *seeker);

/**
 * Applies configured TTLs and removes objects whose expiry time has elapsed.
 *
 * Publication expiry does not remove seekers. Seeker expiry does not remove a
 * publication or other seekers. Empty points are reclaimed automatically.
 *
 * @param rdv Open registry.
 * @return Number of publications plus seekers removed, or zero for NULL.
 */
size_t kc_rdv_prune(kc_rdv_t *rdv);

/**
 * Releases a rendezvous registry and all stored publications and seekers.
 * @param rdv Registry pointer. NULL is a safe no-op.
 * @return None.
 */
void kc_rdv_close(kc_rdv_t *rdv);

/**
 * Returns a stable textual description for one public status code.
 * @param status Public status code.
 * @return Static NUL-terminated description.
 */
const char *kc_rdv_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_rdv_version(void);

#ifdef __cplusplus
}
#endif

#endif
