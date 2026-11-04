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
while `make wasm/wasm32` uses `bin/wasm/wasm32/`. Libraries supporting
Emscripten publish `libNAME.wasm`, `libNAME.a`, and `libNAME.so` for that
target. The `.a` archive contains Emscripten objects for static linking;
the `.so` is an Emscripten side module for a consumer using
`-sMAIN_MODULE`, rather than a native system shared library.
WASM-enabled libraries also publish `libNAME.js` (public JavaScript API),
`libNAME.mjs` (generated loader), `libNAME.wasm` and a shared
`package.json` declaring ES modules.

Distribution combines every library under the same target directory:

```text
dist/
├── linux-x86_64/
│   ├── libemb.so
│   ├── libemb.h
│   ├── libmdp.so
│   ├── libmdp.h
│   ├── LICENSE
│   ├── COPYING
│   └── SHA256SUMS
├── wasm-wasm32/
│   ├── libemb.wasm
│   ├── libemb.mjs
│   ├── libemb.js
│   ├── libemb.a
│   ├── libemb.h
│   ├── package.json
│   ├── LICENSE
│   ├── COPYING
│   └── SHA256SUMS
├── libemb.json
└── libmdp.json
```

Each `<platform>-<arch>` directory contains the built artifacts and public
headers of all libraries available for that target. There is exactly one
`LICENSE` (LGPLv3), one `COPYING` (GPLv3), and one `SHA256SUMS`
for the complete target directory. There are no per-library subdirectories,
duplicate license files, or generated READMEs. Libraries with WASM share
one `package.json` per target as well.

Every source project retains `proj/libNAME.c/manifest.json`. The script
publishes its metadata and the hashes/paths of its own files in the
root-level `dist/libNAME.json` manifest; the shared target's `SHA256SUMS`
checksums all files in that target. `./scripts/dist.sh NAME` updates only
that library's files and leaves others untouched. Stale files previously
published by the selected library are removed. In a local distribution,
`./scripts/dist.sh all` also removes obsolete per-library distribution
folders. The shared `KCDIST_DIR` is not pruned of unrelated repositories'
projects. No ZIP archives are required.

Publish every kclib (omitting `all` is equivalent) or one project:

```sh
./scripts/dist.sh
./scripts/dist.sh all
./scripts/dist.sh demo
```

By default, each repository distributes into its own local `dist/` directory.
`KCDIST_DIR` selects a shared distribution destination for every project.
Each project uses its own metadata and artifact manifest, without a root inventory.
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

Continue with [Project catalog](catalog.md).
