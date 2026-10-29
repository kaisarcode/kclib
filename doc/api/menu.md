# menu scripting API

Native application-menu registration.

## Lua

```lua
menu.add({
    id = "myapp",
    name = "My App",
    description = "My application",
    command = "/path/to/myapp --foo bar",
    icon = "/path/to/icon.png",
    category = "Network"
})

menu.delete("myapp")
```

`id`, `name`, and `command` are the core entry values. `description`, `icon`, and `category` are optional.

Deleting a missing entry is a successful no-op.

## JavaScript

```js
await menu.add({
  id: "myapp",
  name: "My App",
  description: "My application",
  command: "/path/to/myapp --foo bar",
  icon: "/path/to/icon.png",
  category: "Network"
});

await menu.delete("myapp");
```

`version()` remains available as a namespace function.
