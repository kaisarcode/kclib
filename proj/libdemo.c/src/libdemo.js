/**
 * libdemo.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libdemo library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libdemo.mjs";

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

export const demo = {
    greet(name) {
        const input = cstring(name);
        try {
            const result = wasm._kc_demo_greet(input);
            if (!result) throw new Error("Greeting failed");
            try {
                return string(result);
            } finally {
                wasm._kc_demo_free(result);
            }
        } finally {
            wasm._free(input);
        }
    },
    version() { return Number(wasm._kc_demo_version()); }
};
export default demo;
