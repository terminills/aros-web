/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Readable source of the utilityProcess block embedded in electron.library
    (electron_bridge.c, utilityPreamble / UtilityProcess / port proxy).
*/
/* MessagePortMain over a node MessagePort.  Every node port carries the
   envelope {data,ports}, so a transferred port arrives as event.ports on
   whichever side receives it - main process or utility worker - exactly
   like Electron's MessageEvent.  The factory is embedded by source into
   the utility preamble (Function.prototype.toString), so it must only
   close over its parameter. */
/* One-line summary of a message for the utility/port traces: binary
   payloads by size (VS Code's RPC frames are VSBuffers), the rest as
   truncated JSON. */
const briefValue=value=>{
if(value instanceof Uint8Array||value instanceof ArrayBuffer)
return 'bin['+value.byteLength+']';
try{return String(JSON.stringify(value)).slice(0,160);}
catch(error){return String(value).slice(0,160);}
};
const makeMessagePortMain=EventEmitter=>{
class MessagePortMain extends EventEmitter{
constructor(port){
super();
this._port=port;this._started=false;this._closed=false;this._queue=[];
port.on('message',envelope=>{
const event={data:envelope&&envelope.data,ports:MessagePortMain.wrapAll(envelope&&envelope.ports)};
if(this._started)this.emit('message',event);else this._queue.push(event);
});
port.on('close',()=>{
if(this._closed)return;
this._closed=true;this.emit('close');
});
port.on('messageerror',error=>{
console.error('[AROS-ELECTRON] MessagePortMain messageerror: '+(error&&error.stack||error));
});
}
static wrapAll(ports){
return Array.isArray(ports)?ports.map(port=>new MessagePortMain(port)):[];
}
static unwrapAll(ports){
return Array.isArray(ports)?ports.map(port=>port instanceof MessagePortMain?port._port:port):[];
}
postMessage(message,transfer){
if(this._closed)return;
const ports=MessagePortMain.unwrapAll(transfer);
this._port.postMessage({data:message,ports},ports);
}
start(){
if(this._started)return;
this._started=true;
const queued=this._queue.splice(0);
for(const event of queued)this.emit('message',event);
}
close(){
if(this._closed)return;
this._port.close();
}
}
return MessagePortMain;
};
const MessagePortMain=makeMessagePortMain(EventEmitter);
class MessageChannelMain{
constructor(){
const channel=new workerThreads.MessageChannel();
this.port1=new MessagePortMain(channel.port1);
this.port2=new MessagePortMain(channel.port2);
}
}
/* utilityProcess.fork: a child node instance is a worker_threads Worker in
   this process.  The preamble gives it Electron's utility-process shape -
   process.parentPort with {data,ports} events, process.argv as
   `<exe> <module> ...args`, process.type 'utility', original-fs - and then
   requires the module.  The child's stdout/stderr are exposed as streams
   (Electron stdio 'pipe') and echoed to the debug log while the seam is
   young.  VSCODE_PARENT_PID liveness polls process.kill(<our pid>,0): the
   parent is this very process, so that answers true.  AROS getpid() is per
   task (ETask et_UniqueID), so a Worker's process.pid differs from the main
   pid VS Code was given - compare against workerData.parentPid too, or
   bootstrap-fork's terminateWhenParentTerminates exits the service on its
   first 5 s poll. */
const utilityPreamble=
"const {parentPort,workerData}=require('node:worker_threads');"+
"const {EventEmitter}=require('node:events');"+
"const MessagePortMain=("+makeMessagePortMain.toString()+")(EventEmitter);"+
"const Module=require('node:module');"+
"const moduleLoad=Module._load;"+
"Module._load=function(request,parent,isMain){"+
"if(request==='original-fs')return moduleLoad.call(this,'fs',parent,isMain);"+
"if(request==='electron')throw new Error('electron is not available in an AROS utility process');"+
"return moduleLoad.apply(this,arguments);"+
"};"+
/* An eval Worker runs like `node -e`: node publishes module/exports/
   __filename/__dirname as GLOBALS.  UMD bundles (VS Code's semver.js) then
   see `typeof module==='object'` at global scope, take the CommonJS branch
   and never call the AMD loader's define -> "Didn't receive define call".
   A real utility process has no such globals. */
