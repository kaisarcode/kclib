# hnsw scripting API

In-memory HNSW approximate nearest-neighbor index.

## Lua

```lua
local index = hnsw.open({
    dimension = 384,
    metric = hnsw.METRIC_COSINE,
    search_effort = 128
})

index:add("id_1", vector)
index:build()

local results = index:search(query, {
    limit = 5,
    threshold = -1.0
})

local dimension = index:dimension()
local metric = index:metric()
local count = index:count()
index:close()
```

Options: `dimension` is required. `metric`, `max_connections`, `build_effort`, and `search_effort` are optional. Results are arrays of `{ id, score }`.

Adding after a build invalidates the graph until `build()` is called again.

## JavaScript

```js
const index = await hnsw.open({
  dimension: 384,
  metric: hnsw.METRIC_COSINE,
  searchEffort: 128
});

await index.add("id_1", vector);
await index.build();

const results = await index.search(query, {
  limit: 5,
  threshold: -1.0
});

const dimension = await index.dimension();
const metric = await index.metric();
const count = await index.count();
await index.close();
```

Public status constants and `strerror(status)` may remain available for callers that need explicit status inspection.
