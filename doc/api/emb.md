# emb scripting API

Fixed-model text embeddings.

## Lua

```lua
local dimension = emb.dimension()
local vector = emb.embed("The quick brown fox")
local version = emb.version()
```

`embed(text)` returns an array of numbers. The current embedded model has a fixed dimension reported by `dimension()`; callers do not choose it.

## JavaScript

```js
const dimension = await emb.dimension();
const vector = await emb.embed("The quick brown fox");
const version = await emb.version();
```

The vector may be represented as a JavaScript numeric array or an equivalent floating-point typed array. Native output allocation is hidden.
