/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Tests the libhnsw WASM projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {hnsw} from "../bin/wasm/wasm32/libhnsw.js";
const index = hnsw.open({dimension: 2});
index.add("first", new Float32Array([1, 0]));
index.add("second", new Float32Array([0, 1]));
index.build();
const matches = index.search(new Float32Array([1, 0]), {limit: 2, threshold: 0});
assert.equal(matches[0].id, "first");
index.close();
assert.throws(() => index.count(), /closed/);
console.log("[1/1] [PASS] javascript: vector index");
