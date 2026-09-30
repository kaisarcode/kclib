# wvw scripting API

Persistent native WebView windows.

## Lua

```lua
local window = wvw.open({
    url = "https://example.com",
    title = "Example",
    width = 800,
    height = 600,
    posx = 100,
    posy = 100,
    fullscreen = false,
    borderless = false,
    always_on_top = false,
    click_through = false,
    no_focus = false,
    hidden = false,
    unlist = false
})

window:navigate("https://kaisarcode.com")
window:addInitScript("window.APP_VERSION = '1.0'")

window:enableBridge({
    methods = { "get_version" },
    allow_file = true,
    allow_data = false,
    allow_localhost = false,

    callback = function(method, params)
        return { version = 1 }
    end
})

window:postBridgeEvent({ kind = "ready" })

window:hide()
window:show()
window:list()
window:unlist()
window:minimize()
window:maximize()
window:restore()

window:setTitle("Ready")
local title = window:getTitle()

window:setIcon("/path/to/icon.png")
local icon = window:getIcon()

window:setSize(1024, 768)
local width, height = window:getSize()

window:setPosition(200, 150)
local x, y = window:getPosition()

local visible = window:isVisible()
local listed = window:isListed()
local minimized = window:isMinimized()
local maximized = window:isMaximized()
local fullscreen = window:isFullscreen()

local err = window:getError()
window:close()
```

The bridge-facing scripting projection uses ordinary values rather than serialized JSON when the binding can translate them mechanically. Method whitelisting and origin policy remain explicit capability-level controls.

## JavaScript

```js
const window = await wvw.open({
  url: "https://example.com",
  title: "Example",
  width: 800,
  height: 600,
  posx: 100,
  posy: 100,
  fullscreen: false,
  borderless: false,
  alwaysOnTop: false,
  clickThrough: false,
  noFocus: false,
  hidden: false,
  unlist: false
});

await window.navigate("https://kaisarcode.com");
await window.addInitScript("window.APP_VERSION = '1.0'");

await window.enableBridge({
  methods: ["get_version"],
  allowFile: true,
  allowData: false,
  allowLocalhost: false,
  callback(method, params) {
    return { version: 1 };
  }
});

await window.postBridgeEvent({ kind: "ready" });

await window.hide();
await window.show();
await window.list();
await window.unlist();
await window.minimize();
await window.maximize();
await window.restore();

await window.setTitle("Ready");
const title = await window.getTitle();

await window.setIcon("/path/to/icon.png");
const icon = await window.getIcon();

await window.setSize(1024, 768);
const { width, height } = await window.getSize();

await window.setPosition(200, 150);
const { x, y } = await window.getPosition();

const visible = await window.isVisible();
const listed = await window.isListed();
const minimized = await window.isMinimized();
const maximized = await window.isMaximized();
const fullscreen = await window.isFullscreen();

const error = await window.getError();
await window.close();
```

`version()` remains a namespace function.
