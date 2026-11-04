/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libmin WebAssembly scripting API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {min} from "../bin/wasm/wasm32/libmin.js";
assert.equal(min.txt("  Hello   world  "), "Hello world");
assert.equal(min.css("a { color: red; }"), "a{color:red}");
assert.equal(typeof min.js("var x = 1;"), "string");
console.log("[1/1] [PASS] javascript: asset minification");
