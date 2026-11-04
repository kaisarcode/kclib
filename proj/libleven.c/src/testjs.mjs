/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libleven WebAssembly scripting API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {leven} from "../bin/wasm/wasm32/libleven.js";
assert.equal(leven.distance("kitten", "sitting"), 3);
assert.equal(leven.distance("🐱", "🐶"), 1);
assert.equal(leven.distance("kitten", "sitting", {maxDistance: 2}), 3);
assert.equal(leven.distance("same", "same", {maxDistance: 0}), 0);
console.log("[1/1] [PASS] javascript: Unicode edit distance");
