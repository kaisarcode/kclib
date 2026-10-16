# libtpm.c - Text profile matcher

`libtpm.c` creates a reusable text profile from representative text and scores
new text against that profile from `0.0` to `1.0`.

## Quick start

```c
#include "libtpm.h"

kc_tpm_t *profile = NULL;
double score = 0.0;

if (kc_tpm_open(&profile, reference_text, NULL) == KC_TPM_OK) {
    kc_tpm_score(profile, input_text, &score);
    kc_tpm_close(profile);
}
```

A successful open returns a profile that is ready to score repeatedly. The
n-gram size defaults to 3 and accepts values from 1 through 8.

A valid empty input or successfully created empty profile scores `0.0`.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets. See the
[repository build guide](../../README.md#build) for shared toolchain and target
information.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
