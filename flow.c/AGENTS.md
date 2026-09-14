# AGENTS.md

## Project Context

`flow.c` is a small C library and CLI for running local, branch-oriented shell workflows from plain-text `.flow` files.

It is intended for one operator composing commands and reusable child flows on a machine they control. It is not an incomplete scheduler, orchestration platform, hosted automation service, or distributed workflow system.

Read `README.md`, the public header, and the tests before changing behavior.

## Required Mindset

Preserve a direct path from a readable flow record to the command, child flow, or downstream node it invokes.

Prefer explicit records, deterministic traversal, ordinary files, shell composition, bounded structures, and legible failures. Do not introduce infrastructure or abstraction for hypothetical scale, remote administration, or third-party extensibility.

The operator owns the workflow, commands, filesystem, environment, input, and output. Do not replace that model with accounts, hosted state, policy engines, or managed execution.

## Core Invariants

- A flow document is a flat sequence of `flow.*`, `node.*`, and `func.*` records.
- Declared entries and links are traversed in document order.
- Fan-out creates independent branches; branches never merge or share mutable branch data.
- A node may resolve data, run a relative child flow, run a command, and then follow links.
- A terminal branch contributes its bytes to the final output in traversal order.
- Standard input becomes the initial branch input and command stdout becomes branch output.
- Commands run locally with the flow file's directory as their working directory.
- Child imports resolve relative to the importing flow file unless the import path is absolute.
- Runtime set and unset overlays remain ordered operations on exact structural keys.
- `node.use` reuses behavior while the calling node retains its own data and links.
- Flow files and command text are trusted operator input; the runtime is not a sandbox.
- Public output remains caller-owned and is released with `kc_flow_free()`.
- Existing public API names, CLI flags, parseable output, and `.flow` syntax are compatibility boundaries.

## Scope Boundaries

Keep the project focused on local command composition. Do not add, recommend, or assume:

- remote workers or distributed scheduling;
- daemon or server operation;
- queues, brokers, databases, or persistent run history;
- hosted control planes, web dashboards, or accounts;
- organizations, tenants, roles, approval systems, or secrets management;
- retries, compensation, transactions, branch joins, or distributed locks;
- container, virtual-machine, Kubernetes, or cloud execution backends;
- telemetry, analytics, tracing platforms, or remote log collection;
- plugin systems, generic executor interfaces, or workflow marketplaces;
- automatic dependency installation or command discovery;
- sandboxing, privilege separation, or application security policy.

These exclusions keep execution local, inspectable, and composable. They are not missing enterprise features.

## Execution Constraints

Treat every command execution as an explicit trust boundary.

- Template values can become shell command text.
- POSIX execution uses direct built-ins or local programs when possible and `/bin/sh -c` for shell syntax.
- Windows execution uses internal built-ins or `cmd.exe /c`.
- Shell syntax, quoting, available commands, and environment behavior are platform-dependent.
- POSIX command processes receive flow and node data through environment variables; Windows does not currently provide the same exported environment.
- Command stderr is inherited rather than captured in branch output.
- `node.ignore_error=1` permits non-zero command status but does not turn launch, I/O, or allocation failures into success.
- Stop requests are cooperative checks between execution stages; they do not guarantee immediate termination of a command already being waited on.

Do not claim isolation, hermetic execution, reproducibility across shells, or complete POSIX and Windows parity.

## Format and Resource Constraints

Preserve checked parsing and the current hard limits unless a concrete use case justifies a compatible change:

- at most 2048 document records or overlays;
- at most 512 nodes and 512 functions;
- at most 2048 accumulated output branches;
- structural key components limited by 256-byte working buffers;
- paths limited by 4096-byte working buffers;
- recursive use, template, child-flow, and node traversal guarded at depth 64.

The fixed limits bound structure, not payload size. CLI input, command output, templates, and final output use growing memory or temporary files and can consume resources according to operator-provided data. Do not describe execution as fully resource-bounded.

Static links and `node.use` cycles are rejected before execution. Computed links are resolved at runtime and traversal depth remains the final cycle guard.

## State and Ownership

One `kc_flow_t` owns ordered overlays, its error text, and its stop flag. Execution models, resolved data, branch buffers, command capture files, and caches are run-local and released before return.

Do not add hidden persistence, background services, global workflow registries, or implicit network access.

## Source Layout

Preserve the existing four-file `src/` layout:

- `src/flow.c` owns CLI parsing, standard input, standard output, and process exit behavior.
- `src/libflow.c` owns parsing, model validation, template resolution, execution, and reusable runtime behavior.
- `src/libflow.h` is the public API and ownership contract.
- `src/test.c` contains all tests.

Extend these files when behavior changes. Do not add source, header, or test files. Keep the CLI thin and reusable behavior in the library.

## Change Evaluation

Before making a change, determine:

