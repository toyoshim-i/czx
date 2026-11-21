#!/usr/bin/env python3
"""Generate a 128x128 RGBA PNG icon for CZX Emulator (zero external dependencies)."""

import pathlib
import struct
import zlib

W = 128
H = 128

FONT_C = [
    "011110",
    "110011",
    "110000",
    "110000",
    "110000",
    "110011",
    "011110",
]

FONT_Z = [
    "111111",
    "000011",
    "000110",
    "001100",
    "011000",
    "110000",
    "111111",
]

FONT_X = [
    "110011",
    "110011",
    "011110",
    "001100",
    "011110",
    "110011",
    "110011",
]


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def main() -> None:
    buf = bytearray(W * H * 4)

    def set_px(x: int, y: int, r: int, g: int, b: int, a: int = 255) -> None:
        if 0 <= x < W and 0 <= y < H:
            i = (y * W + x) * 4
            buf[i : i + 4] = bytes((r, g, b, a))

    # Rounded retro CRT bezel
    cx, cy = W / 2.0, H / 2.0
    for y in range(H):
        for x in range(W):
            dx = max(0.0, abs(x - cx + 0.5) - 44.0)
            dy = max(0.0, abs(y - cy + 0.5) - 44.0)
            dist = (dx * dx + dy * dy) ** 0.5
            if dist > 18.0:
                continue
            if dist > 15.0:
                set_px(x, y, 64, 210, 255)
            elif dist > 12.5:
                set_px(x, y, 26, 40, 62)
            else:
                # Subtle CRT scanline gradient inside screen
                shade = 22 if (y % 4) < 2 else 14
                set_px(x, y, shade, shade + 10, shade + 24)

    # Inner CRT screen frame
    for x in range(20, 108):
        set_px(x, 22, 48, 92, 132)
        set_px(x, 105, 48, 92, 132)
    for y in range(22, 106):
        set_px(20, y, 48, 92, 132)
        set_px(107, y, 48, 92, 132)

    # Top RGB retro stripe accent
    for x in range(24, 104):
        col = (255, 86, 92) if x < 50 else ((255, 210, 72) if x < 77 else (80, 240, 150))
        for y in range(28, 32):
            set_px(x, y, *col)

    # Draw "CZX" at 4x pixel scale
    glyphs = [
        (FONT_C, 25, 48, (96, 236, 255)),
        (FONT_Z, 52, 48, (128, 255, 172)),
        (FONT_X, 79, 48, (255, 226, 112)),
    ]
    scale = 4
    for glyph, gx, gy, (r, g, b) in glyphs:
        for row_idx, row in enumerate(glyph):
            for col_idx, ch in enumerate(row):
                if ch == "1":
                    for sy in range(scale):
                        for sx in range(scale):
                            set_px(gx + col_idx * scale + sx, gy + row_idx * scale + sy, r, g, b)

    # Bottom status LEDs
    for idx, col in enumerate(((255, 96, 96), (96, 255, 136), (96, 210, 255))):
        lx = 48 + idx * 14
        for dy in range(4):
            for dx in range(6):
                set_px(lx + dx, 92 + dy, *col)

    raw = bytearray()
    stride = W * 4
    for y in range(H):
        raw.append(0)
        raw.extend(buf[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + png_chunk(b"IEND", b"")

    out = pathlib.Path(__file__).resolve().parent.parent / "packaging" / "applaunch" / "czx.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
