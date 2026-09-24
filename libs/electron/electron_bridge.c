/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * Native AROS Electron convergence adapter.
 *
 * Keep Node and CEF external.  This library is the compatibility seam where
 * Electron-specific assumptions can be reconciled without contaminating the
 * independently proven node.library, cef.library, or v8.library contracts.
 */

#include <aros/libcall.h>
#include <aros/debug.h>
#include <dos/dos.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/electron.h>
#include <proto/cef.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/node.h>
#include <proto/v8.h>

#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_callback_capi.h"
#include "include/capi/cef_frame_capi.h"
#include "include/capi/cef_request_capi.h"
#include "include/capi/cef_resource_handler_capi.h"
#include "include/capi/cef_response_capi.h"
#include "include/capi/cef_scheme_capi.h"
#include "include/capi/cef_task_capi.h"
#include "include/capi/cef_v8_capi.h"

#include "electron_libdefs.h"

#define AROS_ELECTRON_VERSION "0.1.0-aros"
#define ELECTRON_REQUEST_CAPACITY 8
#define ELECTRON_REQUEST_METHOD_SIZE 64
#define ELECTRON_REQUEST_PAYLOAD_SIZE 2048

struct ElectronNativeRequest
{
    char method[ELECTRON_REQUEST_METHOD_SIZE];
    char payload[ELECTRON_REQUEST_PAYLOAD_SIZE];
    /* The app runtime slot that made the request. Window ids are
       per app - every app's first BrowserWindow is id 1 - so a host serving
       several apps needs the slot to tell their windows apart. */
    LONG slot;
};

static struct ElectronNativeRequest
    electron_requests[ELECTRON_REQUEST_CAPACITY];
static ULONG electron_request_head;
static ULONG electron_request_tail;

/*
 * Which app is being serviced right now.
 *
 * With several apps in one process each has its own Node runtime, and each of
 * those runtimes talks to this library from its own pump. Requests and events
 * carry the slot that was current when they were made, so a host can route a
 * request to the right window and a poll only takes its own app's events (or
 * untagged ones, which is every event a single-app process makes).
 */
static LONG electron_current_slot;
static char electron_sync_result[32];
static char electron_screen_result[96];

static char *electron_append_long(char *output, LONG value)
{
    char digits[16];
    ULONG magnitude;
    ULONG count = 0;

    if (value < 0)
    {
        *output++ = '-';
        magnitude = (ULONG)(-(value + 1)) + 1;
    }
    else
        magnitude = (ULONG)value;

    do
    {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);

    while (count != 0)
        *output++ = digits[--count];
    return output;
}

static CONST_STRPTR electron_screen_snapshot(void)
{
    struct Screen *screen = LockPubScreen(NULL);
    char *output = electron_screen_result;
    LONG values[5];
    ULONG index;

    if (screen == NULL)
        return "640,480,0,0,0";

    values[0] = screen->Width;
    values[1] = screen->Height;
    values[2] = screen->BarHeight + 1;
    values[3] = screen->MouseX;
    values[4] = screen->MouseY;
    for (index = 0; index < 5; ++index)
    {
        if (index != 0)
            *output++ = ',';
        output = electron_append_long(output, values[index]);
    }
    *output = '\0';
    UnlockPubScreen(NULL, screen);
    return electron_screen_result;
}

static void electron_decode_component(char *text)
{
    char *read = text;
    char *write = text;

    while (*read != '\0')
    {
        if (*read == '%' &&
            ((read[1] >= '0' && read[1] <= '9') ||
             (read[1] >= 'A' && read[1] <= 'F') ||
             (read[1] >= 'a' && read[1] <= 'f')) &&
            ((read[2] >= '0' && read[2] <= '9') ||
             (read[2] >= 'A' && read[2] <= 'F') ||
             (read[2] >= 'a' && read[2] <= 'f')))
        {
            unsigned int high =
                read[1] <= '9' ? read[1] - '0' :
                (read[1] & ~0x20) - 'A' + 10;
            unsigned int low =
                read[2] <= '9' ? read[2] - '0' :
                (read[2] & ~0x20) - 'A' + 10;
            *write++ = (char)((high << 4) | low);
            read += 3;
        }
        else
            *write++ = *read++;
    }
    *write = '\0';
}

static CONST_STRPTR electron_show_message_box(CONST_STRPTR payload)
{
    char *copy;
    char *fields[4] = { NULL, NULL, NULL, NULL };
    char *buttons[16];
    char *cursor;
    char *gadgets;
    char *message;
    ULONG field_count = 0;
    ULONG button_count = 0;
    ULONG gadget_length = 1;
    ULONG message_length;
    LONG result;
    ULONG index;
    struct EasyStruct easy;
    IPTR arguments[1];

    if (payload == NULL)
        payload = (CONST_STRPTR)"";
    copy = AllocVec(strlen((const char *)payload) + 1, MEMF_ANY);
    if (copy == NULL)
        return "-1";
    strcpy(copy, (const char *)payload);

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
    for (index = 0; index < 3; ++index)
        electron_decode_component(fields[index]);

    cursor = fields[3];
    while (*cursor != '\0' && button_count < 16)
    {
        buttons[button_count++] = cursor;
        while (*cursor != '\0' && *cursor != '\x1f')
            ++cursor;
        if (*cursor == '\x1f')
            *cursor++ = '\0';
    }
    if (button_count == 0)
    {
        buttons[button_count++] = (char *)"OK";
    }
    for (index = 0; index < button_count; ++index)
    {
        const char *label;

        electron_decode_component(buttons[index]);
        gadget_length += strlen(buttons[index]) + 1;
        for (label = buttons[index]; *label != '\0'; ++label)
            if (*label == '%')
                ++gadget_length;
    }
    gadgets = AllocVec(gadget_length, MEMF_ANY);
    if (gadgets == NULL)
    {
        FreeVec(copy);
        return "-1";
    }
    gadgets[0] = '\0';
    for (index = 0; index < button_count; ++index)
    {
        const char *label;
        char *end;

        if (index != 0)
            strcat(gadgets, "|");
        end = gadgets + strlen(gadgets);
        for (label = buttons[index]; *label != '\0'; ++label)
        {
            *end++ = *label;
            if (*label == '%')
                *end++ = '%';
        }
        *end = '\0';
    }

    message_length = strlen(fields[1]) + strlen(fields[2]) + 3;
    message = AllocVec(message_length, MEMF_ANY);
    if (message == NULL)
    {
        FreeVec(gadgets);
        FreeVec(copy);
        return "-1";
    }
    strcpy(message, fields[1]);
    if (fields[2][0] != '\0')
    {
        strcat(message, "\n\n");
        strcat(message, fields[2]);
    }

    easy.es_StructSize = sizeof(easy);
    easy.es_Flags = 0;
    easy.es_Title = (STRPTR)(fields[0][0] != '\0' ?
        fields[0] : "AROS Electron");
    easy.es_TextFormat = (STRPTR)"%s";
    easy.es_GadgetFormat = gadgets;
    arguments[0] = (IPTR)message;
    result = EasyRequestArgs(NULL, &easy, NULL, (RAWARG)arguments);

    index = result == 0 ? button_count - 1 : (ULONG)result - 1;
    if (index >= 10)
    {
        electron_sync_result[0] = (char)('0' + index / 10);
        electron_sync_result[1] = (char)('0' + index % 10);
        electron_sync_result[2] = '\0';
    }
    else
    {
        electron_sync_result[0] = (char)('0' + index);
        electron_sync_result[1] = '\0';
    }
    FreeVec(message);
    FreeVec(gadgets);
    FreeVec(copy);
    return (CONST_STRPTR)electron_sync_result;
}

/*
 * Keep Electron's Node-facing compatibility policy in this adapter rather
 * than patching the independently usable node.library.  This first append-only
 * module seam deliberately exposes only the app lifecycle surface that has a
 * proven implementation.  BrowserWindow and IPC will be added when their
 * native contracts exist instead of advertising inert stubs.
 */
