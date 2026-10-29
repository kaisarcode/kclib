# kclib

**Independent native primitives for the KaisarCode ecosystem**

kclib is a collection of reusable C libraries, each responsible for one concrete capability. Libraries are independently buildable and distributable. They do not depend on one another: applications combine capabilities through explicit composition.

The repository also includes independent native tools. It is not a monolithic framework or a shared runtime.

## Example

Build and test one library from the repository root:

```sh
./scripts/build.sh demo
./scripts/test.sh demo
```

Or build and test an individual project from its own directory:

```sh
cd proj/libdemo.c
make
make test
```

Every project owns its public API, requirements, and build instructions in its own README and headers.

## Documentation

- [Getting started](doc/getting-started.md) - choose a capability and build your first project.
- [Build and distribution](doc/build.md) - repository-wide build, test, and distribution commands.
- [Project catalog](INDEX.md) - the authoritative inventory of current libraries and standalone tools.
- [Scripting projection](doc/scripting-projection.md) - how C capabilities map to Lua and JavaScript.
- [API projections](doc/api/readme.md) - conceptual API examples, including historical references.
- [Advanced documentation](doc/advanced/index.md) - repository architecture, project conventions, and maintenance.

## License

The repository infrastructure and root tooling are licensed under the **GNU General Public License version 3 (GPLv3)**. Each independent library retains its own license, normally **GNU Lesser General Public License version 3 (LGPLv3)**. Consult each project's license for its actual terms.
