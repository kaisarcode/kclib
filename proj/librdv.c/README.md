# librdv.c - Generic Rendezvous Registry

`librdv.c` maintains named rendezvous points with one optional publication and
an independent ordered collection of seekers. It stores opaque bytes only and
has no transport, networking, authentication, command, or process semantics.

A seeker may arrive before a publication exists. Removing a publication does
not remove its seekers, so a later publication of the same identifier can meet
participants that are still waiting.

## Public API

```c
#include "librdv.h"

kc_rdv_t *rdv = NULL;
const kc_rdv_seeker_t *seeker = NULL;
const kc_rdv_point_t *point = NULL;

kc_rdv_open(&rdv, NULL);

kc_rdv_seek(rdv, "room", "consumer", 8, &seeker);
kc_rdv_publish(rdv, "room", "publisher", 9, &point);

for (const kc_rdv_seeker_t *it = point->head; it; it = it->next) {
    /* inspect it->data and decide what this application should do */
}

kc_rdv_remove(rdv, seeker);
kc_rdv_unpublish(rdv, "room");
kc_rdv_close(rdv);
```

`kc_rdv_publish()` stores one publication for an identifier. Publishing the same
identifier again while it is still published returns `KC_RDV_EEXIST`.

`kc_rdv_seek()` always adds a seeker, whether or not a publication currently
exists. Each seeker exposes its rendezvous point, so callers can inspect the
current publication immediately when present.

`kc_rdv_unpublish()` removes only the publication. `kc_rdv_remove()` removes
only the selected seeker.

Publication and seeker data are copied and owned by the library. The public
point and seeker structures are read-only views; list links are exposed for
ordinary C traversal while structural mutation remains library-owned.

## Limits and expiry

`kc_rdv_options_t` can limit publications, total seekers, and seekers per point.
Zero means unlimited.

Publication and seeker TTLs are independent. Zero disables expiry. Expiry is
explicit: `kc_rdv_prune()` removes objects whose configured TTL elapsed.
Expiring or unpublishing a publication never removes seekers, and expiring or
removing a seeker never removes a publication or sibling seekers.

The C implementation indexes points by identifier and keeps per-point seeker
head, tail, and count state, so normal identifier lookup, append, and known
seeker removal do not require scanning all stored rendezvous state.

## Build

```bash
make
make test
```

Artifacts are written under `bin/{arch}/{platform}/` for the host build.

## Development requirements

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- a C11 compiler

## Beta notice

This is a beta project tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You are free to test it, use it,
and modify it.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