static const char electron_module_bootstrap[] =
    "(()=>{"
    "const binding=process._linkedBinding('aros_electron');"
    "const describeError=value=>{"
    "if(value&&typeof value.stack==='string')return value.stack;"
    "try{return typeof value==='string'?value:JSON.stringify(value);}"
    "catch{return String(value);}"
    "};"
    "const nativeConsoleError=console.error.bind(console);"
    "console.error=(...values)=>{"
    "binding.invoke('console-error',values.map(describeError).join(' '));"
    "return nativeConsoleError(...values);"
    "};"
    "process.on('uncaughtException',error=>"
    "binding.invoke('uncaught-exception',describeError(error)));"
    "process.on('unhandledRejection',reason=>"
    "binding.invoke('unhandled-rejection',describeError(reason)));"
    "if(!process.versions.electron)process.versions.electron='0.1.0-aros';"
    "if(!process.type)process.type='browser';"
    "if(!process.resourcesPath)process.resourcesPath=process.cwd();"
    "process.env.VSCODE_HANDLES_SIGPIPE='1';"
    "if(typeof globalThis.performance==='undefined')"
    "globalThis.performance=require('node:perf_hooks').performance;"
    "const native=Object.freeze({"
    "ping:()=>binding.invoke('ping','')"
    "});"
    "const {EventEmitter}=require('node:events');"
    "const electronPath=require('node:path');"
    "const electronOs=require('node:os');"
    "const workerThreads=require('node:worker_threads');"
    "const app=new EventEmitter();"
    "let appName='AROS Electron';"
    "const homePath=electronOs.homedir();"
    "const appDataPath=process.env.APPDATA||homePath;"
    "const appPaths=new Map(["
    "['home',homePath],"
    "['appData',appDataPath],"
    "['userData',electronPath.join(appDataPath,appName)],"
    "['sessionData',electronPath.join(appDataPath,appName)],"
    "['temp',electronOs.tmpdir()],"
    "['exe',process.execPath],"
    "['module',process.execPath],"
    "['desktop',electronPath.join(homePath,'Desktop')],"
    "['documents',electronPath.join(homePath,'Documents')],"
    "['downloads',electronPath.join(homePath,'Downloads')],"
    "['music',electronPath.join(homePath,'Music')],"
    "['pictures',electronPath.join(homePath,'Pictures')],"
    "['videos',electronPath.join(homePath,'Videos')],"
    "['logs',electronPath.join(appDataPath,appName,'logs')],"
    "['crashDumps',electronPath.join(appDataPath,appName,'Crashpad')]"
    "]);"
    "let appReady=false;"
    "let resolveAppReady;"
    "const appReadyPromise=new Promise(resolve=>{resolveAppReady=resolve;});"
    "const commandLineSwitches=new Map();"
    "const commandLineArguments=[];"
    "for(const argument of process.argv.slice(1)){"
    "if(argument.startsWith('--')){"
    "const separator=argument.indexOf('=');"
    "const name=(separator<0?argument.slice(2):"
    "argument.slice(2,separator)).toLowerCase();"
    "const value=separator<0?'':argument.slice(separator+1);"
    "commandLineSwitches.set(name,value);"
    "}else commandLineArguments.push(argument);"
    "}"
    "const commandLine=Object.freeze({"
    "hasSwitch:name=>commandLineSwitches.has(String(name).toLowerCase()),"
    "getSwitchValue:name=>"
    "commandLineSwitches.get(String(name).toLowerCase())||'',"
    "appendSwitch:(name,value)=>{"
    "commandLineSwitches.set(String(name).toLowerCase(),"
    "value===undefined?'':String(value));"
    "},"
    "removeSwitch:name=>commandLineSwitches.delete("
    "String(name).toLowerCase()),"
    "appendArgument:value=>commandLineArguments.push(String(value))"
    "});"
    "app.isReady=()=>appReady;"
    "app.whenReady=()=>appReadyPromise;"
    "app.getName=()=>appName;"
    "app.setName=value=>{appName=String(value);};"
    "app.getVersion=()=>'0.1.0-aros';"
    "app.commandLine=commandLine;"
    "app.getAppPath=()=>process.cwd();"
    "app.getPath=name=>{"
    "const key=String(name);"
    "if(!appPaths.has(key))throw new Error('Unknown Electron path: '+key);"
    "return appPaths.get(key);"
    "};"
    "app.setPath=(name,value)=>{"
    "const key=String(name);"
    "const path=String(value);"
    "if(!path)throw new TypeError('Electron path must not be empty');"
    "appPaths.set(key,path);"
    "};"
    "app.setAppLogsPath=value=>{"
    "appPaths.set('logs',value===undefined?"
    "electronPath.join(appPaths.get('userData'),'logs'):String(value));"
    "};"
    "const preferredSystemLanguages=(()=>{"
    "const values=[...(process.env.LANGUAGE||'').split(':'),"
    "process.env.LC_ALL,process.env.LC_MESSAGES,process.env.LANG,"
    "Intl.DateTimeFormat().resolvedOptions().locale];"
    "const languages=[];"
    "for(let value of values){"
    "if(!value)continue;"
    "value=String(value).split('.')[0].replace(/_/g,'-');"
    "if(value==='C'||value==='POSIX')value='en';"
    "if(!languages.includes(value))languages.push(value);"
    "}"
    "return languages.length?languages:['en'];"
    "})();"
    "app.getPreferredSystemLanguages=()=>preferredSystemLanguages.slice();"
    "app.getLocale=()=>preferredSystemLanguages[0];"
    "app.getSystemLocale=()=>preferredSystemLanguages[0];"
    "app.exit=code=>binding.invoke('app.exit',String(code===undefined?0:code));"
    /* Electron's quit is cancellable at both steps: VS Code's lifecycle
       service preventDefault()s 'will-quit' once, runs its shutdown
       promise, then calls app.quit() again with the listener removed. */
    /* Single-instance lock: GitHub Desktop calls this before it does
       anything else and quits when the answer is false, which on AROS made
       it exit with no window and no message.  One Workbench, one instance of
       a program launched from it - there is no second process to hand the
       command line to - so the lock is always ours.  releaseSingleInstanceLock
       exists so a caller that releases it does not throw. */
    "let singleInstanceLock=true;"
    "app.requestSingleInstanceLock=()=>singleInstanceLock;"
    "app.hasSingleInstanceLock=()=>singleInstanceLock;"
    "app.releaseSingleInstanceLock=()=>{singleInstanceLock=false;};"
    /* Protocol registration is a desktop-integration nicety (x-github-client://
       handed to Desktop by the browser). AROS has no URL-scheme registry yet,
       so report that we are not the handler rather than claiming to be one. */
    "app.setAsDefaultProtocolClient=()=>false;"
    "app.isDefaultProtocolClient=()=>false;"
    "app.removeAsDefaultProtocolClient=()=>false;"
    "app.quit=()=>{"
    "const quitEvent=()=>({defaultPrevented:false,preventDefault(){this.defaultPrevented=true;}});"
    "const beforeQuit=quitEvent();"
    "app.emit('before-quit',beforeQuit);"
    "if(beforeQuit.defaultPrevented)return;"
    "const willQuit=quitEvent();"
    "app.emit('will-quit',willQuit);"
    "if(willQuit.defaultPrevented)return;"
    "app.emit('quit',quitEvent(),0);"
    "return binding.invoke('app.exit','0');"
    "};"
    /* Menu/MenuItem keep Electron's object model in JS (VS Code walks
       menu.items and MenuItem.submenu when it rebuilds the menubar). The
       native side only receives a one-line summary of the top level: the
       seam's request payload is small and the shell has no menu strip yet,
       so the summary is what a future menu renderer will start from. */
    "let applicationMenu=null;"
    "let nextMenuItemId=1;"
    "class MenuItem{"
    "constructor(options={}){"
    "if(options===null||typeof options!=='object')"
    "throw new TypeError('MenuItem requires an options object');"
    "this.commandId=nextMenuItemId++;"
    "this.id=options.id!==undefined?String(options.id):undefined;"
    "this.label=options.label!==undefined?String(options.label):'';"
    "this.sublabel=options.sublabel!==undefined?String(options.sublabel):'';"
    "this.toolTip=options.toolTip!==undefined?String(options.toolTip):'';"
    "this.role=options.role;"
    "this.accelerator=options.accelerator;"
    "this.icon=options.icon;"
    "this.enabled=options.enabled!==false;"
    "this.visible=options.visible!==false;"
    "this.checked=!!options.checked;"
    "this.registerAccelerator=options.registerAccelerator!==false;"
    "this.click=typeof options.click==='function'?options.click:undefined;"
    "this.menu=null;"
    "let submenu=options.submenu;"
    "if(Array.isArray(submenu))submenu=Menu.buildFromTemplate(submenu);"
    "if(submenu!==undefined&&submenu!==null&&!(submenu instanceof Menu))"
    "throw new TypeError('submenu must be a Menu or a template array');"
    "this.submenu=submenu||undefined;"
    "this.type=options.type||(this.submenu?'submenu':'normal');"
    "}"
    "}"
    "class Menu{"
    "constructor(){this.items=[];}"
    "append(item){this.insert(this.items.length,item);}"
    "insert(position,item){"
    "if(!(item instanceof MenuItem))"
    "throw new TypeError('Menu.insert requires a MenuItem');"
    "item.menu=this;"
    "this.items.splice(Number(position),0,item);"
    "}"
    "getMenuItemById(id){"
    "for(const item of this.items){"
    "if(item.id===id)return item;"
    "const nested=item.submenu?item.submenu.getMenuItemById(id):null;"
    "if(nested)return nested;"
    "}"
    "return null;"
    "}"
    "popup(){}closePopup(){}"
    "static setApplicationMenu(menu){"
    "if(menu!==null&&!(menu instanceof Menu))"
    "throw new TypeError('application menu must be a Menu or null');"
    "applicationMenu=menu;"
    "binding.invoke('menu.set-application-menu',menu?"
    "menu.items.map(item=>item.type==='separator'?'-':"
    "item.label.replace(/&&/g,'&')).join('\\t'):'');"
    "}"
    "static getApplicationMenu(){return applicationMenu;}"
    "static sendActionToFirstResponder(){}"
    "static buildFromTemplate(template){"
    "if(!Array.isArray(template))throw new TypeError('menu template must be an array');"
    "const menu=new Menu();"
    "for(const item of template)"
    "menu.append(item instanceof MenuItem?item:new MenuItem(item));"
    "return menu;"
    "}"
    "}"
    "const privilegedSchemes=new Map();"
    "const globalFileProtocols=new Map();"
    "const globalInterceptedFileProtocols=new Map();"
    "const registerGlobalFileProtocol=(kind,scheme,handler)=>{"
    "const key=String(scheme);"
    "if(!key||typeof handler!=='function')"
    "throw new TypeError(kind+' requires a scheme and handler');"
    "const registry=kind.startsWith('protocol.intercept-')?"
    "globalInterceptedFileProtocols:globalFileProtocols;"
    "registry.set(key,handler);"
    "binding.invoke(kind,key);"
    "};"
    "const protocol=Object.freeze({"
    "registerSchemesAsPrivileged:schemes=>{"
    "if(appReady)throw new Error("
    "'protocol schemes must be registered before app ready');"
    "if(!Array.isArray(schemes))"
    "throw new TypeError('protocol schemes must be an array');"
    "for(const entry of schemes){"
    "if(!entry||typeof entry.scheme!=='string'||!entry.scheme)"
    "throw new TypeError('protocol scheme must be a non-empty string');"
    "privilegedSchemes.set(entry.scheme,"
    "Object.freeze({...entry.privileges}));"
    "}"
    "binding.invoke('protocol.register-schemes-as-privileged',"
    "[...privilegedSchemes.keys()].join(','));"
    "},"
    "isProtocolHandled:scheme=>"
    "Promise.resolve(privilegedSchemes.has(String(scheme))||"
    "globalFileProtocols.has(String(scheme))||"
    "globalInterceptedFileProtocols.has(String(scheme))),"
    "registerFileProtocol:(scheme,handler)=>"
    "registerGlobalFileProtocol('protocol.register-file-protocol',"
    "scheme,handler),"
    "registerBufferProtocol:(scheme,handler)=>"
    "registerGlobalFileProtocol('protocol.register-buffer-protocol',"
    "scheme,handler),"
    "registerHttpProtocol:(scheme,handler)=>"
    "registerGlobalFileProtocol('protocol.register-http-protocol',"
    "scheme,handler),"
    "interceptFileProtocol:(scheme,handler)=>"
    "registerGlobalFileProtocol('protocol.intercept-file-protocol',"
    "scheme,handler),"
    "unregisterProtocol:scheme=>{"
    "const key=String(scheme);"
    "if(globalFileProtocols.delete(key))"
    "binding.invoke('protocol.unregister-protocol',key);"
    "return Promise.resolve();"
    "},"
    "uninterceptProtocol:scheme=>{"
    "const key=String(scheme);"
    "if(globalInterceptedFileProtocols.delete(key))"
    "binding.invoke('protocol.unintercept-protocol',key);"
    "return Promise.resolve();"
    "}"
    "});"
    "let permissionRequestHandler=null;"
    "let permissionCheckHandler=null;"
    "let codeCachePath='';"
    /* The full webRequest surface, not only the three VS Code uses: GitHub
       Desktop binds every one of Electron's eight observers as it starts
       (new Ze(...) wrapping session.defaultSession.webRequest), so a missing
       member threw "Cannot read properties of undefined (reading 'bind')"
       before its first window.
       The three above are consulted by the CEF seam; the five observers are
       registered and kept but never fired, because nothing reports redirects,
       completions or errors back from the network stack yet. A caller gets
       its registration accepted and no events - not a crash, and not a
       pretence that interception happened. */
    "const requestHandlers={"
    "beforeRequest:[],headersReceived:[],beforeSendHeaders:[],"
    "beforeRedirect:[],completed:[],errorOccurred:[],"
    "responseStarted:[],sendHeaders:[]"
    "};"
    "const registerRequestHandler=(kind,filterOrListener,listener)=>{"
    "const handler=typeof listener==='function'?listener:filterOrListener;"
    "if(typeof handler!=='function')"
    "throw new TypeError('webRequest listener must be a function');"
    "requestHandlers[kind].push(handler);"
    "};"
    "const webRequest=Object.freeze({"
    "onBeforeRequest:(filterOrListener,listener)=>"
    "registerRequestHandler('beforeRequest',filterOrListener,listener),"
    "onHeadersReceived:(filterOrListener,listener)=>"
    "registerRequestHandler('headersReceived',filterOrListener,listener),"
    "onBeforeSendHeaders:(filterOrListener,listener)=>"
    "registerRequestHandler('beforeSendHeaders',filterOrListener,listener),"
    "onBeforeRedirect:(filterOrListener,listener)=>"
    "registerRequestHandler('beforeRedirect',filterOrListener,listener),"
    "onCompleted:(filterOrListener,listener)=>"
    "registerRequestHandler('completed',filterOrListener,listener),"
    "onErrorOccurred:(filterOrListener,listener)=>"
    "registerRequestHandler('errorOccurred',filterOrListener,listener),"
    "onResponseStarted:(filterOrListener,listener)=>"
    "registerRequestHandler('responseStarted',filterOrListener,listener),"
    "onSendHeaders:(filterOrListener,listener)=>"
    "registerRequestHandler('sendHeaders',filterOrListener,listener)"
    "});"
    "const sessionFileProtocols=new Map();"
    "const sessionInterceptedFileProtocols=new Map();"
    "const sessionProtocol=Object.freeze({"
    "registerFileProtocol:(scheme,handler)=>{"
    "const key=String(scheme);"
    "if(!key||typeof handler!=='function')"
    "throw new TypeError('registerFileProtocol requires a scheme and handler');"
    "sessionFileProtocols.set(key,handler);"
    "binding.invoke('session.register-file-protocol',key);"
    "},"
    "interceptFileProtocol:(scheme,handler)=>{"
    "const key=String(scheme);"
    "if(!key||typeof handler!=='function')"
    "throw new TypeError('interceptFileProtocol requires a scheme and handler');"
    "sessionInterceptedFileProtocols.set(key,handler);"
    "binding.invoke('session.intercept-file-protocol',key);"
    "},"
    "unregisterProtocol:scheme=>{"
    "const key=String(scheme);"
    "const removed=sessionFileProtocols.delete(key);"
    "if(removed)binding.invoke('session.unregister-protocol',key);"
    "return Promise.resolve();"
    "},"
    "uninterceptProtocol:scheme=>{"
    "const key=String(scheme);"
    "const removed=sessionInterceptedFileProtocols.delete(key);"
    "if(removed)binding.invoke('session.unintercept-protocol',key);"
    "return Promise.resolve();"
    "}"
    "});"
    "const defaultSession=Object.freeze({"
    "setPermissionRequestHandler:handler=>{"
    "if(handler!==null&&typeof handler!=='function')"
    "throw new TypeError('permission request handler must be a function or null');"
    "permissionRequestHandler=handler;"
    "},"
    "setPermissionCheckHandler:handler=>{"
    "if(handler!==null&&typeof handler!=='function')"
    "throw new TypeError('permission check handler must be a function or null');"
    "permissionCheckHandler=handler;"
    "},"
    "setCodeCachePath:path=>{codeCachePath=String(path);},"
    "webRequest,"
    "protocol:sessionProtocol"
    "});"
    "const session=Object.freeze({defaultSession});"
    "const ipcMain=new EventEmitter();"
    "const ipcMainHandlers=new Map();"
    "ipcMain.handle=(channel,listener)=>{"
    "const key=String(channel);"
    "if(typeof listener!=='function')"
    "throw new TypeError('ipcMain handler must be a function');"
    "if(ipcMainHandlers.has(key))"
    "throw new Error('Attempted to register a second handler for '+key);"
    "ipcMainHandlers.set(key,listener);"
    "};"
    "ipcMain.handleOnce=(channel,listener)=>{"
    "const key=String(channel);"
    "if(typeof listener!=='function')"
    "throw new TypeError('ipcMain handler must be a function');"
    "ipcMain.handle(key,async(...args)=>{"
    "ipcMainHandlers.delete(key);"
    "return listener(...args);"
    "});"
    "};"
    "ipcMain.removeHandler=channel=>{"
    "ipcMainHandlers.delete(String(channel));"
    "};"
    "Object.defineProperty(ipcMain,'_arosDispatchInvoke',{"
    "value:(channel,event,...args)=>{"
    "const key=String(channel);"
    "const handler=ipcMainHandlers.get(key);"
    "if(!handler)"
    "return Promise.reject(new Error('No handler registered for '+key));"
    "return Promise.resolve().then(()=>handler(event,...args));"
    "},enumerable:false"
    "});"
    "Object.defineProperty(ipcMain,'_arosDispatchMessage',{"
    "value:(channel,event,...args)=>"
    "ipcMain.emit(String(channel),event,...args),enumerable:false"
    "});"
    "const nativeTheme=new EventEmitter();"
    "const systemDarkTheme=/^(1|true|dark)$/i.test("
    "String(process.env.AROS_DARK_MODE||process.env.AROS_THEME||''));"
    "let nativeThemeSource='system';"
    "Object.defineProperties(nativeTheme,{"
    "themeSource:{"
    "get:()=>nativeThemeSource,"
    "set:value=>{"
    "const source=String(value);"
    "if(source!=='system'&&source!=='light'&&source!=='dark')"
    "throw new TypeError('themeSource must be system, light, or dark');"
    "if(source===nativeThemeSource)return;"
    "nativeThemeSource=source;"
    "nativeTheme.emit('updated');"
    "},enumerable:true"
    "},"
    "shouldUseDarkColors:{"
    "get:()=>nativeThemeSource==='dark'||"
    "(nativeThemeSource==='system'&&systemDarkTheme),enumerable:true"
    "},"
    "shouldUseHighContrastColors:{get:()=>false,enumerable:true},"
    "shouldUseInvertedColorScheme:{get:()=>false,enumerable:true}"
    "});"
    "const powerMonitor=new EventEmitter();"
    "powerMonitor.getSystemIdleState=threshold=>"
    "(Number(threshold)>=0?'active':'unknown');"
    "powerMonitor.getSystemIdleTime=()=>0;"
    "powerMonitor.isOnBatteryPower=()=>false;"
    "const crashReporterParameters=new Map();"
    "let crashReporterStarted=false;"
    "const crashReporter=Object.freeze({"
    "start:options=>{"
    "if(options!==undefined&&(options===null||typeof options!=='object'))"
    "throw new TypeError('crashReporter options must be an object');"
    "crashReporterStarted=true;"
    "const extra=options&&options.extra;"
    "if(extra&&typeof extra==='object')"
    "for(const [key,value] of Object.entries(extra))"
    "crashReporterParameters.set(String(key),String(value));"
    "binding.invoke('crash-reporter.start',JSON.stringify({"
    "productName:String(options&&options.productName||appName),"
    "companyName:String(options&&options.companyName||''),"
    "uploadToServer:false}));"
    "},"
    "isCrashReporterEnabled:()=>crashReporterStarted,"
    "getLastCrashReport:()=>null,"
    "getUploadedReports:()=>[],"
    "getUploadToServer:()=>false,"
    "setUploadToServer:value=>{"
    "if(value)binding.invoke('crash-reporter.upload-unsupported','');"
    "},"
    "addExtraParameter:(key,value)=>"
    "crashReporterParameters.set(String(key),String(value)),"
    "removeExtraParameter:key=>crashReporterParameters.delete(String(key)),"
    "getParameters:()=>Object.fromEntries(crashReporterParameters)"
    "});"
    "const screen=new EventEmitter();"
    "const readScreen=()=>{"
    "const values=String(binding.invoke('screen.snapshot','')).split(',')"
    ".map(value=>Number(value));"
    "const width=Number.isFinite(values[0])?values[0]:640;"
    "const height=Number.isFinite(values[1])?values[1]:480;"
    "const top=Math.max(0,Number.isFinite(values[2])?values[2]:0);"
    "const cursorX=Number.isFinite(values[3])?values[3]:0;"
    "const cursorY=Number.isFinite(values[4])?values[4]:0;"
    "const bounds={x:0,y:0,width,height};"
    "const workArea={x:0,y:top,width,height:Math.max(1,height-top)};"
    "return {display:Object.freeze({id:1,bounds,workArea,"
    "size:{width,height},workAreaSize:{width,height:workArea.height},"
    "scaleFactor:1,rotation:0,internal:false,touchSupport:'unknown',"
    "monochrome:false,accelerometerSupport:'unknown',colorSpace:'srgb',"
    "colorDepth:24,depthPerComponent:8,displayFrequency:60}),"
    "cursor:{x:cursorX,y:cursorY}};"
    "};"
    "screen.getAllDisplays=()=>[readScreen().display];"
    "screen.getPrimaryDisplay=()=>readScreen().display;"
    "screen.getDisplayNearestPoint=()=>readScreen().display;"
    "screen.getDisplayMatching=()=>readScreen().display;"
    "screen.getCursorScreenPoint=()=>readScreen().cursor;"
    "let nextWindowId=1;"
    "const browserWindows=[];"
    "const closedWindows=new Map();"
    /* IPC payloads cross the seam as JSON; binary chunks (VSBuffer wraps
       Uint8Array/Buffer) travel as {__arosBin:base64}. JSON.stringify runs
       Buffer.prototype.toJSON before the replacer sees the value, so the
       replacer looks at the holder's raw slot instead of `value`: otherwise
       a Buffer reaches the renderer as {type:'Buffer',data:[...]} and
       VSBuffer.wrap() ends up without subarray(). */
    "const ipcReplacer=function(key,value){"
    "const raw=this[key];"
    "if(raw instanceof Uint8Array)"
    "return {__arosBin:Buffer.from(raw.buffer,raw.byteOffset,"
    "raw.byteLength).toString('base64')};"
    "if(raw instanceof ArrayBuffer)"
    "return {__arosBin:Buffer.from(raw).toString('base64')};"
    "return value;"
    "};"
    "const ipcReviver=(key,value)=>"
    "(value&&typeof value==='object'&&typeof value.__arosBin==='string')?"
    "Buffer.from(value.__arosBin,'base64'):value;"
    /* Window id first: with several windows (or several apps) in one engine
       the seam picks the browser by it. */
    "const deliverToRenderer=(contents,message)=>binding.invoke("
    "'web-contents.send',String(contents._window.id)+'\\t'+"
    "JSON.stringify(JSON.stringify(message,ipcReplacer)));"
    "let nextWebContentsId=1;"
    "const webContentsById=new Map();"
    "class WebContents extends EventEmitter{"
    "constructor(window){"
    "super();this.id=nextWebContentsId++;this._window=window;this._url='';"
    "this.session=defaultSession;webContentsById.set(this.id,this);"
    "}"
    "isDestroyed(){return this._window._destroyed;}"
    "loadURL(url){return this._window.loadURL(url);}"
    "getURL(){return this._url;}"
    "getTitle(){return this._window._title;}"
    "send(channel,...args){"
    "deliverToRenderer(this,{type:'message',channel:String(channel),args});"
    "}"
    "postMessage(channel,message,transfer){"
    "const ports=Array.isArray(transfer)?"
    "transfer.map(port=>exportPortToRenderer(this,port)):[];"
    "deliverToRenderer(this,{type:'message',channel:String(channel),"
    "args:[message],ports});"
    "}"
    "executeJavaScript(code){"
    "binding.invoke('web-contents.execute-script',"
    "String(this._window.id)+'\\t'+String(code));"
    "return Promise.resolve();"
    "}"
    "getOSProcessId(){return process.pid;}"
    "getProcessId(){return process.pid;}"
    "focus(){}"
    "openDevTools(){}closeDevTools(){}toggleDevTools(){}"
    "isDevToolsOpened(){return false;}isDevToolsFocused(){return false;}"
    "setWindowOpenHandler(){}"
    "setBackgroundThrottling(){}"
    "setVisualZoomLevelLimits(){}"
    "setIgnoreMenuShortcuts(value){this._ignoreMenuShortcuts=!!value;}"
    "isCrashed(){return false;}"
    "isLoading(){return false;}"
    "isWaitingForResponse(){return false;}"
    "getUserAgent(){return this._userAgent||'';}"
    "setUserAgent(value){this._userAgent=String(value);}"
    "setAudioMuted(value){this._audioMuted=!!value;}"
    "isAudioMuted(){return !!this._audioMuted;}"
    "insertCSS(){return Promise.resolve('');}"
    "reload(){this.loadURL(this._url);}"
    "reloadIgnoringCache(){this.loadURL(this._url);}"
    "stop(){}"
    "inspectElement(){}inspectSharedWorker(){}inspectServiceWorker(){}"
    "copy(){}cut(){}paste(){}undo(){}redo(){}selectAll(){}delete(){}"
    "setZoomLevel(level){"
    "binding.invoke('browser-window.set-zoom-level',"
    "String(this._window.id)+'\\t'+String(Number(level)||0));"
    "}"
    "getZoomLevel(){return 0;}"
    "static fromId(id){return webContentsById.get(Number(id))||null;}"
    "static getAllWebContents(){return [...webContentsById.values()];}"
    "static getFocusedWebContents(){"
    "const window=BrowserWindow.getFocusedWindow();"
    "return window?window.webContents:null;"
    "}"
    "}"
    "class BrowserWindow extends EventEmitter{"
    "constructor(options={}){"
    "super();this.id=nextWindowId++;this.options=options;"
    "this._fullScreen=!!options.fullscreen;"
    "this._simpleFullScreen=!!options.simpleFullscreen;"
    "this._menuBarVisible=options.autoHideMenuBar!==true;"
    "this._title=String(options.title||'');"
    "this._destroyed=false;"
    "this._visible=options.show!==false;"
    "this._minimized=false;"
    "this._maximized=false;"
    "this._enabled=true;"
    "this._focused=false;"
    "this._alwaysOnTop=!!options.alwaysOnTop;"
    "this._representedFilename='';"
    "this._documentEdited=false;"
    "this._backgroundColor=String(options.backgroundColor||'#FFFFFF');"
    /* Geometry is tracked here and mirrored to the shell as requests; the
       shell decides what its Intuition window can honour. */
    "const size=(value,fallback)=>{"
    "const number=Math.trunc(Number(value));"
    "return Number.isFinite(number)&&number>0?number:fallback;"
    "};"
    "const coordinate=value=>{"
    "const number=Math.trunc(Number(value));"
    "return Number.isFinite(number)?number:0;"
    "};"
    "this._bounds={x:coordinate(options.x),y:coordinate(options.y),"
    "width:size(options.width,800),height:size(options.height,600)};"
    /* Without x/y Electron lets the window system place the window; the
       shell is told so with empty position fields. */
    "this._positioned=Number.isFinite(Number(options.x))&&"
    "Number.isFinite(Number(options.y));"
    "this._minimumSize=[size(options.minWidth,0),size(options.minHeight,0)];"
    "this.webContents=new WebContents(this);"
    "browserWindows.push(this);"
    "const webPreferences=options.webPreferences||{};"
    "const preload=String(webPreferences.preload||'');"
    "const additionalArguments=Array.isArray("
    "webPreferences.additionalArguments)"
    "?webPreferences.additionalArguments.map(String):[];"
    /* The renderer's process shim is built from this snapshot; it must be
       stored before browser-window.create reads the preload. */
    "binding.invoke('renderer.process-snapshot',String(this.id)+'\\t'+"
    "JSON.stringify({"
    "platform:process.platform,arch:process.arch,env:{...process.env},"
    "versions:{...process.versions},execPath:process.execPath,"
    "pid:process.pid,cwd:process.cwd(),"
    "argv:[process.execPath,...additionalArguments]}));"
    /* nodeIntegration rides along with the window: GitHub Desktop's renderer
       requires fs, path, child_process and electron at page scope, which only
       works if a Node runtime is installed in the renderer's own context. */
    "binding.invoke('browser-window.node-integration',"
    "String(this.id)+'\\t'+(webPreferences.nodeIntegration?'1':'0'));"
    "binding.invoke('browser-window.create',"
    "[String(this.id),preload,...additionalArguments].join('\\t'));"
    /* The constructor options are the window's initial geometry. */
    "this._syncBounds();"
    "if(this._minimumSize[0]>0||this._minimumSize[1]>0)"
    "binding.invoke('browser-window.set-minimum-size',"
    "[this.id,...this._minimumSize].join('\\t'));"
    "app.emit('browser-window-created',{},this);"
    "app.emit('web-contents-created',{},this.webContents);"
    "}"
    "loadURL(url){"
    "this.webContents._url=String(url);"
    "binding.invoke('browser-window.load-url',"
    "String(this.id)+'\\t'+String(url));"
    "return Promise.resolve();"
    "}"
    "isFullScreen(){return this._fullScreen;}"
    "setFullScreen(value){"
    "this._fullScreen=!!value;"
    "binding.invoke('browser-window.set-full-screen',"
    "String(this.id)+'\\t'+String(this._fullScreen));"
    "}"
    "isSimpleFullScreen(){return this._simpleFullScreen;}"
    "setSimpleFullScreen(value){"
    "this._simpleFullScreen=!!value;"
    "binding.invoke('browser-window.set-simple-full-screen',"
    "String(this.id)+'\\t'+String(this._simpleFullScreen));"
    "}"
    "setMenuBarVisibility(value){"
    "this._menuBarVisible=!!value;"
    "binding.invoke('browser-window.set-menu-bar-visibility',"
    "String(this.id)+'\\t'+String(this._menuBarVisible));"
    "}"
    "isMenuBarVisible(){return this._menuBarVisible;}"
    "setTitle(value){"
    "this._title=String(value);"
    "binding.invoke('browser-window.set-title',"
    "String(this.id)+'\\t'+this._title);"
    "}"
    "getTitle(){return this._title;}"
    "isDestroyed(){return this._destroyed;}"
    "isVisible(){return !this._destroyed&&this._visible;}"
    "show(){"
    "this._visible=true;this._minimized=false;"
    "binding.invoke('browser-window.show',String(this.id));"
    "}"
    "hide(){"
    "this._visible=false;"
    "binding.invoke('browser-window.hide',String(this.id));"
    "}"
    "isMinimized(){return this._minimized;}"
    "minimize(){"
    "this._minimized=true;this._visible=false;"
    "binding.invoke('browser-window.minimize',String(this.id));"
    "}"
    "restore(){"
    "this._minimized=false;this._visible=true;"
    "binding.invoke('browser-window.restore',String(this.id));"
    "}"
    "focus(){"
    "this._focused=true;"
    "binding.invoke('browser-window.focus',String(this.id));"
    "}"
    "blur(){this._focused=false;}"
    "isFocused(){return this._focused&&!this._destroyed;}"
    "destroy(){if(!this._destroyed)this.close();}"
    "setEnabled(value){this._enabled=!!value;}"
    "isEnabled(){return this._enabled;}"
    "setAlwaysOnTop(value){this._alwaysOnTop=!!value;}"
    "isAlwaysOnTop(){return this._alwaysOnTop;}"
    "setAutoHideMenuBar(value){this._menuBarVisible=!value;}"
    "isMenuBarAutoHide(){return !this._menuBarVisible;}"
    "get autoHideMenuBar(){return !this._menuBarVisible;}"
    "set autoHideMenuBar(value){this._menuBarVisible=!value;}"
    "setMenu(){}removeMenu(){}"
    "getBounds(){return {...this._bounds};}"
    "getNormalBounds(){return {...this._bounds};}"
    "getContentBounds(){return {...this._bounds};}"
    "_syncBounds(){"
    "binding.invoke('browser-window.set-bounds',"
    "[this.id,this._positioned?this._bounds.x:'',"
    "this._positioned?this._bounds.y:'',this._bounds.width,"
    "this._bounds.height].join('\\t'));"
    "}"
    "setBounds(bounds){"
    "if(bounds&&typeof bounds==='object'){"
    "for(const key of ['x','y','width','height'])"
    "if(Number.isFinite(Number(bounds[key])))"
    "this._bounds[key]=Math.trunc(Number(bounds[key]));"
    "if(Number.isFinite(Number(bounds.x))&&Number.isFinite(Number(bounds.y)))"
    "this._positioned=true;"
    "}"
    "this._syncBounds();"
    "}"
    "setContentBounds(bounds){this.setBounds(bounds);}"
    "getSize(){return [this._bounds.width,this._bounds.height];}"
    "getContentSize(){return this.getSize();}"
    "setSize(width,height){this.setBounds({width,height});}"
    "setContentSize(width,height){this.setSize(width,height);}"
    "getPosition(){return [this._bounds.x,this._bounds.y];}"
    "setPosition(x,y){this.setBounds({x,y});}"
    "center(){}"
    "getMinimumSize(){return this._minimumSize.slice();}"
    "setMinimumSize(width,height){"
    "this._minimumSize=[Math.max(0,Number(width)||0),Math.max(0,Number(height)||0)];"
    "binding.invoke('browser-window.set-minimum-size',"
    "[this.id,...this._minimumSize].join('\\t'));"
    "}"
    "getMaximumSize(){return [0,0];}"
    "setMaximumSize(){}"
    "setResizable(){}isResizable(){return true;}"
    "setMovable(){}isMovable(){return true;}"
    "setMinimizable(){}isMinimizable(){return true;}"
    "setMaximizable(){}isMaximizable(){return true;}"
    "setClosable(){}isClosable(){return true;}"
    "setFullScreenable(){}isFullScreenable(){return true;}"
    "getBackgroundColor(){return this._backgroundColor;}"
    "setBackgroundColor(value){"
    "this._backgroundColor=String(value);"
    "binding.invoke('browser-window.set-background-color',"
    "String(this.id)+'\\t'+this._backgroundColor);"
    "}"
    "setRepresentedFilename(value){this._representedFilename=String(value);}"
    "getRepresentedFilename(){return this._representedFilename;}"
    "setDocumentEdited(value){this._documentEdited=!!value;}"
    "isDocumentEdited(){return this._documentEdited;}"
    "setWindowButtonPosition(){}getWindowButtonPosition(){return null;}"
    "setWindowButtonVisibility(){}"
    "setTitleBarOverlay(){}"
    "setSheetOffset(){}"
    "setTouchBar(){}"
    "setProgressBar(){}"
    "setOverlayIcon(){}"
    "setIcon(){}"
    "setOpacity(){}getOpacity(){return 1;}"
    "setSkipTaskbar(){}"
    "setKiosk(){}isKiosk(){return false;}"
    "flashFrame(){}"
    "moveTop(){}"
    "addTabbedWindow(){}"
    "hookWindowMessage(){}unhookWindowMessage(){}unhookAllWindowMessages(){}"
    "isMaximized(){return this._maximized;}"
    "maximize(){"
    "this._maximized=true;"
    "binding.invoke('browser-window.maximize',String(this.id));"
    "}"
    "unmaximize(){"
    "this._maximized=false;"
    "binding.invoke('browser-window.unmaximize',String(this.id));"
    "}"
    "close(){"
    "if(this._destroyed)return;"
    "const event={defaultPrevented:false,preventDefault(){this.defaultPrevented=true;}};"
    "this.emit('close',event);"
    "if(event.defaultPrevented)return;"
    "this._retire();"
    "binding.invoke('browser-window.close',String(this.id));"
    "}"
    /* The window leaves the live list at once (getAllWindows() must not
       return it) but stays reachable by id until the shell reports the
       Intuition window gone, which is when 'closed' fires. */
    "_retire(){"
    "const index=browserWindows.indexOf(this);"
    "if(index>=0)browserWindows.splice(index,1);"
    "this._destroyed=true;"
    "this._visible=false;"
    "closedWindows.set(this.id,this);"
    "}"
    /* Window-system events posted by the shell (ElectronPostWindowEvent):
       the shell already acted, these only bring the object model and its
       listeners up to date. */
    "_handleNativeEvent(name,payload){"
    "if(name==='closed'){"
    "if(!this._destroyed)this._retire();"
    "closedWindows.delete(this.id);"
    "this.webContents.emit('destroyed');"
    "this.emit('closed');"
    "if(browserWindows.length===0){"
    "if(app.listenerCount('window-all-closed')===0)app.quit();"
    "else app.emit('window-all-closed');"
    "}"
    "return;"
    "}"
    "if(name==='focus'){this._focused=true;this.emit('focus');return;}"
    "if(name==='blur'){this._focused=false;this.emit('blur');return;}"
    "if(name==='resize'||name==='move'){"
    "const fields=payload.split('\\t').map(Number);"
    "if(fields.length>=4&&fields.every(Number.isFinite)){"
    "this._bounds={x:fields[0],y:fields[1],width:fields[2],height:fields[3]};"
    "this._positioned=true;"
    "}"
    "this.emit(name);"
    "return;"
    "}"
    "if(name==='maximize'){this._maximized=true;this.emit('maximize');return;}"
    "if(name==='unmaximize'){this._maximized=false;this.emit('unmaximize');return;}"
    /* Page load milestones from CEF's load handler. These belong to
       webContents, not to the window: an app that opens its window with
       show:false and shows it on did-finish-load (GitHub Desktop does
       exactly that) otherwise keeps a working window invisible forever. */
    "if(name==='did-finish-load'||name==='did-stop-loading'||"
    "name==='dom-ready'){"
    "this.webContents.emit(name);"
    "if(name==='did-finish-load')this.emit('ready-to-show');"
    "return;"
    "}"
    "console.error('[AROS-ELECTRON] unknown window event '+name);"
    "}"
    "static getAllWindows(){return browserWindows.slice();}"
    "static getFocusedWindow(){"
    "return browserWindows.length?browserWindows[browserWindows.length-1]:null;"
    "}"
    "static fromId(id){"
    "return browserWindows.find(entry=>entry.id===Number(id))||null;"
    "}"
    "static fromWebContents(contents){"
    "const window=contents&&contents._window;"
    "return window instanceof BrowserWindow&&!window._destroyed?window:null;"
    "}"
    "}"
    /* One-line summary of a message for the utility/port traces: binary
       payloads by size (VS Code's RPC frames are VSBuffers), the rest as
       truncated JSON. */
    "const briefValue=value=>{"
    "if(value instanceof Uint8Array||value instanceof ArrayBuffer)"
    "return 'bin['+value.byteLength+']';"
    "try{return String(JSON.stringify(value)).slice(0,160);}"
    "catch(error){return String(value).slice(0,160);}"
    "};"
    /* MessagePortMain over a node MessagePort.  Every node port carries the
       envelope {data,ports}, so a transferred port arrives as event.ports on
       whichever side receives it - main process or utility worker - exactly
       like Electron's MessageEvent.  The factory is embedded by source into
       the utility preamble (Function.prototype.toString), so it must only
       close over its parameter. */
    "const makeMessagePortMain=EventEmitter=>{"
    "class MessagePortMain extends EventEmitter{"
    "constructor(port){"
    "super();"
    "this._port=port;this._started=false;this._closed=false;this._queue=[];"
    "port.on('message',envelope=>{"
    "const event={data:envelope&&envelope.data,ports:MessagePortMain.wrapAll(envelope&&envelope.ports)};"
    "if(this._started)this.emit('message',event);else this._queue.push(event);"
    "});"
    "port.on('close',()=>{"
    "if(this._closed)return;"
    "this._closed=true;this.emit('close');"
    "});"
    "port.on('messageerror',error=>{"
    "console.error('[AROS-ELECTRON] MessagePortMain messageerror: '+(error&&error.stack||error));"
    "});"
    "}"
    "static wrapAll(ports){"
    "return Array.isArray(ports)?ports.map(port=>new MessagePortMain(port)):[];"
    "}"
    "static unwrapAll(ports){"
    "return Array.isArray(ports)?ports.map(port=>port instanceof MessagePortMain?port._port:port):[];"
    "}"
    "postMessage(message,transfer){"
    "if(this._closed)return;"
    "const ports=MessagePortMain.unwrapAll(transfer);"
    "this._port.postMessage({data:message,ports},ports);"
    "}"
    "start(){"
    "if(this._started)return;"
    "this._started=true;"
    "const queued=this._queue.splice(0);"
    "for(const event of queued)this.emit('message',event);"
    "}"
    "close(){"
    "if(this._closed)return;"
    "this._port.close();"
    "}"
    "}"
    "return MessagePortMain;"
    "};"
    "const MessagePortMain=makeMessagePortMain(EventEmitter);"
    "class MessageChannelMain{"
    "constructor(){"
    "const channel=new workerThreads.MessageChannel();"
    "this.port1=new MessagePortMain(channel.port1);"
    "this.port2=new MessagePortMain(channel.port2);"
    "}"
    "}"
    /* utilityProcess.fork: a child node instance is a worker_threads Worker in
       this process.  The preamble gives it Electron's utility-process shape -
       process.parentPort with {data,ports} events, process.argv as
       `<exe> <module> ...args`, process.type 'utility', original-fs - and then
       requires the module.  The child's stdout/stderr are exposed as streams
       (Electron stdio 'pipe') and echoed to the debug log while the seam is
       young.  VSCODE_PARENT_PID liveness polls process.kill(<our pid>,0): the
       parent is this very process, so that answers true.  AROS getpid() is
       per task (ETask et_UniqueID), so a Worker's process.pid differs from
       the main pid VS Code was given - compare against workerData.parentPid
       too, or bootstrap-fork's terminateWhenParentTerminates exits the
       service on its first 5 s poll. */
    "const utilityPreamble="
    "\"const {parentPort,workerData}=require('node:worker_threads');\"+"
    "\"const {EventEmitter}=require('node:events');\"+"
    "\"const MessagePortMain=(\"+makeMessagePortMain.toString()+\")(EventEmitter);\"+"
    "\"const Module=require('node:module');\"+"
    "\"const moduleLoad=Module._load;\"+"
    "\"Module._load=function(request,parent,isMain){\"+"
    "\"if(request==='original-fs')return moduleLoad.call(this,'fs',parent,isMain);\"+"
    "\"if(request==='electron')throw new Error('electron is not available in an AROS utility process');\"+"
    "\"return moduleLoad.apply(this,arguments);\"+"
    "\"};\"+"
    /* An eval Worker runs like `node -e`: node publishes module/exports/
       __filename/__dirname as GLOBALS.  UMD bundles (VS Code's semver.js)
       then see `typeof module==='object'` at global scope, take the
       CommonJS branch and never call the AMD loader's define -> "Didn't
       receive define call".  A real utility process has no such globals. */
    "\"delete globalThis.module;delete globalThis.exports;\"+"
    "\"delete globalThis.__filename;delete globalThis.__dirname;\"+"
    "\"process.type='utility';\"+"
    "\"if(!process.versions.electron)process.versions.electron=workerData.electronVersion;\"+"
    "\"process.argv=[process.execPath,workerData.modulePath,...workerData.args];\"+"
    "\"const nativeKill=process.kill.bind(process);\"+"
    "\"process.kill=(pid,signal)=>{\"+"
    "\"if((Number(pid)===process.pid||Number(pid)===workerData.parentPid)&&(signal===0||signal==='0'))return true;\"+"
    "\"return nativeKill(pid,signal);\"+"
    "\"};\"+"
    /* Electron's ParentPort starts its port only when the first 'message'
       listener is added, so the payload and the MessagePort the main process
       posts right after fork wait until the module is loaded and listening
       (VS Code's shared process, extension host and file watcher all attach
       their listener after tens of seconds of module loading).  Queue until
       then; 'newListener' fires before the listener is on, hence the
       deferred flush.  A real utility process also lives until it exits or
       is killed, whatever its event loop holds - a Worker would leave once
       the loop drains, so a ref'd timer keeps it. */
    "\"const parent=new EventEmitter();\"+"
    "\"const pendingEnvelopes=[];let parentStarted=false;\"+"
    "\"const toParentEvent=envelope=>({data:envelope&&envelope.data,\"+"
    "\"ports:MessagePortMain.wrapAll(envelope&&envelope.ports)});\"+"
    "\"parentPort.on('message',envelope=>{\"+"
    "\"if(parentStarted)parent.emit('message',toParentEvent(envelope));\"+"
    "\"else pendingEnvelopes.push(envelope);\"+"
    "\"});\"+"
    "\"parent.on('newListener',name=>{\"+"
    "\"if(name!=='message'||parentStarted)return;\"+"
    "\"parentStarted=true;\"+"
    "\"console.error('[AROS-UTILITY-LISTEN] '+workerData.serviceName+' queued '+pendingEnvelopes.length+' after '+Math.round(performance.now())+' ms');\"+"
    "\"setImmediate(()=>{for(const envelope of pendingEnvelopes.splice(0))\"+"
    "\"parent.emit('message',toParentEvent(envelope));});\"+"
    "\"});\"+"
    "\"parent.postMessage=(message,transfer)=>{\"+"
    "\"const ports=MessagePortMain.unwrapAll(transfer);\"+"
    "\"parentPort.postMessage({data:message,ports},ports);\"+"
    "\"};\"+"
    "\"parent.start=()=>{};parent.close=()=>{};\"+"
    "\"process.parentPort=parent;\"+"
    "\"setInterval(()=>{},2147483647);\"+"
    /* Diagnostics while the seam is young: VS Code's AMD loader reads every
       module with fs.readFile, so a load that never completes shows as a
       pending count that stops moving; an explicit process.exit shows its
       caller. */
    "\"const fsModule=require('node:fs');\"+"
    "\"const fsStats={issued:0,done:0,pending:new Map()};\"+"
    "\"const nativeReadFile=fsModule.readFile;\"+"
    "\"fsModule.readFile=function(path,...rest){\"+"
    "\"const callback=rest[rest.length-1];\"+"
    "\"if(typeof callback==='function'){\"+"
    "\"const id=++fsStats.issued;fsStats.pending.set(id,String(path));\"+"
    "\"rest[rest.length-1]=function(error){\"+"
    "\"fsStats.done++;fsStats.pending.delete(id);\"+"
    "\"if(error)console.error('[AROS-UTILITY-FS] '+workerData.serviceName+' readFile '+path+' -> '+(error.code||error));\"+"
    "\"return callback.apply(this,arguments);\"+"
    "\"};\"+"
    "\"}\"+"
    "\"return nativeReadFile.call(this,path,...rest);\"+"
    "\"};\"+"
    "\"setInterval(()=>{console.error('[AROS-UTILITY-FS] '+workerData.serviceName+' readFile issued '+fsStats.issued+' done '+fsStats.done+' pending '+[...fsStats.pending.values()].slice(0,3).join(' '));},5000).unref();\"+"
    "\"const nativeExit=process.exit.bind(process);\"+"
    "\"process.exit=code=>{\"+"
    "\"console.error('[AROS-UTILITY-EXIT-CALL] '+workerData.serviceName+' code '+code+' '+String(new Error().stack).split(String.fromCharCode(10)).slice(1,6).join(' | '));\"+"
    "\"return nativeExit(code);\"+"
    "\"};\"+"
    "\"process.on('exit',code=>console.error('[AROS-UTILITY-EXIT] '+workerData.serviceName+' code '+code));\"+"
    "\"require(workerData.modulePath);\";"
    "let nextUtilityId=1;"
    "class UtilityProcess extends EventEmitter{"
    "constructor(modulePath,args,options){"
    "super();"
    "options=options&&typeof options==='object'?options:{};"
    "const serviceName=String(options.serviceName||('utility-'+nextUtilityId++));"
    "this.pid=undefined;this.stdout=null;this.stderr=null;"
    "this._killed=false;this._exited=false;this._worker=null;this._serviceName=serviceName;"
    "const env={};"
    "const source=options.env&&typeof options.env==='object'?options.env:process.env;"
    "for(const key of Object.keys(source))env[key]=String(source[key]);"
    "const argv=Array.isArray(args)?args.map(String):[];"
    "const execArgv=Array.isArray(options.execArgv)?options.execArgv.map(String):[];"
    "const workerOptions={eval:true,argv,env,execArgv,stdout:true,stderr:true,"
    "workerData:{modulePath:String(modulePath),args:argv,serviceName,parentPid:process.pid,"
    "electronVersion:process.versions.electron}};"
    "let worker=null;"
    "try{worker=new workerThreads.Worker(utilityPreamble,workerOptions);}"
    "catch(error){"
    "if(execArgv.length){"
    "console.error('[AROS-ELECTRON] utilityProcess.fork('+serviceName+') retrying without execArgv '+JSON.stringify(execArgv)+': '+describeError(error));"
    "try{worker=new workerThreads.Worker(utilityPreamble,{...workerOptions,execArgv:[]});}"
    "catch(again){error=again;}"
    "}"
    "if(!worker){"
    "console.error('[AROS-ELECTRON] utilityProcess.fork('+serviceName+') failed: '+describeError(error));"
    "setImmediate(()=>this._exit(1));"
    "return;"
    "}"
    "}"
    "this._worker=worker;this.pid=worker.threadId;"
    "this.stdout=worker.stdout;this.stderr=worker.stderr;"
    "const echo=stream=>chunk=>binding.invoke('console-error','[AROS-UTILITY '+serviceName+' '+stream+'] '+String(chunk).trimEnd().slice(0,2000));"
    "worker.stdout.on('data',echo('stdout'));"
    "worker.stderr.on('data',echo('stderr'));"
    "worker.on('message',envelope=>{"
    "binding.invoke('utility-process.message',serviceName+'\\t'+briefValue(envelope&&envelope.data));"
    "this.emit('message',envelope&&envelope.data);"
    "});"
    "worker.on('error',error=>{"
    "console.error('[AROS-ELECTRON] utility process '+serviceName+' crashed: '+describeError(error));"
    "});"
    "worker.on('exit',code=>this._exit(this._killed&&code!==0?15:code));"
    "binding.invoke('utility-process.fork',serviceName+'\\tworker '+worker.threadId+'\\t'+String(modulePath)+'\\t'+argv.join(' '));"
    "setImmediate(()=>{if(!this._exited)this.emit('spawn');});"
    "}"
    "_exit(code){"
    "if(this._exited)return;"
    "this._exited=true;this._worker=null;"
    "binding.invoke('utility-process.exit',this._serviceName+'\\t'+String(code));"
    "this.emit('exit',code);"
    "}"
    "postMessage(message,transfer){"
    "if(!this._worker||this._exited)return;"
    "const ports=MessagePortMain.unwrapAll(transfer);"
    "binding.invoke('utility-process.post',this._serviceName+'\\tports '+ports.length+'\\t'+briefValue(message));"
    "this._worker.postMessage({data:message,ports},ports);"
    "}"
    "kill(){"
    "if(!this._worker||this._exited)return false;"
    "this._killed=true;"
    "this._worker.terminate();"
    "return true;"
    "}"
    "}"
    "const utilityProcess=Object.freeze({"
    "fork:(modulePath,args,options)=>new UtilityProcess(modulePath,args,options)"
    "});"
    /* A MessagePortMain handed to a renderer (webContents.postMessage transfer)
       is proxied over the seam: main keeps the node port and forwards its
       messages as port-message deliveries; the renderer runtime pairs each id
       with a DOM MessageChannel and gives VS Code the far end.  Only the
       main->renderer direction exists in VS Code (ipcMessagePort.acquire). */
    "let nextRendererPortId=1;"
    "const rendererPorts=new Map();"
    /* The first few messages each way are traced so a silent handshake
       (extension host 'ready', shared process 'ipcReady') can be located. */
    "const tracePort=(id,direction,count,data)=>{"
    "if(count<=4||count%500===0)binding.invoke('utility-process.port',"
    "'port '+id+' '+direction+' #'+count+' '+briefValue(data));"
    "};"
    "const exportPortToRenderer=(contents,port)=>{"
    "const id=nextRendererPortId++;"
    "let toRenderer=0;"
    "rendererPorts.set(id,{port,fromRenderer:0});"
    "binding.invoke('utility-process.port','port '+id+' exported to renderer');"
    "port.on('message',event=>{"
    "tracePort(id,'->renderer',++toRenderer,event.data);"
    "deliverToRenderer(contents,{type:'port-message',port:id,"
    "data:event.data,ports:event.ports.map(nested=>exportPortToRenderer(contents,nested))});"
    "});"
    "port.on('close',()=>{"
    "rendererPorts.delete(id);"
    "deliverToRenderer(contents,{type:'port-close',port:id});"
    "});"
    "port.start();"
    "return id;"
    "};"
    "const normalizeDialogOptions=(windowOrOptions,maybeOptions)=>"
    "(windowOrOptions instanceof BrowserWindow?maybeOptions:windowOrOptions)||{};"
    "const serializeDialogOptions=options=>{"
    "const encode=value=>encodeURIComponent(value===undefined?'':String(value));"
    "const buttons=Array.isArray(options.buttons)&&options.buttons.length?"
    "options.buttons:['OK'];"
    "return [encode(options.title||appName),encode(options.message||''),"
    "encode(options.detail||''),buttons.map(encode).join('\\x1f')].join('\\t');"
    "};"
    "const dialog=Object.freeze({"
    "showMessageBoxSync:(windowOrOptions,maybeOptions)=>{"
    "const options=normalizeDialogOptions(windowOrOptions,maybeOptions);"
    "const response=Number(binding.invoke('dialog.show-message-box-sync',"
    "serializeDialogOptions(options)));"
    "return Number.isInteger(response)&&response>=0?response:"
    "(Number.isInteger(options.cancelId)?options.cancelId:0);"
    "},"
    "showMessageBox:(windowOrOptions,maybeOptions)=>"
    "Promise.resolve({response:dialog.showMessageBoxSync("
    "windowOrOptions,maybeOptions),checkboxChecked:false})"
    "});"
    /* Native events: `protocol\tid\tscheme\turl` from the scheme handler
       factory (answered with protocol.response) and `ipc\twindow\tjson`
       from the renderer extension (dispatched to ipcMain). */
    "const respondToProtocol=(id,response)=>{"
    "let reply;"
    "if(typeof response==='string')reply=[id,'ok',response,''];"
    "else if(response&&typeof response==='object'&&"
    "typeof response.path==='string')"
    "reply=[id,'ok',response.path,"
    "response.headers?JSON.stringify(response.headers):''];"
    "else if(response&&typeof response==='object'&&"
    "typeof response.error==='number')reply=[id,String(response.error),'',''];"
    "else reply=[id,'-2','',''];"
    "binding.invoke('protocol.response',reply.join('\\t'));"
    "};"
    "const handleProtocolRequest=(id,scheme,url)=>{"
    "const handler=sessionFileProtocols.get(scheme)||"
    "globalFileProtocols.get(scheme)||"
    "sessionInterceptedFileProtocols.get(scheme)||"
    "globalInterceptedFileProtocols.get(scheme);"
    "if(!handler){respondToProtocol(id,{error:-2});return;}"
    "const request={url,method:'GET',referrer:'',headers:{},uploadData:[]};"
    "let answered=false;"
    "const respond=response=>{"
    "if(answered)return;answered=true;respondToProtocol(id,response);"
    "};"
    "try{"
    "const result=handler(request,respond);"
    "if(result&&typeof result.then==='function')"
    "result.then(respond,()=>respond({error:-2}));"
    "}catch(error){"
    "console.error('[AROS-ELECTRON] protocol handler failed for '+url+': '+"
    "describeError(error));"
    "respond({error:-2});"
    "}"
    "};"
    "const replyToRenderer=(contents,id,payload)=>"
    "deliverToRenderer(contents,{type:'reply',id,...payload});"
    "const handleRendererMessage=(windowId,message)=>{"
    "const window=browserWindows.find(entry=>entry.id===windowId)||"
    "browserWindows[0];"
    "if(!window)return;"
    "const contents=window.webContents;"
    "const event={sender:contents,"
    "senderFrame:{url:contents._url,parent:null,routingId:1,"
    "processId:process.pid},"
    "ports:[],processId:process.pid,frameId:1,returnValue:undefined,"
    "reply:(channel,...args)=>contents.send(channel,...args)};"
    "const args=Array.isArray(message.args)?message.args:[];"
    "if(message.type==='invoke'){"
    "if(message.channel==='aros:process-memory-info'){"
    "const usage=process.memoryUsage();"
    "replyToRenderer(contents,message.id,{result:{"
    "workingSetSize:usage.rss/1024,peakWorkingSetSize:usage.rss/1024,"
    "private:usage.heapUsed/1024,shared:0}});"
    "return;"
    "}"
    "ipcMain._arosDispatchInvoke(message.channel,event,...args).then("
    "result=>replyToRenderer(contents,message.id,{result}),"
    "error=>replyToRenderer(contents,message.id,"
    "{error:String(error&&error.message||error)}));"
    "return;"
    "}"
    "if(message.type==='send'){"
    "ipcMain._arosDispatchMessage(message.channel,event,...args);"
    "return;"
    "}"
    "if(message.type==='port-message'){"
    "const entry=rendererPorts.get(Number(message.port));"
    "if(entry){"
    "tracePort(Number(message.port),'<-renderer',++entry.fromRenderer,message.data);"
    "entry.port.postMessage(message.data);"
    "}"
    "return;"
    "}"
    "if(message.type==='port-close'){"
    "const entry=rendererPorts.get(Number(message.port));"
    "if(entry)entry.port.close();"
    "return;"
    "}"
    "if(message.type==='web-frame.set-zoom-level'){"
    "contents.setZoomLevel(message.level);"
    "return;"
    "}"
    "console.error('[AROS-ELECTRON] unknown renderer message '+message.type);"
    "};"
    "const handleNativeEvent=text=>{"
    "const first=text.indexOf('\\t');"
    "const kind=first<0?text:text.slice(0,first);"
    "const rest=first<0?'':text.slice(first+1);"
    "if(kind==='protocol'){"
    "const idEnd=rest.indexOf('\\t');"
    "const schemeEnd=rest.indexOf('\\t',idEnd+1);"
    "handleProtocolRequest(rest.slice(0,idEnd),rest.slice(idEnd+1,schemeEnd),"
    "rest.slice(schemeEnd+1));"
    "return;"
    "}"
    "if(kind==='ipc'){"
    "const idEnd=rest.indexOf('\\t');"
    "handleRendererMessage(Number(rest.slice(0,idEnd)),"
    "JSON.parse(rest.slice(idEnd+1),ipcReviver));"
    "return;"
    "}"
    "if(kind==='window'){"
    "const idEnd=rest.indexOf('\\t');"
    "const nameEnd=rest.indexOf('\\t',idEnd+1);"
    "const id=Number(rest.slice(0,idEnd));"
    "const window=BrowserWindow.fromId(id)||closedWindows.get(id);"
    "const name=nameEnd<0?rest.slice(idEnd+1):rest.slice(idEnd+1,nameEnd);"
    "const payload=nameEnd<0?'':rest.slice(nameEnd+1);"
    "if(!window){"
    "console.error('[AROS-ELECTRON] window event '+name+' for unknown window '+id);"
    "return;"
    "}"
    "window._handleNativeEvent(name,payload);"
    "return;"
    "}"
    "console.error('[AROS-ELECTRON] unknown native event '+kind);"
    "};"
    "const pumpNativeEvents=()=>{"
    "for(let count=0;count<64;count++){"
    "const text=String(binding.invoke('native.poll','')||'');"
    "if(!text)break;"
    "try{handleNativeEvent(text);}"
    "catch(error){"
    "console.error('[AROS-ELECTRON] native event failed: '+"
    "describeError(error)+' text='+text.slice(0,200));"
    "}"
    "}"
    "};"
    "setInterval(pumpNativeEvents,4);"
    "const webContents=Object.freeze({"
    "fromId:WebContents.fromId,"
    "getAllWebContents:WebContents.getAllWebContents,"
    "getFocusedWebContents:WebContents.getFocusedWebContents"
    "});"
    /* shell: the desktop-integration module. GitHub Desktop calls shell.beep()
       while its main bundle is still loading, so its absence threw
       "Cannot read properties of undefined (reading 'beep')" before a window
       ever existed.
       openExternal goes through C:OpenURL, the same path the Workbench and
       Chromium's command port use, so a sign-in link really opens the browser.
       The rest report failure rather than pretending: AROS has no
       "reveal in file manager" hook wired here yet, and silently doing
       nothing when an app asks to show a file - or to move one to the trash -
       is worse than an error the caller can surface. */
    "const shellSpawn=(cmd,args)=>new Promise((resolve,reject)=>{"
    "let cp;try{cp=require('node:child_process');}catch(e){reject(e);return;}"
    "const child=cp.spawn(cmd,args,{stdio:'ignore'});"
    "child.on('error',reject);"
    "child.on('close',code=>code===0?resolve():"
    "reject(new Error(cmd+' exited with '+code)));"
    "});"
    "const shell=Object.freeze({"
    "beep(){binding.invoke('shell.beep','');},"
    "openExternal(url){return shellSpawn('C:OpenURL',[String(url)]);},"
    "openPath(path){return shellSpawn('C:OpenURL',[String(path)])"
    ".then(()=>'',err=>String(err&&err.message||err));},"
    "showItemInFolder(){return undefined;},"
    "trashItem(){return Promise.reject("
    "new Error('shell.trashItem is not implemented on AROS'));},"
    "moveItemToTrash(){return false;},"
    "readShortcutLink(){throw new Error("
    "'shell.readShortcutLink is Windows only');},"
    "writeShortcutLink(){throw new Error("
    "'shell.writeShortcutLink is Windows only');}"
    "});"
    /* autoUpdater: Electron's Squirrel client. GitHub Desktop wires its
       handlers the moment the app is ready, so its absence threw
       "Cannot read properties of undefined (reading 'on')" inside
       setupAutoUpdater before any window existed.
       AROS updates do not come from Squirrel - they come from the package
       manager and its repository - so this reports "no update
       available" rather than reaching for a feed that is not there. An app
       that asks gets a truthful answer and carries on. */
    "const autoUpdater=new EventEmitter();"
    "let updateFeedURL=null;"
    "autoUpdater.setFeedURL=options=>{"
    "updateFeedURL=typeof options==='string'?options:(options&&options.url)||null;"
    "};"
    "autoUpdater.getFeedURL=()=>updateFeedURL||'';"
    "autoUpdater.checkForUpdates=()=>{"
    "autoUpdater.emit('checking-for-update');"
    "setTimeout(()=>autoUpdater.emit('update-not-available'),0);"
    "};"
    "autoUpdater.quitAndInstall=()=>app.quit();"
    "const api=Object.freeze({"
    "app,native,BrowserWindow,WebContents,webContents,Menu,MenuItem,protocol,session,"
    "dialog,ipcMain,nativeTheme,powerMonitor,crashReporter,screen,shell,autoUpdater,"
    "MessageChannelMain,MessagePortMain,utilityProcess"
    "});"
    "const load=require;"
    "const Module=load('node:module');"
    "const moduleLoad=Module._load;"
    "const signalAppReady=()=>{"
    "if(appReady)return;"
    "appReady=true;"
    "app.emit('ready');"
    "resolveAppReady();"
    "};"
    "Module._load=function(request,parent,isMain){"
    "binding.invoke('module-load',String(request));"
    "if(request==='electron')return api;"
    "if(request==='original-fs')"
    "return moduleLoad.call(this,'fs',parent,isMain);"
    "const result=moduleLoad.apply(this,arguments);"
    "binding.invoke('module-return',String(request));"
    "if(globalThis.__arosElectronMainModuleRequest===String(request)){"
    "binding.invoke('main-module-ready',String(request));"
    "delete globalThis.__arosElectronMainModuleRequest;"
    "signalAppReady();"
    "}"
    "return result;"
    "};"
    "require=function(request){"
    "if(request==='electron')return api;"
    "if(request==='original-fs')return load('fs');"
    "return load(request);"
    "};"
    "setImmediate(signalAppReady);"
    "})();\n";

