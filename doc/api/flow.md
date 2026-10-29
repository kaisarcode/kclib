# flow scripting API

Reusable branch-flow templates with independent asynchronous runs.

## Lua

```lua
local flow = flow.open("file.flow")

flow:set("flow.hello", "Hello")
flow:unset("flow.old")

local run = flow:run({
    entry = nil,
    input = inputBytes,
    complete = function(status, data, err)
        -- status is flow.OK, flow.ESTOP, or flow.ERROR
    end
})

run:stop()
run:close()
flow:close()
```

`open(path)` returns a reusable flow object. `run(options)` returns a run object immediately and eventually invokes `complete(status, data, error)` exactly once. The run snapshots the flow state when started.

`stop()` is cooperative and completes the run with `flow.ESTOP`. `close()` on the flow does not invalidate already-started runs.

## JavaScript

```js
const template = await flow.open("file.flow");

await template.set("flow.hello", "Hello");
await template.unset("flow.old");

const run = await template.run({
  entry: null,
  input: inputBytes,
  complete(status, data, error) {
    // status is flow.OK, flow.ESTOP, or flow.ERROR
  }
});

await run.stop();
await run.close();
await template.close();
```

Successful output is returned as bytes; native ownership is internal.
