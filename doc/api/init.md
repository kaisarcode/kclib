# init scripting API

Persistent operating-system startup registrations keyed by name.

## Lua

```lua
init.create("myapp", { cmd = "/path/to/myapp --start" })

local entry = init.get("myapp")
local entries = init.list()

init.delete("myapp")
local version = init.version()
```

`create(name, options)` creates or replaces a registration. `get(name)` returns `nil` when no registration exists. `list()` returns an array of `{ name, user, cmd }`.

Deleting a missing registration is a successful no-op.

## JavaScript

```js
await init.create("myapp", { cmd: "/path/to/myapp --start" });

const entry = await init.get("myapp");
const entries = await init.list();

await init.delete("myapp");
const version = await init.version();
```

Returned native allocations are projected as ordinary objects and arrays.
