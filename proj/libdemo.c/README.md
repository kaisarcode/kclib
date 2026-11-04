# libdemo.c - Minimal C library example

`libdemo.c` is the reference kclib project for a small reusable C library. It
demonstrates the standard project layout, portable tests, generated artifacts,
and WebAssembly support with one intentionally small API.

## Quick start

```c
#include "libdemo.h"

char *greeting = kc_demo_greet("John");
if (greeting != NULL) {
    kc_demo_free(greeting);
}
```

`kc_demo_greet()` returns an allocated greeting. Release it with
`kc_demo_free()`.

## JavaScript

The WebAssembly target publishes `libdemo.js` with the public JavaScript
projection, `libdemo.mjs` with the Emscripten runtime, and `libdemo.wasm`.
Load the public module as an ES module:

```js
import { demo } from "./libdemo.js";
demo.greet("John");
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
