/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libb64 WebAssembly scripting API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {b64} from "../bin/wasm/wasm32/libb64.js";
assert.equal(b64.encode(new TextEncoder().encode("hello")), "aGVsbG8=");
assert.deepEqual(b64.decode("AAF/gP8="), Uint8Array.from([0, 1, 127, 128, 255]));
assert.deepEqual(b64.decode(""), new Uint8Array());
assert.throws(() => b64.decode("invalid!"));
console.log("[1/1] [PASS] javascript: binary Base64");
