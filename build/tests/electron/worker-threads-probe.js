/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    node.library worker_threads probe.  Electron's utilityProcess.fork()
    contract - a second node instance with process.parentPort, argv/env of
    its own and MessagePorts transferable both ways - is what Node's own
    worker_threads already provide in-process.  This script answers whether
    node.library on the ADT tree can do that: it spawns itself as a Worker,
    hands it one end of a MessageChannel, and expects (1) the worker to come
    online, (2) a reply on parentPort carrying the worker's argv/env, (3) a
    message on the transferred port, (4) a clean exit code.

    Run on the guest:  C:Node SYS:Developer/electron-probes/worker-threads-probe.js
    >SYS:Developer/electron-probes/worker-threads-probe.txt
*/
'use strict';
const { Worker, MessageChannel, isMainThread, parentPort, threadId } =
    require('worker_threads');

const log = (line) => process.stdout.write('[WORKER-PROBE] ' + line + '\n');

if (isMainThread) {
    log('main start node ' + process.version + ' pid ' + process.pid);
    const { port1, port2 } = new MessageChannel();
    const results = { online: false, reply: null, viaPort: null, exit: null };
    const finish = () => {
        log('RESULT ' + JSON.stringify(results));
        log((results.online && results.reply && results.viaPort &&
             results.exit === 0) ? 'PASS' : 'FAIL');
    };
    let worker;
    try {
        worker = new Worker(__filename, {
            argv: ['--type=utility', '--service-name=probe'],
            env: { AROS_PROBE: 'yes' },
        });
    } catch (error) {
        log('new Worker threw ' + (error && error.stack || error));
        finish();
        process.exit(1);
    }
    const timer = setTimeout(() => {
        log('timeout after 30s');
        finish();
        process.exit(2);
    }, 30000);
    worker.on('online', () => { results.online = true; log('online'); });
    worker.on('error', (error) => {
        log('worker error ' + (error && error.stack || error));
    });
    worker.on('message', (message) => {
        log('parentPort reply ' + JSON.stringify(message));
        results.reply = message;
    });
    port1.on('message', (message) => {
        log('transferred port got ' + JSON.stringify(message));
        results.viaPort = message;
        port1.close();
        worker.postMessage({ kind: 'bye' });
    });
    worker.on('exit', (code) => {
        results.exit = code;
        log('exit ' + code);
        clearTimeout(timer);
        finish();
    });
    worker.postMessage({ kind: 'hello', port: port2 }, [port2]);
} else {
    parentPort.on('message', (message) => {
        if (message.kind === 'hello') {
            parentPort.postMessage({
                ack: true,
                threadId,
                argv: process.argv.slice(2),
                env: process.env.AROS_PROBE,
            });
            message.port.postMessage({ viaPort: 'from worker ' + threadId });
            message.port.close();
        } else if (message.kind === 'bye') {
            process.exit(0);
        }
    });
}
