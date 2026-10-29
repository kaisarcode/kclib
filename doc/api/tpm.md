# tpm scripting API

Reusable text-profile matching.

## Lua

```lua
local profile = tpm.open(referenceText, {
    ngram_size = 3
})

local score = profile:score(inputText)
profile:close()
```

`open(referenceText, options)` returns a profile ready to score immediately. `ngram_size` is optional, defaults to `3`, and accepts values from `1` through `8`.

`score(text)` returns a number in `[0, 1]`. Empty input and an empty profile are valid and produce `0.0`.

## JavaScript

```js
const profile = await tpm.open(referenceText, {
  ngramSize: 3
});

const score = await profile.score(inputText);
await profile.close();
```

`version()` remains a namespace function.
