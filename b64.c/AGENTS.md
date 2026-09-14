# AGENTS.md

## Project Context

`b64.c` is a small C library and CLI for RFC 4648 base64 encoding and decoding.

It handles binary data encoding and base64 string decoding. It composes with other kclibs through Unix pipe composition.

Read `README.md`, the public header, and the tests before changing behavior.

## Core Invariants

- Encoding produces standard RFC 4648 base64 with padding.
- Decoding rejects input whose length is not divisible by 4.
- Decoding rejects invalid base64 characters.
- Both functions return NULL on allocation failure.
- NULL input arguments cause NULL return.
- Output is caller-owned and must be freed with `free()`.

## Scope Boundaries

Keep the project focused on base64 encoding and decoding. Do not add:

- URL-safe base64 variant;
- custom padding behavior;
- streaming/chunked API;
- line-wrapping of output;
- other encoding formats (hex, etc.);
- compression or encryption;
- network or file I/O;
- configuration or options.

## Source Layout

Preserve the existing four-file `src/` layout:

- `src/b64.c` owns CLI parsing, standard input, standard output, and process exit behavior.
- `src/libb64.c` owns encoding and decoding.
- `src/libb64.h` is the public API and ownership contract.
- `src/test.c` contains all tests.

Extend these files when behavior changes. Do not add source, header, or test files. Keep the CLI thin and reusable behavior in the library.

## Testing and Validation

Keep public API contract tests in `src/test.c`. Test behavior through the public API and CLI-visible contracts, including:

- encoding and decoding round-trips;
- empty input;
- padding variations (0, 1, 2 padding characters);
- invalid base64 rejection;
- NULL argument handling;
- exact output lengths;
- exact stdout behavior.

Do not weaken tests to accommodate a regression.

For behavioral changes, use the repository build sequence:

```bash
kcs .
make
make test
```

Do not run `make clean` or delete build artifacts without explicit authorization.

## Completion Standard

A change is complete when the requested encoding or decoding behavior is correct, ownership and cleanup are sound, failures are legible, relevant tests pass, documentation matches the implementation, and no unrelated platform or enterprise machinery was introduced.

## Design Summary

### Purpose

`b64.c` provides RFC 4648 base64 encoding and decoding as a composable C library and CLI.

### Architecture

The library owns encoding and decoding logic. The CLI adapts stdin/stdout for human use.

```
b64.c (CLI)
  |
  +-- libb64.c (library)
        |
        +-- kc_b64_encode()
        +-- kc_b64_decode()
```

### Public Contract

- `kc_b64_encode(data, size)` returns a malloc'd base64 string, or NULL on allocation failure.
- `kc_b64_decode(str, out_size)` returns malloc'd binary data, or NULL on failure. The caller owns the returned memory.
- Input must have length divisible by 4 (returns NULL otherwise).
- NULL arguments cause NULL return.

### Ownership

- Encoder output is caller-owned, freed with `free()`.
- Decoder output is caller-owned, freed with `free()`.
- Runner output is caller-owned, freed with `free()`.
- Runner error messages are caller-owned, freed with `free()`.

### Limits

- No fixed maximum input size. Both encode and decode use checked dynamic allocation.
- Memory usage is proportional to input size.

### Composition

- CLI composes through stdin/stdout (binary in, base64 out for encode; base64 in, binary out for decode).
- Library composes through function calls.

### Non-Goals

- No streaming/chunked API (caller manages chunks).
- No URL-safe base64 variant.
- No padding options (always uses standard padding).
- No line-wrapping of output.
