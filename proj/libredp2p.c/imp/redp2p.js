/**
 * redp2p.js - Browser REDP2P implementation.
 * Summary: Provides the complete browser REDP2P publisher and consumer API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

"use strict";

const global = globalThis;
const encoder = new TextEncoder();

class RedP2PError extends Error {

    /**
     * Creates a protocol error.
     * @return Error object.
     */
    constructor(message, code = "internal", status = 0) {
        super(message);
        this.name = "RedP2PError";
        this.code = code;
        this.status = status;
    }
}

/**
 * Validates and normalizes an index URL.
 * @return Normalized URL.
 */
function normalizeIndexUrl(value) {
    const url = new URL(
        value,
        global.location ? global.location.href : undefined
    );
    if (!/^https?:$/.test(url.protocol)) {
        throw new RedP2PError(
            "Index URL must use HTTP or HTTPS",
            "bad_request"
        );
    }
    return url.href;
}

/**
 * Validates one safe protocol integer.
 * @return Whether the value is a safe integer in range.
 */
function safeProtocolInt(value, minimum = 0) {
    return Number.isSafeInteger(value) && value >= minimum;
}

/**
 * Validates one protocol hexadecimal token.
 * @return Whether the token has the expected canonical form.
 */
function protocolHex(value, length) {
    if (typeof value !== "string" || value.length !== length) {
        return false;
    }
    return /^[0-9a-f]+$/.test(value);
}

/**
 * Validates one session description received from an index.
 * @return Whether the description is protocol-valid.
 */
function validDescription(value, type) {
    return value &&
        typeof value === "object" &&
        !Array.isArray(value) &&
        value.type === type &&
        typeof value.sdp === "string" &&
        value.sdp.length > 0 &&
        value.sdp.length <= 49152;
}

/**
 * Sends an index protocol request.
 * @return Response payload.
 */
async function request(index, body, signal = undefined) {
    const response = await fetch(normalizeIndexUrl(index), {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({...body, version: 0}),
        cache: "no-store",
        credentials: "omit",
        signal
    });

    let payload;
    try {
        payload = await response.json();
    } catch (error) {
        throw new RedP2PError(
            `Index returned HTTP ${response.status}`,
            "http_error",
            response.status
        );
    }
    if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
        throw new RedP2PError(
            "Invalid index response",
            "bad_response",
            response.status
        );
    }
    if (!response.ok || payload.ok !== true) {
        throw new RedP2PError(
            payload.error || `HTTP ${response.status}`,
            payload.error || "http_error",
            response.status
        );
    }
    return payload;
}

/**
 * Waits for ICE gathering to finish.
 * @return None.
 */
async function waitIce(peer, timeoutMs = 15000) {
    if (peer.iceGatheringState === "complete") {
        return;
    }

    await new Promise(resolve => {
        let timeout = null;
        let finished = false;

        /**
         * Removes ICE wait resources.
         * @return None.
         */
        const cleanup = () => {
            if (timeout !== null) {
                clearTimeout(timeout);
            }
            peer.removeEventListener("icecandidate", candidateChanged);
            peer.removeEventListener("icegatheringstatechange", changed);
        };

        /**
         * Resolves ICE wait once.
         * @return None.
         */
        const finish = () => {
            if (finished) {
                return;
            }
            finished = true;
            cleanup();
            resolve();
        };

        /**
         * Handles the end-of-candidates notification.
         * @return None.
         */
        const candidateChanged = event => {
            if (event.candidate === null) {
                finish();
            }
        };

        /**
         * Handles gathering state changes.
         * @return None.
         */
        const changed = () => {
            if (peer.iceGatheringState === "complete") {
                finish();
            }
        };

        peer.addEventListener("icecandidate", candidateChanged);
        peer.addEventListener("icegatheringstatechange", changed);

        if (peer.iceGatheringState === "complete") {
            finish();
            return;
        }

        timeout = setTimeout(finish, timeoutMs);
    });
}

/**
 * Converts a session description to protocol data.
 * @return Description data.
 */
