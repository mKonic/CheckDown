"""Generate CheckDown app icons matching the Chrome extension style.
Blue rounded-square background with white down-arrow and baseline bar.
Outputs: resources/checkdown_16.png, 32, 48, 128, and resources/checkdown.ico
"""

import struct, zlib, io, os, math
from PIL import Image, ImageDraw

SIZES = [16, 32, 48, 128]
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resources")
os.makedirs(OUT_DIR, exist_ok=True)

BLUE = (0x33, 0x99, 0xFF, 255)
WHITE = (255, 255, 255, 255)


def draw_icon(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    pad = max(1, size // 16)
    r = max(2, size // 5)  # corner radius

    # Rounded-square background
    x0, y0, x1, y1 = pad, pad, size - pad - 1, size - pad - 1
    d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=BLUE)

    # Arrow geometry
    cx = size / 2
    top_y   = size * 0.20
    tip_y   = size * 0.62
    bar_y   = size * 0.76
    wing_w  = size * 0.28
    lw_stem = max(1, size // 12)
    lw_wing = max(1, size // 10)
    lw_bar  = max(1, size // 11)

    # Vertical stem
    d.line([(cx, top_y), (cx, tip_y)], fill=WHITE, width=lw_stem)

    # Arrowhead wings (45°)
    d.line([(cx, tip_y), (cx - wing_w, tip_y - wing_w)], fill=WHITE, width=lw_wing)
    d.line([(cx, tip_y), (cx + wing_w, tip_y - wing_w)], fill=WHITE, width=lw_wing)

    # Baseline bar
    bar_x0 = size * 0.22
    bar_x1 = size * 0.78
    d.line([(bar_x0, bar_y), (bar_x1, bar_y)], fill=WHITE, width=lw_bar)

    return img


# ── Save PNGs ────────────────────────────────────────────────────────────────
images = {}
for sz in SIZES:
    img = draw_icon(sz)
    out_path = os.path.join(OUT_DIR, f"checkdown_{sz}.png")
    img.save(out_path, "PNG")
    images[sz] = img
    print(f"  wrote {out_path}")

# ── Save .ico (multi-resolution) ─────────────────────────────────────────────
ico_sizes = [sz for sz in SIZES if sz <= 64]  # ICO supports up to 256 but keep small
ico_path = os.path.join(OUT_DIR, "checkdown.ico")
images[128].save(
    ico_path,
    format="ICO",
    sizes=[(sz, sz) for sz in ico_sizes],
    append_images=[images[sz] for sz in ico_sizes[1:]],
)
print(f"  wrote {ico_path}")

# Also save the 128px as a standalone for QRC
png128_path = os.path.join(OUT_DIR, "checkdown.png")
images[128].save(png128_path, "PNG")
print(f"  wrote {png128_path}")

print("Done.")
