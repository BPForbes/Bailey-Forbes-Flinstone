"use strict";
// Regression test for the mojibake bug: the freestanding shell writes real
// UTF-8 text (e.g. an em dash, U+2014 = bytes E2 80 94) one byte at a time
// over its serial/shell-char stream. tools/browser-lab/wasm-adapter.js and
// lab.js must decode that stream with a *streaming* UTF-8 decoder, not
// String.fromCharCode(byte) on each raw byte in isolation — the latter
// treats every byte as its own Latin-1 code point and shreds any multi-byte
// sequence into garbage (an em dash becomes "â" plus two unprintable C1
// control characters).
//
// This file does two things: (1) proves the streaming-decode technique
// itself reconstructs a multi-byte sequence correctly and still decodes
// plain ASCII with no added delay, and (2) checks the actual shipped
// source of both call sites so a future edit can't quietly revert to the
// broken per-byte String.fromCharCode pattern without failing this test.
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const EM_DASH = "—"; // —
const EM_DASH_UTF8_BYTES = [0xe2, 0x80, 0x94];

function decodeByteStream(bytes) {
  const decoder = new TextDecoder("utf-8", { fatal: false });
  let out = "";
  const perByte = [];
  for (const b of bytes) {
    const ch = decoder.decode(new Uint8Array([b]), { stream: true });
    perByte.push(ch);
    out += ch;
  }
  return { out, perByte };
}

// 1a. The 3-byte em dash sequence: only the final byte should yield output.
{
  const { out, perByte } = decodeByteStream(EM_DASH_UTF8_BYTES);
  assert.strictEqual(out, EM_DASH, "streaming decode must reconstruct the em dash exactly");
  assert.deepStrictEqual(perByte, ["", "", EM_DASH],
    "the first two continuation bytes must buffer (empty string), not each emit their own character");
}

// 1b. A realistic mixed line: ASCII text with an em dash in the middle,
// exactly matching the shape of e.g. "defend \xe2\x80\x94 detection...".
{
  const line = "defend " + EM_DASH + " detection & prevention:\r\n";
  const bytes = Buffer.from(line, "utf8");
  const { out } = decodeByteStream(bytes);
  assert.strictEqual(out, line, "a full line with an embedded em dash must round-trip exactly");
}

// 1c. Plain ASCII still decodes one character per byte with zero buffering
// (no regression in the common case, which is the overwhelming majority of
// guest output).
{
  const bytes = Buffer.from("shell> whoami\r\n", "utf8");
  const { perByte } = decodeByteStream(bytes);
  assert.ok(perByte.every(ch => ch.length === 1), "every ASCII byte must decode immediately to exactly one character");
}

// 1d. The old, broken technique must NOT reconstruct the em dash — this
// pins down exactly what the bug looked like, so the fix's necessity is
// documented in the test itself, not just asserted away.
{
  const mojibake = EM_DASH_UTF8_BYTES.map(b => String.fromCharCode(b)).join("");
  assert.notStrictEqual(mojibake, EM_DASH, "sanity: the naive per-byte decode is expected to NOT equal a real em dash");
  assert.strictEqual(mojibake, "â\u0080\u0094", "sanity: naive decode produces the exact mojibake seen in the bug report");
}

// 2. Structural check on the actual shipped files: both call sites that
// render guest-emitted bytes to the screen must use a streaming decoder,
// not a bare String.fromCharCode(byte) on the raw byte.
const root = path.join(__dirname, "..");

{
  const src = fs.readFileSync(path.join(root, "tools/browser-lab/wasm-adapter.js"), "utf8");
  assert.ok(/new TextDecoder\(/.test(src),
    "wasm-adapter.js must decode onShellChar's byte stream with a TextDecoder");
  assert.ok(/stream:\s*true/.test(src),
    "wasm-adapter.js's decoder must use { stream: true } so multi-byte sequences buffer across calls");
  assert.ok(!/const ch = String\.fromCharCode\(code\);/.test(src),
    "wasm-adapter.js must not go back to decoding each raw onShellChar byte with String.fromCharCode");
}

{
  const src = fs.readFileSync(path.join(root, "tools/browser-lab/lab.js"), "utf8");
  assert.ok(/new TextDecoder\(/.test(src),
    "lab.js must decode the emulator's serialByte stream with a TextDecoder");
  assert.ok(!/const ch = String\.fromCharCode\(byte\);/.test(src),
    "lab.js must not go back to decoding each raw serialByte with String.fromCharCode");
}

console.log("test_browser_lab_utf8: PASS");
