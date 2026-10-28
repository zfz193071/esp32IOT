import { DeviceClient } from './client.mjs';
import { ConfigDraft } from './config.mjs';

const byId = (id) => document.getElementById(id);
const sourceNames = { button: '实体按键', infrared: '红外遥控', web: '网页', system: '系统' };
const sensorNames = { initializing: '等待采集', ok: '正常', invalid: '采集无效', stale: '样本已过期' };
const draft = new ConfigDraft();
function showDraft() {
  byId('threshold-on').value = draft.on;
  byId('threshold-off').value = draft.off;
}
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
  byId('sensor-health').textContent = state ? sensorNames[state.sensorStatus] : '等待采集';
  byId('sample-age').textContent = state?.sampleAgeMs == null ? '—' : `${(state.sampleAgeMs / 1000).toFixed(1)} 秒`;
  if (state) {
    draft.observe(state);
    if (!draft.dirty) showDraft();
    if (byId('config-feedback').textContent === '等待加载设备阈值。') {
      byId('config-feedback').textContent = '已加载设备当前阈值，修改后点击保存。';
    }
  }
  for (const id of ['threshold-on', 'threshold-off', 'save-config', 'reset-config']) byId(id).disabled = !usable;
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
for (const id of ['threshold-on', 'threshold-off']) byId(id).addEventListener('input', () => {
  draft.edit(byId('threshold-on').value, byId('threshold-off').value);
  byId('config-feedback').textContent = '正在编辑，尚未保存；同步不会覆盖你的输入。';
});
byId('reset-config').addEventListener('click', () => {
  draft.reset(); showDraft();
  byId('config-feedback').textContent = '已恢复最近同步的设备值，未发送保存请求。';
});
byId('config-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (client.busy || !client.connected) return;
  draft.edit(byId('threshold-on').value, byId('threshold-off').value);
  const value = draft.value();
  if (!value) {
    byId('config-feedback').textContent = '请输入两个 0～60 ℃ 的数值，开启温度至少高于关闭温度 1 ℃。';
    return;
  }
  const bootId = client.state.bootId;
  byId('config-feedback').textContent = '正在保存，等待设备确认…';
  const accepted = await client.write('config', value);
  if (accepted && client.connected && client.state.bootId === bootId && draft.confirm(value)) {
    showDraft();
    byId('config-feedback').textContent = '设备已确认保存，当前阈值已同步。';
  } else {
    byId('config-feedback').textContent = '本次保存未确认。编辑值已保留，请核对设备当前阈值和同步提示；不会自动重发。';
  }
});
// Only the development server returns this header. It is not a production query switch.
fetch('/', { method: 'HEAD', cache: 'no-store' }).then((response) => {
  byId('demo').hidden = response.headers.get('X-ESP32-Simulator') !== 'true';
}).catch(() => {});
client.notify();
client.refresh();
setInterval(() => client.refresh(), 1000);
