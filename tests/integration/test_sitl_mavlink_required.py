#!/usr/bin/env python3

import importlib.util
import math
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

REQUIRED_IDS = {0, 24, 30, 33, 36, 42, 74, 136, 242}


def load_bridge(repo_root):
    path = repo_root / "tools/python/mavlink_live_bridge.py"
    spec = importlib.util.spec_from_file_location("mavlink_live_bridge", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["mavlink_live_bridge"] = module
    spec.loader.exec_module(module)
    return module


def finite(value) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(value)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_sitl_mavlink_required.py <repo-root> <build-root>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    build_root = Path(sys.argv[2])
    runner = build_root / "vehicle/sitl_runner"
    if not runner.exists():
        return 77

    bridge = load_bridge(repo_root)
    parser = bridge.MavlinkV1Parser()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(2.0)
    host, port = sock.getsockname()

    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "sitl.csv"
        proc = subprocess.Popen(
            [
                str(runner),
                "--scenario",
                "cruise6dof",
                "--initial",
                str(repo_root / "tests/integration/cruise6dof_initial.ini"),
                "--duration",
                "0.05",
                "--dt",
                "0.01",
                "--output",
                str(output),
                "--mavlink",
                "--mavlink-host",
                host,
                "--mavlink-port",
                str(port),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        seen = {}
        try:
            while proc.poll() is None or REQUIRED_IDS - set(seen):
                try:
                    data, _ = sock.recvfrom(4096)
                except socket.timeout:
                    if proc.poll() is not None:
                        break
                    continue
                for message in parser.feed(data):
                    seen[message.msg_id] = message
                if not (REQUIRED_IDS - set(seen)):
                    break
        finally:
            stdout, stderr = proc.communicate(timeout=5.0)
            sock.close()

    if proc.returncode != 0:
        print(stderr.decode("utf-8", errors="replace"), file=sys.stderr)
        return proc.returncode

    missing = REQUIRED_IDS - set(seen)
    if missing:
        print(f"missing MAVLink messages: {sorted(missing)}", file=sys.stderr)
        return 1

    snapshot = bridge.LiveSessionSnapshot(bridge.LiveVehicleState())
    snapshot.apply_datagram(seen.values(), now=1.0)
    payload = snapshot.to_jsonable(now=1.0)
    vehicle = payload["vehicles"][0]
    checks = [
        vehicle["globalPosition"]["latDeg"],
        vehicle["globalPosition"]["lonDeg"],
        vehicle["globalPosition"]["altitudeM"],
        vehicle["metrics"]["airspeedMps"],
        vehicle["metrics"]["groundspeedMps"],
        vehicle["terrain"]["currentHeightM"],
        vehicle["home"]["altitudeM"],
    ]
    if not all(finite(value) for value in checks):
        print("decoded MAVLink fields were not finite", file=sys.stderr)
        return 1
    if vehicle["status"]["gpsFix"] != 3 or vehicle["terrain"]["loaded"] != 1:
        print("decoded GPS or terrain fields were implausible", file=sys.stderr)
        return 1
    if vehicle["actuators"]["servoOutputsPwm"][0] is None:
        print("servo output was not decoded", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
