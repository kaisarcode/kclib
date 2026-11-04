/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Tests the libemb WASM projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {emb} from "../bin/wasm/wasm32/libemb.js";
const vector = emb.embed("hello world");
assert.ok(vector instanceof Float32Array);
assert.equal(vector.length, emb.dimension());
console.log("[1/1] [PASS] javascript: embedding vector");
