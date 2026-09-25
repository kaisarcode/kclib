# min.c - Asset Minifier

---

---

## CLI

### Examples

Minify CSS:

```bash
echo 'body { color: red; }' | min --css
```

```bash
echo 'const x = 1; // comment' | min --js
```

Minify HTML:

```bash
echo '<div>  hello  </div>' | min --html
```

Minify generic text:

```bash
echo '  hello   world  ' | min --txt
```

Pass a value directly instead of stdin:

```bash
min -txt "Hello                      World  !"
```

---

### Parameters

When a value argument is omitted, the CLI reads the source from stdin. When a
value is present after the mode flag, that value is minified directly.

---

---

## Public API

The reusable API is stateless and exposes each minification capability directly.

```c
#include "libmin.h"

char *css = kc_min_css("body { color: red; }");
char *js = kc_min_js("const x = 1; // comment");
char *html = kc_min_html("<div>  hello  </div>");
char *txt = kc_min_txt("  hello   world  ");

kc_min_free(css);
kc_min_free(js);
kc_min_free(html);
kc_min_free(txt);
```

The public operations are:

Each minifier accepts one borrowed null-terminated source string. On success it
returns an owned null-terminated string, including an allocated empty string for
empty input. A `NULL` input or allocation failure returns `NULL`.

There is no public context, mode enum, dispatcher, or lifecycle state.

---

### Minification Behavior

**CSS** - removes block comments, collapses whitespace, removes trailing semicolons before `}`, and strips unit suffixes from zero values. Preserves quoted strings and `calc()` spacing.

**JS** - removes line and block comments, collapses whitespace between tokens. Preserves quoted strings, template literals, and regex literals. Regex detection is context-aware using the preceding significant character.

**HTML** - removes HTML comments, collapses whitespace between tokens. Preserves content inside `<pre>` and `<textarea>` verbatim. Preserves spaces adjacent to inline elements.

**Text** - collapses whitespace runs to a single space and trims leading and trailing whitespace. It does not interpret comments, markup, quotes, or other syntax.

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

- Artifact: `bin/wasm32/wasm/min.wasm`
- Exports: `kc_min_css`, `kc_min_js`, `kc_min_html`, `kc_min_txt`, `kc_min_free`, `kc_min_version`
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
