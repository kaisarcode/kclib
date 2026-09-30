# tray scripting API

Persistent native system-tray icons and menu items.

## Lua

```lua
local tray = tray.open({
    icon = nil,
    tooltip = "My app"
})

local quit = tray:addItem("Quit", function(item)
    tray:close()
end)

local separator = tray:addSeparator()

quit:setText("Exit")
local text = quit:getText()

tray:setIcon("/path/to/icon.png")
local icon = tray:getIcon()

tray:setTooltip("Ready")
local tooltip = tray:getTooltip()

local err = tray:getError()

separator:remove()
quit:remove()
tray:close()
```

A tray is an object. Added menu items and separators are item objects owned by that tray.

Item activation callbacks receive the item object. Items expose `setText()`, `getText()`, and `remove()`; separators have no text.

## JavaScript

```js
const tray = await tray.open({
  icon: null,
  tooltip: "My app"
});

const quit = await tray.addItem("Quit", async item => {
  await tray.close();
});

const separator = await tray.addSeparator();

await quit.setText("Exit");
const text = await quit.getText();

await tray.setIcon("/path/to/icon.png");
const icon = await tray.getIcon();

await tray.setTooltip("Ready");
const tooltip = await tray.getTooltip();

const error = await tray.getError();

await separator.remove();
await quit.remove();
await tray.close();
```

`version()` remains a namespace function.
