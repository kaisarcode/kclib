# libregex.c - Portable regular expressions

`libregex.c` compiles small reusable regular expressions and matches text without relying on a system-provided regex engine.
It offers full-string matching, substring search, alternatives, groups and byte-offset captures.
The implementation is native C with no runtime downloads or external regex dependencies.

The supported syntax is deliberately smaller than POSIX ERE or PCRE.
The [syntax and limitations guide](doc/syntax.md) describes exactly what is accepted and what is not.
Matching is byte-oriented: UTF-8 literals can be compared byte-for-byte, but Unicode character classes and code-point matching are not implemented.

## Quick start

Compile a pattern once and reuse it:

```c
#include "libregex.h"

kc_regex_t *regex = NULL;
kc_regex_capture_t captures[3];
size_t count = 0;

if (kc_regex_open(&regex, "/(notes|tasks)/([0-9]+)") == KC_REGEX_OK) {
    int status = kc_regex_match(regex, "/notes/42",
        captures, 3, &count);

    if (status == KC_REGEX_OK) {
        /* captures[0] is "/notes/42"; captures[2] is "42". */
        /* Offsets are relative to the input, not allocated strings. */
    }
    kc_regex_close(regex);
}
```

Use `kc_regex_search()` to find the first matching substring rather than requiring a match of the complete input.

The command-line interface exposes both operations:

```sh
regex match '/(notes|tasks)/([0-9]+)' '/notes/42'
regex search '([0-9]+)' 'ticket 42 is ready'
```

Each successful CLI invocation prints tab-separated capture index, start byte, and end byte.
Group 0 is the complete match. An unmatched optional group prints dashes. No match returns a nonzero exit status.

## Documentation

- [Syntax, examples and limitations](doc/syntax.md) - supported constructs, unsupported features and matching behavior.
- [C API and CLI](doc/api.md) - ownership, capture indices, status codes and command outputs.

## JavaScript

The WebAssembly target includes `libregex.js` (the public ES module),
`libregex.mjs` (the Emscripten loader), and `libregex.wasm`.

```js
import { regex } from "./libregex.js";
regex.open("a(b)").match("ab");
```

Use `make test wasm` to verify the C and JavaScript contracts.

## Build

From the project directory:

```sh
make
make test
```

Use `make all` for the configured target matrix when the corresponding toolchains are installed.
Builds use the repository's normal static, shared and WASM artifact conventions.
The implementation and cross-platform artifacts have not yet been fully validated.

## Notice

No guarantees are provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com). Pull requests are not accepted; the project avoids depending on GitHub as long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
