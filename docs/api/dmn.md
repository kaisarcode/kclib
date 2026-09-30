# dmn scripting API

Persistent named local daemons and EOT-delimited request/response exchanges.

## Lua

```lua
dmn.create("worker", {
    cmd = "my-command",
    eot = "\4"
})

local daemon = dmn.open("worker")
local reply = daemon:sendData(requestBytes)

daemon:setCmd("other-command")
local cmd = daemon:getCmd()

daemon:setEot("\0")
local eot = daemon:getEot()

daemon:sendSignal(15)
daemon:close()

local entries = dmn.list()
dmn.delete("worker")
```

`create(name, options)`, `list()`, `delete(name)`, and `version()` are namespace operations. An opened daemon is an object with `setCmd`, `getCmd`, `setEot`, `getEot`, `sendData`, `sendSignal`, and `close`.

`sendData(bytes)` returns response bytes without the configured EOT marker. `close()` closes only the local handle; it does not delete the resident daemon.

## JavaScript

```js
await dmn.create("worker", {
  cmd: "my-command",
  eot: new Uint8Array([4])
});

const daemon = await dmn.open("worker");
const reply = await daemon.sendData(requestBytes);

await daemon.setCmd("other-command");
const cmd = await daemon.getCmd();

await daemon.setEot(new Uint8Array([0]));
const eot = await daemon.getEot();

await daemon.sendSignal(15);
await daemon.close();

const entries = await dmn.list();
await dmn.delete("worker");
```

List entries are objects of the form `{ name }`. A not-found result should remain distinguishable from a generic failure where relevant.
