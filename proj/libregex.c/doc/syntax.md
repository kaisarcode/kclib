# Syntax and limitations

`libregex.c` implements a compact, byte-oriented regular-expression subset. Patterns are compiled into reusable state by `kc_regex_open()`. A compiled expression can be matched against entire inputs or searched within longer text.

## Supported constructs

| Pattern | Meaning | Example |
| :--- | :--- | :--- |
| `abc` | Exact literal bytes | `abc` matches `abc` |
| `.` | One non-NUL byte | `a.c` matches `abc` |
| `[abc]` | One byte from a class | `[abc]` matches `b` |
| `[a-z]` | Inclusive byte range | `[a-z]+` matches `notes` |
| `[^0-9]` | One byte outside a class | `[^0-9]+` matches `notes` |
| `(abc)` | Capturing group | `(ab)c` captures `ab` |
| `a\|b` | Alternation | `notes\|tasks` matches either word |
| `a*` | Zero or more repetitions | `ab*c` matches `ac` and `abbc` |
| `a+` | One or more repetitions | `ab+c` matches `abc` |
| `a?` | Zero or one occurrence | `colou?r` matches `color` |
| `\.` | Escaped literal metacharacter | `a\.js` matches `a.js` |

Groups can contain alternatives and repetitions. Repetitions prefer longer matches but can backtrack to complete an expression.

## Examples

Match a complete route:

```sh
regex match '/(notes|tasks)/([0-9]+)' '/tasks/123'
```

Result:

```text
0	0	10
1	1	6
2	7	10
```

Find numbers inside text:

```sh
regex search '([0-9]+)' 'ticket 42 is ready'
```

Result:

```text
0	7	9
1	7	9
```

Check a filename extension (literal dot must be escaped):

```sh
regex match '.*\.html' 'index.html'
```

For routing, use `kc_regex_match()` so the pattern must consume the complete normalized path. Use `kc_regex_search()` when looking for a substring inside a document or another string.

## Not implemented

- Full PCRE or POSIX ERE compatibility, including their precise edge-case semantics.
- `{n}`, `{n,m}` and other counted repetitions.
- `^ ` and `$ ` anchors. Whole-input matching is supplied by `match` instead.
- Lookahead, lookbehind, noncapturing and named groups.
- Backreferences, lazy or possessive quantifiers.
- Shorthand classes such as `\d`, `\w`, `\s` or Unicode property escapes.
- Case-insensitive flags, multiline/dotall flags and locale-dependent behavior.
- Unicode case folding, normalization, code-point-aware `.` or Unicode character classes.
- Replacement/substitution, split and global match iteration as dedicated API operations.
- Embedded NUL bytes in text or patterns, since the API accepts NUL-terminated C strings.

The escaping syntax is deliberately minimal. A backslash quotes the following byte; it does not introduce PCRE escape classes.

## Captures and search behavior

The native API returns byte ranges, not allocated capture strings. Capture zero denotes the entire matched substring; groups 1 through 16 follow opening-parenthesis order. Unmatched optional groups are reported with `SIZE_MAX` in both offsets. Up to 16 capturing groups are supported.

`kc_regex_match()` requires the entire text to match. `kc_regex_search()` tests successive starting positions beginning at the supplied byte offset and returns the first successful position. An offset must lie within the string. Capture offsets refer to the original string.

## Limits and performance

The implementation bounds patterns to 4096 bytes, their parsed syntax tree to 512 nodes, captures to 16 groups and matching work to 100000 evaluation steps per operation. Backtracking uses a temporary, dynamically allocated work stack rather than native call-stack recursion. Complex patterns can return `KC_REGEX_ELIMIT` instead of a match result.

Matching uses bounded iterative backtracking, **not** a Thompson NFA or a linear-time engine. Consequently, complex nested repetitions can still be expensive before reaching the work limit. The limits are safeguards, not a guarantee of predictable latency for arbitrary untrusted expressions.

The code is currently an initial implementation. These syntax claims describe its intended public contract and require test validation, especially for complex nested groups, capture ordering, invalid patterns and performance.

## Next step

Continue with [C API and CLI](api.md).
