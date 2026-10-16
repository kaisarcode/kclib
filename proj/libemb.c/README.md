# libemb.c - Vector embeddings

`libemb.c` generates text embeddings with the embedded BGE small v1.5 model. It
has no network or hosted-service dependency and exposes the model dimension so
callers do not need to hardcode it.

## Quick start

```c
#include "libemb.h"

float *vector = NULL;
size_t count = 0;

if (kc_emb_embed("The quick brown fox", &vector, &count) == KC_EMB_OK) {
    size_t dimension = kc_emb_dimension();

    if (count == dimension) {
        /* Use the embedding while vector is owned by the caller. */
    }
}

kc_emb_free(vector);
```

The current embedded model produces 384-element vectors. Treat
`kc_emb_dimension()` as the source of truth instead of depending on that value.

## Model

The embedded model is
[BAAI/bge-small-en-v1.5](https://huggingface.co/BAAI/bge-small-en-v1.5), from
the [FlagEmbedding project](https://github.com/FlagOpen/FlagEmbedding). The
upstream model is distributed under the MIT License.

The local GGUF model is linked into project artifacts and is not downloaded at
runtime.

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
