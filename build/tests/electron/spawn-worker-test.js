// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// Does child_process.spawn work from a worker_threads Worker on AROS?
//
// VS Code's extension host runs as utilityProcess.fork, which ElectronShell
// implements as a worker_threads Worker inside the ElectronShell process;
// the Claude Code extension's "C:Node <shim>" launch failed there with
// "spawn EBADF" (run 22).  This probe separates the thread from the API:
// spawnSync and spawn, on the main thread and in a Worker, results appended
// to SYS:Developer/Chromium/spawn-worker-test.log (the guest input channel
// runs commands with NIL: stdio, so console output would be lost).
//
// Run (guest input channel): C:Node SYS:Developer/Chromium/spawn-worker-test.js
'use strict'
const { Worker, isMainThread, parentPort } = require('node:worker_threads')
const { spawn, spawnSync } = require('node:child_process')
const { appendFileSync } = require('node:fs')

const LOG = 'SYS:Developer/Chromium/spawn-worker-test.log'
const log = line => { try { appendFileSync(LOG, line + '\n') } catch {} }

function probeSync (label) {
  const r = spawnSync('C:Version', [], { encoding: 'utf8' })
  return `${label} spawnSync: status=${r.status} error=${r.error ? r.error.code : 'none'} out=${(r.stdout || '').trim().slice(0, 60)}`
}

function probeAsync (label) {
  return new Promise(resolve => {
    let out = ''
    let child
    try {
      child = spawn('C:Version', [], { stdio: ['pipe', 'pipe', 'pipe'] })
    } catch (e) {
      resolve(`${label} spawn: threw ${e.code || e.message}`)
      return
    }
    child.stdout.on('data', d => { out += d })
    child.on('error', e => resolve(`${label} spawn: error=${e.code} (${e.message})`))
    child.on('close', code => resolve(`${label} spawn: close code=${code} out=${out.trim().slice(0, 60)}`))
  })
}

if (isMainThread) {
  log(`--- ${new Date().toISOString()} pid=${process.pid}`)
  log(probeSync('main'))
  probeAsync('main').then(line => {
    log(line)
    const w = new Worker(__filename)
    w.on('message', m => { log(m); if (m.startsWith('worker spawn:')) { log('done'); process.exit(0) } })
    w.on('error', e => { log('worker error ' + e.stack); process.exit(1) })
  })
} else {
  parentPort.postMessage(probeSync('worker'))
  probeAsync('worker').then(line => parentPort.postMessage(line))
}