function descriptionObject(description) {
    return {type: description.type, sdp: description.sdp};
}

/**
 * Builds ICE server configuration from transport options.
 * @return ICE server entries.
 */
function iceServers(options = {}) {
    const servers = [];
    if (options.stun) {
        servers.push({urls: options.stun});
    }
    if (options.turn) {
        const relay = {urls: options.turn};
        if (options.turn_user !== undefined) {
            relay.username = options.turn_user;
        }
        if (options.turn_pass !== undefined) {
            relay.credential = options.turn_pass;
        }
        servers.push(relay);
    }
    return servers;
}

/**
 * Creates a WebRTC peer connection from REDP2P transport options.
 * @return Peer connection.
 */
function createPeer(options = {}) {
    return new RTCPeerConnection({iceServers: iceServers(options)});
}

/**
 * Encodes bytes as hexadecimal.
 * @return Hexadecimal string.
 */
function bytesToHex(bytes) {
    return Array.from(bytes, value =>
        value.toString(16).padStart(2, "0")).join("");
}

/**
 * Decodes a hexadecimal string.
 * @return Byte array.
 */
function hexToBytes(hex) {
    if (typeof hex !== "string" || hex.length % 2 !== 0 ||
        !/^[0-9a-f]+$/i.test(hex)) {
        throw new RedP2PError("Invalid hexadecimal value", "bad_request");
    }

    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i += 1) {
        bytes[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    }
    return bytes;
}

/**
 * Concatenates byte arrays.
 * @return Concatenated byte array.
 */
function concatBytes(...parts) {
    const length = parts.reduce((total, part) => total + part.length, 0);
    const output = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) {
        output.set(part, offset);
        offset += part.length;
    }
    return output;
}

/**
 * Encodes one integer as unsigned 16-bit big-endian bytes.
 * @return Two-byte representation.
 */
function u16be(value) {
    const bytes = new Uint8Array(2);
    new DataView(bytes.buffer).setUint16(0, value, false);
    return bytes;
}

/**
 * Encodes one safe integer as unsigned 64-bit big-endian bytes.
 * @return Eight-byte representation.
 */
function u64be(value) {
    if (!Number.isSafeInteger(value) || value < 0) {
        throw new RedP2PError("Invalid protocol integer", "bad_request");
    }
    const bytes = new Uint8Array(8);
    const view = new DataView(bytes.buffer);
    const high = Math.floor(value / 4294967296);
    const low = value % 4294967296;
    view.setUint32(0, high, false);
    view.setUint32(4, low, false);
    return bytes;
}

/**
 * Generates random hexadecimal data.
 * @return Hexadecimal string.
 */
function randomHex(size) {
    const bytes = new Uint8Array(size);
    crypto.getRandomValues(bytes);
    return bytesToHex(bytes);
}

/**
 * Hashes text with SHA-256.
 * @return Hexadecimal digest.
 */
async function sha256(text) {
    const digest = await crypto.subtle.digest(
        "SHA-256",
        encoder.encode(text)
    );
    return bytesToHex(new Uint8Array(digest));
}

/**
 * Calculates an HMAC digest.
 * @return Hexadecimal digest.
 */
async function hmacHex(keyText, message) {
    return hmacBytesHex(keyText, encoder.encode(message));
}

/**
 * Calculates an HMAC digest over raw bytes.
 * @return Hexadecimal digest.
 */
async function hmacBytesHex(keyText, message) {
    const key = await crypto.subtle.importKey(
        "raw",
        encoder.encode(keyText),
        {name: "HMAC", hash: "SHA-256"},
        false,
        ["sign"]
    );
    const signature = await crypto.subtle.sign("HMAC", key, message);
    return bytesToHex(new Uint8Array(signature));
}

/**
 * Counts leading zero bits.
 * @return Number of leading zero bits.
 */
