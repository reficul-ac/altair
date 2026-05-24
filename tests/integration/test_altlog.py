#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path


def load_bridge(repo_root: Path):
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


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_altlog.py <repo-root> <tmp-dir>", file=sys.stderr)
        return 2
    repo_root = Path(sys.argv[1])
    tmp_dir = Path(sys.argv[2])
    tmp_dir.mkdir(parents=True, exist_ok=True)
    fixture = repo_root / "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv"
    altlog_dir = tmp_dir / "fixture.altlog"
    exported_csv = tmp_dir / "fixture.csv"
    summary_json = tmp_dir / "summary.json"

    run(
        [
            sys.executable,
            str(repo_root / "tools/python/capture_altlog.py"),
            "--csv",
            str(fixture),
            "--output",
            str(altlog_dir),
        ],
        repo_root,
    )
    run(
        [
            sys.executable,
            str(repo_root / "tools/python/replay_altlog.py"),
            "--input",
            str(altlog_dir),
            "--output-csv",
            str(exported_csv),
        ],
        repo_root,
    )
    run(
        [
            sys.executable,
            str(repo_root / "tools/python/compare_sitl_replay.py"),
            "--expected",
            str(fixture),
            "--actual",
            str(exported_csv),
            "--summary-json",
            str(tmp_dir / "diff-match.json"),
        ],
        repo_root,
    )
    run(
        [
            sys.executable,
            str(repo_root / "tools/python/summarize_altlog.py"),
            "--input",
            str(altlog_dir),
            "--output",
            str(summary_json),
        ],
        repo_root,
    )
    summary = json.loads(summary_json.read_text(encoding="utf-8"))
    if summary["recordCounts"].get("csv_sample") != 50 or summary["missingFields"]:
        print(f"unexpected summary: {summary}", file=sys.stderr)
        return 1

    bridge = load_bridge(repo_root)
    heartbeat = frame(bridge, 0, struct.pack("<IBBBBB", 0, 1, 0, 0, 4, 3), seq=7)
    records_path = altlog_dir / "records.jsonl"
    with records_path.open("a", encoding="utf-8") as handle:
        handle.write(
            json.dumps(
                {
                    "type": "mavlink_packet",
                    "timeS": 999.0,
                    "msgId": 0,
                    "messageName": "HEARTBEAT",
                    "systemId": 1,
                    "componentId": 1,
                    "rawFrameHex": heartbeat.hex(),
                },
                sort_keys=True,
                separators=(",", ":"),
            )
            + "\n"
        )
    tlog = tmp_dir / "capture.tlog"
    run(
        [
            sys.executable,
            str(repo_root / "tools/python/export_tlog.py"),
            "--input",
            str(altlog_dir),
            "--output",
            str(tlog),
        ],
        repo_root,
    )
    if tlog.read_bytes() != heartbeat:
        print("exported tlog bytes did not match captured frame", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
