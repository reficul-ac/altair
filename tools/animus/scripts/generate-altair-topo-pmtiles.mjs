#!/usr/bin/env node
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const output = path.resolve(__dirname, '..', 'public', 'maps', 'altair-topo.pmtiles');
const maxZoom = 8;
const tile = pngTile(256, 256);
const metadata = Buffer.from(JSON.stringify({
  name: 'Altair bundled topographic map',
  description: 'Deterministic offline topographic raster tile bundled with Animus.',
  attribution: 'Generated Altair offline topographic test basemap',
  version: '1'
}), 'utf8');
const rootDirectory = Buffer.from(serializeDirectory(tile.length, addressedTiles(maxZoom)));
const headerLength = 127;
const rootOffset = headerLength;
const metadataOffset = rootOffset + rootDirectory.length;
const tileDataOffset = metadataOffset + metadata.length;
const header = Buffer.alloc(headerLength);

header.write('PMTiles', 0, 'ascii');
header.writeUInt8(3, 7);
writeUint64(header, 8, rootOffset);
writeUint64(header, 16, rootDirectory.length);
writeUint64(header, 24, metadataOffset);
writeUint64(header, 32, metadata.length);
writeUint64(header, 40, 0);
writeUint64(header, 48, 0);
writeUint64(header, 56, tileDataOffset);
writeUint64(header, 64, tile.length);
writeUint64(header, 72, addressedTiles(maxZoom));
writeUint64(header, 80, 1);
writeUint64(header, 88, 1);
header.writeUInt8(1, 96);
header.writeUInt8(1, 97);
header.writeUInt8(1, 98);
header.writeUInt8(2, 99);
header.writeUInt8(0, 100);
header.writeUInt8(maxZoom, 101);
writeCoord(header, 102, -180);
writeCoord(header, 106, -85);
writeCoord(header, 110, 180);
writeCoord(header, 114, 85);
header.writeUInt8(maxZoom, 118);
writeCoord(header, 119, -122);
writeCoord(header, 123, 37);

mkdirSync(path.dirname(output), { recursive: true });
writeFileSync(output, Buffer.concat([header, rootDirectory, metadata, tile]));
console.log(`wrote ${output}`);

function addressedTiles(zoom) {
  return (4 ** (zoom + 1) - 1) / 3;
}

function serializeDirectory(tileLength, runLength) {
  return [
    ...varint(1),
    ...varint(0),
    ...varint(runLength),
    ...varint(tileLength),
    ...varint(1)
  ];
}

function writeUint64(buffer, offset, value) {
  buffer.writeUInt32LE(value >>> 0, offset);
  buffer.writeUInt32LE(Math.floor(value / 2 ** 32), offset + 4);
}

function writeCoord(buffer, offset, value) {
  buffer.writeInt32LE(Math.round(value * 1e7), offset);
}

function varint(value) {
  const bytes = [];
  let remaining = value;
  while (remaining > 127) {
    bytes.push((remaining & 0x7f) | 0x80);
    remaining = Math.floor(remaining / 128);
  }
  bytes.push(remaining);
  return bytes;
}

function pngTile(width, height) {
  const rows = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y += 1) {
    const row = y * (width * 4 + 1);
    rows[row] = 0;
    for (let x = 0; x < width; x += 1) {
      const nx = x / width;
      const ny = y / height;
      const ridge = Math.sin(nx * 18 + Math.cos(ny * 7) * 1.8) + Math.cos((nx + ny) * 11);
      const slope = Math.sin((nx - ny) * 24) * 0.32;
      const contour = Math.abs(Math.sin((ridge + ny * 3.2) * 12));
      const road = Math.abs(ny - (0.58 + Math.sin(nx * 8) * 0.035)) < 0.006;
      const water = Math.abs(nx - (0.23 + Math.sin(ny * 10) * 0.025)) < 0.012 || ny > 0.82 + Math.sin(nx * 12) * 0.025;
      const shade = Math.max(0, Math.min(1, 0.54 + ridge * 0.14 + slope));
      let r = 74 + shade * 52;
      let g = 105 + shade * 72;
      let b = 74 + shade * 38;
      if (contour > 0.965) {
        r = 72;
        g = 76;
        b = 58;
      }
      if (water) {
        r = 58;
        g = 119;
        b = 145;
      }
      if (road) {
        r = 214;
        g = 190;
        b = 128;
      }
      const pixel = row + 1 + x * 4;
      rows[pixel] = Math.round(r);
      rows[pixel + 1] = Math.round(g);
      rows[pixel + 2] = Math.round(b);
      rows[pixel + 3] = 255;
    }
  }
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr(width, height)),
    chunk('IDAT', deflateSync(rows, { level: 9 })),
    chunk('IEND', Buffer.alloc(0))
  ]);
}

function ihdr(width, height) {
  const data = Buffer.alloc(13);
  data.writeUInt32BE(width, 0);
  data.writeUInt32BE(height, 4);
  data.writeUInt8(8, 8);
  data.writeUInt8(6, 9);
  data.writeUInt8(0, 10);
  data.writeUInt8(0, 11);
  data.writeUInt8(0, 12);
  return data;
}

function chunk(type, data) {
  const name = Buffer.from(type, 'ascii');
  const length = Buffer.alloc(4);
  const crc = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  crc.writeUInt32BE(crc32(Buffer.concat([name, data])), 0);
  return Buffer.concat([length, name, data, crc]);
}

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = crc & 1 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1;
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}
