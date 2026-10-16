# libpow.c - Proof-of-work challenges

`libpow.c` creates, solves, and verifies portable proof-of-work challenges.
Challenges and solutions are self-contained binary values. The library provides
proof of computational work, not identity, freshness, transport, or admission
policy.

## Quick start

```c
#include "libpow.h"

void *challenge = NULL;
void *solution = NULL;
size_t challenge_size = 0;
size_t solution_size = 0;

if (kc_pow_challenge(
        1000000,
        &challenge,
        &challenge_size
    ) == KC_POW_OK &&
    kc_pow_solve(
        challenge,
        challenge_size,
        &solution,
        &solution_size
    ) == KC_POW_OK) {
    kc_pow_verify(
        challenge,
        challenge_size,
        solution,
        solution_size
    );
}

kc_pow_free(solution);
kc_pow_free(challenge);
```

The effort value is an approximate expected number of nonce attempts, not a
time duration. `kc_pow_size()` can determine one encoded entity's total size
from its leading bytes.

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
