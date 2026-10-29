# libmmap.c - Persistent binary value

`libmmap.c` manages one file-backed binary value per instance. A missing file is
a valid starting state; callers can replace the in-memory value, persist it
explicitly, or delete the stored value and backing file.

## Quick start

```c
#include "libmmap.h"

kc_mmap_t *value = NULL;

if (kc_mmap_open(&value, "state.bin") == KC_MMAP_OK) {
    if (kc_mmap_set(value, "A", 1) == KC_MMAP_OK) {
        kc_mmap_save(value);
    }

    kc_mmap_close(value);
}
```

`kc_mmap_get()` distinguishes a missing value from an existing zero-byte value.
After `kc_mmap_del()`, the instance is invalid for operations other than final
cleanup.

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
