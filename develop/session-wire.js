(() => {
  "use strict";
  const { OP } = (typeof module === "object" && module.exports)
    ? require("./session-wire-const.js")
    : globalThis.FlintstoneSessionWireConst;

  const HDR_LEN = OP.FL_NET_SESSION_HDR_LEN;

  /*
   * Lab relay opcodes (not session wire: the native shell never sees them). KeyQuorum frames
   * between lab shells travel inside LAB_KQ with their route in front:
   *   dir u8 (1 to the host, 0 to a member), member u16 big-endian, KQ opcode u8, KQ payload
   * The member is the destination when the host sends and the sender when the relay delivers
   * to the host. A KQ payload may fill a whole session frame, so LAB_KQ may carry the four
   * route bytes on top of FL_NET_SESSION_MAX_MSG; every other opcode keeps that limit.
   * LAB_KQ_KEYS carries a member's published public keys (signing 32, box 32); delivered to
   * the host it is: member u16, principal_len u8, principal, keys.
   * LAB_FILE carries a plain lab file (encodeFileOffer) and LAB_FILE_STATUS the relay's
   * answer to its sender, as text.
   */
  const LAB = Object.freeze({ KQ: 0x70, KQ_KEYS: 0x71, FILE: 0x72, FILE_STATUS: 0x73, KQ_ROUTE: 4 });

  function maxPayload(opcode) {
    return opcode === LAB.KQ ? OP.FL_NET_SESSION_MAX_MSG + LAB.KQ_ROUTE : OP.FL_NET_SESSION_MAX_MSG;
  }

  function encodeFrame(opcode, payload) {
    const body = payload ? (payload instanceof Uint8Array ? payload : new TextEncoder().encode(String(payload))) : new Uint8Array(0);
    if (body.length > maxPayload(opcode)) return null;
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
        if (plen > maxPayload(this.buf[2])) {
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

  /* Lab account names match regardless of ASCII case, as the lab's identity layer does. */
  function sameName(a, b) {
    const lower = s => String(s).replace(/[A-Z]/g, c => c.toLowerCase());
    return lower(a) === lower(b);
  }

  function memberIdFromPayload(payload, offset = 0) {
    return ((payload[offset] << 8) | payload[offset + 1]) >>> 0;
  }

  /*
   * LAB.FILE payload, one frame per file (lab files fit one frame):
   *   who_len u8, who, name_len u8, name, size u16 big-endian, bytes[size]
   * Sent to the relay, `who` is the recipient; delivered by the relay, it is the sender.
   * This is the lab's own format, so it has its own opcode: the session wire's FILE_OFFER
   * carries the native channel metadata and offer fields instead.
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

  function encodeKqRoute(toHost, memberId, op, bytes) {
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(0);
    if (data.length > OP.FL_NET_SESSION_MAX_MSG || !(memberId >= 0 && memberId <= 0xffff)) return null;
    const out = new Uint8Array(LAB.KQ_ROUTE + data.length);
    out[0] = toHost ? 1 : 0;
    out[1] = (memberId >> 8) & 0xff;
    out[2] = memberId & 0xff;
    out[3] = op & 0xff;
    out.set(data, LAB.KQ_ROUTE);
    return out;
  }

  function decodeKqRoute(payload) {
    if (!payload || payload.length < LAB.KQ_ROUTE || payload[0] > 1) return null;
    return {
      toHost: payload[0] === 1,
      memberId: memberIdFromPayload(payload, 1),
      op: payload[3],
      bytes: payload.slice(LAB.KQ_ROUTE),
    };
  }

  const api = {
    OP, LAB, HDR_LEN, encodeFrame, FrameParser, payloadText, memberIdFromPayload, sameName,
    encodeFileOffer, decodeFileOffer, encodeKqRoute, decodeKqRoute,
  };
  if (typeof module === "object" && module.exports) module.exports = api;
  else globalThis.FlintstoneSessionWire = api;
})();