static LONG electron_queue_request(CONST_STRPTR method, CONST_STRPTR payload)
{
    ULONG next_tail;
    struct ElectronNativeRequest *request;
    const char *method_text =
        method != NULL ? (const char *)method : "";
    const char *payload_text =
        payload != NULL ? (const char *)payload : "";

    Forbid();
    next_tail = (electron_request_tail + 1) % ELECTRON_REQUEST_CAPACITY;
    if (next_tail == electron_request_head)
    {
        Permit();
        return 20;
    }
    request = &electron_requests[electron_request_tail];
    strncpy(request->method, method_text,
        sizeof(request->method) - 1);
    request->method[sizeof(request->method) - 1] = '\0';
    strncpy(request->payload, payload_text,
        sizeof(request->payload) - 1);
    request->payload[sizeof(request->payload) - 1] = '\0';
    request->slot = electron_current_slot;
    electron_request_tail = next_tail;
    Permit();
    return 0;
}

/* Pops the oldest request; slot (may be NULL) receives the app it came from. */
static LONG electron_poll_request(STRPTR method, ULONG method_size,
    STRPTR payload, ULONG payload_size, LONG *slot)
{
    struct ElectronNativeRequest *request;

    if (method == NULL || method_size == 0 ||
        payload == NULL || payload_size == 0)
        return 20;

    Forbid();
    if (electron_request_head == electron_request_tail)
    {
        Permit();
        return 0;
    }
    request = &electron_requests[electron_request_head];
    strncpy(method, request->method, method_size - 1);
    method[method_size - 1] = '\0';
    strncpy(payload, request->payload, payload_size - 1);
    payload[payload_size - 1] = '\0';
    if (slot != NULL)
        *slot = request->slot;
    electron_request_head =
        (electron_request_head + 1) % ELECTRON_REQUEST_CAPACITY;
    Permit();
    return 1;
}

