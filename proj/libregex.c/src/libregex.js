/**
 * libregex.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libregex library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libregex.mjs";

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
function allocate(length) {
    const ptr = wasm._malloc(Math.max(length, 1));
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
 * Throw when a native operation reports an error.
 * @return void.
 */
function check(status, error) {
    if (status !== 0) throw new Error(error ? string(error(status)) : "Native error " + status);
}
export const regex = {

    /**
     * Open the native capability.
     * @return API with an explicit close operation.
     */
    open(pattern) {
        return withString(pattern, input => {
            const out = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_regex_open(out, input), wasm._kc_regex_strerror);
                let handle = uint(out);
                if (!handle) throw new Error("Expression was not compiled");

                /**
                 * Return the active native handle.
                 * @return Open native handle.
                 */
                const valid = () => {
                    if (!handle) throw new Error("Expression is closed");
                    return handle;
                };

                /**
                 * Run a regular-expression operation on input.
                 * @return Match result.
                 */
                function execute(operation, input, offset = 0) {
                    return withString(input, text => {
                        const captures = allocate(16 * 8);
                        const count = allocate(4);
                        try {
                            const status = operation === "match"
                                ? wasm._kc_regex_match(valid(), text, captures, 16, count)
                                : wasm._kc_regex_search(valid(), text, offset, captures, 16, count);
                            if (status === 1) return null;
                            check(status, wasm._kc_regex_strerror);
                            const utf8 = new TextEncoder().encode(input);
                            return Array.from({length: Math.min(16, uint(count))}, (_, i) => {
                                const start = uint(captures + i * 8);
                                const end = uint(captures + i * 8 + 4);
                                return start === 0xffffffff ? null : {
                                    start, end,
                                    text: new TextDecoder().decode(utf8.subarray(start, end))
                                };
                            });
                        } finally {
                            wasm._free(count);
                            wasm._free(captures);
                        }
                    });
                }
                return {

                    /**
                     * Match input against the expression.
                     * @return Match result.
                     */
                    match(input) { return execute("match", input); },

                    /**
                     * Search the native capability.
                     * @return Search results.
                     */
                    search(input, offset = 0) {
                        if (!Number.isSafeInteger(offset) || offset < 0 || offset > new TextEncoder().encode(input).length) throw new RangeError("Invalid offset");
                        return execute("search", input, offset);
                    },

                    /**
                     * Release the native capability.
                     * @return void.
                     */
                    close() {
                        if (handle) {
                            wasm._kc_regex_close(handle);
                            handle = 0;
                        }
                    }
                };
            } finally { wasm._free(out); }
        });
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_regex_version()); }
};
export default regex;
