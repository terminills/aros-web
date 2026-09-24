// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// Cross-task libuv loop: node's Watchdog (src/node_watchdog.cc) calls
// uv_loop_init() in its constructor on the MAIN task, uv_run()s that loop on
// its own thread, and uv_loop_close()s it from the main task again.  uv1's
// loop exec resources (timer port signal, async signal, timer.device open)
// must therefore belong to whichever task polls the loop, and a close from
// another task must not free bits out of the closer's own allocation
// (workbench/libs/uv/src-aros/aros.c uv__aros_loop_bind/unbind).
//
// A vm timeout is the only mainline node feature that runs the Watchdog.
// Expect: "timeout ok ERR_SCRIPT_EXECUTION_TIMEOUT" twice, "done", and no
// "*** 'C:Node' returned with unfreed signal" from the Shell afterwards.
// Results are appended to SYS:Developer/Chromium/vm-timeout-test.log.
//
// Run: C:Node SYS:Developer/Chromium/vm-timeout-test.js
'use strict'
const vm = require('node:vm')
const { appendFileSync } = require('node:fs')

const LOG = 'SYS:Developer/Chromium/vm-timeout-test.log'
const log = (line) => {
  appendFileSync(LOG, line + '\n')
  console.log(line)
}

log(`--- ${new Date().toISOString()} pid=${process.pid}`)
for (let i = 0; i < 2; i++) {
  const t0 = Date.now()
  try {
    vm.runInNewContext('while (true) {}', {}, { timeout: 200 })
    log(`run ${i}: returned (no timeout!) after ${Date.now() - t0} ms`)
  } catch (e) {
    log(`run ${i}: timeout ok ${e.code || e.message} after ${Date.now() - t0} ms`)
  }
}
log('done')
