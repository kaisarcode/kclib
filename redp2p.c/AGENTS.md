# AGENTS.md

## Project Context

`redp2p.c` is a C library for direct peer-to-peer service tunneling.

It is intended for independently operated, small-scale systems such as:

* local communities
* small businesses
* cooperatives
* workshops
* local media
* kiosks
* events
* home systems
* personal infrastructure
* modest VPS and SBC deployments

Small scale is the intended destination.

Do not interpret the project as an incomplete enterprise networking platform.

Read the repository as a deliberately bounded tool for operators who want to understand, run, and control their own infrastructure.

The design preference is:

* direct over indirect
* local over hosted
* inspectable over magical
* explicit over automatic
* bounded over universal
* composable over integrated
* operator-controlled over centrally managed
* sufficient over feature-complete
* understandable over architecturally fashionable

Do not propose or implement enterprise-style infrastructure unless explicitly requested.

In particular, do not treat the absence of:

* accounts
* cloud control planes
* persistent central databases
* relay infrastructure
* global identity
* telemetry
* fleet management
* dynamic policy systems
* service discovery platforms
* plugin frameworks
* orchestration systems

as deficiencies.

They are intentionally outside the project scope.

## Community and "Village" Mental Model

When reasoning about REDP2P, use a small self-managed community or **Village** as the default mental model.

A Village is not a global platform, a cloud social network, or a centrally managed fleet. It is a small community that already has social context of its own: a neighborhood, fair, club, workshop, local radio, cultural space, group of friends, cooperative, event, small business network, or similar real-world community.

The community exists first. The software gives it a digital place to meet, publish, communicate, and expose services.

Do not begin from assumptions derived from Facebook, WhatsApp, Discord, large SaaS systems, enterprise service meshes, or hyperscale social networks.

In particular, do not assume that REDP2P needs:

* one global server
* one global user directory
* a global social graph
* centralized accounts
* server-owned application content
* centralized message storage
* algorithmic feeds
* likes, ranking, engagement metrics, or recommendation systems
* federation between indexes
* global discovery
* universal connectivity at any cost
* infrastructure sized for mass-market scale

Those are application or product choices, not REDP2P requirements.

### The index is a meeting point

The index is not merely an incidental rendezvous helper, and it is not a massive central application server.

For a small REDP2P network, the index is the shared meeting point that temporarily materializes the network: publishers appear there, peers discover what is available there, and connection coordination happens there.

The index remains deliberately small:

* it coordinates rather than owns the community
* it stores temporary network state rather than a permanent social database
* it does not carry application traffic
* it does not own user identity
* it does not own application content
* it does not define the social rules of the community
* it can be run on modest infrastructure
* compatible indexes may be independently operated

A simple compatible index on inexpensive hosting is a valid deployment. Do not assume a dedicated cloud stack is required.

### Many indexes, many communities

There is no single canonical index and therefore no single canonical Village.

Different indexes may serve different Villages or communities. Indexes do not need to federate or synchronize with each other.

The larger social fabric is formed indirectly by people and applications:

* a person may participate in more than one community
* a person may add another index to their local application
* a person may invite someone else into an existing community
* people may carry relationships between otherwise independent communities
* communities may grow, shrink, move, disappear, or be recreated

Do not introduce server-to-server federation merely to make independent communities look like one global network.

If an application needs persistent contacts, memberships, names, local metadata, social relationships, chat history, groups, or other community semantics, that state belongs to the application unless explicitly specified otherwise. REDP2P must not become the global database for it.

### REDP2P transports capabilities, not social semantics

REDP2P sees services and ports. Applications decide what those services mean.

Once a path is established, ordinary software can use a local port and REDP2P carries the traffic to the remote peer. The transported protocol is opaque to REDP2P.

A published service may therefore represent, for example:

* a personal website or web application
* a local business or virtual storefront
* a club or fair website
* chat
* a game
* a radio or streaming service
* a catalog
* a collaboration tool
* file transfer
* an API
* any other TCP- or UDP-based application supported by the project

Do not add application-specific semantics to REDP2P merely because one Village application uses them.

