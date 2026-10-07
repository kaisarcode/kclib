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
    const core = global.RedP2P;
    if (!core || typeof core.createPeer !== "function") {
        throw new Error("redp2p.js must be loaded before redp2p-con.js");
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
                        new core.Error("Connection failed", "connection_failed")
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
                    throw new core.Error(
                        "Invalid STUN endpoint",
                        "bad_request"
                    );
                }
                this._options.stun = value;
            } else if (option === "turn") {
                if (!value || typeof value !== "object" ||
                    typeof value.url !== "string" || value.url.length === 0 ||
                    (value.user === undefined) !== (value.pass === undefined)) {
                    throw new core.Error(
                        "Invalid TURN configuration",
                        "bad_request"
                    );
                }
                this._options.turn = value.url;
                this._options.turn_user = value.user;
                this._options.turn_pass = value.pass;
            } else {
                throw new core.Error("Unsupported option", "unsupported");
            }

            if (this._peer.connectionState === "new") {
                const configuration = this._peer.getConfiguration();
                configuration.iceServers = core.iceServers(this._options);
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
            !core.protocolHex(value.connection, 32) ||
            !core.protocolHex(value.capability, 64) ||
            !core.safeProtocolInt(value.expires_at)
        ) {
            throw new core.Error("Invalid connection response", "bad_response");
        }
        return value;
    }

    /**
     * Sends an index protocol request.
     * @return Response payload.
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
                reject(new core.Error(
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
                reject(new core.Error(
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
     * Converts a session description to protocol data.
     * @return Description data.
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

            const response = validateConnection(await core.request(index, {
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
                throw new core.Error(
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

    global.RedP2P.con = async function (options) {
        return connect(options || {});
    };
})(globalThis);
