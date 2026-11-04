/**
 * testjs.mjs - libtpl JavaScript tests.
 * Summary: Verifies the WASM API projection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {tpl} from "../bin/wasm/wasm32/libtpl.js";
const page = tpl.open("<h1>{{ title }}</h1>");
assert.equal(page.render({title: "Home"}), "<h1>Home</h1>");
page.close();
assert.throws(() => page.render(), /closed/);
console.log("[1/1] [PASS] javascript: template rendering");
