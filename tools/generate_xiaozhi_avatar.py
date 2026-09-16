"""Generate the recolourable LVGL avatar used by the voice assistant page.

The source artwork is deliberately a small monochrome mask.  LVGL applies the
current dashboard accent colour at draw time, so the same asset works in all
themes without allocating a second full-colour bitmap in PSRAM.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


SIZE = 128
SCALE = 4


def artwork() -> Image.Image:
    canvas = Image.new("L", (SIZE * SCALE, SIZE * SCALE), 0)
    draw = ImageDraw.Draw(canvas)

    def xy(values):
        return tuple(round(value * SCALE) for value in values)

    def line(points, width):
        draw.line([xy(point) for point in points], fill=255, width=width * SCALE, joint="curve")

    def ellipse(box, fill=255, width=1):
        box = xy(box)
        if width == 1:
            draw.ellipse(box, fill=fill)
        else:
            draw.ellipse(box, outline=fill, width=width * SCALE)

    def rounded(box, radius, fill=255, width=1):
        box = xy(box)
        if width == 1:
            draw.rounded_rectangle(box, radius=radius * SCALE, fill=fill)
        else:
            draw.rounded_rectangle(box, radius=radius * SCALE, outline=fill, width=width * SCALE)

    # Antenna and two soft side pads give the mark a friendly robot silhouette.
    line([(64, 18), (64, 29)], 5)
    ellipse((58, 7, 70, 19))
    rounded((14, 53, 29, 75), 7)
    rounded((99, 53, 114, 75), 7)

    # Open face outline keeps the palette disc visible through the centre.
    rounded((25, 28, 103, 99), 22, width=6)
    ellipse((43, 52, 55, 64))
    ellipse((73, 52, 85, 64))
    draw.arc(xy((46, 65, 82, 89)), start=20, end=160, fill=255, width=5 * SCALE)

    # Sound waves make the artwork read as a listening assistant at a glance.
    draw.arc(xy((2, 35, 27, 93)), start=112, end=248, fill=255, width=4 * SCALE)
    draw.arc(xy((101, 35, 126, 93)), start=-68, end=68, fill=255, width=4 * SCALE)

    return canvas.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def write_c(image: Image.Image, output: Path) -> None:
    data = list(image.tobytes())
    lines = []
    for offset in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]) + ",")
    content = (
        '#include "xiaozhi_avatar.h"\n\n'
        '/* 128x128 anti-aliased alpha mask; LVGL recolours it per dashboard theme. */\n'
        'static const uint8_t xiaozhi_avatar_map[] = {\n'
        + "\n".join(lines)
        + "\n};\n\n"
        'const lv_image_dsc_t xiaozhi_avatar_image = {\n'
        '    .header.magic = LV_IMAGE_HEADER_MAGIC,\n'
        '    .header.cf = LV_COLOR_FORMAT_A8,\n'
        '    .header.flags = 0,\n'
        f'    .header.w = {SIZE},\n'
        f'    .header.h = {SIZE},\n'
        f'    .header.stride = {SIZE},\n'
        '    .data_size = sizeof(xiaozhi_avatar_map),\n'
        '    .data = xiaozhi_avatar_map,\n'
        '    .reserved = NULL,\n'
        '};\n'
    )
    output.write_text(content, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("display_board/main/xiaozhi_avatar.c"))
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    image = artwork()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_c(image, args.output)
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        image.save(args.preview)


if __name__ == "__main__":
    main()