function leadingZeroBits(bytes) {
    let bits = 0;
    for (const value of bytes) {
        if (value === 0) {
            bits += 8;
            continue;
        }
        for (let bit = 7; bit >= 0; bit -= 1) {
            if ((value & (1 << bit)) !== 0) {
                return bits;
            }
            bits += 1;
        }
    }
    return bits;
}

/**
 * Solves a registration proof of work.
 * @return Proof solution.
 */
async function solvePow(id, nonce, issuedAt, expiresAt, bits) {
    if (!Number.isInteger(bits) || bits < 0 || bits > 32) {
        throw new RedP2PError(
            "Invalid proof-of-work difficulty",
            "bad_response"
        );
    }
    if (bits === 0) {
        return "0000000000000000";
    }

    let value = 0n;
    let iterations = 0;
    while (value <= 0xffffffffffffffffn) {
        const solution = value.toString(16).padStart(16, "0");
        const idBytes = encoder.encode(id);
        const input = concatBytes(
            encoder.encode("REDP2P-POW"),
            hexToBytes(nonce),
            u64be(issuedAt),
            u64be(expiresAt),
            u16be(idBytes.length),
            idBytes,
            hexToBytes(solution)
        );
        const digest = new Uint8Array(
            await crypto.subtle.digest("SHA-256", input)
        );
        if (leadingZeroBits(digest) >= bits) {
            return solution;
        }
        value += 1n;
        iterations += 1;
        if (iterations % 2048 === 0) {
            await new Promise(resolve => setTimeout(resolve, 0));
        }
    }

    throw new RedP2PError("Unable to solve proof of work", "auth_failed");
}

/**
 * Validates a registration challenge response.
 * @return Validated challenge.
 */
function validateChallenge(value) {
    if (
        !value ||
        !protocolHex(value.nonce, 64) ||
        !protocolHex(value.mac, 64) ||
        !safeProtocolInt(value.issued_at) ||
        !safeProtocolInt(value.expires_at) ||
        value.expires_at - value.issued_at !== 60 ||
        !Number.isInteger(value.bits) ||
        value.bits < 0 ||
        value.bits > 32
    ) {
        throw new RedP2PError(
            "Invalid challenge response",
            "bad_response"
        );
    }
    return value;
}

/**
 * Builds a canonical registration payload.
 * @return Canonical payload.
 */
function registerCanonical(fields) {
    const id = encoder.encode(fields.id);
    const secret = encoder.encode(fields.secret);
    return concatBytes(
        encoder.encode("REDP2P-WEB-REGISTER"),
        hexToBytes(fields.nonce),
        u64be(fields.issued_at),
        u64be(fields.expires_at),
        u16be(id.length),
        id,
        u16be(secret.length),
        secret,
        hexToBytes(fields.pow_solution)
    );
}

/**
 * Builds a canonical control payload.
 * @return Canonical payload.
 */
function controlCanonical(op, id, seq, extra = []) {
    return [op, id, String(seq), ...extra.map(value =>
        String(value))].join("\n");
}

/**
 * Advances a control sequence and creates its proof.
 * @return Sequence and proof.
 */
async function nextProof(state, op, extra = []) {
    if (!Number.isSafeInteger(state.seq) || state.seq < 0 ||
        state.seq >= Number.MAX_SAFE_INTEGER) {
        throw new RedP2PError(
            "Publisher control sequence exhausted",
            "sequence_exhausted"
        );
    }

    state.seq += 1;
    if (typeof state.persist === "function") {
        state.persist();
    }
    const message = controlCanonical(op, state.id, state.seq, extra);
    return {
        seq: state.seq,
        proof: await hmacHex(state.secret, message)
    };
}

/**
 * Creates a persistent state key.
 * @return Storage key.
 */
function storageKey(index, id) {
    return `redp2p-web:v0:${index}:${id}`;
}

/**
 * Loads persisted publisher state.
 * @return State or null.
 */
function loadState(index, id) {
    try {
        const raw = global.localStorage.getItem(storageKey(index, id));
        if (!raw) {
            return null;
        }
        const value = JSON.parse(raw);
        if (value && value.version === 0 && value.id === id &&
            typeof value.secret === "string" &&
            /^[0-9a-f]{16}$/.test(value.secret) &&
            Number.isSafeInteger(value.seq) && value.seq >= 0) {
            return value;
        }
    } catch (error) {
        return null;
    }
    return null;
}

