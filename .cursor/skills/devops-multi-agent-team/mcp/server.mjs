#!/usr/bin/env node
// MCP server for the devops-multi-agent-team skill.
//
// Serves the skill as MCP over stdio (newline-delimited JSON-RPC 2.0), with no
// npm dependencies. The Markdown files stay the single source of truth: every
// resource and prompt is read from disk on each request, never copied here.
//
//   resources  devops://skill, devops://reference/<name>,
//              devops://agent/<name>, devops://command/<name>
//   prompts    devops (the skill), one per other .cursor/commands/*.md,
//              one per .cursor/agents/devops-*.md
//   tools      list_docs, read_doc, mint_seat_id, validate_push_json
//
// Register (from the repo root): see .mcp.json, or
//   claude mcp add devops-multi-agent-team -- node .cursor/skills/devops-multi-agent-team/mcp/server.mjs

import { randomBytes } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const SERVER_NAME = 'devops-multi-agent-team';
const SERVER_VERSION = '1.0.0';
const SUPPORTED_PROTOCOLS = ['2025-11-25', '2025-06-18', '2025-03-26', '2024-11-05'];

const SKILL_DIR = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const REPO_ROOT = resolve(process.env.DEVOPS_TEAM_REPO_ROOT || join(SKILL_DIR, '..', '..', '..'));
const THOUGHTS_DIR = join(REPO_ROOT, '.cursor', 'thoughts');

// ---------------------------------------------------------------------------
// Document catalog (read from disk on every call)
// ---------------------------------------------------------------------------

const KINDS = {
  skill: { dir: SKILL_DIR, match: (f) => f === 'SKILL.md' },
  reference: { dir: join(SKILL_DIR, 'references'), match: (f) => f.endsWith('.md') },
  agent: { dir: join(REPO_ROOT, '.cursor', 'agents'), match: (f) => f.startsWith('devops-') && f.endsWith('.md') },
  command: { dir: join(REPO_ROOT, '.cursor', 'commands'), match: (f) => f.endsWith('.md') },
};

// Minimal YAML frontmatter reader: `key: value` and folded `key: >-` blocks.
export function parseFrontmatter(text) {
  const m = /^---\r?\n([\s\S]*?)\r?\n---\r?\n?/.exec(text);
  if (!m) return { meta: {}, body: text };
  const meta = {};
  const lines = m[1].split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const kv = /^([A-Za-z_][\w-]*):\s*(.*)$/.exec(lines[i]);
    if (!kv) continue;
    let value = kv[2].trim();
    if (/^[>|][-+]?$/.test(value)) {
      const parts = [];
      while (i + 1 < lines.length && /^\s+\S/.test(lines[i + 1])) parts.push(lines[++i].trim());
      value = parts.join(value.startsWith('|') ? '\n' : ' ');
    }
    meta[kv[1]] = value;
  }
  return { meta, body: text.slice(m[0].length) };
}

function docUri(kind, name) {
  return kind === 'skill' ? 'devops://skill' : `devops://${kind}/${name}`;
}

export function listDocs() {
  const docs = [];
  for (const [kind, { dir, match }] of Object.entries(KINDS)) {
    if (!existsSync(dir)) continue;
    for (const file of readdirSync(dir).sort()) {
      const path = join(dir, file);
      if (!match(file) || !statSync(path).isFile()) continue;
      const name = kind === 'skill' ? SERVER_NAME : file.replace(/\.md$/, '');
      const { meta, body } = parseFrontmatter(readFileSync(path, 'utf8'));
      const heading = /^#\s+(.+)$/m.exec(body);
      docs.push({
        kind,
        name,
        uri: docUri(kind, name),
        path: path.slice(REPO_ROOT.length + 1),
        description: meta.description || (heading ? heading[1] : name),
        _abs: path,
      });
    }
  }
  return docs;
}