The application layer may provide feeds, chats, groups, commerce, media, games, local directories, or fully custom member web applications. Another application may provide none of those. The social dynamic belongs to each application and community.

### Locality, trust, and real-world interaction

The intended community model is often small enough that participants know one another directly or through shared local context.

The software may support interactions that begin digitally and continue physically: meeting at a fair, reserving an item for the weekend, visiting a local business, attending a club, organizing a meal, playing games together, hiring a local service, or participating in a community event.

Do not automatically replace this local social context with enterprise identity, global reputation, marketplace escrow, centralized moderation infrastructure, or platform-wide trust machinery.

Such mechanisms may be appropriate for a particular application, but they are not default REDP2P requirements.

### Member-owned digital presence

A useful Village application may allow each participant, group, business, club, fair, radio, or other local actor to expose its own digital presence.

That presence may be a simple page or a complete custom web application with its own appearance, iconography, colors, behavior, and protocol. REDP2P only supplies the underlying connectivity.

Prefer this model over assuming that every participant must fit into one centrally defined profile or content template.

The project should remain compatible with the old-web principle that individuals and small groups can own and run their own software while still participating in a shared community.

### Small-scale economics are part of the design context

Assume that operators may have:

* little or no infrastructure budget
* inexpensive shared hosting
* a small VPS
* a home computer
* a mobile device
* an SBC
* community-provided infrastructure

Do not recommend costly infrastructure merely because it is conventional at larger scale.

Optional external infrastructure may be considered when explicitly requested, but the baseline design must not silently assume recurring cloud expenditure.

A community may also reserve index capacity or visibility for local sponsors, businesses, institutions, or other supporting participants. Do not reinterpret this as a requirement for a centralized advertising platform. Small local sponsorship and community-supported operation are valid application-level models.

### Design test

When evaluating a proposed change, ask:

> Does this help a small independently operated community expose and connect its members' services while preserving local ownership, directness, inspectability, and low operating cost?

If the proposal mainly solves problems created by global scale, centralized ownership, anonymous mass-market participation, or enterprise operations, it is probably outside REDP2P's intended scope unless explicitly requested.

## Core Invariants

The following invariants must be preserved unless the project owner explicitly instructs otherwise:

* the index exposes an HTTP control API
* the index does not relay application traffic
* peer application traffic travels directly over UDP
* TCP mode uses vendored KCP over the peer UDP path
* UDP mode preserves application datagram boundaries
* STUN remains optional
* no TURN-style relay is implemented
* application authentication remains outside REDP2P
* application authorization remains outside REDP2P
* application encryption remains outside REDP2P
* no global identity system is introduced
* no user account system is introduced
* index state remains temporary
* the index requires no persistent database
* publisher session credentials and control sequence remain local, scoped, minimal persistent state
* protocol fields, candidate sets, pending punches, and proof challenges remain bounded
* configured index publisher capacity is enforced through seats, including inactive VIP reservations
* the project remains usable on modest hardware
* the code remains inspectable by one person

The absence of relay, accounts, persistent identity, telemetry, and enterprise control infrastructure is intentional. The local publisher session state used by `pub` and `del` is a narrow operational exception, not an index database or identity system.

These are not missing features.

## Sovereignty and Directness

REDP2P exists to establish direct peer-to-peer connectivity as an end in itself, not merely as a performance optimization. Directness is a required property of a successful REDP2P path.

A connection that depends on an intermediary carrying application traffic is not a degraded REDP2P connection. It is a different communication model and remains outside this project.

Do not evaluate REDP2P against a "connect at any cost" networking checklist. In particular:

* the absence of TURN is not a missing feature
* the absence of a relay fallback is not a missing feature
* the absence of an automatic non-P2P fallback is not a missing feature
* a failed direct punch is a valid final outcome

If direct peer connectivity cannot be established after the supported punch attempts are exhausted, REDP2P reports that failure to the caller. The caller may then choose any independent mechanism it wants, including doing nothing, retrying later, using a private relay, TURN, a VPN, a tunnel, a proxy, or another project entirely. REDP2P does not define, select, configure, authenticate, coordinate, or transport such fallback mechanisms.

Do not add relay sessions, relay credentials, TURN state, relay secrets, relay protocol framing, relay callbacks, generic transport plugins, or fallback transport abstractions unless the project owner explicitly requests them.

