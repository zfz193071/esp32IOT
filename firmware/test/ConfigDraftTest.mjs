import test from 'node:test';
import assert from 'node:assert/strict';
import { ConfigDraft, validConfig } from '../web/config.mjs';

test('polls and reboot snapshots update loaded values without destroying edits', () => {
  const draft = new ConfigDraft();
  draft.observe({ onThresholdC: 30, offThresholdC: 28 });
  assert.equal(draft.on, '30');
  draft.edit('35.5', '32');
  draft.observe({ bootId: 'new-boot', onThresholdC: 40, offThresholdC: 38 });
  assert.equal(draft.on, '35.5');
  assert.equal(draft.off, '32');
  assert.equal(draft.loaded.onThresholdC, 40);
  assert.equal(draft.confirm(draft.value()), false);
  draft.reset();
  assert.equal(draft.on, '40');
  assert.equal(draft.dirty, false);
});

test('blank and invalid drafts cannot be saved; matching confirmation clears dirty state', () => {
  const draft = new ConfigDraft();
  draft.observe({ onThresholdC: 30, offThresholdC: 28 });
  for (const [on, off] of [['', '0'], [' ', '0'], ['30', ''], ['Infinity', '28'], ['30', '29.5'], ['61', '28']]) {
    draft.edit(on, off);
    assert.equal(draft.value(), null);
  }
  draft.edit('35.5', '32');
  const value = draft.value();
  draft.observe(value);
  assert.equal(draft.dirty, true);
  assert.equal(draft.confirm(value), true);
  assert.equal(draft.dirty, false);
  assert.ok(validConfig({ onThresholdC: 1, offThresholdC: 0 }));
  assert.ok(validConfig({ onThresholdC: 60, offThresholdC: 59 }));
});
