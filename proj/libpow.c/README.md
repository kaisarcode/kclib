# libpow.c - Proof-of-Work Challenges

`libpow.c` creates, solves, and verifies portable proof-of-work challenges.
Challenges and solutions are self-contained binary entities. The library does
not perform transport, networking, retries, leases, replay protection, or
application admission policy.

The proof-of-work backend is HashWX. `libpow.c` follows the HashWX client-puzzle
construction: each challenge carries a 256-bit random value and a numeric work
target, and solving searches 64-bit nonces using HashWX functions derived from
SHA-256 of the challenge and nonce batch. HashWX is vendored as a private
implementation detail.

## Public API

```c
#include "libpow.h"

void *challenge = NULL;
void *solution = NULL;
size_t challenge_size = 0;
size_t solution_size = 0;

if (kc_pow_challenge(1000000, &challenge, &challenge_size) == KC_POW_OK &&
    kc_pow_solve(challenge, challenge_size,
        &solution, &solution_size) == KC_POW_OK) {
    int valid = kc_pow_verify(challenge, challenge_size,
        solution, solution_size);
    /* valid == KC_POW_OK */
}

kc_pow_free(solution);
kc_pow_free(challenge);
```

The public API contains:

```c
int kc_pow_challenge(uint64_t effort, void **out_challenge, size_t *out_size);
int kc_pow_solve(const void *challenge, size_t challenge_size,
    void **out_solution, size_t *out_size);
int kc_pow_verify(const void *challenge, size_t challenge_size,
    const void *solution, size_t solution_size);
int kc_pow_size(const void *data, size_t data_size, size_t *out_size);
void kc_pow_free(void *ptr);
uint64_t kc_pow_version(void);
```

`effort` is the approximate expected number of nonce attempts. It is an
application-selected cost, not a time duration.

`kc_pow_size()` determines the total size of one challenge or solution from its
leading bytes. This is independent of transport; it is useful whenever an
application stores or receives multiple binary entities in a byte stream or
other framing context.

A valid solution proves work against the supplied challenge only. The library
does not prove who issued a challenge and intentionally does not impose
freshness, expiry, replay, identity, or authorization policy. Those semantics
belong to the application using the proof.

## Build

```bash
make
make test
```

### WebAssembly

```bash
make wasm32/wasm
make test wasm
```

## Implementation

HashWX is vendored under `lib/hashwx/` under its LGPLv3-or-later license. The
public `libpow` API does not expose HashWX contexts or algorithm machinery.
Native builds use the compiled HashWX backend where supported and fall back to
the interpreter. WebAssembly currently uses the interpreted backend inside the
standalone kclib module.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![LGPLv3](https://www.gnu.org/graphics/lgplv3-147x51.png)](https://www.gnu.org/licenses/lgpl-3.0.html)

This project is distributed under the **GNU Lesser General Public License version 3 (LGPLv3)**.
