/**
 * libleven.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libleven library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libleven.mjs";

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

export const leven = {

    /**
     * Measure the edit distance between two strings.
     * @return Edit distance or null.
     */
    distance(left, right, options = {}) {
        const a = cstring(left);
        let b = 0, maximum = 0, output = 0;
        try {
            b = cstring(right);
            output = wasm._malloc(4);
            if (!output) throw new Error("Out of WebAssembly memory");
            if (options.maxDistance !== undefined) {
                const value = options.maxDistance;
                if (!Number.isSafeInteger(value) || value < 0 || value > 0xfffffffe) {
                    throw new RangeError("Invalid maxDistance");
                }
                maximum = wasm._malloc(4);
                if (!maximum) throw new Error("Out of WebAssembly memory");
                wasm.setValue(maximum, value, "i32");
            }
            const status = wasm._kc_leven_distance(a, b, maximum, output);
            if (status !== 0 && status !== -4) {
                throw new Error(string(wasm._kc_leven_strerror(status)));
            }
            return uint(output);
        } finally {
            if (maximum) wasm._free(maximum);
            if (output) wasm._free(output);
            if (b) wasm._free(b);
            wasm._free(a);
        }
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_leven_version()); }
};
export default leven;
