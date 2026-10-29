# libhnsw.c - HNSW vector search

`libhnsw.c` provides an in-memory HNSW approximate-nearest-neighbor index. The
caller supplies vectors and identifiers, explicitly builds the graph, and then
searches it with L2, cosine, or inner-product scoring.

## Quick start

```c
#include "libhnsw.h"

kc_hnsw_options_t options = {
    .dimension = 384
};
kc_hnsw_t *index = NULL;

if (kc_hnsw_open(&index, &options) == KC_HNSW_OK) {
    kc_hnsw_add(index, "item-1", values);
    kc_hnsw_build(index);

    kc_hnsw_result_t *results = NULL;
    size_t count = 0;

    kc_hnsw_search(index, query, 5, -1.0, &results, &count);
    kc_hnsw_free(results);
    kc_hnsw_close(index);
}
```

Adding vectors after a build invalidates the graph. Build again before the next
search.

## Documentation

Read the [index lifecycle](doc/index-lifecycle.md) for options, metrics,
ownership, rebuild behavior, and concurrent searching.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Notice

No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
