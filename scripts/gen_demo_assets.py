#!/usr/bin/env python3
"""Generates the demo assets: a checker texture, a textured quad mesh, packs them
into data/game.bundle, and writes data/bootset.json (the byte-range manifest).

This mirrors the GTA port's data layout: one archive file + a manifest that lists
which byte ranges to prefetch at boot (here: just the bundle header)."""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
os.makedirs(DATA, exist_ok=True)


def make_texture():
    w = h = 64
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            c = 255 if ((x // 8) + (y // 8)) % 2 == 0 else 60
            rgba += bytes((c, c, c, 255))
    return struct.pack("<4sIII", b"TEX1", w, h, 4) + bytes(rgba)


def make_mesh():
    # A unit quad in the XY plane, facing +Z, UV 0..1.
    verts = [
        (-0.8, -0.8, 0.0, 0.0, 0.0),
        (0.8, -0.8, 0.0, 1.0, 0.0),
        (0.8, 0.8, 0.0, 1.0, 1.0),
        (-0.8, 0.8, 0.0, 0.0, 1.0),
    ]
    idx = [0, 1, 2, 0, 2, 3]
    buf = struct.pack("<4sII", b"MESH", len(verts), len(idx))
    for v in verts:
        buf += struct.pack("<3f2f", *v)
    buf += struct.pack("<%dI" % len(idx), *idx)
    return buf


def main():
    tex = make_texture()
    mesh = make_mesh()

    # Build the bundle the same way tools/make_bundle does (so we don't need the
    # C++ tool just to generate the demo): header + payloads.
    entries = [
        ("quad.mesh", 1, mesh),
        ("checker.tex", 2, tex),
    ]
    header = b"BNDL1" + struct.pack("<I", len(entries))
    offsets = []
    cursor = 5 + 4 + sum(2 + len(name) + 1 + 8 + 8 for name, _, _ in entries)
    for name, typ, payload in entries:
        offsets.append((name, typ, cursor, len(payload)))
        cursor += len(payload)
    for name, typ, payload in entries:
        header += struct.pack("<H", len(name)) + name.encode() + struct.pack("<BQQ", typ, 0, len(payload))
    # patch real offsets
    header = b"BNDL1" + struct.pack("<I", len(entries))
    for name, typ, off, ln in offsets:
        header += struct.pack("<H", len(name)) + name.encode() + struct.pack("<BQQ", typ, off, ln)

    bundle = header + b"".join(p for _, _, p in entries)
    with open(os.path.join(DATA, "game.bundle"), "wb") as f:
        f.write(bundle)

    # Manifest: prefetch only the header range of the bundle.
    manifest = [["game.bundle", [[0, len(header)]]]]
    with open(os.path.join(DATA, "bootset.json"), "w") as f:
        json.dump(manifest, f)

    print(f"game.bundle: {len(bundle)} bytes (header {len(header)})")
    print("bootset.json: prefetch [[0, %d]]" % len(header))


if __name__ == "__main__":
    main()
