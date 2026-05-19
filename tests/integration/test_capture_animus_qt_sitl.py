#!/usr/bin/env python3

import importlib.util
import struct
import sys
import tempfile
import zlib
from pathlib import Path


def load_capture_module(repo_root):
    path = repo_root / "tools/python/capture_animus_qt_sitl.py"
    spec = importlib.util.spec_from_file_location("capture_animus_qt_sitl", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def png_chunk(kind, payload):
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_rgb_png(path, width, height, pixel_fn):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel_fn(x, y))
    payload = b"".join(
        (
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
            png_chunk(b"IDAT", zlib.compress(bytes(raw))),
            png_chunk(b"IEND", b""),
        )
    )
    path.write_bytes(payload)


def rich_pixel(x, y):
    if y < 13:
        return ((30 + x * 7) % 256, (50 + y * 13) % 256, (90 + x + y) % 256)
    return ((x * 5 + y * 3) % 256, (120 + x * 2) % 256, (40 + y * 4) % 256)


def alternate_pixel(x, y):
    if y < 13:
        return ((180 + x * 3) % 256, (30 + y * 17) % 256, (40 + x * 2) % 256)
    return ((220 - x * 2) % 256, (60 + y * 5) % 256, (180 + x + y * 2) % 256)


def terrain_workspace_pixel(x, y):
    if y < 13:
        return ((30 + x * 7) % 256, (50 + y * 13) % 256, (90 + x + y) % 256)
    if x < 46 and y > 57:
        if x < 2 or y < 59:
            return (15, 123, 67)
        if 5 <= x <= 20 and 61 <= y <= 64:
            return (15, 123, 67)
        return (247, 247, 243)
    return ((x * 5 + y * 3) % 256, (120 + x * 2) % 256, (40 + y * 4) % 256)


def expect(condition, message):
    if not condition:
        print(message, file=sys.stderr)
        return 1
    return 0


