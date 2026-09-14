# AGENTS.md

## Project Context

`mdp.c` is a small C library and stdin/stdout CLI for processing Markdown
documents.

It performs three bounded operations:

- render the Markdown body as an HTML fragment;
- return the body without frontmatter;
- return the raw frontmatter content.

The renderer supports the documented, deliberately small Markdown subset. It
does not aim to implement every Markdown dialect or become a publishing
platform.

Read `README.md` before modifying the project.

## Required Mindset

Keep `mdp.c` useful as a local text filter and embeddable parser.

Do not optimize it toward:

- enterprise content management;
- full CommonMark or GitHub compatibility by default;
- website generation;
- collaborative editing;
- hosted publishing;
- document databases;
- plugin ecosystems;
- theme systems;
- generic markup conversion;
- remote rendering services.

More syntax is not automatically an improvement. Every construct adds parsing
interactions, ambiguity, tests, and long-term compatibility obligations.

## Core Invariants

Preserve these properties unless explicitly instructed otherwise:

- the library owns Markdown processing;
- the CLI only adapts flags, stdin, stdout, stderr, and exit status;
- input is one null-terminated document;
- output is returned as a malloc'd NUL-terminated buffer owned by the caller;
- frontmatter is optional and recognized only at the document start;
- frontmatter remains raw text rather than parsed YAML;
- body mode removes recognized frontmatter without otherwise transforming it;
- metadata mode returns only recognized raw frontmatter content;
- HTML mode emits an HTML fragment, not a complete document;
- the supported Markdown subset remains explicit;
- source text emitted into HTML is escaped;
- LF and CRLF frontmatter delimiters remain supported;
- the CLI reads one complete document from stdin before processing;
- stdout stays processable and diagnostics go to stderr;
- no network, external service, or persistent state is required;
- the implementation remains portable C11 and inspectable by one person.

## Parsing Scope

The current renderer handles a compact line-oriented subset including:

- headings from level one through six;
- paragraphs;
- bold and italic spans;
- inline code;
- links, images, and linked images;
- unordered lists;
- blockquotes;
- fenced code blocks;
- horizontal rules;
- pipe tables with separator rows;
- raw HTML pass-through outside code blocks.

This is a project-specific syntax contract, not a claim of conformance with a
larger Markdown specification.

Do not silently change existing interpretation to match another parser.

New syntax requires a concrete local use case, unambiguous behavior, escaping
rules, malformed-input behavior, and public tests.

## Architecture

The processing path is direct:

1. `kc_mdp_exec()` validates the context, input, and output pointers.
2. `kc_mdp_split()` duplicates the raw metadata and body portions.
3. Metadata mode writes the metadata bytes.
4. Body mode writes the body bytes.
5. HTML mode sends the body through the line-oriented renderer.
6. Temporary document buffers are released.

The CLI owns request framing and process behavior. The library owns document
splitting and rendering.

The CLI calls the public C API directly. It builds no intermediate JSON and
needs no runner. Programmatic callers can dlopen the shared library and use
the same functions without a runner or subprocess.

## Frontmatter Boundary

Frontmatter extraction only separates bytes between recognized opening and
closing `---` lines at the beginning of a document.

Recognition requires an exact opening `---` line at the document start. A
delimiter block must be consistently LF or CRLF delimited. Content between
the opening and closing delimiter lines is returned without those lines, and
body content starts immediately after the closing delimiter newline.

Do not add a YAML parser, schema system, typed metadata model, validation
service, inheritance, includes, or remote metadata lookup by default.

Applications that need to interpret metadata should compose `mdp.c` with a
separate parser appropriate to their format.

An opening delimiter without a matching closing delimiter leaves the complete
input as body and returns empty metadata. Preserve this clean fallback unless a
concrete contract change is requested.

## Output Modes

The public modes are:

- `KC_MDP_MODE_HTML`, which renders the body as an HTML fragment;
- `KC_MDP_MODE_BODY`, which writes the body after recognized frontmatter;
- `KC_MDP_MODE_META`, which writes recognized raw frontmatter content.

`kc_mdp_set_mode()` changes one context. `kc_mdp_mode()` converts the three CLI
mode names and flags into constants.

Modes are mutually exclusive. The CLI uses the last valid mode flag supplied.

## Rendering Model

The HTML renderer scans the body line by line and maintains small explicit
states for:

