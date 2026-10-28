import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { auditIssues } from '../tools/check-release-readiness.mjs';

test('release gate requires every mandatory issue, accepted checklist and completion evidence', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'esp32-release-audit-'));
  const file = (n) => join(directory, `ESP-${String(n).padStart(3, '0')}.md`);
  const completed = '# 工单\n\n- 状态：已完成\n\n## 验收步骤与预期\n\n- [x] 实物通过\n\n## 完成记录\n\n固件版本与验证证据已登记。\n';
  try {
    for (let n = 1; n <= 25; n += 1) await writeFile(file(n), completed);
    let audit = await auditIssues(directory);
    assert.equal(audit.ready, true);
    assert.equal(audit.results.length, 25);

    await writeFile(file(1), completed.replace('已完成', '进行中'));
    await writeFile(file(2), completed.replace('[x]', '[ ]'));
    await writeFile(file(3), completed.replace('固件版本与验证证据已登记。', '实施时填写'));
    await rm(file(4));
    audit = await auditIssues(directory);
    assert.equal(audit.ready, false);
    assert.deepEqual(audit.results.filter((item) => item.reasons.length).map((item) => item.id),
      ['ESP-001', 'ESP-002', 'ESP-003', 'ESP-004']);
    assert.match(audit.results[0].reasons.join(), /状态/);
    assert.match(audit.results[1].reasons.join(), /未勾选/);
    assert.match(audit.results[2].reasons.join(), /完成记录/);
    assert.match(audit.results[3].reasons.join(), /缺失/);
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
