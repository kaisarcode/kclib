/**
 * libbm25.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libbm25 library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libbm25.mjs";

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
function withString(text, fn) {
    const ptr = cstring(text);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}
function copyBytes(data) {
    if (!(data instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(data.length);
    wasm.HEAPU8.set(data, ptr);
    return ptr;
}
function check(status, describe) {
    if (status !== 0) throw new Error(describe ? string(describe(status)) : "Native error " + status);
}
/**
 * Return the projected index API for one native BM25 handle.
 * @param handle Native index.
 * @return Index object with explicit lifetime.
 */
function indexObject(handle) {
    const valid = () => {
        if (!handle) throw new Error("Index is closed");
        return handle;
    };
    return {
        add(id, content) {
            return withString(id, key => withString(content, text =>
                check(wasm._kc_bm25_add(valid(), key, text), wasm._kc_bm25_strerror)));
        },
        build() { check(wasm._kc_bm25_build(valid()), wasm._kc_bm25_strerror); },
        search(query, options = {}) {
            return withString(query, text => {
                const result = allocate(4), count = allocate(4);
                const owned = [];
                let cfg = 0;
                try {
                    const fields = ["limit", "k1", "b"];
                    if (fields.some(name => options[name] !== undefined)) {
                        cfg = allocate(12);
                        for (let i = 0; i < fields.length; i++) {
                            const value = options[fields[i]];
                            let ptr = 0;
                            if (value !== undefined) {
                                if (typeof value !== "number" || !Number.isFinite(value)) throw new TypeError("Invalid " + fields[i]);
                                if (i === 0 && (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff)) throw new RangeError("Invalid limit");
                                ptr = allocate(i === 0 ? 4 : 8);
                                owned.push(ptr);
                                wasm.setValue(ptr, value, i === 0 ? "i32" : "double");
                            }
                            wasm.setValue(cfg + i * 4, ptr, "i32");
                        }
                    }
                    wasm.setValue(result, 0, "i32");
                    wasm.setValue(count, 0, "i32");
                    check(wasm._kc_bm25_search(valid(), text, cfg, result, count), wasm._kc_bm25_strerror);
                    const array = uint(result);
                    try {
                        return Array.from({length: uint(count)}, (_, i) => {
                            const pos = array + i * 24;
                            return {
                                ordinal: uint(pos),
                                id: string(uint(pos + 4)),
                                content: string(uint(pos + 8)),
                                score: wasm.getValue(pos + 16, "double")
                            };
                        });
                    } finally { if (array) wasm._kc_bm25_free(array); }
                } finally {
                    for (const ptr of owned) wasm._free(ptr);
                    if (cfg) wasm._free(cfg);
                    wasm._free(count);
                    wasm._free(result);
                }
            });
        },
        export() {
            const out = allocate(4), size = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_bm25_export(valid(), out, size), wasm._kc_bm25_strerror);
                const ptr = uint(out);
                try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
                finally { if (ptr) wasm._kc_bm25_free(ptr); }
            } finally {
                wasm._free(size);
                wasm._free(out);
            }
        },
        save(path) {
            return withString(path, p => check(wasm._kc_bm25_save(valid(), p), wasm._kc_bm25_strerror));
        },
        count() { return wasm._kc_bm25_count(valid()); },
        foldAccents() { return Boolean(wasm._kc_bm25_fold_accents(valid())); },
        tokens(text) {
            return withString(text, input => {
                const result = [];
                const callback = wasm.addFunction(ptr => {
                    result.push(string(ptr));
                    return 0;
                }, "iii");
                try { check(wasm._kc_bm25_tokens(valid(), input, callback, 0), wasm._kc_bm25_strerror); }
                finally { wasm.removeFunction(callback); }
                return result;
            });
        },
        vocabulary() {
            const result = [];
            const callback = wasm.addFunction((ptr, count) => {
                result.push({term: string(ptr), documentCount: count >>> 0});
                return 0;
            }, "iiii");
            try { check(wasm._kc_bm25_vocabulary(valid(), callback, 0), wasm._kc_bm25_strerror); }
            finally { wasm.removeFunction(callback); }
            return result;
        },
        postings(term) {
            return withString(term, input => {
                const result = [];
                const callback = wasm.addFunction((ordinal, frequency) => {
                    result.push({ordinal: ordinal >>> 0, frequency: frequency >>> 0});
                    return 0;
                }, "iiii");
                try { check(wasm._kc_bm25_postings(valid(), input, callback, 0), wasm._kc_bm25_strerror); }
                finally { wasm.removeFunction(callback); }
                return result;
            });
        },
        close() {
            if (handle) {
                wasm._kc_bm25_close(handle);
                handle = 0;
            }
        }
    };
}
export const bm25 = {
    open(options = {}) {
        const out = allocate(4);
        let cfg = 0, fold = 0;
        try {
            if (options.foldAccents !== undefined) {
                fold = allocate(4);
                wasm.setValue(fold, options.foldAccents ? 1 : 0, "i32");
                cfg = allocate(4);
                wasm.setValue(cfg, fold, "i32");
            }
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_bm25_open(out, cfg), wasm._kc_bm25_strerror);
            const handle = uint(out);
            if (!handle) throw new Error("Index was not created");
            return indexObject(handle);
        } finally {
            if (cfg) wasm._free(cfg);
            if (fold) wasm._free(fold);
            wasm._free(out);
        }
    },
    import(data) {
        const input = copyBytes(data), out = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_bm25_import(out, input, data.length), wasm._kc_bm25_strerror);
            const handle = uint(out);
            if (!handle) throw new Error("Imported index was not created");
            return indexObject(handle);
        } finally { wasm._free(out); wasm._free(input); }
    },
    load(path) {
        return withString(path, input => {
            const out = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_bm25_load(out, input), wasm._kc_bm25_strerror);
                const handle = uint(out);
                if (!handle) throw new Error("Loaded index was not created");
                return indexObject(handle);
            } finally { wasm._free(out); }
        });
    },
    version() { return Number(wasm._kc_bm25_version()); }
};
export default bm25;
