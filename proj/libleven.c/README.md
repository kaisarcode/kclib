# libleven.c - Levenshtein distance

`libleven.c` calculates the edit distance between UTF-8 strings. It counts Unicode code points rather than bytes, uses memory proportional to the shorter input, and can stop early when a maximum distance is exceeded.

## Quick start

```c
#include "libleven.h"

size_t distance;
size_t maximum = 2;
int status = kc_leven_distance("kitten", "sitting", &maximum, &distance);

if (status == KC_LEVEN_ELIMIT) {
    /* Distance exceeds maximum; distance is maximum + 1. */
}
```

Omit the maximum argument (`NULL`) to compute the exact distance. In this example the distance exceeds 2, so the function returns `KC_LEVEN_ELIMIT` and writes 3. Invalid UTF-8 returns `KC_LEVEN_EUTF8`. The library does not normalize, fold case or accents, or apply language-specific rules.

The command-line tool accepts two quoted strings:

```sh
leven kitten sitting
leven kitten sitting --max 3
```

It prints the edit distance on success; a threshold exceedance or invalid input exits unsuccessfully.

## Build

From this project directory:

```sh
make
make test
```

Use `make all` to compile the configured cross-platform targets when the required toolchains are installed.

## Notice

No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
