/**
 * testjs.mjs - liblng JavaScript tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {lng} from "../bin/wasm/wasm32/liblng.js";
assert.deepEqual(lng.detect(""), []);
const result = lng.detect("This is an English sentence.", {limit: 2});
assert.ok(Array.isArray(result));
for (const item of result) { assert.equal(typeof item.code, "string"); assert.equal(typeof item.score, "number"); }
console.log("[1/1] [PASS] javascript: language detection");
