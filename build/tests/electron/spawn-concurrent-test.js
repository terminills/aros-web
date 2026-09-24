// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// Concurrent child_process.spawn from the main thread and two worker_threads
// Workers at once, with descriptor churn on the main thread meanwhile.
//
// spawn-worker-test.js proves each shape in isolation; this is the load shape
// VS Code actually produces (the extension host Worker spawning language
// servers and the Claude Code shim while the main thread spawns git/ripgrep
// and opens/closes files).  It exercises the two things fixed in
// posixc: the per-Task vfork()/exec*() context (__taskctx.c), and the fd
// close ordering + locked table copy (__unlinkfdesc/__copy_fdarray) that a
// concurrent close() raced.  Results appended to
// SYS:Developer/Chromium/spawn-concurrent-test.log.
//
// Run (guest input channel): C:Node SYS:Developer/Chromium/spawn-concurrent-test.js [rounds]
'use strict'
const { Worker, isMainThread, parentPort, threadId } = require('node:worker_threads')
const { spawn } = require('node:child_process')
const { appendFileSync, openSync, closeSync, writeSync } = require('node:fs')

const LOG = 'SYS:Developer/Chromium/spawn-concurrent-test.log'
const ROUNDS = Number(process.argv[2]) || 8   // sequential rounds per thread
const PAR = 3          // concurrent spawns per round per thread
const log = line => { try { appendFileSync(LOG, line + '\n') } catch {} }

function one (label) {
  return new Promise(resolve => {
    let out = ''
    let child
    try {
      child = spawn('C:Version', [], { stdio: ['pipe', 'pipe', 'pipe'] })
    } catch (e) {
      resolve({ ok: false, why: `${label} threw ${e.code || e.message}` })
      return
    }
    child.stdout.on('data', d => { out += d })
    child.on('error', e => resolve({ ok: false, why: `${label} error=${e.code} (${e.message})` }))
    child.on('close', code => resolve({ ok: code === 0 && /Kickstart/.test(out), why: `${label} close code=${code} out=${out.trim().slice(0, 40)}` }))
  })
}

async function burst (label) {
  let fails = 0
  const bad = []
  for (let r = 0; r < ROUNDS; r++) {
    const rs = await Promise.all(Array.from({ length: PAR }, (_, i) => one(`${label} r${r}.${i}`)))
    for (const x of rs) if (!x.ok) { fails++; if (bad.length < 5) bad.push(x.why) }
  }
  return `${label}: ${ROUNDS * PAR} spawns, ${fails} failed${bad.length ? ' [' + bad.join('; ') + ']' : ''}`
}

if (isMainThread) {
  log(`--- ${new Date().toISOString()} pid=${process.pid} rounds=${ROUNDS} par=${PAR}`)
  // Descriptor churn while the Workers copy the table for their launchers:
  // this is the close()-during-__copy_fdarray race of spawn-worker-fix2.
  let churn = 0
  const churnTimer = setInterval(() => {
    try {
      const fd = openSync('SYS:Developer/Chromium/spawn-concurrent-churn.tmp', 'w')
      writeSync(fd, 'x')
      closeSync(fd)
      churn++
    } catch (e) { log('churn error ' + (e.code || e.message)) }
  }, 5)
  const results = []
  let pending = 3
  const finish = () => {
    if (--pending) return
    clearInterval(churnTimer)
    burst('main').then(line => {
      results.push(line)
      log(`churn cycles=${churn}`)
      for (const l of results) log(l)
      log(results.every(l => / 0 failed/.test(l)) ? 'PASS' : 'FAIL')
      process.exit(0)
    })
  }
  // Two Workers spawning against the same parent at once, then main after
  // them -- and main also spawns *during* the Workers' bursts (below).
  for (const n of [1, 2]) {
    const w = new Worker(__filename, { argv: process.argv.slice(2) })
    w.on('message', m => { results.push(m); finish() })
    w.on('error', e => { log(`worker${n} error ` + e.stack); process.exit(1) })
  }
  burst('main-overlap').then(line => { results.push(line); finish() })
} else {
  burst(`worker${threadId}`).then(line => parentPort.postMessage(line))
}
