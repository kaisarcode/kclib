# lng.c - Language Detection Library

`lng.c` is a minimalist C library and CLI for detecting the language of the given text using internal language profiles. It is designed as a composable native primitive for the KaisarCode ecosystem.

---

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

---

## Public API

Stateless detection over heap-allocated results. No handle or explicit initialization required - internal language profiles are initialized automatically exactly once with platform once-control and remain read-only thereafter, safe for concurrent detection.

Scores are heuristic ranking values in `[0, 1]` shaped for thresholding, not calibrated probabilities.

```c
#include "liblng.h"

#define KC_LNG_OK 0
#define KC_LNG_ERROR -1

typedef struct {
    const char *code;  // borrowed static storage, do not free or modify
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

- `kc_lng_detect` - stateless detection. Sanitizes/normalizes text internally, scores against all compiled profiles, sorts descending, filters below the threshold, and bounds output by the limit. Threshold defaults internally to `0.001` when omitted; limit defaults internally to `1` when omitted. `text == NULL` yields `KC_LNG_ERROR`; `text[0] == '\0'` yields zero results (`KC_LNG_OK` with `*out_count == 0`, `*out_results == NULL`). On success `*out_results` points to a heap-allocated array owned by the caller; on zero results or error `*out_results` is set to `NULL`. Returns `KC_LNG_OK` on success (including zero matches), `KC_LNG_ERROR` on invalid arguments or allocation failure.
- Ownership - array returned via `out_results` is heap-allocated and caller-owned; release with `kc_lng_free()`. Each `code` string points to static library-owned storage; caller must not free or modify it. Detection does not retain input.
- `kc_lng_free` - release memory allocated by `kc_lng_detect`. `NULL` safe (no-op).
- `kc_lng_version` - returns build version as Unix timestamp.
- `kc_lng_options_t` represents independently optional configuration with nullable pointers. `threshold == NULL` uses `0.001`; `limit == NULL` uses `1`. Non-NULL pointers are explicit and interpreted literally. A pointer to threshold `0.0` is valid; a pointer to limit `0` is invalid. Limits above 32 are clamped to 32.
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
        // results[i].code is borrowed static storage; results[i].score is heuristic
    }
}

kc_lng_free(results);
```

### Lifecycle

No explicit initialization required. Internal profiles are initialized automatically on first detection, exactly once, in a thread-safe manner. Once initialized, profile state remains read-only and safe for concurrent detection without additional synchronization. Results are sorted by descending heuristic score. Scores remain heuristic ranking values, not probabilities or confidence percentages.

---

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