/**
 * Persists publisher state.
 * @return None.
 */
function saveState(index, state) {
    try {
        global.localStorage.setItem(
            storageKey(index, state.id),
            JSON.stringify({
                version: 0,
                id: state.id,
                secret: state.secret,
                seq: state.seq
            })
        );
    } catch (error) {
        return;
    }
}

/**
 * Removes persisted publisher state.
 * @return None.
 */
function removeState(index, id) {
    try {
        global.localStorage.removeItem(storageKey(index, id));
    } catch (error) {
        return;
    }
}

class RedP2PClient {

    /**
     * Creates one application-facing remote client.
     * @return Client capability.
     */
    constructor(peer, channel, handlers = {}) {
        this._peer = peer;
        this._channel = channel;
        this._connect = typeof handlers.connect === "function"
            ? handlers.connect
            : null;
        this._receive = typeof handlers.receive === "function"
            ? handlers.receive
            : null;
        this._disconnect = typeof handlers.disconnect === "function"
            ? handlers.disconnect
            : null;
        this._error = typeof handlers.error === "function"
            ? handlers.error
            : null;

        channel.binaryType = "arraybuffer";

        /**
         * Reports an opened channel.
         * @return None.
         */
        const connected = () => {
            if (this._connect) {
                this._connect(this);
            }
        };
        if (channel.readyState === "open") {
            connected();
        } else {
            channel.addEventListener("open", connected, {once: true});
        }
        channel.addEventListener("message", event => {
            if (this._receive) {
                this._receive({client: this, data: event.data});
            }
        });
        channel.addEventListener("close", () => {
            this._peer.close();
            if (this._disconnect) {
                this._disconnect(this);
            }
        });
        channel.addEventListener("error", () => {
            this._peer.close();
            if (this._error) {
                this._error(
                    new RedP2PError(
                        "Client connection failed",
                        "connection_failed"
                    ),
                    this
                );
            }
        });
        peer.addEventListener("connectionstatechange", () => {
            if (peer.connectionState === "failed") {
                peer.close();
            }
        });
    }

    /**
     * Responds to this client.
     * @return None.
     */
    respond(data) {
        this._channel.send(data);
    }

    /**
     * Closes this client.
     * @return None.
     */
    close() {
        this._channel.close();
        this._peer.close();
    }
}

class Publisher {

    /**
     * Creates a publisher capability.
     * @return Publisher capability.
     */
    constructor(options) {
        this.options = {...options};
        this.registrations = [];
        this.connections = new Map();
        this.rejectedConnections = new Map();
        this.closed = false;
        this.connect = typeof options.connect === "function"
            ? options.connect : null;
        this.receive = typeof options.receive === "function"
            ? options.receive : null;
        this.disconnect = typeof options.disconnect === "function"
            ? options.disconnect : null;
        this.error = typeof options.error === "function"
            ? options.error : null;
    }

    /**
     * Registers the publisher with its indexes.
     * @return Started publisher.
     */
    async start() {
        const indexes = this.indexOptions();
        try {
            for (const item of indexes) {
                const state = await this.openRegistration(item);
                this.registrations.push(state);
            }
        } catch (error) {
            await this.close();
            throw error;
        }

        for (const state of this.registrations) {
            state.heartbeatTimer = setInterval(() => {
                const generation = state.generation;
                this.heartbeat(state).catch(error => {
                    this.controlFailure(state, generation, error);
                });
            }, this.options.heartbeatInterval || 15000);

            state.pollTimer = setInterval(() => {
                const generation = state.generation;
                this.poll(state).catch(error => {
                    this.controlFailure(state, generation, error);
                });
            }, this.options.pollInterval || 3000);
        }
        return this;
    }

