# Build and distribution

Repository-wide commands for building, testing, and publishing individual projects. For the shortest first-use path, see [Getting started](getting-started.md).

## Build and tests

Build every kclib sequentially (omitting `all` is equivalent):

```sh
./scripts/build.sh
./scripts/build.sh all
```

Build one project:

```sh
./scripts/build.sh demo
```

In an interactive terminal, builds show a single updating progress line.
Use `-l` to display the full build log instead:

```sh
./scripts/build.sh -l demo
./scripts/build.sh -l
./scripts/build.sh -l all
```

When output is redirected, the default mode prints one line per completed
target and preserves the detailed log if a build fails.

The build script resolves the logical library name to its project directory and runs `make all`.

The test runner also defaults to `all` when no project is specified.
It uses the same compact terminal display as the build script.
It updates a single line across native, Wine and WASM tests and between
projects. Add `-l` to see every test result and the original platform
separators:

```sh
./scripts/test.sh
./scripts/test.sh all
./scripts/test.sh -l
./scripts/test.sh -l all
./scripts/test.sh -l demo
```

With redirected output, the default prints one summary per runtime. When a
runtime fails, its complete test log is shown.

The normal project-local entry points are:

```sh
make
make test
```

Projects normally use Make over CMake and Ninja. A local build creates artifacts
under `bin/{platform}/{arch}/`; exact multiarch targets depend on the project.
For example, `make linux/x86_64` builds to `proj/libdemo.c/bin/linux/x86_64/`,
while `make wasm/wasm32` uses `bin/wasm/wasm32/`. Distribution
publishes individual files under `dist/libNAME.c/{platform}/{arch}/`,
for example `dist/libdemo.c/linux/x86_64/libdemo.so`. Each target
directory contains its compiled artifacts, public header, `LICENSE` or
`COPYING` when provided, a generated minimal `README.md` (project name,
author and website), and `SHA256SUMS` covering the published files.
Every kclib owns a source `proj/libNAME.c/manifest.json` with stable,
machine-readable metadata (name, description, author, license, website and
repository). `scripts/dist.sh` publishes a per-library
`dist/libNAME.c/manifest.json`, including that metadata plus the list of
available target artifacts, their paths, sizes, SHA-256 hashes, build
checksums, license checksums, and README checksums. There is no root
distribution manifest. Changes to source metadata republish the affected
manifest; changes to binaries, headers, licensing files or README content
republish the affected targets. No ZIP archives are required.

Publish every kclib (omitting `all` is equivalent) or one project:

```sh
./scripts/dist.sh
./scripts/dist.sh all
./scripts/dist.sh demo
```

By default, each repository distributes into its own local `dist/` directory.
`KCDIST_DIR` selects a shared distribution destination for every project,
including native tools such as `kc`. All projects use their own
metadata and artifact manifest, without a root inventory.
For example:

```sh
export KCDIST_DIR="$HOME/kaisarcode-dist"
./scripts/dist.sh all
```

The matching public distribution root is `https://kaisarcode.com/dist`.
When `KCDIST_DIR` is set, the script never prunes projects owned by other
repositories.

The code is primarily C11.

Each project declares its requirements and supported targets in its own README
and Makefile.

## Next step

Continue with [Scripting projection](scripting-projection.md).
