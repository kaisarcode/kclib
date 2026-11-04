/**
 * testjs.mjs - JavaScript contract tests.
 * Summary: Verifies the libhttp WASM API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

import assert from "node:assert/strict";
import {http} from "../bin/wasm/wasm32/libhttp.js";
const wire = http.request({method: "GET", target: "/notes/42", headers: {Host: "localhost"}});
assert.ok(wire instanceof Uint8Array);
const messages = [];
const parser = http.parser({onRequest: value => messages.push(value)});
parser.write(wire);
parser.close();
assert.equal(messages.length, 1);
assert.equal(messages[0].target, "/notes/42");
assert.equal(messages[0].headers[0].name.toLowerCase(), "host");
console.log("[1/1] [PASS] javascript: HTTP builder and parser");
