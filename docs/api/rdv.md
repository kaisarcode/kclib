# rdv scripting API

Named rendezvous points with one optional publication and independent ordered
seekers. Values are arbitrary bytes.

## Lua

```lua
local rendezvous = rdv.open({
    max_publications = 1000,
    max_seekers = 10000,
    max_seekers_per_point = 100,
    publication_ttl_ms = 60000,
    seeker_ttl_ms = 30000
})

local seeker = rendezvous:seek("room", "consumer")
local point = rendezvous:publish("room", "publisher")

for _, waiting in ipairs(point.seekers) do
    -- application-defined processing
end

rendezvous:remove(seeker)
rendezvous:unpublish("room")
rendezvous:prune()
rendezvous:close()
```

## JavaScript

```js
const rendezvous = await rdv.open({
  maxPublications: 1000,
  maxSeekers: 10000,
  maxSeekersPerPoint: 100,
  publicationTtlMs: 60000,
  seekerTtlMs: 30000
});

const seeker = await rendezvous.seek("room", bytes("consumer"));
const point = await rendezvous.publish("room", bytes("publisher"));

for (const waiting of point.seekers) {
  // application-defined processing
}

await rendezvous.remove(seeker);
await rendezvous.unpublish("room");
await rendezvous.prune();
await rendezvous.close();
```

`seek(id, data)` creates a seeker even when `id` is not currently published.
`publish(id, data)` does not change existing seekers. `unpublish(id)` removes
only the publication. `remove(seeker)` removes only that seeker.

`get(id)` returns the current point or `null`/`nil`. A point exposes `id`, an
optional `publication`, and its ordered `seekers` collection.

TTL cleanup is explicit through `prune()`; there is no background maintenance.
