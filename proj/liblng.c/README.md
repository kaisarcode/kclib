# liblng.c - Language detection

`liblng.c` detects the language of text and returns ranked matches. Detection is
stateless and uses local heuristic scoring with no network, external model, or
persistent state.

## Quick start

```c
#include "liblng.h"

kc_lng_result_t *results = NULL;
size_t count = 0;
size_t limit = 3;
kc_lng_options_t options = {
    .limit = &limit
};

if (kc_lng_detect(
        "Hello world",
        &options,
        &results,
        &count
    ) == KC_LNG_OK) {
    kc_lng_free(results);
}
```

Scores are ranking values in the range `[0, 1]`, not calibrated probabilities.
Results are sorted from highest to lowest score. Zero matches is a successful
result.

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
