# ngram.c - Sliding-window n-gram traversal

\`ngram.c\` is a small stateless C library and CLI for descending sliding-window traversal over byte-delimited text tokens. The reusable library emits borrowed spans through a synchronous callback. The CLI can additionally run one command per emitted span.

## CLI

Traverse positional text:

\`\`\`bash
./bin/x86_64/linux/ngram "The quick brown fox"
\`\`\`

Read the complete input from stdin:

\`\`\`bash
echo "The quick brown fox" | ./bin/x86_64/linux/ngram
\`\`\`

Override traversal bounds or separator bytes:

\`\`\`bash
./bin/x86_64/linux/ngram --max 3 --min 2 --sep " ," "The quick brown fox"
\`\`\`

Run one command for each emitted span:

\`\`\`bash
./bin/x86_64/linux/ngram --cmd "grep fox" "The quick brown fox"
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

With \`--cmd\`, each chunk is printed before command evaluation. The command is parsed into argv and executed directly without a shell. The chunk plus a newline is written to the child stdin. Any child stdout closes that span; child exit status alone does not close it. Command plumbing failure makes the CLI fail.

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

### Options and defaults

Options are independently optional. The library owns the default values; bindings do not duplicate them.

| Option | Omitted value | Explicit behavior |
| :--- | :--- | :--- |
| \`max_tokens\` | \`10\` | \`0\` means use all available tokens; otherwise the maximum window size |
| \`min_tokens\` | \`1\` | Must be at least \`1\` |
| \`separators\` | \`" \t\r\n"\` | Byte set used to delimit tokens; an empty string means no separator bytes |

Passing \`options == NULL\` omits every option. Inside a non-NULL options value, a NULL optional field is also omitted. A non-NULL pointer is explicit and interpreted literally, so a pointer to \`max_tokens == 0\` is distinct from an omitted \`max_tokens\`.

There is no public \`options_default()\` function. Default policy stays inside the library.

### Traversal

\`kc_ngram_traverse()\` tokenizes the input using separator bytes, then visits windows from largest to smallest. Windows of the same size are visited left to right.

Each \`kc_ngram_chunk_t\` describes the original input span:

- \`data\` is a borrowed pointer to the first byte of the first token in the window;
- \`data_size\` is the exact byte span through the last byte of the last token, including separator bytes between tokens;
- \`token_start\` is the zero-based index of the first token;
- \`token_count\` is the number of tokens in the window.

The callback runs synchronously and is never retained. \`chunk\`, \`chunk->data\`, the input, the options, and \`userdata\` are borrowed only for the duration implied by the call. The library does not allocate an owned chunk value for the caller.

Visitor return semantics are deliberately numeric:

\`\`\`text
0   continue traversal
1   close this span
<0  abort traversal
\`\`\`

Returning \`1\` suppresses later candidate windows fully contained in that closed span. Returning a negative value stops traversal and makes \`kc_ngram_traverse()\` return \`KC_NGRAM_EABORT\`.

No additional public callback constants or namespace properties are defined.

Empty input is valid and returns \`KC_NGRAM_OK\` without invoking the callback.

### Scripting projection

The public header remains the canonical ABI. Generated cdef output preserves the exact public identifiers, including \`max_tokens\`, \`min_tokens\`, \`token_start\`, and \`token_count\`.

A JavaScript bridge can expose the same capability mechanically:

\`\`\`js
ngram.traverse(
    "uno dos tres cuatro",
    {
        max_tokens: 3,
        min_tokens: 2
    },
    chunk => {
        console.log(chunk.data);
        console.log(chunk.token_start);
        console.log(chunk.token_count);

        return 0;
    }
);
\`\`\`

Omitted properties map to omitted C option fields, so defaults remain native-library policy:

\`\`\`js
ngram.traverse(
    "uno dos tres",
    {},
    chunk => {
        console.log(chunk.data);
        return 0;
    }
);
\`\`\`

The equivalent Lua capability shape keeps the same public field names:

\`\`\`lua
ngram.traverse(
    "uno dos tres cuatro",
    {
        max_tokens = 3,
        min_tokens = 2
    },
    function(chunk)
        print(chunk.data)
        print(chunk.token_start)
        print(chunk.token_count)

        return 0
    end
)
\`\`\`

These examples describe the mechanical scripting projection of the public ABI. They do not add callback constants, camelCase field aliases, default-merging logic, or additional capability semantics.

## Build

Compiled artifacts are generated under \`bin/{arch}/{platform}/\`.

\`\`\`bash
make
\`\`\`

Build all configured targets:

\`\`\`bash
make all
\`\`\`

## Tests

Build the project artifacts before running the portable contract suite:

\`\`\`bash
make
make test
\`\`\`

Windows-through-Wine validation:

\`\`\`bash
make x86_64/windows
make test wine
\`\`\`

The native and Wine suites contain the reusable public-API cases plus exactly one grouped \`kc_ngram_cli\` case. That CLI case covers help/version aliases, positional and stdin input, defaults, min/max/separator flags and aliases, explicit max zero, diagnostics and exit codes, stdout-based \`--cmd\` closure, and literal argv behavior without shell evaluation.

## WebAssembly

WASM applicability is required: the reusable traversal capability has no native-OS dependency.

Build:

\`\`\`bash
make wasm32/wasm
\`\`\`

Validate:

\`\`\`bash
make test wasm
\`\`\`

Artifact:

\`\`\`text
bin/wasm32/wasm/ngram.wasm
\`\`\`

The reusable module exports:

\`\`\`text
kc_ngram_traverse
kc_ngram_version
\`\`\`

The CLI is not part of the WASM module or WASM contract test. The reusable traversal tests run under Emscripten/Node.

## Platform and tooling requirements

The project uses C11, CMake 3.14+, Ninja, and a supported C compiler. No additional system library is required for the reusable traversal capability.

Optional cross-platform toolchains are required only for their respective targets: MinGW/Wine for Windows validation, Emscripten/Node for WASM, osxcross for Apple targets, and the Android NDK for Android.
