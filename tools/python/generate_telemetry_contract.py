#!/usr/bin/env python3
"""Generate Altair telemetry contract artifacts from one JSON source."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "title": "Altair Telemetry Contract",
    "type": "object",
    "required": [
        "schema_version",
        "owner",
        "contract_version",
        "field_states",
        "freshness_timeout_s",
        "sitl_live_mavlink",
        "embedded_bayek_envelope",
        "replay_log",
    ],
    "properties": {
        "schema_version": {"const": 1},
        "owner": {"const": "Altair"},
        "contract_version": {"type": "string"},
        "field_states": {
            "type": "array",
            "items": {"enum": ["fresh", "stale", "unsupported", "unknown"]},
            "minItems": 4,
        },
        "freshness_timeout_s": {"type": "number", "exclusiveMinimum": 0},
        "sitl_live_mavlink": {
            "type": "object",
            "required": ["transport", "required_messages", "deterministic_placeholders"],
        },
        "embedded_bayek_envelope": {
            "type": "object",
            "required": ["topic_id_floor", "required_topics"],
        },
        "replay_log": {"type": "object", "required": ["format", "source_doc", "required_groups"]},
    },
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_contract(source: Path) -> dict:
    return json.loads(source.read_text(encoding="utf-8"))


def validate_contract(contract: dict) -> None:
    required_top = set(SCHEMA["required"])
    missing = sorted(required_top - set(contract))
    if missing:
        raise ValueError(f"contract source missing top-level keys: {', '.join(missing)}")
    if contract["schema_version"] != 1 or contract["owner"] != "Altair":
        raise ValueError("contract source must be schema_version=1 and owner=Altair")
    if sorted(contract["field_states"]) != ["fresh", "stale", "unknown", "unsupported"]:
        raise ValueError("field_states must contain fresh, stale, unsupported, and unknown")
    if contract["freshness_timeout_s"] <= 0:
        raise ValueError("freshness_timeout_s must be positive")
    for section, entries_key in (
        ("sitl_live_mavlink", "required_messages"),
        ("embedded_bayek_envelope", "required_topics"),
    ):
        seen_names: set[str] = set()
        seen_ids: set[int] = set()
        for entry in contract[section][entries_key]:
            for key in ("id", "name", "group"):
                if key not in entry:
                    raise ValueError(f"{section}.{entries_key} entry missing {key}")
            if entry["name"] in seen_names or entry["id"] in seen_ids:
                raise ValueError(f"{section}.{entries_key} contains duplicate names or ids")
            seen_names.add(entry["name"])
            seen_ids.add(entry["id"])
    replay_columns = [
        column
        for columns in contract["replay_log"]["required_groups"].values()
        for column in columns
    ]
    duplicates = sorted({column for column in replay_columns if replay_columns.count(column) > 1})
    if duplicates:
        raise ValueError(f"replay columns appear in multiple groups: {', '.join(duplicates)}")


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return lines


def render_markdown(contract: dict) -> str:
    mavlink_rows = [
        [str(entry["id"]), entry["name"], entry["rate"], entry["group"]]
        for entry in contract["sitl_live_mavlink"]["required_messages"]
    ]
    topic_rows = [
        [str(entry["id"]), entry["name"], entry["group"]]
        for entry in contract["embedded_bayek_envelope"]["required_topics"]
    ]
    replay_rows = [
        [group, ", ".join(columns)]
        for group, columns in contract["replay_log"]["required_groups"].items()
    ]
    field_state_rows = [
        ["fresh", "A supported field was updated within the freshness timeout."],
        ["stale", "A supported field was last updated, but is older than the freshness timeout."],
        ["unsupported", "The current source contract does not provide this field."],
        ["unknown", "The source supports the field, but no sample has arrived yet."],
    ]

    lines = [
        "# Altair Telemetry Contract",
        "",
        "Altair owns the vehicle-specific telemetry contract for live SITL MAVLink, embedded",
        "Bayek-envelope topics, and deterministic replay logs. Bayek owns only the reusable",
        "packet envelope and topic-base rule documented in",
        "[`bayek/docs/telemetry.md`](../bayek/docs/telemetry.md).",
        "",
        "The machine-readable contract is [`telemetry_contract.json`](telemetry_contract.json).",
        "It is generated from [`telemetry_contract_source.json`](telemetry_contract_source.json)",
        "with [`tools/python/generate_telemetry_contract.py`](../tools/python/generate_telemetry_contract.py).",
        "",
        "## Field States",
        "",
        f"Freshness timeout: `{contract['freshness_timeout_s']:.1f} s`.",
        "",
        *markdown_table(["State", "Meaning"], field_state_rows),
        "",
        "## Live SITL MAVLink",
        "",
        "`vehicle/sitl_runner.c --scenario cruise6dof --mavlink` emits MAVLink v1 UDP",
        "packets. `--mavlink` preserves the existing realtime behavior and endpoint flags.",
        "",
        *markdown_table(["ID", "Message", "Rate", "Group"], mavlink_rows),
        "",
        "Deterministic placeholders are allowed only where the current plant has no real",
        "source. The generated JSON lists those placeholder values explicitly.",
        "",
        "## Embedded Bayek Envelope",
        "",
        "Embedded Altair telemetry must use Bayek's reusable packet envelope without",
        "teaching Bayek Altair schemas. Altair-specific topic IDs start at",
        "`TELEMETRY_TOPIC_VEHICLE_BASE`.",
        "",
        *markdown_table(["ID", "Topic", "Group"], topic_rows),
        "",
        "No board transport is introduced by this contract. A future board transport or",
        "binary logger must use these topic groups or update this contract and its tests.",
        "",
        "## Replay Log",
        "",
        "Deterministic replay remains the `cruise6dof` CSV v1 contract in",
        "[`simulation_and_mc.md`](simulation_and_mc.md#sitl-replay-csv-v1). The replay",
        "columns map into the same groups used by live and embedded telemetry.",
        "",
        *markdown_table(["Group", "Replay CSV Columns"], replay_rows),
        "",
        "Changing replay columns, order, names, or units requires updating the source",
        "contract, regenerating these artifacts, and updating the replay fixture checks.",
        "",
    ]
    return "\n".join(lines)


def write_generated(root: Path, check: bool) -> int:
    docs = root / "docs"
    source_path = docs / "telemetry_contract_source.json"
    contract = load_contract(source_path)
    validate_contract(contract)
    outputs = {
        docs / "telemetry_contract.json": json.dumps(contract, indent=2, sort_keys=False) + "\n",
        docs / "telemetry_contract.schema.json": json.dumps(SCHEMA, indent=2, sort_keys=True)
        + "\n",
        docs / "telemetry_contract.md": render_markdown(contract),
    }
    mismatched: list[Path] = []
    for path, content in outputs.items():
        if check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                mismatched.append(path)
        else:
            path.write_text(content, encoding="utf-8")
    if mismatched:
        for path in mismatched:
            print(f"out of date: {path}")
        return 1
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated files are current")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return write_generated(repo_root(), args.check)


if __name__ == "__main__":
    raise SystemExit(main())
