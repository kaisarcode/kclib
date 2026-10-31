# Project catalog

Browse the current kclib libraries and standalone tools by capability. Each project name links to its own README, where you can find usage examples, build instructions, and the public interface.

Projects live under `proj/libNAME.c/`. Each project's own
`proj/libNAME.c/README.md` documents its exact API, build, and constraints; the
index is the summary, not the contract.

## Text and web

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`mdp`](../proj/libmdp.c/README.md) | Separates Markdown frontmatter and body and renders a small subset as HTML. | Reading or serving Markdown, or needing frontmatter metadata. |
| [`tpl`](../proj/libtpl.c/README.md) | Renders templates with variables, includes, blocks, conditions, and iteration. | Generating output from templates (HTML, configs, reports). |
| [`min`](../proj/libmin.c/README.md) | Conservatively minifies CSS, JavaScript, and HTML. | Emitting web assets that should be compact. |
| [`bm25`](../proj/libbm25.c/README.md) | Indexes text and ranks matching documents with Okapi BM25. | Lexical search over standalone text corpora. |
| [`ngram`](../proj/libngram.c/README.md) | Generates n-grams and can run a command for every span. | Sliding windows over text or per-span processing. |
| [`lng`](../proj/liblng.c/README.md) | Detects text language through built-in profiles. | Identifying the language of input text. |
| [`tpm`](../proj/libtpm.c/README.md) | Compares text with an n-gram profile and returns similarity from 0 to 1. | Classifying text or measuring text similarity. |

## Local AI and vectors

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`llm`](../proj/libllm.c/README.md) | Runs local generative inference with GGUF models through llama.cpp. | Local text generation or a chat backend. |
| [`emb`](../proj/libemb.c/README.md) | Converts text into embedding vectors with a local model. | Embeddings of text. |
| [`hnsw`](../proj/libhnsw.c/README.md) | Builds HNSW indexes and searches by L2, cosine, or inner product. | Nearest-neighbor search over vectors. |

## Cryptography

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`pact`](../proj/libpact.c/README.md) | Establishes scoped identities by one-use Noise invitations and seals/unseals messages for confirmed UIDs. | Transport-agnostic identity trust and authenticated encryption. |
| [`pow`](../proj/libpow.c/README.md) | Creates, solves, and verifies portable proof-of-work challenge entities with SHA-256 by default and explicit HashWX support. | Transport-agnostic computational admission proofs or client puzzles. |

## Network and IPC

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`http`](../proj/libhttp.c/README.md) | Parses and builds HTTP messages; it handles protocol, not transport. | Handling HTTP requests or responses over a socket. |
| [`tls`](../proj/libtls.c/README.md) | Maintains authenticated client or server TLS sessions over caller-supplied byte transport. | Adding TLS security without coupling it to a network transport. |
| [`stun`](../proj/libstun.c/README.md) | Discovers live UDP mappings and serves STUN Binding requests. | Discovering an observed UDP endpoint or providing a STUN endpoint for other machines. |
| [`rstrm`](../proj/librstrm.c/README.md) | Provides a reliable ordered full-duplex byte stream over a caller-supplied datagram channel. | Adding stream semantics over lossy or reordered datagram transport. |
| [`netl`](../proj/libnetl.c/README.md) | Maintains persistent bidirectional TCP/UDP sockets with peer-level send and receive callbacks. | Opening, sending, receiving, and closing persistent network interfaces. |
| [`dmn`](../proj/libdmn.c/README.md) | Maintains named resident processes and exchanges data through local IPC. | Persistent subprocess with a message channel. |

## System and UI

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`init`](../proj/libinit.c/README.md) | Registers persistent startup commands through native system mechanisms. | Automatic startup at boot or login. |
| [`menu`](../proj/libmenu.c/README.md) | Adds and deletes applications from the native application menu. | Registering application launchers in the user's desktop menu. |
| [`wch`](../proj/libwch.c/README.md) | Watches files and directories and emits `add`, `upd`, and `del` without polling. | Reacting to filesystem changes. |
| [`mmap`](../proj/libmmap.c/README.md) | Stores bytes in files and later exposes them as mapped memory. | File-backed storage or shared memory. |
| [`wvw`](../proj/libwvw.c/README.md) | Opens a native WebView window with an explicit optional JavaScript bridge. | A GUI window that renders HTML. |
| [`tray`](../proj/libtray.c/README.md) | Provides a persistent native system tray and mutable menu items with callbacks. | A tray/notification-area status entry with a lightweight menu. |
| [`ppn`](../proj/libppn.c/README.md) | Displays native popup notifications through the operating system. | Showing desktop notifications without requiring a WebView or tray. |

## Composition

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`flow`](../proj/libflow.c/README.md) | Runs branched command and child flows; branches remain independent and do not merge. | Branching or parallel execution paths. |
| [`demo`](../proj/libdemo.c/README.md) | Provides the reference blueprint for new libraries in the collection. | Only when creating a new primitive in the collection; not distributed. |

## Standalone tools (not kclibs)

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`kc`](../proj/kc/README.md) | Portable native package manager and generic CLI launcher; reads individual project manifests, installs verified artifacts and dispatches installed commands. | Installing, updating, inspecting or executing KaisarCode project CLIs. |

## Utilities

Support tools that are not full kclibs: no runner interface, not part of the standard distribution contract.

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`b64`](../proj/libb64.c/README.md) | Encodes and decodes RFC 4648 base64 through a small reusable API. | Encoding or decoding base64 bytes. |

## Next step

Continue with [Advanced documentation](advanced/index.md).
