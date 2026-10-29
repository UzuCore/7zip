#!/usr/bin/env python3
"""Package the selected transparent artwork into Windows alpha icon resources.

Requires Pillow. This only slices/resamples the supplied atlas and converts the
file format; the PNG artwork is generated separately and is kept unchanged.
"""
from collections import deque
from pathlib import Path
import json
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Assets/UI/toolbar-color01-transparent.png"
OUTPUT = ROOT / "CPP/7zip/UI/FileManager/ToolbarColor01"
NAMES = ("Compress", "Extract", "Test", "Copy", "Move", "Delete", "Info")
SIZES = (24, 36, 48, 64)


def main_component_bounds(image):
    # Locate the opaque icon body, ignoring detached edge speckles when measuring.
    # The exported pixels retain the original alpha, including antialiasing.
    alpha = image.getchannel("A").tobytes()
    width, height = image.size
    seen = bytearray(width * height)
    best_count, best_box = 0, None
    for start, value in enumerate(alpha):
        if value < 192 or seen[start]:
            continue
        todo = deque([start]); seen[start] = 1
        count = 0; left = right = start % width; top = bottom = start // width
        while todo:
            pos = todo.popleft(); count += 1
            x, y = pos % width, pos // width
            left = min(left, x); right = max(right, x)
            top = min(top, y); bottom = max(bottom, y)
            adjacent = []
            if x: adjacent.append(pos - 1)
            if x + 1 < width: adjacent.append(pos + 1)
            if y: adjacent.append(pos - width)
            if y + 1 < height: adjacent.append(pos + width)
            for other in adjacent:
                if not seen[other] and alpha[other] >= 192:
                    seen[other] = 1; todo.append(other)
        if count > best_count:
            best_count, best_box = count, (left, top, right + 1, bottom + 1)
    if best_count < 512:
        raise ValueError("No sufficiently large opaque icon in the atlas cell")
    left, top, right, bottom = best_box
    return (max(0, left - 3), max(0, top - 3), min(width, right + 3), min(height, bottom + 3))


def main():
    atlas = Image.open(SOURCE).convert("RGBA")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    metadata = {"source": str(SOURCE.relative_to(ROOT)), "source_size": atlas.size, "sizes": SIZES, "icons": []}
    for index, name in enumerate(NAMES):
        column, row = index % 4, index // 4
        cell = (round(column * atlas.width / 4), round(row * atlas.height / 2),
                round((column + 1) * atlas.width / 4), round((row + 1) * atlas.height / 2))
        tile = atlas.crop(cell)
        bounds = main_component_bounds(tile)
        artwork = tile.crop(bounds)
        artwork.thumbnail((58, 58), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        canvas.alpha_composite(artwork, ((64 - artwork.width) // 2, (64 - artwork.height) // 2))
        canvas.save(OUTPUT / (name + ".ico"), format="ICO", sizes=[(size, size) for size in SIZES], bitmap_format="bmp")
        metadata["icons"].append({"name": name, "cell": cell, "body_bounds_in_cell": bounds})
        print("Packaged", name)
    (OUTPUT / "atlas-map.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
