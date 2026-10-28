// Development-only HTTP simulator. Never included in firmware assets.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { randomUUID } from 'node:crypto';
import { validConfig } from '../web/config.mjs';

const port = Number(process.env.ESP32_PREVIEW_PORT || 8312);
const web = new URL('../web/', import.meta.url);
const routes = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/client.mjs', ['client.mjs', 'text/javascript; charset=utf-8']],
  ['/config.mjs', ['config.mjs', 'text/javascript; charset=utf-8']],
]);
const initial = () => ({ bootId: randomUUID(), revision: 0, mode: 'manual', fanOutputOn: false,
  temperatureC: null, sensorStatus: 'initializing', sampleAgeMs: null,
  onThresholdC: 30, offThresholdC: 28, fault: 'none', lastCommandSource: 'system', wifiConnected: true });
let state = initial();
function publish(next) {
  const changed = Object.keys(state).some((key) => key !== 'revision' && state[key] !== next[key]);
  state = { ...next, revision: state.revision + (changed ? 1 : 0) };
}
function automaticOutput(temperatureC, thresholds, previous) {
  if (temperatureC >= thresholds.onThresholdC) return true;
  if (temperatureC <= thresholds.offThresholdC) return false;
  return previous;
}
createServer(async (req, res) => {
  const send = (code, data) => {
    res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store',
      'X-ESP32-Simulator': 'true' });
    res.end(JSON.stringify(data));
  };
  try {
    // Local-only binding plus Host/Origin checks avoid exposing demo mutation to other sites.
    const host = `127.0.0.1:${port}`;
    if (req.headers.host !== host || (req.headers.origin && req.headers.origin !== `http://${host}`)) {
      send(403, { error: { code: 'FORBIDDEN' } }); return;
    }
    const route = routes.get(req.url);
    if (route && ['GET', 'HEAD'].includes(req.method)) {
      const contents = await readFile(new URL(route[0], web));
      res.writeHead(200, { 'Content-Type': route[1], 'Cache-Control': 'no-store', 'X-ESP32-Simulator': 'true' });
      res.end(req.method === 'HEAD' ? undefined : contents); return;
    }
    if (req.method === 'GET' && req.url === '/api/v1/state') { send(200, state); return; }
    const temperatureInjection = req.url === '/__simulator/temperature';
    if (req.method !== 'PUT' ||
        !['/api/v1/power', '/api/v1/mode', '/api/v1/config',
          '/__simulator/temperature'].includes(req.url)) {
      send(404, { error: { code: 'NOT_FOUND' } }); return;
    }
    if (req.headers['content-type']?.split(';')[0].trim() !== 'application/json') {
      send(400, { error: { code: 'INVALID_REQUEST' } }); return;
    }
    let body = '';
    for await (const chunk of req) {
      body += chunk;
      if (Buffer.byteLength(body) > 1024) { send(413, { error: { code: 'TOO_LARGE' } }); return; }
    }
    const value = JSON.parse(body);
    if (temperatureInjection) {
      if (!value || Array.isArray(value) || Object.keys(value).length !== 1 ||
          !Object.hasOwn(value, 'temperatureC') ||
          (value.temperatureC !== null && !Number.isFinite(value.temperatureC))) {
        send(400, { error: { code: 'INVALID_REQUEST' } }); return;
      }
      if (value.temperatureC === null) {
        const wasAuto = state.mode === 'auto';
        publish({ ...state, temperatureC: null, sensorStatus: 'invalid',
          mode: wasAuto ? 'manual' : state.mode, fanOutputOn: wasAuto ? false : state.fanOutputOn,
          fault: wasAuto ? 'temperature_unavailable' : state.fault,
          lastCommandSource: wasAuto ? 'system' : state.lastCommandSource });
      } else {
        const next = { ...state, temperatureC: value.temperatureC,
          sensorStatus: 'ok', sampleAgeMs: 0, fault: 'none' };
        if (state.mode === 'auto') {
          next.fanOutputOn = automaticOutput(value.temperatureC, state, state.fanOutputOn);
          if (next.fanOutputOn !== state.fanOutputOn) next.lastCommandSource = 'system';
        }
        publish(next);
      }
      send(200, state); return;
    }
    if (req.url === '/api/v1/config') {
      if (!validConfig(value)) { send(400, { error: { code: 'INVALID_CONFIG' } }); return; }
      if (state.onThresholdC === value.onThresholdC && state.offThresholdC === value.offThresholdC) {
        send(200, state); return;
      }
      if (process.env.ESP32_SIMULATE_STORAGE_FAILURE === '1') {
        send(503, { error: { code: 'STORAGE_FAILURE' } }); return;
      }
      const next = { ...state, ...value };
      if (state.mode === 'auto') {
        next.fanOutputOn = automaticOutput(state.temperatureC, value, state.fanOutputOn);
        if (next.fanOutputOn !== state.fanOutputOn) next.lastCommandSource = 'system';
      }
      publish(next);
      send(200, state); return;
    }
    const power = req.url.endsWith('/power');
    if (!value || Array.isArray(value) || Object.keys(value).length !== 1 ||
        !(power ? typeof value.on === 'boolean' : ['manual', 'auto'].includes(value.mode))) {
      send(400, { error: { code: 'INVALID_REQUEST' } }); return;
    }
    if (!power && value.mode === 'auto') {
      if (state.sensorStatus !== 'ok' || state.temperatureC === null || state.sampleAgeMs > 5000) {
        send(409, { error: { code: 'TEMPERATURE_UNAVAILABLE' } }); return;
      }
      publish({ ...state, mode: 'auto',
        fanOutputOn: automaticOutput(state.temperatureC, state,
          state.mode === 'auto' && state.fanOutputOn), lastCommandSource: 'web' });
      send(200, state); return;
    }
    const on = power ? value.on : state.fanOutputOn;
    publish({ ...state, mode: 'manual', fanOutputOn: on, lastCommandSource: 'web' });
    send(200, state);
  } catch {
    if (!res.headersSent) send(400, { error: { code: 'INVALID_REQUEST' } });
    else res.end();
  }
}).listen(port, '127.0.0.1', () => {
  console.log(`ESP32 simulated device only: http://127.0.0.1:${port}`);
});
