# libmdp.c - Markdown parser

`libmdp.c` parses Markdown once, separates optional YAML frontmatter, and renders
the same document semantics as HTML or ANSI terminal output.

## Quick start

```c
#include "libmdp.h"

kc_mdp_t *document = NULL;

if (kc_mdp_open(&document, source) == KC_MDP_OK) {
    const char *body = kc_mdp_body(document);
    const char *meta = kc_mdp_meta(document);
    const char *html = kc_mdp_html(document);
    const char *ansi = kc_mdp_ansi(document);

    (void)body;
    (void)meta;
    (void)html;
    (void)ansi;

    kc_mdp_close(document);
}
```

Rendered values are owned by the document and remain valid while the document
is open.

## Documentation

Read the [Markdown behavior guide](doc/markdown.md) for frontmatter, supported
syntax, and renderer-specific behavior.

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
