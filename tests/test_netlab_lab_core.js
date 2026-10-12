"use strict";
// Unit test for the netlab additions to the browser-lab glue parser.
// Verifies that the LAB_NETMODE control token (emitted by the freestanding
// `netlab start`/`stop` verbs) parses into a netmode event, that it is strict,
// and that the existing SERVER_RELAY / session parsing is unchanged.
const assert = require("assert");
const { parseGuestLine } = require("../tools/browser-lab/lab-core.js");

// LAB_NETMODE -> netmode event
assert.deepStrictEqual(parseGuestLine("LAB_NETMODE lab"), { type: "netmode", mode: "lab" });
assert.deepStrictEqual(parseGuestLine("LAB_NETMODE live"), { type: "netmode", mode: "live" });
assert.deepStrictEqual(parseGuestLine("LAB_NETMODE lab\r"), { type: "netmode", mode: "lab" });

// Strict: only lab|live are valid modes; anything else is not a netmode event.
assert.strictEqual(parseGuestLine("LAB_NETMODE sideways"), null);
assert.strictEqual(parseGuestLine("LAB_NETMODE"), null);
assert.strictEqual(parseGuestLine("LAB_NETMODE lab extra"), null);

// Existing tokens still parse (no regression).
assert.deepStrictEqual(parseGuestLine("SESSION 2 user=carol"), { type: "session", session: 2, user: "carol" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY host room1"), { type: "server", op: "host", text: "room1" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY dns example.com"), { type: "dns", host: "example.com" });
assert.strictEqual(parseGuestLine("ordinary shell output"), null);

console.log("test_netlab_lab_core: PASS");