#include "electron_renderer_seam.inc.c"

static CONST_STRPTR electron_node_invoke(CONST_STRPTR method,
    CONST_STRPTR payload, APTR private_data)
{
    (void)payload;
    (void)private_data;
    if (method != NULL && strcmp(method, "ping") == 0)
        return "AROS Electron native binding";
    if (method != NULL &&
        (strcmp(method, "console-error") == 0 ||
        strcmp(method, "uncaught-exception") == 0 ||
        strcmp(method, "unhandled-rejection") == 0))
    {
        bug("[ELECTRON-JS] %s: %s\n", method,
            payload != NULL ? (const char *)payload : "");
        return "logged";
    }
    if (method != NULL && strcmp(method, "shell.beep") == 0)
    {
        /* Intuition's own alert sound, which is what a user has configured
           for "something wants your attention" - the same bell the Shell
           rings. DisplayBeep(NULL) flashes every screen when no audible
           beep is set, which is the documented behaviour, not a fallback. */
        struct IntuitionBase *ib = (struct IntuitionBase *)
            OpenLibrary("intuition.library", 39);
        if (ib != NULL)
        {
            DisplayBeep(NULL);
            CloseLibrary((struct Library *)ib);
        }
        return "beeped";
    }
    if (method != NULL && strcmp(method, "screen.snapshot") == 0)
        return electron_screen_snapshot();
    if (method != NULL &&
        strcmp(method, "dialog.show-message-box-sync") == 0)
        return electron_show_message_box(payload);
    if (method != NULL &&
        (strcmp(method, "module-load") == 0 ||
        strcmp(method, "module-return") == 0 ||
        strcmp(method, "main-module-ready") == 0))
    {
        bug("[ELECTRON-MODULE] %s '%s'\n",
            strcmp(method, "module-load") == 0 ? "require" :
            strcmp(method, "module-return") == 0 ? "returned" :
            "signalling ready after",
            payload != NULL ? (const char *)payload : "");
        return "traced";
    }
    if (method != NULL && strncmp(method, "crash-reporter.", 15) == 0)
    {
        bug("[ELECTRON-CRASH] %s '%s'\n", method,
            payload != NULL ? (const char *)payload : "");
        return strcmp(method, "crash-reporter.start") == 0 ?
            "diagnostics-active" : "upload-disabled";
    }
    if (method != NULL && strncmp(method, "utility-process.", 16) == 0)
    {
        /* Utility children are worker_threads Workers inside this node
           instance (see the bootstrap's UtilityProcess); the JS side only
           reports fork/exit here so the lifecycle shows in the debug log. */
        bug("[ELECTRON-UTILITY] %s '%.400s'\n", method,
            payload != NULL ? (const char *)payload : "");
        return "";
    }
    if (method != NULL && strcmp(method, "native.poll") == 0)
        return electron_poll_event();
    if (method != NULL && strcmp(method, "protocol.response") == 0)
        return electron_protocol_response(payload);
    if (method != NULL &&
        strcmp(method, "protocol.register-schemes-as-privileged") == 0)
        return electron_check_privileged_schemes(payload);
    if (method != NULL &&
        (strcmp(method, "protocol.register-file-protocol") == 0 ||
        strcmp(method, "protocol.intercept-file-protocol") == 0 ||
        strcmp(method, "session.register-file-protocol") == 0 ||
        strcmp(method, "session.intercept-file-protocol") == 0))
        return electron_register_scheme_factory(payload, 0);
    if (method != NULL &&
        (strcmp(method, "protocol.unregister-protocol") == 0 ||
        strcmp(method, "protocol.unintercept-protocol") == 0 ||
        strcmp(method, "session.unregister-protocol") == 0 ||
        strcmp(method, "session.unintercept-protocol") == 0))
        return electron_register_scheme_factory(payload, 1);
    if (method != NULL && strcmp(method, "renderer.process-snapshot") == 0)
        return electron_store_process_snapshot(payload);
    if (method != NULL &&
        strcmp(method, "browser-window.node-integration") == 0)
        return electron_set_node_integration(payload);
    if (method != NULL && strcmp(method, "browser-window.create") == 0)
        return electron_prepare_window(payload);
    if (method != NULL && strcmp(method, "browser-window.set-zoom-level") == 0)
        return electron_set_zoom_level(payload);
    if (method != NULL && strcmp(method, "web-contents.send") == 0)
        return electron_deliver_to_renderer(payload);
    if (method != NULL && strcmp(method, "web-contents.execute-script") == 0)
        return electron_execute_in_window(payload);
    if (method != NULL &&
        (strncmp(method, "browser-window.", 15) == 0 ||
        strcmp(method, "app.exit") == 0 ||
        strncmp(method, "menu.", 5) == 0 ||
        strncmp(method, "session.", 8) == 0 ||
        strncmp(method, "protocol.", 9) == 0))
        return electron_queue_request(method, payload) == 0 ?
            "queued" : "queue-full";
    return "";
}

