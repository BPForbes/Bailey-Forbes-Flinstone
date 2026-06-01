#!/usr/bin/env python3
"""
Standalone decoder for Flinstone P3-13 session frames inside a captured pcap.

Reads `tcp` payload bytes per (src,sport,dst,dport) stream, walks the 6-byte
session header (`contract_p3_session_wire.h`):

    [magic=0x46][version=1][opcode][flags][length_be (u16)][payload...]

and prints one line per frame with the mapped opcode name and an ASCII
preview of the payload.

Usage:
    decode_session_pcap.py <capture.pcap>           # plain text to stdout
    decode_session_pcap.py <capture.pcap> --counts  # also print per-opcode tally

Requires the `scapy` PyPI package (`pip install scapy`). On systems without
scapy the script exits 0 with a stub note so it does not break demo pipelines
that only need the raw pcap as the primary artifact.
"""

import struct
import sys
from collections import Counter

# Mirrors contract_p3_session_wire.h + contract_p3_server.h additions.
OPCODES = {
    0x01: "HELLO",
    0x02: "HELLO_ACK",
    0x03: "MEMBER_LIST",            # reserved (older shard)
    0x04: "NICK_PROMPT",
    0x05: "JOIN_ANNOUNCE",
    0x06: "LEAVE_ANNOUNCE",
    0x07: "NICK_SET_ANNOUNCE",
    0x08: "SERVER_ANNOUNCE",
    0x09: "MEMBER_LIST_SNAPSHOT",
    0x10: "MSG",
    0x11: "MSG_BROADCAST",
    0x12: "MSG_DIRECT",
    0x13: "MSG_DIRECT_DELIVER",
    0x20: "CTRL_LEAVE",
    0x21: "CTRL_KILL",
    0x22: "CTRL_HOST_PROMOTE",
    0x24: "CTRL_HOST_PROMOTE6",
    0x23: "HOST_NICK_SET",
    0x30: "FILE_OFFER",
    0x31: "FILE_CHUNK",
    0x32: "FILE_DONE",
    0x33: "FILE_ACCEPT",
    0x34: "FILE_DECLINE",
    0x35: "FILE_REVOKE",
    0x36: "FILE_LIST",
    0x37: "FILE_STATUS",
    0x38: "FILE_META",
    0x7F: "ERR",
}

MAGIC = 0x46
VERSION = 1
HDR_LEN = 6


def ascii_tail(buf: bytes, limit: int = 64) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in buf[:limit])


def decode_stream(payload: bytes):
    """Yield (offset, opcode, opcode_name, flags, length, body) per frame."""
    off = 0
    while off + HDR_LEN <= len(payload):
        magic, ver, op, flags, plen = struct.unpack(
            ">BBBBH", payload[off:off + HDR_LEN]
        )
        if magic != MAGIC or ver != VERSION:
            yield ("desync", off, magic, ver, payload[off:])
            return
        # Bounds-check the body before yielding a complete frame; a
        # truncated tail is reported as ("partial", ...) and we stop so
        # `off` never advances past the captured payload.
        if off + HDR_LEN + plen > len(payload):
            yield ("partial", off, payload[off:])
            return
        body = payload[off + HDR_LEN:off + HDR_LEN + plen]
        yield ("frame", off, op, flags, plen, body)
        off += HDR_LEN + plen
    if off < len(payload):
        yield ("partial", off, payload[off:])


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 0
    pcap_path = argv[1]
    show_counts = "--counts" in argv[2:]

    try:
        from scapy.all import rdpcap, TCP, Raw
    except Exception as exc:
        sys.stdout.write(
            "[decode_session_pcap] scapy not installed (%s); "
            "install with `pip install scapy` for a per-frame decode.\n" % exc
        )
        return 0

    pkts = rdpcap(pcap_path)
    # Per-stream payload reassembled by TCP seq, not pcap arrival order, so
    # `-i any` In/Out duplicates and retransmits collapse to one byte stream.
    streams = {}
    for p in pkts:
        if TCP not in p or Raw not in p:
            continue
        ip = p["IP"]
        key = (ip.src, p[TCP].sport, ip.dst, p[TCP].dport)
        streams.setdefault(key, [])
        streams[key].append((int(p[TCP].seq), bytes(p[Raw].load)))

    def reassemble(segments):
        """Sort by seq, drop overlap with already-emitted bytes, fill capture
        gaps with 0xFF so the frame walker resyncs at the next magic byte."""
        if not segments:
            return b""
        segments = sorted(segments, key=lambda t: t[0])
        out = bytearray()
        next_seq = segments[0][0]
        for seq, data in segments:
            if not data:
                continue
            if seq < next_seq:
                skip = next_seq - seq
                if skip >= len(data):
                    continue
                data = data[skip:]
                seq = next_seq
            if seq > next_seq:
                out.extend(b"\xff" * (seq - next_seq))
                next_seq = seq
            out.extend(data)
            next_seq += len(data)
        return bytes(out)

    streams = {k: reassemble(v) for k, v in streams.items()}

    opcode_counts = Counter()
    for key, buf in streams.items():
        print(
            "--- {}:{} -> {}:{}  ({} payload bytes) ---".format(
                key[0], key[1], key[2], key[3], len(buf)
            )
        )
        for item in decode_stream(buf):
            kind = item[0]
            if kind == "frame":
                _, off, op, flags, plen, body = item
                name = OPCODES.get(op, "?")
                opcode_counts[name] += 1
                print(
                    "  off=0x{:04x} magic=0x{:02x} ver={} op=0x{:02x} ({}) "
                    "flags={} len={} payload='{}'".format(
                        off, MAGIC, VERSION, op, name, flags, plen,
                        ascii_tail(body, 48),
                    )
                )
            elif kind == "desync":
                _, off, m, v, tail = item
                print(
                    "  off=0x{:04x} non-session bytes (magic=0x{:02x} "
                    "ver={}); sync lost; {} trailing bytes".format(
                        off, m, v, len(tail)
                    )
                )
            else:  # partial
                _, off, tail = item
                print(
                    "  ... {} trailing bytes at off=0x{:04x} "
                    "(partial frame at end)".format(len(tail), off)
                )

    if show_counts:
        print("")
        print("=== opcode counts ===")
        for name, n in sorted(opcode_counts.items(), key=lambda kv: -kv[1]):
            print("  {:24s} {}".format(name, n))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
