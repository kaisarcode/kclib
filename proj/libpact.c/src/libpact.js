/**
 * libpact.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libpact library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libpact.mjs";

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
function withString(value, fn) {
    const ptr = cstring(value);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}
function copyBytes(data) {
    if (!(data instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(data.length);
    wasm.HEAPU8.set(data, ptr);
    return ptr;
}
function check(status) {
    if (status !== 0) throw new Error("Native operation failed: " + status);
}
/**
 * Wrap a native trust context.
 * @param handle Native context.
 * @return Trust API.
 */
function trustObject(handle) {
    const valid = () => {
        if (!handle) throw new Error("Trust context is closed");
        return handle;
    };
    function strings(operation, count) {
        const out = Array.from({length: count}, () => allocate(4));
        try {
            for (const ptr of out) wasm.setValue(ptr, 0, "i32");
            check(operation(...out));
            const values = out.map(ptr => {
                const value = uint(ptr);
                return value ? string(value) : null;
            });
            return values;
        } finally {
            for (const ptr of out) {
                const value = uint(ptr);
                if (value) wasm._kc_pact_free(value);
                wasm._free(ptr);
            }
        }
    }
    function binary(operation) {
        const out = allocate(4), size = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(operation(out, size));
            const ptr = uint(out);
            try { return wasm.HEAPU8.slice(ptr, ptr + uint(size)); }
            finally { if (ptr) wasm._kc_pact_free(ptr); }
        } finally { wasm._free(size); wasm._free(out); }
    }
    return {
        invite() {
            const [uid, code] = strings((a, b) => wasm._kc_pact_invite(valid(), a, b), 2);
            return {uid, code};
        },
        join(code) {
            return withString(code, input => {
                const [uid, confirmation] = strings((a, b) => wasm._kc_pact_join(valid(), input, a, b), 2);
                return {uid, confirmation};
            });
        },
        confirm(confirmation) {
            return withString(confirmation, input =>
                strings(out => wasm._kc_pact_confirm(valid(), input, out), 1)[0]);
        },
        seal(uid, data) {
            return withString(uid, remote => {
                const ptr = copyBytes(data);
                try { return binary((out, size) =>
                    wasm._kc_pact_seal(valid(), remote, ptr, data.length, out, size)); }
                finally { wasm._free(ptr); }
            });
        },
        size(uid, data) {
            return withString(uid, remote => {
                const ptr = copyBytes(data), out = allocate(4);
                try {
                    const status = wasm._kc_pact_size(valid(), remote, ptr, data.length, out);
                    if (status === 1) return null;
                    check(status);
                    return uint(out);
                } finally { wasm._free(out); wasm._free(ptr); }
            });
        },
        unseal(uid, data) {
            return withString(uid, remote => {
                const ptr = copyBytes(data);
                try { return binary((out, size) =>
                    wasm._kc_pact_unseal(valid(), remote, ptr, data.length, out, size)); }
                finally { wasm._free(ptr); }
            });
        },
        revoke(uid) {
            return withString(uid, remote => check(wasm._kc_pact_revoke(valid(), remote)));
        },
        close() {
            if (handle) {
                wasm._kc_pact_close(handle);
                handle = 0;
            }
        }
    };
}

export const pact = {
    init() {
        const out = allocate(4);
        try {
            wasm.setValue(out, 0, "i32");
            check(wasm._kc_pact_init(out));
            const handle = uint(out);
            if (!handle) throw new Error("Trust context was not created");
            return trustObject(handle);
        } finally { wasm._free(out); }
    },
    version() { return Number(wasm._kc_pact_version()); }
};
export default pact;
