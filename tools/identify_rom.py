#!/usr/bin/env python3
"""Identify an N64 dump and check it against the revision this port targets.

Usage:  python tools/identify_rom.py path/to/rom.z64

Accepts big-endian (.z64), byte-swapped (.v64) and little-endian (.n64) dumps,
normalising to big-endian in memory before hashing. Exits non-zero if the dump
is not the supported revision.
"""
import hashlib
import struct
import sys

# ---- port config: the revision this port targets ----------------------------
# Fill in from your own dump (run this script on it once to print the values).
# Only "sha1" decides acceptance; the rest is reported for comparison.
TARGET = {
    "name": "Body Harvest (USA)",
    "cart": "NBHE",
    "size": 0xC00000,
    "entrypoint": 0x80000400,
    "crc1": 0x5326696F,
    "crc2": 0xFE9A99C3,
    "sha1": "bbb6666f5014a473747ee4145f036d9fb25d7348",
    "md5": "3b8585ed03e8ddb89d7de456317545e7",
}
# ----------------------------------------------------------------------------

MAGIC_BE = b"\x80\x37\x12\x40"   # .z64, native
MAGIC_BS = b"\x37\x80\x40\x12"   # .v64, byte-swapped pairs
MAGIC_LE = b"\x40\x12\x37\x80"   # .n64, word-reversed


def to_big_endian(data: bytes) -> tuple[bytes, str]:
    head = data[:4]
    if head == MAGIC_BE:
        return data, "big-endian (.z64)"
    if head == MAGIC_BS:
        b = bytearray(data)
        b[0::2], b[1::2] = data[1::2], data[0::2]
        return bytes(b), "byte-swapped (.v64) -> converted"
    if head == MAGIC_LE:
        out = bytearray(len(data))
        out[0::4] = data[3::4]
        out[1::4] = data[2::4]
        out[2::4] = data[1::4]
        out[3::4] = data[0::4]
        return bytes(out), "little-endian (.n64) -> converted"
    raise SystemExit(f"error: not an N64 ROM (magic {head.hex()})")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} path/to/rom.z64")

    raw = open(sys.argv[1], "rb").read()
    data, fmt = to_big_endian(raw)

    be = lambda off: struct.unpack_from(">I", data, off)[0]
    info = {
        "format": fmt,
        "size": len(data),
        "entrypoint": be(0x08),
        "crc1": be(0x10),
        "crc2": be(0x14),
        "name": data[0x20:0x34].decode("ascii", "replace").strip(),
        "cart": data[0x3B:0x3F].decode("ascii", "replace"),
        "version": data[0x3F],
        "sha1": hashlib.sha1(data).hexdigest(),
        "md5": hashlib.md5(data).hexdigest(),
    }

    print(f"  format      {info['format']}")
    print(f"  size        {info['size']} bytes ({info['size'] / 2**20:.0f} MB)")
    print(f"  title       {info['name']!r}")
    print(f"  cart id     {info['cart']}  (version {info['version']})")
    print(f"  entrypoint  0x{info['entrypoint']:08X}")
    print(f"  crc         0x{info['crc1']:08X} 0x{info['crc2']:08X}")
    print(f"  sha1        {info['sha1']}")
    print(f"  md5         {info['md5']}")

    if info["sha1"] == TARGET["sha1"]:
        print(f"\nOK: this is {TARGET['name']} -- the revision this port targets.")
        return 0

    print(f"\nNOT SUPPORTED: this port targets {TARGET['name']}.")
    print(f"  expected sha1  {TARGET['sha1']}")
    print(f"  got            {info['sha1']}")
    if info["entrypoint"] == TARGET["entrypoint"] and info["cart"][:-1] != TARGET["cart"][:-1]:
        print("  (a different region's cartridge -- the segment map will not match)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