The boundary is:

* REDP2P attempts direct connectivity
* REDP2P reports direct-connectivity failure
* the caller owns all policy after that failure

`REDP2P_EPUNCH` is therefore not evidence that REDP2P is incomplete. It means the requested direct peer path could not be established under the current network conditions.

### Third-party infrastructure boundary

REDP2P may use replaceable coordination infrastructure, but it must not depend on intermediary infrastructure to carry application traffic.

The index belongs to the control plane. It coordinates peers but never carries their application payloads. It is intentionally self-hostable and replaceable; operators may run their own compatible index instead of trusting a centrally operated one.

STUN is tolerated only as an optional aid to discovering a direct path. It may help a peer learn externally visible addressing, but it does not carry established application traffic. STUN must remain optional and must never become a prerequisite for REDP2P operation.

TURN crosses this boundary because it makes a third party part of the application data path. Do not propose TURN merely because it is conventional in ICE/NAT-traversal stacks. A separate application may choose TURN after REDP2P reports failure, but that choice is outside REDP2P.

### Application security boundary

REDP2P does not add application-traffic encryption, authentication, or authorization by default. Those properties belong to the protocol being transported or to a separate composable layer chosen by the caller.

Do not treat this as a security omission. Double-encrypting an already protected protocol such as SSH, TLS, or another authenticated encrypted transport can be unnecessary and wasteful. Conversely, an operator may intentionally carry plaintext on a trusted network. REDP2P must not impose a policy either way.

The project should secure only the authority and control semantics that REDP2P itself creates.

## Engineering Priorities

When priorities conflict, prefer:

1. operator autonomy
2. deterministic behavior
3. inspectability
4. simplicity
5. correctness
6. bounded resource use
7. portability
8. performance
9. convenience

Performance matters, but not at the expense of hidden state, fragile behavior, or unnecessary complexity.

Do not optimize for hypothetical scale.

Do not add architectural machinery merely because it is conventional in enterprise networking software.

## Internal Development Repository

This repository is an internal development and validation repository. Do not assume that every build target, cross-compilation path, test matrix entry, helper, or maintenance mechanism present here is intended to appear unchanged in the public-facing repository.

The large platform and architecture matrix exists primarily to verify that the code continues to compile across supported targets. It is validation infrastructure, not a requirement for the end-user build experience.

The public-facing repository may deliberately expose a much smaller native build path, for example building only for the architecture and platform on which the user is running CMake. Do not interpret simplification of public build files as loss of intended portability, and do not preserve internal build complexity merely because it exists here.

When working in this repository:

* preserve the internal validation matrix unless explicitly instructed otherwise
* do not present the matrix itself as part of REDP2P's user-facing complexity
* do not assume internal maintenance machinery belongs in the public distribution
* distinguish implementation validation concerns from public product surface

## Repository Truth

Before changing code:

1. read this file
2. read `README.md`
3. inspect the relevant public header
4. inspect affected source
5. inspect relevant tests
6. inspect build files before invoking build commands
7. inspect existing implementations before introducing helpers, abstractions, dependencies, or protocol changes

When repository documentation and implementation disagree, determine the current intended contract before changing behavior.

Prefer correcting stale documentation over inventing new behavior.

Do not assume an undocumented capability is intended.

## Scope Discipline

Implement:

* the requested behavior
* defects that block the requested behavior
* regressions introduced by the change
* nearby defects that are materially unsafe, incorrect, or trivial and low-risk to fix

Do not expand scope for:

* stylistic cleanup
* speculative refactoring
* architectural purity
* generic abstractions
* framework introduction
* future scalability
* enterprise-readiness
* unrelated modernization

If a proposed remediation becomes substantially larger than the concrete defect, stop and reconsider the approach.

Prefer a bounded compromise over a disproportionate subsystem.

## Build Safety

Never run:

```sh
make clean
```

Do not delete:

* build directories
* `.build`
* generated files
* caches
* compiled artifacts
* previous build state

unless explicitly authorized.

Ordinary builds are allowed after inspecting the build configuration.

Preserve existing build state.

