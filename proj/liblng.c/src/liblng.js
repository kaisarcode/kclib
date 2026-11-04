/**
 * liblng.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the liblng library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./liblng.mjs";

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
export const lng = {

    /**
     * Detect languages in text.
     * @return Detected languages.
     */
    detect(text, options = {}) {
        return withString(text, input => {
            const result = allocate(4), count = allocate(4);
            let cfg = 0, threshold = 0, limit = 0;
            try {
                if (options.threshold !== undefined || options.limit !== undefined) {
                    cfg = allocate(8);
                    if (options.threshold !== undefined) {
                        if (typeof options.threshold !== "number" || options.threshold < 0 || options.threshold > 1) throw new RangeError("Invalid threshold");
                        threshold = allocate(8);
                        wasm.setValue(threshold, options.threshold, "double");
                    }
                    if (options.limit !== undefined) {
                        if (!Number.isSafeInteger(options.limit) || options.limit < 1 || options.limit > 0xffffffff) throw new RangeError("Invalid limit");
                        limit = allocate(4);
                        wasm.setValue(limit, options.limit, "i32");
                    }
                    wasm.setValue(cfg, threshold, "i32");
                    wasm.setValue(cfg + 4, limit, "i32");
                }
                wasm.setValue(result, 0, "i32");
                wasm.setValue(count, 0, "i32");
                check(wasm._kc_lng_detect(input, cfg, result, count));
                const ptr = uint(result);
                try {
                    return Array.from({length: uint(count)}, (_, i) => ({
                        code: string(uint(ptr + i * 16)),
                        score: wasm.getValue(ptr + i * 16 + 8, "double")
                    }));
                } finally {
                    if (ptr) wasm._kc_lng_free(ptr);
                }
            } finally {
                if (limit) wasm._free(limit);
                if (threshold) wasm._free(threshold);
                if (cfg) wasm._free(cfg);
                wasm._free(count);
                wasm._free(result);
            }
        });
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_lng_version()); }
};
export default lng;
