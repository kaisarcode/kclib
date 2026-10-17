# librstrm.c - Reliable streams

`librstrm.c` provides a reliable ordered full-duplex byte stream over a
caller-supplied datagram channel. The underlying channel may lose, duplicate,
delay, or reorder datagrams; the stream retransmits and reorders data before
delivering it to the application.

The library does not open sockets or choose a network path. One rstrm session
uses one exclusive bidirectional datagram channel.

## Quick start

```c
#include "librstrm.h"

static void receive(const void *data, size_t size, void *userdata) {
    /* consume ordered stream bytes */
}

static int output(const void *data, size_t size, void *userdata) {
    /* send exactly one datagram through the chosen transport */
    return 0;
}

kc_rstrm_t *stream = NULL;
kc_rstrm_options_t options = { .mtu = 1200 };

if (kc_rstrm_open(
        &stream,
        &options,
        receive,
        output,
        NULL,
        NULL,
        NULL,
        NULL
    ) == KC_RSTRM_OK) {
    kc_rstrm_send(stream, "hello", 5);
    kc_rstrm_shutdown(stream);
    kc_rstrm_close(stream);
}
```

Incoming transport datagrams are supplied with `kc_rstrm_input()`. Send calls
copy their bytes and may return `KC_RSTRM_EFULL` when the local send buffer is
full. If a drain callback was supplied, it is called when sending can resume.

`kc_rstrm_shutdown()` gracefully ends only the local write side. The stream can
continue receiving until the peer also shuts down. `kc_rstrm_close()` releases
the local session immediately and does not wait for queued data.

## Transport and lifecycle

The caller owns the datagram channel for the entire rstrm lifetime. Each
outgoing callback is one complete datagram and must be sent or copied before
the callback returns; a nonzero callback result is a terminal transport error.
Supply each incoming datagram once with `kc_rstrm_input()`. Datagrams may be
lost, duplicated, delayed, or reordered. The library schedules KCP internally.

`kc_rstrm_send()` is a byte stream: receive callback boundaries do not preserve
send-call boundaries. `kc_rstrm_shutdown()` orders an end of stream after bytes
already accepted for sending, while retaining the reverse read direction.
`kc_rstrm_close()` aborts local state immediately.

## Build

From this project directory:

```bash
make
make test
```

Use `make all` for the configured cross-platform targets when their required
toolchains are installed.

## Status

This is beta software tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You may test, use, and modify it.

Questions can be sent to [kaisar@kaisarcode.com](mailto:kaisar@kaisarcode.com).
Pull requests are not accepted; the project avoids depending on GitHub as
long-term infrastructure.

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
KCP is vendored under its own permissive license in `lib/kcp/`.
