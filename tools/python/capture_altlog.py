#!/usr/bin/env python3
"""Capture SITL CSV and optional MAVLink UDP packets into `.altlog` v1."""

from __future__ import annotations

import argparse
import socket
import time
from pathlib import Path

from altlog import ALTLOG_SCHEMA_VERSION, csv_to_altlog_records, write_altlog
from mavlink_live_bridge import MAVLINK_MESSAGE_NAMES, MavlinkV1Parser


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_required_groups() -> dict:
    import json

    contract = json.loads(
        (repo_root() / "docs/telemetry_contract.json").read_text(encoding="utf-8")
    )
    return contract["replay_log"]["required_groups"]


def capture_udp(host: str, port: int, duration_s: float) -> list[dict]:
    parser = MavlinkV1Parser()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(0.1)
    start = time.monotonic()
    records: list[dict] = []
    try:
        while time.monotonic() - start < duration_s:
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            now = time.monotonic() - start
            for message in parser.feed(data):
                records.append(
                    {
                        "type": "mavlink_packet",
                        "timeS": now,
                        "msgId": message.msg_id,
                        "messageName": MAVLINK_MESSAGE_NAMES.get(
                            message.msg_id, f"MSG_{message.msg_id}"
                        ),
                        "systemId": message.system_id,
                        "componentId": message.component_id,
                        "rawFrameHex": message.raw_frame.hex(),
                    }
                )
    finally:
        sock.close()
    return records


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", help="SITL CSV source to import")
    parser.add_argument("--output", required=True, help="output .altlog directory or .zip")
    parser.add_argument("--include-source-csv", action="store_true")
    parser.add_argument("--mavlink-udp", metavar="HOST:PORT", help="capture MAVLink UDP packets")
    parser.add_argument("--duration", type=float, default=0.0, help="UDP capture duration seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.csv and not args.mavlink_udp:
        raise SystemExit("capture_altlog.py: provide --csv and/or --mavlink-udp")
    header: list[str] = []
    records: list[dict] = []
    extra_files: dict[str, Path] = {}
    source: dict[str, str | float | bool] = {}
    if args.csv:
        csv_path = Path(args.csv)
        header, csv_records = csv_to_altlog_records(csv_path)
        records.extend(csv_records)
        source["csv"] = str(csv_path)
        if args.include_source_csv:
            extra_files["source.csv"] = csv_path
    if args.mavlink_udp:
        if ":" not in args.mavlink_udp:
            raise SystemExit("capture_altlog.py: --mavlink-udp must be HOST:PORT")
        host, port_text = args.mavlink_udp.rsplit(":", 1)
        records.extend(capture_udp(host, int(port_text), args.duration))
        source["mavlink_udp"] = args.mavlink_udp
        source["duration_s"] = args.duration
    records.sort(key=lambda record: record["timeS"])
    manifest = {
        "schemaVersion": ALTLOG_SCHEMA_VERSION,
        "contractVersion": "altair-telemetry-contract-v1",
        "source": source,
        "recordFiles": ["records.jsonl"],
        "csvHeader": header,
        "requiredTelemetryGroups": load_required_groups(),
        "unsupportedFields": [],
        "startTimeS": records[0]["timeS"] if records else None,
        "endTimeS": records[-1]["timeS"] if records else None,
        "vehicleIds": sorted(
            {
                f"{record['systemId']}:{record['componentId']}"
                for record in records
                if record.get("type") == "mavlink_packet"
            }
        ),
    }
    write_altlog(args.output, manifest, records, extra_files)
    print(f"wrote {args.output}: records={len(records)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
