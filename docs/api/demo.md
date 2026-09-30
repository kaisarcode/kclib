# demo scripting API

Minimal greeting capability used as the reference kclib shape.

## Lua

```lua
local greeting = demo.greet("John")
local version = demo.version()
```

## JavaScript

```js
const greeting = await demo.greet("John");
const version = await demo.version();
```

`greet(name)` returns `"Hello <name>!"`. Native allocation cleanup is internal.
