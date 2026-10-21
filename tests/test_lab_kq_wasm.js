"use strict";
/*
 * KeyQuorum co-signed delivery in the lab's WebAssembly shell, end to end: two copies of the
 * built module play two browser tabs (alice hosts the server, bob joins), connected through
 * the relay room itself (session-relay-room.js) with the lab's wire encoding, as the page
 * does. The shell commands are the real ones; the checkpoints are the native C ones.
 *
 * Needs the module built with KeyQuorum (EMCC_REQUIRED=1 make wasm); skips otherwise.
 */
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const {
  Room, attachSocket, LAB, OP, encodeFrame, FrameParser, encodeKqRoute, decodeKqRoute, payloadText,
} = require("../tools/browser-lab/session-relay-room.js");

const WASM_DIR = path.join(__dirname, "../tools/browser-lab/wasm");
const JS = path.join(WASM_DIR, "flintstone.js");

function skip(why) {
  console.log(`test_lab_kq_wasm: SKIP (${why})`);
  process.exit(0);
}

if (!fs.existsSync(JS)) skip("tools/browser-lab/wasm/flintstone.js not built; run make wasm");

async function tab(name, relay) {
  const factory = require(JS);
  const out = [];
  const events = [];
  let line = "";
  const mod = await factory({
    locateFile: f => path.join(WASM_DIR, f),
    print: text => out.push(String(text)),
    printErr: () => {},
  });
  if (typeof mod._wasm_kq_frame !== "function") skip("module built without KeyQuorum (KQ_LAB=0)");
  mod.onShellChar = code => {
    const ch = String.fromCharCode(code);
    if (ch === "\n") { out.push(line.replace(/\r$/, "")); line = ""; } else line += ch;
  };
  mod.onKqEvent = e => events.push(e);
  mod.onKqFrame = f => relay.send(name, f);
  mod._wasm_shell_init();
  const t = {
    name, mod, out, events,
    run(cmd) {
      const p = mod.stringToNewUTF8(cmd);
      mod._wasm_shell_line(p);
      mod._free(p);
    },
    frame(toHost, fromId, op, bytes) {
      const p = mod._malloc(bytes.length || 1);
      mod.HEAPU8.set(bytes, p);
      mod._wasm_kq_frame(toHost ? 1 : 0, fromId, op, p, bytes.length);
      mod._free(p);
    },
    publicKeys(user) {
      const p = mod.stringToNewUTF8(user);
      const at = mod._wasm_kq_public(p);
      mod._free(p);
      return at ? mod.HEAPU8.slice(at, at + 64) : null;
    },
    learn(user, keys) {
      const p = mod.stringToNewUTF8(user);
      const k = mod._malloc(64);
      mod.HEAPU8.set(keys, k);
      const ok = mod._wasm_kq_learn(p, k, k + 32);
      mod._free(p);
      mod._free(k);
      return ok;
    },
    roster(selfId, members) {
      const bytes = [];
      for (const m of members) {
        const p = Buffer.from(m.principal);
        bytes.push((m.id >> 8) & 0xff, m.id & 0xff, m.host ? 1 : 0, p.length, ...p);
      }
      const at = mod._malloc(bytes.length || 1);
      mod.HEAPU8.set(bytes, at);
      mod._wasm_kq_roster(selfId, at, bytes.length);
      mod._free(at);
    },
    text() { return out.join("\n") + "\n" + line; },
  };
  return t;
}

// The relay: the room itself, each tab on a socket that queues what the room sends it.
function makeRelay() {
  const room = new Room("lab");
  const carried = [];
  const relay = {
    tabs: {},
    links: {},
    join(name) {
      const socket = { readyState: 1, inbox: [], send(frame) { this.inbox.push(Uint8Array.from(frame)); }, close() {} };
      const link = attachSocket(room, socket);
      link.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, name));
      relay.links[name] = { link, socket, parser: new FrameParser() };
    },
    memberId(name) { return room.members.find(m => m.principal === name).memberId; },
    members() { return room.members.map(m => ({ id: m.memberId, principal: m.principal, host: m.isHost })); },
    // What the page does with a frame its shell hands it.
    send(from, f) {
      carried.push(Buffer.from(f.bytes));
      relay.links[from].link.onMessage(encodeFrame(LAB.KQ, encodeKqRoute(f.toHost, f.memberId, f.op, f.bytes)));
    },
    publish(from, keys) { relay.links[from].link.onMessage(encodeFrame(LAB.KQ_KEYS, keys)); },
    // What the page does with frames the room sends it.
    drain() {
      for (let busy = true, guard = 0; busy && guard < 1000; guard++) {
        busy = false;
        for (const [name, { socket, parser }] of Object.entries(relay.links)) {
          const chunks = socket.inbox.splice(0);
          for (const chunk of chunks) {
            for (const frame of parser.push(chunk)) {
              busy = true;
              if (frame.opcode === LAB.KQ) {
                const f = decodeKqRoute(frame.payload);
                relay.tabs[name].frame(f.toHost, f.memberId, f.op, f.bytes);
              } else if (frame.opcode === LAB.KQ_KEYS) {
                const p = frame.payload;
                relay.learned.push(relay.tabs[name].learn(payloadText(p.slice(3, 3 + p[2])), p.slice(3 + p[2])));
              }
            }
          }
        }
      }
    },
    learned: [],
    carried,
  };
  return relay;
}

function registered(t, user) {
  // The lab's own way in: Root adds the account, then the session switches to it.
  t.run("switchuser root");
  t.run(`useradd ${user}`);
  t.run(user);
  t.run(`switchuser ${user}`);
}

