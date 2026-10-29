# Getting started

Choose a library from the [project catalog](catalog.md), then build and test it using its own project instructions. This example uses the reference demo library.

## Build a project

From the kclib repository root:

```sh
./scripts/build.sh demo
./scripts/test.sh demo
```

Or from the project directory:

```sh
cd proj/libdemo.c
make
make test
```

The project README and public header describe its actual interface. Other projects may have additional requirements or supported targets.

## Next step

Continue with [Build and distribution](build.md).
