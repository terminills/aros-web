/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/
'use strict';
// Child module in the utility-process shape VS Code's bootstrap-fork.js expects.
const fs = require('original-fs');
process.stdout.write('child argv ' + JSON.stringify(process.argv.slice(1)) + ' type ' + process.type + ' env ' + process.env.VSCODE_AMD_ENTRYPOINT + ' fs=' + typeof fs.readFileSync + '\n');
// VS Code attaches its parentPort listener only after loading its modules;
// the config + port posted at fork time must still arrive (Electron queues).
setTimeout(() => process.parentPort.on('message', e => {
  if (e.data && e.data.kind === 'config') {
    const port = e.ports[0];
    port.on('message', m => { port.postMessage({ echo: m.data, isBuf: m.data instanceof Uint8Array }); });
    port.start();
    process.parentPort.postMessage('ipc-ready');
    process.parentPort.postMessage({ alive: process.kill(Number(process.env.VSCODE_PARENT_PID), 0) });
  }
}), 300);
