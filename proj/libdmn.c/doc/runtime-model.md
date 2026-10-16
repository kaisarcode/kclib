# Daemon runtime model

`libdmn.c` separates the lifetime of a named daemon from the lifetime of local
management handles. This page describes the behavior that applications need to
understand when managing resident commands.

## Resident lifecycle

`kc_dmn_create()` creates or replaces a named persistent daemon. The command is
required. Once created, the managed process remains resident independently of
the process that created it.

`kc_dmn_open()` creates a local handle for an existing daemon. The handle refers
to the persistent identity and configuration, but does not own the resident
process.

`kc_dmn_close()` releases only the local handle. It does not stop the daemon.

`kc_dmn_delete()` stops the resident process and removes the persistent daemon
registration.

Replacing a daemon with `kc_dmn_create()` preserves the name while replacing
the managed definition.

## Request and response framing

Each daemon uses an end-of-transmission marker to delimit complete exchanges.
The default marker is byte `0x04`.

For one `kc_dmn_send_data()` call, the library:

1. writes the request bytes;
2. appends the configured EOT marker;
3. reads until the same EOT marker terminates the response;
4. returns the response without the marker.

The managed process remains resident between exchanges. Repeated requests do
not restart the command.

`kc_dmn_set_eot()` changes the persistent framing marker. Passing `NULL, 0`
restores the default marker.

## Configuration changes

`kc_dmn_set_cmd()` replaces the command associated with the daemon while
preserving its persistent name.

The getter functions return the configuration associated with the opened
handle. Configuration is part of the named daemon rather than transient request
state.

## Signals

`kc_dmn_send_signal()` sends a platform signal request to the managed process.
Signal availability and meaning depend on the target platform.

## Listing and ownership

`kc_dmn_list()` returns the registered daemon identities. Runtime transport
details such as sockets or pipes are implementation details and are not part of
the public contract.

Release allocations returned by the library with `kc_dmn_free()`.

## Related documentation

See the [project README](../README.md) for the shortest path to first use and
the [public header](../src/libdmn.h) for the complete C interface.
