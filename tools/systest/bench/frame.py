#!/usr/bin/env python3
"""Decode an SWD dump of the global `frameBuffer` (Oled::OledBuffer, 0x41C bytes, same layout in base and remote) to an image.

Object layout (midicontroller-fw inc/oled.hpp, midicontrol-fw the same): 4-byte vtable pointer (gfx::IFrameBuffer), then
uint8_t buffer[STRIDE * PAGES] with STRIDE = 1 control byte (OLED_MEM_DAT 0x40) + WIDTH 130, PAGES 8. Pixel (x, y) is bit y%8 of
buffer[(y/8) * STRIDE + 1 + x] (OledBuffer::getPixel). The SH1106 column offset 2 is applied when the page is sent, not here.

usage: frame.py <samples.jsonl|frames.jsonl> <outdir>   -> one PNG per distinct frame + index.html contact sheet
"""
import html, json, os, struct, sys, zlib

WIDTH, HEIGHT, PAGES, CONTROL_BYTES = 130, 64, 8, 1
STRIDE = CONTROL_BYTES + WIDTH
OBJ_SIZE = 4 + STRIDE * PAGES  # 0x41C
SCALE = 3


def pixels(frame_hex):
    """frame hex (as swd.py 'words' fields store it: memory byte order) -> list of HEIGHT rows of WIDTH 0/1 values."""
    b = bytes.fromhex(frame_hex)
    if len(b) != OBJ_SIZE:
        raise ValueError(f"frame is {len(b)} bytes, expected {OBJ_SIZE}")
    buf = b[4:]
    bad = [p for p in range(PAGES) if buf[p * STRIDE] != 0x40]
    if bad:
        raise ValueError(f"control byte != 0x40 on pages {bad}: not an OledBuffer or torn read")
    return [[(buf[(y // 8) * STRIDE + CONTROL_BYTES + x] >> (y % 8)) & 1 for x in range(WIDTH)] for y in range(HEIGHT)]


def png(rows, scale=SCALE):
    """1-bit rows -> PNG bytes (greyscale 8-bit, white-on-black like the OLED, nearest-neighbour scaled)."""
    raw = bytearray()
    for r in rows:
        line = bytes(255 if v else 0 for v in r for _ in range(scale))
        for _ in range(scale):
            raw += b"\x00" + line
    w, h = len(rows[0]) * scale, len(rows) * scale

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def ascii_art(rows):
    """Two pixel rows per text line (for logs / quick looks in a terminal)."""
    ch = {(0, 0): " ", (1, 0): "▀", (0, 1): "▄", (1, 1): "█"}
    return "\n".join("".join(ch[(rows[y][x], rows[y + 1][x])] for x in range(WIDTH)) for y in range(0, HEIGHT, 2))


def contact_sheet(entries, outdir, title="frames"):
    """entries: list of dicts with png (file name), caption. Writes outdir/index.html."""
    cells = "\n".join(
        f'<figure><img src="{html.escape(e["png"])}" width="{WIDTH * 2}"><figcaption>{html.escape(e["caption"])}</figcaption></figure>'
        for e in entries)
    page = f"""<!doctype html><meta charset="utf-8"><title>{html.escape(title)}</title>
<style>body{{background:#222;color:#ddd;font:12px monospace}}figure{{display:inline-block;margin:6px}}img{{image-rendering:pixelated;border:1px solid #555}}</style>
<h1>{html.escape(title)}</h1>
{cells}
"""
    with open(os.path.join(outdir, "index.html"), "w") as f:
        f.write(page)


def render_jsonl(path, outdir):
    """Every sample/frame record with a 'frame' field -> PNG when it differs from the previous frame of the same unit."""
    os.makedirs(outdir, exist_ok=True)
    last, entries = {}, []
    with open(path) as f:
        for n, line in enumerate(f):
            try:
                d = json.loads(line)
            except ValueError:
                continue
            fr, unit = d.get("frame"), d.get("unit", "?")
            if not fr or last.get(unit) == fr:
                continue
            last[unit] = fr
            try:
                rows = pixels(fr)
            except ValueError as e:
                entries.append({"png": "", "caption": f"{unit} line {n}: {e}"})
                continue
            name = f"{unit}-{len(entries):04d}.png"
            with open(os.path.join(outdir, name), "wb") as o:
                o.write(png(rows))
            entries.append({"png": name, "caption": f'{unit} t={d.get("t")} {d.get("screen", "")}'})
    contact_sheet(entries, outdir, os.path.basename(path))
    return entries


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__); sys.exit(2)
    print(f"{len(render_jsonl(sys.argv[1], sys.argv[2]))} frames -> {sys.argv[2]}/index.html")
