/**
 * redp2p-con.js - Browser consumer capability.
 * Summary: Provides the standalone browser REDP2P con implementation.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

"use strict";

(function (global) {
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();

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
                    this._receive({
                        client: this,
                        data: event.data
                    });
                }
            });
            channel.addEventListener("close", () => {
                if (this._disconnect) {
                    this._disconnect(this);
                }
            });
            channel.addEventListener("error", () => {
                if (this._error) {
                    this._error(
                        new RedP2PError("Client connection failed", "connection_failed"),
                        this
                    );
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
                if (this._disconnect) {
                    this._disconnect();
                }
            });
            channel.addEventListener("error", () => {
                if (this._error) {
                    this._error(
                        new RedP2PError("Connection failed", "connection_failed")
                    );
                }
            });
        }

        /**
         * Updates one supported live peer option.
         * @return Consumer capability.
         */
        set(option, value) {
            const active = this._peer.connectionState !== "closed";
            this._options = updatePeerOptions(
                this._options,
                option,
                value,
                active
            );
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
     * Encodes bytes as hexadecimal.
     * @return Hexadecimal string.
     */
    function bytesToHex(bytes) {
        return Array.from(bytes, value => value.toString(16).padStart(2, "0")).join("");
    }

    /**
     * Decodes a hexadecimal string.
     * @return Byte array.
     */
    function hexToBytes(hex) {
        if (typeof hex !== "string" || hex.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(hex)) {
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
        const digest = await crypto.subtle.digest("SHA-256", encoder.encode(text));
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
            throw new RedP2PError("Invalid proof-of-work difficulty", "bad_response");
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
            const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", input));
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
     * Validates and normalizes an index URL.
     * @return Normalized URL.
     */
    function normalizeIndexUrl(value) {
        const url = new URL(value, global.location ? global.location.href : undefined);
        if (!/^https?:$/.test(url.protocol)) {
            throw new RedP2PError("Index URL must use HTTP or HTTPS", "bad_request");
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
            throw new RedP2PError("Invalid challenge response", "bad_response");
        }
        return value;
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
            throw new RedP2PError(`Index returned HTTP ${response.status}`, "http_error", response.status);
        }

        if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
            throw new RedP2PError("Invalid index response", "bad_response", response.status);
        }

        if (!response.ok || payload.ok !== true) {
            throw new RedP2PError(
                payload && payload.error ? payload.error : `HTTP ${response.status}`,
                payload && payload.error ? payload.error : "http_error",
                response.status
            );
        }

        return payload;
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
        return [op, id, String(seq), ...extra.map(value => String(value))].join("\n");
    }

    /**
     * Advances a control sequence and creates its proof.
     * @return Sequence and proof.
     */
    async function nextProof(state, op, extra = []) {
        if (
            !Number.isSafeInteger(state.seq) ||
            state.seq < 0 ||
            state.seq >= Number.MAX_SAFE_INTEGER
        ) {
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
            if (
                value &&
                value.version === 0 &&
                value.id === id &&
                typeof value.secret === "string" &&
                /^[0-9a-f]{16}$/.test(value.secret) &&
                Number.isSafeInteger(value.seq) &&
                value.seq >= 0
            ) {
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
            global.localStorage.setItem(storageKey(index, state.id), JSON.stringify({
                version: 0,
                id: state.id,
                secret: state.secret,
                seq: state.seq
            }));
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

    /**
     * Waits for ICE gathering to finish.
     * @return None.
     */
    async function waitIce(peer) {
        if (peer.iceGatheringState === "complete") {
            return;
        }

        await new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                cleanup();
                reject(new RedP2PError("ICE gathering timed out", "ice_timeout"));
            }, 15000);

            /**
             * Handles ICE gathering state changes.
             * @return None.
             */
            const changed = () => {
                if (peer.iceGatheringState === "complete") {
                    cleanup();
                    resolve();
                }
            };

            /**
             * Removes ICE wait resources.
             * @return None.
             */
            const cleanup = () => {
                clearTimeout(timeout);
                peer.removeEventListener("icegatheringstatechange", changed);
            };

            peer.addEventListener("icegatheringstatechange", changed);
        });
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
                reject(new RedP2PError("Data channel timed out", "connection_timeout"));
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
                reject(new RedP2PError("Data channel failed", "connection_failed"));
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
     * Converts a session description to protocol data.
     * @return Description data.
     */
    function descriptionObject(description) {
        return {type: description.type, sdp: description.sdp};
    }

    /**
     * Builds WebRTC ICE servers from REDP2P transport options.
     * @return ICE server list.
     */
    function iceServers(options = {}) {
        const servers = [];

        if (options.stun) {
            servers.push({urls: options.stun});
        }
        if (options.turn) {
            const relay = {urls: options.turn};

            if (options.turn_user !== undefined && options.turn_user !== null) {
                relay.username = options.turn_user;
            }
            if (options.turn_pass !== undefined && options.turn_pass !== null) {
                relay.credential = options.turn_pass;
            }
            servers.push(relay);
        }
        return servers;
    }

    /**
     * Returns transport options with one supported live option updated.
     * @return Updated transport options.
     */
    function updatePeerOptions(options, option, value, active = false) {
        const next = {...options};

        if (option === "stun") {
            if (typeof value !== "string" || value.length === 0) {
                throw new RedP2PError("Invalid STUN endpoint", "bad_request");
            }
            next.stun = value;
            return next;
        }

        if (option !== "turn") {
            throw new RedP2PError("Unsupported peer option", "unsupported");
        }
        if (
            !value ||
            typeof value !== "object" ||
            Array.isArray(value) ||
            typeof value.url !== "string" ||
            !value.url.startsWith("turn:")
        ) {
            throw new RedP2PError("Invalid TURN configuration", "bad_request");
        }

        const hasUser = value.user !== undefined && value.user !== null;
        const hasPass = value.pass !== undefined && value.pass !== null;
        if (
            hasUser !== hasPass ||
            (hasUser && typeof value.user !== "string") ||
            (hasPass && typeof value.pass !== "string")
        ) {
            throw new RedP2PError("Invalid TURN configuration", "bad_request");
        }
        if (active && next.turn && next.turn !== value.url) {
            throw new RedP2PError(
                "Changing TURN endpoint on an active peer is not supported",
                "unsupported"
            );
        }

        next.turn = value.url;
        next.turn_user = hasUser ? value.user : undefined;
        next.turn_pass = hasPass ? value.pass : undefined;
        return next;
    }

    /**
     * Creates a WebRTC peer connection from REDP2P transport options.
     * @return Peer connection.
     */
    function createPeer(options = {}) {
        return new RTCPeerConnection({iceServers: iceServers(options)});
    }

    global.RedP2PCore = Object.freeze({
        Error: RedP2PError,
        Client: RedP2PClient,
        Consumer: RedP2PConsumer,
        bytesToHex,
        hexToBytes,
        concatBytes,
        randomHex,
        sha256,
        hmacHex,
        hmacBytesHex,
        solvePow,
        request,
        safeProtocolInt,
        protocolHex,
        validDescription,
        validateChallenge,
        validateConnection,
        registerCanonical,
        controlCanonical,
        nextProof,
        normalizeIndexUrl,
        loadState,
        saveState,
        removeState,
        waitIce,
        waitChannel,
        descriptionObject,
        iceServers,
        updatePeerOptions,
        createPeer,
        decoder
    });

    if (!global.RedP2P) {
        global.RedP2P = {};
    }
})(globalThis);

