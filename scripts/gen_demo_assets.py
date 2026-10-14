#!/usr/bin/env python3
"""Generates the demo world: a tiled ground plane + a solid "character" cube,
packs them into data/game.bundle, and writes data/bootset.json (the byte-range
manifest, which prefetches only the bundle header at boot).

Layout mirrors the GTA port's data model: one archive + a manifest of byte ranges.
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
os.makedirs(DATA, exist_ok=True)


def pack_texture(w, h, rgba):
    return struct.pack("<4sIII", b"TEX1", w, h, 4) + bytes(rgba)


def checker_texture(size=64, tile=8):
    rgba = bytearray()
    for y in range(size):
        for x in range(size):
            c = 255 if ((x // tile) + (y // tile)) % 2 == 0 else 60
            rgba += bytes((c, c, c, 255))
    return pack_texture(size, size, rgba)


def solid_texture(color=(255, 140, 30), size=64):
    rgba = bytearray()
    for _ in range(size * size):
        rgba += bytes((color[0], color[1], color[2], 255))
    return pack_texture(size, size, rgba)


def pack_mesh(verts, indices):
    buf = struct.pack("<4sII", b"MESH", len(verts), len(indices))
    for v in verts:
        buf += struct.pack("<3f2f", *v)
    buf += struct.pack("<%dI" % len(indices), *indices)
    return buf


def ground_mesh(size=20.0, uv_tile=4.0):
    h = size / 2
    verts = [
        (-h, 0.0, -h, 0.0, 0.0),
        (h, 0.0, -h, uv_tile, 0.0),
        (h, 0.0, h, uv_tile, uv_tile),
        (-h, 0.0, h, 0.0, uv_tile),
    ]
    return pack_mesh(verts, [0, 1, 2, 0, 2, 3])


def cube_mesh(size=1.0, y_off=0.5):
    s = size / 2
    # 8 corners (position + uv; uv is irrelevant for a solid colour).
    # y_off lifts the cube so it sits ON the ground (y in [0, size]).
    corners = [
        (-s, -s, -s), (s, -s, -s), (s, s, -s), (-s, s, -s),
        (-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s),
    ]
    verts = [(c[0], c[1] + y_off, c[2], 0.0, 0.0) for c in corners]
    # 12 triangles (36 indices), wound CCW viewed from outside
    faces = [
        (4, 5, 6, 4, 6, 7),  # +Z
        (1, 0, 3, 1, 3, 2),  # -Z
        (0, 4, 7, 0, 7, 3),  # -X
        (5, 1, 2, 5, 2, 6),  # +X
        (3, 7, 6, 3, 6, 2),  # +Y
        (0, 1, 5, 0, 5, 4),  # -Y
    ]
    indices = [i for f in faces for i in f]
    return pack_mesh(verts, indices)


def main():
    ground_tex = checker_texture()
    char_tex = solid_texture()
    ground = ground_mesh()
    char = cube_mesh(size=1.0, y_off=0.0)

    entries = [
        ("ground.mesh", 1, ground),
        ("char.mesh", 1, char),
        ("ground.tex", 2, ground_tex),
        ("char.tex", 2, char_tex),
    ]

    # Header (rebuilt below with real offsets) + payloads.
    header_len = 5 + 4 + sum(2 + len(name) + 1 + 8 + 8 for name, _, _ in entries)
    cursor = header_len
    offsets = []
    for name, typ, payload in entries:
        offsets.append((name, typ, cursor, len(payload)))
        cursor += len(payload)

    header = b"BNDL1" + struct.pack("<I", len(entries))
    for name, typ, off, ln in offsets:
        header += struct.pack("<H", len(name)) + name.encode() + struct.pack("<BQQ", typ, off, ln)

    bundle = header + b"".join(p for _, _, p in entries)
    with open(os.path.join(DATA, "game.bundle"), "wb") as f:
        f.write(bundle)

    manifest = [["game.bundle", [[0, header_len]]]]
    with open(os.path.join(DATA, "bootset.json"), "w") as f:
        json.dump(manifest, f)

    print(f"game.bundle: {len(bundle)} bytes (header {header_len}), entries: "
          f"{[e[0] for e in entries]}")
    print(f"bootset.json: prefetch [[0, {header_len}]]")


if __name__ == "__main__":
    main()