def main():
    if len(sys.argv) != 2:
        print("usage: test_capture_animus_qt_sitl.py <repo-root>", file=sys.stderr)
        return 2

    module = load_capture_module(Path(sys.argv[1]))
    status = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        rich_path = tmp_dir / "terrain-3d.png"
        blank_path = tmp_dir / "blank.png"
        small_path = tmp_dir / "small.png"
        alternate_path = tmp_dir / "setup.png"
        terrain_workspace_path = tmp_dir / "terrain-3d-workspace.png"
        write_rgb_png(rich_path, 128, 82, rich_pixel)
        write_rgb_png(blank_path, 128, 82, lambda _x, _y: (20, 20, 20))
        write_rgb_png(small_path, 64, 41, rich_pixel)
        write_rgb_png(alternate_path, 128, 82, alternate_pixel)
        write_rgb_png(terrain_workspace_path, 128, 82, terrain_workspace_pixel)

        rich = module.inspect_png(rich_path, "terrain-3d", expected_size=(128, 82))
        status |= expect(rich["ok"], f"expected rich fixture to pass, got {rich}")
        status |= expect(len(rich["diagnostics"]) == 2, "expected Cesium terrain diagnostics")

        workspace = module.inspect_png(
            terrain_workspace_path, "terrain-3d-workspace", expected_size=(128, 82)
        )
        status |= expect(
            workspace["ok"], f"expected terrain workspace fixture to pass, got {workspace}"
        )
        status |= expect(
            any(
                diagnostic["name"] == "clearance overlay panel"
                for diagnostic in workspace["diagnostics"]
            ),
            f"missing clearance overlay panel diagnostic: {workspace}",
        )

        blank = module.inspect_png(blank_path, "map-2d", expected_size=(128, 82))
        status |= expect(not blank["ok"], "expected blank fixture to fail")
        status |= expect(
            any("blank" in failure for failure in blank["failures"]),
            f"missing blank failure: {blank}",
        )

        wrong_size = module.inspect_png(small_path, "setup", expected_size=(128, 82))
        status |= expect(not wrong_size["ok"], "expected wrong dimensions to fail")
        status |= expect(
            any("unexpected dimensions" in failure for failure in wrong_size["failures"]),
            wrong_size,
        )

        captures = [
            {"workspace": "map-2d", "screenshot": str(rich_path), "png": {"ok": True}},
            {"workspace": "terrain-3d", "screenshot": str(rich_path), "png": {"ok": True}},
            {"workspace": "setup", "screenshot": str(alternate_path), "png": {"ok": True}},
        ]
        comparisons = module.compare_workspace_screenshots(captures)
        identical = [
            item for item in comparisons if item["workspaces"] == ["map-2d", "terrain-3d"]
        ][0]
        different = [item for item in comparisons if item["workspaces"] == ["map-2d", "setup"]][0]
        status |= expect(
            identical["status"] == "fail",
            f"expected identical workspace screenshots to fail: {identical}",
        )
        status |= expect(
            different["status"] == "pass",
            f"expected different workspace screenshots to pass: {different}",
        )

        diagnostic_path = tmp_dir / "terrain-3d-control-surfaces.json"
        diagnostic_path.write_text(
            """{
  "ok": true,
  "renderer": "cesium-webengine",
  "workspaceMode": "terrain-3d",
  "cameraMode": "chase",
  "freeRoamAvailable": true,
  "vehicleLocked": true,
  "profileId": "generic_fixed_wing_smooth",
  "profileAssetUri": "models/generic_fixed_wing_smooth.glb",
  "loadedModelUri": "models/generic_fixed_wing_smooth.glb",
  "modelMatchesProfileAsset": true,
  "profileLoaded": true,
  "modelLoaded": true,
  "surfaces": [
    {"id": "left_aileron", "node": "aileron_left_pivot", "resolved": true, "deflectionDeg": 12.0, "matrixChanged": true},
    {"id": "right_aileron", "node": "aileron_right_pivot", "resolved": true, "deflectionDeg": -12.0, "matrixChanged": true},
    {"id": "elevator", "node": "elevator_pivot", "resolved": true, "deflectionDeg": 10.0, "matrixChanged": true},
    {"id": "rudder", "node": "rudder_pivot", "resolved": true, "deflectionDeg": 14.0, "matrixChanged": true}
  ],
  "failures": []
}
""",
            encoding="utf-8",
        )
        diagnostic = module.inspect_control_surface_diagnostic(diagnostic_path, "terrain-3d")
        status |= expect(
            diagnostic["ok"], f"expected control-surface diagnostic to pass: {diagnostic}"
        )

        diagnostic_path.write_text(
            """{
  "ok": false,
  "renderer": "cesium-webengine",
  "workspaceMode": "terrain-3d",
  "cameraMode": "chase",
  "freeRoamAvailable": true,
  "vehicleLocked": true,
  "profileLoaded": true,
  "modelLoaded": true,
  "surfaces": [
    {"id": "left_aileron", "node": "aileron_left_pivot", "resolved": true, "deflectionDeg": 12.0, "matrixChanged": false},
    {"id": "right_aileron", "node": "aileron_right_pivot", "resolved": false, "deflectionDeg": -12.0, "matrixChanged": false},
    {"id": "elevator", "node": "elevator_pivot", "resolved": true, "deflectionDeg": 0.0, "matrixChanged": false},
    {"id": "rudder", "node": "rudder_pivot", "resolved": true, "deflectionDeg": 14.0, "matrixChanged": true}
  ],
  "failures": []
}
""",
            encoding="utf-8",
        )
        diagnostic = module.inspect_control_surface_diagnostic(diagnostic_path, "terrain-3d")
        status |= expect(not diagnostic["ok"], "expected unresolved/neutral surfaces to fail")
        status |= expect(
            any(
                "deflected matrix remained neutral" in failure for failure in diagnostic["failures"]
            ),
            f"missing neutral matrix failure: {diagnostic}",
        )

        tactical_surface_path = tmp_dir / "tactical-control-surfaces.json"
        tactical_surface_path.write_text(
            """{
  "ok": true,
  "renderer": "cesium-webengine",
  "workspaceMode": "tactical",
  "cameraMode": "tactical",
  "freeRoamAvailable": false,
  "vehicleLocked": true,
  "profileId": "generic_fixed_wing_smooth",
  "profileAssetUri": "models/generic_fixed_wing_smooth.glb",
  "loadedModelUri": "models/generic_fixed_wing_smooth.glb",
  "modelMatchesProfileAsset": true,
  "profileLoaded": true,
  "modelLoaded": true,
  "surfaces": [
    {"id": "left_aileron", "node": "aileron_left_pivot", "resolved": true, "polarity": 1.0, "deflectionDeg": 12.0, "matrixChanged": true},
    {"id": "right_aileron", "node": "aileron_right_pivot", "resolved": true, "polarity": -1.0, "deflectionDeg": -12.0, "matrixChanged": true},
    {"id": "elevator", "node": "elevator_pivot", "resolved": true, "polarity": 1.0, "deflectionDeg": 10.0, "matrixChanged": true},
    {"id": "rudder", "node": "rudder_pivot", "resolved": true, "polarity": 1.0, "deflectionDeg": 14.0, "matrixChanged": true}
  ],
  "failures": []
}
""",
            encoding="utf-8",
        )
        tactical_surface = module.inspect_control_surface_diagnostic(
            tactical_surface_path, "tactical"
        )
        status |= expect(
            tactical_surface["ok"],
            f"expected native tactical control-surface diagnostic to pass: {tactical_surface}",
        )
        tactical_surface_path.write_text(
            tactical_surface_path.read_text(encoding="utf-8")
            .replace('"renderer": "cesium-webengine"', '"renderer": "qml-fallback"')
            .replace('"modelMatchesProfileAsset": true', '"modelMatchesProfileAsset": false'),
            encoding="utf-8",
        )
        tactical_surface = module.inspect_control_surface_diagnostic(
            tactical_surface_path, "tactical"
        )
        status |= expect(
            not tactical_surface["ok"],
            "expected tactical fallback diagnostic to fail",
        )
        status |= expect(
            any("native cesium-webengine" in failure for failure in tactical_surface["failures"]),
            f"missing tactical fallback renderer failure: {tactical_surface}",
        )

        chrome_path = tmp_dir / "terrain-3d-chrome.json"
        chrome_path.write_text(
            """{
  "selectedWorkspace": "terrain-3d",
  "chrome": {"semanticallyVisible": true},
  "tabs": [
    {"label": "Map 2D", "semanticallyVisible": true, "enabled": true, "width": 80, "height": 32, "labelItem": {"label": "Map 2D", "semanticallyVisible": true, "width": 45, "height": 18}, "labelTextMatches": true, "labelInsideTab": true},
    {"label": "Terrain 3D", "semanticallyVisible": true, "enabled": true, "width": 100, "height": 32, "labelItem": {"label": "Terrain 3D", "semanticallyVisible": true, "width": 70, "height": 18}, "labelTextMatches": true, "labelInsideTab": true},
    {"label": "FPV", "semanticallyVisible": true, "enabled": true, "width": 62, "height": 32, "labelItem": {"label": "FPV", "semanticallyVisible": true, "width": 26, "height": 18}, "labelTextMatches": true, "labelInsideTab": true},
    {"label": "Tactical", "semanticallyVisible": true, "enabled": true, "width": 88, "height": 32, "labelItem": {"label": "Tactical", "semanticallyVisible": true, "width": 58, "height": 18}, "labelTextMatches": true, "labelInsideTab": true},
    {"label": "Setup", "semanticallyVisible": true, "enabled": true, "width": 70, "height": 32, "labelItem": {"label": "Setup", "semanticallyVisible": true, "width": 42, "height": 18}, "labelTextMatches": true, "labelInsideTab": true}
  ]
}
""",
            encoding="utf-8",
        )
        chrome = module.inspect_chrome_diagnostic(chrome_path, "terrain-3d")
        status |= expect(chrome["ok"], f"expected chrome diagnostic to pass: {chrome}")
        chrome_payload = chrome_path.read_text(encoding="utf-8")
        chrome_path.write_text(
            chrome_payload.replace('"labelInsideTab": true', '"labelInsideTab": false', 1),
            encoding="utf-8",
        )
        chrome = module.inspect_chrome_diagnostic(chrome_path, "terrain-3d")
        status |= expect(not chrome["ok"], "expected clipped tab text diagnostic to fail")
        status |= expect(
            any("outside the tab bounds" in failure for failure in chrome["failures"]),
            f"missing clipped tab text failure: {chrome}",
        )

        command = module.command_for_workspace(Path("animus_qt"), tmp_dir, "terrain-3d", 1200)
        status |= expect(
            "--verify-terrain-control-surfaces" in command,
            f"terrain command missing control-surface verification: {command}",
        )
        tactical_command = module.command_for_workspace(
            Path("animus_qt"), tmp_dir, "tactical", 1200
        )
        status |= expect(
            "--verify-terrain-control-surfaces" in tactical_command,
            f"tactical command missing control-surface verification: {tactical_command}",
        )
        fpv_command = module.command_for_workspace(Path("animus_qt"), tmp_dir, "fpv", 1200)
        status |= expect(
            "--verify-terrain-control-surfaces" in fpv_command,
            f"FPV command missing control-surface verification: {fpv_command}",
        )

        camera_path = tmp_dir / "tactical-camera.json"
        camera_path.write_text(
            """{
  "ok": true,
  "renderer": "cesium-webengine",
  "workspaceMode": "tactical",
  "mode": "tactical",
  "cameraMode": "tactical",
  "freeRoamAvailable": false,
  "vehicleLocked": true
}
""",
            encoding="utf-8",
        )
        camera = module.inspect_tactical_camera_diagnostic(camera_path)
        status |= expect(camera["ok"], f"expected tactical camera diagnostic to pass: {camera}")
        camera_path.write_text(
            camera_path.read_text(encoding="utf-8").replace(
                '"freeRoamAvailable": false', '"freeRoamAvailable": true'
            ),
            encoding="utf-8",
        )
        camera = module.inspect_tactical_camera_diagnostic(camera_path)
        status |= expect(not camera["ok"], "expected tactical free-roam diagnostic to fail")
        camera_path.write_text(
            camera_path.read_text(encoding="utf-8")
            .replace('"renderer": "cesium-webengine"', '"renderer": "qml-fallback"')
            .replace('"freeRoamAvailable": true', '"freeRoamAvailable": false'),
            encoding="utf-8",
        )
        camera = module.inspect_tactical_camera_diagnostic(camera_path)
        status |= expect(not camera["ok"], "expected tactical fallback camera to fail")

        fpv_camera_path = tmp_dir / "fpv-camera.json"
        fpv_camera_path.write_text(
            """{
  "ok": true,
  "renderer": "cesium-webengine",
  "workspaceMode": "fpv",
  "mode": "fpv",
  "cameraMode": "fpv",
  "freeRoamAvailable": false,
  "vehicleLocked": true,
  "terrainEnabled": true,
  "ownshipHidden": true,
  "fixedFovDeg": 70.0,
  "forwardHemisphereDot": 0.5,
  "forwardHemisphereCompliant": true
}
""",
            encoding="utf-8",
        )
        fpv_camera = module.inspect_fpv_camera_diagnostic(fpv_camera_path)
        status |= expect(fpv_camera["ok"], f"expected FPV camera diagnostic to pass: {fpv_camera}")
        fpv_camera_path.write_text(
            fpv_camera_path.read_text(encoding="utf-8").replace(
                '"forwardHemisphereCompliant": true', '"forwardHemisphereCompliant": false'
            ),
            encoding="utf-8",
        )
        fpv_camera = module.inspect_fpv_camera_diagnostic(fpv_camera_path)
        status |= expect(not fpv_camera["ok"], "expected FPV hemisphere diagnostic to fail")

        xcb_log = tmp_dir / "xcb-startup.log"
        xcb_log.write_text(
            "qt.qpa.xcb: could not connect to display :99\n"
            'Could not load the Qt platform plugin "xcb" in "" even though it was found.\n'
            "This application failed to start because no Qt platform plugin could be initialized.\n",
            encoding="utf-8",
        )
        status |= expect(
            module.xcb_startup_failure(xcb_log),
            "expected representative Qt/xcb startup failure log to be detected",
        )

        ordinary_log = tmp_dir / "ordinary-failure.log"
        ordinary_log.write_text(
            "capture timed out while waiting for screenshot\n", encoding="utf-8"
        )
        status |= expect(
            not module.xcb_startup_failure(ordinary_log),
            "expected ordinary capture failure log not to be detected as Qt/xcb startup",
        )

        base_env = {
            "PATH": "/usr/bin",
            "ALT_AIR_TEST": "preserve-me",
            "QTWEBENGINE_CHROMIUM_FLAGS": "--existing-flag",
        }
        offscreen = module.offscreen_env(base_env)
        status |= expect(
            offscreen["QT_QPA_PLATFORM"] == "offscreen",
            f"offscreen env did not set QT_QPA_PLATFORM: {offscreen}",
        )
        status |= expect(
            offscreen["QT_QUICK_BACKEND"] == "software",
            f"offscreen env did not set software Qt Quick backend: {offscreen}",
        )
        status |= expect(
            offscreen["QT_OPENGL"] == "software",
            f"offscreen env did not set software OpenGL: {offscreen}",
        )
        status |= expect(
            offscreen["QTWEBENGINE_CHROMIUM_FLAGS"] == "--existing-flag",
            f"offscreen env should preserve existing Chromium flags: {offscreen}",
        )
        status |= expect(
            offscreen["ALT_AIR_TEST"] == "preserve-me",
            f"offscreen env did not preserve unrelated base values: {offscreen}",
        )
        status |= expect(
            "QT_QPA_PLATFORM" not in base_env,
            f"offscreen env should not mutate the base environment: {base_env}",
        )

        default_offscreen = module.offscreen_env({})
        status |= expect(
            "--disable-gpu" in default_offscreen.get("QTWEBENGINE_CHROMIUM_FLAGS", ""),
            f"offscreen env missing default software Chromium flags: {default_offscreen}",
        )

        status |= expect(
            module.should_retry_offscreen("xvfb-run", xcb_log, strategies_len=1, attempt_index=1),
            "expected final xvfb-run Qt/xcb startup failure to request offscreen retry",
        )
        status |= expect(
            not module.should_retry_offscreen(
                "xvfb-run", ordinary_log, strategies_len=1, attempt_index=1
            ),
            "expected non-xcb failure not to request offscreen retry",
        )
        status |= expect(
            not module.should_retry_offscreen(
                "offscreen-retry", xcb_log, strategies_len=2, attempt_index=2
            ),
            "expected offscreen retry failure not to request another offscreen retry",
        )
        status |= expect(
            not module.should_retry_offscreen(
                "xvfb-run", xcb_log, strategies_len=2, attempt_index=1
            ),
            "expected non-final xvfb-run strategy not to request duplicate offscreen retry",
        )

    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())
