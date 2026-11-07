(() => {
  "use strict";
  const { OP } = (typeof module === "object" && module.exports)
    ? require("./session-wire-const.js")
    : globalThis.FlintstoneSessionWireConst;

  const HDR_LEN = OP.FL_NET_SESSION_HDR_LEN;

  function encodeFrame(opcode, payload) {
    const body = payload ? (payload instanceof Uint8Array ? payload : new TextEncoder().encode(String(payload))) : new Uint8Array(0);
    if (body.length > OP.FL_NET_SESSION_MAX_MSG) return null;
    const out = new Uint8Array(HDR_LEN + body.length);
    out[0] = OP.FL_NET_SESSION_MAGIC;
    out[1] = OP.FL_NET_SESSION_VERSION;
    out[2] = opcode;
    out[3] = 0;
    out[4] = (body.length >> 8) & 0xff;
    out[5] = body.length & 0xff;
    if (body.length) out.set(body, HDR_LEN);
    return out;
  }

  class FrameParser {
    constructor() {
      this.buf = new Uint8Array(0);
    }
    push(chunk) {
      if (!chunk || !chunk.length) return [];
      const merged = new Uint8Array(this.buf.length + chunk.length);
      merged.set(this.buf);
      merged.set(chunk, this.buf.length);
      this.buf = merged;
      const frames = [];
      for (;;) {
        if (this.buf.length < HDR_LEN) break;
        if (this.buf[0] !== OP.FL_NET_SESSION_MAGIC || this.buf[1] !== OP.FL_NET_SESSION_VERSION || this.buf[3] !== 0) {
          this.buf = this.buf.slice(1);
          continue;
        }
        const plen = (this.buf[4] << 8) | this.buf[5];
        if (plen > OP.FL_NET_SESSION_MAX_MSG) {
          this.buf = this.buf.slice(1);
          continue;
        }
        const total = HDR_LEN + plen;
        if (this.buf.length < total) break;
        frames.push({ opcode: this.buf[2], payload: this.buf.slice(HDR_LEN, total) });
        this.buf = this.buf.slice(total);
      }
      return frames;
    }
  }

  function payloadText(payload) {
    return new TextDecoder().decode(payload);
  }

  function memberIdFromPayload(payload, offset = 0) {
    return ((payload[offset] << 8) | payload[offset + 1]) >>> 0;
  }

  const api = { OP, HDR_LEN, encodeFrame, FrameParser, payloadText, memberIdFromPayload };
  if (typeof module === "object" && module.exports) module.exports = api;
  else globalThis.FlintstoneSessionWire = api;
})();