Do not assume a clean build.

## Dependencies

Do not add dependencies without explicit approval.

Prefer:

* existing project code
* vendored dependencies already present
* system facilities
* small direct implementations where appropriate

Do not introduce:

* frameworks
* package managers
* external daemons
* remote APIs
* cloud services
* hidden runtime services
* telemetry
* auto-update infrastructure

unless explicitly requested.

If avoiding a dependency would require reimplementing a large or security-sensitive subsystem badly, stop and present the tradeoff instead of forcing dependency purity.

## Networking Philosophy

REDP2P is a direct peer connectivity tool.

Its job is to help two peers establish a usable direct path and carry application traffic through that path. Direct peer connectivity is the product requirement, not merely a faster path than relaying.

It is not responsible for defining the application's trust model.

A peer may transport:

* SSH
* TLS
* HTTPS
* application-specific encrypted protocols
* plaintext protocols on trusted local networks
* any other protocol appropriate to the operator's environment

REDP2P must not impose application-layer identity, authorization, or encryption merely because it carries peer traffic.

Security effort belongs primarily where REDP2P itself defines authority:

* publisher registration
* publisher ownership
* heartbeat
* punch polling
* deregistration
* temporary index state
* replay resistance
* bounded protocol parsing

The rule is:

REDP2P secures the control semantics it creates.

It does not invent security policy for application traffic it merely transports.

## Architecture

REDP2P separates coordination from application transport.

The index exposes an HTTP control API. Each operation is one bounded HTTP POST with a JSON body carrying an `op` field; the index dispatches on `op`, keeps no open channel with peers, and closes each connection after one request. Peer lifetime is tied to a `last_seen` timestamp and the TTL, not to a connection. The index holds only temporary publisher records and short-lived pending punch calls; it stores no per-peer connection state and no proof-of-work challenge state. Any language that can serve HTTP can replicate the contract.

The index handles:

* publisher registration and deregistration
* publisher lookup and listing
* authenticated publisher heartbeats (sequenced HMAC proof over `last_seen`-refreshing endpoint state)
* proof-of-work challenges (stateless, verified by recomputation)
* candidate exchange
* request-driven punch coordination (`punch_req` / `punch_poll`)
* temporary registration state

The index does not:

* bind UDP for peer traffic
* relay application payloads
* inspect application protocols
* keep an open channel with publishers
* provide application authentication
* provide consumer identity
* provide global accounts
* maintain a permanent network database
* act as a control plane for managed clients

Application traffic travels directly between peers through UDP.

### Index contract

The HTTP contract is a single endpoint dispatching on `op`:

* `challenge` issues a random nonce and a difficulty; the server stores nothing.
* `register` validates the id, challenge, candidates, proof of work, publisher-session proof, and any configured password admission proof before capacity, ownership, or publisher-state checks; it creates a publisher record only when the id is not already active.
* `heartbeat` authenticates with a strictly increasing sequence and HMAC proof derived from the publisher session secret (no proof of work) and refreshes `last_seen` and the endpoint fields.
* `lookup` and `list` return fresh records, filtering expired ones without writing.
* `deregister` removes a record only after a valid strictly increasing sequence and HMAC proof derived from the stored publisher session secret.
* `punch_req` stores a bounded pending call for a currently active target publisher id; an absent or expired target is rejected, and nothing is enqueued.
* `punch_poll` authenticates with the same sequenced-HMAC session contract, then returns and consumes only the pending calls included in the successful response.

Expired records are filtered from `lookup` and `list` responses. Physical removal runs automatically every 60 seconds in the index event loop. The CLI `redp2p idx <port> --prune` reports this behavior and does not expose an HTTP endpoint.

Proof of work is paid at registration events only; authenticated publisher-control operations never carry a PoW nonce or solution and instead use a strictly increasing sequence plus HMAC proof. A quiet publisher becomes an expired record, not a disconnection event. After expiry, the publisher creates a new session through `register`.