AROS_LH0(CONST_STRPTR, ElectronVersion,
    LIBBASETYPEPTR, LIBBASE, 5, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return AROS_ELECTRON_VERSION;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(ULONG, ElectronCapabilities,
    LIBBASETYPEPTR, LIBBASE, 6, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return ELECTRON_CAP_CEF | ELECTRON_CAP_NODE |
        ELECTRON_CAP_MODULE_SHIM | ELECTRON_CAP_BROWSER_WINDOW_REQUESTS;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(CONST_STRPTR, ElectronNodeVersion,
    LIBBASETYPEPTR, LIBBASE, 7, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return NodeVersion();
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronCEFVersionInfo,
    AROS_LHA(LONG, entry, D0),
    LIBBASETYPEPTR, LIBBASE, 8, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return cef_version_info(entry);
    AROS_LIBFUNC_EXIT
}

/*
 * Give the process ONE heap, before anything allocates.
 *
 * cef-blink.library allocates through PartitionAlloc; v8.library and
 * node.library fall through libstdc++ to stdc.library's malloc. Two heaps in
 * one process means an object allocated by Blink and destroyed by V8 - Blink's
 * background script streamer hands V8 exactly such a vector - is freed to the
 * wrong allocator and corrupts stdc's TLSF free list. PartitionAlloc is the
 * side to standardise on, because unlike stdc's malloc it needs no AROS
 * library base and so works on a Chromium-created thread.
 *
 * WHY HERE AND NOT LATER. Electron runs node first and initialises CEF
 * afterwards, and node uses V8. Installing the shared heap when CEF starts
 * would leave everything V8 allocated during node's phase owned by stdc and
 * freed by PartitionAlloc - the same bug, inverted. So this runs before
 * NodeStart(), and v8.library refuses a late install rather than honouring it.
 *
 * Best-effort by design: if cef-blink cannot supply an allocator, the process
 * keeps the old split-heap behaviour rather than failing to start. That is a
 * known bug, not a new one, and a node-only embedder never loads cef-blink at
 * all.
 */
static void ElectronEnsureSharedHeap(void)
{
    static BOOL attempted = FALSE;
    APTR allocFn = NULL;
    APTR freeFn = NULL;

    if (attempted)
        return;
    attempted = TRUE;

    CEFGetAllocator(&allocFn, &freeFn);
    if (allocFn == NULL || freeFn == NULL)
        return;

    /* A FALSE return means v8.library had already served an allocation from
       the default heap, so switching would free those objects to the wrong
       allocator; it refused rather than corrupt the heap. Not logged here on
       purpose: electron.library is a DISK library and kprintf/bug need
       _arosdebuglock from the kernel ROM, which makes any disk-loaded library
       that calls them fail to LINK on a pc target. An embedder that needs to
       know can call V8AllocatorInstall() itself and read the result. */
    (void)V8AllocatorInstall(allocFn, freeFn);
}

AROS_LH2(LONG, ElectronRunNode,
    AROS_LHA(LONG, argc, D0),
    AROS_LHA(STRPTR *, argv, A0),
    LIBBASETYPEPTR, LIBBASE, 9, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    ElectronEnsureSharedHeap();
    return NodeStart(argc, argv);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ElectronExecuteCEFProcess,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, application, A1),
    AROS_LHA(APTR, sandbox_info, A2),
    LIBBASETYPEPTR, LIBBASE, 10, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    ElectronEnsureSharedHeap();
    return CEFExecuteProcess(args, application, sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ElectronInitializeCEF,
    AROS_LHA(APTR, args, A0),
    AROS_LHA(APTR, settings, A1),
    AROS_LHA(APTR, application, A2),
    AROS_LHA(APTR, sandbox_info, A3),
    LIBBASETYPEPTR, LIBBASE, 11, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    ElectronEnsureSharedHeap();
    return CEFInitialize(args, settings, application, sandbox_info);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, ElectronDoCEFMessageLoopWork,
    LIBBASETYPEPTR, LIBBASE, 12, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFDoMessageLoopWork();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, ElectronShutdownCEF,
    LIBBASETYPEPTR, LIBBASE, 13, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFShutdown();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, ElectronPrepareNodeEmbedder,
    LIBBASETYPEPTR, LIBBASE, 14, Electron)
{
    LONG result;

    AROS_LIBFUNC_INIT
    (void)LIBBASE;

    result = NodeRegisterEmbedderLinkedBinding(
        "aros_electron", (APTR)electron_node_invoke, NULL);
    if (result != 0)
        return result;
    return NodePrepareEmbedder();
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronAttachNodeCurrentV8Context,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 15, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return NodeAttachCurrentV8Context(script);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, ElectronPumpNodeEmbedder,
    LIBBASETYPEPTR, LIBBASE, 16, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return NodePumpEmbedder();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, ElectronDetachNodeEmbedder,
    LIBBASETYPEPTR, LIBBASE, 17, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    NodeDetachEmbedder();
    AROS_LIBFUNC_EXIT
}

AROS_LH6(LONG, ElectronCreateCEFBrowser,
    AROS_LHA(APTR, window_info, A0),
    AROS_LHA(APTR, client, A1),
    AROS_LHA(APTR, url, A2),
    AROS_LHA(APTR, settings, A3),
    AROS_LHA(APTR, extra_info, D0),
    AROS_LHA(APTR, request_context, D1),
    LIBBASETYPEPTR, LIBBASE, 18, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFCreateBrowser(window_info, client, url, settings, extra_info,
        request_context);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ElectronPostCEFTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    LIBBASETYPEPTR, LIBBASE, 19, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFPostTask(thread_id, task);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ElectronPostDelayedCEFTask,
    AROS_LHA(LONG, thread_id, D0),
    AROS_LHA(APTR, task, A0),
    AROS_LHA(LONG, delay_ms, D1),
    LIBBASETYPEPTR, LIBBASE, 20, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFPostDelayedTask(thread_id, task, delay_ms);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ElectronRegisterCEFExtension,
    AROS_LHA(APTR, name, A0),
    AROS_LHA(APTR, javascript_code, A1),
    AROS_LHA(APTR, handler, A2),
    LIBBASETYPEPTR, LIBBASE, 21, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return CEFRegisterV8Extension(name, javascript_code, handler);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, ElectronRunCEFMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 22, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFRunMessageLoop();
    AROS_LIBFUNC_EXIT
}

AROS_LH0(void, ElectronQuitCEFMessageLoop,
    LIBBASETYPEPTR, LIBBASE, 23, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    CEFQuitMessageLoop();
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronAttachNodeIsolatedV8Context,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 24, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return NodeAttachIsolatedV8Context(script);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronAttachNodeIsolatedV8ContextWithModule,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 25, Electron)
{
    AROS_LIBFUNC_INIT
    const size_t bootstrap_length = sizeof(electron_module_bootstrap) - 1;
    const size_t script_length =
        script != NULL ? strlen((const char *)script) : 0;
    char *combined;
    LONG result;

    (void)LIBBASE;
    combined = AllocVec(bootstrap_length + script_length + 1, MEMF_ANY);
    if (combined == NULL)
        return 20;

    CopyMem(electron_module_bootstrap, combined, bootstrap_length);
    if (script_length != 0)
        CopyMem(script, combined + bootstrap_length, script_length);
    combined[bootstrap_length + script_length] = '\0';

    signal(SIGPIPE, SIG_IGN);
    result = NodeAttachIsolatedV8Context((CONST_STRPTR)combined);
    FreeVec(combined);
    return result;
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ElectronPollNativeRequest,
    AROS_LHA(STRPTR, method, A0),
    AROS_LHA(ULONG, method_size, D0),
    AROS_LHA(STRPTR, payload, A1),
    AROS_LHA(ULONG, payload_size, D1),
    LIBBASETYPEPTR, LIBBASE, 26, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_poll_request(method, method_size, payload, payload_size,
        NULL);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronAttachNodeOwnedV8ContextWithModule,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 27, Electron)
{
    AROS_LIBFUNC_INIT
    const size_t bootstrap_length = sizeof(electron_module_bootstrap) - 1;
    const size_t script_length =
        script != NULL ? strlen((const char *)script) : 0;
    char *combined;
    LONG result;
    LONG worker_threads;

    (void)LIBBASE;
    combined = AllocVec(bootstrap_length + script_length + 1, MEMF_ANY);
    if (combined == NULL)
        return 20;

    CopyMem(electron_module_bootstrap, combined, bootstrap_length);
    if (script_length != 0)
        CopyMem(script, combined + bootstrap_length, script_length);
    combined[bootstrap_length + script_length] = '\0';

    worker_threads = CEFDefaultV8WorkerThreadCount();
    signal(SIGPIPE, SIG_IGN);
    result = NodeConfigureOwnedV8WorkerThreads(worker_threads);
    if (result == 0)
        result = NodeAttachOwnedV8Context((CONST_STRPTR)combined);
    FreeVec(combined);
    return result;
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ElectronDeclareCustomScheme,
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(ULONG, options, D0),
    LIBBASETYPEPTR, LIBBASE, 28, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_declare_scheme(name, options);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, ElectronRegisterRendererExtension,
    LIBBASETYPEPTR, LIBBASE, 29, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_register_renderer_extension();
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, ElectronSetActiveBrowser,
    AROS_LHA(APTR, browser, A0),
    LIBBASETYPEPTR, LIBBASE, 30, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    electron_set_active_browser((cef_browser_t *)browser);
    AROS_LIBFUNC_EXIT
}

/*
 * App slots: several Electron apps in one process.
 *
 * CEF keeps one browser context and one UI thread per process, so hosting
 * several apps means hosting them together rather than as separate AROS
 * processes - the second process page-faults inside cef-blink.library while
 * the first keeps running. Each app still needs its own Node runtime;
 * node.library hands those out as slots, and the seam needs to know which app
 * it is serving so native events reach the right one.
 */
AROS_LH0(LONG, ElectronCreateAppRuntime,
    LIBBASETYPEPTR, LIBBASE, 33, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return NodeCreateRuntimeSlot();
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronSelectAppRuntime,
    AROS_LHA(LONG, slot, D0),
    LIBBASETYPEPTR, LIBBASE, 34, Electron)
{
    LONG rc;

    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    rc = NodeSelectRuntimeSlot(slot);
    if (rc == 0)
        electron_current_slot = slot;
    return rc;
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ElectronReleaseAppRuntime,
    AROS_LHA(LONG, slot, D0),
    LIBBASETYPEPTR, LIBBASE, 35, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    if (electron_current_slot == slot)
        electron_current_slot = 0;
    return NodeReleaseRuntimeSlot(slot);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ElectronPostWindowEvent,
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(CONST_STRPTR, payload, A1),
    LIBBASETYPEPTR, LIBBASE, 32, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_post_window_event(name, payload);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ElectronInjectRendererContext,
    AROS_LHA(APTR, browser, A0),
    AROS_LHA(APTR, frame, A1),
    AROS_LHA(APTR, context, A2),
    LIBBASETYPEPTR, LIBBASE, 31, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_inject_renderer_context((cef_browser_t *)browser,
        (cef_frame_t *)frame, (cef_v8context_t *)context);
    AROS_LIBFUNC_EXIT
}

/*
 * Windows addressed by (app slot, window id): the multi-app seam.
 *
 * A host that serves several apps polls with ElectronPollAppRequest so it
 * learns which app a `browser-window.*` request belongs to, binds each CEF
 * browser it creates to that (slot, id) with ElectronBindWindowBrowser, and
 * reports window-system facts with ElectronPostAppWindowEvent so the event
 * reaches that app's poll and no other. The three older vectors
 * (ElectronPollNativeRequest, ElectronSetActiveBrowser,
 * ElectronPostWindowEvent) keep serving a single-window host unchanged.
 */
AROS_LH5(LONG, ElectronPollAppRequest,
    AROS_LHA(STRPTR, method, A0),
    AROS_LHA(ULONG, method_size, D0),
    AROS_LHA(STRPTR, payload, A1),
    AROS_LHA(ULONG, payload_size, D1),
    AROS_LHA(LONG *, slot, A2),
    LIBBASETYPEPTR, LIBBASE, 36, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_poll_request(method, method_size, payload, payload_size,
        slot);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ElectronBindWindowBrowser,
    AROS_LHA(LONG, slot, D0),
    AROS_LHA(ULONG, id, D1),
    AROS_LHA(APTR, browser, A0),
    LIBBASETYPEPTR, LIBBASE, 37, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_bind_window_browser(slot, id, (cef_browser_t *)browser);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ElectronPostAppWindowEvent,
    AROS_LHA(LONG, slot, D0),
    AROS_LHA(ULONG, id, D1),
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(CONST_STRPTR, payload, A1),
    LIBBASETYPEPTR, LIBBASE, 38, Electron)
{
    AROS_LIBFUNC_INIT
    (void)LIBBASE;
    return electron_post_app_window_event(slot, id, name, payload);
    AROS_LIBFUNC_EXIT
}