(async () => {
  const relay = makeRelay();
  const alice = await tab("alice", relay);
  const bob = await tab("bob", relay);
  relay.tabs = { alice, bob };
  registered(alice, "alice");
  registered(bob, "bob");
  relay.join("alice"); // first in: hosts the server
  relay.join("bob");
  relay.drain();
  alice.roster(relay.memberId("alice"), relay.members());
  bob.roster(relay.memberId("bob"), relay.members());

  // Keys published on join, as the page does; the host keeps the first keys it sees for a name.
  const bobKeys = bob.publicKeys("bob");
  relay.publish("bob", bobKeys);
  relay.drain();
  assert.deepStrictEqual(relay.learned, [1], "the host learned bob's keys through the room");
  assert.strictEqual(alice.learn("bob", bobKeys), 1, "the same keys again are fine");
  const other = Uint8Array.from(bobKeys); other[0] ^= 1;
  assert.strictEqual(alice.learn("bob", other), 0, "different keys for a known name are refused");

  // Encode (alice): the host answers bob's keys; the offer and the sealed letter go out.
  alice.run("write note.txt hello bob");
  alice.run("server file send -t note.txt -user bob");
  relay.drain();
  assert.match(alice.text(), /encode checkpoint ok/);
  assert.match(alice.text(), /offered note\.txt to bob \(delivery [0-9a-f]{32}\)/);

  // Latent (host) then announce: bob gets the request, not the file.
  const req = bob.events.find(e => e.kind === "request");
  assert(req, "bob is asked to co-sign");
  assert.strictEqual(req.b, "alice");
  assert.strictEqual(req.c, "note.txt");
  bob.run("cat note.txt");
  assert.match(bob.text(), /not found/, "nothing is delivered before bob co-signs");

  // Co-sign (bob) -> the host fires once and releases -> decode (bob).
  bob.run(`server file cosign ${req.a}`);
  relay.drain();
  bob.mod._wasm_kq_tick();
  relay.drain();
  const bt = bob.text();
  assert.match(bt, /co-sign checkpoint ok/);
  assert.match(bt, /decode checkpoint ok/);
  assert.match(bt, /note\.txt from alice decoded and verified/);
  bob.run("cat note.txt");
  assert.match(bob.text(), /\nhello bob\n/);
  assert(bob.events.some(e => e.kind === "received" && e.a === "note.txt"));

  // The relay never carried the plaintext: only the sealed letter and public offers.
  assert(!relay.carried.some(b => b.includes(Buffer.from("hello bob"))), "plaintext crossed the relay");

  // Names match regardless of case, as lab accounts do: BOB finds bob, and the delivery
  // names the account as it is spelled.
  alice.run("write case.txt any case");
  alice.run("server file send -t case.txt -user BOB");
  relay.drain();
  const reqCase = bob.events.filter(e => e.kind === "request").pop();
  assert.strictEqual(reqCase.c, "case.txt", "BOB reaches bob");
  assert.match(alice.text(), /offered case\.txt to bob /);
  bob.run(`server file cosign ${reqCase.a}`);
  relay.drain();
  bob.run("cat case.txt");
  assert.match(bob.text(), /\nany case\n/);

  // A user the server has no keys for.
  alice.run("server file send -t note.txt -user nobody");
  relay.drain();
  assert.match(alice.text(), /the server holds no keys for nobody/);

  // A second revision arrives as newer, but it needs the editor's supervisor first: root is an
  // ancestor of alice's scope, so root permits the head, and only then does the newer copy go out.
  alice.run("write note.txt hello again");
  alice.run("server file send -t note.txt -user bob");
  relay.drain();
  const req2 = bob.events.filter(e => e.kind === "request").pop();
  assert.notStrictEqual(req2.a, req.a);
  bob.run(`server file cosign ${req2.a}`);
  relay.drain();
  assert.match(bob.text(), /decode checkpoint refused\n  a newer copy needs the editor's supervisor/);
  bob.run("cat note.txt");
  assert.match(bob.text(), /cat note\.txt\nhello bob\n/, "bob keeps the copy he holds");

  alice.run("switchuser root");
  alice.run("server file permit /kq/alice/out/note.txt"); // the tracked copy the send staged
  assert.match(alice.text(), /permitted the head of note\.txt as Root/);
  alice.run("switchuser alice");
  alice.run("server file send -t note.txt -user bob --permitted");
  relay.drain();
  const reqPermitted = bob.events.filter(e => e.kind === "request").pop();
  assert.notStrictEqual(reqPermitted.a, req2.a);
  bob.run(`server file cosign ${reqPermitted.a}`);
  relay.drain();
  bob.run("cat note.txt");
  assert.match(bob.text(), /\nhello again\n/);

  // Only the recipient can co-sign: the host's checkpoint refuses anyone else's signature,
  // and nothing is released.
  alice.run("write other.txt for bob only");
  alice.run("server file send -t other.txt -user bob");
  relay.drain();
  const req3 = bob.events.filter(e => e.kind === "request").pop();
  bob.run("switchuser root");
  bob.run(`server file cosign ${req3.a}`);
  relay.drain();
  assert.match(bob.text(), /host checkpoint refused\n  co-signature refused: not addressed to this account/);
  bob.run("cat other.txt");
  assert.match(bob.text(), /cat other\.txt\nnot found/);

  console.log("test_lab_kq_wasm: PASS");
})().catch(error => {
  console.error(error);
  process.exit(1);
});