- pending paragraph text;
- an unordered list;
- a blockquote;
- a fenced code block;
- a possible or active pipe table;
- a raw HTML block.

Blank lines flush paragraphs and close applicable blocks. A non-table line
after an active table closes the table and is processed again under normal
block rules.

Headings require one through six leading `#` bytes followed by a space.
Unordered list items require `- ` or `* `. Blockquotes require `> `. Fenced
code uses lines beginning with three backticks. Horizontal rules are exact
`---` or `***` lines. Paragraph source lines are joined with one space.

A line beginning with a block-level HTML opening tag enters a raw HTML block.
Every line inside the block, including Markdown-like content and blank lines,
is emitted unchanged until a line contains the matching closing tag. An
unclosed block ends at the document end. Block-level tag recognition is
case-insensitive.

A line beginning with `|` is held as a possible table header. It becomes a
table only when the next line is a valid separator row composed of pipe cells,
optional edge colons, and hyphens. The held header becomes a normal paragraph
if no separator follows. Active table rows must begin with `|`. Alignment
markers are accepted as separator syntax but do not produce CSS or HTML
alignment attributes.

Inline processing recognizes:

- `**bold**`;
- `*italic*`;
- backtick code spans;
- `[label](url)` links;
- `![alt](url)` images;
- linked images.

Raw HTML tags, comments, and doctype declarations are passed through unchanged
when they form a valid token. A tag is recognized only when the name is
preceded by `<` and followed by `>`, `/`, whitespace, or an attribute list;
quoted attribute values may contain `>`. Anything else, such as `2 < 3`, is
escaped as literal text.

Link labels may contain inline formatting. Label bracket matching supports
nested brackets, and closing-character searches skip backslash-escaped bytes.

Malformed or unmatched inline syntax is emitted as escaped literal text.

## HTML Boundary

HTML mode renders a fragment into the caller-owned output buffer.

Plain source bytes and generated attribute values escape `&`, `<`, `>`, and
double quotes before being written into HTML. Fenced code content is escaped
and wrapped in `<pre><code>`; inside a code block raw HTML is text rather than
executable markup.

Recognized HTML tokens and raw HTML blocks pass through unchanged outside code
blocks. `mdp.c` is not an HTML sanitizer, URL policy engine, browser security
layer, or trusted-content system.

Do not add URL allowlists, content security policy, DOM rewriting, template
layout, CSS, JavaScript, syntax-highlighting runtimes, or browser integration.

Callers remain responsible for deciding whether supplied links, images, and
rendered output are acceptable in their application context.

## Public API

Treat `src/libmdp.h` as a compatibility boundary.

Public changes must define:

- valid modes and inputs;
- caller and library ownership;
- output buffer ownership;
- error behavior;
- context independence;
- portability implications;
- tests for malformed and boundary input.

Do not expose internal parser state, add a generic AST API, or introduce
callbacks for hypothetical syntax extensions without an existing caller.

The current memory-buffer output is intentional: text in, buffer out. Do not
replace it with a framework-specific stream or hidden allocation model.

Contexts copy scalar options and own stop state. `kc_mdp_exec()` does not
retain input after returning; the caller owns the context and the returned
output buffer.

## Source Layout

Preserve the existing `src/` structure:

- `src/mdp.c` contains the CLI;
- `src/libmdp.c` contains all reusable implementation;
- `src/libmdp.h` contains the public contract;
- `src/test.c` contains all tests.

Do not create additional source, header, or test files. Add new CLI behavior to
`mdp.c`, reusable behavior to `libmdp.c`, public declarations to `libmdp.h`, and
all new test cases and fixtures to `test.c`.

The CLI calls the public C API directly. The public header exposes no runner or
JSON type.

Do not split tests into files such as `test_stress.c`, `test_markdown.c`, or
platform-specific test sources. The complete project source layout must remain
visible in these four files.

## Stream Framing

The CLI reads one complete document from stdin, processes it, and writes the
result to stdout. EOF terminates input.

Do not reintroduce a generic control socket or expand this into a resident
content service. A prior control plane was deliberately removed.

## Configuration

The effective precedence is:

1. Built-in defaults.
2. `KC_MDP_MODE` environment value, applied only when a library caller invokes
   `kc_mdp_options_load_env()`.
3. CLI flags.

