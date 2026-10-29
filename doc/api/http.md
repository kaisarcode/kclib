# http scripting API

Incremental HTTP parser plus request and response wire builders.

## Lua

```lua
local parser = http.parser({
    request = function(message) end,
    response = function(message) end,
    error = function(status) end
})

parser:write(chunk)
parser:close()

local requestBytes = http.request({
    method = "GET",
    target = "/",
    headers = {
        { name = "Host", value = "example.com" }
    }
})

local responseBytes = http.response({
    status = 200,
    body = "hello"
})
```

Parsed requests and responses become ordinary tables. Fields include `version`, request `method/target/path/query`, response `status/reason`, `headers`, `body`, `trailers`, `chunked`, and `chunk_size`.

Builder defaults remain those of the C API. Binary bodies are represented as scripting byte values.

## JavaScript

```js
const parser = await http.parser({
  request(message) {},
  response(message) {},
  error(status) {}
});

await parser.write(chunk);
await parser.close();

const requestBytes = await http.request({
  method: "GET",
  target: "/",
  headers: [{ name: "Host", value: "example.com" }]
});

const responseBytes = await http.response({
  status: 200,
  body: new TextEncoder().encode("hello")
});
```

One `write()` may emit zero, one, or multiple parsed messages. `strerror(status)` and `version()` remain namespace functions.
