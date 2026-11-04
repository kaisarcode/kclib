/**
 * libtpl.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libtpl library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libtpl.mjs";

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


function allocate(length) {
    const ptr = wasm._malloc(Math.max(length, 1));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}
function withString(value, fn) {
    const ptr = cstring(value);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}
function check(status, error) {
    if (status !== 0) throw new Error(error ? string(error(status)) : "Native error " + status);
}
export const tpl = {
    open(source, options = {}) {
        return withString(source, input => {
            const out = allocate(4);
            let root = 0, cfg = 0;
            try {
                if (options.root !== undefined) {
                    root = cstring(options.root);
                    cfg = allocate(4);
                    wasm.setValue(cfg, root, "i32");
                }
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_tpl_open(out, input, cfg));
                let handle = uint(out);
                if (!handle) throw new Error("Template was not created");
                const valid = () => {
                    if (!handle) throw new Error("Template is closed");
                    return handle;
                };
                return {
                    render(variables = {}) {
                        const entries = Object.entries(variables);
                        const array = allocate(entries.length * 8);
                        const owned = [];
                        try {
                            for (let i = 0; i < entries.length; i++) {
                                const key = cstring(entries[i][0]);
                                owned.push(key);
                                const value = cstring(entries[i][1]);
                                owned.push(value);
                                wasm.setValue(array + i * 8, key, "i32");
                                wasm.setValue(array + i * 8 + 4, value, "i32");
                            }
                            const result = wasm._kc_tpl_render(valid(), entries.length ? array : 0, entries.length);
                            if (!result) throw new Error(string(wasm._kc_tpl_error(valid())));
                            try { return string(result); }
                            finally { wasm._kc_tpl_free(result); }
                        } finally {
                            for (const ptr of owned) wasm._free(ptr);
                            wasm._free(array);
                        }
                    },
                    error() { return string(wasm._kc_tpl_error(valid())); },
                    close() {
                        if (handle) {
                            wasm._kc_tpl_close(handle);
                            handle = 0;
                        }
                    }
                };
            } finally {
                if (root) wasm._free(root);
                if (cfg) wasm._free(cfg);
                wasm._free(out);
            }
        });
    },
    version() { return Number(wasm._kc_tpl_version()); }
};
export default tpl;
