# tpm.c - Text Profile Matcher

`tpm.c` tells you how similar a piece of text is to a reference text. Create a
profile from representative samples, then score any input from 0.0 (dissimilar)
to 1.0 (very similar).

---
## CLI

### Example

Create a map file with Python code:

```bash
cat > python.map <<EOF
def hello():
    print("hello world")
```

Score Python input against it:

```bash
echo "def foo(): pass" | tpm python.map
0.999992
```

### Demo: use cases

Create profiles for different domains and compare how inputs score against
matching vs mismatching profiles.

**Natural language - English vs Spanish:**

```
cat > english.map <<EOF
The quick brown fox jumps over the lazy dog.
Pack my box with five dozen liquor jugs.
The five boxing wizards jump quickly.
EOF

cat > spanish.map <<EOF
El zorro marron salta sobre el perro perezoso.
Los exploradores descubrieron una nueva especie.
Las civilizaciones antiguas construyeron estructuras magnificas.
EOF
```

| Input | vs english.map | vs spanish.map |
| :---- | :------------: | :------------: |
| "The quick brown fox jumps over the lazy dog near the river bank." | 0.445819 | 0.000334 |
| "El zorro marron salta sobre el perro perezoso cerca del arroyo." | 0.001779 | 0.131115 |

Tells Python and JS syntax apart. Python input scores higher on the Python
profile than on the JS profile.

**Log format - Apache vs Syslog:**

Distinguishes HTTP access logs from system logs by their line structure.

| Input | vs apache.map | vs syslog.map |
| :---- | :-----------: | :-----------: |
| '127.0.0.1 - admin [12/May/2026:08:30:00 +0000] "GET /dashboard HTTP/1.1" 200 4567' | 0.390322 | 0.000875 |
| 'May 12 08:30:00 laptop kernel: PCI device enabled for power management' | 0.001641 | 0.016910 |

### Parameters

| Flag | Description |
| :--- | :--- |
| `-n <size>` | N-gram size (default 3, max 8) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

### Input

Map file: one or more lines of representative text. Stdin: text to score.

### Output

A single line with the score formatted to six decimal places:

```
0.763264
```

### Exit codes

| Code | Meaning |
| :--- | :------ |
| 0 | Success |
| 1 | Error (missing map, invalid args, I/O failure, profile build error) |

---
## Public API

### Types

```c
typedef struct kc_tpm kc_tpm_t;

typedef struct {
    const int *ngram_size;
} kc_tpm_options_t;
```

### Status codes

| Symbol | Value |
| :----- | :---- |
| `KC_TPM_OK` | 0 |
| `KC_TPM_ERROR` | -1 |

### Functions

| Function | Returns | Description |
| :------- | :------ | :---------- |
| `kc_tpm_open(out, map_text, options)` | `int` | Create a ready-to-score profile. An omitted n-gram option uses size 3. |
| `kc_tpm_score(tpm, input_text, out_score)` | `int` | Score input text against the profile and write a 0.0-1.0 result to `out_score`. |
| `kc_tpm_close(tpm)` | `void` | Release the profile. |
| `kc_tpm_version(void)` | `uint64_t` | Return the build version timestamp. |

### Lifecycle

```c
kc_tpm_t *profile = NULL;
double score;

if (kc_tpm_open(&profile, map_text, NULL) == KC_TPM_OK) {
    if (kc_tpm_score(profile, input_text, &score) == KC_TPM_OK) {
        /* use score */
    }
    kc_tpm_close(profile);
}
```

A successful `kc_tpm_open()` always returns a complete profile that can be
scored immediately and repeatedly. There is no separate build phase.

The n-gram size defaults to 3 and accepts values from 1 through 8.

`kc_tpm_score()` returns `KC_TPM_ERROR` for invalid arguments, allocation
failure, or input-profile capacity overflow. A valid empty input or a
successfully created empty profile returns `KC_TPM_OK` with a score of
`0.0`.

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

- Artifact: `bin/wasm32/wasm/tpm.wasm`
- Exports: `kc_tpm_score`, `kc_tpm_open`, `kc_tpm_close`, `kc_tpm_version`
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
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Test Dependencies

- No additional test dependencies required.

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
