# llm.c - Local Generative Inference Library and CLI

`llm.c` is a C library for local generative inference, plus a thin command-line
tool that wraps the public API. It loads a local GGUF model once and exposes
message-oriented generation with optional model chat templating, LoRA adapters,
and multimodal image attachments. The CLI reads request content from stdin,
separated by the `--until` delimiter byte (default EOT 4), streams each
response to stdout, and sends diagnostics and errors to stderr.

---

## CLI

`MODEL` is the first positional argument. If it is missing, `KC_LLM_MODEL` is
used. Requests are read from stdin until the `--until` delimiter byte (EOT by default).
When stdin closes the process exits cleanly.

The CLI parses its arguments and stdin into the public API, calls
`kc_llm_open`/`kc_llm_generate_role`/`kc_llm_info_query`/`kc_llm_close`
directly, and prints results to stdout. Generation is streamed through a
callback so tokens appear as they are produced. Errors and diagnostics go to
stderr.

### Examples

Generate a user message from stdin:

```bash
echo 'hello' | llm model.gguf
```

```bash
cat prompt.txt | llm models/qwen.gguf --ctx 8192 --predict 256
```

Generate with an explicit role:

```bash
echo 'You are a concise assistant.' | llm model.gguf --role system
```

Disable model thinking when the template supports it:

```bash
echo 'Hello' | llm model.gguf --think 0
```

Enable MTP with a draft model:

```bash
echo 'Hello' | llm model.gguf \
  --mtp draft.gguf \
  --mtp-tokens 4 \
  --fattn 1
```

Apply a LoRA adapter:

```bash
cat prompt.txt | llm models/qwen.gguf \
  --lora adapters/style.gguf \
  --lora-scale 1.0
```

Apply multiple LoRA adapters:

```bash
cat prompt.txt | llm models/qwen.gguf \
  --lora adapters/style.gguf \
  --lora-scale 1.0 \
  --lora adapters/domain.gguf \
  --lora-scale 0.7
```

Use GPU offloading:

```bash
cat prompt.txt | llm models/qwen.gguf \
  --gpu -1 \
  --gpu-layers 35
```

Send multiple requests in one run using EOT (byte 4) as delimiter:

```bash
printf 'hello\004what is 2+2?\004' | llm models/qwen.gguf
```

Save and resume a conversation:

```bash
printf 'remember the code 7392\004' | llm models/qwen.gguf --kv-save chat.kv
printf 'what was the code?\004' | llm models/qwen.gguf --kv-load chat.kv
```

Multimodal inference with an image:

```bash
echo 'describe the image' | llm models/vision.gguf \
  --mmproj models/mmproj.gguf \
  --image image.png
```

Multiple image attachments:

```bash
echo 'compare these' | llm models/vision.gguf \
  --mmproj models/mmproj.gguf \
  --image a.png \
  --image b.png
```

Inspect static model fields without reading stdin:

```bash
llm model.gguf --info architecture,name,parameters
```

Inspect input-dependent fields without generating output:

```bash
printf 'hello\004' | llm model.gguf --info input-tokens,context-used,context-after
```

---

### Parameters

| Command/Flag | Description | Default |
| :--- | :--- | :--- |
| `MODEL` | Path to the GGUF model | `KC_LLM_MODEL` if set |
| `--ctx N` | Context size in tokens | llama.cpp default |
| `--predict N` | Maximum tokens to generate, `-1` for unlimited | -1 |
| `--threads N` | Number of threads | auto (online CPU cores) |
| `--gpu N` | GPU mode: -1 auto, 0 CPU, >0 require GPU | `-1` |
| `--gpu-layers N` | Layers to offload to GPU | all |
| `--seed N` | RNG seed, -1 for random | -1 |
| `--temp F` | Temperature | 0.80 |
| `--top-k N` | Top-k sampling | 40 |
| `--top-p F` | Top-p sampling | 0.95 |
| `--min-p F` | Min-p sampling | 0.0 |
| `--repeat-penalty F` | Repeat penalty | 1.10 |
| `--repeat-last-n N` | Last tokens for repeat penalty | 64 |
| `--until N` | Request/response delimiter byte | 4 (EOT) |
| `--kv-load PATH` | Load a KV state snapshot before reading prompts | - |
| `--kv-save PATH` | Save a KV state snapshot after requests | - |
| `--lora FILE` | LoRA adapter file, repeatable | - |
| `--lora-scale F` | Scale for the previous `--lora`, repeatable | 1.0 |
| `--mmproj FILE` | Multimodal projector file | - |
| `--mtp FILE` | Draft model file for MTP decoding | - |
| `--mtp-tokens N` | Max speculative draft tokens | 4 |
| `--mtp-min N` | Min speculative draft tokens | 0 |
| `--mtp-psplit F` | Draft split probability | 0.10 |
| `--mtp-pmin F` | Draft minimum probability | 0.00 |
| `--mtp-threads N` | Draft threads | same as `--threads` |
| `--mtp-gpu-layers N` | Draft GPU layers, `0` CPU-only | 0 |
| `--fattn MODE` | Flash attention: `auto`, `1`, or `0` | auto |
| `--think N` | Enable template thinking: `1` on, `0` off | 1 |
| `--role ROLE` | Message role: `system`, `user`, or `assistant` | `user` |
| `--image FILE` | Image attachment, repeatable | - |
| `--info VALUE` | Print inspection fields instead of generating output | - |
| `-h`, `--help` | Show help and usage | - |
| `-v`, `--version` | Show build version | - |

