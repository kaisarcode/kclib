# trust scripting API

Persistent scoped trust relationships and binary message protection.

## Lua

```lua
local trust = trust.init()

local invitedUid, code = trust:invite()

local inviterUid, confirmation = trust:join(code)

local confirmedUid = trust:confirm(confirmation)

local protected = trust:seal(invitedUid, messageBytes)
local message = trust:unseal(invitedUid, protected)

trust:revoke(invitedUid)
trust:close()
```

`invite()` returns `uid, code`. `join(code)` returns `uid, confirmation`. `confirm(confirmation)` returns the confirmed UID.

`seal(uid, bytes)` and `unseal(uid, bytes)` operate on arbitrary binary messages. Transport, ordering, retries, timeouts, and replay policy are outside this capability.

Closing the trust object does not remove persistent relationships.

## JavaScript

```js
const store = await trust.init();

const { uid: invitedUid, code } = await store.invite();

const {
  uid: inviterUid,
  confirmation
} = await store.join(code);

const confirmedUid = await store.confirm(confirmation);

const protectedData = await store.seal(invitedUid, messageBytes);
const message = await store.unseal(invitedUid, protectedData);

await store.revoke(invitedUid);
await store.close();
```

For JavaScript, multi-value operations are projected as named objects when that is clearer than positional arrays. Binary values are `Uint8Array`. `version()` remains a namespace function.
