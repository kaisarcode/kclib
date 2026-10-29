# ppn scripting API

Native operating-system popup notifications.

## Lua

```lua
local ok, status = ppn.show({
    title = "Download complete",
    message = "package.tar.gz is ready"
})

if not ok then
    error(status)
end
```

The table maps mechanically to `kc_ppn_notification_t`. Both `title` and
`message` are required strings.

## JavaScript

```js
await ppn.show({
  title: "Download complete",
  message: "package.tar.gz is ready"
});
```

`show()` completes when the notification has been accepted by the native
notification mechanism. `version()` remains a namespace function.