CLI flags override environment variables, which override built-in defaults.

The following environment variables map to the parameters above:

`KC_LLM_MODEL`, `KC_LLM_CTX`, `KC_LLM_PREDICT`, `KC_LLM_THREADS`,
`KC_LLM_GPU`, `KC_LLM_GPU_LAYERS`, `KC_LLM_SEED`, `KC_LLM_TEMP`,
`KC_LLM_TOP_K`, `KC_LLM_TOP_P`, `KC_LLM_MIN_P`, `KC_LLM_REPEAT_PENALTY`,
`KC_LLM_REPEAT_LAST_N`, `KC_LLM_UNTIL`, `KC_LLM_KV_LOAD`, `KC_LLM_KV_SAVE`,
`KC_LLM_LORA`, `KC_LLM_LORA_SCALE`, `KC_LLM_MMPROJ`, `KC_LLM_IMAGE`.

`--info` has no environment-variable form.

---

## Public API

```c
#include "libllm.h"

kc_llm_options_t opts = kc_llm_options_default();
opts.model_path = "model.gguf";
opts.predict = 128;

kc_llm_t *ctx = NULL;
if (kc_llm_open(&ctx, &opts) != KC_LLM_OK) {
    fprintf(stderr, "error: %s\n", kc_llm_error(ctx));
    return 1;
}

if (kc_llm_generate(ctx, "hello", write_callback, NULL) != KC_LLM_OK) {
    fprintf(stderr, "error: %s\n", kc_llm_error(ctx));
}

if (kc_llm_generate_role(ctx, "system", "You are concise.", write_callback, NULL) != KC_LLM_OK) {
    fprintf(stderr, "error: %s\n", kc_llm_error(ctx));
}

kc_llm_close(ctx);
```

Model-only inspection without creating a generation runtime:

```c
kc_llm_options_t opts = kc_llm_options_default();
opts.model_path = "model.gguf";

kc_llm_t *ctx = NULL;
if (kc_llm_open_model(&ctx, &opts) != KC_LLM_OK) {
    return 1;
}

kc_llm_info_field_t fields[] = {
    KC_LLM_INFO_ARCHITECTURE,
    KC_LLM_INFO_PARAMETERS,
    KC_LLM_INFO_CONTEXT_MAX,
};
kc_llm_info_t values[3];

if (kc_llm_info_query(ctx, fields, 3, NULL, values) != KC_LLM_OK) {
    fprintf(stderr, "error: %s\n", kc_llm_error(ctx));
}

kc_llm_close(ctx);
```

Input-dependent inspection uses the same role, template, thinking,
special-token, and tokenization path as generation:

```c
kc_llm_info_request_t request = {
    .role = "user",
    .content = "hello",
};
kc_llm_info_field_t fields[] = {
    KC_LLM_INFO_INPUT_TOKENS,
    KC_LLM_INFO_CONTEXT_USED,
    KC_LLM_INFO_CONTEXT_AFTER,
};
kc_llm_info_t values[3];

if (kc_llm_info_query(ctx, fields, 3, &request, values) != KC_LLM_OK) {
    fprintf(stderr, "error: %s\n", kc_llm_error(ctx));
}
```

Unavailable values are returned as `KC_LLM_INFO_VALUE_NONE`.

String results are owned by `kc_llm_t` and remain valid until
`kc_llm_close()`.

Inspection field semantics:

- `input-tokens` is the final token count after the same request preparation path used by inference, not a raw stdin token count.
- `context-used` is the current effective context occupancy before the current input. It may include restored KV occupancy and occupancy left by earlier work in the same runtime.
- `context-size` is the effective runtime context capacity.
- `context-max` is the model maximum context.
- `context-free = context-size - context-used`.
- `context-after` is the projected occupancy after the current input is added and excludes future generated output.

`kc_llm_generate()` treats its input as a `user` message, tries to apply the
model chat template, and falls back to the raw input when templating is not
available. `kc_llm_generate_role()` does the same with an explicit role.

The `write_callback` receives generated text bytes and can return non-zero to
request an early stop.

```c
int write_callback(const char *data, size_t len, void *user) {
    fwrite(data, 1, len, stdout);
    return 0;
}
```

---

## Lifecycle

- `kc_llm_options_default()` - returns default options.
- `kc_llm_options_load_env()` - overrides options from `KC_LLM_*` environment variables.
- `kc_llm_open_model()` - loads a model-only context for static inspection.
- `kc_llm_open()` - allocates a context and loads the model.
- `kc_llm_info_query()` - queries static model fields and pre-generation runtime accounting.
- `kc_llm_generate()` - runs inference for one prompt.
- `kc_llm_stop()` - requests early stop for the current generation.
- `kc_llm_memory_clear()` - clears KV state to reset the conversation.
- `kc_llm_close()` - releases the context and all associated resources.

