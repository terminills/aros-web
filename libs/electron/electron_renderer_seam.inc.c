/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Renderer and protocol seam of electron.library.
 *
 * Included by electron_bridge.c (not compiled separately) so the seam shares
 * the request queue and the node binding without exporting private symbols.
 *
 * Three data paths meet here:
 *
 *  - Custom-scheme requests (vscode-file://...): CEF asks the scheme factory
 *    for a resource handler on the IO thread; the handler parks the request in
 *    the pending list, posts a `protocol` event, and continues once the node
 *    main script answered through `protocol.response` with a native file path
 *    (served by DOS) or an error code.
 *
 *  - Renderer -> main: the `aros/electron_renderer` V8 extension gives page
 *    scripts `AROSElectronRenderer.send(text)`; its native handler posts an
 *    `ipc` event that the bootstrap's poll loop dispatches to ipcMain.
 *
 *  - Main -> renderer: `web-contents.send` runs
 *    `AROSElectronRenderer.deliver(<json literal>)` in the active browser's
 *    main frame through CefFrame::ExecuteJavaScript.
 *
 * Events are a semaphore-guarded list of heap strings, popped one at a time by
 * `native.poll`; the popped text stays valid until the next poll, which is
 * enough because node.library copies binding results into a V8 string before
 * returning to script.
 */

#define ELECTRON_MAX_SCHEMES 8

struct ElectronEvent
{
    struct MinNode node;
    char *text;
    /* Which app this event belongs to when one process hosts several of them. 0 means "any app may take it", which is what a
       single-app process produces and consumes. */
    LONG slot;
};

struct ElectronResource
{
    cef_resource_handler_t handler;     /* must stay first: CEF hands it back */
    struct MinNode node;                /* pending list membership */
    LONG refcount;
    ULONG id;
    char *scheme;
    char *url;
    cef_callback_t *open_callback;
    char *path;
    char *headers;                      /* flat JSON object from the handler */
    LONG error;                         /* cef_errorcode_t, 0 = serve path */
    BPTR file;
    LONG length;
    LONG slot;                          /* app whose browser asked */
};

static int electron_state_ready;
static struct SignalSemaphore electron_event_lock;
static struct MinList electron_events;
static char *electron_event_current;
static struct SignalSemaphore electron_resource_lock;
static struct MinList electron_pending_resources;
static ULONG electron_resource_next_id;
static cef_scheme_handler_factory_t electron_scheme_factory;
static int electron_scheme_factory_ready;
static cef_v8handler_t electron_renderer_handler;
static char *electron_declared_schemes[ELECTRON_MAX_SCHEMES];
static ULONG electron_declared_scheme_count;
/* The browser bound most recently, and the id of the window prepared most
   recently: what the single-window vectors (ElectronSetActiveBrowser,
   ElectronPostWindowEvent) operate on, and the fallback when a lookup by
   window finds nothing. */
static cef_browser_t *electron_active_browser;
static ULONG electron_window_id;

/*
 * Windows, addressed by (app slot, window id).
 *
 * One engine serves several apps and each app several BrowserWindows, and
 * BrowserWindow ids are per app (every app's first window is id 1), so a
 * window is named by both. The record collects what the bootstrap says about
 * the window before it exists (nodeIntegration, preload, process snapshot),
 * then the CEF browser the host created for it.
 *
 * The browser side and the renderer side of CEF each have their own
 * cef_browser_t for the same browser, so the join key is the browser
 * identifier, which both sides agree on, not the pointer.
 */
#define ELECTRON_MAX_WINDOWS 16

struct ElectronWindow
{
    int in_use;
    LONG slot;
    ULONG id;
    cef_browser_t *browser;
    int browser_identifier;
    /* webPreferences.nodeIntegration: the renderer context gets its own Node
       runtime when this is set, which is how an app whose page code requires
       fs/path/child_process at scope can run at all (GitHub Desktop builds
       its renderer that way). */
    int node_integration;
    char *preload_path;
    char *preload_source;
    char *process_snapshot;
};

static struct ElectronWindow electron_windows[ELECTRON_MAX_WINDOWS];
static struct SignalSemaphore electron_window_lock;
/* The record prepared most recently: what a renderer context is matched to
   when its browser is not bound to any record (yet). */
static struct ElectronWindow *electron_window_last;

static void electron_set_active_browser(cef_browser_t *browser);

static const cef_char_t electron_renderer_extension_name[] =
    u"aros/electron_renderer";
static const cef_char_t electron_renderer_extension_code[] =
    u"var AROSElectronRenderer;"
    u"if(!AROSElectronRenderer)AROSElectronRenderer={};"
    u"(function(){"
    /* __w is the window's address, "<slot>\t<id>", set by the injected
       context code; it goes ahead of every message so the engine can route
       the IPC to the app that owns this window. */
    u"AROSElectronRenderer.send=function(text){"
    u"native function AROSElectronSend(text);"
    u"return AROSElectronSend(AROSElectronRenderer.__w?"
    u"AROSElectronRenderer.__w+'\\t'+text:text);"
    u"};"
    /* The renderer's own debug channel.  A page's console is Blink's and lands
       in CEF's console, not in aros-debug.log, so an app that goes quiet
       mid-start says nothing an operator can read.  This puts the page's text
       in the same log as the engine's. */
    u"AROSElectronRenderer.log=function(text){"
    u"native function AROSElectronLog(text);"
    u"return AROSElectronLog(text);"
    u"};"
    /* Reports the renderer node pump's iteration count into the debug log.
       The page can call this from a requestAnimationFrame loop, which is
       Blink's and keeps running even when node's timers do not. */
    u"AROSElectronRenderer.pumpStats=function(){"
    u"native function AROSElectronPumpStats();"
    u"return AROSElectronPumpStats();"
    u"};"
    /* Runs the renderer's own Node loop, from the renderer's own thread. */
    u"AROSElectronRenderer.pumpNode=function(){"
    u"native function AROSElectronPumpNode();"
    u"return AROSElectronPumpNode();"
    u"};"
    /* Attaches a Node runtime to the context this call is running in. */
    u"AROSElectronRenderer.attachNode=function(){"
    u"native function AROSElectronAttachNode();"
    u"return AROSElectronAttachNode();"
    u"};"
    u"})();";
static const cef_char_t electron_renderer_script_url[] =
    u"aros://electron/preload";
static const cef_char_t electron_renderer_deliver_url[] =
    u"aros://electron/deliver";
static const cef_char_t electron_renderer_execute_url[] =
    u"aros://electron/execute-script";

/*
 * The renderer-side `require('electron')` for a sandboxed, context-isolated
 * preload: ipcRenderer, webFrame, contextBridge, webUtils and a process shim
 * built from the snapshot the main bootstrap took when it created the window.
 * The preload source is spliced in between the two halves and runs with the
 * CommonJS wrapper signature so its `require`/`module`/`process` resolve here.
 */
/*
 * What a nodeIntegration page needs that node does not give it.
 *
 * node hands require, module and process to the main script as WRAPPER
 * ARGUMENTS, so a page that calls require() at top level still sees nothing -
 * which is why the renderer kept reporting "require is not defined" even
 * after a Node runtime attached successfully. Publish them as globals.
 *
 * require('electron') is the other half: node's resolver knows nothing about
 * it, and the object lives in the renderer runtime injected below. The hook
 * hands that over once it exists, so load order between the two does not
 * matter.
 */
static const char electron_renderer_node_globals[] =
    /* Wrap require rather than patching Module._load: the page's call has to
       be answered before node's resolver sees it at all, or 'electron' comes
       back as "No such built-in module". */
    /* Resolve the page's requires from the PAGE's directory.
       The require node hands the attach script resolves against a synthetic
       module, so an app's own node_modules is invisible to it and every
       dependency comes back as "No such built-in module: <name>". The page
       knows where it lives, so build a require rooted there - for GitHub
       Desktop that is SYS:Developer/GitHubDesktop/out/, whose parent carries
       the app's node_modules. */
    "let __arosNodeRequire=require;"
    "try{"
    "const href=String((globalThis.location&&globalThis.location.href)||'');"
    "if(href.startsWith('file:')){"
    "let base=decodeURIComponent(href.slice(5).replace(/^\\/+/,''));"
    "base=base.replace(/[^/]*$/,'');"
    "if(base)"
    "__arosNodeRequire=require('module').createRequire(base+'index.js');"
    "}"
    "}catch(error){"
    "console.error('[AROS-ELECTRON] page require root failed: '+error);"
    "}"
    "globalThis.require=function(request){"
    "if(request==='electron'||request==='node:electron'){"
    "if(globalThis.__arosElectronRenderer)"
    "return globalThis.__arosElectronRenderer;"
    "throw new Error('electron is not available in this renderer yet');"
    "}"
    "return __arosNodeRequire(request);"
    "};"
    "globalThis.require.resolve=__arosNodeRequire.resolve;"
    "globalThis.require.cache=__arosNodeRequire.cache;"
    "globalThis.require.main=__arosNodeRequire.main;"
    "globalThis.module=module;"
    "globalThis.exports=module.exports;"
    "globalThis.process=process;"
    "globalThis.global=globalThis;"
    "globalThis.Buffer=require('buffer').Buffer;"
    "globalThis.setImmediate=setImmediate;"
    "globalThis.clearImmediate=clearImmediate;"
    /* A bundle built for a nodeIntegration renderer expects the CommonJS
       wrapper's file globals too - GitHub Desktop's renderer resolves paths
       against __dirname while it is still starting. The page is the app's own
       out/ directory, which is where its main script lives. */
    "globalThis.__filename=__filename;"
    "globalThis.__dirname=__dirname;"
    /* Anything the page loads with its own createRequire still goes through
       node, which is correct: only 'electron' is ours to answer. */
    "";

/*
 * Tee the page's console and its unhandled failures into the AROS debug log.
 *
 * A renderer's console is Blink's, so everything an app says about its own
 * startup lands in CEF's console and never in aros-debug.log - which is how
 * GitHub Desktop could go quiet mid-start with no error an operator can read.
 * AROSElectronRenderer.log is the engine's own channel out of the page, so the
 * app's text joins the engine's in one log, and the page's own console keeps
 * behaving exactly as it did.
 *
 * Every renderer gets this: the runtime script injects it for a preload
 * window, and the nodeIntegration attach carries it into the page context.
 */
static const char electron_renderer_console_tee[] =
    "try{"
    "const rawDebug=(globalThis.AROSElectronRenderer&&"
    "globalThis.AROSElectronRenderer.log)?"
    "globalThis.AROSElectronRenderer.log:null;"
    "const pageConsole=globalThis.console;"
    "if(rawDebug&&pageConsole&&!pageConsole.__arosTeed){"
    "const describe=value=>{"
    "try{"
    "if(typeof value==='string')return value;"
    "if(value&&value.stack)return String(value.stack);"
    "return JSON.stringify(value);"
    "}catch(error){return String(value);}"
    "};"
    "const say=(tag,values)=>{"
    "try{"
    "rawDebug(tag+': '+"
    "values.map(describe).join(' ').slice(0,2000));"
    "}catch(error){}"
    "};"
    "['log','info','warn','error','debug'].forEach(level=>{"
    "const original=pageConsole[level]&&"
    "pageConsole[level].bind(pageConsole);"
    "pageConsole[level]=function(...values){"
    "say(level,values);"
    "if(original)original(...values);"
    "};"
    "});"
    "pageConsole.__arosTeed=true;"
    "if(globalThis.addEventListener){"
    "globalThis.addEventListener('error',event=>"
    "say('uncaught',[(event&&event.error)||(event&&event.message)]));"
    "globalThis.addEventListener('unhandledrejection',event=>"
    "say('rejected',[event&&event.reason]));"
    "}"
    "}"
    "}catch(error){}"
    "";

/* Injected as the "preload" for a nodeIntegration window that has none of its
   own: it only publishes the renderer API the hook above hands to
   require('electron'). */
static const char electron_renderer_publish_only[] =
    "globalThis.__arosElectronRenderer=require('electron');";

static const char electron_renderer_runtime_head[] =
    "(function(){"
    "const snapshot=";
static const char electron_renderer_runtime_middle[] =
    ";"
    "const preloadPath=";
static const char electron_renderer_runtime_body[] =
    ";"
    "let nextId=1;"
    "const pending=new Map();"
    "const listeners=new Map();"
    "const binToBase64=bytes=>{"
    "let text='';"
    "for(let index=0;index<bytes.length;index+=8192)"
    "text+=String.fromCharCode.apply(null,bytes.subarray(index,index+8192));"
    "return btoa(text);"
    "};"
    "const base64ToBin=text=>{"
    "const raw=atob(text);"
    "const bytes=new Uint8Array(raw.length);"
    "for(let index=0;index<raw.length;index++)bytes[index]=raw.charCodeAt(index);"
    "return bytes;"
    "};"
    "const replacer=(key,value)=>{"
    "if(value instanceof Uint8Array)return {__arosBin:binToBase64(value)};"
    "if(value instanceof ArrayBuffer)"
    "return {__arosBin:binToBase64(new Uint8Array(value))};"
    "return value;"
    "};"
    "const reviver=(key,value)=>"
    "(value&&typeof value==='object'&&typeof value.__arosBin==='string')?"
    "base64ToBin(value.__arosBin):value;"
    "const post=message=>AROSElectronRenderer.send("
    "JSON.stringify(message,replacer));"
    "const ipcRenderer={"
    "invoke:(channel,...args)=>new Promise((resolve,reject)=>{"
    "const id=nextId++;"
    "pending.set(id,{resolve,reject});"
    "post({type:'invoke',id,channel:String(channel),args});"
    "}),"
    "send:(channel,...args)=>{post({type:'send',channel:String(channel),args});},"
    "sendSync:channel=>{"
    "throw new Error('ipcRenderer.sendSync('+channel+') is not supported on AROS');"
    "},"
    "postMessage:(channel,message)=>{"
    "post({type:'send',channel:String(channel),args:[message]});"
    "},"
    "on:(channel,listener)=>{"
    "const key=String(channel);"
    "if(!listeners.has(key))listeners.set(key,new Set());"
    "listeners.get(key).add(listener);"
    "return ipcRenderer;"
    "},"
    "once:(channel,listener)=>{"
    "const wrapper=(...values)=>{"
    "ipcRenderer.removeListener(channel,wrapper);"
    "listener(...values);"
    "};"
    "wrapper._arosOriginal=listener;"
    "return ipcRenderer.on(channel,wrapper);"
    "},"
    "removeListener:(channel,listener)=>{"
    "const set=listeners.get(String(channel));"
    "if(set)for(const entry of [...set])"
    "if(entry===listener||entry._arosOriginal===listener)set.delete(entry);"
    "return ipcRenderer;"
    "},"
    "off:(channel,listener)=>ipcRenderer.removeListener(channel,listener),"
    "removeAllListeners:channel=>{"
    "listeners.delete(String(channel));"
    "return ipcRenderer;"
    "}"
    "};"
    /* A MessagePortMain the main process transferred to this window arrives
       as a port id: pair it with a DOM MessageChannel, keep the near end as
       the relay (renderer->main port-message), hand the far end to the page
       as event.ports[0] so window.postMessage(nonce,'*',ports) transfers a
       real MessagePort into the workbench. */
    "const rendererPorts=new Map();"
    "const importPort=id=>{"
    "const channel=new MessageChannel();"
    "rendererPorts.set(id,channel.port1);"
    "channel.port1.onmessage=event=>{"
    "post({type:'port-message',port:id,data:event.data});"
    "};"
    "return channel.port2;"
    "};"
    "const importPorts=ids=>Array.isArray(ids)?ids.map(importPort):[];"
    "AROSElectronRenderer.deliver=text=>{"
    "let message;"
    "try{message=JSON.parse(text,reviver);}"
    "catch(error){console.error('[AROS-ELECTRON] bad delivery: '+error);return;}"
    "if(message.type==='reply'){"
    "const entry=pending.get(message.id);"
    "if(!entry)return;"
    "pending.delete(message.id);"
    "if(message.error!==undefined)entry.reject(new Error(String(message.error)));"
    "else entry.resolve(message.result);"
    "return;"
    "}"
    "if(message.type==='port-message'){"
    "const port=rendererPorts.get(message.port);"
    "if(port)port.postMessage(message.data,importPorts(message.ports));"
    "return;"
    "}"
    "if(message.type==='port-close'){"
    "const port=rendererPorts.get(message.port);"
    "if(port){rendererPorts.delete(message.port);port.close();}"
    "return;"
    "}"
    "if(message.type==='message'){"
    "const ports=importPorts(message.ports);"
    "const set=listeners.get(message.channel);"
    "if(!set)return;"
    "const event={sender:ipcRenderer,senderId:0,ports,"
    "reply:(channel,...args)=>ipcRenderer.send(channel,...args)};"
    "for(const listener of [...set])listener(event,...(message.args||[]));"
    "}"
    "};"
    "const webFrame={"
    "setZoomLevel:level=>{"
    "post({type:'web-frame.set-zoom-level',level:Number(level)||0});"
    "},"
    "getZoomLevel:()=>0,"
    "setZoomFactor:()=>{},"
    "getZoomFactor:()=>1"
    "};"
    "const contextBridge={"
    "exposeInMainWorld:(name,value)=>{globalThis[String(name)]=value;}"
    "};"
    "const webUtils={"
    "getPathForFile:file=>(file&&typeof file.path==='string')?file.path:''"
    "};"
    /* A nodeIntegration page gets the same electron object a preload does,
       and apps built that way reach for the desktop-integration modules from
       the page: GitHub Desktop calls shell.beep() and clipboard.writeText()
       in its renderer, so their absence threw on a read of 'beep' from
       undefined before the UI rendered a single pixel.
       openExternal really opens a browser - the renderer has node here, so it
       spawns C:OpenURL the same way the main process does. The rest report
       failure rather than pretending, and the clipboard says plainly that it
       does not reach the system clipboard yet. */
    "const shell={"
    "beep(){try{globalThis.require('electron');}catch(error){}},"
    "openExternal(url){"
    "return new Promise((resolve,reject)=>{"
    "try{"
    "const cp=globalThis.require('node:child_process');"
    "const child=cp.spawn('C:OpenURL',[String(url)],{stdio:'ignore'});"
    "child.on('error',reject);"
    "child.on('close',code=>code===0?resolve():"
    "reject(new Error('C:OpenURL exited with '+code)));"
    "}catch(error){reject(error);}"
    "});"
    "},"
    "openPath(){return Promise.resolve("
    "'shell.openPath is not implemented on AROS');},"
    "showItemInFolder(){},"
    "trashItem(){return Promise.reject(new Error("
    "'shell.trashItem is not implemented on AROS'));},"
    "moveItemToTrash(){return false;}"
    "};"
    "let clipboardText='';"
    "const clipboard={"
    "writeText(text){clipboardText=String(text);},"
    "readText(){return clipboardText;},"
    "writeHTML(){},readHTML(){return '';},"
    "clear(){clipboardText='';},"
    "availableFormats(){return clipboardText?['text/plain']:[];}"
    "};"
    "const electron={ipcRenderer,webFrame,contextBridge,webUtils,shell,"
    "clipboard};"
    "const processListeners=new Map();"
    "const process={"
    "type:'renderer',"
    "platform:snapshot.platform,"
    "arch:snapshot.arch,"
    "env:snapshot.env||{},"
    "versions:snapshot.versions||{},"
    "execPath:snapshot.execPath||'',"
    "pid:snapshot.pid||0,"
    "argv:snapshot.argv||[],"
    "sandboxed:true,"
    "contextIsolated:true,"
    "cwd:()=>snapshot.cwd||'',"
    "on:(name,listener)=>{"
    "const key=String(name);"
    "if(!processListeners.has(key))processListeners.set(key,new Set());"
    "processListeners.get(key).add(listener);"
    "return process;"
    "},"
    "once:(name,listener)=>process.on(name,listener),"
    "removeListener:(name,listener)=>{"
    "const set=processListeners.get(String(name));"
    "if(set)set.delete(listener);"
    "return process;"
    "},"
    "getProcessMemoryInfo:()=>ipcRenderer.invoke('aros:process-memory-info'),"
    "getSystemVersion:()=>'',"
    "nextTick:callback=>{Promise.resolve().then(callback);}"
    "};"
    "const require=name=>{"
    "if(name==='electron')return electron;"
    "throw new Error('Cannot find module \\''+name+"
    "'\\' in the AROS Electron renderer');"
    "};"
    "const module={exports:{}};"
    "const preloadDirectory=preloadPath.slice(0,Math.max("
    "preloadPath.lastIndexOf('/'),preloadPath.lastIndexOf(':')+1));"
    "try{"
    "(function(require,module,exports,process,__filename,__dirname,global){\n";
static const char electron_renderer_runtime_tail[] =
    "\n}).call(globalThis,require,module,module.exports,process,preloadPath,"
    "preloadDirectory,globalThis);"
    "}catch(error){"
    "console.error('[AROS-ELECTRON] preload failed: '+"
    "(error&&error.stack||error));"
    "}"
    "})();";

static void electron_ensure_state(void)
{
    Forbid();
    if (!electron_state_ready)
    {
        NEWLIST((struct List *)&electron_events);
        NEWLIST((struct List *)&electron_pending_resources);
        InitSemaphore(&electron_event_lock);
        InitSemaphore(&electron_resource_lock);
        InitSemaphore(&electron_window_lock);
        electron_state_ready = 1;
    }
    Permit();
}

static char *electron_strdup(const char *text)
{
    size_t length = text != NULL ? strlen(text) : 0;
    char *copy = AllocVec(length + 1, MEMF_ANY);

    if (copy == NULL)
        return NULL;
    if (length != 0)
        CopyMem((APTR)text, copy, length);
    copy[length] = '\0';
    return copy;
}

static char *electron_concat(const char *const *parts, ULONG count)
{
    size_t total = 0;
    ULONG index;
    char *result;
    char *cursor;

    for (index = 0; index < count; ++index)
        total += parts[index] != NULL ? strlen(parts[index]) : 0;
    result = AllocVec(total + 1, MEMF_ANY);
    if (result == NULL)
        return NULL;
    cursor = result;
    for (index = 0; index < count; ++index)
    {
        size_t length = parts[index] != NULL ? strlen(parts[index]) : 0;

        if (length != 0)
            CopyMem((APTR)parts[index], cursor, length);
        cursor += length;
    }
    *cursor = '\0';
    return result;
}

static LONG electron_parse_long(const char *text)
{
    LONG value = 0;
    int negative = 0;

    if (text == NULL)
        return 0;
    if (*text == '-')
    {
        negative = 1;
        ++text;
    }
    while (*text >= '0' && *text <= '9')
        value = value * 10 + (*text++ - '0');
    return negative ? -value : value;
}

static char *electron_utf16_to_utf8(const cef_char_t *text, size_t length)
{
    size_t index;
    size_t size = 0;
    char *result;
    char *out;

    if (text == NULL)
        length = 0;
    for (index = 0; index < length; ++index)
    {
        ULONG code = text[index];

        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < length &&
            text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
        {
            size += 4;
            ++index;
        }
        else if (code < 0x80)
            size += 1;
        else if (code < 0x800)
            size += 2;
        else
            size += 3;
    }
    result = AllocVec(size + 1, MEMF_ANY);
    if (result == NULL)
        return NULL;
    out = result;
    for (index = 0; index < length; ++index)
    {
        ULONG code = text[index];

        if (code >= 0xD800 && code <= 0xDBFF && index + 1 < length &&
            text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
        {
            code = 0x10000 + ((code - 0xD800) << 10) +
                (text[index + 1] - 0xDC00);
            ++index;
        }
        if (code < 0x80)
            *out++ = (char)code;
        else if (code < 0x800)
        {
            *out++ = (char)(0xC0 | (code >> 6));
            *out++ = (char)(0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            *out++ = (char)(0xE0 | (code >> 12));
            *out++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *out++ = (char)(0x80 | (code & 0x3F));
        }
        else
        {
            *out++ = (char)(0xF0 | (code >> 18));
            *out++ = (char)(0x80 | ((code >> 12) & 0x3F));
            *out++ = (char)(0x80 | ((code >> 6) & 0x3F));
            *out++ = (char)(0x80 | (code & 0x3F));
        }
    }
    *out = '\0';
    return result;
}

static cef_char_t *electron_utf8_to_utf16(const char *text,
    size_t *length_out)
{
    const unsigned char *in = (const unsigned char *)(text != NULL ? text : "");
    size_t length = strlen((const char *)in);
    size_t index = 0;
    size_t out = 0;
    cef_char_t *result = AllocVec((length + 1) * sizeof(cef_char_t), MEMF_ANY);

    if (result == NULL)
        return NULL;
    while (index < length)
    {
        ULONG code = in[index];
        size_t extra = 0;

        if (code >= 0xF0 && code < 0xF8)
        {
            code &= 0x07;
            extra = 3;
        }
        else if (code >= 0xE0)
        {
            code &= 0x0F;
            extra = 2;
        }
        else if (code >= 0xC0)
        {
            code &= 0x1F;
            extra = 1;
        }
        else if (code >= 0x80)
            code = 0xFFFD;
        ++index;
        while (extra != 0 && index < length && (in[index] & 0xC0) == 0x80)
        {
            code = (code << 6) | (in[index] & 0x3F);
            ++index;
            --extra;
        }
        if (extra != 0 || code > 0x10FFFF)
            code = 0xFFFD;
        if (code >= 0x10000)
        {
            code -= 0x10000;
            result[out++] = (cef_char_t)(0xD800 + (code >> 10));
            result[out++] = (cef_char_t)(0xDC00 + (code & 0x3FF));
        }
        else
            result[out++] = (cef_char_t)code;
    }
    result[out] = 0;
    if (length_out != NULL)
        *length_out = out;
    return result;
}

static void electron_set_cef_string(cef_string_t *target,
    const cef_char_t *value, size_t length)
{
    target->str = (cef_char_t *)value;
    target->length = length;
    target->dtor = NULL;
}

/* ASCII into a caller-provided UTF-16 buffer, for the short fixed strings. */
static void electron_set_ascii_string(cef_string_t *target,
    cef_char_t *buffer, size_t capacity, const char *ascii)
{
    size_t index = 0;

    while (ascii[index] != '\0' && index + 1 < capacity)
    {
        buffer[index] = (cef_char_t)(unsigned char)ascii[index];
        ++index;
    }
    buffer[index] = 0;
    electron_set_cef_string(target, buffer, index);
}

static char *electron_userfree_to_utf8(cef_string_userfree_t text)
{
    char *result;

    if (text == NULL)
        return electron_strdup("");
    result = electron_utf16_to_utf8(text->str, text->length);
    CEFStringUserfreeFree(text);
    return result;
}

/*
 * Events carry the slot of the app they belong to (electron_current_slot in
 * electron_bridge.c): with several apps in one process each app's runtime
 * polls this queue from its own pump, and without the tag the first poller
 * would swallow every event, including the did-finish-load another app's
 * window is waiting on to become visible. A poll only takes its own events.
 * Slot 0 is the primary app, not "anyone": treating it as a broadcast tag
 * let the second app's poll swallow the primary's did-finish-load (hosted
 * user-21, 2026-09-16, hello-A never showed). A single-app process tags
 * every event 0 and polls as 0, so strict matching costs it nothing.
 */

/* Takes ownership of text. */
static LONG electron_post_event_for(char *text, LONG slot)
{
    struct ElectronEvent *event;

    if (text == NULL)
        return 20;
    event = AllocVec(sizeof(*event), MEMF_ANY);
    if (event == NULL)
    {
        FreeVec(text);
        return 20;
    }
    event->text = text;
    event->slot = slot;
    electron_ensure_state();
    ObtainSemaphore(&electron_event_lock);
    AddTail((struct List *)&electron_events, (struct Node *)&event->node);
    ReleaseSemaphore(&electron_event_lock);
    return 0;
}

static LONG electron_post_event(char *text)
{
    return electron_post_event_for(text, electron_current_slot);
}

/*
 * The shell owns the native window, so window-system facts (the user closed
 * it, resized it, ...) enter the Electron object model from there:
 * `window\t<id>\t<name>\t<payload>` reaches the bootstrap's poll loop,
 * which turns it into the BrowserWindow event of the same name.
 */
static LONG electron_post_app_window_event(LONG slot, ULONG id,
    CONST_STRPTR name, CONST_STRPTR payload)
{
    char id_text[16];
    const char *parts[6];

    if (name == NULL || name[0] == '\0')
        return 20;
    *electron_append_long(id_text, (LONG)id) = '\0';
    parts[0] = "window\t";
    parts[1] = id_text;
    parts[2] = "\t";
    parts[3] = (const char *)name;
    parts[4] = "\t";
    parts[5] = payload != NULL ? (const char *)payload : "";
    return electron_post_event_for(electron_concat(parts, 6), slot);
}

/* The single-window form: the window prepared most recently, the app being
   serviced right now. */
static LONG electron_post_window_event(CONST_STRPTR name, CONST_STRPTR payload)
{
    return electron_post_app_window_event(electron_current_slot,
        electron_window_id, name, payload);
}

/*
 * The window registry (see struct ElectronWindow).
 */
static struct ElectronWindow *electron_find_window(LONG slot, ULONG id)
{
    int index;

    for (index = 0; index < ELECTRON_MAX_WINDOWS; ++index)
    {
        struct ElectronWindow *window = &electron_windows[index];

        if (window->in_use && window->slot == slot && window->id == id)
            return window;
    }
    return NULL;
}

static struct ElectronWindow *electron_find_window_by_browser(
    cef_browser_t *browser)
{
    int identifier;
    int index;

    if (browser == NULL)
        return NULL;
    identifier = browser->get_identifier(browser);
    for (index = 0; index < ELECTRON_MAX_WINDOWS; ++index)
    {
        struct ElectronWindow *window = &electron_windows[index];

        if (window->in_use && window->browser != NULL &&
            window->browser_identifier == identifier)
            return window;
    }
    return NULL;
}

static struct ElectronWindow *electron_find_or_create_window(LONG slot,
    ULONG id)
{
    struct ElectronWindow *window;
    int index;

    electron_ensure_state();
    ObtainSemaphore(&electron_window_lock);
    window = electron_find_window(slot, id);
    if (window == NULL)
    {
        for (index = 0; index < ELECTRON_MAX_WINDOWS; ++index)
        {
            if (!electron_windows[index].in_use)
            {
                window = &electron_windows[index];
                memset(window, 0, sizeof(*window));
                window->in_use = 1;
                window->slot = slot;
                window->id = id;
                break;
            }
        }
    }
    ReleaseSemaphore(&electron_window_lock);
    return window;
}

static void electron_release_window(struct ElectronWindow *window)
{
    if (window == NULL)
        return;
    electron_ensure_state();
    ObtainSemaphore(&electron_window_lock);
    if (window->browser != NULL)
        window->browser->base.release(&window->browser->base);
    FreeVec(window->preload_path);
    FreeVec(window->preload_source);
    FreeVec(window->process_snapshot);
    if (electron_window_last == window)
        electron_window_last = NULL;
    memset(window, 0, sizeof(*window));
    ReleaseSemaphore(&electron_window_lock);
}

/*
 * The window the bootstrap is talking about: the payload of every per-window
 * request starts with the window id and a tab, the app is the one being
 * serviced. Returns the rest of the payload.
 */
static CONST_STRPTR electron_window_from_payload(CONST_STRPTR payload,
    struct ElectronWindow **window)
{
    const char *cursor = (const char *)payload;
    ULONG id;

    *window = NULL;
    if (cursor == NULL)
        return "";
    id = (ULONG)electron_parse_long(cursor);
    while (*cursor >= '0' && *cursor <= '9')
        ++cursor;
    if (*cursor == '\t')
        ++cursor;
    *window = electron_find_or_create_window(electron_current_slot, id);
    return (CONST_STRPTR)cursor;
}

/* Bound browsers can go: the browser identifier is stable per browser, the
   record keeps its own reference. NULL unbinds and forgets the window. */
static LONG electron_bind_window_browser(LONG slot, ULONG id,
    cef_browser_t *browser)
{
    struct ElectronWindow *window;

    if (browser == NULL)
    {
        window = electron_find_window(slot, id);
        if (window != NULL && electron_active_browser == window->browser)
            electron_set_active_browser(NULL);
        electron_release_window(window);
        return window != NULL ? 0 : 5;
    }
    window = electron_find_or_create_window(slot, id);
    if (window == NULL)
        return 20;
    browser->base.add_ref(&browser->base);
    if (window->browser != NULL)
        window->browser->base.release(&window->browser->base);
    window->browser = browser;
    window->browser_identifier = browser->get_identifier(browser);
    electron_set_active_browser(browser);
    return 0;
}

static CONST_STRPTR electron_poll_event(void)
{
    struct ElectronEvent *event;

    electron_ensure_state();
    if (electron_event_current != NULL)
    {
        FreeVec(electron_event_current);
        electron_event_current = NULL;
    }
    ObtainSemaphore(&electron_event_lock);
    {
        struct ElectronEvent *scan;
        struct ElectronEvent *next;

        event = NULL;
        for (scan = (struct ElectronEvent *)electron_events.mlh_Head;
             (next = (struct ElectronEvent *)scan->node.mln_Succ) != NULL;
             scan = next)
        {
            if (scan->slot == electron_current_slot)
            {
                Remove((struct Node *)&scan->node);
                event = scan;
                break;
            }
        }
    }
    ReleaseSemaphore(&electron_event_lock);
    if (event == NULL)
        return "";
    electron_event_current = event->text;
    FreeVec(event);
    return electron_event_current;
}

/* ---- scheme handler factory + resource handler ---------------------- */

static void electron_resource_dispose(struct ElectronResource *resource)
{
    if (resource->open_callback != NULL)
        resource->open_callback->base.release(&resource->open_callback->base);
    if (resource->file != BNULL)
        Close(resource->file);
    FreeVec(resource->scheme);
    FreeVec(resource->url);
    FreeVec(resource->path);
    FreeVec(resource->headers);
    FreeVec(resource);
}

static void CEF_CALLBACK electron_resource_add_ref(
    cef_base_ref_counted_t *base)
{
    struct ElectronResource *resource = (struct ElectronResource *)base;

    __sync_add_and_fetch(&resource->refcount, 1);
}

static int CEF_CALLBACK electron_resource_release(
    cef_base_ref_counted_t *base)
{
    struct ElectronResource *resource = (struct ElectronResource *)base;

    if (__sync_sub_and_fetch(&resource->refcount, 1) == 0)
    {
        electron_resource_dispose(resource);
        return 1;
    }
    return 0;
}

static int CEF_CALLBACK electron_resource_has_one_ref(
    cef_base_ref_counted_t *base)
{
    return ((struct ElectronResource *)base)->refcount == 1;
}

static int CEF_CALLBACK electron_resource_has_at_least_one_ref(
    cef_base_ref_counted_t *base)
{
    return ((struct ElectronResource *)base)->refcount >= 1;
}

static int CEF_CALLBACK electron_resource_open(cef_resource_handler_t *self,
    cef_request_t *request, int *handle_request, cef_callback_t *callback)
{
    struct ElectronResource *resource = (struct ElectronResource *)self;
    char id_text[16];
    const char *parts[6];
    char *text;

    (void)request;
    *handle_request = 0;
    *electron_append_long(id_text, (LONG)resource->id) = '\0';
    parts[0] = "protocol\t";
    parts[1] = id_text;
    parts[2] = "\t";
    parts[3] = resource->scheme;
    parts[4] = "\t";
    parts[5] = resource->url;
    text = electron_concat(parts, 6);
    if (text == NULL)
        return 0;

    callback->base.add_ref(&callback->base);
    resource->open_callback = callback;
    /* The pending list holds its own reference until the script answers. */
    electron_resource_add_ref(&self->base);
    ObtainSemaphore(&electron_resource_lock);
    AddTail((struct List *)&electron_pending_resources,
        (struct Node *)&resource->node);
    ReleaseSemaphore(&electron_resource_lock);
    electron_post_event_for(text, resource->slot);
    return 1;
}

static const char *electron_mime_for_path(const char *path)
{
    static const struct
    {
        const char *extension;
        const char *mime;
    } table[] = {
        { "html", "text/html" }, { "htm", "text/html" },
        { "js", "text/javascript" }, { "mjs", "text/javascript" },
        { "css", "text/css" }, { "json", "application/json" },
        { "svg", "image/svg+xml" }, { "png", "image/png" },
        { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
        { "gif", "image/gif" }, { "webp", "image/webp" },
        { "ico", "image/x-icon" }, { "woff", "font/woff" },
        { "woff2", "font/woff2" }, { "ttf", "font/ttf" },
        { "otf", "font/otf" }, { "wasm", "application/wasm" },
        { "map", "application/json" }, { "txt", "text/plain" },
        { "md", "text/markdown" }, { "xml", "text/xml" },
        { "mp3", "audio/mpeg" }, { "mp4", "video/mp4" },
        { "webm", "video/webm" }, { "wav", "audio/wav" },
    };
    const char *dot = NULL;
    const char *cursor;
    ULONG index;

    for (cursor = path; *cursor != '\0'; ++cursor)
        if (*cursor == '.')
            dot = cursor + 1;
        else if (*cursor == '/' || *cursor == ':')
            dot = NULL;
    if (dot == NULL)
        return "application/octet-stream";
    for (index = 0; index < sizeof(table) / sizeof(table[0]); ++index)
    {
        const char *a = table[index].extension;
        const char *b = dot;

        while (*a != '\0' && *b != '\0' && *a == (*b | 0x20))
        {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0')
            return table[index].mime;
    }
    return "application/octet-stream";
}

/* Read one JSON string starting at `*cursor` (pointing at the quote). */
static char *electron_json_string(const char **cursor)
{
    const char *read = *cursor;
    char *result;
    char *write;

    if (*read != '"')
        return NULL;
    result = AllocVec(strlen(read) + 1, MEMF_ANY);
    if (result == NULL)
        return NULL;
    write = result;
    ++read;
    while (*read != '\0' && *read != '"')
    {
        if (*read == '\\' && read[1] != '\0')
        {
            ++read;
            switch (*read)
            {
            case 'n': *write++ = '\n'; break;
            case 't': *write++ = '\t'; break;
            case 'r': *write++ = '\r'; break;
            default: *write++ = *read; break;
            }
            ++read;
        }
        else
            *write++ = *read++;
    }
    *write = '\0';
    if (*read == '"')
        ++read;
    *cursor = read;
    return result;
}

static void electron_apply_headers(cef_response_t *response,
    const char *headers)
{
    const char *cursor = headers;

    while (*cursor != '\0')
    {
        char *name;
        char *value;

        while (*cursor != '\0' && *cursor != '"')
            ++cursor;
        if (*cursor == '\0')
            break;
        name = electron_json_string(&cursor);
        while (*cursor == ' ' || *cursor == ':')
            ++cursor;
        value = electron_json_string(&cursor);
        if (name != NULL && value != NULL)
        {
            cef_string_t name16;
            cef_string_t value16;
            size_t name_length;
            size_t value_length;
            cef_char_t *name_text = electron_utf8_to_utf16(name, &name_length);
            cef_char_t *value_text =
                electron_utf8_to_utf16(value, &value_length);

            if (name_text != NULL && value_text != NULL)
            {
                electron_set_cef_string(&name16, name_text, name_length);
                electron_set_cef_string(&value16, value_text, value_length);
                response->set_header_by_name(response, &name16, &value16, 1);
            }
            FreeVec(name_text);
            FreeVec(value_text);
        }
        FreeVec(name);
        FreeVec(value);
        while (*cursor != '\0' && *cursor != ',')
        {
            if (*cursor == '}')
                return;
            ++cursor;
        }
        if (*cursor == ',')
            ++cursor;
    }
}

static void CEF_CALLBACK electron_resource_get_response_headers(
    cef_resource_handler_t *self, cef_response_t *response,
    int64_t *response_length, cef_string_t *redirect_url)
{
    struct ElectronResource *resource = (struct ElectronResource *)self;
    cef_char_t text_buffer[96];
    cef_string_t text;

    (void)redirect_url;
    *response_length = 0;
    if (resource->error != 0 || resource->path == NULL)
    {
        response->set_error(response,
            resource->error != 0 ? resource->error : ERR_FAILED);
        return;
    }

    resource->file = Open(resource->path, MODE_OLDFILE);
    if (resource->file == BNULL)
    {
        bug("[ELECTRON-PROTOCOL] cannot open '%s' for %s (IoErr %ld)\n",
            resource->path, resource->url, (long)IoErr());
        response->set_error(response, ERR_FILE_NOT_FOUND);
        return;
    }
    Seek(resource->file, 0, OFFSET_END);
    resource->length = Seek(resource->file, 0, OFFSET_BEGINNING);

    response->set_status(response, 200);
    electron_set_ascii_string(&text, text_buffer,
        sizeof(text_buffer) / sizeof(text_buffer[0]), "OK");
    response->set_status_text(response, &text);
    electron_set_ascii_string(&text, text_buffer,
        sizeof(text_buffer) / sizeof(text_buffer[0]),
        electron_mime_for_path(resource->path));
    response->set_mime_type(response, &text);
    if (resource->headers != NULL)
        electron_apply_headers(response, resource->headers);
    *response_length = resource->length >= 0 ? resource->length : -1;
}

static int CEF_CALLBACK electron_resource_read(cef_resource_handler_t *self,
    void *data_out, int bytes_to_read, int *bytes_read,
    cef_resource_read_callback_t *callback)
{
    struct ElectronResource *resource = (struct ElectronResource *)self;
    LONG count;

    (void)callback;
    if (resource->file == BNULL)
    {
        *bytes_read = 0;
        return 0;
    }
    count = Read(resource->file, data_out, bytes_to_read);
    if (count > 0)
    {
        *bytes_read = count;
        return 1;
    }
    *bytes_read = count == 0 ? 0 : ERR_FAILED;
    return 0;
}

static void CEF_CALLBACK electron_resource_cancel(cef_resource_handler_t *self)
{
    struct ElectronResource *resource = (struct ElectronResource *)self;
    BPTR file;

    Forbid();
    file = resource->file;
    resource->file = BNULL;
    Permit();
    if (file != BNULL)
        Close(file);
}

static cef_resource_handler_t *CEF_CALLBACK electron_scheme_factory_create(
    cef_scheme_handler_factory_t *self, cef_browser_t *browser,
    cef_frame_t *frame, const cef_string_t *scheme_name,
    cef_request_t *request)
{
    struct ElectronResource *resource;
    struct ElectronWindow *window;

    (void)self;
    (void)frame;
    resource = AllocVec(sizeof(*resource), MEMF_CLEAR);
    if (resource == NULL)
        return NULL;
    /*
     * The request is answered by the protocol handler the REQUESTING app
     * registered, so it must reach that app's runtime.  This runs on a CEF
     * IO thread while the main pump may be servicing any app, so the pump's
     * current slot is the wrong answer with two apps up: a vscode-file://
     * script request that landed in GitHub Desktop's runtime found no
     * handler there and failed the whole workbench load (user-29,
     * 2026-09-16).  The browser that asked is bound to its window record
     * (ElectronBindWindowBrowser, from on_after_created); the fallbacks are
     * the ones renderer-context injection uses.
     */
    window = electron_find_window_by_browser(browser);
    if (window == NULL)
        window = electron_window_last;
    resource->slot = window != NULL ? window->slot : electron_current_slot;
    resource->handler.base.size = sizeof(cef_resource_handler_t);
    resource->handler.base.add_ref = electron_resource_add_ref;
    resource->handler.base.release = electron_resource_release;
    resource->handler.base.has_one_ref = electron_resource_has_one_ref;
    resource->handler.base.has_at_least_one_ref =
        electron_resource_has_at_least_one_ref;
    resource->handler.open = electron_resource_open;
    resource->handler.get_response_headers =
        electron_resource_get_response_headers;
    resource->handler.read = electron_resource_read;
    resource->handler.cancel = electron_resource_cancel;
    resource->refcount = 1;
    resource->scheme = scheme_name != NULL ?
        electron_utf16_to_utf8(scheme_name->str, scheme_name->length) :
        electron_strdup("");
    resource->url = electron_userfree_to_utf8(request->get_url(request));
    if (resource->scheme == NULL || resource->url == NULL)
    {
        electron_resource_dispose(resource);
        return NULL;
    }
    electron_ensure_state();
    ObtainSemaphore(&electron_resource_lock);
    resource->id = ++electron_resource_next_id;
    ReleaseSemaphore(&electron_resource_lock);
    return &resource->handler;
}

static void CEF_CALLBACK electron_static_add_ref(cef_base_ref_counted_t *base)
{
    (void)base;
}

static int CEF_CALLBACK electron_static_release(cef_base_ref_counted_t *base)
{
    (void)base;
    return 0;
}

static int CEF_CALLBACK electron_static_has_ref(cef_base_ref_counted_t *base)
{
    (void)base;
    return 1;
}

static void electron_ensure_scheme_factory(void)
{
    if (electron_scheme_factory_ready)
        return;
    memset(&electron_scheme_factory, 0, sizeof(electron_scheme_factory));
    electron_scheme_factory.base.size = sizeof(electron_scheme_factory);
    electron_scheme_factory.base.add_ref = electron_static_add_ref;
    electron_scheme_factory.base.release = electron_static_release;
    electron_scheme_factory.base.has_one_ref = electron_static_has_ref;
    electron_scheme_factory.base.has_at_least_one_ref = electron_static_has_ref;
    electron_scheme_factory.create = electron_scheme_factory_create;
    electron_scheme_factory_ready = 1;
}

static CONST_STRPTR electron_register_scheme_factory(CONST_STRPTR scheme,
    int remove)
{
    cef_string_t name;
    cef_char_t *text;
    size_t length;
    LONG rc;

    if (scheme == NULL || *scheme == '\0')
        return "no-scheme";
    electron_ensure_scheme_factory();
    text = electron_utf8_to_utf16((const char *)scheme, &length);
    if (text == NULL)
        return "no-memory";
    electron_set_cef_string(&name, text, length);
    rc = CEFRegisterSchemeHandlerFactory(&name, NULL,
        remove ? NULL : &electron_scheme_factory);
    FreeVec(text);
    bug("[ELECTRON-PROTOCOL] scheme '%s' factory %s rc=%ld\n",
        (const char *)scheme, remove ? "removed" : "registered", (long)rc);
    return rc != 0 ? "registered" : "register-failed";
}

/* `id \t ok|<errcode> \t path \t headers` from the protocol handler. */
static CONST_STRPTR electron_protocol_response(CONST_STRPTR payload)
{
    char *copy;
    char *fields[4] = { NULL, NULL, NULL, NULL };
    char *cursor;
    ULONG field_count = 0;
    ULONG id;
    struct ElectronResource *resource = NULL;
    struct MinNode *node;
    cef_callback_t *callback;

    copy = electron_strdup(payload != NULL ? (const char *)payload : "");
    if (copy == NULL)
        return "no-memory";
    cursor = copy;
    fields[field_count++] = cursor;
    while (*cursor != '\0' && field_count < 4)
    {
        if (*cursor == '\t')
        {
            *cursor = '\0';
            fields[field_count++] = cursor + 1;
        }
        ++cursor;
    }
    while (field_count < 4)
        fields[field_count++] = (char *)"";
    id = (ULONG)electron_parse_long(fields[0]);

    electron_ensure_state();
    ObtainSemaphore(&electron_resource_lock);
    /* The list node is not the first member, so walk MinNodes and recover
       the container by hand instead of using ForeachNode. */
    for (node = electron_pending_resources.mlh_Head; node->mln_Succ != NULL;
        node = node->mln_Succ)
    {
        struct ElectronResource *candidate = (struct ElectronResource *)
            ((char *)node - offsetof(struct ElectronResource, node));

        if (candidate->id == id)
        {
            Remove((struct Node *)node);
            resource = candidate;
            break;
        }
    }
    ReleaseSemaphore(&electron_resource_lock);
    if (resource == NULL)
    {
        bug("[ELECTRON-PROTOCOL] response for unknown request %lu\n",
            (unsigned long)id);
        FreeVec(copy);
        return "unknown";
    }

    if (strcmp(fields[1], "ok") == 0)
    {
        resource->error = 0;
        resource->path = electron_strdup(fields[2]);
        if (fields[3][0] != '\0')
            resource->headers = electron_strdup(fields[3]);
    }
    else
    {
        resource->error = electron_parse_long(fields[1]);
        if (resource->error == 0)
            resource->error = ERR_FAILED;
        bug("[ELECTRON-PROTOCOL] %s -> error %ld\n", resource->url,
            (long)resource->error);
    }
    FreeVec(copy);

    callback = resource->open_callback;
    resource->open_callback = NULL;
    if (callback != NULL)
    {
        callback->cont(callback);
        callback->base.release(&callback->base);
    }
    electron_resource_release(&resource->handler.base);
    return "continued";
}

/* ---- declared schemes ------------------------------------------------ */

static LONG electron_declare_scheme(CONST_STRPTR name, ULONG options)
{
    char *copy;

    (void)options;
    if (name == NULL || *name == '\0' ||
        electron_declared_scheme_count >= ELECTRON_MAX_SCHEMES)
        return 20;
    copy = electron_strdup((const char *)name);
    if (copy == NULL)
        return 20;
    electron_declared_schemes[electron_declared_scheme_count++] = copy;
    return 0;
}

static CONST_STRPTR electron_check_privileged_schemes(CONST_STRPTR payload)
{
    char *copy = electron_strdup(payload != NULL ? (const char *)payload : "");
    char *cursor;

    if (copy == NULL)
        return "no-memory";
    cursor = copy;
    while (*cursor != '\0')
    {
        char *name = cursor;
        ULONG index;
        int declared = 0;

        while (*cursor != '\0' && *cursor != ',')
            ++cursor;
        if (*cursor == ',')
            *cursor++ = '\0';
        for (index = 0; index < electron_declared_scheme_count; ++index)
            if (strcmp(electron_declared_schemes[index], name) == 0)
                declared = 1;
        if (!declared)
            bug("[ELECTRON-PROTOCOL] scheme '%s' registered as privileged "
                "but not declared at CEF init: pass --standard-schemes=%s "
                "(and --secure-schemes/--cors-schemes/--fetch-schemes as "
                "needed) to the shell\n", name, name);
    }
    FreeVec(copy);
    return "checked";
}

/* ---- renderer extension + main<->renderer execution ------------------- */

/*
 * Drive the renderer's own Node loop.
 *
 * A nodeIntegration page gets a Node runtime of its own, and node's bootstrap
 * replaces the page's setTimeout/setInterval with node's - which run on node's
 * uv loop, not Blink's. Nothing pumped that loop in the windowed shell, so in a
 * renderer every node timer and every async node call (fs/promises, net, child
 * processes) simply never completed: no error, no rejection, just silence.
 * GitHub Desktop stopped on the first `await access(...)` of its startup editor
 * scan, and its window stayed blank because React renders null until that
 * promise resolves.
 *
 * The pump has to run ON the renderer thread - only the thread that owns an
 * isolate may run it - and it has to be driven by something that ticks there.
 * Three candidates were measured before this one:
 *
 *   - A CEF task that reposts itself DELAYED: runs exactly once (pumps=1) and
 *     never again.
 *   - The same task reposted IMMEDIATELY: livelocks the thread; the page never
 *     runs at all.
 *   - One task posted per shell iteration from the browser side: also exactly
 *     one pump, because CefPostTask(TID_RENDERER) from another thread reports
 *     success and delivers nothing.
 *
 * What does tick reliably is Blink's own timers, verified in a plain renderer:
 * 0 ms, 1 s and 5 s timeouts and a repeating interval all fire. So the pump is
 * a Blink interval - the catch being that node's bootstrap REPLACES the page's
 * setTimeout/setInterval with its own, which is exactly what is broken. Hence
 * the capture script below: it saves Blink's timers before the Node attach, and
 * the driver uses the saved ones.
 */
static int electron_renderer_pump_running;

/*
 * Drives the renderer's Node loop from a Blink interval.
 *
 * The hard part is reaching Blink's setInterval at all - node's bootstrap has
 * already replaced setTimeout/setInterval on the page's global by the time
 * anything can be injected, and node's versions are precisely what cannot run
 * until the loop is pumped. Four routes were measured before this one:
 *
 *   - Save the timers before the attach: does not work, because
 *     execute_java_script QUEUES a script, so "save them first" still runs
 *     after node's bootstrap.
 *   - Do the attach from a native call mid-script to force the order: wedges
 *     the shell; node's bootstrap is not safe to run while V8 is executing the
 *     page's JS.
 *   - Reach the original through Window.prototype: there is nothing there.
 *     Blink installs setInterval as an OWN property of the global, which is
 *     exactly what node overwrote (measured: own property on the global is
 *     node's, Window/WindowProperties/EventTarget/Object all have none).
 *   - CEF tasks posted to the renderer thread: delayed ones run exactly once,
 *     immediate ones livelock the thread, cross-thread ones report success and
 *     deliver nothing.
 *
 * A fresh iframe, though, gets its own untouched Blink globals: its
 * setInterval is native code and it fires (verified in the page). So the pump
 * borrows the child document's timer to run the parent's Node loop.
 *
 * The iframe hangs off documentElement rather than body, so an app that
 * replaces the body - React does - does not take the pump with it. It is
 * created on DOMContentLoaded because at context-creation time there is no
 * document to attach it to yet.
 *
 * 16 ms, not 1 ms: at 1 ms the interval saturated the renderer thread and the
 * shell's own loop stopped getting turns. ~60 Hz is far more than a startup's
 * worth of timers and fs callbacks.
 */
static const cef_char_t electron_renderer_pump_driver[] =
    u"(function(){"
    u"var start=function(){"
    u"if(globalThis.__arosNodePumpTimer)return;"
    u"var root=document.documentElement||document.body;"
    u"if(!root)return;"
    u"var frame=document.createElement('iframe');"
    u"frame.style.display='none';"
    u"frame.setAttribute('aria-hidden','true');"
    u"root.appendChild(frame);"
    u"var view=frame.contentWindow;"
    u"if(!view||typeof view.setInterval!=='function'){"
    u"AROSElectronRenderer.log('node pump: no Blink timer available');"
    u"return;"
    u"}"
    u"globalThis.__arosNodePumpFrame=frame;"
    u"globalThis.__arosNodePumpTimer=view.setInterval(function(){"
    u"AROSElectronRenderer.pumpNode();"
    u"},16);"
    u"AROSElectronRenderer.log('node pump driver running');"
    u"};"
    u"if(document.readyState==='loading')"
    u"document.addEventListener('DOMContentLoaded',start);"
    u"else start();"
    u"document.addEventListener('readystatechange',start);"
    u"})();";

static ULONG electron_renderer_pumps;
static ULONG electron_renderer_pump_refused;

/* Runs one turn of the renderer's Node loop; called by the driver interval. */
static void electron_pump_renderer_node(void)
{
    static int pumping;

    /* Node callbacks run during a pump and can reach JS that pumps again;
       one turn at a time. */
    if (!electron_renderer_pump_running || pumping)
        return;
    pumping = 1;
    ++electron_renderer_pumps;
    NodePumpEmbedder();
    pumping = 0;
}

/*
 * Gives the page a Node runtime, called from the page's own script.
 *
 * A runtime of its OWN: without a slot the renderer thread falls back to its
 * creator's record - the main process's runtime, whose environment already
 * exists - so the attach refuses with rc=20. The slot stays selected for this
 * thread, so the renderer's later calls keep finding its own runtime rather
 * than the app's. This is only possible since node.library started keeping one
 * runtime per owner rather than one per library (2c77ef2b).
 */
static void electron_attach_renderer_node(void)
{
    LONG slot;
    LONG node_rc;

    if (electron_renderer_pump_running)
        return;
    slot = NodeCreateRuntimeSlot();
    if (slot > 0 && NodeSelectRuntimeSlot(slot) != 0)
    {
        NodeReleaseRuntimeSlot(slot);
        slot = 0;
    }
    /* No context->enter() here: the page's script is what is running, so the
       page's context is already the current one. */
    node_rc = NodeAttachCurrentV8Context(
        (CONST_STRPTR)electron_renderer_node_globals);
    if (node_rc != 0 && slot > 0)
    {
        NodeSelectRuntimeSlot(0);
        NodeReleaseRuntimeSlot(slot);
    }
    else if (node_rc == 0)
    {
        electron_renderer_pump_running = 1;
    }
    bug("[ELECTRON-RENDERER] nodeIntegration attach rc=%ld slot=%ld\n",
        (long)node_rc, (long)slot);
}

/* Injects one of the static scripts above into the page's frame. */
static void electron_run_renderer_script(cef_frame_t *frame,
    const cef_char_t *source, size_t length)
{
    cef_string_t code;
    cef_string_t url;

    if (frame == NULL || frame->execute_java_script == NULL)
        return;
    memset(&code, 0, sizeof(code));
    memset(&url, 0, sizeof(url));
    electron_set_cef_string(&code, source, length);
    electron_set_cef_string(&url, electron_renderer_script_url,
        sizeof(electron_renderer_script_url) / sizeof(cef_char_t) - 1);
    frame->execute_java_script(frame, &code, &url, 1);
}

/* One handler serves every native function in the extension, so the one being
   called has to be told apart by name. */
static int electron_native_name_is(const cef_string_t *name,
    const cef_char_t *wanted, size_t wanted_length)
{
    size_t i;

    if (name == NULL || name->str == NULL || name->length != wanted_length)
        return 0;
    for (i = 0; i < wanted_length; ++i)
    {
        if (name->str[i] != wanted[i])
            return 0;
    }
    return 1;
}

static int CEF_CALLBACK electron_renderer_execute(cef_v8handler_t *self,
    const cef_string_t *name, cef_v8value_t *object, size_t argument_count,
    cef_v8value_t *const *arguments, cef_v8value_t **retval,
    cef_string_t *exception)
{
    static const cef_char_t log_function[] = u"AROSElectronLog";
    static const cef_char_t pump_stats_function[] = u"AROSElectronPumpStats";
    static const cef_char_t pump_function[] = u"AROSElectronPumpNode";
    static const cef_char_t attach_function[] = u"AROSElectronAttachNode";

    (void)self;
    (void)object;
    (void)retval;
    (void)exception;
    /* These take no arguments, so they are answered before the
       string-argument natives below. */
    if (electron_native_name_is(name, pump_function,
        sizeof(pump_function) / sizeof(cef_char_t) - 1))
    {
        electron_pump_renderer_node();
        return 1;
    }
    if (electron_native_name_is(name, attach_function,
        sizeof(attach_function) / sizeof(cef_char_t) - 1))
    {
        electron_attach_renderer_node();
        return 1;
    }
    if (electron_native_name_is(name, pump_stats_function,
        sizeof(pump_stats_function) / sizeof(cef_char_t) - 1))
    {
        bug("[ELECTRON-RENDERER] pump stats pumps=%lu refused=%lu running=%ld\n",
            (unsigned long)electron_renderer_pumps,
            (unsigned long)electron_renderer_pump_refused,
            (long)electron_renderer_pump_running);
        return 1;
    }
    if (argument_count >= 1 && arguments[0]->is_string(arguments[0]))
    {
        char *text = electron_userfree_to_utf8(
            arguments[0]->get_string_value(arguments[0]));
        char id_text[16];
        const char *parts[4];

        if (text != NULL)
        {
            if (electron_native_name_is(name, log_function,
                sizeof(log_function) / sizeof(cef_char_t) - 1))
            {
                bug("[ELECTRON-PAGE] %s\n", text);
                FreeVec(text);
                return 1;
            }
            /* The extension prefixes the message with the window's own
               address, "<slot>\t<id>\t" (AROSElectronRenderer.__w), when the
               context was injected with one; a context without it is the
               window prepared most recently. */
            {
                const char *cursor = text;
                LONG slot = electron_current_slot;
                ULONG id = electron_window_id;

                if (*cursor >= '0' && *cursor <= '9')
                {
                    const char *probe = cursor;

                    while (*probe >= '0' && *probe <= '9')
                        ++probe;
                    if (*probe == '\t' && probe[1] >= '0' && probe[1] <= '9')
                    {
                        const char *second = probe + 1;

                        while (*second >= '0' && *second <= '9')
                            ++second;
                        if (*second == '\t')
                        {
                            slot = electron_parse_long(cursor);
                            id = (ULONG)electron_parse_long(probe + 1);
                            cursor = second + 1;
                        }
                    }
                }
                *electron_append_long(id_text, (LONG)id) = '\0';
                parts[0] = "ipc\t";
                parts[1] = id_text;
                parts[2] = "\t";
                parts[3] = cursor;
                electron_post_event_for(electron_concat(parts, 4), slot);
            }
            FreeVec(text);
        }
    }
    return 1;
}

static LONG electron_register_renderer_extension(void)
{
    cef_string_t name;
    cef_string_t code;

    memset(&electron_renderer_handler, 0, sizeof(electron_renderer_handler));
    electron_renderer_handler.base.size = sizeof(electron_renderer_handler);
    electron_renderer_handler.base.add_ref = electron_static_add_ref;
    electron_renderer_handler.base.release = electron_static_release;
    electron_renderer_handler.base.has_one_ref = electron_static_has_ref;
    electron_renderer_handler.base.has_at_least_one_ref =
        electron_static_has_ref;
    electron_renderer_handler.execute = electron_renderer_execute;
    electron_set_cef_string(&name, electron_renderer_extension_name,
        sizeof(electron_renderer_extension_name) / sizeof(cef_char_t) - 1);
    electron_set_cef_string(&code, electron_renderer_extension_code,
        sizeof(electron_renderer_extension_code) / sizeof(cef_char_t) - 1);
    return CEFRegisterV8Extension(&name, &code, &electron_renderer_handler);
}

static void electron_set_active_browser(cef_browser_t *browser)
{
    cef_browser_t *previous = electron_active_browser;

    if (browser != NULL)
        browser->base.add_ref(&browser->base);
    electron_active_browser = browser;
    if (previous != NULL)
        previous->base.release(&previous->base);
}

/* The browser a per-window request addresses: the window's own once the
   host has bound one, otherwise whatever is active (a request that arrives
   between browser-window.create and the browser's creation). */
static cef_browser_t *electron_window_browser(struct ElectronWindow *window)
{
    if (window != NULL && window->browser != NULL)
        return window->browser;
    return electron_active_browser;
}

static CONST_STRPTR electron_execute_in_renderer(cef_browser_t *browser,
    const char *code_utf8, const cef_char_t *url_text, size_t url_length)
{
    cef_frame_t *frame;
    cef_string_t code;
    cef_string_t url;
    cef_char_t *text;
    size_t length;

    if (browser == NULL)
        return "no-browser";
    frame = browser->get_main_frame(browser);
    if (frame == NULL)
        return "no-frame";
    text = electron_utf8_to_utf16(code_utf8, &length);
    if (text == NULL)
    {
        frame->base.release(&frame->base);
        return "no-memory";
    }
    electron_set_cef_string(&code, text, length);
    electron_set_cef_string(&url, url_text, url_length);
    frame->execute_java_script(frame, &code, &url, 0);
    frame->base.release(&frame->base);
    FreeVec(text);
    return "executed";
}

/* `web-contents.send` payload: id \t JSON literal of the message JSON */
static CONST_STRPTR electron_deliver_to_renderer(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *literal = electron_window_from_payload(payload, &window);
    const char *parts[3];
    char *code;
    CONST_STRPTR result;

    parts[0] = "AROSElectronRenderer.deliver(";
    parts[1] = literal[0] != '\0' ? literal : "\"\"";
    parts[2] = ")";
    code = electron_concat(parts, 3);
    if (code == NULL)
        return "no-memory";
    result = electron_execute_in_renderer(electron_window_browser(window),
        code, electron_renderer_deliver_url,
        sizeof(electron_renderer_deliver_url) / sizeof(cef_char_t) - 1);
    FreeVec(code);
    return result;
}

/* `web-contents.execute-script` payload: id \t JavaScript source */
static CONST_STRPTR electron_execute_in_window(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *code = electron_window_from_payload(payload, &window);

    return electron_execute_in_renderer(electron_window_browser(window),
        code, electron_renderer_script_url,
        sizeof(electron_renderer_script_url) / sizeof(cef_char_t) - 1);
}

/* `browser-window.set-zoom-level` payload: id \t level */
static CONST_STRPTR electron_set_zoom_level(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *level = electron_window_from_payload(payload, &window);
    cef_browser_t *browser = electron_window_browser(window);
    cef_browser_host_t *host;

    if (browser == NULL)
        return "no-browser";
    host = browser->get_host(browser);
    if (host == NULL)
        return "no-host";
    host->set_zoom_level(host, strtod(level, NULL));
    host->base.release(&host->base);
    return "zoomed";
}

/* `browser-window.node-integration` payload: id \t 0|1 */
static CONST_STRPTR electron_set_node_integration(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *cursor = electron_window_from_payload(payload, &window);

    if (window == NULL)
        return "no-window";
    window->node_integration = (*cursor == '1');
    bug("[ELECTRON-RENDERER] window %ld/%lu nodeIntegration=%ld\n",
        (long)window->slot, (unsigned long)window->id,
        (long)window->node_integration);
    return "stored";
}

/* `browser-window.create` payload: id \t preload \t additional args... */
static CONST_STRPTR electron_prepare_window(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *cursor = electron_window_from_payload(payload, &window);
    const char *preload_start;
    size_t preload_length = 0;
    BPTR file;
    LONG length;

    if (window == NULL)
        return "no-window";
    electron_window_id = window->id;
    electron_window_last = window;
    preload_start = cursor;
    while (*cursor != '\0' && *cursor != '\t')
    {
        ++cursor;
        ++preload_length;
    }

    FreeVec(window->preload_path);
    FreeVec(window->preload_source);
    window->preload_path = NULL;
    window->preload_source = NULL;
    if (preload_length != 0)
    {
        window->preload_path = AllocVec(preload_length + 1, MEMF_ANY);
        if (window->preload_path != NULL)
        {
            CopyMem((APTR)preload_start, window->preload_path,
                preload_length);
            window->preload_path[preload_length] = '\0';
            file = Open(window->preload_path, MODE_OLDFILE);
            if (file != BNULL)
            {
                Seek(file, 0, OFFSET_END);
                length = Seek(file, 0, OFFSET_BEGINNING);
                if (length >= 0)
                {
                    window->preload_source =
                        AllocVec((ULONG)length + 1, MEMF_ANY);
                    if (window->preload_source != NULL)
                    {
                        LONG got = Read(file, window->preload_source,
                            length);

                        if (got < 0)
                            got = 0;
                        window->preload_source[got] = '\0';
                    }
                }
                Close(file);
            }
            bug("[ELECTRON-RENDERER] window %ld/%lu preload '%s' %s "
                "(%ld bytes)\n",
                (long)window->slot, (unsigned long)window->id,
                window->preload_path,
                window->preload_source != NULL ? "loaded" : "NOT readable",
                window->preload_source != NULL ?
                (long)strlen(window->preload_source) : 0L);
        }
    }
    return electron_queue_request("browser-window.create", payload) == 0 ?
        "queued" : "queue-full";
}

/* `renderer.process-snapshot` payload: id \t JSON */
static CONST_STRPTR electron_store_process_snapshot(CONST_STRPTR payload)
{
    struct ElectronWindow *window;
    const char *json = electron_window_from_payload(payload, &window);

    if (window == NULL)
        return "no-window";
    FreeVec(window->process_snapshot);
    window->process_snapshot = electron_strdup(json[0] != '\0' ? json : "{}");
    return window->process_snapshot != NULL ? "stored" : "no-memory";
}

static char *electron_json_quote(const char *text)
{
    size_t length = strlen(text);
    char *result = AllocVec(length * 2 + 3, MEMF_ANY);
    char *write;

    if (result == NULL)
        return NULL;
    write = result;
    *write++ = '"';
    while (*text != '\0')
    {
        if (*text == '"' || *text == '\\')
            *write++ = '\\';
        *write++ = *text++;
    }
    *write++ = '"';
    *write = '\0';
    return result;
}

static LONG electron_inject_renderer_context(cef_browser_t *browser,
    cef_frame_t *frame, cef_v8context_t *context)
{
    const char *parts[9];
    char *quoted_path;
    char *script;
    cef_char_t *text;
    size_t length;
    cef_string_t code;
    cef_string_t url;
    cef_v8value_t *retval = NULL;
    cef_v8exception_t *exception = NULL;
    int rc;
    struct ElectronWindow *window;
    char *address;

    if (frame == NULL || context == NULL || !frame->is_main(frame))
        return 0;

    /*
     * Which window this context belongs to. The host binds the browser to
     * its window record in on_after_created, which runs before the first
     * context is created; a context whose browser is not bound (a host
     * predating ElectronBindWindowBrowser) is the window prepared most
     * recently, which is right for one window at a time.
     *
     * The address goes into the page first, so that every message the page
     * sends back (AROSElectronRenderer.send) carries it.
     */
    window = electron_find_window_by_browser(browser);
    if (window == NULL)
        window = electron_window_last;
    if (window == NULL)
        window = electron_find_or_create_window(electron_current_slot,
            electron_window_id);
    if (window == NULL)
        return 20;
    {
        char slot_text[16];
        char id_text[16];
        const char *address_parts[5];

        *electron_append_long(slot_text, window->slot) = '\0';
        *electron_append_long(id_text, (LONG)window->id) = '\0';
        address_parts[0] = "AROSElectronRenderer.__w=\"";
        address_parts[1] = slot_text;
        address_parts[2] = "\\t";
        address_parts[3] = id_text;
        address_parts[4] = "\";";
        address = electron_concat(address_parts, 5);
        if (address == NULL)
            return 20;
    }

    /*
     * nodeIntegration: give the page its own Node runtime.
     *
     * An app built the old Electron way - webPreferences {nodeIntegration:
     * true, contextIsolation: false} - has renderer code that calls require()
     * at page scope. GitHub Desktop's renderer bundle requires fs, path,
     * child_process, net, os and electron that way, so without this it dies
     * on its first line with "require is not defined" and paints nothing,
     * however well the main process is doing.
     *
     * This runs ON the renderer thread inside the page's own context, so
     * NodeAttachCurrentV8Context() attaches to exactly the right isolate.
     * That is only possible since node.library started keeping one runtime
     * per owner rather than one per library (2c77ef2b): the renderer thread
     * is its own task, so it gets its own record and does not collide with
     * the main process's runtime.
     */
    if (window->node_integration)
    {
        /*
         * Inside the page's context, not merely on its thread:
         * NodeAttachCurrentV8Context() reads Isolate::GetCurrent() and
         * GetCurrentContext(), and both are empty until the context is
         * entered.
         */
        context->enter(context);
        electron_attach_renderer_node();
        context->exit(context);
        /* And now something has to run the loop the page just acquired. */
        if (electron_renderer_pump_running)
            electron_run_renderer_script(frame,
                electron_renderer_pump_driver,
                sizeof(electron_renderer_pump_driver) /
                    sizeof(cef_char_t) - 1);
    }

    if (window->preload_source == NULL && !window->node_integration)
    {
        /* No preload and no Node, but the page still deserves to be heard:
           inject the console tee on its own so a plain window's logging and
           unhandled failures reach the AROS debug log like everything else. */
        const char *tee_parts[2];
        char *tee_script;
        cef_string_t tee_code;
        cef_string_t tee_url;
        cef_char_t *tee_text = NULL;
        size_t tee_length = 0;

        tee_parts[0] = address;
        tee_parts[1] = electron_renderer_console_tee;
        tee_script = electron_concat(tee_parts, 2);
        FreeVec(address);
        if (tee_script != NULL)
        {
            tee_text = electron_utf8_to_utf16(tee_script, &tee_length);
            FreeVec(tee_script);
        }
        if (tee_text != NULL)
        {
            memset(&tee_code, 0, sizeof(tee_code));
            memset(&tee_url, 0, sizeof(tee_url));
            electron_set_cef_string(&tee_code, tee_text, tee_length);
            electron_set_cef_string(&tee_url, electron_renderer_script_url,
                sizeof(electron_renderer_script_url) /
                    sizeof(cef_char_t) - 1);
            frame->execute_java_script(frame, &tee_code, &tee_url, 1);
            FreeVec(tee_text);
        }
        bug("[ELECTRON-RENDERER] window %ld/%lu main frame context created, "
            "no preload to inject (console tee only)\n",
            (long)window->slot, (unsigned long)window->id);
        return 0;
    }
    quoted_path = electron_json_quote(window->preload_path != NULL ?
        window->preload_path : "");
    if (quoted_path == NULL)
    {
        FreeVec(address);
        return 20;
    }
    parts[0] = address;
    parts[1] = electron_renderer_runtime_head;
    parts[2] = window->process_snapshot != NULL ?
        window->process_snapshot : "{}";
    parts[3] = electron_renderer_runtime_middle;
    parts[4] = quoted_path;
    parts[5] = electron_renderer_runtime_body;
    /* Before the app's preload, so whatever the preload logs is already being
       teed. */
    parts[6] = electron_renderer_console_tee;
    /* A nodeIntegration window with no preload of its own still gets the
       runtime, so require('electron') has something to return. */
    parts[7] = window->preload_source != NULL ?
        window->preload_source : electron_renderer_publish_only;
    parts[8] = electron_renderer_runtime_tail;
    script = electron_concat(parts, 9);
    FreeVec(quoted_path);
    FreeVec(address);
    if (script == NULL)
        return 20;
    text = electron_utf8_to_utf16(script, &length);
    FreeVec(script);
    if (text == NULL)
        return 20;
    electron_set_cef_string(&code, text, length);
    electron_set_cef_string(&url, electron_renderer_script_url,
        sizeof(electron_renderer_script_url) / sizeof(cef_char_t) - 1);

    context->enter(context);
    rc = context->eval(context, &code, &url, 1, &retval, &exception);
    if (exception != NULL)
    {
        char *message = electron_userfree_to_utf8(
            exception->get_message(exception));

        bug("[ELECTRON-RENDERER] preload injection threw at line %ld: %s\n",
            (long)exception->get_line_number(exception),
            message != NULL ? message : "?");
        FreeVec(message);
        exception->base.release(&exception->base);
    }
    if (retval != NULL)
        retval->base.release(&retval->base);
    context->exit(context);
    FreeVec(text);
    bug("[ELECTRON-RENDERER] preload injected into main frame rc=%ld "
        "(%lu chars)\n", (long)rc, (unsigned long)length);
    return rc != 0 ? 0 : 20;
}
