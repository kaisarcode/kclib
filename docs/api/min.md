# min scripting API

Stateless CSS, JavaScript, HTML, and text minification.

## Lua

```lua
local css = min.css(source)
local js = min.js(source)
local html = min.html(source)
local text = min.txt(source)
```

Each operation accepts a string and returns its minified string. There is no context object or lifecycle.

## JavaScript

```js
const css = await min.css(source);
const js = await min.js(source);
const html = await min.html(source);
const text = await min.txt(source);
```

Native output allocation is internal. `version()` remains available.
