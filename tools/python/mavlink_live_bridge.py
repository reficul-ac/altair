#!/usr/bin/env python3
"""Bridge MAVLink UDP telemetry to QGroundControl and a browser WebSocket UI."""

from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import math
import socket
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
from altlog import ALTLOG_SCHEMA_VERSION, write_altlog

MAVLINK_V1_STX = 0xFE
MAVLINK_CRC_EXTRA = {
    0: 50,
    24: 24,
    30: 39,
    33: 104,
    36: 222,
    42: 28,
    44: 221,
    45: 232,
    47: 153,
    51: 196,
    73: 38,
    74: 20,
    76: 152,
    77: 143,
    136: 1,
    242: 104,
}

MAVLINK_MESSAGE_NAMES = {
    0: "HEARTBEAT",
    24: "GPS_RAW_INT",
    30: "ATTITUDE",
    33: "GLOBAL_POSITION_INT",
    36: "SERVO_OUTPUT_RAW",
    42: "MISSION_CURRENT",
    44: "MISSION_COUNT",
    45: "MISSION_CLEAR_ALL",
    47: "MISSION_ACK",
    51: "MISSION_REQUEST_INT",
    73: "MISSION_ITEM_INT",
    74: "VFR_HUD",
    76: "COMMAND_LONG",
    77: "COMMAND_ACK",
    136: "TERRAIN_REPORT",
    242: "HOME_POSITION",
}

MAV_TYPE_NAMES = {
    0: "Generic",
    1: "Fixed-wing",
    2: "Quadrotor",
    3: "Coaxial",
    4: "Helicopter",
    10: "Ground rover",
    12: "Submarine",
    13: "Hexarotor",
    14: "Octorotor",
    15: "Tricopter",
    19: "VTOL",
}


