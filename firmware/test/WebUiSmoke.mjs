import assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
// Use a fresh, dedicated simulator; this check changes its in-memory thresholds.
const base = process.env.ESP32_UI_BASE || 'http://127.0.0.1:18316';
const marker = await fetch(base, { method: 'HEAD' });
assert.equal(marker.headers.get('X-ESP32-Simulator'), 'true', 'only run against the development simulator');
const version = await (await fetch(`${process.env.ESP32_UI_CDP || 'http://127.0.0.1:19316'}/json/version`)).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
let sequence = 0;
const pending = new Map();
ws.onmessage = ({data}) => {
  const message = JSON.parse(data);
  if (!message.id) return;
  const task = pending.get(message.id); if (!task) return;
  pending.delete(message.id); clearTimeout(task.timer);
  message.error ? task.reject(new Error(JSON.stringify(message.error))) : task.resolve(message.result);
};
function send(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error('CDP timeout: ' + method)); }, 10000);
    pending.set(id, {resolve, reject,timer});
    ws.send(JSON.stringify({id, method, params, sessionId}));
  });
}
const {targetId} = await send('Target.createTarget', {url: 'about:blank', background: true});
const {sessionId} = await send('Target.attachToTarget', {targetId, flatten: true});
const cdp = (method, params) => send(method, params, sessionId);
async function evaluate(expression) {
  const result = await cdp('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function until(expression) {
  const deadline = Date.now() + 8000;
  while (Date.now() < deadline) {
    if (await evaluate(expression)) return;
    await new Promise(r => setTimeout(r, 100));
  }
  throw new Error('Timed out: ' + expression);
}
const field = id => `document.getElementById(${JSON.stringify(id)})`;
const edit = async (on, off) => evaluate(`(() => {
  ${field('threshold-on')}.value = ${JSON.stringify(on)};
  ${field('threshold-off')}.value = ${JSON.stringify(off)};
  ${field('threshold-on')}.dispatchEvent(new Event('input', {bubbles:true}));
})()`);
try {
  await cdp('Network.enable');
  await cdp('Emulation.setDeviceMetricsOverride', {width:390, height:844, deviceScaleFactor:1, mobile:true});
  await cdp('Page.navigate', {url:base});
  await until(`${field('connection')}.textContent === '已连接'`);
  const initial = await evaluate(`({url:location.href,width:innerWidth,scrollWidth:document.documentElement.scrollWidth,
    on:${field('threshold-on')}.value,off:${field('threshold-off')}.value, demo:!${field('demo')}.hidden})`);
  assert.equal(initial.url, base + '/'); assert.equal(initial.scrollWidth,390);
  assert.equal(initial.on,'30'); assert.equal(initial.off,'28'); assert.equal(initial.demo,true);
  console.log('PASS initial mobile layout', initial);
  await edit('30','29.5');
  await evaluate(`${field('config-form')}.requestSubmit()`);
  await until(`${field('config-feedback')}.textContent.includes('请输入')`);
  assert.equal((await (await fetch(base+'/api/v1/state')).json()).onThresholdC,30);
  await edit('35.5','32');
  await new Promise(r => setTimeout(r,1200));
  assert.equal(await evaluate(`${field('threshold-on')}.value`),'35.5');
  await evaluate(`${field('config-form')}.requestSubmit()`);
  await until(`${field('config-feedback')}.textContent.includes('设备已确认保存')`);
  assert.equal((await (await fetch(base+'/api/v1/state')).json()).onThresholdC,35.5);
  console.log('PASS invalid input, draft polling and save confirmation');
  await edit('40','38');
  await cdp('Network.emulateNetworkConditions',{offline:true,latency:0,downloadThroughput:-1,uploadThroughput:-1});
  await until(`${field('connection')}.textContent === '未同步'`);
  assert.equal(await evaluate(`${field('save-config')}.disabled`),true);
  assert.equal(await evaluate(`${field('threshold-on')}.value`),'40');
  await cdp('Network.emulateNetworkConditions',{offline:false,latency:0,downloadThroughput:-1,uploadThroughput:-1});
  await until(`${field('connection')}.textContent === '已连接'`);
  assert.equal(await evaluate(`${field('threshold-on')}.value`),'40');
  assert.equal((await (await fetch(base+'/api/v1/state')).json()).onThresholdC,35.5);
  await evaluate(`${field('reset-config')}.click()`);
  assert.equal(await evaluate(`${field('threshold-on')}.value`),'35.5');
  console.log('PASS disconnect, reconnect, draft preservation and reset without saving');
  if (process.env.ESP32_UI_SCREENSHOT) {
    const shot = await cdp('Page.captureScreenshot',{captureBeyondViewport:true});
    await writeFile(process.env.ESP32_UI_SCREENSHOT,Buffer.from(shot.data,'base64'));
  }
} finally {
  await send('Target.closeTarget',{targetId});
  ws.close();
}
