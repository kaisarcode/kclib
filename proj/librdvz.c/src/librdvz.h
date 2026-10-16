/**
 * librdvz.h - Generic rendezvous registry.
 * Summary: Public API for named publications and independent seekers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_RDVZ_H
#define KC_RDVZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_rdvz kc_rdvz_t;
typedef struct kc_rdvz_point kc_rdvz_point_t;
typedef struct kc_rdvz_seeker kc_rdvz_seeker_t;

#define KC_RDVZ_OK 0
#define KC_RDVZ_ENOENT -1
#define KC_RDVZ_EEXIST -2
#define KC_RDVZ_EINVAL -3
#define KC_RDVZ_ENOMEM -4
#define KC_RDVZ_ELIMIT -5

/**
 * Configures storage limits and expiry policy.
 *
 * A zero count limit means unlimited. A zero TTL disables expiry for that
 * object type. Expiry is applied only when kc_rdvz_prune() is called.
 */
typedef struct {
    size_t max_publications;
    size_t max_seekers;
    size_t max_seekers_per_point;
    uint64_t publication_ttl_ms;
    uint64_t seeker_ttl_ms;
} kc_rdvz_options_t;

/**
 * Read-only view of one published value.
 */
typedef struct {
    const void *data;
    size_t data_size;
} kc_rdvz_publication_t;

/**
 * Read-only view of one seeker in a rendezvous point.
 *
 * Seekers remain in insertion order until removed or pruned. previous and next
 * are traversal links only; the library owns the list structure.
 */
struct kc_rdvz_seeker {
    const kc_rdvz_point_t *point;
    const void *data;
    size_t data_size;
    const kc_rdvz_seeker_t *previous;
    const kc_rdvz_seeker_t *next;
};

/**
 * Read-only view of one named rendezvous point.
 *
 * A point may contain seekers with no publication, a publication with no
 * seekers, or both.
 */
struct kc_rdvz_point {
    const char *id;
    const kc_rdvz_publication_t *publication;
    const kc_rdvz_seeker_t *head;
    const kc_rdvz_seeker_t *tail;
    size_t seeker_count;
};

/**
 * Creates an independent rendezvous registry.
 * @param out Destination registry pointer.
 * @param options Optional storage policy. NULL selects unlimited storage and no
 *                expiry.
 * @return KC_RDVZ_OK or a negative status code.
 */
int kc_rdvz_open(kc_rdvz_t **out, const kc_rdvz_options_t *options);

/**
 * Returns the current point for one identifier.
 * @param rdvz Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @return Borrowed point view, or NULL when no publication or seeker exists.
 */
const kc_rdvz_point_t *kc_rdvz_get(kc_rdvz_t *rdvz, const char *id);

/**
 * Publishes one value at an identifier.
 *
 * Publishing does not modify existing seekers. Only one publication may exist
 * at a point; publishing an already-published identifier returns KC_RDVZ_EEXIST.
 * The data bytes are copied by the library.
 *
 * @param rdvz Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @param data Publication bytes, or NULL when data_size is zero.
 * @param data_size Publication byte count.
 * @param out Optional destination for the resulting borrowed point view.
 * @return KC_RDVZ_OK or a negative status code.
 */
int kc_rdvz_publish(kc_rdvz_t *rdvz, const char *id,
    const void *data, size_t data_size, const kc_rdvz_point_t **out);

/**
 * Removes only the publication at an identifier.
 *
 * Existing seekers remain attached to the rendezvous point and may later meet
 * a new publication of the same identifier.
 *
 * @param rdvz Open registry.
 * @param id Published identifier.
 * @return KC_RDVZ_OK, KC_RDVZ_ENOENT, or KC_RDVZ_EINVAL.
 */
int kc_rdvz_unpublish(kc_rdvz_t *rdvz, const char *id);

/**
 * Adds one seeker to an identifier.
 *
 * Seeking succeeds even when no publication currently exists. The data bytes
 * are copied by the library. The returned seeker exposes its point, so a caller
 * can inspect the point publication immediately when present.
 *
 * @param rdvz Open registry.
 * @param id Arbitrary NUL-terminated identifier.
 * @param data Seeker bytes, or NULL when data_size is zero.
 * @param data_size Seeker byte count.
 * @param out Destination for the borrowed seeker view.
 * @return KC_RDVZ_OK or a negative status code.
 */
int kc_rdvz_seek(kc_rdvz_t *rdvz, const char *id,
    const void *data, size_t data_size, const kc_rdvz_seeker_t **out);

/**
 * Removes one seeker without affecting the publication or other seekers.
 * @param rdvz Open registry that owns seeker.
 * @param seeker Borrowed seeker returned by kc_rdvz_seek() or point traversal.
 * @return KC_RDVZ_OK or KC_RDVZ_EINVAL.
 */
int kc_rdvz_remove(kc_rdvz_t *rdvz, const kc_rdvz_seeker_t *seeker);

/**
 * Applies configured TTLs and removes objects whose expiry time has elapsed.
 *
 * Publication expiry does not remove seekers. Seeker expiry does not remove a
 * publication or other seekers. Empty points are reclaimed automatically.
 *
 * @param rdvz Open registry.
 * @return Number of publications plus seekers removed, or zero for NULL.
 */
size_t kc_rdvz_prune(kc_rdvz_t *rdvz);

/**
 * Releases a rendezvous registry and all stored publications and seekers.
 * @param rdvz Registry pointer. NULL is a safe no-op.
 * @return None.
 */
void kc_rdvz_close(kc_rdvz_t *rdvz);

/**
 * Returns a stable textual description for one public status code.
 * @param status Public status code.
 * @return Static NUL-terminated description.
 */
const char *kc_rdvz_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_rdvz_version(void);

#ifdef __cplusplus
}
#endif

#endif