def x25_crc(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        tmp = byte ^ (crc & 0xFF)
        tmp ^= (tmp << 4) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc


@dataclass(frozen=True)
class MavlinkMessage:
    msg_id: int
    payload: bytes
    seq: int
    system_id: int
    component_id: int
    raw_frame: bytes = b""


class MavlinkV1Parser:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[MavlinkMessage]:
        self._buffer.extend(data)
        messages: list[MavlinkMessage] = []
        while True:
            try:
                start = self._buffer.index(MAVLINK_V1_STX)
            except ValueError:
                self._buffer.clear()
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < 8:
                break
            payload_len = self._buffer[1]
            frame_len = 6 + payload_len + 2
            if len(self._buffer) < frame_len:
                break
            frame = bytes(self._buffer[:frame_len])
            del self._buffer[:frame_len]
            msg_id = frame[5]
            expected_extra = MAVLINK_CRC_EXTRA.get(msg_id)
            if expected_extra is None:
                continue
            expected_crc = x25_crc(bytes([expected_extra]), x25_crc(frame[1 : 6 + payload_len]))
            actual_crc = frame[6 + payload_len] | (frame[7 + payload_len] << 8)
            if expected_crc != actual_crc:
                continue
            messages.append(
                MavlinkMessage(
                    msg_id=msg_id,
                    payload=frame[6 : 6 + payload_len],
                    seq=frame[2],
                    system_id=frame[3],
                    component_id=frame[4],
                    raw_frame=frame,
                )
            )
        return messages


def field_state(last_update_s: float | None, now: float, timeout_s: float = 2.0) -> str:
    if last_update_s is None:
        return "unknown"
    return "fresh" if now - last_update_s < timeout_s else "stale"


def _unpack(fmt: str, payload: bytes, min_len: int):
    if len(payload) < min_len:
        return None
    return struct.unpack_from(fmt, payload)


@dataclass
class LiveVehicleState:
    connected: bool = False
    last_packet_time: float | None = None
    last_heartbeat_time: float | None = None
    last_attitude_time: float | None = None
    last_position_time: float | None = None
    last_metrics_time: float | None = None
    last_status_time: float | None = None
    last_home_time: float | None = None
    last_terrain_time: float | None = None
    last_actuator_time: float | None = None
    system_id: int | None = None
    component_id: int | None = None
    roll_rad: float = 0.0
    pitch_rad: float = 0.0
    yaw_rad: float = 0.0
    rollspeed_rps: float = 0.0
    pitchspeed_rps: float = 0.0
    yawspeed_rps: float = 0.0
    lat_deg: float | None = None
    lon_deg: float | None = None
    altitude_m: float | None = None
    relative_altitude_m: float | None = None
    vel_n_mps: float = 0.0
    vel_e_mps: float = 0.0
    vel_d_mps: float = 0.0
    heading_deg: float | None = None
    airspeed_mps: float | None = None
    groundspeed_mps: float | None = None
    climb_mps: float | None = None
    throttle_pct: int | None = None
    gps_fix_type: int | None = None
    satellites_visible: int | None = None
    mission_seq: int | None = None
    home_lat_deg: float | None = None
    home_lon_deg: float | None = None
    home_altitude_m: float | None = None
    terrain_lat_deg: float | None = None
    terrain_lon_deg: float | None = None
    terrain_height_m: float | None = None
    terrain_current_height_m: float | None = None
    terrain_pending: int | None = None
    terrain_loaded: int | None = None
    servo_outputs_pwm: list[int | None] = field(default_factory=lambda: [None] * 16)
    vehicle_type: str | None = None
    autopilot: int | None = None
    base_mode: int | None = None
    custom_mode: int | None = None
    system_status: int | None = None
    armed: bool | None = None
    origin_lat_deg: float | None = None
    origin_lon_deg: float | None = None
    origin_altitude_m: float | None = None
    writable_animus: bool = False
    trail: list[dict[str, float]] = field(default_factory=list)

    def apply(self, message: MavlinkMessage, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        self.connected = True
        self.last_packet_time = now
        self.system_id = message.system_id
        self.component_id = message.component_id
        if message.msg_id == 0:
            self.last_heartbeat_time = now
            self.last_status_time = now
            decoded = _unpack("<IBBBBB", message.payload, 9)
            if decoded is not None:
                (
                    self.custom_mode,
                    vehicle_type,
                    self.autopilot,
                    self.base_mode,
                    self.system_status,
                    _mavlink_version,
                ) = decoded
                self.vehicle_type = MAV_TYPE_NAMES.get(vehicle_type, f"MAV type {vehicle_type}")
                self.armed = bool(self.base_mode & 0x80)
        elif message.msg_id == 24:
            self.last_status_time = now
            decoded = _unpack("<QiiiHHHHBB", message.payload, 30)
            if decoded is not None:
                _, lat, lon, alt, _eph, _epv, _vel, _cog, fix_type, satellites = decoded
                self.lat_deg = lat / 10000000.0
                self.lon_deg = lon / 10000000.0
                self.altitude_m = alt / 1000.0
                self.gps_fix_type = fix_type
                self.satellites_visible = None if satellites == 255 else satellites
        elif message.msg_id == 30:
            self.last_attitude_time = now
            decoded = _unpack("<Iffffff", message.payload, 28)
            if decoded is not None:
                (
                    _,
                    self.roll_rad,
                    self.pitch_rad,
                    self.yaw_rad,
                    self.rollspeed_rps,
                    self.pitchspeed_rps,
                    self.yawspeed_rps,
                ) = decoded
        elif message.msg_id == 33:
            self.last_position_time = now
            decoded = _unpack("<IiiiihhhH", message.payload, 28)
            if decoded is not None:
                _, lat, lon, alt, rel_alt, vx, vy, vz, hdg = decoded
                self.lat_deg = lat / 10000000.0
                self.lon_deg = lon / 10000000.0
                self.altitude_m = alt / 1000.0
                self.relative_altitude_m = rel_alt / 1000.0
                self.vel_n_mps = vx / 100.0
                self.vel_e_mps = vy / 100.0
                self.vel_d_mps = vz / 100.0
                self.heading_deg = None if hdg == 65535 else hdg / 100.0
                if self.origin_lat_deg is None:
                    self.origin_lat_deg = self.lat_deg
                    self.origin_lon_deg = self.lon_deg
                    self.origin_altitude_m = self.altitude_m
                self._append_trail(now)
        elif message.msg_id == 74:
            self.last_metrics_time = now
            decoded = _unpack("<ffhHff", message.payload, 20)
            if decoded is not None:
                (
                    self.airspeed_mps,
                    self.groundspeed_mps,
                    heading,
                    throttle,
                    self.altitude_m,
                    self.climb_mps,
                ) = decoded
                self.heading_deg = float(heading)
                self.throttle_pct = throttle
        elif message.msg_id == 36:
            self.last_actuator_time = now
            if len(message.payload) >= 21:
                channels = min(8, (len(message.payload) - 4) // 2)
                for index in range(channels):
                    self.servo_outputs_pwm[index] = struct.unpack_from(
                        "<H", message.payload, 4 + index * 2
                    )[0]
                if len(message.payload) >= 37:
                    for index in range(8, 16):
                        self.servo_outputs_pwm[index] = struct.unpack_from(
                            "<H", message.payload, 21 + (index - 8) * 2
                        )[0]
        elif message.msg_id == 42:
            self.last_status_time = now
            decoded = _unpack("<H", message.payload, 2)
            if decoded is not None:
                (self.mission_seq,) = decoded
        elif message.msg_id == 136:
            self.last_terrain_time = now
            decoded = _unpack("<iiHffHH", message.payload, 22)
            if decoded is not None:
                (
                    lat,
                    lon,
                    _spacing,
                    self.terrain_height_m,
                    self.terrain_current_height_m,
                    self.terrain_pending,
                    self.terrain_loaded,
                ) = decoded
                self.terrain_lat_deg = lat / 10000000.0
                self.terrain_lon_deg = lon / 10000000.0
        elif message.msg_id == 242:
            self.last_home_time = now
            decoded = _unpack("<iiifff", message.payload, 24)
            if decoded is not None:
                lat, lon, alt, _x, _y, _z = decoded
                self.home_lat_deg = lat / 10000000.0
                self.home_lon_deg = lon / 10000000.0
                self.home_altitude_m = alt / 1000.0

    def to_jsonable(self, now: float | None = None) -> dict:
        now = time.monotonic() if now is None else now
        packet_age = None if self.last_packet_time is None else now - self.last_packet_time
        heartbeat_age = None if self.last_heartbeat_time is None else now - self.last_heartbeat_time
        north_m, east_m, up_m = self.local_position()
        return {
            "type": "vehicle_state",
            "connected": self.connected and (packet_age is None or packet_age < 2.0),
            "packetAgeS": packet_age,
            "heartbeatAgeS": heartbeat_age,
            "systemId": self.system_id,
            "componentId": self.component_id,
            "id": (
                f"{self.system_id}:{self.component_id}"
                if self.system_id is not None and self.component_id is not None
                else None
            ),
            "vehicleType": self.vehicle_type,
            "attitude": {
                "rollRad": self.roll_rad,
                "pitchRad": self.pitch_rad,
                "yawRad": self.yaw_rad,
                "rollRateRps": self.rollspeed_rps,
                "pitchRateRps": self.pitchspeed_rps,
                "yawRateRps": self.yawspeed_rps,
            },
            "globalPosition": {
                "latDeg": self.lat_deg,
                "lonDeg": self.lon_deg,
                "altitudeM": self.altitude_m,
                "relativeAltitudeM": self.relative_altitude_m,
                "originLatDeg": self.origin_lat_deg,
                "originLonDeg": self.origin_lon_deg,
                "originAltitudeM": self.origin_altitude_m,
            },
            "localPosition": {"northM": north_m, "eastM": east_m, "upM": up_m},
            "velocity": {
                "northMps": self.vel_n_mps,
                "eastMps": self.vel_e_mps,
                "downMps": self.vel_d_mps,
            },
            "metrics": {
                "headingDeg": self.heading_deg,
                "airspeedMps": self.airspeed_mps,
                "groundspeedMps": self.groundspeed_mps,
                "climbMps": self.climb_mps,
                "throttlePct": self.throttle_pct,
            },
            "status": {
                "armed": self.armed,
                "mode": None,
                "baseMode": self.base_mode,
                "customMode": self.custom_mode,
                "systemStatus": self.system_status,
                "gpsFix": self.gps_fix_type,
                "satellitesVisible": self.satellites_visible,
                "batteryRemainingPct": None,
                "batteryVoltageV": None,
                "onboardControlSensorsHealth": None,
                "missionSeq": self.mission_seq,
                "lastStatusText": None,
            },
            "home": {
                "latDeg": self.home_lat_deg,
                "lonDeg": self.home_lon_deg,
                "altitudeM": self.home_altitude_m,
            },
            "terrain": {
                "latDeg": self.terrain_lat_deg,
                "lonDeg": self.terrain_lon_deg,
                "terrainHeightM": self.terrain_height_m,
                "currentHeightM": self.terrain_current_height_m,
                "pending": self.terrain_pending,
                "loaded": self.terrain_loaded,
            },
            "actuators": {"servoOutputsPwm": self.servo_outputs_pwm},
            "fieldStates": {
                "attitude": field_state(self.last_attitude_time, now),
                "globalPosition": field_state(self.last_position_time, now),
                "localPosition": field_state(self.last_position_time, now),
                "velocity": field_state(self.last_position_time, now),
                "metrics": field_state(self.last_metrics_time, now),
                "status": field_state(self.last_status_time, now),
                "home": field_state(self.last_home_time, now),
                "terrain": field_state(self.last_terrain_time, now),
                "actuators": field_state(self.last_actuator_time, now),
                "battery": "unsupported",
            },
            "trail": self.trail,
            "commandCapabilities": {
                "liveLink": self.connected and (packet_age is None or packet_age < 2.0),
                "writableLink": self.writable_animus,
                "authority": "sitl-writable" if self.writable_animus else "read-only",
                "stale": packet_age is not None and packet_age >= 2.0,
                "supported": (
                    [
                        "arm",
                        "disarm",
                        "emergency-stop",
                        "takeoff",
                        "land",
                        "return-to-launch",
                        "pause",
                        "change-altitude",
                        "mission-start",
                        "mission-continue",
                        "mission-resume",
                    ]
                    if self.writable_animus
                    and self.connected
                    and (packet_age is None or packet_age < 2.0)
                    else []
                ),
                "blockedReason": (
                    None
                    if self.writable_animus
                    and self.connected
                    and (packet_age is None or packet_age < 2.0)
                    else "Animus writes require a SITL session started with --writable-animus."
                ),
            },
        }

    def local_position(self) -> tuple[float | None, float | None, float | None]:
        if self.lat_deg is None or self.lon_deg is None or self.altitude_m is None:
            return None, None, None
        if (
            self.origin_lat_deg is None
            or self.origin_lon_deg is None
            or self.origin_altitude_m is None
        ):
            return None, None, None
        earth_radius_m = 6378137.0
        lat0_rad = math.radians(self.origin_lat_deg)
        north_m = math.radians(self.lat_deg - self.origin_lat_deg) * earth_radius_m
        east_m = (
            math.radians(self.lon_deg - self.origin_lon_deg) * earth_radius_m * math.cos(lat0_rad)
        )
        up_m = self.altitude_m - self.origin_altitude_m
        return north_m, east_m, up_m

    def _append_trail(self, now: float) -> None:
        north_m, east_m, up_m = self.local_position()
        if north_m is None or east_m is None or up_m is None:
            return
        point = {"eastM": east_m, "northM": north_m, "upM": up_m, "timestampS": now}
        if self.trail:
            last = self.trail[-1]
            distance_m = math.hypot(east_m - last["eastM"], north_m - last["northM"])
            if distance_m < 0.05 and abs(up_m - last["upM"]) < 0.05:
                return
        self.trail.append(point)
        if len(self.trail) > 900:
            del self.trail[: len(self.trail) - 900]


@dataclass
class MessageSummary:
    key: str
    msg_id: int
    name: str
    system_id: int
    component_id: int
    first_seen_s: float
    last_seen_s: float
    count: int = 0
    fields: dict[str, float | int | str | None] = field(default_factory=dict)

    def apply(
        self, message: MavlinkMessage, fields: dict[str, float | int | str | None], now: float
    ) -> None:
        self.last_seen_s = now
        self.count += 1
        self.fields = fields

    def to_jsonable(self, now: float) -> dict:
        elapsed_s = max(0.001, self.last_seen_s - self.first_seen_s)
        return {
            "key": self.key,
            "msgId": self.msg_id,
            "name": self.name,
            "systemId": self.system_id,
            "componentId": self.component_id,
            "lastAgeS": now - self.last_seen_s,
            "rateHz": self.count / elapsed_s,
            "count": self.count,
            "fields": self.fields,
        }


@dataclass
class LiveSessionSnapshot:
    state: LiveVehicleState
    messages: dict[str, MessageSummary] = field(default_factory=dict)
    events: list[dict] = field(default_factory=list)
    packet_count: int = 0
    decoded_count: int = 0

    def apply_datagram(self, messages: Iterable[MavlinkMessage], now: float) -> None:
        self.packet_count += 1
        for message in messages:
            self.decoded_count += 1
            self.state.apply(message, now)
            fields = decode_message_fields(message)
            key = f"{message.system_id}:{message.component_id}:{message.msg_id}"
            summary = self.messages.get(key)
            if summary is None:
                summary = MessageSummary(
                    key=key,
                    msg_id=message.msg_id,
                    name=MAVLINK_MESSAGE_NAMES.get(message.msg_id, f"MSG_{message.msg_id}"),
                    system_id=message.system_id,
                    component_id=message.component_id,
                    first_seen_s=now,
                    last_seen_s=now,
                )
                self.messages[key] = summary
            summary.apply(message, fields, now)

    def to_jsonable(self, now: float | None = None) -> dict:
        now = time.monotonic() if now is None else now
        vehicle = self.state.to_jsonable(now)
        return {
            "type": "session_snapshot",
            "vehicles": [vehicle],
            "selectedVehicleId": vehicle.get("id"),
            "messages": [
                summary.to_jsonable(now)
                for summary in sorted(
                    self.messages.values(), key=lambda item: item.last_seen_s, reverse=True
                )
            ],
            "events": self.events[:80],
            "packetCount": self.packet_count,
            "decodedCount": self.decoded_count,
        }


@dataclass
class AltlogRecorder:
    output: Path
    records: list[dict] = field(default_factory=list)

    def record(self, messages: Iterable[MavlinkMessage], now: float) -> None:
        for message in messages:
            self.records.append(
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

    def close(self) -> None:
        self.records.sort(key=lambda record: record["timeS"])
        manifest = {
            "schemaVersion": ALTLOG_SCHEMA_VERSION,
            "contractVersion": "altair-telemetry-contract-v1",
            "source": {"tool": "mavlink_live_bridge.py"},
            "recordFiles": ["records.jsonl"],
            "csvHeader": [],
            "requiredTelemetryGroups": {},
            "unsupportedFields": [],
            "startTimeS": self.records[0]["timeS"] if self.records else None,
            "endTimeS": self.records[-1]["timeS"] if self.records else None,
            "vehicleIds": sorted(
                {
                    f"{record['systemId']}:{record['componentId']}"
                    for record in self.records
                    if record.get("type") == "mavlink_packet"
                }
            ),
        }
        write_altlog(self.output, manifest, self.records)


def decode_message_fields(message: MavlinkMessage) -> dict[str, float | int | str | None]:
    if message.msg_id == 0:
        decoded = _unpack("<IBBBBB", message.payload, 9)
        if decoded is None:
            return {}
        custom_mode, vehicle_type, autopilot, base_mode, system_status, mavlink_version = decoded
        return {
            "customMode": custom_mode,
            "vehicleType": MAV_TYPE_NAMES.get(vehicle_type, f"MAV type {vehicle_type}"),
            "autopilot": autopilot,
            "baseMode": base_mode,
            "armed": 1 if base_mode & 0x80 else 0,
            "systemStatus": system_status,
            "mavlinkVersion": mavlink_version,
        }
    if message.msg_id == 30:
        decoded = _unpack("<Iffffff", message.payload, 28)
        if decoded is None:
            return {}
        time_boot_ms, roll, pitch, yaw, rollspeed, pitchspeed, yawspeed = decoded
        return {
            "timeBootMs": time_boot_ms,
            "rollRad": roll,
            "pitchRad": pitch,
            "yawRad": yaw,
            "rollRateRps": rollspeed,
            "pitchRateRps": pitchspeed,
            "yawRateRps": yawspeed,
        }
    if message.msg_id == 24:
        decoded = _unpack("<QiiiHHHHBB", message.payload, 30)
        if decoded is None:
            return {}
        time_usec, lat, lon, alt, eph, epv, vel, cog, fix_type, satellites = decoded
        return {
            "timeUsec": time_usec,
            "latDeg": lat / 10000000.0,
            "lonDeg": lon / 10000000.0,
            "altitudeM": alt / 1000.0,
            "ephCm": eph,
            "epvCm": epv,
            "groundspeedMps": vel / 100.0,
            "courseDeg": None if cog == 65535 else cog / 100.0,
            "fixType": fix_type,
            "satellitesVisible": None if satellites == 255 else satellites,
        }
    if message.msg_id == 33:
        decoded = _unpack("<IiiiihhhH", message.payload, 28)
        if decoded is None:
            return {}
        time_boot_ms, lat, lon, alt, rel_alt, vx, vy, vz, hdg = decoded
        return {
            "timeBootMs": time_boot_ms,
            "latDeg": lat / 10000000.0,
            "lonDeg": lon / 10000000.0,
            "altitudeM": alt / 1000.0,
            "relativeAltitudeM": rel_alt / 1000.0,
            "northMps": vx / 100.0,
            "eastMps": vy / 100.0,
            "downMps": vz / 100.0,
            "headingDeg": None if hdg == 65535 else hdg / 100.0,
        }
    if message.msg_id == 74:
        decoded = _unpack("<ffhHff", message.payload, 20)
        if decoded is None:
            return {}
        airspeed, groundspeed, heading, throttle, altitude, climb = decoded
        return {
            "airspeedMps": airspeed,
            "groundspeedMps": groundspeed,
            "headingDeg": heading,
            "throttlePct": throttle,
            "altitudeM": altitude,
            "climbMps": climb,
        }
    if message.msg_id == 36:
        fields: dict[str, float | int | str | None] = {
            "timeBootMs": (
                struct.unpack_from("<I", message.payload)[0] if len(message.payload) >= 4 else None
            )
        }
        if len(message.payload) >= 21:
            for index in range(8):
                fields[f"servo{index + 1}Raw"] = struct.unpack_from(
                    "<H", message.payload, 4 + index * 2
                )[0]
        return fields
    if message.msg_id == 42:
        decoded = _unpack("<H", message.payload, 2)
        return {} if decoded is None else {"seq": decoded[0]}
    if message.msg_id == 136:
        decoded = _unpack("<iiHffHH", message.payload, 22)
        if decoded is None:
            return {}
        lat, lon, spacing, terrain_height, current_height, pending, loaded = decoded
        return {
            "latDeg": lat / 10000000.0,
            "lonDeg": lon / 10000000.0,
            "spacingM": spacing,
            "terrainHeightM": terrain_height,
            "currentHeightM": current_height,
            "pending": pending,
            "loaded": loaded,
        }
    if message.msg_id == 242:
        decoded = _unpack("<iiifff", message.payload, 24)
        if decoded is None:
            return {}
        lat, lon, alt, x, y, z = decoded
        return {
            "latDeg": lat / 10000000.0,
            "lonDeg": lon / 10000000.0,
            "altitudeM": alt / 1000.0,
            "xM": x,
            "yM": y,
            "zM": z,
        }
    return {"payloadBytes": len(message.payload), "sequence": message.seq}


@dataclass
class UdpForwarder:
    endpoints: list[tuple[str, int]]
    sockets: list[socket.socket] = field(default_factory=list)

    def __post_init__(self) -> None:
        for _ in self.endpoints:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setblocking(False)
            self.sockets.append(sock)

    def forward(self, packet: bytes) -> None:
        for sock, endpoint in zip(self.sockets, self.endpoints):
            sock.sendto(packet, endpoint)

    def close(self) -> None:
        for sock in self.sockets:
            sock.close()
        self.sockets.clear()


class BridgeProtocol(asyncio.DatagramProtocol):
    def __init__(
        self,
        parser: MavlinkV1Parser,
        snapshot: LiveSessionSnapshot,
        forwarder: UdpForwarder,
        broadcasters: Iterable,
        recorder: AltlogRecorder | None = None,
    ) -> None:
        self.parser = parser
        self.snapshot = snapshot
        self.forwarder = forwarder
        self.broadcasters = list(broadcasters)
        self.recorder = recorder

    def datagram_received(self, data: bytes, addr) -> None:
        self.forwarder.forward(data)
        now = time.monotonic()
        messages = self.parser.feed(data)
        self.snapshot.apply_datagram(messages, now)
        if self.recorder is not None:
            self.recorder.record(messages, now)
        payload = json.dumps(self.snapshot.state.to_jsonable(now), separators=(",", ":"))
        snapshot_payload = json.dumps(self.snapshot.to_jsonable(now), separators=(",", ":"))
        for broadcaster in self.broadcasters:
            broadcaster(payload)
            broadcaster(snapshot_payload)


class WebSocketClient:
    def __init__(self, writer: asyncio.StreamWriter) -> None:
        self.writer = writer
        self.closed = False

    async def send(self, text: str) -> None:
        if self.closed:
            return
        data = text.encode("utf-8")
        if len(data) < 126:
            header = bytes([0x81, len(data)])
        elif len(data) <= 0xFFFF:
            header = bytes([0x81, 126]) + struct.pack("!H", len(data))
        else:
            header = bytes([0x81, 127]) + struct.pack("!Q", len(data))
        try:
            self.writer.write(header + data)
            await self.writer.drain()
        except (ConnectionError, RuntimeError):
            self.closed = True

    async def close(self) -> None:
        self.closed = True
        self.writer.close()
        try:
            await self.writer.wait_closed()
        except ConnectionError:
            pass


async def read_http_headers(reader: asyncio.StreamReader) -> dict[str, str]:
    request = await reader.readuntil(b"\r\n\r\n")
    lines = request.decode("iso-8859-1").split("\r\n")
    headers: dict[str, str] = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    return headers


async def websocket_handler(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    snapshot: LiveSessionSnapshot,
    clients: set[WebSocketClient],
) -> None:
    try:
        headers = await read_http_headers(reader)
        key = headers.get("sec-websocket-key")
        if key is None:
            writer.close()
            await writer.wait_closed()
            return
        accept = base64.b64encode(
            hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()
        ).decode("ascii")
        writer.write(
            (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n"
                "\r\n"
            ).encode("ascii")
        )
        await writer.drain()
        client = WebSocketClient(writer)
        clients.add(client)
        await client.send(json.dumps(snapshot.state.to_jsonable(), separators=(",", ":")))
        await client.send(json.dumps(snapshot.to_jsonable(), separators=(",", ":")))
        while not reader.at_eof():
            chunk = await reader.read(512)
            if not chunk:
                break
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        for client in list(clients):
            if client.writer is writer:
                clients.discard(client)
                await client.close()
                break


def parse_endpoint(text: str) -> tuple[str, int]:
    if ":" not in text:
        raise argparse.ArgumentTypeError(f"expected host:port, got {text!r}")
    host, port_text = text.rsplit(":", 1)
    try:
        port = int(port_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid endpoint port: {text!r}") from exc
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError(f"endpoint port out of range: {text!r}")
    return host, port


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Forward MAVLink UDP packets and publish decoded live vehicle state over WebSocket."
    )
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=14551)
    parser.add_argument(
        "--forward",
        action="append",
        type=parse_endpoint,
        help="UDP endpoint to forward raw MAVLink packets to; repeatable",
    )
    parser.add_argument(
        "--no-forward",
        action="store_true",
        help="do not forward raw MAVLink packets to any UDP endpoint",
    )
    parser.add_argument("--ws-host", default="127.0.0.1")
    parser.add_argument("--ws-port", type=int, default=8765)
    parser.add_argument(
        "--writable-animus",
        action="store_true",
        help="advertise guarded SITL-only write support to Animus clients",
    )
    parser.add_argument(
        "--record-altlog",
        help="write received raw MAVLink packets to an .altlog directory or .zip on shutdown",
    )
    args = parser.parse_args(argv)
    if args.no_forward:
        args.forward = []
    elif args.forward is None:
        args.forward = [("127.0.0.1", 14550)]
    return args


async def serve(args: argparse.Namespace) -> None:
    parser = MavlinkV1Parser()
    state = LiveVehicleState(writable_animus=args.writable_animus)
    snapshot = LiveSessionSnapshot(state)
    forwarder = UdpForwarder(args.forward)
    recorder = AltlogRecorder(Path(args.record_altlog)) if args.record_altlog else None
    clients: set[WebSocketClient] = set()

    def broadcast(payload: str) -> None:
        stale: list[WebSocketClient] = []
        for client in clients:
            if client.closed:
                stale.append(client)
            else:
                asyncio.create_task(client.send(payload))
        for client in stale:
            clients.discard(client)

    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: BridgeProtocol(parser, snapshot, forwarder, [broadcast], recorder),
        local_addr=(args.listen_host, args.listen_port),
    )
    server = await asyncio.start_server(
        lambda reader, writer: websocket_handler(reader, writer, snapshot, clients),
        args.ws_host,
        args.ws_port,
    )
    try:
        async with server:
            print(
                f"mavlink bridge listening udp={args.listen_host}:{args.listen_port} "
                f"ws=ws://{args.ws_host}:{args.ws_port} forward={args.forward}",
                flush=True,
            )
            await server.serve_forever()
    finally:
        transport.close()
        server.close()
        await server.wait_closed()
        forwarder.close()
        if recorder is not None:
            recorder.close()


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
