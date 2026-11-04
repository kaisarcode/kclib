/**
 * libtpm.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libtpm library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libtpm.mjs";

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
export const tpm = {
    open(text, options = {}) {
        return withString(text, input => {
            const out = allocate(4);
            let cfg = 0, size = 0;
            try {
                if (options.ngramSize !== undefined) {
                    if (!Number.isInteger(options.ngramSize) || options.ngramSize < 1 || options.ngramSize > 0x7fffffff) throw new RangeError("Invalid ngramSize");
                    size = allocate(4);
                    wasm.setValue(size, options.ngramSize, "i32");
                    cfg = allocate(4);
                    wasm.setValue(cfg, size, "i32");
                }
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_tpm_open(out, input, cfg));
                let handle = uint(out);
                if (!handle) throw new Error("Profile was not created");
                const valid = () => {
                    if (!handle) throw new Error("Profile is closed");
                    return handle;
                };
                return {
                    score(text) {
                        return withString(text, textPtr => {
                            const result = allocate(8);
                            try {
                                check(wasm._kc_tpm_score(valid(), textPtr, result));
                                return wasm.getValue(result, "double");
                            } finally { wasm._free(result); }
                        });
                    },
                    close() {
                        if (handle) {
                            wasm._kc_tpm_close(handle);
                            handle = 0;
                        }
                    }
                };
            } finally {
                if (size) wasm._free(size);
                if (cfg) wasm._free(cfg);
                wasm._free(out);
            }
        });
    },
    version() { return Number(wasm._kc_tpm_version()); }
};
export default tpm;
