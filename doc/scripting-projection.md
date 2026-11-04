# Scripting API Projection

This document defines the canonical projection of kclib public APIs into Lua and JavaScript.

The public C header remains the native contract. These scripting shapes preserve the same capability model while removing ABI-only mechanics such as output pointers, explicit allocation cleanup, buffer/count pairs, and opaque pointer handling.

These references describe language-facing APIs independently of any binding runtime,
host application framework or transport. They guide the design of every kclib
public API, including libraries that do not support WebAssembly.

For a kclib with a `wasm/wasm32` build target, the JavaScript projection is
also an **implemented public contract**. The target must publish a complete
ES module exposing the projected operations; exporting C ABI functions from
the WASM binary alone is not sufficient. A binding translates ABI values,
memory ownership and callbacks without implementing separate library semantics.

The standard WASM distribution includes `libNAME.js` (public JavaScript
projection), `libNAME.mjs` (generated Emscripten loader),
`libNAME.wasm` (execution module), and target-specific static/side-module
artifacts when supported. `package.json` identifies `.js` files as ES
modules for Node.js. JavaScript consumers import `libNAME.js`.
`make test wasm` must exercise both the compiled C contract and the actual
public JavaScript module. The build and distribution process must include
these artifacts and their checksums.

## General rules

- `kc_name_operation()` becomes `name.operation()`.
- Opaque capability handles become objects. Operations whose first meaningful argument is that handle normally become object methods.
- Public C structs used as input become Lua tables or JavaScript objects.
- Public arrays plus counts become Lua arrays or JavaScript arrays.
- NUL-terminated text becomes a string.
- Arbitrary bytes become a Lua string or a JavaScript `Uint8Array`.
- Numeric vectors become ordinary numeric arrays unless a binding has a more direct typed-array representation.
- Output parameters become return values.
- Library-owned cleanup functions such as `*_free()` are not exposed when cleanup can be handled mechanically.
- Borrowed strings and buffers are copied or represented as ordinary scripting values when necessary to make lifetime safe.
- Optional pointer fields become omitted or `nil`/`null` properties.
- Status codes that only indicate binding success/failure should normally become binding errors. Status values that represent capability-level state, such as not-found or cooperative stop, remain observable.
- `version()` remains a namespace function.
- `strerror()` may remain available when public status codes are exposed.

## Object projection

Given a C lifecycle such as:

```c
int kc_mdp_open(kc_mdp_t **out, const char *input);
const char *kc_mdp_html(kc_mdp_t *mdp);
void kc_mdp_close(kc_mdp_t *mdp);
```

the canonical shape is:

```lua
local document = mdp.open(input)
local html = document:html()
document:close()
```

```js
const document = await mdp.open(input);
const html = await document.html();
await document.close();
```

The scripting user should not see the output pointer, native handle, borrowed pointer lifetime, or cleanup choreography.

## Structured values

A C value such as:

```c
typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
} kc_netl_options_t;
```

projects naturally as:

```lua
{ host = "127.0.0.1", port = 8080, protocol = netl.TCP }
```

```js
{ host: "127.0.0.1", port: 8080, protocol: netl.TCP }
```

For the actual API of each library, consult its project README and public header. The [project catalog](catalog.md) lists the available projects.

## Next step

Continue with [Advanced documentation](advanced/index.md).
