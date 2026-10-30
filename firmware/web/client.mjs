import { validConfig } from './config.mjs';

// These fields advance the device revision; sampleAgeMs alone does not.
const revisionFields = ['mode', 'fanOutputOn', 'temperatureC', 'sensorStatus',
  'onThresholdC', 'offThresholdC', 'configSaveBlocked', 'fault', 'lastCommandSource', 'wifiConnected'];

export const storageUnknownMessage = '保存结果未知，设备当前仍使用原阈值；已暂停配置保存，请重启设备后核实实际保存值';

export function validState(s) {
  if (!s || typeof s !== 'object' || Array.isArray(s)) return false;
  const healthy = s.sensorStatus === 'ok';
  const validTemperature = healthy
    ? Number.isFinite(s.temperatureC) && Number.isSafeInteger(s.sampleAgeMs) &&
      s.sampleAgeMs >= 0 && s.sampleAgeMs <= 5000
    : s.temperatureC === null;
  return typeof s.bootId === 'string' && s.bootId.length > 0 && s.bootId.length <= 128 &&
    /^[\x00-\x7f]+$/.test(s.bootId) &&
    Number.isSafeInteger(s.revision) && s.revision >= 0 &&
    ['manual', 'auto'].includes(s.mode) && typeof s.fanOutputOn === 'boolean' &&
    ['initializing', 'ok', 'invalid', 'stale'].includes(s.sensorStatus) &&
    (s.sampleAgeMs === null || (Number.isSafeInteger(s.sampleAgeMs) && s.sampleAgeMs >= 0)) &&
    validTemperature && validConfig({ onThresholdC: s.onThresholdC, offThresholdC: s.offThresholdC }) &&
    ['none', 'temperature_unavailable'].includes(s.fault) &&
    (s.mode !== 'auto' || (healthy && s.fault === 'none')) &&
    (!healthy || s.fault === 'none') &&
    ['button', 'infrared', 'web', 'system'].includes(s.lastCommandSource) &&
    typeof s.wifiConnected === 'boolean' && typeof s.configSaveBlocked === 'boolean';
}

export class DeviceClient {
  constructor({ fetcher = globalThis.fetch.bind(globalThis), timeoutMs = 4000,
    now = Date.now, onChange = () => {} } = {}) {
    this.fetcher = fetcher;
    this.timeoutMs = timeoutMs;
    this.now = now;
    this.onChange = onChange;
    this.state = null;
    this.connected = false;
    this.busy = false;
    this.message = '正在连接设备…';
    this.lastSync = null;
    this.epoch = 0;
    this.readId = 0;
    this.reading = false;
    this.retiredBoots = new Set();
  }

  notify() {
    this.onChange({ state: this.state ? { ...this.state } : null,
      connected: this.connected, busy: this.busy,
      message: this.message, lastSync: this.lastSync });
  }

  async request(path, options = {}) {
    const controller = new AbortController();
    let timer;
    // Race also covers body parsing; a timed-out response cannot later apply state.
    const deadline = new Promise((_, reject) => {
      timer = setTimeout(() => {
        controller.abort();
        reject(new Error('请求超时'));
      }, this.timeoutMs);
    });
    try {
      return await Promise.race([deadline, (async () => {
        const response = await this.fetcher(path, { ...options,
          signal: controller.signal, cache: 'no-store', credentials: 'same-origin' });
        const body = await response.json();
        if (!response.ok) {
          const storageFailure = response.status === 503 && body?.error?.code === 'STORAGE_FAILURE';
          const storageUnknown = response.status === 503 && body?.error?.code === 'STORAGE_UNKNOWN';
          const error = new Error(storageUnknown ? storageUnknownMessage : storageFailure ? '保存失败，设备保留原配置' :
            body?.error?.code === 'TEMPERATURE_UNAVAILABLE'
              ? '温度不可用，无法开启自动模式' : `设备拒绝请求（${response.status}）`);
          error.rejected = storageUnknown || storageFailure || (response.status >= 400 && response.status < 500);
          throw error;
        }
        if (!validState(body)) throw new Error('设备状态格式无效');
        return body;
      })()]);
    } finally {
      clearTimeout(timer);
    }
  }

  apply(next) {
    if (this.retiredBoots.has(next.bootId)) return false;
    if (this.state && next.bootId === this.state.bootId &&
        next.revision < this.state.revision) return false;
    if (this.state && next.bootId === this.state.bootId &&
        this.state.configSaveBlocked && !next.configSaveBlocked) return false;
    if (this.state && next.bootId === this.state.bootId &&
        next.revision === this.state.revision &&
        revisionFields.some((field) => next[field] !== this.state[field])) return false;
    if (this.state && next.bootId !== this.state.bootId) {
      this.retiredBoots.add(this.state.bootId);
    }
    this.state = next;
    this.connected = true;
    this.lastSync = this.now();
    return true;
  }

  async refresh({ internal = false } = {}) {
    if (!internal && (this.busy || this.reading)) return false;
    const epoch = this.epoch;
    const id = ++this.readId;
    this.reading = true;
    try {
      const next = await this.request('/api/v1/state');
      if (epoch !== this.epoch || id !== this.readId) return false;
      const rebooted = this.state && this.state.bootId !== next.bootId;
      if (!this.apply(next)) throw new Error('收到旧状态或版本冲突，等待重新同步');
      this.message = next.configSaveBlocked ? storageUnknownMessage :
        rebooted ? '设备已重启，已同步新状态' : '状态已同步';
      return true;
    } catch (error) {
      if (epoch !== this.epoch || id !== this.readId) return false;
      this.connected = false;
      this.message = `设备失联或同步失败：${error.message}。显示最后同步记录。`;
      return false;
    } finally {
      if (id === this.readId) this.reading = false;
      this.notify();
    }
  }

  async write(kind, value) {
    if (this.busy || !this.connected) return false;
    if (kind === 'config' && this.state.configSaveBlocked) return false;
    if (!((kind === 'power' && typeof value === 'boolean') ||
          (kind === 'mode' && ['manual', 'auto'].includes(value)) ||
          (kind === 'config' && validConfig(value)))) return false;
    this.busy = true;
    // Invalidate a pre-command poll, including a late failure from that poll.
    ++this.epoch;
    ++this.readId;
    this.reading = false;
    this.message = '正在提交，等待设备确认…';
    this.notify();
    let accepted = false;
    let notice = '';
    try {
      const next = await this.request(`/api/v1/${kind}`, { method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(kind === 'power' ? { on: value } : kind === 'config' ? value : { mode: value }) });
      if (this.retiredBoots.has(next.bootId)) {
        notice = '收到旧响应，结果待同步';
      } else if (next.bootId !== this.state.bootId) {
        // A write crossing a reboot cannot establish that the requested action won.
        // Remember the observed restart even before its confirming GET succeeds.
        this.retiredBoots.add(this.state.bootId);
        this.connected = false;
        notice = '设备在请求期间重启；命令不重发';
      } else if (this.apply(next)) {
        accepted = true;
      } else {
        notice = '收到旧响应或版本冲突，结果待同步';
      }
    } catch (error) {
      this.connected = false;
      notice = error.rejected ? error.message : '操作结果未知，正在同步；不会自动重发命令';
    }
    if (notice) {
      this.message = notice;
      this.notify();
    }
    try {
      await this.refresh({ internal: true });
      if (notice) this.message = `${notice}。${this.message}`;
    } finally {
      this.busy = false;
      this.notify();
    }
    return accepted;
  }
}
