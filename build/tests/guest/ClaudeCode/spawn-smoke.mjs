// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// spawn-smoke.mjs: the child_process path Claude Code's login takes to open
// the browser -- execFile($BROWSER, [url]) -- exercised on its own so a
// failure lands in SYS:Developer/ClaudeCode/spawn-state.json with the errno
// rather than as a silent "Browser didn't open?" in the TUI.
import { execFile, spawn } from 'node:child_process';
import fs from 'node:fs';

const t0 = Date.now();
const state = { steps: [] };
const step = (name, extra) => {
  const s = { name, ms: Date.now() - t0, ...extra };
  state.steps.push(s);
  console.log(`[spawn-smoke] ${JSON.stringify(s)}`);
  // Persist after every step: a trap later in the run must not hide what
  // already worked.
  try { fs.writeFileSync('SYS:Developer/ClaudeCode/spawn-state.json', JSON.stringify(state, null, 1)); } catch {}
};
const errinfo = (e) => e ? { code: e.code, errno: e.errno, syscall: e.syscall, path: e.path, message: e.message } : null;
const finish = () => {
  try { fs.writeFileSync('SYS:Developer/ClaudeCode/spawn-state.json', JSON.stringify(state, null, 1)); } catch (e) { console.log('[spawn-smoke] write failed: ' + e.message); }
  console.log('[spawn-smoke] SPAWN_SMOKE_DONE');
  process.exit(0);
};
setTimeout(() => { step('timeout'); finish(); }, 50000);

const run = (file, args, opts = {}) => new Promise((res) => {
  const t = Date.now();
  try {
    execFile(file, args, { timeout: 15000, ...opts }, (err, stdout, stderr) => {
      step('execFile', { file, args, tookMs: Date.now() - t, err: errinfo(err), stdout: String(stdout).slice(0, 200), stderr: String(stderr).slice(0, 200) });
      res();
    });
  } catch (e) { step('execFile-throw', { file, err: errinfo(e) }); res(); }
});

// 1. A trivial AROS command with output: proves pipes + exit code.
await run('C:Version', []);
// 2. Same, by bare name (PATH search through posixc execvp).
await run('Version', []);
// 3. (folded into 6: one browser launch per run -- a second chrome in the
//    same 8 GB guest cannot reserve its 1 GiB PA exec range and CHECK-fails.)
// 4. spawn() with stdio ignore + detached, the `open` package's shape.
await new Promise((res) => {
  const t = Date.now();
  let child;
  try {
    child = spawn('C:Version', ['C:Version'], { stdio: 'ignore', detached: true });
  } catch (e) { step('spawn-throw', { err: errinfo(e) }); return res(); }
  child.on('error', (e) => { step('spawn-detached-error', { tookMs: Date.now() - t, err: errinfo(e) }); res(); });
  child.on('exit', (code, sig) => { step('spawn-detached-exit', { tookMs: Date.now() - t, code, sig }); res(); });
});
// 5. Claude Code's w1() passes cwd: process.cwd() (its b8()); uv_spawn
//    chdir()s to it in the pretend-child, so a cwd chdir() must not fail.
step('cwd', { cwd: process.cwd() });
await run('C:Version', [], { cwd: process.cwd() });
await run('C:Version', [], { cwd: 'SYS:' });
// 6. What Claude Code does: $BROWSER = C:OpenURL with the URL as argv[1].
//    The real thing: Claude Code's OAuth URL (431 chars, query string with
//    &, =, %-escapes) through the exact w1() option shape.  The short
//    example.com URL in step 3 hides every argument-length limit.
const oauth = 'https://claude.com/cai/oauth/authorize?code=true&client_id=9d1c250a-e61b-44d9-88ed-5944d1962f5e&response_type=code&redirect_uri=http%3A%2F%2Flocalhost%3A1031%2Fcallback&scope=org%3Acreate_api_key+user%3Aprofile+user%3Ainference+user%3Asessions%3Aclaude_code&code_challenge=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx&code_challenge_method=S256&state=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx';
await run(process.env.BROWSER || 'C:OpenURL', [oauth], { maxBuffer: 1e6, timeout: 600000, cwd: process.cwd() });
finish();
