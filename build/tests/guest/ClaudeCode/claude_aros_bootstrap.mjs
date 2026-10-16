// Copyright (C) 2026, The AROS Development Team. All rights reserved.
/*
 * AROS Claude Code bootstrap.
 *
 * uv_signal_init() does not yet implement Node's SignalWrap contract on AROS.
 * Claude Code registers Unix signal listeners during startup, and Node treats
 * the failed SignalWrap construction as a fatal assertion.  Keep the upstream
 * CLI immutable and suppress only signal listener registration until libuv's
 * AROS signal backend is complete.
 */
import { appendFileSync, mkdirSync, writeFileSync } from 'node:fs'

// Logs live in T: (RAM:T), which is writable on a Live CD too.
const logDir = 'T:ClaudeCode'
try {
  mkdirSync(logDir, { recursive: true })
} catch {}
const consoleLog = `${logDir}/claude-console.log`
try {
  writeFileSync(consoleLog, '')
} catch {}

// The Live CD has no writable SYS: and no netshell, so the only channel that
// reaches the host is the serial port.  fd.library numbers descriptors
// system-wide, so a long-lived SER: descriptor gets reused by unrelated code
// and megabytes of someone else's buffers land on the serial line: open and
// close SER: per line instead, and mirror only these markers (never the whole
// TUI stream, whose escape sequences the console needs).
const serialWrite = text => {
  try {
    writeFileSync('SER:', String(text).replace(/\u001b\[[0-9;?]*[A-Za-z]/g, ''))
  } catch {}
}

const mirrorWrites = stream => {
  const originalWrite = stream.write.bind(stream)
  stream.write = function (chunk, encoding, callback) {
    try {
      appendFileSync(consoleLog, chunk)
    } catch {}
    return originalWrite(chunk, encoding, callback)
  }
}

process.on('uncaughtException', error => {
  serialWrite(`[CC] uncaught ${error?.stack ?? error}\n`)
  throw error
})
process.on('unhandledRejection', reason => {
  serialWrite(`[CC] unhandled rejection ${reason?.stack ?? reason}\n`)
})

mirrorWrites(process.stdout)
mirrorWrites(process.stderr)

// CLAUDE_AROS_TRACE_EXIT=1: after the UI has given the terminal back (Ink's
// ESC[?2004l), report every 2 s whether the event loop still runs and what
// keeps it alive, and when exit is requested and reached.
if (process.env.CLAUDE_AROS_TRACE_EXIT) {
  const started = Date.now()
  const stamp = () => `${((Date.now() - started) / 1000).toFixed(1)}s`
  const handles = () => {
    const names = {}
    for (const h of process._getActiveHandles()) {
      const name = h?.constructor?.name ?? typeof h
      names[name] = (names[name] ?? 0) + 1
    }
    return JSON.stringify(names)
  }
  let ticking = false
  const originalWrite = process.stdout.write
  process.stdout.write = function (chunk, ...rest) {
    if (!ticking && String(chunk).includes('\u001b[?2004l')) {
      ticking = true
      serialWrite(`[CC] ${stamp()} terminal released, handles ${handles()}\n`)
      setInterval(() => {
        serialWrite(`[CC] ${stamp()} loop alive, handles ${handles()} requests ${process._getActiveRequests().length}\n`)
      }, 2000).unref()
    }
    return originalWrite.call(this, chunk, ...rest)
  }
  const originalExit = process.exit
  process.exit = function (code) {
    serialWrite(`[CC] ${stamp()} process.exit(${code}) from ${new Error().stack.split('\n')[2]?.trim()}\n`)
    return originalExit.call(this, code)
  }
  process.on('exit', code => serialWrite(`[CC] ${stamp()} exit event ${code}\n`))
}

const ttyState = {
  stdin: process.stdin.isTTY ?? false,
  stdout: process.stdout.isTTY ?? false,
  stderr: process.stderr.isTTY ?? false,
  columns: process.stdout.columns ?? null,
  rows: process.stdout.rows ?? null,
  term: process.env.TERM ?? null
}
try {
  writeFileSync(
    `${logDir}/claude-bootstrap-state.log`,
    `${JSON.stringify(ttyState)}\n`
  )
} catch {}
process.stdout.write(`[AROS bootstrap TTY ${JSON.stringify(ttyState)}]\n`)
serialWrite(`[CC] cwd=${process.cwd()} HOME=${process.env.HOME ?? '(unset)'}\n`)
// Claude Code keeps its settings and login under the home directory; on a
// Live CD that is the read-only SYS:Home, so fall back to RAM:Home.
try {
  const os = await import('node:os')
  const probe = `${os.homedir()}/.claude-write-test`
  try {
    writeFileSync(probe, '')
    ;(await import('node:fs')).unlinkSync(probe)
  } catch {
    mkdirSync('RAM:Home', { recursive: true })
    process.env.HOME = 'RAM:Home'
  }
  serialWrite(`[CC] homedir=${os.homedir()}\n`)
} catch (error) {
  serialWrite(`[CC] homedir threw ${error?.message ?? error}\n`)
}

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
      `${logDir}/claude-uncaught.log`,
      `origin=${origin}\n${error?.stack ?? error}\n`
    )
  } catch {}
})

// Claude Code is drawn for an xterm: give the console xterm's default
// palette and a dark default background (OSC 4/10/11), and hand the console
// back to the Workbench colours on exit (OSC 104/110/111). Ink draws relative
// to the cursor and never clears, so start from a cleared window (scrollback
// is kept): otherwise a previous session's cells show through wherever the
// new UI does not draw.
const xtermPalette = [
  '#000000', '#cd0000', '#00cd00', '#cdcd00', '#0000ee', '#cd00cd',
  '#00cdcd', '#e5e5e5', '#7f7f7f', '#ff0000', '#00ff00', '#ffff00',
  '#5c5cff', '#ff00ff', '#00ffff', '#ffffff'
]
if (process.stdout.isTTY) {
  process.stdout.write(
    '\u001b]4;' + xtermPalette.map((c, i) => `${i};${c}`).join(';') + '\u0007' +
    '\u001b]10;#ffffff\u0007\u001b]11;#000000\u0007' +
    '\u001b[0m\u001b[H\u001b[2J'
  )
  process.on('exit', () => {
    process.stdout.write('\u001b[0m\u001b]104\u0007\u001b]110\u0007\u001b]111\u0007')
  })
}

// AROS delivers no SIGWINCH yet (uv_signal is not implemented), so a resized
// console never reaches Ink. tty.WriteStream._refreshSize() re-reads the window
// size and emits 'resize' only when it changed: poll it until signals exist.
if (process.stdout.isTTY && typeof process.stdout._refreshSize === 'function') {
  setInterval(() => {
    try {
      process.stdout._refreshSize()
    } catch {}
  }, 500).unref()
}

const cliRel = 'runtime/node_modules/@anthropic-ai/claude-code/cli.js'
process.argv[1] = process.argv[1].replace(/[^/:]*$/, cliRel)
serialWrite('[CC] importing cli.js\n')
await import(`./${cliRel}`)
serialWrite('[CC] cli.js import returned\n')
