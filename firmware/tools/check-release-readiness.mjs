import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const defaultIssues = fileURLToPath(new URL('../../docs/issues/', import.meta.url));
const requiredCount = 25;

export async function auditIssues(directory = defaultIssues) {
  const results = [];
  for (let number = 1; number <= requiredCount; number += 1) {
    const id = `ESP-${String(number).padStart(3, '0')}`;
    let source;
    try {
      source = await readFile(join(directory, `${id}.md`), 'utf8');
    } catch {
      results.push({ id, reasons: ['工单文件缺失或不可读取'] });
      continue;
    }
    const reasons = [];
    const statuses = [...source.matchAll(/^- 状态：(.+)$/gm)];
    if (statuses.length !== 1 || statuses[0][1].trim() !== '已完成')
      reasons.push('状态未标为已完成');
    const acceptance = [...source.matchAll(/^- \[([ xX])\] /gm)];
    if (acceptance.length === 0) reasons.push('缺少验收勾选项');
    else if (acceptance.some((item) => item[1] === ' ')) reasons.push('存在未勾选验收项');
    const record = source.split(/^## 完成记录[ \t]*$/m)[1]?.split(/^## /m)[0]?.trim();
    if (!record || record.includes('实施时填写')) reasons.push('完成记录未填写');
    results.push({ id, reasons });
  }
  return { ready: results.every((item) => item.reasons.length === 0), results };
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  if (process.argv.length !== 2) {
    console.error('Usage: node firmware/tools/check-release-readiness.mjs');
    process.exitCode = 2;
  } else {
    const audit = await auditIssues();
    const passed = audit.results.filter((item) => item.reasons.length === 0).length;
    console.log(`必需工单验收记录：${passed}/${requiredCount}`);
    for (const item of audit.results) {
      if (item.reasons.length) console.log(`${item.id}: ${item.reasons.join('；')}`);
    }
    console.log(audit.ready ? '工单文档门槛已满足；仍需人工核对证据与产物。' :
      '尚未满足首版归档的工单文档门槛。');
    if (!audit.ready) process.exitCode = 1;
  }
}
