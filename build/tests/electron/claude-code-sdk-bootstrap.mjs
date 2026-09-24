/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * AROS Claude Code bootstrap for the VS Code extension (SDK mode).
 *
 * The extension talks to the CLI over stdin/stdout as NDJSON
 * (--input-format/--output-format stream-json), so unlike the interactive
 * bootstrap (SYS:Developer/ClaudeCode/claude_aros_bootstrap.mjs) this one
 * must not print anything of its own to stdout.  Everything else is the
 * same: uv_signal_init() does not yet implement Node's SignalWrap contract
 * on AROS, and Claude Code registers Unix signal listeners during startup
 * that Node would otherwise turn into a fatal assertion, so signal listener
 * registration is suppressed here.  The upstream CLI stays immutable.
 *
 * Staged as SYS:Developer/ClaudeCode/claude_aros_sdk_bootstrap.mjs by
 * stage-claude-code-extension.sh; reached through the extension's
 * resources/native-binary/claude shim (claude-code-native-shim.cjs).
 */
import { appendFileSync, writeFileSync } from 'node:fs'

const logDir = 'SYS:Developer/ClaudeCode'
const stderrLog = `${logDir}/claude-sdk-stderr.log`
const stdoutLog = `${logDir}/claude-sdk-stdout.log`
const stdinLog = `${logDir}/claude-sdk-stdin.log`

for (const f of [stderrLog, stdoutLog, stdinLog]) {
  try {
    writeFileSync(f, '')
  } catch {}
}

// Mirror the protocol streams to files (no banner on stdout: the extension
// parses it).
const mirrorWrites = (stream, file) => {
  const originalWrite = stream.write.bind(stream)
  stream.write = function (chunk, encoding, callback) {
    try {
      appendFileSync(file, chunk)
    } catch {}
    return originalWrite(chunk, encoding, callback)
  }
}
mirrorWrites(process.stdout, stdoutLog)
mirrorWrites(process.stderr, stderrLog)
process.stdin.on('data', chunk => {
  try {
    appendFileSync(stdinLog, chunk)
  } catch {}
})

try {
  appendFileSync(
    stderrLog,
    `[AROS sdk bootstrap] argv=${JSON.stringify(process.argv.slice(2))} cwd=${process.cwd()}\n`
  )
} catch {}

const isSignalEvent = event =>
  typeof event === 'string' && event.startsWith('SIG')

const originalOn = process.on
const originalOnce = process.once

process.on = process.addListener = function (event, listener) {
  if (isSignalEvent(event)) {
    process.stderr.write(`[AROS] deferred signal listener: ${event}\n`)
    return this
  }
  return originalOn.call(this, event, listener)
}

process.once = function (event, listener) {
  if (isSignalEvent(event)) {
    process.stderr.write(`[AROS] deferred one-shot signal listener: ${event}\n`)
    return this
  }
  return originalOnce.call(this, event, listener)
}

process.on('uncaughtExceptionMonitor', (error, origin) => {
  try {
    appendFileSync(
      `${logDir}/claude-sdk-uncaught.log`,
      `origin=${origin}\n${error?.stack ?? error}\n`
    )
  } catch {}
})

const cliPath =
  'SYS:Developer/ClaudeCode/runtime/node_modules/@anthropic-ai/claude-code/cli.js'
process.argv[1] = cliPath
await import('./runtime/node_modules/@anthropic-ai/claude-code/cli.js')