// Resolves only names that exist in the catalog, so no caller-built path is
// ever opened.
export function findDoc({ uri, kind, name }) {
  const docs = listDocs();
  if (uri) return docs.find((d) => d.uri === uri);
  if (kind === 'skill') return docs.find((d) => d.kind === 'skill');
  return docs.find((d) => d.kind === kind && d.name === name);
}

function readDoc(doc) {
  return readFileSync(doc._abs, 'utf8');
}

function publicDoc({ _abs, ...rest }) {
  return rest;
}

// ---------------------------------------------------------------------------
// Seat ids (references/role-identity.md, Seat ids)
// ---------------------------------------------------------------------------

export const SEAT_ID_RE = /^[a-z][a-z0-9]*(?:-[a-z0-9]+)*-[0-9a-f]{6}$/;
const ROLE_RE = /^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/;

// True when `needle` appears in any file name or file content under dir.
function thoughtsMention(dir, needle) {
  if (!existsSync(dir)) return false;
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.name.includes(needle)) return true;
    if (entry.isDirectory()) {
      if (thoughtsMention(path, needle)) return true;
    } else if (entry.isFile() && readFileSync(path).includes(needle)) {
      return true;
    }
  }
  return false;
}

export function mintSeatId(role, thoughtsDir = THOUGHTS_DIR) {
  if (typeof role !== 'string' || !ROLE_RE.test(role)) {
    throw new UserError('role must be lowercase letters, digits and single hyphens, e.g. "coder" or "qa"');
  }
  for (let attempt = 0; attempt < 32; attempt++) {
    const id = `${role}-${randomBytes(3).toString('hex')}`;
    if (!thoughtsMention(thoughtsDir, id)) return id;
  }
  throw new UserError('could not mint an unused seat id after 32 attempts');
}

// ---------------------------------------------------------------------------
// Push JSON validation (references/push-json.md)
// ---------------------------------------------------------------------------

const PUSH_KEYS = ['topic', 'round', 'seat', 'authors', 'to', 'notifies', 'closes', 'qa', 'files'];
const QA_KEYS = ['id', 'from', 'to', 'ask', 'answer', 'status'];
const QA_STATUS = ['open', 'answered', 'withdrawn'];
const LINES_RE = /^(\d+)(?:-(\d+))?$/;
const DELTA_RE = /^\+(\d+)\/-(\d+)$/;
const ROLE_OR_SEAT_RE = /^[A-Za-z][A-Za-z0-9 -]*$/;

