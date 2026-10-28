import { DeviceClient } from './client.mjs';

const byId = (id) => document.getElementById(id);
const sourceNames = { button: '实体按键', infrared: '红外遥控', web: '网页', system: '系统' };
const client = new DeviceClient({ onChange: ({ state, connected, busy, message, lastSync }) => {
  const usable = connected && !busy;
  byId('connection').textContent = connected ? '已连接' : (lastSync ? '未同步' : '连接中');
  byId('connection').className = `badge${connected ? ' online' : ''}`;
  byId('output').textContent = state ? (state.fanOutputOn ? '开' : '关') : '—';
  byId('freshness').textContent = !state ? '等待首次同步' :
    busy ? '操作进行中 · 等待同步确认' : connected ? '最近同步的控制输出' : '历史记录 · 当前输出未知';
  byId('mode').textContent = state ? (state.mode === 'auto' ? '自动' : '手动') : '—';
  byId('temperature').textContent = state?.sensorStatus === 'ok' && state.temperatureC !== null
    ? `${state.temperatureC.toFixed(1)} ℃` : '不可用';
  byId('thresholds').textContent = state ? `${state.onThresholdC} ℃ 开 / ${state.offThresholdC} ℃ 关` : '—';
  byId('source').textContent = state ? sourceNames[state.lastCommandSource] : '—';
  byId('fault').hidden = state?.fault !== 'temperature_unavailable';
  for (const id of ['power-on', 'power-off', 'manual']) byId(id).disabled = !usable;
  const autoReady = state?.sensorStatus === 'ok' && state.temperatureC !== null &&
    state.sampleAgeMs !== null && state.sampleAgeMs <= 5000;
  byId('auto').disabled = !usable || !autoReady;
  byId('temperature-hint').textContent = autoReady
    ? '自动模式按温度阈值控制输出，设备会再次检查温度有效性。' : '温度未就绪时，自动模式不可用。';
  byId('refresh').disabled = busy;
  byId('message').textContent = message;
  byId('last-sync').textContent = lastSync === null ? '尚未同步' : `最后同步：${new Date(lastSync).toLocaleTimeString('zh-CN')}`;
} });

byId('power-on').addEventListener('click', () => client.write('power', true));
byId('power-off').addEventListener('click', () => client.write('power', false));
byId('manual').addEventListener('click', () => client.write('mode', 'manual'));
byId('auto').addEventListener('click', () => client.write('mode', 'auto'));
byId('refresh').addEventListener('click', () => client.refresh());
// Only the development server returns this header. It is not a production query switch.
fetch('/', { method: 'HEAD', cache: 'no-store' }).then((response) => {
  byId('demo').hidden = response.headers.get('X-ESP32-Simulator') !== 'true';
}).catch(() => {});
client.notify();
client.refresh();
setInterval(() => client.refresh(), 1000);
