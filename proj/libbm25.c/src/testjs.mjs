/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {bm25} from "../bin/wasm/wasm32/libbm25.js";
const index = bm25.open();
index.add("find", "find files by name");
index.add("grep", "search text inside files");
index.build();
assert.equal(index.count(), 2);
assert.equal(index.search("find files", {limit: 1})[0].id, "find");
const binary = index.export();
assert.ok(binary instanceof Uint8Array && binary.length > 0);
const imported = bm25.import(binary);
assert.equal(imported.count(), 2);
imported.close();
index.close();
console.log("[1/1] [PASS] javascript: BM25 build and import");
