"""Writes DayZ PAA textures from PIL images, without DayZ Tools' ImageToPAA.

Layout (matched against ImageToPAA 2026-11-03):
  u16 type          0xFF01 DXT1 (opaque) or 0xFF05 DXT5 (with alpha)
  tags              "GGAT" + reversed name + u32 length + data:
                    CGVA average colour (B, G, R, A bytes), CXAM max colour (ffffffff),
                    GALF alpha flag (1 = smooth alpha, DXT5 only), SFFO 16 u32 mip offsets
  u16 palette       0
  mips              u16 width, u16 height, u24 size, DXT data; full size down to 4x4.
                    Mips 256 px and wider are LZO1X-compressed (width | 0x8000), as ImageToPAA
                    does: ~10% on map textures, ~4x on mostly transparent drawings.
  trailer           6 zero bytes
Sides must be powers of two. DXT blocks come from Pillow's DDS encoder (Pillow 11+).
"""
import io
import struct

from PIL import Image

DDS_HEADER = 128


def _dxt(img, fmt):
    buf = io.BytesIO()
    img.save(buf, "DDS", pixel_format=fmt)
    return buf.getvalue()[DDS_HEADER:]


def lzo1x(data):
    """LZO1X-compatible compression (what PAA mips use), greedy matching, in plain Python.

    Stream rules (as the LZO1X decompressor reads them):
      literal run L: first in the stream and L <= 238 -> byte 17 + L; else L <= 3 after a match -> in
      that match's low 2 bits; else byte L - 3 (L <= 18), or 0, then a 0 byte per 255, then the rest - 18.
      match (length >= 3), M3 distance 1..16384: 0x20 | (len - 2), or 0x20 + zero bytes + rest when
      len - 2 > 31, then u16 ((dist - 1) << 2 | literals after). M4 distance 16385..49151:
      0x10 | (bit 14 of dist - 16384) << 3 | (len - 2), extended past 7 the same way, then
      u16 ((dist - 16384) & 0x3fff) << 2 | literals after. End: 0x11 0x00 0x00.
    Checked against python-lzo (LZO 2.10) and ImageToPAA decoding the result (2026-11-03)."""
    n = len(data)
    out = bytearray()
    table = {}
    lit_start = 0
    last_s = -1            # where the previous match's "literals after" bits are; -1 = no match yet
    first = True

    def literals(end):
        nonlocal first
        count = end - lit_start
        if count == 0:
            return
        if first and count <= 238:
            out.append(17 + count)
        elif count <= 3 and last_s >= 0:
            out[last_s] |= count
        elif count <= 18:
            out.append(count - 3)
        else:
            rest = count - 18
            out.append(0)
            while rest > 255:
                out.append(0)
                rest -= 255
            out.append(rest)
        out.extend(data[lit_start:end])
        first = False

    def length_bytes(head, rest):
        out.append(head)
        while rest > 255:
            out.append(0)
            rest -= 255
        out.append(rest)

    i = 0
    while i + 4 <= n:
        key = data[i:i + 4]
        cand = table.get(key)
        table[key] = i
        dist = i - cand if cand is not None else 0
        if not 0 < dist <= 49151:
            i += 1
            continue
        k = 4
        while i + k < n and data[cand + k] == data[i + k]:
            k += 1
        literals(i)
        ml = k - 2
        if dist <= 16384:
            if ml <= 31:
                out.append(0x20 | ml)
            else:
                length_bytes(0x20, ml - 31)
            code = (dist - 1) << 2
        else:
            d = dist - 16384
            h = ((d >> 14) & 1) << 3
            if ml <= 7:
                out.append(0x10 | h | ml)
            else:
                length_bytes(0x10 | h, ml - 7)
            code = (d & 0x3FFF) << 2
        out.append(code & 0xFF)
        out.append(code >> 8)
        last_s = len(out) - 2
        first = False
        i += k
        lit_start = i
    literals(n)
    out += b"\x11\x00\x00"
    return bytes(out)


def _pot(n):
    return n > 0 and n & (n - 1) == 0


def has_alpha(img):
    return img.mode == "RGBA" and img.getchannel("A").getextrema()[0] < 255


def encode(img, alpha=None):
    """PAA bytes for a PIL image with power-of-two sides. alpha: None = DXT5 only when it has
    transparent pixels."""
    img = img.convert("RGBA")
    if not (_pot(img.width) and _pot(img.height)):
        raise ValueError(f"PAA sides must be powers of two, not {img.width}x{img.height}")
    if alpha is None:
        alpha = has_alpha(img)
    fmt = "DXT5" if alpha else "DXT1"
    small = img.resize((1, 1), Image.BOX).getpixel((0, 0))
    tags = [(b"CGVA", bytes((small[2], small[1], small[0], small[3] if alpha else 255))),
            (b"CXAM", b"\xff\xff\xff\xff")]
    if alpha:
        tags.append((b"GALF", struct.pack("<I", 1)))
    mips, level = [], img
    while True:
        data = _dxt(level, fmt)
        width = level.width
        if width >= 256:                              # as ImageToPAA: big mips LZO-compressed
            packed = lzo1x(data)
            if len(packed) < len(data):
                data, width = packed, width | 0x8000
        mips.append(struct.pack("<HH", width, level.height) + len(data).to_bytes(3, "little") + data)
        if min(level.width, level.height) <= 4 or len(mips) == 16:
            break
        level = level.resize((max(1, level.width // 2), max(1, level.height // 2)), Image.LANCZOS)
    head = struct.pack("<H", 0xFF05 if alpha else 0xFF01)
    for name, data in tags:
        head += b"GGAT" + name + struct.pack("<I", len(data)) + data
    sffo_at = len(head) + 12
    head += b"GGAT" + b"SFFO" + struct.pack("<I", 64) + bytes(64)
    head += struct.pack("<H", 0)                       # no palette
    offsets, pos = [], len(head)
    for m in mips:
        offsets.append(pos)
        pos += len(m)
    head = bytearray(head)
    head[sffo_at:sffo_at + 64] = struct.pack("<16I", *(offsets + [0] * (16 - len(offsets))))
    return bytes(head) + b"".join(mips) + bytes(6)


def save(img, path, alpha=None):
    data = encode(img, alpha)
    with open(path, "wb") as f:
        f.write(data)
    return len(data)