"delete globalThis.module;delete globalThis.exports;"+
"delete globalThis.__filename;delete globalThis.__dirname;"+
"process.type='utility';"+
"if(!process.versions.electron)process.versions.electron=workerData.electronVersion;"+
"process.argv=[process.execPath,workerData.modulePath,...workerData.args];"+
"const nativeKill=process.kill.bind(process);"+
"process.kill=(pid,signal)=>{"+
"if((Number(pid)===process.pid||Number(pid)===workerData.parentPid)&&(signal===0||signal==='0'))return true;"+
"return nativeKill(pid,signal);"+
"};"+
/* Electron's ParentPort starts its port only when the first 'message'
   listener is added, so the payload and the MessagePort the main process
   posts right after fork wait until the module is loaded and listening
   (VS Code's shared process, extension host and file watcher all attach
   their listener after tens of seconds of module loading).  Queue until
   then; 'newListener' fires before the listener is on, hence the
   deferred flush.  A real utility process also lives until it exits or
   is killed, whatever its event loop holds - a Worker would leave once
   the loop drains, so a ref'd timer keeps it. */
"const parent=new EventEmitter();"+
"const pendingEnvelopes=[];let parentStarted=false;"+
"const toParentEvent=envelope=>({data:envelope&&envelope.data,"+
"ports:MessagePortMain.wrapAll(envelope&&envelope.ports)});"+
"parentPort.on('message',envelope=>{"+
"if(parentStarted)parent.emit('message',toParentEvent(envelope));"+
"else pendingEnvelopes.push(envelope);"+
"});"+
"parent.on('newListener',name=>{"+
"if(name!=='message'||parentStarted)return;"+
"parentStarted=true;"+
"console.error('[AROS-UTILITY-LISTEN] '+workerData.serviceName+' queued '+pendingEnvelopes.length+' after '+Math.round(performance.now())+' ms');"+
"setImmediate(()=>{for(const envelope of pendingEnvelopes.splice(0))"+
"parent.emit('message',toParentEvent(envelope));});"+
"});"+
"parent.postMessage=(message,transfer)=>{"+
"const ports=MessagePortMain.unwrapAll(transfer);"+
"parentPort.postMessage({data:message,ports},ports);"+
"};"+
"parent.start=()=>{};parent.close=()=>{};"+
"process.parentPort=parent;"+
"setInterval(()=>{},2147483647);"+
/* Diagnostics while the seam is young: VS Code's AMD loader reads every
   module with fs.readFile, so a load that never completes shows as a
   pending count that stops moving; an explicit process.exit shows its
   caller. */
