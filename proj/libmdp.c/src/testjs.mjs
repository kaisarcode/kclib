/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libmdp WebAssembly scripting API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {mdp} from "../bin/wasm/wasm32/libmdp.js";
const document = await mdp.open("---\ntitle: Home\n---\n# Hello");
assert.equal(await document.meta(), "title: Home");
assert.equal(await document.body(), "# Hello");
assert.match(await document.html(), /<h1>Hello<\/h1>/);
await document.close();
assert.throws(() => document.body(), /closed/);
console.log("[1/1] [PASS] javascript: document lifecycle");
