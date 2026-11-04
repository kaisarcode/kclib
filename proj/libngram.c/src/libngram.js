/**
 * libngram.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libngram library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libngram.mjs";

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
export const ngram = {
    traverse(input, options = {}, visit = null) {
        return withString(input, text => {
            const config = allocate(12), owned = [], chunks = [];
            let callback = 0;
            try {
                for (const [offset, key] of [[0, "maxTokens"], [4, "minTokens"]]) {
                    let ptr = 0;
                    if (options[key] !== undefined) {
                        if (!Number.isSafeInteger(options[key]) || options[key] < 1 || options[key] > 0xffffffff) throw new RangeError("Invalid " + key);
                        ptr = allocate(4);
                        owned.push(ptr);
                        wasm.setValue(ptr, options[key], "i32");
                    }
                    wasm.setValue(config + offset, ptr, "i32");
                }
                let separators = 0;
                if (options.separators !== undefined) {
                    separators = cstring(options.separators);
                    owned.push(separators);
                }
                wasm.setValue(config + 8, separators, "i32");
                const decoder = new TextDecoder();
                callback = wasm.addFunction(chunk => {
                    const ptr = uint(chunk), size = uint(chunk + 4);
                    const value = {
                        text: decoder.decode(wasm.HEAPU8.subarray(ptr, ptr + size)),
                        tokenStart: uint(chunk + 8),
                        tokenCount: uint(chunk + 12)
                    };
                    if (visit) return visit(value) ? 1 : 0;
                    chunks.push(value);
                    return 0;
                }, "iii");
                const status = wasm._kc_ngram_traverse(text, config, callback, 0);
                if (status !== 0 && !(status === -2 && visit)) throw new Error("N-gram traversal failed: " + status);
                return visit ? undefined : chunks;
            } finally {
                if (callback) wasm.removeFunction(callback);
                for (const ptr of owned) wasm._free(ptr);
                wasm._free(config);
            }
        });
    },
    version() { return Number(wasm._kc_ngram_version()); }
};
export default ngram;
