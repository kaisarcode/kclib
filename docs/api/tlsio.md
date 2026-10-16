# tlsio scripting API

Authenticated client TLS connections for plaintext byte exchange.

## Lua

```lua
local connection = tlsio.connect({
    host = "example.com",
    port = 443
})

connection:write(bytes)

local data = connection:read()
connection:close()
```

`read()` returns the next plaintext byte chunk. A clean peer shutdown is exposed
as the capability-level `tlsio.EOF` status. Native allocation cleanup is handled
by the binding.

## JavaScript

```js
const connection = await tlsio.connect({
  host: "example.com",
  port: 443
});

await connection.write(bytes);

const data = await connection.read();
await connection.close();
```

`strerror(status)` and `version()` remain namespace functions.
