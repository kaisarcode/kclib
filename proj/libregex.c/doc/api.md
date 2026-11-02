# C API and CLI

The public header is [`src/libregex.h`](../src/libregex.h). The native API owns compiled pattern state and returns capture offsets into caller-owned input text.

## Lifecycle and matching

```c
kc_regex_t *regex = NULL;
int status = kc_regex_open(&regex, "([0-9]+)");

if (status == KC_REGEX_OK) {
    kc_regex_capture_t spans[2];
    size_t count = 0;

    status = kc_regex_search(regex, "item 42", 0,
        spans, 2, &count);
    if (status == KC_REGEX_OK) {
        /* spans[0] = {5, 7}; spans[1] = {5, 7}. */
    }
}
kc_regex_close(regex);
```

- `kc_regex_open(out, pattern)`: compile a NUL-terminated expression. The output handle is owned by the caller.
- `kc_regex_match(regex, text, captures, capacity, count)`: match the entire text.
- `kc_regex_search(regex, text, offset, captures, capacity, count)`: find the earliest matching substring at or after `offset`.
- `kc_regex_close(regex)`: release the compiled expression. A NULL handle is valid.
- `kc_regex_strerror(status)`: return a static status description.
- `kc_regex_version()`: report the generated build timestamp.

The `captures` buffer is caller-owned; `capacity` gives the number of available slots. It may be omitted with a zero capacity when only match status is needed. `count` optionally reports the required number of slots, including capture zero. Output offsets are byte positions into the input, and the end is exclusive.

## Status codes

| Status | Meaning |
| :--- | :--- |
| `KC_REGEX_OK` | Match succeeded or a pattern was compiled |
| `KC_REGEX_NOMATCH` | A valid pattern did not match |
| `KC_REGEX_EINVAL` | Invalid argument or search offset |
| `KC_REGEX_ENOMEM` | Allocation failure |
| `KC_REGEX_EPATTERN` | Unsupported or malformed expression |
| `KC_REGEX_ELIMIT` | Matching work limit reached |

## CLI

```text
Usage: regex match <pattern> <text>
       regex search <pattern> <text>

Commands:
  match   Require the pattern to match the full input
  search  Find the first substring matching the pattern

Options:
  -h, --help     Show this help
  -v, --version  Show version
```

For a successful match, stdout contains one tab-separated row per capture:

```text
group<TAB>start<TAB>end
```

Unmatched optional groups show `-` for each offset. No match returns status 1 without a normal match result; malformed input returns a nonzero status and writes diagnostics to stderr. This CLI is an independent projection and does not manage network connections or route dispatch.

## Scripting projections

Following [kclib scripting conventions](../../../doc/scripting-projection.md), a projected handle would expose operations such as `regex.open(pattern)`, `compiled.match(text)` and `compiled.search(text, offset)` in JavaScript or Lua. These are API projections, not standalone JavaScript/Lua regex implementations. No runtime binding is included in this increment.

## Next step

Return to the [project README](../README.md).
