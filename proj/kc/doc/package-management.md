# Package management

A library CLI such as `netl` is resolved from
`<install-root>/libnetl.c/<platform>/<arch>/netl` (or `netl.exe` on
Windows). A standalone application uses
`<install-root>/<command>/<platform>/<arch>/<command>`. No project names
are hardcoded in the launcher.

The package manager reads independent `PROJECT/manifest.json` files,
downloads or copies the selected platform/architecture artifacts, verifies
their SHA-256 checksums and stores each installed manifest beside its files.
No global manifest, receipt or separate package database is used.

The distribution source defaults to `https://kaisarcode.com/dist`.
`KCDIST_DIR` selects a unified local distribution when set;
`KCPKG_SOURCE` is an optional source override when `KCDIST_DIR` is not set.
`KCPKG_HOME` overrides the installation root. Otherwise files are installed
under the per-user KaisarCode data directory. No package state is stored
outside that directory.

Run the unified 25-case contract suite with:

```sh
make linux/x86_64
make test
make windows/x86_64
make test wine
make all
```

All tests live in `src/test.c` and produce one numbered `[PASS]` series and
one final summary. All build outputs
are placed under `bin/<platform>/<arch>/`.

## Next step

Return to the [project README](../README.md).
