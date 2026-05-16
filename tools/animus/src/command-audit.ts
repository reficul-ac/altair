import { mkdir, readFile, appendFile } from 'node:fs/promises';
import path from 'node:path';
import type { CommandAuditEntry } from './state.js';

export type CommandAuditLog = {
  append: (entry: CommandAuditEntry) => Promise<void>;
  recent: (limit: number) => Promise<CommandAuditEntry[]>;
};

export function createCommandAuditLog(filePath: string): CommandAuditLog {
  let appendQueue = Promise.resolve();
  return {
    append: (entry) => {
      appendQueue = appendQueue
        .catch(() => undefined)
        .then(() => appendCommandAuditEntry(filePath, entry));
      return appendQueue;
    },
    recent: (limit) => readRecentCommandAuditEntries(filePath, limit)
  };
}

export async function appendCommandAuditEntry(filePath: string, entry: CommandAuditEntry): Promise<void> {
  await mkdir(path.dirname(filePath), { recursive: true });
  await appendFile(filePath, `${JSON.stringify(entry)}\n`, 'utf8');
}

export async function readRecentCommandAuditEntries(filePath: string, limit: number): Promise<CommandAuditEntry[]> {
  if (!Number.isFinite(limit) || limit <= 0) {
    return [];
  }
  let contents = '';
  try {
    contents = await readFile(filePath, 'utf8');
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') {
      return [];
    }
    throw error;
  }
  const entries: CommandAuditEntry[] = [];
  for (const line of contents.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    try {
      const parsed = JSON.parse(trimmed) as Partial<CommandAuditEntry>;
      if (parsed.schemaVersion === 1 && typeof parsed.eventKind === 'string' && typeof parsed.timestamp === 'string') {
        entries.push(parsed as CommandAuditEntry);
      }
    } catch {
      // Preserve append-only durability even if a prior process left a partial line.
    }
  }
  return entries.slice(-Math.floor(limit)).reverse();
}
