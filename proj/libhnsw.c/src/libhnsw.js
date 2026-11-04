/**
 * libhnsw.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libhnsw library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libhnsw.mjs";

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
export const hnsw = {
    COSINE: 1,
    INNER_PRODUCT: 2,
    L2: 3,
    open(options) {
        if (!Number.isSafeInteger(options?.dimension) || options.dimension < 1 || options.dimension > 0xffffffff) throw new RangeError("Invalid dimension");
        const opt = allocate(20);
        const owned = [];
        const out = allocate(4);
        try {
            wasm.setValue(opt, options.dimension, "i32");
            for (const [i, key] of ["metric", "maxConnections", "buildEffort", "searchEffort"].entries()) {
                let ptr = 0;
                if (options[key] !== undefined) {
                    if (!Number.isInteger(options[key])) throw new TypeError("Invalid " + key);
                    ptr = allocate(4);
                    owned.push(ptr);
                    wasm.setValue(ptr, options[key], "i32");
                }
                wasm.setValue(opt + 4 + i * 4, ptr, "i32");
            }
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_hnsw_open(out, opt), wasm._kc_hnsw_strerror);
            let handle = uint(out);
            if (!handle) throw new Error("Index was not created");
            const valid = () => {
                if (!handle) throw new Error("Index is closed");
                return handle;
            };
            const vector = values => {
                if (!(values instanceof Float32Array) || values.length !== options.dimension) throw new TypeError("Invalid vector dimension");
                const ptr = allocate(values.byteLength);
                wasm.HEAPF32.set(values, ptr / 4);
                return ptr;
            };
            return {
                add(id, values) {
                    return withString(id, name => {
                        const ptr = vector(values);
                        try { check(wasm._kc_hnsw_add(valid(), name, ptr), wasm._kc_hnsw_strerror); }
                        finally { wasm._free(ptr); }
                    });
                },
                build() { check(wasm._kc_hnsw_build(valid()), wasm._kc_hnsw_strerror); },
                search(values, {limit = 10, threshold = 0} = {}) {
                    if (!Number.isSafeInteger(limit) || limit < 0 || limit > 0xffffffff) throw new RangeError("Invalid limit");
                    const ptr = vector(values);
                    const outResults = allocate(4), outCount = allocate(4);
                    try {
                        check(wasm._kc_hnsw_search(valid(), ptr, limit, threshold, outResults, outCount), wasm._kc_hnsw_strerror);
                        const array = uint(outResults);
                        try {
                            return Array.from({length: uint(outCount)}, (_, i) => ({
                                id: string(uint(array + i * 16)),
                                score: wasm.getValue(array + i * 16 + 8, "double")
                            }));
                        } finally { if (array) wasm._kc_hnsw_free(array); }
                    } finally {
                        wasm._free(outCount);
                        wasm._free(outResults);
                        wasm._free(ptr);
                    }
                },
                dimension() { return wasm._kc_hnsw_dimension(valid()); },
                metric() { return wasm._kc_hnsw_metric(valid()); },
                count() { return wasm._kc_hnsw_count(valid()); },
                close() {
                    if (handle) { wasm._kc_hnsw_close(handle); handle = 0; }
                }
            };
        } finally {
            for (const ptr of owned) wasm._free(ptr);
            wasm._free(out);
            wasm._free(opt);
        }
    },
    version() { return Number(wasm._kc_hnsw_version()); }
};
export default hnsw;
