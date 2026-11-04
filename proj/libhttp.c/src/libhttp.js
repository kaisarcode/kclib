/**
 * libhttp.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libhttp library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libhttp.mjs";

const wasm = await createModule();

/**
 * Copy a JavaScript string into WebAssembly memory.
 * @return Owned native string pointer.
 */
function cstring(value) {
    if (typeof value !== "string") throw new TypeError("Expected a string");
    const size = wasm.lengthBytesUTF8(value) + 1;
    const ptr = wasm._malloc(size);
    if (!ptr) throw new Error("Out of WebAssembly memory");
    wasm.stringToUTF8(value, ptr, size);
    return ptr;
}

/**
 * Decode a native string pointer.
 * @return JavaScript string.
 */
function string(ptr) {
    if (!ptr) throw new Error("Native operation failed");
    return wasm.UTF8ToString(ptr);
}

/**
 * Read an unsigned 32-bit value from WebAssembly memory.
 * @return Unsigned integer value.
 */
function uint(ptr) {
    return wasm.getValue(ptr, "i32") >>> 0;
}

/**
 * Allocate WebAssembly memory for a temporary value.
 * @return Owned native memory pointer.
 */
function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}

/**
 * Pass a temporary native string to a callback.
 * @return Callback result.
 */
function withString(value, fn) {
    const ptr = cstring(value);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}

/**
 * Copy bytes into WebAssembly memory.
 * @return Owned native memory pointer.
 */
