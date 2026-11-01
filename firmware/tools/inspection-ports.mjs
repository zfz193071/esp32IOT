import { readdir } from 'node:fs/promises';

const portName = /^(cu\.[A-Za-z0-9._-]+|ttyUSB\d+|ttyACM\d+)$/;
export const isSerialPort = (port) => typeof port === 'string' && port.startsWith('/dev/') &&
  portName.test(port.slice(5));

export async function listSerialPorts() {
  return (await readdir('/dev')).filter((name) => portName.test(name))
    .map((name) => `/dev/${name}`).sort();
}

export function compareSerialPorts(snapshot, current) {
  if (snapshot?.kind !== 'serial-port-snapshot' || snapshot.schemaVersion !== 1 ||
      typeof snapshot.capturedAt !== 'string' || !Array.isArray(snapshot.ports) ||
      snapshot.ports.length > 128 || snapshot.ports.some((port) => !isSerialPort(port)) ||
      new Set(snapshot.ports).size !== snapshot.ports.length)
    throw new Error('Invalid serial port snapshot');
  const before = new Set(snapshot.ports);
  const after = new Set(current);
  return {
    addedPorts: [...after].filter((port) => !before.has(port)).sort(),
    removedPorts: [...before].filter((port) => !after.has(port)).sort(),
  };
}