    /**
     * Updates one live transport option.
     * @return Publisher capability.
     */
    async set(option, value) {
        if (this.closed) {
            throw new RedP2PError("Publisher is closed", "bad_request");
        }
        if (option === "stun") {
            if (typeof value !== "string" || value.length === 0) {
                throw new RedP2PError(
                    "Invalid STUN endpoint",
                    "bad_request"
                );
            }
            this.options.stun = value;
        } else if (option === "turn") {
            if (!value || typeof value !== "object" ||
                typeof value.url !== "string" || value.url.length === 0 ||
                (value.user === undefined) !== (value.pass === undefined)) {
                throw new RedP2PError(
                    "Invalid TURN configuration",
                    "bad_request"
                );
            }
            if (this.options.turn && this.options.turn !== value.url &&
                this.connections.size > 0) {
                throw new RedP2PError(
                    "Changing an active TURN endpoint is unsupported",
                    "unsupported"
                );
            }
            this.options.turn = value.url;
            this.options.turn_user = value.user;
            this.options.turn_pass = value.pass;
        } else {
            throw new RedP2PError("Unsupported option", "unsupported");
        }

        for (const peer of this.connections.values()) {
            if (peer.connectionState !== "new") {
                continue;
            }
            const configuration = peer.getConfiguration();
            configuration.iceServers = iceServers(this.options);
            peer.setConfiguration(configuration);
        }
        return this;
    }

    /**
     * Normalizes publisher index options.
     * @return Index options.
     */
    indexOptions() {
        if (Array.isArray(this.options.indexes)) {
            return this.options.indexes.map(item => ({
                url: normalizeIndexUrl(item.url || item.index),
                id: item.id || this.options.id,
                pass: item.pass
            }));
        }
        if (!this.options.index || !this.options.id) {
            throw new RedP2PError(
                "pub requires id and index",
                "bad_request"
            );
        }
        return [{
            url: normalizeIndexUrl(this.options.index),
            id: this.options.id,
            pass: this.options.pass
        }];
    }

    /**
     * Opens one index registration.
     * @return Registration state.
     */
    async openRegistration(item) {
        if (!/^[A-Za-z0-9]{1,63}$/.test(item.id || "")) {
            throw new RedP2PError("Invalid publisher id", "invalid_id");
        }
        const saved = loadState(item.url, item.id);
        if (saved) {
            const state = this.state(item, saved.secret, saved.seq);
            try {
                await this.heartbeat(state);
                return state;
            } catch (error) {
                if (error.code === "not_found") {
                    removeState(item.url, item.id);
                } else {
                    throw error;
                }
            }
        }
        const fresh = await this.freshRegistration(item);
        const state = this.state(item, fresh.secret, 0);
        saveState(item.url, state);
        return state;
    }

    /**
     * Creates one fresh index registration.
     * @return Fresh registration material.
     */
    async freshRegistration(item) {
        const challenge = validateChallenge(
            await request(item.url, {op: "challenge", id: item.id})
        );
        const secret = randomHex(8);
        const solution = await solvePow(
            item.id,
            challenge.nonce,
            challenge.issued_at,
            challenge.expires_at,
            challenge.bits
        );
        const fields = {
            id: item.id,
            nonce: challenge.nonce,
            issued_at: challenge.issued_at,
            expires_at: challenge.expires_at,
            secret,
            pow_solution: solution,
            transport: "rtc"
        };
        const canonical = registerCanonical(fields);
        const requestBody = {
            op: "register",
            ...fields,
            mac: challenge.mac,
            proof: await hmacBytesHex(secret, canonical)
        };
        if (item.pass) {
            requestBody.access_proof = await hmacBytesHex(
                item.pass,
                concatBytes(
                    encoder.encode("REDP2P-ADMISSION-v1"),
                    canonical
                )
            );
        }
        await request(item.url, requestBody);
        return {secret};
    }