function copyBytes(data) {
    if (!(data instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(data.length);
    wasm.HEAPU8.set(data, ptr);
    return ptr;
}

/**
 * Throw when a native operation reports an error.
 * @return void.
 */
function check(status) {
    if (status !== 0) throw new Error("Native operation failed: " + status);
}

/**
 * Copy an array of borrowed HTTP fields.
 * @param ptr Native field array.
 * @param count Number of fields.
 * @return Independent JavaScript objects.
 */
function fields(ptr, count) {
    return Array.from({length: count}, (_, i) => ({
        name: string(uint(ptr + i * 8)),
        value: string(uint(ptr + i * 8 + 4))
    }));
}

/**
 * Decode an optional native string pointer.
 * @return String value or null.
 */
function nullable(ptr) {
    return ptr ? string(ptr) : null;
}

/**
 * Copy a native HTTP request into JavaScript data.
 * @return Request object.
 */
function requestValue(ptr) {
    const body = uint(ptr + 28);
    return {
        version: nullable(uint(ptr)),
        method: nullable(uint(ptr + 4)),
        target: nullable(uint(ptr + 8)),
        path: nullable(uint(ptr + 12)),
        query: nullable(uint(ptr + 16)),
        headers: fields(uint(ptr + 20), uint(ptr + 24)),
        body: wasm.HEAPU8.slice(body, body + uint(ptr + 32)),
        trailers: fields(uint(ptr + 36), uint(ptr + 40)),
        chunked: Boolean(wasm.getValue(ptr + 44, "i32")),
        chunkSize: uint(ptr + 48)
    };
}

/**
 * Copy a native HTTP response into JavaScript data.
 * @return Response object.
 */
function responseValue(ptr) {
    const body = uint(ptr + 20);
    return {
        version: nullable(uint(ptr)),
        status: wasm.getValue(ptr + 4, "i32"),
        reason: nullable(uint(ptr + 8)),
        headers: fields(uint(ptr + 12), uint(ptr + 16)),
        body: wasm.HEAPU8.slice(body, body + uint(ptr + 24)),
        trailers: fields(uint(ptr + 28), uint(ptr + 32)),
        chunked: Boolean(wasm.getValue(ptr + 36, "i32")),
        chunkSize: uint(ptr + 40)
    };
}

/**
 * Convert HTTP options into a temporary C build structure.
 * @param options HTTP options.
 * @param response Whether this is a response.
 * @param fn Native builder.
 * @return Wire bytes.
 */
function build(options, response, fn) {
    const owned = [], data = allocate(44), out = allocate(4), size = allocate(4);

    /**
     * Retain native memory until the build completes.
     * @return Retained memory pointer.
     */
    function hold(ptr) {
        owned.push(ptr);
        return ptr;
    }

    /**
     * Encode an optional string for native use.
     * @return Native string pointer or zero.
     */
    function optional(value) {
        return value === undefined || value === null ? 0 : hold(cstring(value));
    }

    /**
     * Encode HTTP fields in a native structure.
     * @return void.
     */
    function headers(value, offset) {
        const entries = Array.isArray(value) ? value.map(v => [v.name, v.value]) : Object.entries(value || {});
        const ptr = entries.length ? hold(allocate(entries.length * 8)) : 0;
        for (let i = 0; i < entries.length; i++) {
            wasm.setValue(ptr + i * 8, optional(entries[i][0]), "i32");
            wasm.setValue(ptr + i * 8 + 4, optional(entries[i][1]), "i32");
        }
        wasm.setValue(data + offset, ptr, "i32");
        wasm.setValue(data + offset + 4, entries.length, "i32");
    }
    try {
        wasm.HEAPU8.fill(0, data, data + 44);
        wasm.setValue(data, optional(options.version), "i32");
        if (response) {
            if (options.status !== undefined) {
                const ptr = hold(allocate(4));
                wasm.setValue(ptr, options.status, "i32");
                wasm.setValue(data + 4, ptr, "i32");
            }
        } else {
            wasm.setValue(data + 4, optional(options.method), "i32");
        }
        wasm.setValue(data + 8, optional(response ? options.reason : options.target), "i32");
        headers(options.headers, 12);
        const body = typeof options.body === "string"
            ? new TextEncoder().encode(options.body)
            : options.body || new Uint8Array();
        if (!(body instanceof Uint8Array)) throw new TypeError("Expected Uint8Array body");
        const payload = body.length ? hold(copyBytes(body)) : 0;
        wasm.setValue(data + 20, payload, "i32");
        wasm.setValue(data + 24, body.length, "i32");
        headers(options.trailers, 28);
        wasm.setValue(data + 36, options.chunked ? 1 : 0, "i32");
        if (options.chunkSize !== undefined) {
            const ptr = hold(allocate(4));
            wasm.setValue(ptr, options.chunkSize, "i32");
            wasm.setValue(data + 40, ptr, "i32");
        }
        wasm.setValue(out, 0, "i32");
        check(fn(data, out, size));
        const ptr = uint(out);
        try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
        finally { if (ptr) wasm._kc_http_free(ptr); }
    } finally {
        for (const ptr of owned) wasm._free(ptr);
        wasm._free(size);
        wasm._free(out);
        wasm._free(data);
    }
}

export const http = {

    /**
     * Build an HTTP request.
     * @return Encoded HTTP request.
     */
    request(options = {}) {
        return build(options, false, wasm._kc_http_request);
    },

    /**
     * Build an HTTP response.
     * @return Encoded HTTP response.
     */
    response(options = {}) {
        return build(options, true, wasm._kc_http_response);
    },

    /**
     * Open an incremental HTTP parser.
     * @return Parser API with explicit close operation.
     */
    parser({onRequest, onResponse, onError} = {}) {
        const callbacks = [];
        let pending = null;

        /**
         * Wrap a JavaScript callback for native events.
         * @return Native callback pointer or zero.
         */
        const wrap = (fn, transform) => {
            if (!fn) return 0;
            const ptr = wasm.addFunction((value) => {
                try { fn(transform ? transform(value) : value); }
                catch (error) { pending = error; }
            }, "vii");
            callbacks.push(ptr);
            return ptr;
        };
        const request = wrap(onRequest, requestValue);
        const response = wrap(onResponse, responseValue);
        const error = wrap(onError, value => value);
        const out = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_http_parser_open(out, request, response, error, 0));
            let handle = uint(out);

            /**
             * Return the active native handle.
             * @return Open native handle.
             */
            const valid = () => {
                if (!handle) throw new Error("Parser is closed");
                return handle;
            };
            return {

                /**
                 * Write bytes to the native parser.
                 * @return void.
                 */
                write(data) {
                    const ptr = copyBytes(data);
                    try {
                        check(wasm._kc_http_parser_write(valid(), ptr, data.length));
                        if (pending) {
                            const failure = pending;
                            pending = null;
                            throw failure;
                        }
                    } finally { wasm._free(ptr); }
                },

                /**
                 * Release the native capability.
                 * @return void.
                 */
                close() {
                    if (!handle) return;
                    wasm._kc_http_parser_close(handle);
                    handle = 0;
                    for (const ptr of callbacks) wasm.removeFunction(ptr);
                    if (pending) {
                        const failure = pending;
                        pending = null;
                        throw failure;
                    }
                }
            };
        } catch (failure) {
            for (const ptr of callbacks) wasm.removeFunction(ptr);
            throw failure;
        } finally { wasm._free(out); }
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_http_version()); }
};
export default http;
