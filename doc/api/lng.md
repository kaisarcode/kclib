# lng scripting API

Stateless language detection with ranked heuristic scores.

## Lua

```lua
local results = lng.detect("Hello world", {
    threshold = 0.001,
    limit = 3
})

for _, result in ipairs(results) do
    print(result.code, result.score)
end
```

`detect(text, options)` returns an array of `{ code, score }` sorted by descending score. `threshold` and `limit` are optional. Zero matches returns an empty array, not an error.

Scores are heuristic ranking values in `[0, 1]`, not calibrated probabilities.

## JavaScript

```js
const results = await lng.detect("Hello world", {
  threshold: 0.001,
  limit: 3
});

for (const result of results) {
  console.log(result.code, result.score);
}
```

`version()` remains available as a namespace function.
