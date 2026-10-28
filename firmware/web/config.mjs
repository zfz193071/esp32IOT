export function validConfig(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value) &&
    Object.keys(value).length === 2 &&
    Number.isFinite(value.onThresholdC) && Number.isFinite(value.offThresholdC) &&
    value.onThresholdC >= 0 && value.onThresholdC <= 60 &&
    value.offThresholdC >= 0 && value.offThresholdC <= 60 &&
    value.onThresholdC - value.offThresholdC >= 1;
}

export class ConfigDraft {
  constructor() {
    this.loaded = null;
    this.on = '';
    this.off = '';
    this.dirty = false;
  }
  observe(state) {
    this.loaded = { onThresholdC: state.onThresholdC, offThresholdC: state.offThresholdC };
    if (!this.dirty) this.reset();
  }
  edit(on, off) { this.on = on; this.off = off; this.dirty = true; }
  reset() {
    if (!this.loaded) return;
    this.on = String(this.loaded.onThresholdC);
    this.off = String(this.loaded.offThresholdC);
    this.dirty = false;
  }
  value() {
    if (!this.on.trim() || !this.off.trim()) return null;
    const value = { onThresholdC: Number(this.on), offThresholdC: Number(this.off) };
    return validConfig(value) ? value : null;
  }
  confirm(value) {
    if (!this.loaded || this.loaded.onThresholdC !== value.onThresholdC ||
        this.loaded.offThresholdC !== value.offThresholdC) return false;
    this.reset();
    return true;
  }
}