The CLI resolves the mode from defaults and flags only; it does not read the
environment. CLI mode flags use names and validate through `kc_mdp_mode()`.
The environment stores the mode as its integer constant.

Unknown CLI options fail with diagnostics on stderr.

## Forbidden Default Recommendations

Do not recommend or implement these without explicit instruction:

- full CommonMark or GitHub Flavored Markdown conformance;
- a generic parser generator;
- an abstract syntax tree framework;
- syntax plugins or extension registries;
- YAML parsing;
- HTML sanitization frameworks;
- template engines or themes;
- static-site generation;
- CMS features;
- file watching;
- document indexing or search;
- content storage;
- network fetching;
- remote image processing;
- hosted rendering APIs;
- accounts, permissions, or collaboration;
- telemetry, analytics, or tracing;
- background workers;
- cloud deployment or orchestration;
- a generic control socket.

Do not justify changes through enterprise readiness, ecosystem compatibility,
market adoption, or hypothetical future scale.

## Change Evaluation

Before changing behavior, determine:

- the concrete document that currently cannot be processed;
- whether the behavior belongs to the documented Markdown subset;
- whether composition with another small tool is more appropriate;
- whether frontmatter, body, or HTML semantics change;
- whether existing output compatibility changes;
- how malformed and incomplete syntax behaves;
- whether all source-controlled HTML contexts remain escaped;
- whether memory use remains understandable;
- whether stream framing or public API compatibility changes;
- whether the implementation remains directly inspectable.

Reject speculative syntax and extension points.

Prefer a small explicit parser branch over a generic parsing framework when the
branch corresponds to one supported construct.

## Implementation Preferences

Prefer:

- direct C11 parsing;
- explicit line and inline state;
- small internal helpers for concrete syntax;
- exact frontmatter delimiter handling;
- direct buffer output;
- checked allocation and deterministic cleanup;
- stable output for existing syntax;
- escaped text and attributes;
- library-first behavior;
- public-contract tests.

Avoid:

- parser frameworks;
- generic token or node hierarchies without a current need;
- recursive syntax processing without strict bounds;
- hidden global parser state;
- implicit allocations returned to callers;
- external runtime dependencies;
- platform-specific rendering differences;
- silent recovery that changes unrelated source text.

## Resource and Failure Model

The library receives a complete null-terminated document and allocates working
buffers for frontmatter, body, paragraphs, labels, and table cells.

The CLI buffers one complete document before parsing it. It does not provide a
streaming parser or a fixed document-size limit.

Do not claim bounded input memory. If a concrete deployment requires a maximum
document size, define it explicitly as a public behavior and test it rather
than adding hidden truncation.

Allocation failure or invalid public input must return an error. Output is
produced entirely in memory; on failure no buffer is returned.

The implementation is portable C11 and links native math facilities. Document
splitting, output modes, escaping, and byte framing must remain
platform-independent. Cross-compilation demonstrates build compatibility, not
runtime validation.

## Testing

Every behavioral change must add or update tests for the public result.

Relevant tests include:

- frontmatter with LF and CRLF;
- absent, empty, and unclosed frontmatter;
- all supported block and inline constructs;
- malformed and incomplete markup;
- HTML escaping in text and attributes;
- mode selection and invalid modes;
- empty documents and final lines without newline;
- custom delimiter behavior;
- multiple independent contexts;
- allocation and cleanup paths where testable.

Do not weaken output assertions to accommodate a parser change. Either preserve
the contract or explicitly revise and document it.

All tests stay in `src/test.c`.

## Build and Documentation

For documentation-only changes, run:

```bash
kcs .
```

For source or behavioral changes, use the repository build and test entry
points documented in `README.md`.

Do not run `make clean` or delete build artifacts without authorization.

Use:

- `README.md` for effective syntax, CLI, API, build, and test behavior;
- `AGENTS.md` for implementation constraints, architecture, and agent behavior.

## Completion Standard

A change is complete when:

- the concrete Markdown use case is handled;
- existing modes and output remain compatible unless explicitly revised;
- malformed input fails or degrades predictably;
- output escaping remains correct for supported constructs;
- allocations and cleanup are correct;
- relevant contract tests pass for behavioral changes;
- documentation matches actual behavior;
- no unrelated publishing platform or enterprise machinery was introduced.

The goal is not to parse every Markdown document ever defined.

The goal is a small, predictable parser that composes cleanly with local tools.