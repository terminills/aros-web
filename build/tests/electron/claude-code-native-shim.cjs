/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Stands in for the Claude Code extension's resources/native-binary/claude.
 *
 * The extension resolves the CLI as resources/native-binaries/<platform>/
 * claude, then resources/native-binary/claude, and throws "Unsupported
 * platform" when neither exists.  With claudeCode.claudeProcessWrapper set
 * it spawns <wrapper> <that path> <claude args...> instead, so on AROS the
 * wrapper is C:Node and this file is "that path": a CommonJS script (no
 * extension, so Node treats it as CJS) that hands argv through to the SDK
 * bootstrap, which loads the real cli.js under node.library.
 *
 * Staged by stage-claude-code-extension.sh.
 */
'use strict'

const { pathToFileURL } = require('node:url')

import(
  pathToFileURL('SYS:Developer/ClaudeCode/claude_aros_sdk_bootstrap.mjs').href
).catch(error => {
  process.stderr.write(`[AROS] claude shim failed: ${error?.stack ?? error}\n`)
  process.exit(1)
})
