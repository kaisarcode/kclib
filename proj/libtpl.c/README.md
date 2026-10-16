# libtpl.c - Template renderer

`libtpl.c` renders reusable templates with variables, includes, blocks,
conditionals, and iteration. A template instance keeps its source and include
root while each render receives an isolated variable set.

## Quick start

```c
#include "libtpl.h"

kc_tpl_t *page = NULL;
kc_tpl_var_t vars[] = {
    { "title", "Home" },
    { "user_name", "John" }
};

if (kc_tpl_open(
        &page,
        "<h1>{{ title }}</h1><p>{{ user_name }}</p>",
        NULL
    ) == KC_TPL_OK) {
    char *html = kc_tpl_render(page, vars, 2);

    kc_tpl_free(html);
    kc_tpl_close(page);
}
```

Each render starts with a fresh root scope. Values from one render do not leak
into later renders.

## Documentation

Read the [template syntax guide](doc/template-syntax.md) for escaping, includes,
variables, blocks, conditionals, and iteration.

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