    /**
     * Creates persistent registration state.
     * @return Registration state.
     */
    state(item, secret, seq) {
        const state = {
            url: item.url,
            id: item.id,
            secret,
            seq,
            pass: item.pass,
            generation: 0,
            heartbeatTimer: null,
            pollTimer: null,
            queue: Promise.resolve()
        };
        state.persist = () => saveState(state.url, state);
        state.run = task => {
            const operation = state.queue.then(task, task);
            state.queue = operation.catch(() => undefined);
            return operation;
        };
        return state;
    }

    /**
     * Sends a publisher heartbeat.
     * @return None.
     */
    async heartbeat(state) {
        if (this.closed) {
            return;
        }
        return state.run(async () => {
            const auth = await nextProof(state, "heartbeat");
            await request(state.url, {
                op: "heartbeat",
                id: state.id,
                ...auth
            });
        });
    }

    /**
     * Polls an index for consumers.
     * @return None.
     */
    async poll(state) {
        if (this.closed || state.polling) {
            return;
        }
        state.polling = true;
        try {
            await state.run(async () => {
                const auth = await nextProof(state, "poll");
                const response = await request(state.url, {
                    op: "poll",
                    id: state.id,
                    ...auth
                });
                if (!Array.isArray(response.connections) ||
                    response.connections.length > 4) {
                    throw new RedP2PError(
                        "Invalid publisher poll response",
                        "bad_response"
                    );
                }
                this.pruneRejectedConnections();
                for (const pending of response.connections) {
                    if (!pending ||
                        !protocolHex(pending.connection, 32) ||
                        !validDescription(pending.offer, "offer")) {
                        throw new RedP2PError(
                            "Invalid consumer signaling response",
                            "bad_response"
                        );
                    }
                    if (!this.connections.has(pending.connection) &&
                        !this.rejectedConnections.has(pending.connection)) {
                        this.accept(state, pending).catch(error =>
                            this.fail(error));
                    }
                }
            });
        } finally {
            state.polling = false;
        }
    }

    /**
     * Accepts a pending consumer connection.
     * @return None.
     */
    async accept(state, pending) {
        const peer = createPeer(this.options);
        this.connections.set(pending.connection, peer);
        try {
            peer.addEventListener("connectionstatechange", () => {
                if (peer.connectionState === "failed") {
                    peer.close();
                }
                if (peer.connectionState === "closed" ||
                    peer.connectionState === "failed") {
                    this.connections.delete(pending.connection);
                }
            });

            peer.addEventListener("datachannel", event => {
                new RedP2PClient(peer, event.channel, {
                    connect: this.connect,
                    receive: this.receive,
                    disconnect: client => {
                        this.connections.delete(pending.connection);
                        if (this.disconnect) {
                            this.disconnect(client);
                        }
                    },
                    error: (error, client) => {
                        if (this.error) {
                            this.error(error, client);
                        }
                    }
                });
            });

            await peer.setRemoteDescription(pending.offer);
            const answer = await peer.createAnswer();
            await peer.setLocalDescription(answer);
            await waitIce(peer);

            const description = descriptionObject(peer.localDescription);
            const digest = await sha256(
                `${description.type}\n${description.sdp}`
            );
            await state.run(async () => {
                const auth = await nextProof(state, "answer", [
                    pending.connection,
                    digest
                ]);
                await request(state.url, {
                    op: "answer",
                    id: state.id,
                    connection: pending.connection,
                    answer: description,
                    ...auth
                });
            });
        } catch (error) {
            this.connections.delete(pending.connection);
            this.rejectedConnections.set(
                pending.connection,
                Date.now() + 31000
            );
            peer.close();
            throw error;
        }
    }

    /**
     * Removes expired rejected signaling identifiers.
     * @return None.
     */
    pruneRejectedConnections() {
        const now = Date.now();
        for (const [connection, expiresAt] of
            this.rejectedConnections.entries()) {
            if (expiresAt <= now) {
                this.rejectedConnections.delete(connection);
            }
        }
    }

    /**
     * Handles a failed authenticated index control operation.
     * @return None.
     */
    controlFailure(state, generation, error) {
        if (error && error.code === "not_found" &&
            state.generation === generation && !this.closed) {
            this.restoreRegistration(state, generation).catch(failure => {
                this.fail(failure);
            });
            return;
        }
        this.fail(error);
    }

