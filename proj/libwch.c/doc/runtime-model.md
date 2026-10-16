# Watcher runtime model

`libwch.c` separates resident watcher lifetime from temporary local management
handles and subscriptions.

## Resident lifecycle

`kc_wch_create()` creates or replaces a named watcher and starts its resident
process.

The watcher continues running after the creating process exits.

`kc_wch_open()` creates a local handle to an existing named watcher. It does not
start or stop the resident process.

`kc_wch_close()` releases only the local handle and its temporary subscriptions.

`kc_wch_delete()` stops and removes the resident watcher.

## Persistent configuration

A watcher stores:

- the watched path;
- the command to execute for matching events;
- whether traversal is recursive.

Changing path, command, or recursive mode updates the persistent configuration.
The resident process restarts while preserving the watcher name.

Getter functions return the current persistent configuration through the local
handle.

## Events

The supported subscription names are:

```text
add
upd
del
```

The persistent command can run for filesystem events independently of temporary
client subscriptions.

## Temporary subscriptions

`kc_wch_on()` attaches a callback to the current process.

Subscriptions belong to the local handle rather than the persistent watcher.
Closing the handle removes them, while the resident watcher keeps running and
continues executing its persistent command.

Clearing a handler removes the selected subscription.

## Listing and ownership

`kc_wch_list()` returns persistent watcher entries, including whether each
resident process is currently running.

The returned entries and their strings share one allocation. Release it with
`kc_wch_free()`.

`KC_WCH_DIR` is an advanced process-level override for controlled deployments
and tests. Normal callers do not need to select the runtime directory.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libwch.h) for the complete C interface.
