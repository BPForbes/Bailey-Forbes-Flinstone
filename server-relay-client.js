(() => {
  "use strict";
  const wire = globalThis.FlintstoneSessionWire;
  if (!wire) throw new Error("FlintstoneSessionWire is not loaded");

  const { OP, encodeFrame, FrameParser, payloadText, memberIdFromPayload } = wire;
  const LOCAL_HOSTS = new Set(["127.0.0.1", "127.0.0.2", "localhost", "::1"]);
  const DEFAULT_PUBLIC_RELAY_HOST = "flintstone.bailey-forbes.com";

  function roomName(config) {
    return (config && config.relayRoom) || "lab";
  }

  function publicRelayUrl(config) {
    if (config && config.publicRelayUrl) return config.publicRelayUrl;
    return `wss://${DEFAULT_PUBLIC_RELAY_HOST}/ws?room=${encodeURIComponent(roomName(config))}`;
  }

  function hostnameOf(config) {
    if (config && config.hostname) return config.hostname;
    try {
      if (typeof location !== "undefined" && location.hostname) return location.hostname;
    } catch (_) { /* ignore */ }
    return "";
  }

  function wsUrl(config) {
    if (config.relayUrl) return config.relayUrl;
    const room = encodeURIComponent(roomName(config));
    const host = hostnameOf(config);
    if (LOCAL_HOSTS.has(host)) {
      const port = config.relayPort || 8767;
      const proto = (typeof location !== "undefined" && location.protocol === "https:") ? "wss:" : "ws:";
      return `${proto}//${host}:${port}/ws?room=${room}`;
    }
    if (host === DEFAULT_PUBLIC_RELAY_HOST || (host.endsWith(".workers.dev") && host.includes("flintstone"))) {
      return `wss://${host}/ws?room=${room}`;
    }
    return publicRelayUrl(config);
  }

  function healthUrl(ws) {
    try {
      const url = new URL(ws);
      url.protocol = url.protocol === "wss:" ? "https:" : "http:";
      url.pathname = "/relay-health";
      url.search = "";
      url.hash = "";
      return url.href;
    } catch (_) {
      return "";
    }
  }

  function shouldTryWebSocket(config) {
    if (!config) return false;
    if (config.forceBroadcast) return false;
    if (config.relayUrl) return true;
    if (config.skipPublicRelay && !LOCAL_HOSTS.has(hostnameOf(config))) return false;
    return true;
  }

  async function relayEndpointReady(ws, config) {
    if (config && config.skipRelayHealth) return true;
    let host = "";
    try { host = new URL(ws).hostname; } catch (_) { return true; }
    if (LOCAL_HOSTS.has(host)) return true;
    if (typeof fetch !== "function") return true;
    const probe = healthUrl(ws);
    if (!probe) return true;
    const ctrl = typeof AbortController === "function" ? new AbortController() : null;
    const timer = setTimeout(() => { try { ctrl && ctrl.abort(); } catch (_) { /* ignore */ } }, 900);
    try {
      const response = await fetch(probe, {
        method: "GET",
        cache: "no-store",
        mode: "cors",
        signal: ctrl ? ctrl.signal : undefined,
      });
      return response.ok;
    } catch (_) {
      return false;
    } finally {
      clearTimeout(timer);
    }
  }

  function formatChatLine(display, text) {
    return `${display || "flinstone"}: ${text || ""}`;
  }

  function randomMemberId() {
    return 2 + Math.floor(Math.random() * 253);
  }

  function createRelayClient(config) {
    let socket = null;
    let parser = new FrameParser();
    let memberId = null;
    let display = "";
    let principalName = "";
    let members = [];
    let connectGate = Promise.resolve();
    const listeners = new Set();
    const emit = event => listeners.forEach(fn => { try { fn(event); } catch (_) { /* ignore */ } });

    function handleFrame(frame) {
      const { opcode, payload } = frame;
      if (opcode === OP.FL_NET_SESSION_OP_HELLO_ACK && payload.length >= 2) {
        memberId = memberIdFromPayload(payload);
        display = payloadText(payload.slice(2));
        emit({ type: "hello", memberId, display });
        return;
      }
      if (opcode === OP.FL_NET_SESSION_OP_JOIN_ANNOUNCE || opcode === OP.FL_NET_SESSION_OP_LEAVE_ANNOUNCE ||
          opcode === OP.FL_NET_SESSION_OP_SERVER_ANNOUNCE) {
        emit({ type: "announcement", text: payloadText(payload) });
        return;
      }
      if (opcode === OP.FL_NET_SESSION_OP_MSG_BROADCAST) {
        emit({ type: "message", text: payloadText(payload), broadcast: true });
        return;
      }
      if (opcode === OP.FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT) {
        members = [];
        let off = 0;
        while (off + 6 <= payload.length) {
          const id = memberIdFromPayload(payload, off);
          const isHost = payload[off + 2] === 1;
          const plen = payload[off + 4];
          off += 5;
          if (off + plen > payload.length) break;
          const principal = payloadText(payload.slice(off, off + plen));
          off += plen;
          if (off >= payload.length) break;
          const nlen = payload[off++];
          const nick = off + nlen <= payload.length ? payloadText(payload.slice(off, off + nlen)) : "";
          off += nlen;
          members.push({ memberId: id, principal, nick, isHost });
        }
        emit({ type: "roster", members: members.slice() });
        return;
      }
      if (opcode === OP.FL_NET_SESSION_OP_ERR) {
        emit({ type: "error", text: payloadText(payload) });
      }
    }

    function closeSocket() {
      if (!socket) return;
      try { socket.close(); } catch (_) { /* ignore */ }
      socket = null;
    }

    function openBroadcast(name) {
      if (typeof BroadcastChannel === "undefined")
        throw new Error("Session relay connection failed");
      closeSocket();
      const room = config.relayRoom || "lab";
      const tabId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
      const channel = new BroadcastChannel(`flintstone-relay-${room}`);
      memberId = randomMemberId();
      display = name;
      principalName = name;
      members = [{ memberId, principal: name, nick: "", isHost: true }];
      channel.onmessage = ({ data }) => {
        if (!data || data.v !== 1 || data.tabId === tabId) return;
        if (data.kind === "hello") {
          members = members.filter(m => m.memberId !== data.memberId);
          members.push({ memberId: data.memberId, principal: data.principal, nick: "", isHost: false });
          if (members.length && members.every(m => m.memberId >= memberId))
            members.forEach(m => { if (m.memberId === memberId) m.isHost = true; });
          channel.postMessage({ v: 1, kind: "hello-ack", tabId, memberId, principal: display });
          emit({ type: "roster", members: members.slice() });
        } else if (data.kind === "hello-ack") {
          members = members.filter(m => m.memberId !== data.memberId);
          members.push({ memberId: data.memberId, principal: data.principal, nick: "", isHost: data.memberId < memberId });
          if (data.memberId < memberId)
            members.forEach(m => { if (m.memberId === memberId) m.isHost = false; });
          emit({ type: "roster", members: members.slice() });
        } else if (data.kind === "msg") {
          emit({ type: "message", text: data.text, broadcast: true });
        } else if (data.kind === "leave") {
          members = members.filter(m => m.memberId !== data.memberId);
          emit({ type: "roster", members: members.slice() });
        }
      };
      channel.postMessage({ v: 1, kind: "hello", tabId, memberId, principal: name });
      socket = { readyState: 1, send() {}, close() { channel.close(); }, channel, tabId };
      emit({ type: "hello", memberId, display });
      emit({ type: "announcement", text: "using same-origin BroadcastChannel relay" });
    }

    function connectWebSocket(name) {
      return new Promise((resolve, reject) => {
        let settled = false;
        const finish = (err) => {
          if (settled) return;
          settled = true;
          clearTimeout(timer);
          if (err) reject(err);
          else resolve();
        };
        const timer = setTimeout(() => finish(new Error("Session relay connection timed out")), 2500);
        const ws = new WebSocket(wsUrl(config));
        socket = ws;
        ws.binaryType = "arraybuffer";
        ws.onopen = () => {
          const hello = encodeFrame(OP.FL_NET_SESSION_OP_HELLO, name);
          ws.send(hello);
          finish();
        };
        ws.onerror = () => finish(new Error("Session relay connection failed"));
        ws.onclose = () => {
          if (socket === ws) emit({ type: "closed" });
        };
        ws.onmessage = ({ data }) => {
          const chunk = new Uint8Array(data);
          for (const frame of parser.push(chunk)) handleFrame(frame);
        };
      });
    }

    async function connectOnce(principal) {
      const name = principal || "flinstone";
      if (socket && socket.readyState === 1 && principalName === name) return;
      if (socket && socket.readyState === 1) {
        if (socket.channel)
          socket.channel.postMessage({ v: 1, kind: "leave", tabId: socket.tabId, memberId });
        else {
          const frame = encodeFrame(OP.FL_NET_SESSION_OP_CTRL_LEAVE, new Uint8Array(0));
          if (frame) socket.send(frame);
        }
        closeSocket();
      }
      parser = new FrameParser();
      memberId = null;
      display = "";
      principalName = name;
      members = [];
      if (shouldTryWebSocket(config)) {
        try {
          const url = wsUrl(config);
          if (!(await relayEndpointReady(url, config))) throw new Error("Session relay unavailable");
          await connectWebSocket(name);
          return;
        } catch (error) {
          closeSocket();
          if (typeof BroadcastChannel === "undefined") throw error;
        }
      }
      openBroadcast(name);
    }

    const client = {
      get connected() { return socket && socket.readyState === 1; },
      get memberId() { return memberId; },
      get display() { return display; },
      get principal() { return principalName; },
      get members() { return members.slice(); },
      on(listener) { listeners.add(listener); return () => listeners.delete(listener); },
      connect(principal) {
        const run = connectGate.then(() => connectOnce(principal), () => connectOnce(principal));
        connectGate = run.catch(() => {});
        return run;
      },
      sendMessage(text) {
        if (!this.connected) return false;
        const line = formatChatLine(display, text);
        if (socket.channel) {
          socket.channel.postMessage({ v: 1, kind: "msg", tabId: socket.tabId, text: line, memberId });
          emit({ type: "message", text: line, local: true });
          return true;
        }
        const frame = encodeFrame(OP.FL_NET_SESSION_OP_MSG, text);
        if (!frame) return false;
        socket.send(frame);
        emit({ type: "message", text: line, local: true });
        return true;
      },
      leave() {
        if (!this.connected) return;
        if (socket.channel) {
          socket.channel.postMessage({ v: 1, kind: "leave", tabId: socket.tabId, memberId });
        } else {
          const frame = encodeFrame(OP.FL_NET_SESSION_OP_CTRL_LEAVE, new Uint8Array(0));
          if (frame) socket.send(frame);
        }
        closeSocket();
        memberId = null;
        display = "";
        principalName = "";
        members = [];
      },
    };
    return client;
  }

  const api = {
    createRelayClient, wsUrl, shouldTryWebSocket, formatChatLine,
    healthUrl, publicRelayUrl, DEFAULT_PUBLIC_RELAY_HOST,
  };
  globalThis.FlintstoneServerRelayClient = api;
  if (typeof module === "object" && module.exports) module.exports = api;
})();