    /**
     * Re-registers a publisher whose index state disappeared.
     * @return None.
     */
    async restoreRegistration(state, generation) {
        return state.run(async () => {
            if (this.closed || state.generation !== generation) {
                return;
            }
            removeState(state.url, state.id);
            const fresh = await this.freshRegistration({
                url: state.url,
                id: state.id,
                pass: state.pass
            });
            state.secret = fresh.secret;
            state.seq = 0;
            state.generation += 1;
            saveState(state.url, state);
        });
    }

    /**
     * Reports a publisher error.
     * @return None.
     */
    fail(error) {
        if (this.error) {
            this.error(error);
        }
    }

    /**
     * Deregisters and closes the publisher.
     * @return None.
     */
    async close() {
        if (this.closed) {
            return;
        }
        this.closed = true;
        for (const state of this.registrations) {
            clearInterval(state.heartbeatTimer);
            clearInterval(state.pollTimer);
        }
        for (const peer of this.connections.values()) {
            peer.close();
        }
        this.connections.clear();
        this.rejectedConnections.clear();
        for (const state of this.registrations) {
            try {
                await state.run(async () => {
                    const auth = await nextProof(state, "deregister");
                    await request(state.url, {
                        op: "deregister",
                        id: state.id,
                        ...auth
                    });
                });
                removeState(state.url, state.id);
            } catch (error) {
                this.fail(error);
            }
        }
    }
}

class RedP2PConsumer {

    /**
     * Creates one application-facing consumer.
     * @return Consumer capability.
     */
    constructor(peer, channel, handlers = {}, options = {}) {
        this._peer = peer;
        this._channel = channel;
        this._options = {...options};
        this._receive = typeof handlers.receive === "function"
            ? handlers.receive
            : null;
        this._disconnect = typeof handlers.disconnect === "function"
            ? handlers.disconnect
            : null;
        this._error = typeof handlers.error === "function"
            ? handlers.error
            : null;

        channel.binaryType = "arraybuffer";
        channel.addEventListener("message", event => {
            if (this._receive) {
                this._receive(event.data);
            }
        });
        channel.addEventListener("close", () => {
            this._peer.close();
            if (this._disconnect) {
                this._disconnect();
            }
        });
        channel.addEventListener("error", () => {
            this._peer.close();
            if (this._error) {
                this._error(
                    new RedP2PError("Connection failed", "connection_failed")
                );
            }
        });
        peer.addEventListener("connectionstatechange", () => {
            if (peer.connectionState === "failed") {
                peer.close();
            }
        });
    }

    /**
     * Updates one live transport option.
     * @return Consumer capability.
     */
    async set(option, value) {
        if (option === "stun") {
            if (typeof value !== "string" || value.length === 0) {
                throw new RedP2PError(
                    "Invalid STUN endpoint",
                    "bad_request"
                );
            }
            this._options.stun = value;
        } else if (option === "turn") {
            if (!value || typeof value !== "object" ||
                typeof value.url !== "string" || value.url.length === 0 ||
                (value.user === undefined) !== (value.pass === undefined)) {
                throw new RedP2PError(
                    "Invalid TURN configuration",
                    "bad_request"
                );
            }
            this._options.turn = value.url;
            this._options.turn_user = value.user;
            this._options.turn_pass = value.pass;
        } else {
            throw new RedP2PError("Unsupported option", "unsupported");
        }

        if (this._peer.connectionState === "new") {
            const configuration = this._peer.getConfiguration();
            configuration.iceServers = iceServers(this._options);
            this._peer.setConfiguration(configuration);
        }
        return this;
    }

    /**
     * Sends data to the publisher.
     * @return None.
     */
    send(data) {
        this._channel.send(data);
    }

    /**
     * Closes the consumer.
     * @return None.
     */
    close() {
        this._channel.close();
        this._peer.close();
    }
}

