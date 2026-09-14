(() => {
  "use strict";
  const wire = globalThis.FlintstoneSessionWire;
  if (!wire) throw new Error("FlintstoneSessionWire is not loaded");

  const { OP, encodeFrame, FrameParser, payloadText, memberIdFromPayload } = wire;

  function wsUrl(config) {
    if (config.relayUrl) return config.relayUrl;
    const port = config.relayPort || 8767;
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    return `${proto}//${location.hostname}:${port}/ws?room=${encodeURIComponent(config.relayRoom || "lab")}`;
  }

  function createRelayClient(config) {
    let socket = null;
    let parser = new FrameParser();
    let memberId = null;
    let display = "";
    let members = [];
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

    return {
      get connected() { return socket && socket.readyState === WebSocket.OPEN; },
      get memberId() { return memberId; },
      get display() { return display; },
      get members() { return members.slice(); },
      on(listener) { listeners.add(listener); return () => listeners.delete(listener); },
      async connect(principal) {
        if (this.connected) return;
        parser = new FrameParser();
        memberId = null;
        display = "";
        await new Promise((resolve, reject) => {
          socket = new WebSocket(wsUrl(config));
          socket.binaryType = "arraybuffer";
          socket.onopen = () => {
            const hello = encodeFrame(OP.FL_NET_SESSION_OP_HELLO, principal);
            socket.send(hello);
            resolve();
          };
          socket.onerror = () => reject(new Error("Session relay connection failed"));
          socket.onclose = () => emit({ type: "closed" });
          socket.onmessage = ({ data }) => {
            const chunk = new Uint8Array(data);
            for (const frame of parser.push(chunk)) handleFrame(frame);
          };
        });
      },
      sendMessage(text) {
        if (!this.connected) return false;
        const frame = encodeFrame(OP.FL_NET_SESSION_OP_MSG, text);
        if (!frame) return false;
        socket.send(frame);
        return true;
      },
      leave() {
        if (!this.connected) return;
        const frame = encodeFrame(OP.FL_NET_SESSION_OP_CTRL_LEAVE, new Uint8Array(0));
        if (frame) socket.send(frame);
        socket.close();
        socket = null;
        memberId = null;
      },
    };
  }

  globalThis.FlintstoneServerRelayClient = { createRelayClient, wsUrl };
})();
