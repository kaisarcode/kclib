/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libpact WASM API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {pact} from "../bin/wasm/wasm32/libpact.js";
const trust = pact.init();
assert.ok(trust);
const invite = trust.invite();
assert.equal(typeof invite.uid, "string");
assert.equal(typeof invite.code, "string");
trust.close();
assert.throws(() => trust.invite(), /closed/);
console.log("[1/1] [PASS] javascript: trust context and invitation");
