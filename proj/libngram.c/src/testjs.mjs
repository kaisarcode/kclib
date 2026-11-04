/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Tests the libngram WASM projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {ngram} from "../bin/wasm/wasm32/libngram.js";
const chunks = ngram.traverse("hello world");
assert.ok(Array.isArray(chunks) && chunks.length > 0);
assert.equal(typeof chunks[0].tokenStart, "number");
assert.equal(typeof chunks[0].text, "string");
console.log("[1/1] [PASS] javascript: n-gram traversal");