---

## CLI Inspection

`llm MODEL --info VALUE` prints `key=value` lines and does not generate output.

Examples:

```bash
llm model.gguf --info all
llm model.gguf --info model
llm model.gguf --info architecture,name,parameters
printf 'hello\004' | llm model.gguf --info input-tokens
printf 'hello\004' | llm model.gguf --info context-used,context-after,context-size
```

CLI groups:

- `all`
- `model`
- `context`

Concrete selectors:

- `architecture`
- `name`
- `parameters`
- `size`
- `vocabulary`
- `context-max`
- `embedding-size`
- `layers`
- `heads`
- `kv-heads`
- `input-tokens`
- `context-used`
- `context-size`
- `context-free`
- `context-after`

Notes:

- `input-tokens` is not a raw stdin token count.
- `context-used` is the current effective runtime occupancy before the current input.
- `context-size` is the effective runtime context capacity.
- `context-max` is the model maximum context.
- `context-free = context-size - context-used`.
- `context-after` is calculated before output generation.
- `--info` reads stdin only when the requested fields require current input.
- `--info` does not run inference.
- `output-tokens` and `tokens` are intentionally excluded.

---

## Dependency

`llm.c` requires a separate llama.cpp source checkout. llama.cpp is not
included.

Set `KC_LLAMACPP_DIR` in the environment before configuring or building:

```bash
export KC_LLAMACPP_DIR=/path/to/llama.cpp
make
```

The build does not assume a default llama.cpp location.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

Each `bin/{arch}/{platform}/` directory contains only the project artifacts:
the `llm` CLI (`llm.exe` on Windows), `libllm.a`, and the project shared
library (`libllm.so` on Linux and Android, `libllm.dylib` on macOS and iOS,
`libllm.dll` plus the `libllm.dll.a` import library on Windows). Each artifact
is self-contained: `libllm.a` is a standalone archive whose members are merged
from the llama.cpp static libraries at build time, so linking only `libllm.a`
is sufficient; the shared library and CLI need no additional llama.cpp runtime.
llama.cpp intermediate libraries and objects stay under `.build/`; they are
needed only to build and link and are not published to `bin/`.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run directly.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
make wasm32/wasm
```

### CUDA

CUDA support is opt-in:

```bash
make CUDA=1
```

CUDA-enabled executables and libraries are placed under `bin/{arch}/{platform}/cuda/`.

```bash
make CUDA=1 x86_64/linux
make CUDA=1 aarch64/linux
```

By default, CUDA builds target only the native GPU architecture for faster compilation. The architecture is shown during configure as `CMAKE_CUDA_ARCHITECTURES_NATIVE`.

To build for specific architectures or for distribution, look up the compute capability of the target GPU (for example, RTX 30 series is 8.6, RTX 40 series is 8.9) and pass it without the dot:

```bash
make CUDA=1 CMAKE_CUDA_ARCHITECTURES="86" x86_64/linux
make CUDA=1 CMAKE_CUDA_ARCHITECTURES="80;86;89" x86_64/linux
```

### WebAssembly

The project builds a standalone WebAssembly module for Emscripten. It is a pure
WASI reactor (no JS glue, `--no-entry`) that exports the same 14 public
`kc_llm_*` functions as the native library. Building it requires the Emscripten
SDK (`emcmake`/`emcc` on `PATH`) and `KC_LLAMACPP_DIR` set to the llama.cpp
checkout (see `## Dependency`):

```bash
make wasm32/wasm
```

The module is written to `bin/wasm32/wasm/llm.wasm`. `make all` includes the
`wasm32/wasm` target.

Inference under wasm is deterministic single-threaded CPU: `get_cpu_count()`
returns 1 on Emscripten, so llama.cpp never creates a thread pool. Offloading
to GPU, MTP draft decoding, and mmap-backed weights are unavailable; ggml's
mmap is reported unsupported and the model is read into memory. The build uses
wasm64 memory (`-sMEMORY64=1`) and `-sALLOW_MEMORY_GROWTH=1` with 256 MiB
initial memory, matching upstream llama.cpp's wasm defaults.

The exported module is a standalone reactor: a host must supply the WASI
`wasi_snapshot_preview1` surface (filesystem, clock, environment, random) and
three `env` imports: `emscripten_notify_memory_growth`,
`__syscall_getcwd`, and `__syscall_getdents64` (the latter two come from
llama.cpp's `common` filesystem use). External model and LoRA files are opened
at the filesystem paths passed to `kc_llm_open_model()`/`kc_llm_open()`.

To run the contract tests under Emscripten and Node.js:

```bash
make test wasm
```

This builds `llm_contract_test` with Node.js raw filesystem support
(`-sNODERAWFS=1`) so `fopen()` reads host paths, and runs
`.build/test-wasm/llm_contract_test.js` from the project root, so the model
and LoRA fixtures resolve as `lib/model.gguf` and `lib/lora.gguf`.

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 and C++17 compatible)

### System Libraries

Linux:
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.

### Test Dependencies

No additional test dependencies required.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these tools, but no guarantees are provided regarding its
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
