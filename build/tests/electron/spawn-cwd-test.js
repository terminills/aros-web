// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// Does a spawn with a `cwd` survive a nested vfork() on AROS?
//
// Run 26 of the desktop VS Code gate: the extension host (a Worker) spawned
// the Claude CLI ("C:Node <shim>", cwd = the workspace), that CLI vfork()ed
// its first child, and emul-handler freed the same 40-byte FileLock twice
// (ACTION_FREE_LOCK) -- the whole guest died.  None of the earlier spawn
// probes passed cwd, and none spawned from a process that was itself
// exec()ed by a vfork() launcher.  This probe does both, in stages, so the
// log shows which one trips the double UnLock:
//
//   main  (started by the Shell)   spawns "C:Node <self> child" with cwd,
//                                  from the main thread and from a Worker;
//   child (runs inside a launcher) spawns C:Version with cwd, sync and
//                                  async, a bare name that fails PATH
//                                  lookup, then process.chdir() + spawn,
//                                  then the same from a Worker.
//
// Results are appended to SYS:Developer/Chromium/spawn-cwd-test.log.
//
// Run: C:Node SYS:Developer/Chromium/spawn-cwd-test.js
'use strict'
const { Worker, isMainThread, parentPort, workerData } = require('node:worker_threads')
const { spawn, spawnSync } = require('node:child_process')
const { appendFileSync } = require('node:fs')

const LOG = 'SYS:Developer/Chromium/spawn-cwd-test.log'
const mode = workerData ? workerData.mode : (process.argv[2] || 'main')
const tag = `${mode}${isMainThread ? '' : '/worker'} pid=${process.pid}`
const log = line => { try { appendFileSync(LOG, `${tag}: ${line}\n`) } catch {} }

function runSync (label, cmd, args, opts) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', ...opts })
  log(`${label} spawnSync: status=${r.status} error=${r.error ? r.error.code : 'none'} out=${(r.stdout || '').trim().slice(0, 60)}`)
}

function runAsync (label, cmd, args, opts) {
  return new Promise(resolve => {
    let out = ''
    let child
    try {
      child = spawn(cmd, args, { stdio: ['pipe', 'pipe', 'pipe'], ...opts })
    } catch (e) {
      log(`${label} spawn: threw ${e.code || e.message}`)
      resolve()
      return
    }
    child.stdout.on('data', d => { out += d })
    child.stderr.on('data', d => { out += d })
    child.on('error', e => { log(`${label} spawn: error=${e.code} (${e.message})`); resolve() })
    child.on('close', code => {
      // node.library's own bring-up traces share the child's stderr; drop them.
      const text = out.split('\n').filter(l => !/^\[Node[A-Za-z0-9]*:/.test(l)).join(' ').replace(/\s+/g, ' ').trim()
      log(`${label} spawn: close code=${code} out=${text.slice(0, 400)}`)
      resolve()
    })
  })
}

// The child-side probes; run on the child's main thread and in its Worker.
async function childProbes () {
  runSync('version cwd=SYS:C', 'C:Version', [], { cwd: 'SYS:C' })
  await runAsync('version cwd=SYS:C', 'C:Version', [], { cwd: 'SYS:C' })
  await runAsync('bare-name-missing cwd=SYS:S', 'NoSuchCommandAnywhere', [], { cwd: 'SYS:S' })
  runSync('version cwd=SYS:NoSuchDir', 'C:Version', [], { cwd: 'SYS:NoSuchDir' })
  if (isMainThread) {
    try { process.chdir('SYS:S'); log(`chdir SYS:S -> cwd=${process.cwd()}`) } catch (e) { log(`chdir threw ${e.code}`) }
    runSync('version after chdir, cwd=SYS:Libs', 'C:Version', [], { cwd: 'SYS:Libs' })
    await runAsync('version after chdir, no cwd', 'C:Version', [], {})
  }
  log('probes done')
}

function runWorker (m) {
  return new Promise(resolve => {
    const w = new Worker(__filename, { workerData: { mode: m } })
    w.on('message', resolve)
    w.on('error', e => { log(`worker error ${e.stack}`); resolve() })
  })
}

async function main () {
  if (mode === 'main') {
    if (isMainThread) {
      log(`--- ${new Date().toISOString()}`)
      await runAsync('child cwd=SYS:Developer', 'C:Node', [__filename, 'child'], { cwd: 'SYS:Developer' })
      await runWorker('main')
      log('done')
      process.exit(0)
    } else {
      await runAsync('child cwd=SYS:Developer', 'C:Node', [__filename, 'child'], { cwd: 'SYS:Developer' })
      parentPort.postMessage('ok')
    }
  } else if (mode === 'child') {
    if (isMainThread) {
      log(`cwd=${process.cwd()}`)
      await childProbes()
      await runWorker('child')
      log('child exit')
      process.exit(0)
    } else {
      await childProbes()
      parentPort.postMessage('ok')
    }
  }
}

main().catch(e => { log(`failed ${e.stack}`); process.exit(1) })
