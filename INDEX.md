# kclib Index

`INDEX.md` is the single catalog of the kclib collection: every project, what
it does, when to use it, and where its repository lives. kclib documentation
and app agent instructions read from this file as the source of truth for
the inventory. Update it whenever a project is added, removed, or renamed.

Local checkouts of the projects live under `repo/NAME.c/`. Each project's own
`NAME.c/README.md` documents its exact CLI, API, build, and constraints; the
index is the summary, not the contract.

## Text and web

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`mdp`](https://github.com/kaisarcode/mdp.c) | Separates Markdown frontmatter and body and renders a small subset as HTML. | Reading or serving Markdown, or needing frontmatter metadata. |
| [`tpl`](https://github.com/kaisarcode/tpl.c) | Renders templates with variables, includes, blocks, conditions, and iteration. | Generating output from templates (HTML, configs, reports). |
| [`min`](https://github.com/kaisarcode/min.c) | Conservatively minifies CSS, JavaScript, and HTML. | Emitting web assets that should be compact. |
| [`ngram`](https://github.com/kaisarcode/ngram.c) | Generates n-grams and can run a command for every span. | Sliding windows over text or per-span processing. |
| [`lng`](https://github.com/kaisarcode/lng.c) | Detects text language through built-in profiles. | Identifying the language of input text. |
| [`tpm`](https://github.com/kaisarcode/tpm.c) | Compares text with an n-gram profile and returns similarity from 0 to 1. | Classifying text or measuring text similarity. |

## Local AI and vectors

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`llm`](https://github.com/kaisarcode/llm.c) | Runs local generative inference with GGUF models through llama.cpp. | Local text generation or a chat backend. |
| [`emb`](https://github.com/kaisarcode/emb.c) | Converts text into embedding vectors with a local model. | Embeddings of text. |
| [`hnsw`](https://github.com/kaisarcode/hnsw.c) | Builds HNSW indexes and searches by L2, cosine, or inner product. | Nearest-neighbor search over vectors. |

## Cryptography

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`trust`](https://github.com/kaisarcode/trust.c) | Seals and opens messages with Noise X and reports TOFU peer identity continuity. | Encrypted, authenticated peer messaging. |

## Network and IPC

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`http`](https://github.com/kaisarcode/http.c) | Parses and builds HTTP messages; it handles protocol, not transport. | Handling HTTP requests or responses over a socket. |
| [`netl`](https://github.com/kaisarcode/netl.c) | Maintains named TCP or UDP listeners and delivers traffic to commands. | Listening and serving on the network. |
| [`nets`](https://github.com/kaisarcode/nets.c) | Sends stdin to a TCP or UDP destination and copies the response to stdout. | Connecting to a remote host and exchanging data. |
| [`dmn`](https://github.com/kaisarcode/dmn.c) | Maintains named resident processes and connects stdin/stdout through local IPC. | Persistent subprocess with a message channel. |
| [`redp2p`](https://github.com/kaisarcode/redp2p.c) | Creates direct P2P tunnels for publishing or consuming TCP/UDP services. | Peer-to-peer connectivity. |

## System and UI

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`init`](https://github.com/kaisarcode/init.c) | Registers persistent startup commands through native system mechanisms. | Automatic startup at boot or login. |
| [`wch`](https://github.com/kaisarcode/wch.c) | Watches files and directories and emits `add`, `upd`, and `del` without polling. | Reacting to filesystem changes. |
| [`mmap`](https://github.com/kaisarcode/mmap.c) | Stores bytes in files and later exposes them as mapped memory. | File-backed storage or shared memory. |
| [`grd`](https://github.com/kaisarcode/grd.c) | Calculates hierarchical row and column layouts with weights, gaps, and minimums. | Computing UI layouts. |
| [`wvw`](https://github.com/kaisarcode/wvw.c) | Opens a native WebView window with an explicit optional JavaScript bridge. | A GUI window that renders HTML. |
| [`tray`](https://github.com/kaisarcode/tray.c) | Shows a native system tray icon and menu and runs a configured local program on activation. | A tray/notification-area status entry with a lightweight menu. |

## Composition

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`abi`](https://github.com/kaisarcode/abi.c) | Exposes target-specific ABI metadata for public kclib functions. | Building bridges, FFI bindings, or other tooling that needs to inspect kclib public ABIs. |
| [`flow`](https://github.com/kaisarcode/flow.c) | Runs branched command and child flows; branches remain independent and do not merge. | Branching or parallel execution paths. |
| [`libr`](https://github.com/kaisarcode/libr.c) | Provides the reference blueprint for new libraries and CLIs in the collection. | Only when creating a new primitive in the collection; not distributed. |

## Utilities

Support tools that are not full kclibs: no runner interface, not part of the standard distribution contract.

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`b64`](https://github.com/kaisarcode/b64.c) | Encodes and decodes RFC 4648 base64 through a small library and CLI. | Encoding or decoding base64 bytes. |
