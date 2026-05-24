#!/usr/bin/env python3

import json
import sys
from pathlib import Path

REQUIRED_MAVLINK = {
    "HEARTBEAT",
    "ATTITUDE",
    "GLOBAL_POSITION_INT",
    "GPS_RAW_INT",
    "VFR_HUD",
    "MISSION_CURRENT",
    "HOME_POSITION",
    "TERRAIN_REPORT",
    "SERVO_OUTPUT_RAW",
}

REQUIRED_EMBEDDED = {
    "altair.heartbeat_status",
    "altair.vehicle_state",
    "altair.actuator_outputs",
    "altair.navigation_mission_state",
    "altair.environment_terrain_state",
    "altair.health_fault_summary",
}

REQUIRED_REPLAY_GROUPS = {
    "heartbeat_status",
    "vehicle_state",
    "actuator_outputs",
    "navigation_mission_state",
    "environment_terrain_state",
    "health_fault_summary",
}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_telemetry_contract_manifest.py <repo-root>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    manifest = json.loads((root / "docs/telemetry_contract.json").read_text(encoding="utf-8"))
    source = json.loads((root / "docs/telemetry_contract_source.json").read_text(encoding="utf-8"))
    schema = json.loads((root / "docs/telemetry_contract.schema.json").read_text(encoding="utf-8"))
    if manifest != source:
        print("generated telemetry_contract.json is out of date", file=sys.stderr)
        return 1
    for key in schema["required"]:
        if key not in manifest:
            print(f"schema required key missing from manifest: {key}", file=sys.stderr)
            return 1
    if set(manifest["field_states"]) != {"fresh", "stale", "unsupported", "unknown"}:
        print("field-state values are incomplete", file=sys.stderr)
        return 1

    mavlink = {entry["name"] for entry in manifest["sitl_live_mavlink"]["required_messages"]}
    embedded = {entry["name"] for entry in manifest["embedded_bayek_envelope"]["required_topics"]}
    replay_groups = set(manifest["replay_log"]["required_groups"])

    missing_mavlink = REQUIRED_MAVLINK - mavlink
    missing_embedded = REQUIRED_EMBEDDED - embedded
    missing_replay = REQUIRED_REPLAY_GROUPS - replay_groups
    if missing_mavlink or missing_embedded or missing_replay:
        print(
            f"missing mavlink={sorted(missing_mavlink)} "
            f"embedded={sorted(missing_embedded)} replay={sorted(missing_replay)}",
            file=sys.stderr,
        )
        return 1

    topic_floor = manifest["embedded_bayek_envelope"]["topic_id_floor"]
    topic_ids = [entry["id"] for entry in manifest["embedded_bayek_envelope"]["required_topics"]]
    if topic_floor != "TELEMETRY_TOPIC_VEHICLE_BASE" or min(topic_ids) < 1000:
        print(
            "embedded topic IDs must stay at or above TELEMETRY_TOPIC_VEHICLE_BASE", file=sys.stderr
        )
        return 1

    fixture_header = (
        (root / "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv")
        .read_text(encoding="utf-8")
        .splitlines()[0]
        .split(",")
    )
    represented = {
        column
        for columns in manifest["replay_log"]["required_groups"].values()
        for column in columns
    }
    missing_columns = sorted(set(fixture_header) - represented)
    if missing_columns:
        print(
            f"replay CSV columns missing from telemetry contract: {missing_columns}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
