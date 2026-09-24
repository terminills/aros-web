// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// Does a spawned C:Node child start, and does it see the env the parent
// passed?  The Claude Code extension launches "C:Node <shim>" with a
// custom env (CLAUDE_CODE_*), so both halves matter: uv_spawn swaps
// `environ = options->env` around execvp, but posixc execvp reads
// *__posixc_get_environptr(), which is only the same thing if the task's
// environptr resolves to uv1's global.  Results are appended to
// SYS:Developer/Chromium/spawn-env-test.log (the guest input channel runs
// commands with NIL: stdio, so console output would be lost).
//
// Run (guest input channel, file-backed stdio):
//   C:Node >SYS:Developer/Chromium/node-stdout.txt
//     <SYS:Developer/Chromium/node-stdin.txt
//     SYS:Developer/Chromium/spawn-env-test.js
'use strict'
const { spawnSync } = require('node:child_process')
const { appendFileSync } = require('node:fs')

const LOG = 'SYS:Developer/Chromium/spawn-env-test.log'
const log = line => { try { appendFileSync(LOG, line + '\n') } catch {} }

log(`--- ${new Date().toISOString()} pid=${process.pid} SPAWN_ENV_PROBE=${process.env.SPAWN_ENV_PROBE || 'unset'}`)

// 1. inherit: child sees the parent's own environment
let r = spawnSync('C:Node', ['-e', 'process.stdout.write(String(Object.keys(process.env).length))'], { encoding: 'utf8' })
log(`inherit: status=${r.status} error=${r.error ? r.error.code : 'none'} keys=${(r.stdout || '').trim()} stderr=${(r.stderr || '').trim().slice(0, 80)}`)

// 2. explicit env: child must see SPAWN_ENV_PROBE=from-parent and nothing else of note
r = spawnSync('C:Node', ['-e', 'process.stdout.write((process.env.SPAWN_ENV_PROBE || "unset") + " " + Object.keys(process.env).length)'],
  { encoding: 'utf8', env: { SPAWN_ENV_PROBE: 'from-parent', PATH: process.env.PATH || '' } })
log(`explicit: status=${r.status} error=${r.error ? r.error.code : 'none'} out=${(r.stdout || '').trim()} stderr=${(r.stderr || '').trim().slice(0, 80)}`)

// 3. parent's own env must be untouched afterwards
log(`parent after: SPAWN_ENV_PROBE=${process.env.SPAWN_ENV_PROBE || 'unset'} keys=${Object.keys(process.env).length}`)
log('done')
