# libb64.c - Base64 encode and decode

`libb64.c` encodes binary data as RFC 4648 Base64 and decodes strict canonical
Base64 strings. The API is stateless and allocates returned output for the
caller.

## Quick start

```c
#include "libb64.h"

char *encoded = kc_b64_encode(data, data_size);
if (encoded != NULL) {
    kc_b64_free(encoded);
}

size_t decoded_size = 0;
void *decoded = kc_b64_decode(encoded_text, &decoded_size);
if (decoded != NULL) {
    kc_b64_free(decoded);
}
```

Empty input is valid. Use `kc_b64_free()` for every buffer returned by the
library.

## JavaScript

The WebAssembly target publishes `libb64.js` with the public JavaScript
projection, `libb64.mjs` with the Emscripten runtime, and `libb64.wasm`.
Load the public module as an ES module:

```js
import { b64 } from "./libb64.js";
b64.encode(new TextEncoder().encode("hello"));
```

`make test wasm` runs the native WASM contract tests and the JavaScript API tests.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Notice

No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.

## Command-line interface

The `b64` executable is built from `src/b64.c` and links against the project's static archive (`libb64.a`) and does not require
`libb64.so` or `libb64.dll` to run. Use `make`
to build the library and CLI, and `make test` to test both. No separate
`kccli` checkout or `KCLIB_DIR` variable is needed.
