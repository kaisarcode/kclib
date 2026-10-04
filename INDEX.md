# kclib Index

`INDEX.md` is the single catalog of the kclib collection: every project, what
it does, and when to use it. kclib documentation and app agent instructions
read from this file as the source of truth for the inventory. Update it whenever
a project is added, removed, or renamed.

Projects live under `proj/libNAME.c/`. Each project's own
`proj/libNAME.c/README.md` documents its exact API, build, and constraints; the
index is the summary, not the contract.

## Text and web

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`mdp`](https://github.com/kaisarcode/kclib/tree/master/proj/libmdp.c) | Separates Markdown frontmatter and body and renders a small subset as HTML. | Reading or serving Markdown, or needing frontmatter metadata. |
| [`tpl`](https://github.com/kaisarcode/kclib/tree/master/proj/libtpl.c) | Renders templates with variables, includes, blocks, conditions, and iteration. | Generating output from templates (HTML, configs, reports). |
| [`min`](https://github.com/kaisarcode/kclib/tree/master/proj/libmin.c) | Conservatively minifies CSS, JavaScript, and HTML. | Emitting web assets that should be compact. |
| [`ngram`](https://github.com/kaisarcode/kclib/tree/master/proj/libngram.c) | Generates n-grams and can run a command for every span. | Sliding windows over text or per-span processing. |
| [`lng`](https://github.com/kaisarcode/kclib/tree/master/proj/liblng.c) | Detects text language through built-in profiles. | Identifying the language of input text. |
| [`tpm`](https://github.com/kaisarcode/kclib/tree/master/proj/libtpm.c) | Compares text with an n-gram profile and returns similarity from 0 to 1. | Classifying text or measuring text similarity. |

## Local AI and vectors

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`llm`](https://github.com/kaisarcode/kclib/tree/master/proj/libllm.c) | Runs local generative inference with GGUF models through llama.cpp. | Local text generation or a chat backend. |
| [`emb`](https://github.com/kaisarcode/kclib/tree/master/proj/libemb.c) | Converts text into embedding vectors with a local model. | Embeddings of text. |
| [`hnsw`](https://github.com/kaisarcode/kclib/tree/master/proj/libhnsw.c) | Builds HNSW indexes and searches by L2, cosine, or inner product. | Nearest-neighbor search over vectors. |

## Cryptography

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`trust`](https://github.com/kaisarcode/kclib/tree/master/proj/libtrust.c) | Establishes scoped identities by one-use Noise invitations and seals/unseals messages for confirmed UIDs. | Transport-agnostic identity trust and authenticated encryption. |

## Network and IPC

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`http`](https://github.com/kaisarcode/kclib/tree/master/proj/libhttp.c) | Parses and builds HTTP messages; it handles protocol, not transport. | Handling HTTP requests or responses over a socket. |
| [`netl`](https://github.com/kaisarcode/kclib/tree/master/proj/libnetl.c) | Maintains named TCP or UDP listeners and delivers incoming traffic to consumers. | Listening and serving on the network. |
| [`nets`](https://github.com/kaisarcode/kclib/tree/master/proj/libnets.c) | Exchanges data with a TCP or UDP destination. | Connecting to a remote host and exchanging data. |
| [`dmn`](https://github.com/kaisarcode/kclib/tree/master/proj/libdmn.c) | Maintains named resident processes and exchanges data through local IPC. | Persistent subprocess with a message channel. |
| [`rtc`](https://github.com/kaisarcode/kclib/tree/master/proj/librtc.c) | Provides native WebRTC peer connections and DataChannels through a compact C API. | Native WebRTC DataChannel connectivity without depending on a browser engine. |
| [`redp2p`](https://github.com/kaisarcode/kclib/tree/master/proj/libredp2p.c) | Creates direct P2P tunnels for publishing or consuming TCP/UDP services. | Peer-to-peer connectivity. |

## System and UI

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`init`](https://github.com/kaisarcode/kclib/tree/master/proj/libinit.c) | Registers persistent startup commands through native system mechanisms. | Automatic startup at boot or login. |
| [`menu`](https://github.com/kaisarcode/kclib/tree/master/proj/libmenu.c) | Adds and deletes applications from the native application menu. | Registering application launchers in the user's desktop menu. |
| [`wch`](https://github.com/kaisarcode/kclib/tree/master/proj/libwch.c) | Watches files and directories and emits `add`, `upd`, and `del` without polling. | Reacting to filesystem changes. |
| [`mmap`](https://github.com/kaisarcode/kclib/tree/master/proj/libmmap.c) | Stores bytes in files and later exposes them as mapped memory. | File-backed storage or shared memory. |
| [`wvw`](https://github.com/kaisarcode/kclib/tree/master/proj/libwvw.c) | Opens a native WebView window with an explicit optional JavaScript bridge. | A GUI window that renders HTML. |
| [`tray`](https://github.com/kaisarcode/kclib/tree/master/proj/libtray.c) | Provides a persistent native system tray and mutable menu items with callbacks. | A tray/notification-area status entry with a lightweight menu. |

## Composition

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`flow`](https://github.com/kaisarcode/kclib/tree/master/proj/libflow.c) | Runs branched command and child flows; branches remain independent and do not merge. | Branching or parallel execution paths. |
| [`demo`](https://github.com/kaisarcode/kclib/tree/master/proj/libdemo.c) | Provides the reference blueprint for new libraries in the collection. | Only when creating a new primitive in the collection; not distributed. |

## Utilities

Support tools that are not full kclibs: no runner interface, not part of the standard distribution contract.

| Project | Purpose | Use when |
| :--- | :--- | :--- |
| [`b64`](https://github.com/kaisarcode/kclib/tree/master/proj/libb64.c) | Encodes and decodes RFC 4648 base64 through a small reusable API. | Encoding or decoding base64 bytes. |
