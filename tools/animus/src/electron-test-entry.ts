import dgram from 'node:dgram';
import {
  MavlinkTelemetryService,
  mavlinkV1Frame,
  type VehicleStatePayload
} from './mavlink.js';

function onceState(service: MavlinkTelemetryService): Promise<VehicleStatePayload> {
  return new Promise((resolve) => {
    service.once('vehicle-state', (payload) => resolve(payload as VehicleStatePayload));
  });
}

function sendUdp(packet: Buffer, port: number): Promise<void> {
  const socket = dgram.createSocket('udp4');
  return new Promise((resolve, reject) => {
    socket.send(packet, port, '127.0.0.1', (error) => {
      socket.close();
      if (error) {
        reject(error);
      } else {
        resolve();
      }
    });
  });
}

async function main(): Promise<void> {
  const service = new MavlinkTelemetryService({
    listenHost: '127.0.0.1',
    listenPort: Number(process.env.ALTAIR_TEST_MAVLINK_PORT ?? 0),
    qgcForwarding: false
  });
  await service.start();
  const heartbeatPayload = Buffer.from([0, 0, 0, 0, 1, 0, 0, 4, 3]);
  const statePromise = onceState(service);
  await sendUdp(mavlinkV1Frame(0, heartbeatPayload, 2), service.getConfig().listenPort);
  const state = await statePromise;
  await service.stop();
  if (state.type !== 'vehicle_state' || state.systemId !== 1 || state.heartbeatAgeS === null) {
    throw new Error(`unexpected bridge state ${JSON.stringify(state)}`);
  }
  console.log(JSON.stringify({ ok: true, systemId: state.systemId, listenPort: service.getConfig().listenPort }));
}

main().catch(async (error) => {
  console.error(error);
  process.exitCode = 1;
});
