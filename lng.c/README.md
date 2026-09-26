# lng.c - Language Detection Library

`lng.c` detects the language of text and returns ranked language matches.

---
## CLI

Detect the language of text provided as an argument or via standard input.

### Examples

Single language detection:

```bash
lng "Hello world"
```

Ranked detection with threshold and limit:

```bash
lng "Hello world" -l 3 -t 0.1
```

Standard input processing (one-shot):

```bash
printf 'hola mundo' | lng
```

EOF terminates the one-shot request and the process exits.

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `-t`, `--threshold <n>` | Minimum score threshold (default `0.001`) |
| `-l`, `--limit <n>` | Maximum number of results (default `1`) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

### Output

Results are printed as the language code (for single match) or code and score (for ranked results):

```
en
```

```
en: 0.9500
es: 0.0400
```

Defaults are threshold `0.001` and limit `1`. Limit `1` prints only the code; larger limits print `code: score` with four decimal places. Empty input produces no output.

---
## Public API

Language detection is stateless and requires no explicit initialization.

Scores are heuristic ranking values in `[0, 1]` shaped for thresholding, not calibrated probabilities.

```c
#include "liblng.h"

#define KC_LNG_OK 0
#define KC_LNG_ERROR -1

typedef struct {
    const char *code;
    double score;      // heuristic ranking value, not a probability
} kc_lng_result_t;

typedef struct {
    const double *threshold;
    const size_t *limit;
} kc_lng_options_t;

int kc_lng_detect(const char *text, const kc_lng_options_t *options,
                  kc_lng_result_t **out_results, size_t *out_count);
void kc_lng_free(void *ptr);
uint64_t kc_lng_version(void);
```

- `kc_lng_detect` detects and ranks matching languages. Threshold defaults to `0.001` and limit defaults to `1`.
- `kc_lng_free` releases detection results.
- `kc_lng_version` - returns build version as Unix timestamp.
- `kc_lng_options_t` configures threshold and result limit. Limits above 32 are clamped to 32.
- Threshold filters results; limit bounds output count. Zero matches is a successful result with count zero, not an error. No network, external model, or persistent state is required.

### Example

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
    for (size_t i = 0; i < count; i++) {
    }
}

kc_lng_free(results);
```

### Lifecycle

Results are sorted by descending heuristic score. Scores are ranking values, not probabilities or confidence percentages.

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

- Artifact: `bin/wasm32/wasm/lng.wasm`
- Exports: `kc_lng_free`, `kc_lng_detect`, `kc_lng_version`
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

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

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
