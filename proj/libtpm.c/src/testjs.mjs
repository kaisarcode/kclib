/**
 * testjs.mjs - libtpm JavaScript tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {tpm} from "../bin/wasm/wasm32/libtpm.js";
const profile = tpm.open("hello world");
const score = profile.score("hello world");
assert.ok(score >= 0 && score <= 1);
profile.close();
assert.throws(() => profile.score("hello"), /closed/);
console.log("[1/1] [PASS] javascript: text profile lifecycle");
