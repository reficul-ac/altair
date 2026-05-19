#!/usr/bin/env python3
"""Generate the bundled Animus Terrain 3D generic fixed-wing GLB."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
from dataclasses import dataclass

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = (
    REPO_ROOT / "tools/animus-qt/web/cesium/models/generic_fixed_wing_smooth.glb"
)
GENERATOR_NAME = "Altair Animus procedural RC aircraft generator"


@dataclass(frozen=True)
class Material:
    name: str
    color: tuple[float, float, float, float]
    roughness: float = 0.72
    metallic: float = 0.0
    alpha_mode: str = "OPAQUE"


MATERIALS = (
    Material("warm_white_foam_body", (0.92, 0.90, 0.84, 1.0)),
    Material("matte_charcoal_control_surfaces", (0.10, 0.11, 0.12, 1.0)),
    Material("clear_blue_canopy", (0.16, 0.36, 0.62, 0.72), roughness=0.25, alpha_mode="BLEND"),
    Material("safety_red_trim", (0.86, 0.08, 0.06, 1.0), roughness=0.55),
    Material("dark_propeller_and_skids", (0.025, 0.027, 0.03, 1.0), roughness=0.5),
    Material("brushed_spinner", (0.72, 0.74, 0.76, 1.0), roughness=0.32, metallic=0.15),
)


class MeshBuilder:
    def __init__(self) -> None:
        self.positions: list[tuple[float, float, float]] = []
        self.normals: list[tuple[float, float, float]] = []
        self.indices: list[int] = []

    def add_vertex(
        self, position: tuple[float, float, float], normal: tuple[float, float, float]
    ) -> int:
        self.positions.append(position)
        self.normals.append(normalize(normal))
        return len(self.positions) - 1

    def add_triangle(
        self,
        a: tuple[float, float, float],
        b: tuple[float, float, float],
        c: tuple[float, float, float],
        normal: tuple[float, float, float] | None = None,
    ) -> None:
        face_normal = normal if normal is not None else triangle_normal(a, b, c)
        ia = self.add_vertex(a, face_normal)
        ib = self.add_vertex(b, face_normal)
        ic = self.add_vertex(c, face_normal)
        self.indices.extend((ia, ib, ic))

    def add_quad(
        self,
        a: tuple[float, float, float],
        b: tuple[float, float, float],
        c: tuple[float, float, float],
        d: tuple[float, float, float],
        normal: tuple[float, float, float] | None = None,
    ) -> None:
        face_normal = normal if normal is not None else triangle_normal(a, b, c)
        self.add_triangle(a, b, c, face_normal)
        self.add_triangle(a, c, d, face_normal)

    def add_box(self, center: tuple[float, float, float], size: tuple[float, float, float]) -> None:
        cx, cy, cz = center
        sx, sy, sz = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
        x0, x1 = cx - sx, cx + sx
        y0, y1 = cy - sy, cy + sy
        z0, z1 = cz - sz, cz + sz
        self.add_quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1), (1, 0, 0))
        self.add_quad((x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (-1, 0, 0))
        self.add_quad((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1), (0, 1, 0))
        self.add_quad((x1, y0, z0), (x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (0, -1, 0))
        self.add_quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1))
        self.add_quad((x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0), (0, 0, -1))


def normalize(v: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length <= 1.0e-9:
        return (0.0, 0.0, 1.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def triangle_normal(
    a: tuple[float, float, float],
    b: tuple[float, float, float],
    c: tuple[float, float, float],
) -> tuple[float, float, float]:
    ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    return normalize((uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx))


def add_elliptical_fuselage(builder: MeshBuilder) -> None:
    stations = (
        (0.94, 0.00, 0.00),
        (0.72, 0.11, 0.10),
        (0.34, 0.18, 0.15),
        (-0.30, 0.20, 0.16),
        (-0.96, 0.15, 0.13),
        (-1.58, 0.08, 0.08),
        (-1.95, 0.03, 0.04),
    )
    segments = 18
    rings: list[list[tuple[float, float, float]]] = []
    for x, ry, rz in stations:
        ring = []
        for i in range(segments):
            angle = 2.0 * math.pi * i / segments
            ring.append((x, ry * math.cos(angle), rz * math.sin(angle)))
        rings.append(ring)

    for r in range(len(rings) - 1):
        for i in range(segments):
            j = (i + 1) % segments
            builder.add_quad(rings[r][i], rings[r + 1][i], rings[r + 1][j], rings[r][j])

    builder.add_triangle(rings[0][0], rings[0][6], rings[0][12], (1, 0, 0))
    builder.add_triangle(rings[-1][0], rings[-1][12], rings[-1][6], (-1, 0, 0))


def add_wing_panel(
    builder: MeshBuilder,
    y0: float,
    y1: float,
    root_le: float,
    root_te: float,
    tip_le: float,
    tip_te: float,
    z: float,
    thickness: float,
) -> None:
    z0 = z - thickness * 0.5
    z1 = z + thickness * 0.5
    top = ((root_le, y0, z1), (tip_le, y1, z1), (tip_te, y1, z1), (root_te, y0, z1))
    bot = ((root_le, y0, z0), (root_te, y0, z0), (tip_te, y1, z0), (tip_le, y1, z0))
    builder.add_quad(*top, normal=(0, 0, 1))
    builder.add_quad(*bot, normal=(0, 0, -1))
    builder.add_quad(top[0], top[1], bot[3], bot[0], normal=(0, -1 if y1 < y0 else 1, 0))
    builder.add_quad(top[1], bot[3], bot[2], top[2])
    builder.add_quad(top[3], top[2], bot[2], bot[1])
    builder.add_quad(top[0], top[3], bot[1], bot[0])


def add_main_wing(builder: MeshBuilder) -> None:
    add_wing_panel(builder, -0.15, -1.62, 0.18, -0.46, 0.04, -0.38, 0.035, 0.055)
    add_wing_panel(builder, 0.15, 1.62, 0.18, -0.46, 0.04, -0.38, 0.035, 0.055)


def add_tailplane(builder: MeshBuilder) -> None:
    add_wing_panel(builder, -0.08, -0.72, -1.48, -1.76, -1.53, -1.74, 0.095, 0.035)
    add_wing_panel(builder, 0.08, 0.72, -1.48, -1.76, -1.53, -1.74, 0.095, 0.035)


def add_vertical_stabilizer(builder: MeshBuilder) -> None:
    y0, y1 = -0.018, 0.018
    points = ((-1.55, 0.0, 0.14), (-1.78, 0.0, 0.62), (-1.91, 0.0, 0.15))
    left = tuple((x, y0, z) for x, _, z in points)
    right = tuple((x, y1, z) for x, _, z in points)
    builder.add_triangle(left[0], left[1], left[2], (0, -1, 0))
    builder.add_triangle(right[0], right[2], right[1], (0, 1, 0))
    builder.add_quad(left[0], right[0], right[1], left[1])
    builder.add_quad(left[1], right[1], right[2], left[2])
    builder.add_quad(left[2], right[2], right[0], left[0])


def add_canopy(builder: MeshBuilder) -> None:
    stations = ((0.48, 0.02, 0.0), (0.24, 0.13, 0.105), (-0.08, 0.11, 0.085))
    segments = 10
    rings: list[list[tuple[float, float, float]]] = []
    for x, half_width, height in stations:
        ring = []
        for i in range(segments + 1):
            angle = math.pi * i / segments
            y = half_width * math.cos(angle)
            z = height * math.sin(angle)
            ring.append((x, y, z))
        rings.append(ring)
    for r in range(len(rings) - 1):
        for i in range(segments):
            builder.add_quad(rings[r][i], rings[r + 1][i], rings[r + 1][i + 1], rings[r][i + 1])


def add_propeller(builder: MeshBuilder) -> None:
    builder.add_box((1.035, 0.0, 0.0), (0.025, 0.86, 0.055))
    builder.add_box((1.05, 0.0, 0.0), (0.028, 0.055, 0.62))


def add_spinner(builder: MeshBuilder) -> None:
    segments = 18
    base_x = 0.96
    tip_x = 1.10
    radius = 0.105
    center = (base_x, 0.0, 0.0)
    ring = []
    for i in range(segments):
        angle = 2.0 * math.pi * i / segments
        ring.append((base_x, radius * math.cos(angle), radius * math.sin(angle)))
    tip = (tip_x, 0.0, 0.0)
    for i in range(segments):
        j = (i + 1) % segments
        builder.add_triangle(ring[i], tip, ring[j])
        builder.add_triangle(center, ring[j], ring[i], (-1, 0, 0))


def add_landing_skids(builder: MeshBuilder) -> None:
    builder.add_box((-0.10, -0.22, -0.20), (1.05, 0.035, 0.035))
    builder.add_box((-0.10, 0.22, -0.20), (1.05, 0.035, 0.035))
    builder.add_box((0.20, -0.17, -0.09), (0.04, 0.035, 0.23))
    builder.add_box((0.20, 0.17, -0.09), (0.04, 0.035, 0.23))
    builder.add_box((-0.58, -0.17, -0.09), (0.04, 0.035, 0.20))
    builder.add_box((-0.58, 0.17, -0.09), (0.04, 0.035, 0.20))


def make_meshes() -> list[tuple[str, MeshBuilder, int]]:
    meshes: list[tuple[str, MeshBuilder, int]] = []

    def add(name: str, material: int, populate) -> None:
        builder = MeshBuilder()
        populate(builder)
        meshes.append((name, builder, material))

    add("fuselage_smooth_tapered_mesh", 0, add_elliptical_fuselage)
    add("main_wing_tapered_mesh", 0, add_main_wing)
    add("tailplane_mesh", 0, add_tailplane)
    add("vertical_stabilizer_mesh", 0, add_vertical_stabilizer)
    add("canopy_bubble_mesh", 2, add_canopy)
    add("propeller_cross_mesh", 4, add_propeller)
    add("spinner_mesh", 5, add_spinner)
    add("landing_skid_mesh", 4, add_landing_skids)
    add("wing_red_trim_mesh", 3, lambda b: b.add_box((-0.05, 0.0, 0.075), (0.035, 2.35, 0.018)))
    add("aileron_left_mesh", 1, lambda b: b.add_box((-0.15, -0.24, 0.0), (0.25, 0.48, 0.032)))
    add("aileron_right_mesh", 1, lambda b: b.add_box((-0.15, 0.24, 0.0), (0.25, 0.48, 0.032)))
    add("elevator_mesh", 1, lambda b: b.add_box((-0.12, 0.0, 0.0), (0.20, 1.12, 0.03)))
    add("rudder_mesh", 1, lambda b: b.add_box((-0.11, 0.0, 0.12), (0.19, 0.032, 0.24)))
    return meshes


def pack_accessor_data(builder: MeshBuilder) -> tuple[bytes, dict[str, tuple[int, int, int]]]:
    data = bytearray()
    offsets: dict[str, tuple[int, int, int]] = {}

    def align4() -> None:
        while len(data) % 4:
            data.append(0)

    align4()
    position_offset = len(data)
    for position in builder.positions:
        data.extend(struct.pack("<3f", *position))
    offsets["POSITION"] = (position_offset, len(builder.positions) * 12, len(builder.positions))

    align4()
    normal_offset = len(data)
    for normal in builder.normals:
        data.extend(struct.pack("<3f", *normal))
    offsets["NORMAL"] = (normal_offset, len(builder.normals) * 12, len(builder.normals))

    align4()
    index_offset = len(data)
    for index in builder.indices:
        data.extend(struct.pack("<H", index))
    offsets["INDICES"] = (index_offset, len(builder.indices) * 2, len(builder.indices))
    align4()
    return bytes(data), offsets


def bounds(values: list[tuple[float, float, float]]) -> tuple[list[float], list[float]]:
    mins = [min(v[i] for v in values) for i in range(3)]
    maxs = [max(v[i] for v in values) for i in range(3)]
    return mins, maxs


def build_glb() -> bytes:
    mesh_defs = make_meshes()
    binary = bytearray()
    buffer_views = []
    accessors = []
    meshes = []

    for name, builder, material_index in mesh_defs:
        mesh_blob, offsets = pack_accessor_data(builder)
        base_offset = len(binary)
        binary.extend(mesh_blob)

        position_offset, position_length, position_count = offsets["POSITION"]
        normal_offset, normal_length, normal_count = offsets["NORMAL"]
        index_offset, index_length, index_count = offsets["INDICES"]

        position_view = len(buffer_views)
        buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": base_offset + position_offset,
                "byteLength": position_length,
                "target": 34962,
            }
        )
        normal_view = len(buffer_views)
        buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": base_offset + normal_offset,
                "byteLength": normal_length,
                "target": 34962,
            }
        )
        index_view = len(buffer_views)
        buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": base_offset + index_offset,
                "byteLength": index_length,
                "target": 34963,
            }
        )

        mesh_min, mesh_max = bounds(builder.positions)
        position_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": position_view,
                "componentType": 5126,
                "count": position_count,
                "type": "VEC3",
                "min": mesh_min,
                "max": mesh_max,
            }
        )
        normal_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": normal_view,
                "componentType": 5126,
                "count": normal_count,
                "type": "VEC3",
            }
        )
        index_accessor = len(accessors)
        accessors.append(
            {
                "bufferView": index_view,
                "componentType": 5123,
                "count": index_count,
                "type": "SCALAR",
            }
        )
        meshes.append(
            {
                "name": name,
                "primitives": [
                    {
                        "attributes": {"POSITION": position_accessor, "NORMAL": normal_accessor},
                        "indices": index_accessor,
                        "material": material_index,
                    }
                ],
            }
        )

    materials = []
    for material in MATERIALS:
        entry = {
            "name": material.name,
            "pbrMetallicRoughness": {
                "baseColorFactor": list(material.color),
                "metallicFactor": material.metallic,
                "roughnessFactor": material.roughness,
            },
        }
        if material.alpha_mode != "OPAQUE":
            entry["alphaMode"] = material.alpha_mode
        materials.append(entry)

    nodes = [
        {"name": "fuselage", "mesh": 0},
        {"name": "main_wing", "mesh": 1},
        {"name": "horizontal_tail", "mesh": 2},
        {"name": "vertical_stabilizer", "mesh": 3},
        {"name": "canopy", "mesh": 4, "translation": (0.0, 0.0, 0.11)},
        {"name": "propeller", "mesh": 5},
        {"name": "spinner", "mesh": 6},
        {"name": "landing_skid", "mesh": 7},
        {"name": "red_wing_trim", "mesh": 8},
        {"name": "aileron_left_surface", "mesh": 9},
        {"name": "aileron_right_surface", "mesh": 10},
        {"name": "elevator_surface", "mesh": 11},
        {"name": "rudder_surface", "mesh": 12},
        {"name": "aileron_left_pivot", "children": [9], "translation": [-0.28, -1.42, 0.02]},
        {"name": "aileron_right_pivot", "children": [10], "translation": [-0.28, 1.42, 0.02]},
        {"name": "elevator_pivot", "children": [11], "translation": [-1.72, 0.0, 0.08]},
        {"name": "rudder_pivot", "children": [12], "translation": [-1.84, 0.0, 0.34]},
        {
            "name": "generic_fixed_wing_smooth_root",
            "children": [0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 16],
            "rotation": [0.0, 0.0, 0.7071067811865475, 0.7071067811865476],
        },
    ]

    gltf = {
        "asset": {
            "version": "2.0",
            "generator": GENERATOR_NAME,
            "copyright": "Original procedural Altair asset; no third-party artwork.",
            "extras": {
                "source": "tools/python/generate_animus_aircraft_model.py",
                "provenance": "original deterministic procedural geometry",
            },
        },
        "scene": 0,
        "scenes": [{"name": "generic_fixed_wing_smooth_scene", "nodes": [17]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "extras": {
            "altairComponentNames": [
                "fuselage",
                "main_wing",
                "horizontal_tail",
                "vertical_stabilizer",
                "canopy",
                "propeller",
                "spinner",
                "landing_skid",
                "aileron_left_surface",
                "aileron_right_surface",
                "elevator_surface",
                "rudder_surface",
            ]
        },
    }

    json_bytes = json.dumps(gltf, separators=(",", ":"), sort_keys=True).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    while len(binary) % 4:
        binary.append(0)

    total_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    header = struct.pack("<4sII", b"glTF", 2, total_length)
    json_chunk = struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
    bin_chunk = struct.pack("<II", len(binary), 0x004E4942) + bytes(binary)
    return header + json_chunk + bin_chunk


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=DEFAULT_OUTPUT,
        help="GLB path to write.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(build_glb())
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
