# libnetl.c - Persistent bidirectional networking

`libnetl.c` creates, operates, and closes persistent TCP and UDP sockets.
Inbound listeners and outbound connections use the same `peer, data` receive
callback and `kc_netl_send(peer, bytes, size)` operation.

It is transport infrastructure, not an application protocol. STUN, ICE,
rendezvous and P2P remain independent compositions.

## Native C API

```c
#include "libnetl.h"

static void receive(kc_netl_peer_t *peer,const void *data,size_t size,
    void *userdata)
{
    static const char answer[]="Received OK";
    (void)data;
    (void)size;
    (void)userdata;
    (void)kc_netl_send(peer,answer,sizeof(answer)-1U);
}

int main(void)
{
    kc_netl_options_t options={0};
    kc_netl_t *server=NULL;
    options.host="0.0.0.0";
    options.port=5000;
    options.protocol=KC_NETL_UDP;
    if(kc_netl_open(&server,&options,receive,NULL,NULL,NULL)!=KC_NETL_OK)
        return 1;

    /* Keep the instance active while the application does other work. */
    kc_netl_close(server);
    return 0;
}
```

To establish an outbound connection, use `kc_netl_connect()` with the remote
address and retain the returned peer. Multiple calls to `kc_netl_send()` use
its original persistent socket. Disconnect and error callbacks are optional.

TCP peers identify live connections. An incoming UDP peer is valid only within
the receive callback; its `send()` sends back through the same bound socket.
Received bytes are independent from bytes chosen for the reply.

## CLI

```sh
netl servidor 0.0.0.0:5000 handler -t udp
netl cliente 192.168.1.20:6000 -t udp
echo "one" | netl cliente
echo "two" | netl cliente
netl -l
netl cliente -d
netl servidor -d
```

Creating the named endpoint starts a resident process holding the socket.
Subsequent calls with only its ID transfer binary stdin via IPC without
creating a new socket.

## Build and tests

```sh
make
make test
```

Use `make all` for other platforms with installed toolchains.

## License

GNU Lesser General Public License, version 3 (LGPLv3).

The CLI exposes one address-based interface model: `netl <id> <addr:port> [command...]`.
Creating an interface does not imply a client or server role, nor does it
transmit any bytes. `netl <id>` sends stdin over its resident socket; the
optional command processes received data asynchronously.

For UDP, the runtime first attempts to bind the configured address locally.
If the address is not available on this machine, the runtime uses an ephemeral
local port and retains the configured address as its default send target.
Failed UDP datagrams do not close the persistent socket. For TCP the native
runtime must still use different listening and connected sockets internally:
it tries local listening first and remote connection otherwise. TCP connection
establishment and disconnection continue to follow OS socket semantics.

The native public API exposes `kc_netl_open`, `kc_netl_send`,
`kc_netl_peer_send`, and `kc_netl_close`. An optional
`kc_netl_endpoint_t` argument to `kc_netl_send` selects a UDP destination
for an individual datagram; NULL uses the configured address. It does not
alter the local bind or create another socket. A wildcard address is never
rewritten to loopback. The low-level socket machinery is private.
