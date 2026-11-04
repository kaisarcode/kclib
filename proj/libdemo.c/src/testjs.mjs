/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libdemo WebAssembly scripting API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {demo} from "../bin/wasm/wasm32/libdemo.js";
assert.equal(demo.greet("John"), "Hello John!");
assert.equal(demo.greet(""), "Hello !");
assert.ok(Number.isSafeInteger(demo.version()));
console.log("[1/1] [PASS] javascript: greeting and version");
