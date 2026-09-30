# mdp scripting API

Reusable parsed Markdown documents with frontmatter, body, and cached HTML.

## Lua

```lua
local document = mdp.open(source)

local body = document:body()
local meta = document:meta()
local html = document:html()

document:close()
```

`open(source)` returns one document object. `body()` and `meta()` expose the stored split views. `html()` renders on first use and returns the cached HTML on later calls.

Empty body, metadata, or HTML is represented as an empty string.

## JavaScript

```js
const document = await mdp.open(source);

const body = await document.body();
const meta = await document.meta();
const html = await document.html();

await document.close();
```

`version()` is a namespace function.
