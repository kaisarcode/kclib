# AGENTS.md

## Project Context

`mmap.c` is a small C library and CLI for exact binary file replacement and
read-only memory mapping.

It is a byte-storage primitive, not a database, cache, shared-memory protocol,
object store, serialization system, or enterprise persistence layer.

Read `README.md` before modifying the project.

## Core Invariants

- `mmap --set|-set <file>` copies stdin bytes directly into a truncated file.
- `mmap --get|-get <file>` writes the mapped bytes exactly to stdout.
- The library opens existing files read-only.
- POSIX mappings use `PROT_READ` and `MAP_PRIVATE`.
- Windows mappings use `PAGE_READONLY` and `FILE_MAP_READ`.
- Empty files open successfully with size zero and a NULL data pointer.
- Mapping size is the file size observed at open time.
- The data pointer remains borrowed and valid only until context close.
- Multiple contexts remain independent.
- No file format, metadata, index, daemon, or network service is introduced.

## Architecture

The CLI owns command parsing and ordinary stream I/O. The library owns only
opening an existing file, obtaining its size, creating a read-only native
mapping, exposing its address and size, and releasing all resources.

The source files have fixed responsibilities:

- `src/mmap.c` owns `--set|-set` and `--get|-get` CLI behavior;
- `src/libmmap.c` owns portable mapping behavior;
- `src/libmmap.h` defines the public context and API;
- `src/test.c` contains all tests.

## Set Operation

`mmap --set|-set <file>` opens the destination with binary write and truncation
semantics. It reads stdin in 8,192-byte chunks and writes each chunk exactly.
No bytes are interpreted and no newline is added.

The destination is modified in place. Opening truncates before stdin is fully
read, and later read or write failure can leave partial content. It is not
atomic and has no duration promise. There is no temporary file, atomic rename,
fsync, rollback, locking, or durability promise.

Any atomic-write feature must be explicitly requested and define temporary-file
placement, permissions, fsync behavior, rename semantics, cross-filesystem
failure, and cleanup. Do not add hidden journals or databases.

## Get Operation

`mmap --get|-get <file>` opens a mapping context, obtains its borrowed address and
size, and writes exactly that byte range to stdout with one `fwrite()`. Empty
files produce no output and are represented by size zero with a NULL address.

The operation does not parse, copy, transform, delimit, decompress, or validate
the bytes.

## Mapping Boundary

Treat `kc_mmap_open()`, `kc_mmap_data()`, `kc_mmap_size()`, and
`kc_mmap_close()` as one explicit lifetime. Never retain the pointer after close,
free it, write through it, or imply that it owns copied bytes.

The mapping does not make a mutable file immutable. Concurrent truncation or
replacement can change platform behavior and may invalidate safe access. File
locking, version checks, atomic publication, and writer coordination remain the
caller's responsibility.

Do not add transparent copying, caching, remapping, page management, mutation,
serialization, checksums, compression, encryption, or record semantics.

## Cross-Platform Mapping

POSIX opens the file with `O_RDONLY`, obtains size with `fstat()`, and maps
non-empty content using `mmap()` with `PROT_READ | MAP_PRIVATE`. The descriptor
remains open for the context lifetime and is closed after `munmap()`.

Windows opens the file for read with shared read access, obtains a 64-bit size,
creates a `PAGE_READONLY` file mapping, and maps a `FILE_MAP_READ` view. The
context retains the file handle and mapping handle until close. Cleanup unmaps
the view before closing both handles. Empty files retain no native handles.

The size is captured at open. Concurrent modification is not coordinated. In
particular, truncating mapped storage can make later access unsafe according to
operating-system mapping semantics. The public size is `size_t`; files that
cannot be represented in the process address space or size type cannot be
safely mapped.

## Public API and Ownership

Treat `src/libmmap.h` as a compatibility boundary. The context structure is
public, so its fields, initialization assumptions, status codes, pointer
lifetime, empty-file behavior, and close semantics are externally visible.

`kc_mmap_open()` allocates the context. `kc_mmap_close()` unmaps, closes native
handles, and frees it. Closing NULL currently returns `KC_MMAP_ERROR`.

Options are reserved and own no resources. The stop flag does not interrupt
mapping or output operations; do not claim cancellation behavior that is not
implemented.

## Resource and Failure Model

The library allocates one small context and asks the operating system to reserve
an address range equal to the file size. Physical pages remain
operating-system managed; there is no user-space page cache or complete-file
copy.

Failures include invalid arguments, open or size errors, unrepresentable or
unmappable files, address-space exhaustion, and native mapping errors. The CLI
also exposes stdin, destination-write, and stdout-write failures.

Every failure path releases already acquired descriptors, handles, views, and
allocations. There is no persistent internal state. Keep every descriptor, file
handle, mapping handle, view, and allocation paired with deterministic cleanup.
Do not add worker threads, background flushes, hidden temporary storage, or
remote dependencies.

## Composition

`mmap --set|-set` and `mmap --get|-get` are binary Unix filters around a named file. Other
tools own data formats, checksums, locking, atomic publication, compression,
encryption, and interpretation.

The library can provide read-only bytes to parsers without forcing those parsers
to own platform-specific mapping code.

## Source Layout

Preserve exactly:

- `src/mmap.c` for CLI parsing and stdin/stdout byte transfer;
- `src/libmmap.c` for mapping, native handles, and reusable behavior;
- `src/libmmap.h` for the public API and context layout;
- `src/test.c` for all tests, including large-file, platform, stress, and
    integration cases.

Do not create additional source, header, platform, mapping, storage, or test
files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add databases, key-value stores, shared-memory coordinators, memory
allocators, file-format frameworks, caches, object stores, cloud storage,
replication, locking services, telemetry, analytics, dashboards, accounts,
plugins, background daemons, or generic persistence abstractions. Do not add
writable mappings, transactions, atomic file replacement, durability
guarantees, checksums, serialization, compression, encryption, caching,
sharing, or a plugin system.

Do not justify changes through enterprise readiness, hypothetical scale,
framework parity, managed operation, or platform growth.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover exact binary
bytes, empty files, page-boundary sizes, large files, nonexistent and unreadable
files, mapping failure, independent contexts, pointer lifetime, concurrent file
replacement or truncation where safely testable, native cleanup, partial CLI
writes, stdout errors, and POSIX and Windows differences.

Do not weaken tests to accommodate an implementation change.

## Build and Completion

For documentation-only changes run `kcs .`. For behavior changes use the
repository build and tests without cleaning unless authorized.

A change is complete when exact bytes, mapping protection, lifetime, native
ownership, failure behavior, tests, and documentation agree.

The goal is one sharp read-only mapping primitive.
