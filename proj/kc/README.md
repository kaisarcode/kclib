# kc

KaisarCode's portable command launcher and package manager, built as one
standalone native executable. This project is licensed under GNU GPL v3.

`kc` provides five reserved management commands. They are implemented
inside `kc`, not delegated to a `kcpkg` executable:

```sh
kc install libnetl
kc update libnetl
kc update
kc remove libnetl
kc list
kc info libnetl
```

These commands are reserved: no kclib or application CLI may be named
`install`, `update`, `remove`, `list` or `info`. All other commands are
dispatched generically with their arguments, standard streams and exit status:

```sh
kc netl -h
kc probe -h
```

## Documentation

- [Package management](doc/package-management.md) - resolution, sources, storage, and verification.

## Build and tests

```sh
make linux/x86_64
make test
```

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
