/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    node.library concurrent fs.readFile probe.  VS Code's extension scanner
    and AMD loader read hundreds of files at once (fs.readFile = open, fstat,
    read, close on libuv's thread pool, several in flight); on the ADT tree
    single reads came back `EIO: i/o error, read` + `EIO ... close` from
    both the main thread and worker_threads Workers.  This script reads a
    directory tree the same way - every file at once, main thread and a
    Worker in parallel - and reports how many reads fail, with the errno,
    so the failure can be reproduced without Electron.

    Run on the guest:  C:Node SYS:Developer/electron-probes/concurrent-readfile-probe.js
    SYS:Developer/VSCode/extensions [passes] [workers]
    >SYS:Developer/electron-probes/concurrent-readfile-probe.txt

    VS Code runs the scan with THREE utility Workers alive (file watcher,
    shared process, extension host) next to the main thread, so the worker
    count is a parameter (default 1).
*/
'use strict';
const fs = require('fs');
const path = require('path');
const { Worker, isMainThread, parentPort, workerData, threadId } = require('worker_threads');

const root = (isMainThread ? process.argv[2] : workerData.root) || 'SYS:Developer/VSCode/extensions';
const passes = Number((isMainThread ? process.argv[3] : workerData.passes) || 3);
const workers = Number(process.argv[4] || 1);
const tag = isMainThread ? 'main' : 'worker' + threadId;
const log = (line) => process.stdout.write('[READ-PROBE ' + tag + '] ' + line + '\n');

const listFiles = (dir, out) => {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); }
    catch (error) { log('readdir ' + dir + ' failed: ' + error.code); return out; }
    for (const entry of entries) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) listFiles(full, out);
        else if (entry.isFile()) out.push(full);
    }
    return out;
};

const readAll = (files) => new Promise((resolve) => {
    let pending = files.length;
    const failures = [];
    let bytes = 0;
    if (!pending) return resolve({ failures, bytes });
    for (const file of files) {
        fs.readFile(file, (error, data) => {
            if (error) failures.push(file + ': ' + error.code + ' ' + error.syscall +
                (error.errors ? ' [' + error.errors.map(e => e.code + ' ' + e.syscall).join(', ') + ']' : ''));
            else bytes += data.length;
            if (--pending === 0) resolve({ failures, bytes });
        });
    }
});

const run = async () => {
    const files = listFiles(root, []);
    log('root ' + root + ' files ' + files.length + ' passes ' + passes);
    let total = 0;
    for (let pass = 1; pass <= passes; pass++) {
        const started = Date.now();
        const { failures, bytes } = await readAll(files);
        total += failures.length;
        log('pass ' + pass + ': ' + failures.length + ' failed of ' + files.length +
            ', ' + bytes + ' bytes, ' + (Date.now() - started) + ' ms');
        for (const failure of failures.slice(0, 10)) log('  ' + failure);
    }
    return { files: files.length, failed: total };
};

if (isMainThread) {
    log('node ' + process.version + ' platform ' + process.platform + ' workers ' + workers);
    const workerResults = [];
    const workersDone = [];
    for (let i = 0; i < workers; i++) {
        const worker = new Worker(__filename, { workerData: { root, passes } });
        worker.on('message', (m) => { workerResults.push(m); });
        worker.on('error', (e) => log('worker error ' + (e && e.stack || e)));
        workersDone.push(new Promise((resolve) => worker.on('exit', resolve)));
    }
    run().then(async (mainResult) => {
        await Promise.all(workersDone);
        const workersOk = workerResults.length === workers && workerResults.every((r) => r.failed === 0);
        const verdict = (mainResult.failed === 0 && workersOk) ? 'PASS' : 'FAIL';
        log('RESULT main ' + JSON.stringify(mainResult) + ' workers ' + JSON.stringify(workerResults) + ' ' + verdict);
        process.exit(verdict === 'PASS' ? 0 : 1);
    });
} else {
    run().then((result) => { parentPort.postMessage(result); });
}
