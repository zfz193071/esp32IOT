import { spawn } from 'node:child_process';

export function inspectionSucceeded(result) {
  return result.code === 0 && !result.signal && !result.timedOut && !result.cancelled && !result.spawnError &&
    !result.preflightError && !result.stdoutTruncated && !result.stderrTruncated;
}

// POSIX only, matching inspect-board's supported hosts. The dedicated process
// group also covers a standalone esptool launcher's own child process.
export function captureInspectionProcess(executable, args, {
  cwd, env = process.env, timeoutMs = 45000, killGraceMs = 1000, maxOutputBytes = 65536, abortSignal,
} = {}) {
  if (!['darwin', 'linux'].includes(process.platform)) throw new Error('Inspection requires macOS or Linux');
  for (const value of [timeoutMs, killGraceMs, maxOutputBytes])
    if (!Number.isSafeInteger(value) || value <= 0) throw new Error('Invalid process capture limit');
  const limits = { timeoutMs, killGraceMs, maxOutputBytesPerStream: maxOutputBytes };
  if (abortSignal?.aborted) return Promise.resolve({
    code: null, signal: null, timedOut: false, forcedKill: false, spawnError: null,
    cancelled: true, cancelReason: String(abortSignal.reason ?? 'aborted'),
    stdout: '', stderr: '', stdoutTruncated: false, stderrTruncated: false, durationMs: 0, limits,
  });
  return new Promise((resolve) => {
    const startedAt = performance.now();
    const output = { stdout: [], stderr: [] };
    const retained = { stdout: 0, stderr: 0 };
    const truncated = { stdout: false, stderr: false };
    let timedOut = false, forcedKill = false, spawnError = null;
    let cancelled = false, cancelReason = null, spawned = false, stopping = false;
    let deadline, escalation;
    const child = spawn(executable, args, { cwd, env, detached: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const signalGroup = (signal) => {
      // Only this newly created process group; never an existing shell/session.
      try { process.kill(-child.pid, signal); }
      catch (error) { if (error.code !== 'ESRCH') spawnError = { code: error.code, message: error.message }; }
    };
    const terminate = () => {
      if (stopping) return;
      stopping = true;
      clearTimeout(deadline);
      signalGroup('SIGTERM');
      escalation = setTimeout(() => {
        forcedKill = true;
        signalGroup('SIGKILL');
      }, killGraceMs);
    };
    const cancel = () => {
      cancelled = true;
      cancelReason = String(abortSignal.reason ?? 'aborted');
      if (spawned) terminate();
    };
    abortSignal?.addEventListener('abort', cancel, { once: true });
    child.once('spawn', () => {
      spawned = true;
      if (abortSignal?.aborted) cancel();
      else deadline = setTimeout(() => { timedOut = true; terminate(); }, timeoutMs);
    });
    for (const name of ['stdout', 'stderr']) child[name].on('data', (chunk) => {
      const remaining = maxOutputBytes - retained[name];
      const accepted = chunk.subarray(0, remaining);
      if (accepted.length) output[name].push(Buffer.from(accepted));
      retained[name] += accepted.length;
      if (accepted.length < chunk.length) truncated[name] = true;
    });
    child.once('error', (error) => { spawnError = { code: error.code, message: error.message }; });
    child.once('close', (code, signal) => {
      clearTimeout(deadline);
      clearTimeout(escalation);
      abortSignal?.removeEventListener('abort', cancel);
      resolve({ code, signal, timedOut, forcedKill, spawnError, cancelled, cancelReason,
        stdout: Buffer.concat(output.stdout).toString('utf8'),
        stderr: Buffer.concat(output.stderr).toString('utf8'),
        stdoutTruncated: truncated.stdout, stderrTruncated: truncated.stderr,
        durationMs: Math.ceil(performance.now() - startedAt),
        limits,
      });
    });
  });
}