(function (global) {
    const core = global.RedP2PCore;
    if (!core) {
        throw new Error("redp2p-core.js must be loaded before redp2p-con.js");
    }

    /**
     * Connects to a publisher through an index.
     * @param options Connection options.
     * @return A connected client capability.
     */
    async function connect(options) {
        const index = core.normalizeIndexUrl(options.index);
        const id = options.id;

        if (!/^[A-Za-z0-9]{1,63}$/.test(id || "")) {
            throw new core.Error("Invalid publisher id", "invalid_id");
        }

        const peer = core.createPeer(options);
        const dataChannel = peer.createDataChannel("redp2p", {
            ordered: options.ordered !== false
        });

        try {
            const offer = await peer.createOffer();
            await peer.setLocalDescription(offer);
            await core.waitIce(peer);

            const response = core.validateConnection(await core.request(index, {
                op: "connect",
                id,
                offer: core.descriptionObject(peer.localDescription)
            }));

            const timeoutAt = Math.min(
                Date.now() + (options.timeout || 30000),
                response.expires_at * 1000
            );

            let answer = null;
            while (Date.now() < timeoutAt) {
                const polled = await core.request(index, {
                    op: "poll",
                    connection: response.connection,
                    capability: response.capability
                });

                if (polled.answer !== null && polled.answer !== undefined) {
                    if (!core.validDescription(polled.answer, "answer")) {
                        throw new core.Error(
                            "Invalid consumer signaling response",
                            "bad_response"
                        );
                    }
                    answer = polled.answer;
                    break;
                }

                await new Promise(resolve => setTimeout(resolve, options.pollInterval || 500));
            }

            if (!answer) {
                throw new core.Error("Connection negotiation timed out", "connection_timeout");
            }

            await peer.setRemoteDescription(answer);
            await core.waitChannel(dataChannel, Math.max(1, timeoutAt - Date.now()));
            return new core.Consumer(peer, dataChannel, {
                receive: options.receive,
                disconnect: options.disconnect,
                error: options.error
            }, options);
        } catch (error) {
            peer.close();
            throw error;
        }
    }

    global.RedP2P.con = async function (options) {
        return connect(options || {});
    };
})(globalThis);
