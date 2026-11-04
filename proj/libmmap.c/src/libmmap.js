/**
 * libmmap.js - WebAssembly JavaScript projection.
 * Summary: JavaScript API for the libmmap library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import createModule from "./libmmap.mjs";

const wasm = await createModule();

/**
 * Copy a JavaScript string into WebAssembly memory.
 * @return Owned native string pointer.
 */
function cstring(value) {
    if (typeof value !== "string") throw new TypeError("Expected a string");
    const size = wasm.lengthBytesUTF8(value) + 1;
    const ptr = wasm._malloc(size);
    if (!ptr) throw new Error("Out of WebAssembly memory");
    wasm.stringToUTF8(value, ptr, size);
    return ptr;
}

/**
 * Decode a native string pointer.
 * @return JavaScript string.
 */
function string(ptr) {
    if (!ptr) throw new Error("Native operation failed");
    return wasm.UTF8ToString(ptr);
}

/**
 * Read an unsigned 32-bit value from WebAssembly memory.
 * @return Unsigned integer value.
 */
function uint(ptr) {
    return wasm.getValue(ptr, "i32") >>> 0;
}

/**
 * Allocate WebAssembly memory for a temporary value.
 * @return Owned native memory pointer.
 */
function allocate(size) {
    const ptr = wasm._malloc(Math.max(1, size));
    if (!ptr) throw new Error("Out of WebAssembly memory");
    return ptr;
}

/**
 * Pass a temporary native string to a callback.
 * @return Callback result.
 */
function withString(input, fn) {
    const ptr = cstring(input);
    try { return fn(ptr); }
    finally { wasm._free(ptr); }
}

/**
 * Throw when a native operation reports an error.
 * @return void.
 */
function check(status, error) {
    if (status !== 0) throw new Error(error ? string(error(status)) : "Native error " + status);
}

/**
 * Copy bytes into WebAssembly memory.
 * @return Owned native memory pointer.
 */
function copyBytes(input) {
    if (!(input instanceof Uint8Array)) throw new TypeError("Expected Uint8Array");
    const ptr = allocate(input.length);
    wasm.HEAPU8.set(input, ptr);
    return ptr;
}
export const mmap = {

    /**
     * Open the native capability.
     * @return API with an explicit close operation.
     */
    open(path) {
        return withString(path, input => {
            const out = allocate(4);
            try {
                wasm.setValue(out, 0, "i32");
                check(wasm._kc_mmap_open(out, input));
                let handle = uint(out);
                if (!handle) throw new Error("Map was not created");

                /**
                 * Return the active native handle.
                 * @return Open native handle.
                 */
                const valid = () => {
                    if (!handle) throw new Error("Map is closed");
                    return handle;
                };
                return {

                    /**
                     * Read the mapped value.
                     * @return Mapped value.
                     */
                    get() {
                        const value = allocate(4), size = allocate(4);
                        try {
                            const status = wasm._kc_mmap_get(valid(), value, size);
                            if (status === 1) return null;
                            check(status);
                            const ptr = uint(value);
                            return wasm.HEAPU8.slice(ptr, ptr + uint(size));
                        } finally { wasm._free(size); wasm._free(value); }
                    },

                    /**
                     * Set the mapped value.
                     * @return void.
                     */
                    set(value) {
                        if (value === null) {
                            check(wasm._kc_mmap_set(valid(), 0, 0));
                            return;
                        }
                        const ptr = copyBytes(value);
                        try { check(wasm._kc_mmap_set(valid(), ptr, value.length)); }
                        finally { wasm._free(ptr); }
                    },

                    /**
                     * Save the native capability state.
                     * @return void.
                     */
                    save() { check(wasm._kc_mmap_save(valid())); },

                    /**
                     * Delete the mapped value.
                     * @return void.
                     */
                    del() { check(wasm._kc_mmap_del(valid())); },

                    /**
                     * Release the native capability.
                     * @return void.
                     */
                    close() {
                        if (handle) {
                            wasm._kc_mmap_close(handle);
                            handle = 0;
                        }
                    }
                };
            } finally { wasm._free(out); }
        });
    },

    /**
     * Return the native library version.
     * @return Version number.
     */
    version() { return Number(wasm._kc_mmap_version()); }
};
export default mmap;