"const fsModule=require('node:fs');"+
"const fsStats={issued:0,done:0,pending:new Map()};"+
"const nativeReadFile=fsModule.readFile;"+
"fsModule.readFile=function(path,...rest){"+
"const callback=rest[rest.length-1];"+
"if(typeof callback==='function'){"+
"const id=++fsStats.issued;fsStats.pending.set(id,String(path));"+
"rest[rest.length-1]=function(error){"+
"fsStats.done++;fsStats.pending.delete(id);"+
"if(error)console.error('[AROS-UTILITY-FS] '+workerData.serviceName+' readFile '+path+' -> '+(error.code||error));"+
"return callback.apply(this,arguments);"+
"};"+
"}"+
"return nativeReadFile.call(this,path,...rest);"+
"};"+
"setInterval(()=>{console.error('[AROS-UTILITY-FS] '+workerData.serviceName+' readFile issued '+fsStats.issued+' done '+fsStats.done+' pending '+[...fsStats.pending.values()].slice(0,3).join(' '));},5000).unref();"+
"const nativeExit=process.exit.bind(process);"+
"process.exit=code=>{"+
"console.error('[AROS-UTILITY-EXIT-CALL] '+workerData.serviceName+' code '+code+' '+String(new Error().stack).split(String.fromCharCode(10)).slice(1,6).join(' | '));"+
"return nativeExit(code);"+
"};"+
"process.on('exit',code=>console.error('[AROS-UTILITY-EXIT] '+workerData.serviceName+' code '+code));"+
"require(workerData.modulePath);";
let nextUtilityId=1;
class UtilityProcess extends EventEmitter{
constructor(modulePath,args,options){
super();
options=options&&typeof options==='object'?options:{};
const serviceName=String(options.serviceName||('utility-'+nextUtilityId++));
this.pid=undefined;this.stdout=null;this.stderr=null;
this._killed=false;this._exited=false;this._worker=null;this._serviceName=serviceName;
const env={};
const source=options.env&&typeof options.env==='object'?options.env:process.env;
for(const key of Object.keys(source))env[key]=String(source[key]);
const argv=Array.isArray(args)?args.map(String):[];
const execArgv=Array.isArray(options.execArgv)?options.execArgv.map(String):[];
const workerOptions={eval:true,argv,env,execArgv,stdout:true,stderr:true,
workerData:{modulePath:String(modulePath),args:argv,serviceName,parentPid:process.pid,
electronVersion:process.versions.electron}};
let worker=null;
try{worker=new workerThreads.Worker(utilityPreamble,workerOptions);}
catch(error){
if(execArgv.length){
console.error('[AROS-ELECTRON] utilityProcess.fork('+serviceName+') retrying without execArgv '+JSON.stringify(execArgv)+': '+describeError(error));
try{worker=new workerThreads.Worker(utilityPreamble,{...workerOptions,execArgv:[]});}
catch(again){error=again;}
}
if(!worker){
console.error('[AROS-ELECTRON] utilityProcess.fork('+serviceName+') failed: '+describeError(error));
setImmediate(()=>this._exit(1));
return;
}
}
this._worker=worker;this.pid=worker.threadId;
this.stdout=worker.stdout;this.stderr=worker.stderr;
const echo=stream=>chunk=>binding.invoke('console-error','[AROS-UTILITY '+serviceName+' '+stream+'] '+String(chunk).trimEnd().slice(0,2000));
worker.stdout.on('data',echo('stdout'));
worker.stderr.on('data',echo('stderr'));
worker.on('message',envelope=>{
binding.invoke('utility-process.message',serviceName+'\t'+briefValue(envelope&&envelope.data));
this.emit('message',envelope&&envelope.data);
});
worker.on('error',error=>{
console.error('[AROS-ELECTRON] utility process '+serviceName+' crashed: '+describeError(error));
});
worker.on('exit',code=>this._exit(this._killed&&code!==0?15:code));
binding.invoke('utility-process.fork',serviceName+'\tworker '+worker.threadId+'\t'+String(modulePath)+'\t'+argv.join(' '));
setImmediate(()=>{if(!this._exited)this.emit('spawn');});
}
_exit(code){
if(this._exited)return;
this._exited=true;this._worker=null;
binding.invoke('utility-process.exit',this._serviceName+'\t'+String(code));
this.emit('exit',code);
}
postMessage(message,transfer){
if(!this._worker||this._exited)return;
const ports=MessagePortMain.unwrapAll(transfer);
binding.invoke('utility-process.post',this._serviceName+'\tports '+ports.length+'\t'+briefValue(message));
this._worker.postMessage({data:message,ports},ports);
}
kill(){
if(!this._worker||this._exited)return false;
this._killed=true;
this._worker.terminate();
return true;
}
}
const utilityProcess=Object.freeze({
fork:(modulePath,args,options)=>new UtilityProcess(modulePath,args,options)
});
/* A MessagePortMain handed to a renderer (webContents.postMessage transfer)
   is proxied over the seam: main keeps the node port and forwards its
   messages as port-message deliveries; the renderer runtime pairs each id
   with a DOM MessageChannel and gives VS Code the far end.  Only the
   main->renderer direction exists in VS Code (ipcMessagePort.acquire). */
let nextRendererPortId=1;
const rendererPorts=new Map();
/* The first few messages each way are traced so a silent handshake
   (extension host 'ready', shared process 'ipcReady') can be located. */
const tracePort=(id,direction,count,data)=>{
if(count<=4||count%500===0)binding.invoke('utility-process.port',
'port '+id+' '+direction+' #'+count+' '+briefValue(data));
};
const exportPortToRenderer=(contents,port)=>{
const id=nextRendererPortId++;
let toRenderer=0;
rendererPorts.set(id,{port,fromRenderer:0});
binding.invoke('utility-process.port','port '+id+' exported to renderer');
port.on('message',event=>{
tracePort(id,'->renderer',++toRenderer,event.data);
deliverToRenderer(contents,{type:'port-message',port:id,
data:event.data,ports:event.ports.map(nested=>exportPortToRenderer(contents,nested))});
});
port.on('close',()=>{
rendererPorts.delete(id);
deliverToRenderer(contents,{type:'port-close',port:id});
});
port.start();
return id;
};
