'use strict'

/*
 * AROS currently has no usable Node module compile-cache backend.  Keep npm
 * on the uncached path unless the caller explicitly selected another policy.
 */
if (process.env.NODE_DISABLE_COMPILE_CACHE === undefined) {
  process.env.NODE_DISABLE_COMPILE_CACHE = '1'
}

/*
 * Initialize the asynchronous providers before npm starts issuing concurrent
 * filesystem and registry work.  This is deliberately an in-memory adapter:
 * npm and Node remain unmodified.
 */
const fs = require('node:fs')
const fsp = require('node:fs/promises')
const { createRequire } = require('node:module')
fsp.realpath = path => new Promise((resolve, reject) => {
  fs.realpath(path, (error, result) => {
    if (error) {
      reject(error)
    } else {
      resolve(result)
    }
  })
})

/*
 * Arborist registers rollback handlers for a broad Unix signal set before it
 * touches the package tree.  AROS signal listeners are not ready for that
 * contract yet and process.on() can block during registration.  Replace only
 * Arborist's rollback hook; normal Node signal behavior remains untouched.
 */
const npmRequire = createRequire('SYS:Lib/node_modules/npm/package.json')
const signalHandlingPath =
  npmRequire.resolve('@npmcli/arborist/lib/signal-handling.js')
npmRequire(signalHandlingPath)
require.cache[signalHandlingPath].exports = Object.assign(
  () => () => {},
  { process },
)

for (const moduleName of [
  'node:dns',
  'node:net',
  'node:tls',
  'node:http',
  'node:https',
]) {
  require(moduleName)
}

require('SYS:Lib/node_modules/npm/bin/npm-cli.js')
