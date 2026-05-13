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
from typing import Iterable


MAVLINK_V1_STX = 0xFE
MAVLINK_CRC_EXTRA = {
    0: 50,
    30: 39,
    33: 104,
    74: 20,
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
                )
            )
        return messages


def _unpack(fmt: str, payload: bytes, min_len: int):
    if len(payload) < min_len:
        return None
    return struct.unpack_from(fmt, payload)


@dataclass
class LiveVehicleState:
    connected: bool = False
    last_packet_time: float | None = None
    last_heartbeat_time: float | None = None
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
    origin_lat_deg: float | None = None
    origin_lon_deg: float | None = None
    origin_altitude_m: float | None = None

    def apply(self, message: MavlinkMessage, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        self.connected = True
        self.last_packet_time = now
        self.system_id = message.system_id
        self.component_id = message.component_id
        if message.msg_id == 0:
            self.last_heartbeat_time = now
        elif message.msg_id == 30:
            decoded = _unpack("<Iffffff", message.payload, 28)
            if decoded is not None:
                _, self.roll_rad, self.pitch_rad, self.yaw_rad, self.rollspeed_rps, self.pitchspeed_rps, self.yawspeed_rps = decoded
        elif message.msg_id == 33:
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
        elif message.msg_id == 74:
            decoded = _unpack("<ffhHff", message.payload, 20)
            if decoded is not None:
                self.airspeed_mps, self.groundspeed_mps, heading, throttle, self.altitude_m, self.climb_mps = decoded
                self.heading_deg = float(heading)
                self.throttle_pct = throttle

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
        }

    def local_position(self) -> tuple[float | None, float | None, float | None]:
        if self.lat_deg is None or self.lon_deg is None or self.altitude_m is None:
            return None, None, None
        if self.origin_lat_deg is None or self.origin_lon_deg is None or self.origin_altitude_m is None:
            return None, None, None
        earth_radius_m = 6378137.0
        lat0_rad = math.radians(self.origin_lat_deg)
        north_m = math.radians(self.lat_deg - self.origin_lat_deg) * earth_radius_m
        east_m = math.radians(self.lon_deg - self.origin_lon_deg) * earth_radius_m * math.cos(lat0_rad)
        up_m = self.altitude_m - self.origin_altitude_m
        return north_m, east_m, up_m


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
    def __init__(self, parser: MavlinkV1Parser, state: LiveVehicleState, forwarder: UdpForwarder, broadcasters: Iterable) -> None:
        self.parser = parser
        self.state = state
        self.forwarder = forwarder
        self.broadcasters = list(broadcasters)

    def datagram_received(self, data: bytes, addr) -> None:
        self.forwarder.forward(data)
        now = time.monotonic()
        for message in self.parser.feed(data):
            self.state.apply(message, now)
        payload = json.dumps(self.state.to_jsonable(now), separators=(",", ":"))
        for broadcaster in self.broadcasters:
            broadcaster(payload)


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


async def websocket_handler(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, state: LiveVehicleState, clients: set[WebSocketClient]) -> None:
    try:
        headers = await read_http_headers(reader)
        key = headers.get("sec-websocket-key")
        if key is None:
            writer.close()
            await writer.wait_closed()
            return
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()).decode("ascii")
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
        await client.send(json.dumps(state.to_jsonable(), separators=(",", ":")))
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
    parser = argparse.ArgumentParser(description="Forward MAVLink UDP packets and publish decoded live vehicle state over WebSocket.")
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=14551)
    parser.add_argument("--forward", action="append", type=parse_endpoint, help="UDP endpoint to forward raw MAVLink packets to; repeatable")
    parser.add_argument("--ws-host", default="127.0.0.1")
    parser.add_argument("--ws-port", type=int, default=8765)
    args = parser.parse_args(argv)
    if args.forward is None:
        args.forward = [("127.0.0.1", 14550)]
    return args


async def serve(args: argparse.Namespace) -> None:
    parser = MavlinkV1Parser()
    state = LiveVehicleState()
    forwarder = UdpForwarder(args.forward)
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
        lambda: BridgeProtocol(parser, state, forwarder, [broadcast]),
        local_addr=(args.listen_host, args.listen_port),
    )
    server = await asyncio.start_server(
        lambda reader, writer: websocket_handler(reader, writer, state, clients),
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


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
