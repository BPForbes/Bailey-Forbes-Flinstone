// node --test .cursor/skills/devops-multi-agent-team/mcp/server.test.mjs
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { SEAT_ID_RE, findDoc, listDocs, mintSeatId, parseFrontmatter, validatePushJson } from './server.mjs';

const SERVER = fileURLToPath(new URL('./server.mjs', import.meta.url));

const goodPush = () => ({
  topic: 'mcp',
  round: 1,
  seat: 'coder-0a0a0a',
  authors: ['coder-0a0a0a'],
  to: 'reviewer-0b0b0b',
  notifies: ['QA'],
  closes: [],
  qa: [{ id: 'q1', from: 'coder-0a0a0a', to: 'reviewer-0b0b0b', ask: 'ok?', answer: 'yes', status: 'answered' }],
  files: { 'a.c': { hunks: [{ op: '+', lines: '12-20', why: 'add' }, { op: '-', lines: '3-4', why: 'drop' }], delta: '+9/-2' } },
});

test('frontmatter: plain and folded values', () => {
  const { meta, body } = parseFrontmatter('---\nname: x\ndescription: >-\n  one\n  two\n---\n# Body\n');
  assert.equal(meta.name, 'x');
  assert.equal(meta.description, 'one two');
  assert.equal(body, '# Body\n');
});

test('catalog: skill, references, agents and commands', () => {
  const docs = listDocs();
  const kinds = new Set(docs.map((d) => d.kind));
  for (const k of ['skill', 'reference', 'agent', 'command']) assert.ok(kinds.has(k), k);
  assert.ok(findDoc({ uri: 'devops://reference/push-json' }));
  assert.ok(findDoc({ kind: 'agent', name: 'devops-reviewer' }));
  assert.equal(findDoc({ kind: 'reference', name: '../SKILL' }), undefined);
  assert.equal(findDoc({ uri: 'devops://reference/../../../../etc/passwd' }), undefined);
});

test('mint_seat_id: format, role check, avoids ids already in thoughts', () => {
  const dir = mkdtempSync(join(tmpdir(), 'thoughts-'));
  try {
    const id = mintSeatId('coder', dir);
    assert.match(id, SEAT_ID_RE);
    assert.ok(id.startsWith('coder-'));
    assert.throws(() => mintSeatId('Coder', dir));
    assert.throws(() => mintSeatId('coder;rm', dir));
    assert.throws(() => mintSeatId('coder\nx', dir));
    mkdirSync(join(dir, 'non-finalized'));
    writeFileSync(join(dir, 'non-finalized', 'seat-registry.md'), `| ${id} | coder |\n`);
    for (let i = 0; i < 50; i++) assert.notEqual(mintSeatId('coder', dir), id);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('validate_push_json: accepts the documented shape', () => {
  const r = validatePushJson(JSON.stringify(goodPush()));
  assert.deepEqual(r.errors, []);
  assert.equal(r.valid, true);
});

test('validate_push_json: rejects bad shapes', () => {
  assert.equal(validatePushJson('{').valid, false);
  const p = goodPush();
  delete p.files;
  p.from = 'coder-0a0a0a';
  p.to = 'coder-0a0a0a';
  p.qa[0].status = 'done';
  const r = validatePushJson(p);
  assert.equal(r.valid, false);
  assert.ok(r.errors.some((e) => e.includes('"files"')));
  assert.ok(r.errors.some((e) => e.includes('replaced by "seat"')));
  assert.ok(r.errors.some((e) => e.includes('is an author')));
  assert.ok(r.errors.some((e) => e.includes('status')));
});

test('validate_push_json: warns on delta mismatch and open qa', () => {
  const p = goodPush();
  p.files['a.c'].delta = '+1/-0';
  p.qa[0].status = 'open';
  const r = validatePushJson(p);
  assert.equal(r.valid, true);
  assert.equal(r.warnings.length, 2);
});

test('stdio: initialize, tools, resources, prompts', async () => {
  const child = spawn(process.execPath, [SERVER], { stdio: ['pipe', 'pipe', 'inherit'] });
  const pending = new Map();
  let buf = '';
  child.stdout.on('data', (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const msg = JSON.parse(buf.slice(0, i));
      buf = buf.slice(i + 1);
      pending.get(msg.id)?.(msg);
    }
  });
  let next = 1;
  const rpc = (method, params) => new Promise((res) => {
    const id = next++;
    pending.set(id, res);
    child.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`);
  });
  try {
    const init = await rpc('initialize', { protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 't', version: '0' } });
    assert.equal(init.result.protocolVersion, '2025-06-18');
    child.stdin.write(`${JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' })}\n`);

    const tools = await rpc('tools/list', {});
    assert.deepEqual(tools.result.tools.map((t) => t.name).sort(), ['list_docs', 'mint_seat_id', 'read_doc', 'validate_push_json']);

    const minted = await rpc('tools/call', { name: 'mint_seat_id', arguments: { role: 'qa' } });
    assert.match(minted.result.structuredContent.seat_id, /^qa-[0-9a-f]{6}$/);

    const bad = await rpc('tools/call', { name: 'validate_push_json', arguments: { file: '../../AGENTS.md' } });
    assert.equal(bad.result.isError, true);

    const read = await rpc('resources/read', { uri: 'devops://skill' });
    assert.match(read.result.contents[0].text, /DevOps multi-agent team/);

    const prompts = await rpc('prompts/list', {});
    const names = prompts.result.prompts.map((p) => p.name);
    for (const n of ['devops', 'goal', 'devops-reviewer']) assert.ok(names.includes(n), n);
    assert.equal(names.filter((n) => n === 'devops').length, 1);
    const skill = await rpc('prompts/get', { name: 'devops', arguments: { goal: 'ship X' } });
    assert.match(skill.result.messages[0].content.text, /^---\nname: devops-multi-agent-team/);

    const got = await rpc('prompts/get', { name: 'goal', arguments: { args: 'ship X' } });
    assert.match(got.result.messages[0].content.text, /Arguments: ship X$/);

    const unknown = await rpc('nope', {});
    assert.equal(unknown.error.code, -32601);
  } finally {
    child.stdin.end();
    child.kill();
  }
});
