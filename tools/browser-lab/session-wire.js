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

  /*
   * FILE_OFFER payload, one frame per file (lab files fit one frame):
   *   who_len u8, who, name_len u8, name, size u16 big-endian, bytes[size]
   * Sent to the relay, `who` is the recipient; delivered by the relay, it is the sender.
   */
  function encodeFileOffer(who, name, bytes) {
    const w = new TextEncoder().encode(String(who || ""));
    const n = new TextEncoder().encode(String(name || ""));
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(0);
    if (!w.length || w.length > 255 || !n.length || n.length > 255 || data.length > 0xffff) return null;
    const out = new Uint8Array(1 + w.length + 1 + n.length + 2 + data.length);
    let off = 0;
    out[off++] = w.length;
    out.set(w, off);
    off += w.length;
    out[off++] = n.length;
    out.set(n, off);
    off += n.length;
    out[off++] = (data.length >> 8) & 0xff;
    out[off++] = data.length & 0xff;
    out.set(data, off);
    return out;
  }

  function decodeFileOffer(payload) {
    if (!payload || payload.length < 1) return null;
    let off = 0;
    const wlen = payload[off++];
    if (!wlen || off + wlen + 1 > payload.length) return null;
    const who = payloadText(payload.slice(off, off + wlen));
    off += wlen;
    const nlen = payload[off++];
    if (!nlen || off + nlen + 2 > payload.length) return null;
    const name = payloadText(payload.slice(off, off + nlen));
    off += nlen;
    const size = (payload[off] << 8) | payload[off + 1];
    off += 2;
    if (off + size !== payload.length) return null;
    return { who, name, bytes: payload.slice(off) };
  }

  const api = {
    OP, HDR_LEN, encodeFrame, FrameParser, payloadText, memberIdFromPayload,
    encodeFileOffer, decodeFileOffer,
  };
  if (typeof module === "object" && module.exports) module.exports = api;
  else globalThis.FlintstoneSessionWire = api;
})();