- which current operator problem it solves;
- whether plain shell or another small tool already solves it through composition;
- whether it changes `.flow` parsing, traversal order, branch bytes, command context, or ownership;
- whether it broadens trusted input or creates hidden execution;
- whether it adds unbounded work, memory, or recursion;
- whether it changes POSIX or Windows behavior;
- whether it introduces persistent state, network dependence, or a new dependency;
- whether a smaller change preserves the current model.

Reject speculative architecture, unused extension points, and configuration without an existing requirement.

## Testing and Validation

Keep public API contract tests and generated fixtures in `src/test.c`. Test behavior through the public API and CLI-visible contracts, including malformed records, overlays, ordering, fan-out, child imports, computed values, template quoting, cycles, stop behavior, ownership, and platform differences.

Do not weaken tests to accommodate a regression.

For behavioral changes, use the repository build sequence:

```bash
make
make test
```

Use `make x86_64/windows` and `make test wine` when Windows behavior is affected. Run `kcs .` for repository conformance. Do not run `make clean` or delete build artifacts without explicit authorization.

Documentation-only changes require `kcs .` and source comparison, not a rebuild.

## Completion Standard

A change is complete when the requested local workflow behavior is correct, traversal and byte ordering remain explicit, ownership and cleanup are sound, failures are legible, relevant tests pass, documentation matches the implementation, and no unrelated platform or enterprise machinery was introduced.

## Design Summary

### Purpose

`flow.c` executes small local workflows described by plain-text `.flow` files. It connects ordinary commands and reusable child flows while keeping the document, execution path, and produced bytes inspectable by one operator.

The runtime is a synchronous command-composition primitive. It is not a distributed scheduler, service, control plane, or durable workflow engine.

### Document Model

A document is read as ordered `key=value` records. Empty lines and lines beginning with `#` are ignored. Keys must begin with `flow.`, `node.`, or `func.` and each dotted segment accepts letters, digits, underscores, and hyphens.

Literal values are trimmed. A value beginning with `<<marker` reads through a matching marker line and becomes an executable value. Executable values are template-expanded, run as commands, and resolved from stdout with one trailing newline removed.

The parsed model contains:

- flow data and one or more ordered `flow.link` entries;
- named nodes with data, behavior reuse, an import, an action, and ordered links;
- named functions whose action text can be expanded with node-supplied arguments.

Repeated ordinary data keys replace the prior parsed value. Repeated entry and link records preserve fan-out order.

### Execution Flow

The CLI reads piped standard input completely into memory. Interactive standard input is treated as empty. The library receives the initial byte buffer directly.

Execution proceeds synchronously:

1. Read records from the selected file.
2. Apply ordered context overlays.
3. Parse and validate the effective model.
4. Start each declared entry, or one explicit entry selected by the caller.
5. Resolve node data, including inherited and executable values.
6. Run an imported child flow when the selected behavior declares one.
7. Run the selected behavior's command when present.
8. Copy each active branch to every resolved downstream link.
9. Collect terminal branch bytes and concatenate them in traversal order.

The implementation walks entries, active branches, and links sequentially. Fan-out is logical, not concurrent. There is no join operation: downstream branches remain independent, and terminal outputs are concatenated without separators.

### Node Behavior

`node.<ref>.use` selects another node's behavior recursively. The resolved behavior supplies `import` and `exec`; the calling node keeps its own data and links. Data collection follows the use chain from base to caller, so caller data replaces inherited keys.

An import runs before an action. Its path is template-resolved and interpreted relative to the current flow file unless absolute. The calling node's resolved data replaces matching child flow data for that invocation. Context overlays are applied to the top-level document, not recursively reapplied as child document overlays.

An action receives each active branch as standard input and replaces it with command stdout. A node without an import or action passes branch bytes through unchanged.

### Templates and Functions

Templates resolve `<flow.*>`, `<node.*>`, and `<func.*>` references. Nested placeholder names are supported, and missing or malformed references fail resolution rather than becoming empty strings.

Function calls use `<func.name argument>` syntax. The function action is expanded with `<arg.*>` values from a named node or a dotted data prefix, then processed through the normal template engine. Heredoc function actions execute and return their stdout; literal function actions expand into command text.

Executable values are cached within the current node execution scope. Recursive behavior, value resolution, template expansion, calls, and node traversal use a depth guard of 64.

Shell-mode template substitution escapes values according to the surrounding single- or double-quote state. Unquoted substitution remains direct command text. This reduces accidental quote breakage but is not a security boundary or a complete shell parser.

### Command Boundary

Commands are trusted local operator instructions. The runtime does not sandbox them, restrict filesystem access, remove inherited privileges, or supply an application security model.

On POSIX systems, simple `echo`, `cat`, and `mkdir` forms may run as internal built-ins. Other simple commands use `execvp`, and commands containing shell metacharacters use `/bin/sh -c`. The child works in the flow file's directory and inherits stderr. It receives flow and node values through documented environment variable families.

On Windows, the same limited built-ins are attempted before `cmd.exe /c`. The Windows path does not export the POSIX flow and node environment families. Shell syntax and command availability therefore differ by platform.

