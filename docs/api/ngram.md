# ngram scripting API

Descending sliding-window n-gram traversal over byte-delimited text tokens.

## Lua

```lua
local status = ngram.traverse("The quick brown fox", {
    max_tokens = 3,
    min_tokens = 1,
    separators = " \t\r\n"
}, function(chunk)
    print(chunk.data, chunk.token_start, chunk.token_count)
    return 0
end)
```

Each chunk is a table containing `data`, `token_start`, and `token_count`. The native `data_size` field is absorbed into the scripting string value.

Visitor return semantics are preserved:

```text
0   continue traversal
1   close this span
<0  abort traversal
```

Empty input succeeds without invoking the visitor.

## JavaScript

```js
const status = await ngram.traverse(
  "The quick brown fox",
  {
    maxTokens: 3,
    minTokens: 1,
    separators: " \t\r\n"
  },
  chunk => {
    console.log(chunk.data, chunk.tokenStart, chunk.tokenCount);
    return 0;
  }
);
```

`version()` remains a namespace function.
