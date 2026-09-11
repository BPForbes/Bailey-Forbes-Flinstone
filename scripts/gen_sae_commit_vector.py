#!/usr/bin/env python3
"""Independent SAE group-19 Commit known-answer generator.

Uses only hashlib HMAC-SHA256 plus a pure-Python NIST P-256 implementation.
Not linked to kernel/core/net/net_wifi_sae.c — regenerate the embedded KAT
in fl_net_wifi_sae_dragonfly_selftest() when the documented inputs change.
"""
from __future__ import annotations

import hashlib
import hmac

# NIST P-256 (IEEE SAE group 19)
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

PASSWORD = b"vector-psk"
STA = bytes.fromhex("020000000001")
AP = bytes.fromhex("020000000002")
# Fixed secrets in (2, r-1); big-endian 32-byte integers 2 and 3.
RAND = (b"\x00" * 31) + b"\x02"
MASK = (b"\x00" * 31) + b"\x03"


def i2osp(val: int, length: int) -> bytes:
    return val.to_bytes(length, "big")


def os2ip(buf: bytes) -> int:
    return int.from_bytes(buf, "big")


def kdf_sha256(key: bytes, label: bytes, context: bytes, out_len: int) -> bytes:
    """IEEE 802.11 KDF-Hash-256: counter_le16 || Label || Context || length_bits_le16."""
    bits = (out_len * 8).to_bytes(2, "little")
    out = b""
    counter = 1
    while len(out) < out_len:
        msg = counter.to_bytes(2, "little") + label + context + bits
        out += hmac.new(key, msg, hashlib.sha256).digest()
        counter += 1
    return out[:out_len]


def point_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        s = (3 * x1 * x1 + A) * pow(2 * y1, P - 2, P) % P
    else:
        s = (y2 - y1) * pow(x2 - x1, P - 2, P) % P
    x3 = (s * s - x1 - x2) % P
    y3 = (s * (x1 - x3) - y1) % P
    return x3, y3


def point_mul(k: int, pt):
    acc = None
    q = pt
    while k:
        if k & 1:
            acc = point_add(acc, q)
        q = point_add(q, q)
        k >>= 1
    return acc


def hunt_and_peck_pwe(password: bytes, addr1: bytes, addr2: bytes):
    if addr1 > addr2:
        key = addr1 + addr2
    else:
        key = addr2 + addr1
    prime_bin = i2osp(P, 32)
    first = None
    for counter in range(1, 41):
        pwd_seed = hmac.new(key, password + bytes([counter]), hashlib.sha256).digest()
        pwd_value = os2ip(kdf_sha256(pwd_seed, b"SAE Hunting and Pecking", prime_bin, 32))
        if pwd_value >= P:
            continue
        y2 = (pow(pwd_value, 3, P) + A * pwd_value + B) % P
        if pow(y2, (P - 1) // 2, P) != 1:
            continue
        y = pow(y2, (P + 1) // 4, P)
        if (y & 1) != (pwd_seed[-1] & 1):
            y = P - y
        if first is None:
            first = (pwd_value, y)
    if first is None:
        raise RuntimeError("no PWE in 40 iterations")
    return first


def main() -> None:
    pwe = hunt_and_peck_pwe(PASSWORD, STA, AP)
    rand_i = os2ip(RAND)
    mask_i = os2ip(MASK)
    scalar = (rand_i + mask_i) % N
    masked = point_mul(mask_i, pwe)
    if masked is None:
        raise RuntimeError("mask*PWE is infinity")
    element = (masked[0], (P - masked[1]) % P)
    group = (19).to_bytes(2, "little")
    body = group + i2osp(scalar, 32) + i2osp(element[0], 32) + i2osp(element[1], 32)
    print("password", PASSWORD.decode())
    print("sta", STA.hex())
    print("ap", AP.hex())
    print("rand", RAND.hex())
    print("mask", MASK.hex())
    print("scalar", i2osp(scalar, 32).hex())
    print("element_x", i2osp(element[0], 32).hex())
    print("element_y", i2osp(element[1], 32).hex())
    print("commit", body.hex())


if __name__ == "__main__":
    main()
