# wch scripting API

Named resident filesystem watchers.

## Lua

```lua
wch.create("source", {
    path = "./src",
    cmd = "make",
    recursive = true
})

local watcher = wch.open("source")

watcher:on("add", function(path) end)
watcher:on("upd", function(path) end)
watcher:on("del", function(path) end)

watcher:setPath("./other")
local path = watcher:getPath()

watcher:setCmd("make test")
local cmd = watcher:getCmd()

watcher:setRecursive(false)
local recursive = watcher:getRecursive()

watcher:close()

local entries = wch.list()
wch.delete("source")
```

Supported event names are `"add"`, `"upd"`, and `"del"`. Passing no handler or `nil` clears that subscription.

`close()` releases only the local management handle and temporary subscriptions. It does not stop the resident watcher. `delete(name)` stops and removes it.

List entries contain `name`, `path`, `cmd`, `recursive`, and `running`.

## JavaScript

```js
await wch.create("source", {
  path: "./src",
  cmd: "make",
  recursive: true
});

const watcher = await wch.open("source");

await watcher.on("add", path => {});
await watcher.on("upd", path => {});
await watcher.on("del", path => {});

await watcher.setPath("./other");
const path = await watcher.getPath();

await watcher.setCmd("make test");
const cmd = await watcher.getCmd();

await watcher.setRecursive(false);
const recursive = await watcher.getRecursive();

await watcher.close();

const entries = await wch.list();
await wch.delete("source");
```

A not-found state remains distinguishable from generic failure where applicable. `version()` remains a namespace function.