A publisher registers with `challenge` + `register`, generating and persisting a scoped session secret and control sequence locally. Index passwords are opaque, case-sensitive 1..255-byte tokens: every byte except ASCII whitespace, controls, and DEL is allowed. An empty global password leaves the index open. When the index selects a global or VIP password, the publisher sends an `access_proof`, never the password: HMAC-SHA256 over `REDP2P-ADMISSION-v1` and the full canonical registration message, encoded as exactly 64 lowercase hexadecimal characters. VIP passwords override the global password for their IDs. Passwords are admission HMAC keys only; they neither identify clients nor protect application traffic. An authentication failure is `403 auth_failed` and does not reveal which password class applied. `heartbeat`, `punch_poll`, and `deregister` prove possession of the separate session secret with operation-specific HMACs and strictly increasing sequences; the raw secret is sent only by the initial `register`. On restart, the publisher first attempts an authenticated `heartbeat` with its persisted session state. A valid proof resumes the active record; `not_found` causes a new registration with a new session secret; `invalid_proof` cannot replace an active record and must wait for expiry. A consumer looks the publisher up, announces itself with `punch_req`, and punches directly against the publisher candidates.

## Peer Protocol

The probes travel directly between peers; the index carries only coordination.
Pre-session punch establishment retains the textual
`REDP2P_CTRTOK_PUNCH_PING:<session>:<from>:<to>` and
`REDP2P_CTRTOK_PUNCH_PONG:<session>:<from>:<to>` exchange and the publisher's
`REDP2P_CTRTOK_PUNCH:server` completion marker.

### Established Session Envelope

After punching, both TCP and UDP modes use the same 24-byte REDP2P envelope
on each peer UDP datagram. The envelope is session-level framing.

| Offset | Size | Field | Value |
| :----- | :--- | :---- | :---- |
| 0 | 4 | magic | `0x50434b52`, little-endian (bytes `52 4b 43 50`) |
| 4 | 1 | version | `2` |
| 5 | 1 | type | See types below |
| 6 | 1 | role | `1` initiator (consumer), `2` responder (publisher) |
| 7 | 1 | protocol | `1` TCP, `2` UDP |
| 8 | 16 | session_id | TCP session identity; reserved for UDP |
| 24 | remaining | payload | One DATA payload; empty for controls |

Common types are `DATA=3` and `KEEPALIVE=7`. TCP also uses `HELLO=1`,
`HELLO_ACK=2`, `CLOSE=4`, `CLOSE_ACK=5`, and `RESET=6`; these controls are
invalid in UDP mode. Unknown types and nonempty control payloads are ignored.

The common decoder validates the minimum size, magic, version, protocol and
role, then extracts the type, session field and payload. Each transport applies
its own type, payload-size and established-session checks. Packets from a
nonmatching peer address cannot carry established application data.

For TCP, DATA contains exactly one complete KCP output packet, at most 1400
bytes. The full 16-byte identity and opposite role must match the established
stream; DATA enters KCP only after the existing HELLO/HELLO_ACK handshake.
KCP behavior, conversation derivation, retransmission and close lifecycle are
unchanged. The prior type value `3` now denotes TCP DATA carrying KCP.

For UDP, DATA contains exactly one application datagram, including empty
payloads, up to the unchanged 1412-byte application limit. Datagram boundaries
and every byte are preserved in both directions. UDP sessions retain their
existing peer-address matching: their temporary punch token is not retained
as an independent 16-byte session identity. Senders zero the reserved session
field; receivers do not use it for identity validation.

KEEPALIVE has no application payload. UDP receivers refresh session activity
for valid KEEPALIVE packets without forwarding anything to the local service
or client. Malformed envelopes, wrong protocol or role, unsupported controls
and oversized payloads are ignored.

Established DATA is opaque. Application bytes equal to or beginning with any
former textual control token, including `REDP2P_CTRTOK_PUNCH:`,
`REDP2P_CTRTOK_PUNCH_PING:`, `REDP2P_CTRTOK_PUNCH_PONG:` and
`REDP2P_CTRTOK_KA:`, remain application bytes and are never interpreted as
control. Textual probes belong only to punch establishment.

This changes the established UDP wire format: both UDP peers must use the
session envelope. There is no fallback to unframed application datagrams.
TCP envelope values and the public C API are unchanged.

## Protocol Boundaries

Protocol handling must remain bounded.

Do not accept unbounded:

* HTTP request lines
* headers
* JSON bodies
* ids
* hostnames
* addresses
* candidate lists
* pending punch queues
* strings
* packet buffers
* session tables

Bounds should be explicit constants or configuration with clear limits.

Do not silently turn bounded structures into dynamically growing infrastructure.

Reject malformed or oversized input explicitly.

Failure should be legible and deterministic.

## Index Capacity

The index may be configured with a publisher capacity.

Capacity is not a suggestion.

If configured, it must be enforced.

VIP or reserved publisher ids consume seats even when inactive if that is the documented configuration behavior.

Do not reinterpret capacity as:

* current socket count
* active TCP connection count
* pending HTTP request count
* memory-pressure heuristic

It represents publisher admission capacity according to the documented index contract.

## Publisher Ownership

An active publisher id must not be silently replaced.

Registration of an already-active id must fail unless the current protocol explicitly provides a valid ownership-preserving path.

A process restart does not imply authority to replace an active registration.

Possession of valid local session state may allow authenticated continuation of an existing record.

Loss of that local state does not create an administrative bypass.

If a publisher loses its current session credential while the index record remains active, expiry is the recovery mechanism.

Do not add:

* force takeover
* administrative override over the public protocol
* weak fallback authentication
* identity guesses
* IP-based ownership
* hostname-based ownership

unless explicitly requested.

## Replay Resistance

Authenticated publisher-control operations use a strictly increasing sequence.

The index must reject reused or older sequence values.

The sequence is part of the publisher session state and must survive process restart when the session remains valid.

Do not replace monotonic sequence enforcement with timestamps unless explicitly requested.

Do not accept a control operation merely because its HMAC is otherwise valid if its sequence is stale.

## Local Publisher Session State

Successful publisher registration stores one index-host, index-port, and publisher-scoped session secret plus the persisted control sequence below `$HOME/.local/share/redp2p/keys/`.
The session secret proves ownership of the active publisher record for authenticated publisher-control operations; it does not identify a user or authenticate application traffic.

Preserve these properties:

* session state files remain local to the operator
* scoped filenames do not expose the index or publisher text
* POSIX session state files remain mode `0600`
* writes remain temporary-file replacements with durability checks
* reads reject malformed files, non-regular files, and links where the platform permits
* an unreadable or unrecognized scoped session-state file is discarded before a new registration; `kc_redp2p_ttl_expiry` covers this recovery, while a parsed session rejected with `invalid_proof` is not discarded or re-registered
* failed initial session-state storage rolls back index registration
* failed deregistration preserves the session state for retry
* successful deregistration removes only the session state whose secret matches the value actually used
* legacy identifier-named session state remains read-only migration input

Do not expand this narrow state into accounts, identity stores, history, synchronization, remote custody, or a general persistence layer.

## State Directory

REDP2P persists two kinds of local state under a single configurable directory:

* **keys/**: publisher session state containing the scoped session secret and persisted control sequence used by authenticated publisher-control operations.
* **pids/**: PID files for background processes started via the CLI.

Resolution follows the XDG Base Directory convention, per artifact class:

* publisher session state: `redp2p_set_state_dir()` override > `$XDG_DATA_HOME` > `$HOME/.local/share/redp2p/` (POSIX) or `$USERPROFILE/.local/share/redp2p/` (Windows)
* PID files: `--state-dir` flag or `REDP2P_STATE_DIR` environment variable > `$XDG_STATE_HOME` > `$HOME/.local/state/redp2p/` (POSIX) or `$USERPROFILE/.local/state/redp2p/` (Windows)

Publisher session state contains persistent credentials and therefore resolves against the data base directory. PID files describe running processes and therefore resolve against the state base directory.

The library creates subdirectories automatically. Applications embedding the library via `redp2p_set_state_dir()` control where state is stored. On Android this points to the app's private file directory where `$HOME` is unavailable.

PID files enable `--down` / `-d` to gracefully stop background processes by sending SIGTERM (or `TerminateProcess` on Windows). The naming scheme encodes the subcommand and address to avoid collisions: `idx_<port>.pid`, `pub_<sanitized_addr>.pid`, `con_<sanitized_addr>.pid`.
