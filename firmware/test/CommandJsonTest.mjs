import test from 'node:test';
import assert from 'node:assert/strict';
import { parseCommand } from '../tools/parse-command.mjs';

test('simulator flat commands reject duplicate decoded names without confusing string values', () => {
  for (const body of ['{"on":false,"on":true}', '{"on":false,"\\u006fn":true}',
    '{"onThresholdC":30,"offThresholdC":28,"onThresholdC":40}',
    'null', '[]', '{"on":{}}', '{"on":[]}', '{"on":true,}', '{"on":true}{}']) {
    assert.throws(() => parseCommand(body), undefined, body);
  }
  assert.deepEqual(parseCommand(' { "\\u006fn" : true } '), { on: true });
  assert.deepEqual(parseCommand('{"offThresholdC":2.8e1,"onThresholdC":30}'),
    { offThresholdC: 28, onThresholdC: 30 });
  assert.deepEqual(parseCommand('{"temperatureC":null}'), { temperatureC: null });
  assert.deepEqual(parseCommand('{"mode":"auto"}'), { mode: 'auto' });
  const tricky = { mode: 'text "mode": with \\ escape', extra: 'mode' };
  assert.deepEqual(parseCommand(JSON.stringify(tricky)), tricky);
});
