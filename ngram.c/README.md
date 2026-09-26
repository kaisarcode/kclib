# ngram.c - Sliding-window n-gram traversal

\`ngram.c\` traverses descending sliding windows over byte-delimited text tokens. The CLI can also run one command for each emitted window.

---

## CLI

Traverse positional text:

\`\`\`bash
ngram "The quick brown fox"
\`\`\`

Read the complete input from stdin:

\`\`\`bash
echo "The quick brown fox" | ngram
\`\`\`

Override traversal bounds or separator bytes:

\`\`\`bash
ngram --max 3 --min 2 --sep " ," "The quick brown fox"
\`\`\`

Run one command for each emitted span:

\`\`\`bash
ngram --cmd "grep fox" "The quick brown fox"
\`\`\`

The CLI contract is unchanged:

| Flag | Description |
| :--- | :--- |
| \`--max\`, \`-max <n>\` | Maximum tokens per block |
| \`--min\`, \`-min <n>\` | Minimum tokens per block |
| \`--sep\`, \`-sep <s>\` | Custom separator characters |
| \`--cmd\`, \`-cmd <cmd>\` | Execute command for each chunk |
| \`--help\`, \`-h\` | Show help and usage |
| \`--version\`, \`-v\` | Show version |

---

## Public API

The library is stateless and reentrant. Traversal state belongs to one call.

\`\`\`c
#include "libngram.h"

#define KC_NGRAM_OK      0
#define KC_NGRAM_ERROR  -1
#define KC_NGRAM_EABORT -2

typedef struct {
    const char *data;
    size_t data_size;
    size_t token_start;
    size_t token_count;
} kc_ngram_chunk_t;

typedef struct {
    const size_t *max_tokens;
    const size_t *min_tokens;
    const char *separators;
} kc_ngram_options_t;

typedef int (*kc_ngram_visit_fn)(
    const kc_ngram_chunk_t *chunk,
    void *userdata
);

int kc_ngram_traverse(
    const char *input,
    const kc_ngram_options_t *options,
    kc_ngram_visit_fn visit,
    void *userdata
);

uint64_t kc_ngram_version(void);
\`\`\`

A normal C call can override only the options it needs:

\`\`\`c
static int visit(const kc_ngram_chunk_t *chunk, void *userdata) {
    (void)userdata;

printf("%.*s\n", (int)chunk->data_size, chunk->data);

return 0;
}

size_t max_tokens = 3;
kc_ngram_options_t options = {0};

options.max_tokens = &max_tokens;

int rc = kc_ngram_traverse(
    "The quick brown fox",
    &options,
    visit,
    NULL
);
\`\`\`

### Options

| Option | Default | Behavior |
| :--- | :--- | :--- |
| \`max_tokens\` | \`10\` | \`0\` means use all available tokens; otherwise the maximum window size |
| \`min_tokens\` | \`1\` | Must be at least \`1\` |
| \`separators\` | \`" \t\r\n"\` | Byte set used to delimit tokens; an empty string means no separator bytes |

There is no public \`options_default()\` function. Default policy stays inside the library.

### Traversal

\`kc_ngram_traverse()\` tokenizes the input using separator bytes, then visits windows from largest to smallest. Windows of the same size are visited left to right.

Each \`kc_ngram_chunk_t\` describes the original input span:

- \`data\` identifies the text covered by the current window;
- \`data_size\` is the exact byte span through the last byte of the last token, including separator bytes between tokens;
- \`token_start\` is the zero-based index of the first token;
- \`token_count\` is the number of tokens in the window.

Visitor return semantics are deliberately numeric:

\`\`\`text
0   continue traversal
1   close this span
<0  abort traversal
\`\`\`

Returning \`1\` suppresses later candidate windows fully contained in that closed span. Returning a negative value stops traversal and makes \`kc_ngram_traverse()\` return \`KC_NGRAM_EABORT\`.

No additional public callback constants or namespace properties are defined.

Empty input is valid and returns \`KC_NGRAM_OK\` without invoking the callback.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/ngram.wasm`
- Exports: `kc_ngram_traverse`, `kc_ngram_version`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains are required only by enabled targets.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
