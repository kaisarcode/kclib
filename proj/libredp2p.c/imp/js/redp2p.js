/**
 * redp2p.js - Browser REDP2P shared runtime.
 * Summary: Provides reusable browser REDP2P protocol and WebRTC helpers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

"use strict";

(function (global) {
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
        if (!payload || typeof payload !== "object" ||
            Array.isArray(payload)) {
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

            /** Removes ICE wait resources. */
            const cleanup = () => {
                if (timeout !== null) {
                    clearTimeout(timeout);
                }
                peer.removeEventListener("icecandidate", candidateChanged);
                peer.removeEventListener("icegatheringstatechange", changed);
            };

            /** Resolves ICE wait once. */
            const finish = () => {
                if (finished) {
                    return;
                }
                finished = true;
                cleanup();
                resolve();
            };

            /** Handles the end-of-candidates notification. */
            const candidateChanged = event => {
                if (event.candidate === null) {
                    finish();
                }
            };

            /** Handles gathering state changes. */
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

    if (!global.RedP2P) {
        global.RedP2P = {};
    }

    Object.assign(global.RedP2P, {
        Error: RedP2PError,
        normalizeIndexUrl,
        safeProtocolInt,
        protocolHex,
        validDescription,
        request,
        waitIce,
        descriptionObject,
        iceServers,
        createPeer
    });
})(globalThis);
