# AGENTS.md

## Kclib

This repository belongs to the kclib family.

Read the workspace-level `AGENTS.md` first. These rules add kclib-specific conventions.

A kclib is a small, independent native library built around one concrete capability, usually with a thin CLI.

## Project form

Prefer:

* reusable behavior in the library;
* a thin CLI over the public API;
* a compact C-friendly public header;
* explicit ownership, lifetime, errors, and cleanup;
* ordinary native build artifacts;
* independent local use without hosted services or shared runtimes.

Use the current kclib blueprint as the default for naming, layout, lifecycle, CLI style, configuration, build targets, tests, and README structure.

Do not copy blueprint mechanics when the capability is materially different.

Rule:

```text
same concept -> same convention
different concept -> capability-specific design
```

## Compatibility

Treat the public header and CLI behavior as compatibility contracts.

Preserve existing public symbols, ownership semantics, return codes, CLI behavior, parseable output, configuration precedence, and lifecycle behavior unless the task explicitly requires a change.

Do not rename or redesign stable APIs solely for family consistency.

## Library and CLI

Keep reusable capability logic in the library.

The CLI may parse configuration and arguments, call the public API, print results or diagnostics, select exit status, and release resources.

Do not implement the capability twice.

Do not add public API solely to expose an internal CLI helper.

## Configuration

Where defaults, environment, and CLI configuration all exist, use:

1. built-in defaults;
2. documented environment values;
3. CLI arguments.

Use established `KC_*` environment naming.

Do not introduce generic configuration infrastructure.

## State and lifecycle

Use contexts only when persistent state is required.

Keep independent contexts independent.

Use established kclib lifecycle terminology for equivalent concepts, but do not invent `context`, `run`, `exec`, `stop`, or other lifecycle operations merely for symmetry.

Keep unavoidable process-global state narrow and explicit.

## Errors and I/O

Prefer explicit failures and established family return-code conventions where applicable.

Do not silently ignore malformed input, unsupported behavior, or native failures.

Use the simplest interface appropriate to the capability. No transport or protocol is mandatory across kclibs.

Preserve machine-parseable output where applicable.

## Structure and dependencies

Keep public headers, library source, CLI source, tests, vendored code, and platform-specific code easy to identify.

Do not restructure stable code solely to match another kclib.

Do not add shared runtimes, frameworks, registries, daemons, service layers, or common infrastructure merely to remove small duplication.

Small project-local or platform-specific duplication is acceptable when it keeps behavior easier to inspect.

## Platform code

Do not force materially different native platforms through a common abstraction solely for symmetry.

Share code only when it removes meaningful repeated complexity or inconsistent behavior.

## Tests and documentation

Use the project's existing test model and test shipped behavior.

Do not expose private internals or redesign production code solely for tests.

Keep README organization recognizably consistent with other kclibs, but document only actual project behavior.

Update documentation when public or operational behavior changes.

## Existing projects

Do not migrate an existing kclib to the current blueprint merely because it differs.

When modifying existing code:

* preserve compatibility;
* avoid introducing new inconsistencies;
* adopt current family conventions when relevant and low-risk;
* leave harmless historical variation alone.

## Final rule

Keep kclibs consistent where they express the same concepts.

Let the concrete capability determine everything else.
