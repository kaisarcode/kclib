/**
 * libmdp.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libmdp library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libmdp.mjs";

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

export const mdp = {
    open(source) {
        const input = cstring(source);
        const out = wasm._malloc(4);
        if (!out) {
            wasm._free(input);
            throw new Error("Out of WebAssembly memory");
        }
        try {
            wasm.setValue(out, 0, "i32");
            if (wasm._kc_mdp_open(out, input) !== 0) throw new Error("Markdown open failed");
            let handle = uint(out);
            if (!handle) throw new Error("Markdown document was not created");
            function valid() {
                if (!handle) throw new Error("Document is closed");
                return handle;
            }
            return {
                html() { return string(wasm._kc_mdp_html(valid())); },
                ansi() { return string(wasm._kc_mdp_ansi(valid())); },
                body() { return string(wasm._kc_mdp_body(valid())); },
                meta() { return string(wasm._kc_mdp_meta(valid())); },
                close() {
                    if (handle) {
                        wasm._kc_mdp_close(handle);
                        handle = 0;
                    }
                }
            };
        } finally {
            wasm._free(out);
            wasm._free(input);
        }
    },
    version() { return Number(wasm._kc_mdp_version()); }
};
export default mdp;
