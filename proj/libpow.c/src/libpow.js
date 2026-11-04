/**
 * libpow.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libpow library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libpow.mjs";

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
export const pow = {
    AUTO: 0,
    SHA256: 1,
    HASHWX: 2,
    INVALID: -1,
    challenge(effort, {backend} = {}) {
        const out = allocate(4), size = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            const status = backend === undefined
                ? wasm._kc_pow_challenge(BigInt(effort), out, size)
                : wasm._kc_pow_challenge_with_backend(backend, BigInt(effort), out, size);
            check(status);
            const ptr = uint(out);
            try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
            finally { if (ptr) wasm._kc_pow_free(ptr); }
        } finally { wasm._free(size); wasm._free(out); }
    },
    solve(challenge) {
        const input = copyBytes(challenge), out = allocate(4), size = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_pow_solve(input, challenge.length, out, size));
            const ptr = uint(out);
            try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
            finally { if (ptr) wasm._kc_pow_free(ptr); }
        } finally {
            wasm._free(size);
            wasm._free(out);
            wasm._free(input);
        }
    },
    verify(challenge, solution) {
        const a = copyBytes(challenge), b = copyBytes(solution);
        try {
            const status = wasm._kc_pow_verify(a, challenge.length, b, solution.length);
            if (status === 2) return false;
            check(status);
            return true;
        } finally { wasm._free(b); wasm._free(a); }
    },
    size(data) {
        const input = copyBytes(data), output = allocate(4);
        try {
            const status = wasm._kc_pow_size(input, data.length, output);
            if (status === 1) return null;
            check(status);
            return uint(output);
        } finally { wasm._free(output); wasm._free(input); }
    },
    getBackend(challenge) {
        const input = copyBytes(challenge), output = allocate(4);
        try {
            check(wasm._kc_pow_get_backend(input, challenge.length, output));
            return wasm.getValue(output, "i32");
        } finally { wasm._free(output); wasm._free(input); }
    },
    backendAvailable(backend) { return Boolean(wasm._kc_pow_backend_available(backend)); },
    backendName(backend) {
        const ptr = wasm._kc_pow_backend_name(backend);
        return ptr ? string(ptr) : null;
    },
    backendFromName(name) {
        return withString(name, ptr => wasm._kc_pow_backend_from_name(ptr));
    },
    version() { return Number(wasm._kc_pow_version()); }
};
export default pow;