export function validatePushJson(input) {
  const errors = [];
  const warnings = [];
  let doc = input;
  if (typeof input === 'string') {
    try {
      doc = JSON.parse(input);
    } catch (e) {
      return { valid: false, errors: [`not valid JSON: ${e.message}`], warnings };
    }
  }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) {
    return { valid: false, errors: ['top level must be a JSON object'], warnings };
  }

  for (const k of PUSH_KEYS) if (!(k in doc)) errors.push(`missing top-level key "${k}"`);
  for (const k of Object.keys(doc)) {
    if (k === 'from') errors.push('"from" was replaced by "seat"');
    else if (!PUSH_KEYS.includes(k)) warnings.push(`unknown top-level key "${k}"`);
  }

  const seatOrRole = (v) => typeof v === 'string' && (SEAT_ID_RE.test(v) || ROLE_OR_SEAT_RE.test(v));
  if ('topic' in doc && (typeof doc.topic !== 'string' || !doc.topic)) errors.push('"topic" must be a non-empty string');
  if ('round' in doc && !(Number.isInteger(doc.round) && doc.round >= 1)) errors.push('"round" must be an integer >= 1');
  if ('seat' in doc && !(typeof doc.seat === 'string' && SEAT_ID_RE.test(doc.seat))) {
    errors.push('"seat" must be a seat id like "coder-0a0a0a"');
  }
  if ('authors' in doc) {
    if (!Array.isArray(doc.authors) || doc.authors.length === 0) errors.push('"authors" must be a non-empty array of seat ids');
    else doc.authors.forEach((a, i) => { if (typeof a !== 'string' || !SEAT_ID_RE.test(a)) errors.push(`authors[${i}] is not a seat id`); });
  }
  if ('to' in doc && !seatOrRole(doc.to)) errors.push('"to" must be a seat id or a role');
  if ('notifies' in doc) {
    if (!Array.isArray(doc.notifies)) errors.push('"notifies" must be an array');
    else doc.notifies.forEach((n, i) => { if (!seatOrRole(n)) errors.push(`notifies[${i}] must be a seat id or a role`); });
  }
  if ('closes' in doc && !Array.isArray(doc.closes)) errors.push('"closes" must be an array (it may be empty)');
  if (Array.isArray(doc.authors) && typeof doc.to === 'string' && doc.authors.includes(doc.to)) {
    errors.push(`"to" (${doc.to}) is an author; no seat reviews its own change`);
  }

  if ('qa' in doc) {
    if (!Array.isArray(doc.qa)) errors.push('"qa" must be an array');
    else {
      const ids = new Set();
      doc.qa.forEach((row, i) => {
        if (!row || typeof row !== 'object') { errors.push(`qa[${i}] must be an object`); return; }
        for (const k of QA_KEYS) if (!(k in row)) errors.push(`qa[${i}] missing "${k}"`);
        if ('status' in row && !QA_STATUS.includes(row.status)) errors.push(`qa[${i}].status must be one of ${QA_STATUS.join(', ')}`);
        if (row.id !== undefined) {
          if (ids.has(row.id)) errors.push(`qa[${i}].id "${row.id}" is duplicated`);
          ids.add(row.id);
        }
        for (const k of ['from', 'to']) if (k in row && !seatOrRole(row[k])) errors.push(`qa[${i}].${k} must be a seat id or a role`);
        if (row.status === 'open') warnings.push(`qa[${i}] (${row.id}) is open; it must be answered or withdrawn before Satisfied`);
      });
    }
  }

  if ('files' in doc) {
    if (!doc.files || typeof doc.files !== 'object' || Array.isArray(doc.files)) errors.push('"files" must be an object keyed by file path');
    else {
      for (const [path, entry] of Object.entries(doc.files)) {
        const at = `files["${path}"]`;
        if (!entry || typeof entry !== 'object' || Array.isArray(entry)) { errors.push(`${at} must be an object with "hunks" and "delta"`); continue; }
        if (!Array.isArray(entry.hunks)) errors.push(`${at}.hunks must be an array`);
        const delta = typeof entry.delta === 'string' ? DELTA_RE.exec(entry.delta) : null;
        if (!delta) errors.push(`${at}.delta must look like "+N/-M"`);
        let added = 0;
        let removed = 0;
        (Array.isArray(entry.hunks) ? entry.hunks : []).forEach((h, i) => {
          const hat = `${at}.hunks[${i}]`;
          if (!h || typeof h !== 'object') { errors.push(`${hat} must be an object`); return; }
          if (!['-', '+', '~'].includes(h.op)) errors.push(`${hat}.op must be "-", "+" or "~"`);
          const lm = typeof h.lines === 'string' ? LINES_RE.exec(h.lines) : null;
          if (!lm) errors.push(`${hat}.lines must be "N" or "start-end"`);
          else {
            const start = Number(lm[1]);
            const end = lm[2] ? Number(lm[2]) : start;
            if (end < start) errors.push(`${hat}.lines ends before it starts`);
            const count = end - start + 1;
            if (h.op === '+') added += count;
            if (h.op === '-') removed += count;
          }
          if (typeof h.why !== 'string' || !h.why.trim()) errors.push(`${hat}.why must say why`);
        });
        if (delta && Array.isArray(entry.hunks) && (Number(delta[1]) !== added || Number(delta[2]) !== removed)) {
          warnings.push(`${at}.delta is ${entry.delta} but the hunks add ${added} and remove ${removed} lines`);
        }
      }
    }
  }

  return { valid: errors.length === 0, errors, warnings };
}

