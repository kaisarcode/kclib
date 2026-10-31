# libbm25.c - BM25 lexical retrieval

`libbm25.c` indexes text and searches documents using Okapi BM25. It works
locally without a model, server, or network connection. Documents retain their
original ID and text, and each inserted record has a unique ordinal even when
public IDs repeat.

## Quick start

```c
#include "libbm25.h"
#include <stdio.h>

kc_bm25_t *index = NULL;
kc_bm25_result_t *results = NULL;
size_t count = 0;

if (kc_bm25_open(&index, NULL) == KC_BM25_OK) {
    kc_bm25_add(index, "a", "Peer authentication and encryption");
    kc_bm25_add(index, "b", "Network transport over TCP");
    kc_bm25_build(index);

    if (kc_bm25_search(index, "peer authentication", NULL,
        &results, &count) == KC_BM25_OK) {
        for (size_t i = 0; i < count; i++) {
            printf("%zu %s %.6f\n",
                results[i].ordinal, results[i].id, results[i].score);
        }
        kc_bm25_free(results);
    }
    kc_bm25_save(index, "documents.bm25");
    kc_bm25_close(index);
}
```

A saved index can be opened with `kc_bm25_load()` and searched without
rebuilding. `kc_bm25_export()` and `kc_bm25_import()` offer the same
portable representation in an owned memory buffer, allowing applications
to embed BM25 data inside a larger file without temporary files.
Loaded indexes are read-only. Search results include the source
text, public ID, insertion ordinal, and a nonnegative ranking score.

## Search options

The default BM25 parameters are `k1 = 1.2` and `b = 0.75`. The default
result limit is 10. Set a different limit, `k1`, or `b` in
`kc_bm25_search_options_t`; the index does not need to be rebuilt.

The built-in Unicode analyzer lowercases UTF-8 text, treats punctuation as
word boundaries, and matches canonically equivalent accented characters.
Accents remain distinct by default. Set `fold_accents` when opening an
index to remove combining marks from normalized terms. The selected mode is
preserved in saved indexes. Language-specific stemming, synonyms, and query
expansion are not applied.

## Normalized vocabulary

The same Unicode analyzer used for indexing and search is available to
composing libraries through `kc_bm25_tokens()`. It visits normalized tokens
with the index's saved accent-folding policy.

`kc_bm25_vocabulary()` visits each distinct normalized term and its document
frequency. `kc_bm25_postings()` visits the ordered document ordinals and
frequencies associated with one normalized term. These operations work on both
built and imported indexes; returned strings are borrowed for the callbacks.
Nonzero visitor returns stop traversal with `KC_BM25_EABORT`.

This exposes lexical data without requiring clients to copy Unicode folding
rules or duplicate the inverted index. Search policy and spelling correction
remain the responsibility of the consumer.

## CLI

Build a portable index from tab-separated ID/text records:

```sh
printf 'a\tPeer authentication and encryption\n' |
    bm25 build documents.bm25
bm25 build documents.bm25 -i documents.tsv --fold-accents
```

Search an existing index:

```sh
bm25 search documents.bm25 "peer authentication"
bm25 search documents.bm25 "network" --limit 5 --k1 1.2 --b 0.75
```

Each result is one tab-separated line containing ordinal, public ID,
BM25 score, and original text. Source lines must contain one ID/text pair
separated by a tab, with no embedded tabs, newlines, or NUL bytes.

## Build

From this project directory:

```sh
make
make test
```

Use `make all` for the configured cross-platform targets when their
toolchains are installed.

## Notice

No guarantees are provided regarding stability or future support. You may test,
use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
