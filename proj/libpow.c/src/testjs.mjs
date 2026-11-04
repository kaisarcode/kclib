/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {pow} from "../bin/wasm/wasm32/libpow.js";
const challenge = pow.challenge(1);
assert.ok(challenge instanceof Uint8Array);
const solution = pow.solve(challenge);
assert.equal(pow.verify(challenge, solution), true);
assert.equal(pow.size(challenge), challenge.length);
assert.equal(pow.backendName(pow.SHA256), "sha256");
console.log("[1/1] [PASS] javascript: proof-of-work challenge");
