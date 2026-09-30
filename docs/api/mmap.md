# mmap scripting API

One file-backed binary value per opened object.

## Lua

```lua
local value = mmap.open("file.bin")

local data = value:get()       -- nil when no value exists
value:set(bytes)               -- replace in memory
value:save()                   -- persist
value:del()                    -- delete file and invalidate object
value:close()
```

A missing file is valid and opens with no value. `get()` distinguishes no value from a real zero-byte value.

Passing `nil` to `set()` represents the null value; an empty byte string represents a real zero-byte value.

After `del()`, operations other than final `close()` fail.

## JavaScript

```js
const value = await mmap.open("file.bin");

const data = await value.get(); // null when no value exists
await value.set(bytes);
await value.save();
await value.del();
await value.close();
```

Binary values are `Uint8Array`. `version()` remains a namespace function.
