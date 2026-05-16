#!/usr/bin/env python3

import importlib.util
import json
import socket
import struct
import sys
from pathlib import Path


def load_bridge(repo_root):
    path = repo_root / "tools/python/mavlink_live_bridge.py"
    spec = importlib.util.spec_from_file_location("mavlink_live_bridge", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["mavlink_live_bridge"] = module
    spec.loader.exec_module(module)
    return module


def frame(bridge, msg_id, payload, seq=1):
    header = bytes([0xFE, len(payload), seq, 1, 1, msg_id])
    crc = bridge.x25_crc(
        bytes([bridge.MAVLINK_CRC_EXTRA[msg_id]]), bridge.x25_crc(header[1:] + payload)
    )
    return header + payload + bytes([crc & 0xFF, crc >> 8])


def main():
    if len(sys.argv) != 2:
        print("usage: test_mavlink_live_bridge.py <repo-root>", file=sys.stderr)
        return 2

    bridge = load_bridge(Path(sys.argv[1]))
    args = bridge.parse_args(["--no-forward"])
    if args.forward != []:
        print("--no-forward did not disable default forwarding", file=sys.stderr)
        return 1

    parser = bridge.MavlinkV1Parser()
    state = bridge.LiveVehicleState()
    heartbeat = frame(bridge, 0, struct.pack("<IBBBBB", 0, 1, 0, 0, 4, 3), seq=7)
    attitude = frame(
        bridge, 30, struct.pack("<Iffffff", 100, 0.1, -0.2, 1.3, 0.01, 0.02, 0.03), seq=8
    )
    global_position = frame(
        bridge,
        33,
        struct.pack(
            "<IiiiihhhH",
            100,
            int(37.4275 * 1e7),
            int(-122.1697 * 1e7),
            151000,
            151000,
            1800,
            100,
            -20,
            13000,
        ),
        seq=9,
    )
    vfr_hud = frame(bridge, 74, struct.pack("<ffhHff", 18.5, 18.1, 130, 0, 151.0, 0.2), seq=10)

    for message in parser.feed(heartbeat + attitude + global_position + vfr_hud):
        state.apply(message, now=10.0)

    payload = state.to_jsonable(now=10.5)
    if payload["heartbeatAgeS"] != 0.5:
        print("heartbeat age was not decoded", file=sys.stderr)
        return 1
    if abs(payload["attitude"]["rollRad"] - 0.1) > 1e-6:
        print("attitude was not decoded", file=sys.stderr)
        return 1
    if abs(payload["globalPosition"]["latDeg"] - 37.4275) > 1e-7:
        print("global position was not decoded", file=sys.stderr)
        return 1
    if abs(payload["metrics"]["airspeedMps"] - 18.5) > 1e-6:
        print("VFR_HUD was not decoded", file=sys.stderr)
        return 1
    if payload["id"] != "1:1" or payload["vehicleType"] != "Fixed-wing":
        print("vehicle identity was not decoded", file=sys.stderr)
        return 1
    if len(payload["trail"]) != 1:
        print("vehicle trail was not populated", file=sys.stderr)
        return 1

    snapshot = bridge.LiveSessionSnapshot(bridge.LiveVehicleState())
    snapshot.apply_datagram(parser.feed(heartbeat + attitude + global_position + vfr_hud), now=20.0)
    snapshot_payload = snapshot.to_jsonable(now=20.5)
    message_names = {message["name"] for message in snapshot_payload["messages"]}
    if (
        snapshot_payload["type"] != "session_snapshot"
        or snapshot_payload["selectedVehicleId"] != "1:1"
    ):
        print("session snapshot identity was not populated", file=sys.stderr)
        return 1
    if not {"HEARTBEAT", "ATTITUDE", "GLOBAL_POSITION_INT", "VFR_HUD"}.issubset(message_names):
        print("session snapshot message summaries were not populated", file=sys.stderr)
        return 1
    attitude_summary = next(
        message for message in snapshot_payload["messages"] if message["name"] == "ATTITUDE"
    )
    if abs(attitude_summary["fields"]["rollRad"] - 0.1) > 1e-6:
        print("session snapshot message fields were not decoded", file=sys.stderr)
        return 1

    sink = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sink.bind(("127.0.0.1", 0))
    sink.settimeout(1.0)
    endpoint = sink.getsockname()
    forwarder = bridge.UdpForwarder([endpoint])
    broadcasts = []
    protocol = bridge.BridgeProtocol(
        bridge.MavlinkV1Parser(),
        bridge.LiveSessionSnapshot(bridge.LiveVehicleState()),
        forwarder,
        [broadcasts.append],
    )
    protocol.datagram_received(attitude, ("127.0.0.1", 14551))
    forwarded, _ = sink.recvfrom(512)
    forwarder.close()
    sink.close()

    if forwarded != attitude:
        print("forwarded packet did not match input packet", file=sys.stderr)
        return 1
    broadcast_types = [json.loads(payload)["type"] for payload in broadcasts]
    if broadcast_types != ["vehicle_state", "session_snapshot"]:
        print("bridge did not broadcast vehicle state and session snapshot", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
