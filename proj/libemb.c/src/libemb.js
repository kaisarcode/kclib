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


function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}
function withString(input, fn) {
    const ptr = cstring(input);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}
function check(status, error) {
    if (status !== 0) throw new Error(error ? string(error(status)) : "Native error " + status);
}
function copyBytes(input) {
    if (!(input instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(input.length);
    wasm.HEAPU8.set(input, ptr);
    return ptr;
}
export const emb = {
    dimension() { return wasm._kc_emb_dimension(); },
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
    version() { return Number(wasm._kc_emb_version()); }
};
export default emb;
