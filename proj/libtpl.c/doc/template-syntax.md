# Template syntax

`libtpl.c` keeps template syntax separate from the C API. This page describes
the supported directives and scope behavior.

## Output expressions

Escaped output uses double braces:

```text
{{ title }}
```

Raw output uses triple braces:

```text
{{{ html }}}
```

Dot notation can access values represented by flat object keys and foreach
aliases:

```text
{{ user.name }}
```

Template comments are removed from output:

```text
{{/* internal template note */}}
```

## Includes

Include another file relative to the configured template root:

```text
{{@include "partial.html"}}
```

If no non-empty root is supplied when the template is opened, the root defaults
to the current directory.

Include paths are resolved by the template engine and should remain within the
application's intended template tree.

## Variables

Set a value in the current scope:

```text
{{@var name expr}}
```

Each `kc_tpl_render()` begins with a fresh root scope. Values created during one
render do not persist into later renders.

## Blocks

Define a named block:

```text
{{@setblock card}}
<div>{{ title }}</div>
{{@endsetblock}}
```

Render it later:

```text
{{@block card}}
```

Inline block properties can be supplied when rendering:

```text
{{@block card [ title: "Hello" ]}}
```

## Conditionals

Conditional rendering uses `if`, optional `else`, and `endif`:

```text
{{@if logged_in}}
Welcome
{{@else}}
Sign in
{{@endif}}
```

Expressions support negation, equality, inequality, logical AND, and logical OR.

## Iteration

Iterate over a comma-separated list or bracket list:

```text
{{@foreach item in items}}
{{ item }}
{{@endforeach}}
```

The loop alias is scoped to the iteration.

## Errors and ownership

`kc_tpl_error()` reports the latest error associated with the template instance.

Rendered output is caller-owned and must be released with `kc_tpl_free()`.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libtpl.h) for the complete C interface.
