# tpl scripting API

Reusable template instances with isolated render variables.

## Lua

```lua
local template = tpl.open(
    "<h1>{{ title }}</h1>",
    { root = "./views" }
)

local html = template:render({
    title = "Home",
    user_name = "John"
})

local err = template:error()
template:close()
```

`open(source, options)` returns a reusable template object. The `root` option is optional and defaults to `"."`.

`render(vars)` accepts a Lua table of string values and returns rendered text. Each render receives an isolated variable set.

`error()` returns the latest template error.

## JavaScript

```js
const template = await tpl.open(
  "<h1>{{ title }}</h1>",
  { root: "./views" }
);

const html = await template.render({
  title: "Home",
  user_name: "John"
});

const error = await template.error();
await template.close();
```

Native render-buffer cleanup is internal. `version()` remains a namespace function.
