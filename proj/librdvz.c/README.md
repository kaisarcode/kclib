# librdvz.c - Rendezvous registry

`librdvz.c` maintains named rendezvous points with one optional publication and
an ordered collection of seekers. It stores opaque bytes only and does not
impose networking, authentication, command, or process semantics.

## Quick start

```c
#include "librdvz.h"

kc_rdvz_t *rdvz = NULL;
const kc_rdvz_seeker_t *seeker = NULL;
const kc_rdvz_point_t *point = NULL;

if (kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK) {
    kc_rdvz_seek(rdvz, "room", "consumer", 8, &seeker);
    kc_rdvz_publish(rdvz, "room", "publisher", 9, &point);

    kc_rdvz_remove(rdvz, seeker);
    kc_rdvz_unpublish(rdvz, "room");
    kc_rdvz_close(rdvz);
}
```

Seekers may exist before a publication. Unpublishing a point does not remove its
seekers, and removing a seeker does not remove the publication.

## Limits and expiry

Limits for publications and seekers are optional; zero means unlimited.
Publication and seeker TTLs are independent. Expiry occurs when
`kc_rdvz_prune()` is called.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