// ---------------------------------------------------------------------------
// MCP surface
// ---------------------------------------------------------------------------

class UserError extends Error {}

const TOOLS = [
  {
    name: 'list_docs',
    title: 'List DevOps team docs',
    description: 'List the skill, its references, the DevOps agent prompts and the slash commands, with their resource URIs.',
    inputSchema: {
      type: 'object',
      properties: { kind: { type: 'string', enum: Object.keys(KINDS), description: 'Only list this kind.' } },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: 'read_doc',
    title: 'Read a DevOps team doc',
    description: 'Read one doc by its URI (from list_docs), or by kind and name (for example kind "reference", name "push-json").',
    inputSchema: {
      type: 'object',
      properties: {
        uri: { type: 'string' },
        kind: { type: 'string', enum: Object.keys(KINDS) },
        name: { type: 'string' },
      },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: 'mint_seat_id',
    title: 'Mint a seat id',
    description: 'Mint a new seat id "<role>-<6 hex>" that appears nowhere under .cursor/thoughts (file names or contents), per references/role-identity.md. The Orchestrator still records it in seat-registry.md.',
    inputSchema: {
      type: 'object',
      properties: { role: { type: 'string', description: 'Lowercase role, e.g. coder, reviewer, security, qa, researcher.' } },
      required: ['role'],
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true },
  },
  {
    name: 'validate_push_json',
    title: 'Validate a Push JSON',
    description: 'Check a Push JSON against references/push-json.md: required keys, seat ids, qa rows, hunks and delta. Pass the JSON text, or a file name under .cursor/thoughts/non-finalized/. It does not compare to the side-branch diff; Reviewers still do that.',
    inputSchema: {
      type: 'object',
      properties: {
        json: { type: 'string', description: 'The Push JSON text.' },
        file: { type: 'string', description: 'A push-*.json file name in .cursor/thoughts/non-finalized/.' },
      },
      additionalProperties: false,
    },
    annotations: { readOnlyHint: true },
  },
];

// The skill prompt is named after the /devops command; that command file only
// points at the skill, so it is not served as a second prompt.
const SKILL_PROMPT = 'devops';

function promptCatalog() {
  const prompts = [];
  for (const doc of listDocs()) {
    if (doc.kind === 'skill') {
      prompts.push({ doc, name: SKILL_PROMPT, description: 'Run as the DevOps multi-agent Orchestrator.', arg: { name: 'goal', description: 'What the team should achieve.' } });
    } else if (doc.kind === 'command' && doc.name === SKILL_PROMPT) {
      continue;
    } else if (doc.kind === 'command') {
      prompts.push({ doc, name: doc.name, description: doc.description, arg: { name: 'args', description: 'Arguments for the command.' } });
    } else if (doc.kind === 'agent') {
      prompts.push({ doc, name: doc.name, description: doc.description, arg: { name: 'task', description: 'The task and seat id for this role.' } });
    }
  }
  return prompts;
}

function textResult(text, isError = false) {
  return { content: [{ type: 'text', text }], isError };
}

function callTool(name, args = {}) {
  switch (name) {
    case 'list_docs': {
      const docs = listDocs().filter((d) => !args.kind || d.kind === args.kind).map(publicDoc);
      return { ...textResult(JSON.stringify(docs, null, 2)), structuredContent: { docs } };
    }
    case 'read_doc': {
      const doc = findDoc(args);
      if (!doc) throw new UserError('no such doc; call list_docs for the names');
      return textResult(readDoc(doc));
    }
    case 'mint_seat_id': {
      const id = mintSeatId(args.role);
      return { ...textResult(id), structuredContent: { seat_id: id } };
    }
    case 'validate_push_json': {
      let text = args.json;
      if (args.file !== undefined) {
        if (typeof args.file !== 'string' || !/^push-[a-z0-9-]+\.json$/.test(args.file)) {
          throw new UserError('file must be a name like push-coder-0a0a0a.json');
        }
        const path = join(THOUGHTS_DIR, 'non-finalized', args.file);
        if (!existsSync(path)) throw new UserError(`${args.file} not found in .cursor/thoughts/non-finalized/`);
        text = readFileSync(path, 'utf8');
      }
      if (typeof text !== 'string') throw new UserError('pass "json" or "file"');
      const result = validatePushJson(text);
      return { ...textResult(JSON.stringify(result, null, 2), !result.valid), structuredContent: result };
    }
    default:
      return null;
  }
}

class RpcError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

export function handle(method, params = {}) {
  switch (method) {
    case 'initialize': {
      const asked = params.protocolVersion;
      return {
        protocolVersion: SUPPORTED_PROTOCOLS.includes(asked) ? asked : SUPPORTED_PROTOCOLS[0],
        capabilities: { tools: {}, resources: {}, prompts: {} },
        serverInfo: { name: SERVER_NAME, version: SERVER_VERSION },
        instructions:
          'DevOps multi-agent team skill for this repo. Start with the devops prompt (/devops) or read devops://skill. ' +
          'Only QA may give the OK to push. AGENTS.md, CLAUDE.md and .cursor/rules/*.mdc take precedence over the skill.',
      };
    }
    case 'ping':
      return {};
    case 'tools/list':
      return { tools: TOOLS };
    case 'tools/call': {
      try {
        const result = callTool(params.name, params.arguments || {});
        if (!result) throw new RpcError(-32602, `unknown tool: ${params.name}`);
        return result;
      } catch (e) {
        if (e instanceof UserError) return textResult(e.message, true);
        throw e;
      }
    }
    case 'resources/list':
      return {
        resources: listDocs().map((d) => ({ uri: d.uri, name: `${d.kind}/${d.name}`, description: d.description, mimeType: 'text/markdown' })),
      };
    case 'resources/templates/list':
      return { resourceTemplates: [] };
    case 'resources/read': {
      const doc = findDoc({ uri: params.uri });
      if (!doc) throw new RpcError(-32002, `resource not found: ${params.uri}`);
      return { contents: [{ uri: doc.uri, mimeType: 'text/markdown', text: readDoc(doc) }] };
    }
    case 'prompts/list':
      return {
        prompts: promptCatalog().map((p) => ({ name: p.name, description: p.description, arguments: [{ ...p.arg, required: false }] })),
      };
    case 'prompts/get': {
      const p = promptCatalog().find((x) => x.name === params.name);
      if (!p) throw new RpcError(-32602, `unknown prompt: ${params.name}`);
      const value = params.arguments && params.arguments[p.arg.name];
      let text = `${readDoc(p.doc)}\n\n---\nSource: ${p.doc.path}. Other skill docs: devops://reference/<name> (see list_docs).`;
      if (value) text += `\n\n${p.arg.name === 'goal' ? 'Goal' : p.arg.name === 'task' ? 'Task' : 'Arguments'}: ${value}`;
      return { description: p.description, messages: [{ role: 'user', content: { type: 'text', text } }] };
    }
    default:
      throw new RpcError(-32601, `method not found: ${method}`);
  }
}

function serve() {
  const send = (msg) => process.stdout.write(`${JSON.stringify(msg)}\n`);
  const rl = createInterface({ input: process.stdin, crlfDelay: Infinity });
  rl.on('line', (line) => {
    if (!line.trim()) return;
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      send({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'parse error' } });
      return;
    }
    const isRequest = msg && msg.id !== undefined && msg.id !== null && typeof msg.method === 'string';
    if (!isRequest) return; // notifications and responses need no reply
    try {
      send({ jsonrpc: '2.0', id: msg.id, result: handle(msg.method, msg.params || {}) });
    } catch (e) {
      const code = e instanceof RpcError ? e.code : -32603;
      send({ jsonrpc: '2.0', id: msg.id, error: { code, message: e.message } });
    }
  });
  rl.on('close', () => process.exit(0));
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) serve();
