#!/usr/bin/env python3
"""Generate the built-in low-poly Generic RC Plane GLB asset."""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "vehicles" / "generic_rc_plane" / "generic_rc_plane.glb"


def normal(
    a: tuple[float, float, float], b: tuple[float, float, float], c: tuple[float, float, float]
):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length == 0.0:
        return (0.0, 1.0, 0.0)
    return (nx / length, ny / length, nz / length)


def add_box(primitives, center, size, material):
    cx, cy, cz = center
    sx, sy, sz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
    corners = {
        "lbn": (cx - sx, cy - sy, cz - sz),
        "rbn": (cx + sx, cy - sy, cz - sz),
        "ltn": (cx - sx, cy + sy, cz - sz),
        "rtn": (cx + sx, cy + sy, cz - sz),
        "lbf": (cx - sx, cy - sy, cz + sz),
        "rbf": (cx + sx, cy - sy, cz + sz),
        "ltf": (cx - sx, cy + sy, cz + sz),
        "rtf": (cx + sx, cy + sy, cz + sz),
    }
    faces = [
        ("nose", ["lbn", "ltn", "rtn", "rbn"]),
        ("tail", ["lbf", "rbf", "rtf", "ltf"]),
        ("left", ["lbn", "lbf", "ltf", "ltn"]),
        ("right", ["rbn", "rtn", "rtf", "rbf"]),
        ("top", ["ltn", "ltf", "rtf", "rtn"]),
        ("bottom", ["lbn", "rbn", "rbf", "lbf"]),
    ]
    positions = []
    normals = []
    indices = []
    for _, names in faces:
        base = len(positions)
        verts = [corners[name] for name in names]
        n = normal(verts[0], verts[1], verts[2])
        positions.extend(verts)
        normals.extend([n] * 4)
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
    primitives.append((positions, normals, indices, material))


def add_disk(primitives, center, radius, half_width, material, segments=20):
    cx, cy, cz = center
    positions = []
    normals = []
    indices = []
    for side, z in enumerate((cz - half_width, cz + half_width)):
        base = len(positions)
        positions.append((cx, cy, z))
        normals.append((0.0, 0.0, -1.0 if side == 0 else 1.0))
        for i in range(segments):
            angle = 2.0 * math.pi * i / segments
            positions.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius, z))
            normals.append((0.0, 0.0, -1.0 if side == 0 else 1.0))
        for i in range(segments):
            a = base + 1 + i
            b = base + 1 + ((i + 1) % segments)
            if side == 0:
                indices.extend([base, b, a])
            else:
                indices.extend([base, a, b])
    primitives.append((positions, normals, indices, material))


def append_bytes(blob: bytearray, payload: bytes, alignment=4):
    offset = len(blob)
    blob.extend(payload)
    while len(blob) % alignment:
        blob.append(0)
    return offset, len(payload)


def accessor_min_max(positions):
    return (
        [min(p[i] for p in positions) for i in range(3)],
        [max(p[i] for p in positions) for i in range(3)],
    )


def main():
    material_body = 0
    material_wing = 1
    material_nose = 2
    material_prop = 3
    primitives = []
    add_box(primitives, (0.0, 0.0, 0.02), (0.16, 0.18, 1.15), material_body)
    add_box(primitives, (0.0, 0.0, -0.08), (1.55, 0.055, 0.26), material_wing)
    add_box(primitives, (0.0, 0.07, 0.53), (0.55, 0.045, 0.16), material_wing)
    add_box(primitives, (0.0, 0.18, 0.52), (0.08, 0.32, 0.16), material_wing)
    add_box(primitives, (0.0, 0.0, -0.63), (0.24, 0.21, 0.08), material_nose)
    add_disk(primitives, (0.0, 0.0, -0.70), 0.17, 0.008, material_prop)

    blob = bytearray()
    buffer_views = []
    accessors = []
    gltf_primitives = []
    for positions, normals, indices, material in primitives:
        pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
        norm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
        idx_bytes = b"".join(struct.pack("<H", i) for i in indices)

        pos_offset, pos_length = append_bytes(blob, pos_bytes)
        norm_offset, norm_length = append_bytes(blob, norm_bytes)
        idx_offset, idx_length = append_bytes(blob, idx_bytes)

        pos_view = len(buffer_views)
        buffer_views.append(
            {"buffer": 0, "byteOffset": pos_offset, "byteLength": pos_length, "target": 34962}
        )
        norm_view = len(buffer_views)
        buffer_views.append(
            {"buffer": 0, "byteOffset": norm_offset, "byteLength": norm_length, "target": 34962}
        )
        idx_view = len(buffer_views)
        buffer_views.append(
            {"buffer": 0, "byteOffset": idx_offset, "byteLength": idx_length, "target": 34963}
        )

        min_pos, max_pos = accessor_min_max(positions)
        pos_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": pos_view,
                "componentType": 5126,
                "count": len(positions),
                "type": "VEC3",
                "min": min_pos,
                "max": max_pos,
            }
        )
        norm_accessor = len(accessors)
        accessors.append(
            {"bufferView": norm_view, "componentType": 5126, "count": len(normals), "type": "VEC3"}
        )
        idx_accessor = len(accessors)
        accessors.append(
            {"bufferView": idx_view, "componentType": 5123, "count": len(indices), "type": "SCALAR"}
        )
        gltf_primitives.append(
            {
                "attributes": {"POSITION": pos_accessor, "NORMAL": norm_accessor},
                "indices": idx_accessor,
                "material": material,
                "mode": 4,
            }
        )

    gltf = {
        "asset": {"version": "2.0", "generator": "Animus generic RC plane generator"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Generic RC Plane"}],
        "meshes": [{"name": "Generic RC Plane Mesh", "primitives": gltf_primitives}],
        "materials": [
            {
                "name": "Body",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.82, 0.86, 0.88, 1.0],
                    "roughnessFactor": 0.8,
                },
            },
            {
                "name": "Wing",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.18, 0.48, 0.78, 1.0],
                    "roughnessFactor": 0.7,
                },
            },
            {
                "name": "Nose",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.92, 0.22, 0.16, 1.0],
                    "roughnessFactor": 0.65,
                },
            },
            {
                "name": "Prop Marker",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.08, 0.09, 0.10, 0.56],
                    "roughnessFactor": 0.9,
                },
            },
        ],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(json_chunk) % 4:
        json_chunk += b" "
    while len(blob) % 4:
        blob.append(0)

    total_length = 12 + 8 + len(json_chunk) + 8 + len(blob)
    glb = bytearray()
    glb.extend(struct.pack("<III", 0x46546C67, 2, total_length))
    glb.extend(struct.pack("<I4s", len(json_chunk), b"JSON"))
    glb.extend(json_chunk)
    glb.extend(struct.pack("<I4s", len(blob), b"BIN\x00"))
    glb.extend(blob)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(glb)
    print(f"wrote {OUT} ({len(glb)} bytes)")


if __name__ == "__main__":
    main()
