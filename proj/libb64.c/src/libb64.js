/**
 * libb64.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libb64 library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libb64.mjs";

const wasm = await createModule();

function cstring(value) {
    if (typeof value !== "string") throw new TypeError("Expected a string");
    const size = wasm.lengthBytesUTF8(value) + 1;
    const ptr = wasm._malloc(size);
    if (!ptr) throw new Error("Out of WebAssembly memory");
    wasm.stringToUTF8(value, ptr, size);
    return ptr;
}

function string(ptr) {
    if (!ptr) throw new Error("Native operation failed");
    return wasm.UTF8ToString(ptr);
}

function uint(ptr) {
    return wasm.getValue(ptr, "i32") >>> 0;
}

export const b64 = {
    encode(data) {
        if (!(data instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
        const input = wasm._malloc(Math.max(1, data.length));
        if (!input) throw new Error("Out of WebAssembly memory");
        try {
            wasm.HEAPU8.set(data, input);
            const result = wasm._kc_b64_encode(input, data.length);
            if (!result) throw new Error("Encoding failed");
            try { return string(result); }
            finally { wasm._kc_b64_free(result); }
        } finally {
            wasm._free(input);
        }
    },
    decode(encoded) {
        const input = cstring(encoded);
        const size = wasm._malloc(4);
        if (!size) {
            wasm._free(input);
            throw new Error("Out of WebAssembly memory");
        }
        try {
            const ptr = wasm._kc_b64_decode(input, size);
            if (!ptr) throw new Error("Invalid Base64 input");
            try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
            finally { wasm._kc_b64_free(ptr); }
        } finally {
            wasm._free(size);
            wasm._free(input);
        }
    },
    version() { return Number(wasm._kc_b64_version()); }
};
export default b64;
