/**
 * libemb.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libemb library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libemb.mjs";

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
function withString(input, fn) {
    const ptr = cstring(input);
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

/**
 * Copy bytes into WebAssembly memory.
 * @return Owned native memory pointer.
 */
function copyBytes(input) {
    if (!(input instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(input.length);
    wasm.HEAPU8.set(input, ptr);
    return ptr;
}
export const emb = {

    /**
     * Return the configured vector dimension.
     * @return Vector dimension.
     */
    dimension() { return wasm._kc_emb_dimension(); },

    /**
     * Embed text with the native model.
     * @return Embedding vector.
     */
    embed(text) {
        return withString(text, input => {
            const out = allocate(4), count = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                wasm.setValue(count, 0, "i32");
                check(wasm._kc_emb_embed(input, out, count));
                const ptr = uint(out);
                try {
                    if (!ptr) throw new Error("Embedding not allocated");
                    return new Float32Array(wasm.HEAPF32.subarray(ptr / 4, ptr / 4 + uint(count)));
                } finally { if (ptr) wasm._kc_emb_free(ptr); }
            } finally { wasm._free(count); wasm._free(out); }
        });
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_emb_version()); }
};
export default emb;
