/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Tests the libmmap WASM projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {mmap} from "../bin/wasm/wasm32/libmmap.js";
const map = mmap.open("kc-wasm-test.bin");
assert.equal(map.get(), null);
map.set(Uint8Array.from([0, 1, 255]));
assert.deepEqual(map.get(), Uint8Array.from([0, 1, 255]));
map.save();
map.del();
map.close();
console.log("[1/1] [PASS] javascript: file-backed value");
