import { mkdtemp, readFile, appendFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { appendCommandAuditEntry, readRecentCommandAuditEntries } from './command-audit';
import type { CommandAuditEntry } from './state';

function entry(index: number): CommandAuditEntry {
  return {
    schemaVersion: 1,
    eventKind: 'dispatch-sent',
    transactionId: `cmd-${index}`,
    sessionId: 'session-test',
    operatorId: 'operator-test',
    timestamp: `2026-01-01T00:00:0${index}.000Z`,
    vehicleId: '1:1',
    commandName: 'arm',
    commandId: 400,
    params: [1],
    payload: { params: {}, encodedParams: [1] },
    confirmationType: 'browser-confirm',
    accepted: true,
    state: 'sent',
    reason: 'command sent',
    failureReason: null,
    ack: null,
    authority: 'sitl-writable',
    writable: true,
    retryCount: 0
  };
}

describe('command audit log', () => {
  it('appends valid JSONL entries in order', async () => {
    const filePath = path.join(await mkdtemp(path.join(os.tmpdir(), 'animus-audit-')), 'command-audit.jsonl');
    await appendCommandAuditEntry(filePath, entry(1));
    await appendCommandAuditEntry(filePath, entry(2));

    const lines = (await readFile(filePath, 'utf8')).trim().split('\n').map((line) => JSON.parse(line) as CommandAuditEntry);
    expect(lines.map((line) => line.transactionId)).toEqual(['cmd-1', 'cmd-2']);
  });

  it('reads recent entries newest-first', async () => {
    const filePath = path.join(await mkdtemp(path.join(os.tmpdir(), 'animus-audit-')), 'command-audit.jsonl');
    await appendCommandAuditEntry(filePath, entry(1));
    await appendCommandAuditEntry(filePath, entry(2));
    await appendCommandAuditEntry(filePath, entry(3));

    const recent = await readRecentCommandAuditEntries(filePath, 2);
    expect(recent.map((line) => line.transactionId)).toEqual(['cmd-3', 'cmd-2']);
  });

  it('ignores malformed existing lines when reading recent history', async () => {
    const filePath = path.join(await mkdtemp(path.join(os.tmpdir(), 'animus-audit-')), 'command-audit.jsonl');
    await appendFile(filePath, `${JSON.stringify(entry(1))}\nnot-json\n${JSON.stringify(entry(2))}\n`, 'utf8');

    const recent = await readRecentCommandAuditEntries(filePath, 10);
    expect(recent.map((line) => line.transactionId)).toEqual(['cmd-2', 'cmd-1']);
  });

  it('reads schema version 1 entries that predate identity fields', async () => {
    const filePath = path.join(await mkdtemp(path.join(os.tmpdir(), 'animus-audit-')), 'command-audit.jsonl');
    const legacy = {
      schemaVersion: 1,
      eventKind: 'dispatch-sent',
      timestamp: '2026-01-01T00:00:00.000Z',
      vehicleId: '1:1',
      commandName: 'arm',
      params: [1],
      accepted: true,
      state: 'sent',
      reason: 'command sent',
      ack: null
    };
    await appendFile(filePath, `${JSON.stringify(legacy)}\n`, 'utf8');

    const recent = await readRecentCommandAuditEntries(filePath, 10);
    expect(recent[0]).toMatchObject({ eventKind: 'dispatch-sent', commandName: 'arm' });
  });
});
