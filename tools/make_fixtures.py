#!/usr/bin/env python3
"""Generates PNG fixtures for tests/test_main.cpp using only the Python stdlib.

Every fixture encodes the same procedural pattern, which the C++ tests recompute:
    sample(x, y, c) = (x*13 + y*29 + c*71 + (x*y >> 3)) & 0xFF
Scanline filters cycle through all five types (None/Sub/Up/Average/Paeth), and zlib level 9
produces dynamic-Huffman blocks, so the whole inflate + unfilter path is exercised.
"""
import struct, sys, zlib, os

def sample(x, y, c):
    return (x * 13 + y * 29 + c * 71 + ((x * y) >> 3)) & 0xFF

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

def filter_row(ft, cur, prev, bpp):
    out = bytearray()
    for i, v in enumerate(cur):
        a = cur[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        if ft == 0: p = 0
        elif ft == 1: p = a
        elif ft == 2: p = b
        elif ft == 3: p = (a + b) // 2
        else: p = paeth(a, b, c)
        out.append((v - p) & 0xFF)
    return bytes([ft]) + bytes(out)

def make_png(path, w, h, ctype, level=9, palette=None, trns=None):
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = bytearray()
    prev = bytes(w * ch)
    for y in range(h):
        if ctype == 3:
            cur = bytes(((x + y) % 4) for x in range(w))
        else:
            cur = bytes(sample(x, y, c) for x in range(w) for c in range(ch))
        raw += filter_row(y % 5, cur, prev, ch)
        prev = cur
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    if palette: data += chunk(b"PLTE", bytes(palette))
    if trns: data += chunk(b"tRNS", bytes(trns))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), level))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f: f.write(data)

out = sys.argv[1] if len(sys.argv) > 1 else "tests/data"
os.makedirs(out, exist_ok=True)
pal = [255, 0, 0,  0, 255, 0,  0, 0, 255,  255, 255, 0]
make_png(f"{out}/rgba.png", 200, 150, 6)
make_png(f"{out}/rgb.png", 97, 61, 2)
make_png(f"{out}/gray.png", 80, 40, 0)
make_png(f"{out}/graya.png", 50, 33, 4)
make_png(f"{out}/palette.png", 64, 16, 3, palette=pal, trns=[255, 128, 0])
make_png(f"{out}/stored.png", 40, 30, 6, level=0)
print("fixtures written to", out)