External commands receive input and return captured output through temporary files; internal built-ins resolve entirely in memory. The parent waits synchronously for completion and then reads stdout into memory. A stop request is checked between stages and during output capture, but a running command is not actively terminated by the current stop mechanism.

### Branch and Output Model

Each branch owns a byte buffer and size. Commands may process binary input and output because sizes are carried separately, although templates and document fields are null-terminated text.

Fan-out copies branch buffers for each path. Terminal buffers are concatenated exactly in traversal order. The runtime does not insert delimiters, merge structured state, deduplicate output, or coordinate concurrent branches.

The API returns one allocated output buffer even for empty success. The caller owns it and must release it with `kc_flow_free()`.

### Overlays

`kc_flow_set()` and `kc_flow_unset()` append operations to a context. Set requires a valid structural key and literal value. Unset removes all records matching one exact key at the point where the operation is applied. Later operations see the result of earlier operations.

Overlays can replace data, actions, imports, or links without changing the source file. They are run configuration, not persistent mutation. A context retains its overlays across executions until closed.

### Validation and Limits

The parser and runtime use explicit structural limits:

- 2048 records per document or overlay list;
- 512 nodes and 512 functions per model;
- 2048 accumulated branches;
- 256-byte key-oriented working buffers;
- 4096-byte path-oriented working buffers;
- recursion and traversal depth limited to 64.

Static entry and link targets must exist. Static link cycles and missing `node.use` targets are rejected before execution. Executable links cannot be known during static validation; runtime lookup and the depth guard reject invalid or non-terminating traversal.

These limits bound model structure. Input, command output, final output, and several dynamically expanded text buffers are not capped. The operator remains responsible for the resource consequences of trusted workflows.

### State

A `kc_flow_t` owns overlays, the last error string, and a persistent stop flag. A stop request makes subsequent execution fail until the context is closed; there is no reset operation.

Execution models, data stores, caches, branches, and child models are invocation-local. Flow documents and run results are not persisted by the library.

### Public Boundaries

`src/libflow.h` defines context lifecycle, overlays, execution, stop state, error text, output release, and build version. Public names, return values, ownership rules, and output byte behavior are compatibility boundaries.

`src/flow.c` is a thin adapter. It parses one file path, `--link`, ordered `--set` and `--unset` options, reads input, invokes the library, writes output bytes, and reports failures to stderr.

The `.flow` key grammar, heredoc behavior, traversal order, relative import resolution, and CLI flags are also public behavioral contracts.

### Portability and Dependencies

The runtime is C11 and uses the standard library plus native process APIs. It has no network, database, service, or runtime framework dependency. Build support spans multiple operating systems and architectures, but cross-compilation does not establish runtime parity.

POSIX and Windows command launch, shells, built-ins, environment export, quoting, and available utilities differ. Those differences must remain explicit rather than hidden behind claims of identical behavior.

### Composition

The runtime composes through plain files, stdin, stdout, stderr, local commands, environment values, and process exit status. It should rely on existing focused tools for rendering, networking, scheduling, supervision, secret handling, and application-specific work.

Visual graph rendering belongs in a separate converter such as `fldot`. Process supervision belongs outside the runtime. Remote execution belongs in an explicitly chosen external command rather than an implicit flow backend.

### Accepted Limitations

- Execution is synchronous and sequential.
- Fan-out has no join or shared branch state.
- There is no retry, rollback, checkpoint, or resume model.
- Flow files and commands must be trusted.
- Command and final output can grow without a configured byte limit.
- Stop requests do not forcibly interrupt a running child process.
- Windows command context differs from POSIX command context.
- Behavior reuse and computed traversal are depth-guarded rather than comprehensively graph-validated.
- A direct path that cannot be executed locally fails; there is no remote fallback.

These are consequences of a small local workflow runner, not an enterprise roadmap.

### Non-goals

The project does not provide distributed execution, background workers, durable queues, run databases, remote APIs, accounts, multi-tenant policy, dashboards, telemetry, billing, plugins, generic execution providers, infrastructure provisioning, container orchestration, secret storage, or sandboxing.

Adding these concerns would obscure the local file-to-command execution model and should be rejected unless the project is explicitly redefined.

### Change Criteria

A proposed change should solve a demonstrated local workflow problem, preserve deterministic branch and byte ordering, keep ownership explicit, expose failures clearly, retain plain-text inspectability, avoid hidden network or persistent state, and justify any increase in limits or resource use.

Prefer composition with another small tool over growing `flow.c` into a platform. Preserve the four-file source layout and keep reusable behavior behind the public library boundary.

### Defining Invariants

- Flow documents remain plain, local, and inspectable.
- Execution remains synchronous and operator-controlled.
- Entries and links retain deterministic document order.
- Branches fan out independently and do not merge.
- Commands transform stdin bytes into stdout bytes.
- Child imports remain explicit and relative to the importing file by default.
- Shell execution remains a documented trust boundary.
- Structural limits and recursion guards fail explicitly.
- Output ownership remains explicit.
- No service, account, remote control plane, or hidden network dependency is required.
