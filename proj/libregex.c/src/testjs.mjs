/**
 * testjs.mjs - libregex JavaScript tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {regex} from "../bin/wasm/wasm32/libregex.js";
const expression = regex.open("a(b)");
assert.equal(expression.match("ab")[1].text, "b");
assert.equal(expression.match("ac"), null);
assert.equal(expression.search("xxab", 2)[0].start, 2);
expression.close();
assert.throws(() => expression.match("ab"), /closed/);
console.log("[1/1] [PASS] javascript: regex captures and lifecycle");
