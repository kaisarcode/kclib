# Markdown behavior

`libmdp.c` uses one parser for HTML and ANSI output. This page describes the
syntax and rendering behavior that callers need beyond the public C
interface.

## Frontmatter

An optional metadata block can appear at the start of the document:

```text
---
title: My Page
date: 2026-05-07
---

My Page
```

The raw metadata text is returned separately and excluded from body rendering.
Both LF and CRLF delimiters are accepted.

## Supported syntax

The parser recognizes:

- headings from H1 through H6;
- paragraphs;
- bold, italic, and strikethrough text;
- inline code;
- links and images;
- unordered, ordered, and task lists;
- blockquotes;
- fenced code blocks with optional language identifiers;
- pipe tables;
- horizontal rules;
- raw HTML outside code blocks.

Longer backtick delimiters can be used when inline-code content itself contains
backticks.

## HTML rendering

The HTML backend emits structural HTML from the parsed Markdown events.

Fenced-code language identifiers are emitted as `language-NAME` classes.

Raw HTML is passed through by the HTML renderer.

## ANSI rendering

The ANSI backend consumes the same semantic parser events instead of
reinterpreting Markdown independently.

Headings use terminal emphasis. Inline and fenced code use terminal styling.
Links can use OSC 8 hyperlinks. Lists, task lists, and tables are rendered with
terminal-oriented markers and layout.

Raw HTML tags are omitted while visible raw-block text is retained where
possible.

## Caching and lifetime

The source is parsed when the document is opened. HTML and ANSI renderings are
created on first use and cached on the document.

Body, metadata, HTML, and ANSI views are owned by the document. Close the
document only after callers no longer need those views.

## Related documentation

See the [project README](../README.md) for first use and the
[public header](../src/libmdp.h) for the complete C interface.
