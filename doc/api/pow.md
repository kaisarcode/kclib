# pow scripting API

Portable proof-of-work challenges with no transport or application policy.

## Lua

```lua
local challenge = pow.challenge(1000000)
local solution = pow.solve(challenge)
local valid = pow.verify(challenge, solution)
```

`challenge(effort)` returns an opaque binary challenge. `effort` is the
approximate expected nonce work. `solve(challenge)` returns an opaque binary
solution. `verify(challenge, solution)` returns whether the proof satisfies the
challenge.

Challenge and solution values are binary. Transport, storage, freshness, replay,
expiry, identity, authorization, and admission policy are outside this
capability.

## JavaScript

```js
const challenge = await pow.challenge(1000000);
const solution = await pow.solve(challenge);
const valid = await pow.verify(challenge, solution);
```

Binary values are `Uint8Array`. `version()` remains a namespace function.
