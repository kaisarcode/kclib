# libmin.c - Asset minifier

`libmin.c` minifies CSS, JavaScript, HTML, and plain text through a small
stateless API. Each operation returns allocated output owned by the caller.

## Quick start

```c
#include "libmin.h"

char *css = kc_min_css("body { color: red; }");
char *js = kc_min_js("const x = 1; // comment");
char *html = kc_min_html("<div>  hello  </div>");
char *text = kc_min_txt("  hello   world  ");

kc_min_free(css);
kc_min_free(js);
kc_min_free(html);
kc_min_free(text);
```

The minifiers preserve syntax-sensitive content such as quoted strings. HTML
also preserves verbatim content inside `pre` and `textarea` elements.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets. See the
[repository build guide](../../README.md#build) for shared toolchain and target
information.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
