// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// git-remote-https for AROS, backed by node's TLS.
//
// AROS has no TLS of its own - no AmiSSL, no OpenSSL, no curl - so git is built
// with no HTTP(S) transport, and GitHub serves only https/ssh. node.library
// does have a working OpenSSL here (verified: a real pkt-line protocol-v2
// advertisement fetched from github.com over TLS), so git's http transport is
// provided by this helper instead of by libcurl.
//
// git talks to a remote helper over stdin/stdout with a tiny line protocol. We
// implement protocol v2's `stateless-connect`, which makes this a tunnel rather
// than a git implementation: git writes a request as pkt-lines, we POST it to
// <url>/git-upload-pack and stream the response back. All ref and pack logic
// stays in git.
'use strict'

const https = require('https')
const http = require('http')
const { URL } = require('url')

const remoteName = process.argv[2]
const remoteUrl = process.argv[3] || remoteName

const trace = process.env.AROS_GIT_HELPER_TRACE
  ? m => { try { process.stderr.write('[git-remote-https] ' + m + '\n') } catch {} }
  : () => {}

const base = String(remoteUrl).replace(/\/+$/, '')
const agent = 'git/2.55.0 (AROS)'

// pkt-line framing: 4 hex digits of length (inclusive), or 0000 flush /
// 0001 delimiter. Used to find where git's request ends.
const FLUSH = '0000'
const DELIM = '0001'

function request(method, path, body, headers, callback) {
  const url = new URL(base + path)
  const mod = url.protocol === 'http:' ? http : https
  const options = {
    method,
    host: url.hostname,
    port: url.port || (url.protocol === 'http:' ? 80 : 443),
    path: url.pathname + url.search,
    headers: Object.assign({
      'User-Agent': agent,
      'Git-Protocol': 'version=2',
    }, headers || {}),
  }
  const req = mod.request(options, res => {
    // git servers redirect freely (github.com -> .git suffix, http -> https)
    if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
      res.resume()
      trace('redirect ' + res.statusCode + ' -> ' + res.headers.location)
      const next = new URL(res.headers.location, url)
      return request(method, next.pathname + next.search, body, headers, callback)
    }
    callback(null, res)
  })
  req.on('error', err => callback(err))
  if (body) req.write(body)
  req.end()
}

function fail(message) {
  process.stderr.write('fatal: ' + message + '\n')
  process.exit(128)
}

// Self-test: prove the helper's JS runs and can reach the remote, without any
// stdin plumbing in the way. AROS_GIT_HELPER_SELFTEST=1 runs this and exits.
if (process.env.AROS_GIT_HELPER_SELFTEST) {
  process.stderr.write('[selftest] argv=' + JSON.stringify(process.argv.slice(1)) + '\n')
  process.stderr.write('[selftest] base=' + base + '\n')
  request('GET', '/info/refs?service=git-upload-pack', null, {}, (err, res) => {
    if (err) { process.stderr.write('[selftest] FAILED ' + err.message + '\n'); process.exit(1) }
    process.stderr.write('[selftest] status ' + res.statusCode + ' ' +
      res.headers['content-type'] + '\n')
    let n = 0
    res.on('data', c => { n += c.length })
    res.on('end', () => {
      process.stderr.write('[selftest] advertisement ' + n + ' bytes - OK\n')
      process.exit(0)
    })
  })
  return
}


// One POST per git request: read pkt-lines from git until a flush that ends a
// complete request, send them, stream the answer back.
function statelessConnect(service, carried, readChunk) {
  // Relay: collect whole pkt-lines from git until a flush ends the request,
  // POST them, write the answer straight back, repeat.
  let buffer = carried

  const takeRequest = () => {
    for (;;) {
      let offset = 0
      while (buffer.length - offset >= 4) {
        const head = buffer.slice(offset, offset + 4).toString('ascii')
        if (head === FLUSH) {
          offset += 4
          const payload = buffer.slice(0, offset)
          buffer = buffer.slice(offset)
          return payload
        }
        if (head === DELIM) { offset += 4; continue }
        const length = parseInt(head, 16)
        if (!Number.isFinite(length) || length < 4) fail('bad pkt-line ' + head)
        if (buffer.length - offset < length) break
        offset += length
      }
      const chunk = readChunk()
      if (chunk === null) return null
      buffer = Buffer.concat([buffer, chunk])
    }
  }

  const post = (payload, done) => {
    request('POST', '/' + service, payload, {
      'Content-Type': 'application/x-' + service + '-request',
      'Accept': 'application/x-' + service + '-result',
      'Content-Length': String(payload.length),
    }, (err, res) => {
      if (err) fail('POST ' + service + ': ' + err.message)
      if (res.statusCode !== 200) { res.resume(); fail(service + ' returned HTTP ' + res.statusCode) }
      res.on('data', chunk => process.stdout.write(chunk))
      res.on('end', done)
    })
  }

  const pump = () => {
    const payload = takeRequest()
    if (payload === null) { process.exit(0); return }
    trace('request ' + payload.length + ' bytes')
    post(payload, pump)
  }
  pump()
}

function capabilities() {
  process.stdout.write('stateless-connect\n')
  process.stdout.write('fetch\n')
  process.stdout.write('\n')
}

// Command loop.
//
// Read fd 0 synchronously rather than through process.stdin: git drives the
// helper as a strict request/response conversation, so blocking reads are the
// natural shape - and AROS' uv stream layer is exactly where stdio has bitten
// before (a pipe that guessed UNKNOWN silently black-holed node's stdout
// earlier today). fs.readSync needs none of that machinery.
const fs = require('fs')

function readChunk() {
  const buffer = Buffer.alloc(65536)
  try {
    const count = fs.readSync(0, buffer, 0, buffer.length, null)
    return count > 0 ? buffer.slice(0, count) : null
  } catch (error) {
    if (error.code === 'EAGAIN') return Buffer.alloc(0)
    if (error.code === 'EOF') return null
    throw error
  }
}

let pending = Buffer.alloc(0)

function readLine() {
  for (;;) {
    const index = pending.indexOf(0x0a)
    if (index >= 0) {
      const line = pending.slice(0, index).toString('utf8')
      pending = pending.slice(index + 1)
      return line
    }
    const chunk = readChunk()
    if (chunk === null) return null
    pending = Buffer.concat([pending, chunk])
  }
}

for (;;) {
  const line = readLine()
  if (line === null) break
  trace('command: ' + JSON.stringify(line))

  if (line === 'capabilities') { capabilities(); continue }
  if (line === '') continue
  if (line.startsWith('option ')) { process.stdout.write('unsupported\n'); continue }
  if (line.startsWith('stateless-connect ')) {
    const service = line.slice('stateless-connect '.length).trim()
    process.stdout.write('\n')
    statelessConnect(service, pending, readChunk)
    break
  }
  process.stdout.write('\n')
}