/**
 * Validates a consumer connection response.
 * @return Validated connection response.
 */
function validateConnection(value) {
    if (
        !value ||
        !protocolHex(value.connection, 32) ||
        !protocolHex(value.capability, 64) ||
        !safeProtocolInt(value.expires_at)
    ) {
        throw new RedP2PError("Invalid connection response", "bad_response");
    }
    return value;
}

/**
 * Waits for a data channel to open.
 * @return None.
 */
async function waitChannel(channel, timeoutMs = 30000) {
    if (channel.readyState === "open") {
        return;
    }

    await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            cleanup();
            reject(new RedP2PError(
                "Data channel timed out",
                "connection_timeout"
            ));
        }, timeoutMs);

        /**
         * Resolves an opened channel wait.
         * @return None.
         */
        const opened = () => {
            cleanup();
            resolve();
        };

        /**
         * Rejects a failed channel wait.
         * @return None.
         */
        const failed = () => {
            cleanup();
            reject(new RedP2PError(
                "Data channel failed",
                "connection_failed"
            ));
        };

        /**
         * Removes channel wait resources.
         * @return None.
         */
        const cleanup = () => {
            clearTimeout(timeout);
            channel.removeEventListener("open", opened);
            channel.removeEventListener("close", failed);
            channel.removeEventListener("error", failed);
        };

        channel.addEventListener("open", opened, {once: true});
        channel.addEventListener("close", failed, {once: true});
        channel.addEventListener("error", failed, {once: true});
    });
}

/**
 * Connects to a publisher through an index.
 * @return A connected client capability.
 */
async function connect(options) {
    const index = normalizeIndexUrl(options.index);
    const id = options.id;

    if (!/^[A-Za-z0-9]{1,63}$/.test(id || "")) {
        throw new RedP2PError("Invalid publisher id", "invalid_id");
    }

    const peer = createPeer(options);
    const dataChannel = peer.createDataChannel("redp2p", {
        ordered: options.ordered !== false
    });

    try {
        const offer = await peer.createOffer();
        await peer.setLocalDescription(offer);
        await waitIce(peer);

        const response = validateConnection(await request(index, {
            op: "connect",
            id,
            offer: descriptionObject(peer.localDescription)
        }));

        const timeoutAt = Math.min(
            Date.now() + (options.timeout || 30000),
            response.expires_at * 1000
        );

        let answer = null;
        while (Date.now() < timeoutAt) {
            const polled = await request(index, {
                op: "poll",
                connection: response.connection,
                capability: response.capability
            });

            if (polled.answer !== null && polled.answer !== undefined) {
                if (!validDescription(polled.answer, "answer")) {
                    throw new RedP2PError(
                        "Invalid publisher signaling response",
                        "bad_response"
                    );
                }
                answer = polled.answer;
                break;
            }

            await new Promise(resolve => setTimeout(
                resolve,
                options.pollInterval || 500
            ));
        }

        if (!answer) {
            throw new RedP2PError(
                "Connection negotiation timed out",
                "connection_timeout"
            );
        }

        await peer.setRemoteDescription(answer);
        await waitChannel(dataChannel, Math.max(1, timeoutAt - Date.now()));
        return new RedP2PConsumer(peer, dataChannel, {
            receive: options.receive,
            disconnect: options.disconnect,
            error: options.error
        }, options);
    } catch (error) {
        peer.close();
        throw error;
    }
}

/**
 * Starts a browser publisher.
 * @return Started publisher capability.
 */
async function pub(options = {}) {
    return new Publisher(options).start();
}

/**
 * Connects a browser consumer.
 * @return Connected consumer capability.
 */
async function con(options = {}) {
    return connect(options);
}

const RedP2P = {
    Error: RedP2PError,
    pub,
    con,
    normalizeIndexUrl,
    safeProtocolInt,
    protocolHex,
    validDescription,
    request,
    waitIce,
    descriptionObject,
    iceServers,
    createPeer
};

global.RedP2P = RedP2P;

export {pub, con};
export default RedP2P;
