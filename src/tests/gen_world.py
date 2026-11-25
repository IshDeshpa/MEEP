#!/usr/bin/env python3
"""Generate the existing world binary format from a JSON sphere description."""

import json
import math
from pathlib import Path
import struct
import sys

ELEMENTS = {name: index for index, name in enumerate(
    ("air", "water", "earth", "lava", "block", "emitter", "void"))}
WORLDS = Path(__file__).resolve().parents[1] / "worlds"


def generate(config, filename):
    if not isinstance(config, dict):
        raise ValueError("root must be an object")
    dimensions = config.get("dimensions")
    if (not isinstance(dimensions, list) or len(dimensions) != 3 or
            any(type(v) is not int or v <= 0 for v in dimensions)):
        raise ValueError("dimensions must be three positive integers [x, y, z]")
    spheres = config.get("spheres")
    if not isinstance(spheres, list):
        raise ValueError("spheres must be an array")
    if not filename or Path(filename).name != filename or filename in (".", ".."):
        raise ValueError("output must be a filename without directories")
    n, m, k = dimensions
    cells = bytearray(n * m * k)
    for index, sphere in enumerate(spheres):
        if not isinstance(sphere, dict):
            raise ValueError(f"sphere {index}: expected an object")
        center, radius, element = sphere.get("center"), sphere.get("radius"), sphere.get("element")
        if (not isinstance(center, list) or len(center) != 3 or
                any(type(v) not in (int, float) or not math.isfinite(v) for v in center)):
            raise ValueError(f"sphere {index}: center must be three finite numbers")
        if type(radius) not in (int, float) or not math.isfinite(radius) or radius < 0:
            raise ValueError(f"sphere {index}: radius must be a finite nonnegative number")
        if not isinstance(element, str) or element.lower() not in ELEMENTS:
            raise ValueError(f"sphere {index}: unknown element {element!r}")
        x0, y0, z0 = center
        ranges = [range(max(0, math.ceil(c - radius)), min(size - 1, math.floor(c + radius)) + 1)
                  for c, size in zip(center, dimensions)]
        for x in ranges[0]:
            for y in ranges[1]:
                for z in ranges[2]:
                    if (x - x0) ** 2 + (y - y0) ** 2 + (z - z0) ** 2 <= radius ** 2:
                        cells[z + k * (y + m * x)] = ELEMENTS[element.lower()]
    WORLDS.mkdir(parents=True, exist_ok=True)
    output = WORLDS / filename
    with output.open("wb") as stream:
        stream.write(struct.pack("@NNN", n, m, k))
        stream.write(cells)
    print(f"Wrote {output}: {n}x{m}x{k} with {len(spheres)} spheres")


if __name__ == "__main__":
    try:
        args = sys.argv[1:]
        if args and args[0] == "--json" and len(args) in (2, 3):
            with open(args[1], encoding="utf-8") as stream:
                config = json.load(stream)
            filename = args[2] if len(args) == 3 else "world_spheres.bin"
        elif len(args) in (1, 2) and args[0].lower() in ELEMENTS:
            element = args[0].lower()
            config = {"dimensions": [5, 5, 5], "spheres": [
                {"center": [2, 2, 2], "radius": 2, "element": element}]}
            filename = args[1] if len(args) == 2 else f"world_{element}_sphere_5x5x5.bin"
        else:
            raise ValueError("usage: gen_world.py ELEMENT [OUTPUT] or --json INPUT [OUTPUT]")
        generate(config, filename)
    except (OSError, ValueError, OverflowError, MemoryError, struct.error) as error:
        print(f"Could not generate world: {error}", file=sys.stderr)
        sys.exit(1)
