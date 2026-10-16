# libngram.c - Sliding-window n-gram traversal

`libngram.c` traverses descending token windows over byte-delimited text. It is
stateless and reentrant; traversal state belongs to one call.

## Quick start

```c
#include "libngram.h"

static int visit(
    const kc_ngram_chunk_t *chunk,
    void *userdata
) {
    (void)userdata;
    printf("%.*s\n", (int)chunk->data_size, chunk->data);
    return 0;
}

size_t max_tokens = 3;
kc_ngram_options_t options = {
    .max_tokens = &max_tokens
};

kc_ngram_traverse(
    "The quick brown fox",
    &options,
    visit,
    NULL
);
```

Windows are visited from largest to smallest and left to right within the same
size. Visitor return value `0` continues, `1` closes the current span, and a
negative value aborts traversal.

## Options

The default maximum is 10 tokens, the minimum is 1 token, and the default
separator set is ASCII whitespace. A maximum of zero means all available
tokens.

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
