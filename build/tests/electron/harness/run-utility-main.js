/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/
'use strict';
const path = require('path');
const { EventEmitter } = require('node:events');
const workerThreads = require('node:worker_threads');
const log = [];
const binding = { invoke: (m, p) => { log.push(m + ' ' + p); return ''; } };
const describeError = e => (e && e.stack) || String(e);
const delivered = [];
const deliverToRenderer = (contents, message) => delivered.push(message);
const src = require('fs').readFileSync(path.join(__dirname, '..', 'utility-process-main.js'), 'utf8');
const api = new Function('EventEmitter', 'workerThreads', 'binding', 'describeError', 'deliverToRenderer',
  src + '\nreturn {MessagePortMain,MessageChannelMain,utilityProcess,exportPortToRenderer,rendererPorts};')
  (EventEmitter, workerThreads, binding, describeError, deliverToRenderer);
const child = api.utilityProcess.fork(path.join(__dirname, 'utility-child.js'), ['--type=probe', '--x'], {
  serviceName: 'probe-1', env: { VSCODE_AMD_ENTRYPOINT: 'entry', VSCODE_PARENT_PID: String(process.pid) }, execArgv: ['--bogus-flag'] });
const results = { spawn: false, messages: [], echoed: null, rendererDelivered: null, exit: null };
child.on('spawn', () => { results.spawn = true; });
child.on('message', m => results.messages.push(m));
const { port1, port2 } = new api.MessageChannelMain();
child.postMessage({ kind: 'config' }, [port2]);
// port1 goes to the "renderer": exported over the seam
const contents = {};
const id = api.exportPortToRenderer(contents, port1);
// renderer -> port: simulate a port-message from the renderer
api.rendererPorts.get(id).port.postMessage(Buffer.from('hello'));
const timer = setInterval(() => {
  const echo = delivered.find(m => m.type === 'port-message');
  if (echo && results.messages.length >= 2) {
    clearInterval(timer);
    results.rendererDelivered = echo;
    child.kill();
  }
}, 20);
child.on('exit', code => {
  results.exit = code;
  console.log('RESULT', JSON.stringify(results));
  console.log('BINDING', JSON.stringify(log));
  const pass = results.spawn && results.messages[0] === 'ipc-ready' && results.messages[1].alive === true &&
    results.rendererDelivered.data.echo instanceof Uint8Array && results.rendererDelivered.data.isBuf && results.exit === 15;
  console.log(pass ? 'PASS' : 'FAIL');
  process.exit(pass ? 0 : 1);
});
