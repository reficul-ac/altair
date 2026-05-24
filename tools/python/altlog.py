#!/usr/bin/env python3
"""Shared helpers for Altair `.altlog` v1 bundles."""

from __future__ import annotations

import csv
import json
import shutil
import zipfile
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Iterable

ALTLOG_SCHEMA_VERSION = 1
RECORD_TYPES = {"csv_sample", "mavlink_packet", "vehicle_state", "event"}


@dataclass(frozen=True)
class AltlogBundle:
    manifest: dict
    records: list[dict]


def _json_dumps(value: dict) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _read_member(bundle_path: Path, member: str) -> str:
    if bundle_path.is_dir():
        return (bundle_path / member).read_text(encoding="utf-8")
    with zipfile.ZipFile(bundle_path) as archive:
        return archive.read(member).decode("utf-8")


def _member_exists(bundle_path: Path, member: str) -> bool:
    if bundle_path.is_dir():
        return (bundle_path / member).exists()
    with zipfile.ZipFile(bundle_path) as archive:
        return member in archive.namelist()


def load_altlog(path: str | Path) -> AltlogBundle:
    bundle_path = Path(path)
    manifest = json.loads(_read_member(bundle_path, "manifest.json"))
    validate_manifest(bundle_path, manifest)
    records: list[dict] = []
    for record_file in manifest["recordFiles"]:
        for line_number, line in enumerate(
            _read_member(bundle_path, record_file).splitlines(), start=1
        ):
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{record_file}:{line_number}: invalid JSONL record") from exc
    validate_records(records)
    return AltlogBundle(manifest=manifest, records=records)


def validate_manifest(bundle_path: Path, manifest: dict) -> None:
    if manifest.get("schemaVersion") != ALTLOG_SCHEMA_VERSION:
        raise ValueError("manifest schemaVersion must be 1")
    record_files = manifest.get("recordFiles")
    if not isinstance(record_files, list) or not record_files:
        raise ValueError("manifest recordFiles must be a non-empty list")
    for record_file in record_files:
        if not isinstance(record_file, str) or not _member_exists(bundle_path, record_file):
            raise ValueError(f"manifest references missing record file: {record_file}")


def validate_records(records: Iterable[dict]) -> None:
    last_time: float | None = None
    for index, record in enumerate(records):
        record_type = record.get("type")
        if record_type not in RECORD_TYPES:
            raise ValueError(f"record {index}: invalid record type {record_type!r}")
        timestamp = record.get("timeS")
        if not isinstance(timestamp, (int, float)):
            raise ValueError(f"record {index}: timeS must be numeric")
        if last_time is not None and timestamp < last_time:
            raise ValueError(f"record {index}: timestamps must be ordered")
        last_time = float(timestamp)
        if record_type == "csv_sample" and "values" not in record:
            raise ValueError(f"record {index}: csv_sample requires values")
        if record_type == "mavlink_packet" and "rawFrameHex" not in record:
            raise ValueError(f"record {index}: mavlink_packet requires rawFrameHex")


def _prepare_output(output: Path) -> Path:
    if output.exists():
        if output.is_dir():
            shutil.rmtree(output)
        else:
            output.unlink()
    if output.suffix == ".zip":
        return output
    output.mkdir(parents=True)
    return output


def write_altlog(
    output: str | Path,
    manifest: dict,
    records: Iterable[dict],
    extra_files: dict[str, Path] | None = None,
) -> None:
    output_path = _prepare_output(Path(output))
    records_text = "".join(_json_dumps(record) + "\n" for record in records)
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if output_path.suffix == ".zip":
        with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", manifest_text)
            archive.writestr("records.jsonl", records_text)
            for name, source in (extra_files or {}).items():
                archive.write(source, name)
        return
    (output_path / "manifest.json").write_text(manifest_text, encoding="utf-8")
    (output_path / "records.jsonl").write_text(records_text, encoding="utf-8")
    for name, source in (extra_files or {}).items():
        shutil.copyfile(source, output_path / name)


def csv_to_altlog_records(csv_path: str | Path) -> tuple[list[str], list[dict]]:
    path = Path(csv_path)
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        rows = list(reader)
    if len(rows) < 2:
        raise ValueError(f"{path}: CSV must include a header and at least one data row")
    header = rows[0]
    try:
        time_index = header.index("time_s")
    except ValueError as exc:
        raise ValueError(f"{path}: CSV header must include time_s") from exc
    records = [
        {
            "type": "csv_sample",
            "timeS": float(row[time_index]),
            "source": path.name,
            "values": row,
        }
        for row in rows[1:]
    ]
    validate_records(records)
    return header, records


def records_to_csv(bundle: AltlogBundle, output_csv: str | Path) -> None:
    header = bundle.manifest.get("csvHeader")
    if not header:
        raise ValueError("altlog manifest does not contain csvHeader")
    rows = [record["values"] for record in bundle.records if record.get("type") == "csv_sample"]
    with Path(output_csv).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def write_tlog(bundle: AltlogBundle, output_tlog: str | Path) -> int:
    count = 0
    with Path(output_tlog).open("wb") as handle:
        for record in bundle.records:
            if record.get("type") != "mavlink_packet":
                continue
            handle.write(bytes.fromhex(record["rawFrameHex"]))
            count += 1
    return count


def summarize(bundle: AltlogBundle) -> dict:
    counts: dict[str, int] = {}
    message_names: dict[str, int] = {}
    vehicle_ids: set[str] = set()
    times: list[float] = []
    for record in bundle.records:
        record_type = record["type"]
        counts[record_type] = counts.get(record_type, 0) + 1
        times.append(float(record["timeS"]))
        if record_type == "mavlink_packet":
            name = record.get("messageName", f"MSG_{record.get('msgId')}")
            message_names[name] = message_names.get(name, 0) + 1
            if "systemId" in record and "componentId" in record:
                vehicle_ids.add(f"{record['systemId']}:{record['componentId']}")
    required_groups = bundle.manifest.get("requiredTelemetryGroups", {})
    csv_header = set(bundle.manifest.get("csvHeader", []))
    missing_fields = {
        group: [field for field in fields if field not in csv_header]
        for group, fields in required_groups.items()
    }
    missing_fields = {group: fields for group, fields in missing_fields.items() if fields}
    return {
        "schemaVersion": ALTLOG_SCHEMA_VERSION,
        "recordCount": len(bundle.records),
        "recordCounts": dict(sorted(counts.items())),
        "startTimeS": min(times) if times else None,
        "endTimeS": max(times) if times else None,
        "vehicleIds": sorted(vehicle_ids),
        "mavlinkMessages": dict(sorted(message_names.items())),
        "requiredTelemetryGroups": sorted(required_groups),
        "missingFields": missing_fields,
        "unsupportedFields": bundle.manifest.get("unsupportedFields", []),
    }


def write_summary(bundle: AltlogBundle, output_json: str | Path) -> None:
    Path(output_json).write_text(
        json.dumps(summarize(bundle), indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def materialize_altlog(path: Path):
    if path.is_dir():
        yield path
        return
    with TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        with zipfile.ZipFile(path) as archive:
            archive.extractall(tmp_path)
        yield tmp_path
