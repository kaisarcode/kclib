/**
 * libregex.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libregex library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libregex.mjs";

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
export const regex = {
    open(pattern) {
        return withString(pattern, input => {
            const out = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_regex_open(out, input), wasm._kc_regex_strerror);
                let handle = uint(out);
                if (!handle) throw new Error("Expression was not compiled");
                const valid = () => {
                    if (!handle) throw new Error("Expression is closed");
                    return handle;
                };
                function execute(operation, input, offset = 0) {
                    return withString(input, text => {
                        const captures = allocate(16 * 8);
                        const count = allocate(4);
                        try {
                            const status = operation === "match"
                                ? wasm._kc_regex_match(valid(), text, captures, 16, count)
                                : wasm._kc_regex_search(valid(), text, offset, captures, 16, count);
                            if (status === 1) return null;
                            check(status, wasm._kc_regex_strerror);
                            const utf8 = new TextEncoder().encode(input);
                            return Array.from({length: Math.min(16, uint(count))}, (_, i) => {
                                const start = uint(captures + i * 8);
                                const end = uint(captures + i * 8 + 4);
                                return start === 0xffffffff ? null : {
                                    start, end,
                                    text: new TextDecoder().decode(utf8.subarray(start, end))
                                };
                            });
                        } finally {
                            wasm._free(count);
                            wasm._free(captures);
                        }
                    });
                }
                return {
                    match(input) { return execute("match", input); },
                    search(input, offset = 0) {
                        if (!Number.isSafeInteger(offset) || offset < 0 || offset > new TextEncoder().encode(input).length) throw new RangeError("Invalid offset");
                        return execute("search", input, offset);
                    },
                    close() {
                        if (handle) {
                            wasm._kc_regex_close(handle);
                            handle = 0;
                        }
                    }
                };
            } finally { wasm._free(out); }
        });
    },
    version() { return Number(wasm._kc_regex_version()); }
};
export default regex;
