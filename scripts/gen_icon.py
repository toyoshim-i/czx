#!/usr/bin/env python3
"""Generate a 128x128 RGBA PNG icon for CZX Emulator (zero external dependencies)."""

import pathlib
import struct
import zlib

BASE_SIZE = 128

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


def render_icon(size: int) -> bytes:
    if size <= 0 or size % BASE_SIZE != 0:
        raise ValueError(f"size must be a positive multiple of {BASE_SIZE}, got {size}")
    s = size // BASE_SIZE
    buf = bytearray(size * size * 4)

    def set_px(x: int, y: int, r: int, g: int, b: int, a: int = 255) -> None:
        if 0 <= x < size and 0 <= y < size:
            i = (y * size + x) * 4
            buf[i : i + 4] = bytes((r, g, b, a))

    def fill_scaled_px(bx: int, by: int, r: int, g: int, b: int, a: int = 255) -> None:
        for sy in range(s):
            for sx in range(s):
                set_px(bx * s + sx, by * s + sy, r, g, b, a)

    # Rounded retro CRT bezel
    cx, cy = size / 2.0, size / 2.0
    for y in range(size):
        by = y // s
        for x in range(size):
            dx = max(0.0, abs(x - cx + 0.5) - 44.0 * s)
            dy = max(0.0, abs(y - cy + 0.5) - 44.0 * s)
            dist = (dx * dx + dy * dy) ** 0.5
            if dist > 18.0 * s:
                continue
            if dist > 15.0 * s:
                set_px(x, y, 64, 210, 255)
            elif dist > 12.5 * s:
                set_px(x, y, 26, 40, 62)
            else:
                # Subtle CRT scanline gradient inside screen
                shade = 22 if (by % 4) < 2 else 14
                set_px(x, y, shade, shade + 10, shade + 24)

    # Inner CRT screen frame
    for bx in range(20, 108):
        fill_scaled_px(bx, 22, 48, 92, 132)
        fill_scaled_px(bx, 105, 48, 92, 132)
    for by in range(22, 106):
        fill_scaled_px(20, by, 48, 92, 132)
        fill_scaled_px(107, by, 48, 92, 132)

    # Top RGB retro stripe accent
    for bx in range(24, 104):
        col = (255, 86, 92) if bx < 50 else ((255, 210, 72) if bx < 77 else (80, 240, 150))
        for by in range(28, 32):
            fill_scaled_px(bx, by, *col)

    # Draw "CZX" at 4x base pixel scale
    glyphs = [
        (FONT_C, 25, 48, (96, 236, 255)),
        (FONT_Z, 52, 48, (128, 255, 172)),
        (FONT_X, 79, 48, (255, 226, 112)),
    ]
    gscale = 4 * s
    for glyph, gx, gy, (r, g, b) in glyphs:
        for row_idx, row in enumerate(glyph):
            for col_idx, ch in enumerate(row):
                if ch == "1":
                    for sy in range(gscale):
                        for sx in range(gscale):
                            set_px(gx * s + col_idx * gscale + sx, gy * s + row_idx * gscale + sy, r, g, b)

    # Bottom status LEDs
    for idx, col in enumerate(((255, 96, 96), (96, 255, 136), (96, 210, 255))):
        lx = 48 + idx * 14
        for dy in range(4):
            for dx in range(6):
                fill_scaled_px(lx + dx, 92 + dy, *col)

    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)
        raw.extend(buf[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    outputs = [
        (128, root / "packaging" / "applaunch" / "czx.png"),
        (256, root / "packaging" / "store" / "icon.png"),
    ]
    for size, out in outputs:
        png = render_icon(size)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(png)
        print(f"wrote {out} ({size}x{size}, {len(png)} bytes)")


if __name__ == "__main__":
    main()
