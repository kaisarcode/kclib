/**
 * libmin.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libmin library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libmin.mjs";

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

function minify(operation, source) {
    const input = cstring(source);
    try {
        const output = operation(input);
        if (!output) throw new Error("Minification failed");
        try { return string(output); }
        finally { wasm._kc_min_free(output); }
    } finally {
        wasm._free(input);
    }
}
export const min = {
    css(source) { return minify(wasm._kc_min_css, source); },
    js(source) { return minify(wasm._kc_min_js, source); },
    html(source) { return minify(wasm._kc_min_html, source); },
    txt(source) { return minify(wasm._kc_min_txt, source); },
    version() { return Number(wasm._kc_min_version()); }
};
export default min;
