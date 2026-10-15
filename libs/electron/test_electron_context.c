/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.
*/

/*
 * First simultaneous CEF and Node context probe.
 *
 * CEF owns V8 initialization and creates the renderer isolate/context.  The
 * renderer callback then asks node.library, through electron.library, to
 * create a Node Environment in that already-current context.  No engine
 * archive is linked into this client or electron.library.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <exec/rawfmt.h>

#include <dos/dosextens.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/types.h>
#include <libraries/electron.h>
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/electron.h>
#include <proto/exec.h>
#ifdef ELECTRON_SHELL_WINDOWED
#include <dos/dostags.h>
#include <dos/var.h>
#include <exec/lists.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/intuition.h>
#endif

#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_display_handler_capi.h"
#include "include/capi/cef_frame_capi.h"
#include "include/capi/cef_life_span_handler_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_render_handler_capi.h"
#include "include/capi/cef_render_process_handler_capi.h"
#include "include/capi/cef_scheme_capi.h"
#include "include/capi/cef_v8_capi.h"

/*
 * Log a line, from a disk-loaded program, on a native target.
 *
 * The obvious KPrintF() cannot be used here. The debug linklib's copy compiles
 * to a call through a library base that the ELF loader never relocates for a
 * program loaded from disk on pc-x86_64:
 *
 *     movabs 0x0,%rax          ; the base, read from absolute address 0
 *     callq  *-0x448(%rax)     ; ...and called through anyway
 *
 * so the shell's first line of output is a privilege violation in
 * ElectronShell!.text+0x24da, with CrBrowserMain as the task and nothing
 * printed to say why. Hosted hides this: there the same symbol resolves.
 *
 * exec's own formatter has no such problem - SysBase is set up correctly for
 * any program - and RAWFMTFUNC_SERIAL is the same debug stream KPrintF was
 * aiming at, so the output is unchanged.
 */
static void shell_log(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    VNewRawDoFmt(format, RAWFMTFUNC_SERIAL, NULL, args);
    va_end(args);
}

/* Everything below logs through the safe path. */
#undef KPrintF
#define KPrintF shell_log


#define VIEW_WIDTH 640
#define VIEW_HEIGHT 360
#define NODE_PUMP_TICKS 64
#define CLOSE_AFTER_PAINT_TICKS 100
#define CREATE_TIMEOUT_TICKS 30000
#define CLOSE_TIMEOUT_TICKS 500
/* App mode: after the last window closed, how long the main script gets to
   run its shutdown (VS Code saves state before it calls app.quit()). */
#define APP_CLOSE_GRACE_TICKS 3000
#define SHUTDOWN_DRAIN_TICKS 200
#define MAIN_SCRIPT_OPTION "--main-script="
#ifdef ELECTRON_SHELL_WINDOWED
/*
 * One engine per boot.  cef-blink.library is resident and CEF can be
 * initialised once in it, so every ElectronShell after the first must reuse
 * the running engine.  The engine is its own process (ENGINE_HOST_OPTION),
 * owning CEF's UI thread, the windows and every app's Node runtime slot.
 * Each ElectronShell is a launcher: it sends its app to the engine port,
 * waits for the app's exit code and exits with it.
 */
#define APP_SCRIPT_OPTION "--app-script="
#define ENGINE_HOST_OPTION "--engine-host"
#define ENGINE_PORT_NAME "ElectronShell.engine"
#define ENGINE_REQUEST_MAGIC 0x45534531UL
#define ENGINE_RUN_APP 1
#define ENGINE_CANCEL 2
#define MAX_EXTRA_APPS 4
#define MAX_ENGINE_APPS 8
#define ENGINE_LIVENESS_TICKS 50
#define ENGINE_START_TIMEOUT_TICKS 3000
#define APP_CANCELLED_CODE 10

struct EngineRequest
{
    struct Message msg;
    ULONG magic;
    LONG kind;
    ULONG launcher_pid;
    LONG app_index;             /* 0 = the main script, 1.. = --app-script */
    const char *main_script;    /* NULL: the built-in probe script */
    const char *url;            /* command URL, or NULL */
    int argc;
    char **argv;
    const char *env;            /* local variables, "name\0value\0...\0" */
    BPTR current_dir;           /* the engine owns it once received */
    BPTR output;                /* the launcher's, written while it waits */
    BPTR error;
    LONG result;
};

/* The engine's app registry: one entry per app attached to a runtime slot. */
struct EngineApp
{
    int in_use;
    LONG slot;
    LONG index;
    struct EngineRequest *request;
    ULONG launcher_pid;
    int launcher_gone;
    BPTR current_dir;
    struct DateStamp started;
    ULONG start_tick;
    int probe_mode;
    cef_char_t *default_url;
    size_t default_url_length;
    int has_window;
    int first_window_seen;
    ULONG first_window_tick;
    int windows_gone;
    ULONG windows_gone_tick;
    int exit_requested;
    ULONG exit_tick;
    LONG exit_code;
};

static struct EngineApp engine_apps[MAX_ENGINE_APPS];
static int engine_app_count;
static struct MsgPort *engine_port;
#endif
static cef_app_t app;
static cef_client_t client;
static cef_display_handler_t display_handler;
static cef_life_span_handler_t life_span_handler;
static cef_load_handler_t load_handler;
static cef_render_handler_t render_handler;
static cef_render_process_handler_t render_process_handler;
static cef_v8handler_t node_pump_handler;

static cef_browser_t *browser;
static cef_v8context_t *node_context;
static struct Task *node_context_task;
static struct MsgPort *ui_pump_port;
static struct Message ui_pump_message;
static int browser_closed;
static int close_requested;
static int node_attached;
static int node_detached;
static int node_task_mismatch;
static int node_marker_seen;
static int completion_queued;
static int external_url_mode;
static ULONG browser_closed_tick;
static ULONG node_pump_ticks;
static int first_paint;
static ULONG first_paint_tick;
static ULONG pump_tick;

/*
 * Developer:CEF, not Developer:Chromium: the paks must come from the build
 * cef-blink.library was linked from (grit numbers IDR_* per build).  See the
 * lifecycle probe for the failure a chrome-target pak produces.
 */
static cef_char_t resources_path[] = u"Developer:CEF";
static cef_char_t locales_path[] = u"Developer:CEF/locales";
static cef_char_t root_cache_path[] = u"T:ElectronContext";
static cef_char_t cache_path[] = u"T:ElectronContext/Default";
#ifdef ELECTRON_SHELL_WINDOWED
static cef_char_t test_url[] =
    u"data:text/html,<html><head><title>AROS Electron Shell</title>"
    u"<style>"
    u"html,body{margin:0;width:100%;height:100%;overflow:hidden;"
    u"background:rgb(20,26,36);color:rgb(239,244,255);"
    u"font-family:sans-serif}"
    u"body{display:flex;align-items:center;justify-content:center}"
    u"main{width:78%;padding:48px;border:1px solid rgb(54,78,104);"
    u"background:rgb(27,36,50);box-shadow:0 18px 60px rgb(5,8,14)}"
    u"h1{margin:0 0 12px;font-size:44px;font-weight:500;"
    u"color:rgb(64,203,255)}"
    u"p{font-size:21px;line-height:1.5;margin:8px 0}"
    u".ok{margin-top:30px;padding:14px 18px;background:rgb(34,50,68);"
    u"border-left:5px solid rgb(64,203,255)}"
    u"</style></head><body><main><h1>Electron on AROS</h1>"
    u"<p>CEF is rendering this native window.</p>"
    u"<p>Node is attached to the live Blink V8 context through "
    u"electron.library.</p>"
    u"<div class='ok'>Shared CEF + shared Node + shared V8</div>"
    u"</main></body></html>";
static cef_char_t window_title[] = u"AROS Electron Shell";
#else
static cef_char_t test_url[] =
    u"data:text/html,<html><body>AROS Electron Node Context</body></html>";
#endif
static cef_char_t extension_name[] = u"aros/electron_node_pump";
static cef_char_t extension_code[] =
    u"Object.defineProperty(globalThis, 'AROSElectron', {"
    u"value: {}, writable: false, configurable: false"
    u"});"
    u"(function() {"
    u"globalThis.AROSElectron.pumpNode = function() {"
    u"native function AROSPumpNodeNative();"
    u"return AROSPumpNodeNative();"
    u"};"
    u"})();";
static cef_char_t timer_code[] =
    u"globalThis.__arosNodePumpTimer = setInterval(function() {"
    u"globalThis.AROSElectron.pumpNode();"
    u"}, 1);";
static cef_char_t timer_script_url[] = u"aros://electron/node-pump";
static const unsigned char node_script[] =
    "const {app,native,BrowserWindow,ipcMain,nativeTheme,powerMonitor,screen,"
    "protocol}=require('electron');"
    "if(app.isReady())throw new Error('Electron app became ready synchronously');"
    "const primaryDisplay=screen.getPrimaryDisplay();"
    "if(!primaryDisplay||primaryDisplay.bounds.width<1||"
    "primaryDisplay.bounds.height<1)"
    "throw new Error('Electron screen snapshot failed');"
    "if(typeof powerMonitor.on!=='function')"
    "throw new Error('Electron powerMonitor event contract failed');"
    "protocol.registerFileProtocol('aros-test',(_request,callback)=>"
    "callback({path:'T:ElectronProtocolTest'}));"
    "protocol.registerBufferProtocol('aros-buffer-test',(_request,callback)=>"
    "callback({data:Buffer.from('AROS')}));"
    "protocol.registerHttpProtocol('aros-http-test',(_request,callback)=>"
    "callback({url:'https://aros.org/'}));"
    "if(nativeTheme.themeSource!=='system')"
    "throw new Error('Electron nativeTheme default source mismatch');"
    "let themeUpdates=0;"
    "nativeTheme.on('updated',()=>{themeUpdates++;});"
    "nativeTheme.themeSource='dark';"
    "if(!nativeTheme.shouldUseDarkColors||themeUpdates!==1)"
    "throw new Error('Electron nativeTheme update failed');"
    "nativeTheme.themeSource='system';"
    "let ipcMessage='';"
    "ipcMain.on('aros-test-message',(_event,value)=>{ipcMessage=value;});"
    "ipcMain._arosDispatchMessage('aros-test-message',{},'message-pass');"
    "if(ipcMessage!=='message-pass')"
    "throw new Error('Electron ipcMain message dispatch failed');"
    "ipcMain.handle('aros-test-invoke',(_event,value)=>value+'-pass');"
    "ipcMain._arosDispatchInvoke('aros-test-invoke',{},'invoke').then(value=>{"
    "if(value!=='invoke-pass')"
    "throw new Error('Electron ipcMain invoke dispatch failed');"
    "process._rawDebug('[ELECTRON-MODULE] ipcMain dispatch pass');"
    "});"
    "const nativeResult=native.ping();"
    "if(nativeResult!=='AROS Electron native binding')"
    "throw new Error('Electron native binding mismatch: '+nativeResult);"
    "process._rawDebug('[ELECTRON-MODULE] require electron app='+"
    "app.getName()+' version='+app.getVersion()+' native='+nativeResult);"
#ifdef ELECTRON_SHELL_WINDOWED
    "app.whenReady().then(()=>{"
    "if(!app.isReady())throw new Error('Electron ready promise resolved early');"
    "process._rawDebug('[ELECTRON-MODULE] app ready');"
    "const mainWindow=new BrowserWindow({width:640,height:360});"
    "if(typeof mainWindow.webContents.on!=='function')"
    "throw new Error('Electron webContents event contract failed');"
    "if(BrowserWindow.getFocusedWindow()!==mainWindow)"
    "throw new Error('Electron focused window contract failed');"
    "mainWindow.setTitle('AROS Electron Context');"
    "if(mainWindow.getTitle()!=='AROS Electron Context'||mainWindow.isDestroyed())"
    "throw new Error('Electron window title/lifecycle contract failed');"
    "if(!mainWindow.isVisible()||mainWindow.isMinimized())"
    "throw new Error('Electron window visibility default failed');"
    "mainWindow.hide();"
    "if(mainWindow.isVisible())"
    "throw new Error('Electron window hide state failed');"
    "mainWindow.show();"
    "if(!mainWindow.isVisible())"
    "throw new Error('Electron window show state failed');"
    "mainWindow.minimize();"
    "if(!mainWindow.isMinimized()||mainWindow.isVisible())"
    "throw new Error('Electron window minimize state failed');"
    "mainWindow.restore();"
    "if(mainWindow.isMinimized()||!mainWindow.isVisible())"
    "throw new Error('Electron window restore state failed');"
    "mainWindow.focus();"
    "if(mainWindow.isMaximized()||mainWindow.webContents.isDestroyed())"
    "throw new Error('Electron native lifecycle default failed');"
    "mainWindow.maximize();"
    "if(!mainWindow.isMaximized())"
    "throw new Error('Electron maximize state failed');"
    "if(mainWindow.isFullScreen())"
    "throw new Error('Electron fullscreen default failed');"
    "mainWindow.setFullScreen(true);"
    "if(!mainWindow.isFullScreen())"
    "throw new Error('Electron fullscreen state failed');"
    "if(mainWindow.isSimpleFullScreen())"
    "throw new Error('Electron simple fullscreen default failed');"
    "mainWindow.setSimpleFullScreen(true);"
    "if(!mainWindow.isSimpleFullScreen())"
    "throw new Error('Electron simple fullscreen state failed');"
    "mainWindow.setMenuBarVisibility(false);"
    "if(mainWindow.isMenuBarVisible())"
    "throw new Error('Electron menu visibility state failed');"
    "mainWindow.loadURL('aros-electron:startup');"
    "});"
#endif
    "const fs=require('fs');"
    "let arosTicks=0;"
    "const arosStep=()=>{"
    "if(++arosTicks===3){"
    "fs.writeFileSync('T:ElectronNodePersistent.pass',"
    "'ELECTRON_NODE_PERSISTENT_PASS '+process.version+' '+"
    "app.getName()+' '+app.getVersion()+'\\n');"
    "}else{setImmediate(arosStep);}"
    "};"
    "setImmediate(arosStep);"
    "globalThis.AROSNodeVersion=process.version;";

static void set_static_cef_string(cef_string_t *target, cef_char_t *value,
    size_t length)
{
    target->str = value;
    target->length = length;
    target->dtor = NULL;
}

static void CEF_CALLBACK static_add_ref(cef_base_ref_counted_t *self)
{
    (void)self;
}

static int CEF_CALLBACK static_release(cef_base_ref_counted_t *self)
{
    (void)self;
    return 0;
}

static int CEF_CALLBACK static_has_one_ref(cef_base_ref_counted_t *self)
{
    (void)self;
    return 1;
}

static int CEF_CALLBACK static_has_at_least_one_ref(
    cef_base_ref_counted_t *self)
{
    (void)self;
    return 1;
}

static void initialize_base(cef_base_ref_counted_t *base, size_t size)
{
    base->size = size;
    base->add_ref = static_add_ref;
    base->release = static_release;
    base->has_one_ref = static_has_one_ref;
    base->has_at_least_one_ref = static_has_at_least_one_ref;
}

static void log_cef_string(const char *label, const cef_string_t *value)
{
    char text[512];
    size_t i;
    size_t length = value != NULL ? value->length : 0;

    if (length >= sizeof(text))
        length = sizeof(text) - 1;
    for (i = 0; i < length; ++i)
    {
        cef_char_t character = value->str[i];
        text[i] = character <= 0x7f ? (char)character : '?';
    }
    text[length] = '\0';
    KPrintF("[ELECTRON-CONTEXT] %s '%s'\n", label, text);
}

static int marker_exists(void)
{
    BPTR marker = Lock("T:ElectronNodePersistent.pass", ACCESS_READ);

    if (marker == BNULL)
        return 0;
    UnLock(marker);
    return 1;
}

static void release_node_context(void)
{
    if (node_context != NULL && node_context->base.release != NULL)
        node_context->base.release(&node_context->base);
    node_context = NULL;
}

static void finish_node_on_renderer(void)
{
    ElectronDetachNodeEmbedder();
    node_detached = 1;
    KPrintF("[ELECTRON-CONTEXT] persistent Node detached after %lu pumps\n",
        node_pump_ticks);
}

static void release_browser_reference(cef_browser_t *cef_browser)
{
    if (cef_browser != NULL && cef_browser->base.release != NULL)
        cef_browser->base.release(&cef_browser->base);
}

static cef_browser_host_t *get_browser_host(void)
{
    if (browser == NULL || browser->get_host == NULL)
        return NULL;
    return browser->get_host(browser);
}

static void release_browser_host(cef_browser_host_t *host)
{
    if (host != NULL && host->base.release != NULL)
        host->base.release(&host->base);
}

static void service_renderer_pump_port(void);

static void request_browser_close(void)
{
    cef_browser_host_t *host;

    if (close_requested)
        return;
    close_requested = 1;
    host = get_browser_host();
    if (host != NULL && host->close_browser != NULL)
        host->close_browser(host, 1);
    release_browser_host(host);
}

static void drain_cef_before_shutdown(void)
{
    ULONG drain_ticks;

    for (drain_ticks = 0; drain_ticks < SHUTDOWN_DRAIN_TICKS; ++drain_ticks)
    {
        ElectronDoCEFMessageLoopWork();
        service_renderer_pump_port();
        Delay(1);
    }
    KPrintF("[ELECTRON-CONTEXT] CEF shutdown drain ticks=%lu\n",
        (unsigned long)SHUTDOWN_DRAIN_TICKS);
}

static void pump_node_on_context_task(void)
{
    struct Task *current_task = FindTask(NULL);
    LONG pump_rc;

    if (!node_attached || node_detached)
        return;
    if (current_task != node_context_task)
    {
        node_task_mismatch = 1;
        KPrintF("[ELECTRON-CONTEXT] Node pump task mismatch current=%p "
            "context=%p\n", current_task, node_context_task);
        return;
    }

    pump_rc = ElectronPumpNodeEmbedder();
    ++node_pump_ticks;
    if ((node_pump_ticks & 7UL) == 0)
        KPrintF("[ELECTRON-CONTEXT] direct Node pump=%lu rc=%ld\n",
            node_pump_ticks, pump_rc);

    if (node_pump_ticks >= NODE_PUMP_TICKS)
    {
        node_marker_seen = marker_exists();
        finish_node_on_renderer();
        release_node_context();
    }
}

static void service_renderer_pump_port(void)
{
    struct Message *message;

    if (ui_pump_port == NULL)
        return;
    message = GetMsg(ui_pump_port);
    if (message == NULL)
        return;
    KPrintF("[ELECTRON-CONTEXT] UI dequeued renderer handoff pump=%lu\n",
        node_pump_ticks);

    if (node_detached && !external_url_mode)
    {
        KPrintF("[ELECTRON-CONTEXT] renderer completion reached UI task\n");
        request_browser_close();
    }
}

#ifdef ELECTRON_SHELL_WINDOWED
/*
 * BrowserWindow geometry.  Electron's BrowserWindow owns a native top-level
 * window and its setBounds/maximize/setMinimumSize/setTitle are window-system
 * calls; here the window system is Intuition.  Before the browser exists the
 * requests shape the CefWindowInfo it is created with; afterwards CEF hands
 * the Intuition window back through get_window_handle() (as the X11 build
 * hands back its ::Window) and the shell drives it with ChangeWindowBox &
 * co.  The Ozone platform sees the IDCMP_CHANGEWINDOW like any user resize
 * and relays it to the renderer.
 */
struct shell_window_geometry
{
    LONG x, y, width, height;   /* client bounds Electron asked for */
    int has_position;           /* x/y were given (else Intuition's choice) */
    LONG min_width, min_height;
    int maximized;
    int fullscreen;
    LONG restore_x, restore_y, restore_width, restore_height;
    int restore_valid;
    int bounds_dirty;
    int limits_dirty;
    int title_dirty;
    int activate_dirty;
    char title[256];
};

/*
 * One record per BrowserWindow, of any app in this process.  A
 * window is addressed the way the engine addresses it, by (app runtime slot,
 * BrowserWindow id) - ids restart at 1 in every app.  The record collects
 * what the script asked for before the browser exists (URL, geometry), then
 * the browser CEF created for it; CEF's callbacks find it again through the
 * browser identifier, which the browser and renderer sides agree on.
 */
#define MAX_SHELL_WINDOWS 16

struct ShellWindow
{
    int in_use;
    LONG slot;
    ULONG id;
    cef_browser_t *browser;
    int browser_identifier;
    /* Created, waiting for on_after_created; the oldest such record is the
       one the next created browser belongs to. */
    int creating;
    ULONG create_order;
    int create_requested;       /* create once this service pass drained */
    int close_requested;
    int url_dirty;              /* a load-url arrived after creation */
    cef_char_t url[2048];
    size_t url_length;
    struct shell_window_geometry geometry;
};

static struct ShellWindow shell_windows[MAX_SHELL_WINDOWS];
static ULONG shell_create_counter;
static struct ShellWindow *shell_find_window(LONG slot, ULONG id)
{
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use && shell_windows[i].slot == slot &&
            shell_windows[i].id == id)
            return &shell_windows[i];
    return NULL;
}

static struct ShellWindow *shell_find_window_by_browser(
    cef_browser_t *cef_browser)
{
    int identifier;
    int i;

    if (cef_browser == NULL || cef_browser->get_identifier == NULL)
        return NULL;
    identifier = cef_browser->get_identifier(cef_browser);
    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use && shell_windows[i].browser != NULL &&
            shell_windows[i].browser_identifier == identifier)
            return &shell_windows[i];
    return NULL;
}

static struct ShellWindow *shell_find_or_create_window(LONG slot, ULONG id)
{
    struct ShellWindow *window = shell_find_window(slot, id);
    int i;

    if (window != NULL)
        return window;
    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
    {
        if (!shell_windows[i].in_use)
        {
            window = &shell_windows[i];
            memset(window, 0, sizeof(*window));
            window->in_use = 1;
            window->slot = slot;
            window->id = id;
            /* Electron's BrowserWindow default until the script says
               otherwise. */
            window->geometry.width = 800;
            window->geometry.height = 600;
            return window;
        }
    }
    KPrintF("[ELECTRON-CONTEXT] no window record for %ld/%lu "
        "(%ld windows max)\n", slot, id, (LONG)MAX_SHELL_WINDOWS);
    return NULL;
}

static int shell_live_window_count(void)
{
    int count = 0;
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use)
            ++count;
    return count;
}

static struct Window *shell_native_window(struct ShellWindow *window)
{
    cef_browser_host_t *host = NULL;
    struct Window *native_window = NULL;

    if (window->browser != NULL && window->browser->get_host != NULL)
        host = window->browser->get_host(window->browser);
    if (host != NULL)
    {
        native_window = (struct Window *)host->get_window_handle(host);
        release_browser_host(host);
    }
    return native_window;
}

/* The screen area a window may cover: everything below the screen title bar,
   or the whole screen for full-screen.  Same policy as the Ozone platform's
   work area, so pre- and post-creation clamping agree. */
static void shell_work_area(struct Window *native_window, int fullscreen,
    LONG *x, LONG *y, LONG *width, LONG *height)
{
    struct Screen *screen = native_window != NULL ? native_window->WScreen :
        LockPubScreen(NULL);

    *x = 0;
    *y = 0;
    *width = 640;
    *height = 480;
    if (screen != NULL)
    {
        *y = fullscreen ? 0 : screen->BarHeight + 1;
        *width = screen->Width;
        *height = screen->Height - *y;
        if (native_window == NULL)
            UnlockPubScreen(NULL, screen);
    }
}

static LONG clamp_long(LONG value, LONG low, LONG high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

/* Resolve the client bounds to apply: the maximize/full-screen work area or
   the requested bounds clamped into it. */
static void shell_resolve_bounds(struct ShellWindow *window,
    struct Window *native_window,
    LONG *x, LONG *y, LONG *width, LONG *height, int *has_position)
{
    struct shell_window_geometry *geometry = &window->geometry;
    LONG area_x, area_y, area_width, area_height;
    LONG border_x = 0, border_y = 0;

    if (native_window != NULL)
    {
        border_x = native_window->BorderLeft + native_window->BorderRight;
        border_y = native_window->BorderTop + native_window->BorderBottom;
    }
    shell_work_area(native_window, geometry->fullscreen,
        &area_x, &area_y, &area_width, &area_height);
    if (geometry->maximized || geometry->fullscreen)
    {
        *x = area_x;
        *y = area_y;
        *width = area_width - border_x;
        *height = area_height - border_y;
        *has_position = 1;
        return;
    }
    *width = clamp_long(geometry->width, 1, area_width - border_x);
    *height = clamp_long(geometry->height, 1, area_height - border_y);
    *has_position = geometry->has_position;
    *x = clamp_long(geometry->x, area_x,
        area_x + area_width - (*width + border_x));
    *y = clamp_long(geometry->y, area_y,
        area_y + area_height - (*height + border_y));
}

/* Apply whatever changed to the live Intuition window; a no-op until the
   browser (and with it the window) exists. */
static void shell_sync_window_geometry(struct ShellWindow *window)
{
    struct shell_window_geometry *geometry = &window->geometry;
    struct Window *native_window;
    LONG x, y, width, height;
    int has_position;

    if (window->browser == NULL)
        return;
    native_window = shell_native_window(window);
    if (native_window == NULL)
    {
        if (geometry->bounds_dirty || geometry->title_dirty ||
            geometry->limits_dirty)
            KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu has no native "
                "window handle; geometry request dropped\n",
                window->slot, window->id);
        geometry->bounds_dirty = 0;
        geometry->limits_dirty = 0;
        geometry->title_dirty = 0;
        geometry->activate_dirty = 0;
        return;
    }
    if (geometry->limits_dirty)
    {
        geometry->limits_dirty = 0;
        WindowLimits(native_window,
            geometry->min_width + native_window->BorderLeft +
                native_window->BorderRight,
            geometry->min_height + native_window->BorderTop +
                native_window->BorderBottom,
            ~0, ~0);
    }
    if (geometry->bounds_dirty)
    {
        geometry->bounds_dirty = 0;
        shell_resolve_bounds(window, native_window, &x, &y, &width, &height,
            &has_position);
        if (!has_position)
        {
            x = native_window->LeftEdge;
            y = native_window->TopEdge;
        }
        KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu bounds -> %ld,%ld "
            "%ldx%ld (maximized=%ld fullscreen=%ld window=%p)\n",
            window->slot, window->id, x, y, width, height,
            (LONG)geometry->maximized, (LONG)geometry->fullscreen,
            native_window);
        ChangeWindowBox(native_window, x, y,
            width + native_window->BorderLeft + native_window->BorderRight,
            height + native_window->BorderTop + native_window->BorderBottom);
    }
    if (geometry->title_dirty)
    {
        geometry->title_dirty = 0;
        SetWindowTitles(native_window, geometry->title, (STRPTR)~0);
    }
    if (geometry->activate_dirty)
    {
        geometry->activate_dirty = 0;
        WindowToFront(native_window);
        ActivateWindow(native_window);
    }
}

static void shell_sync_all_window_geometry(void)
{
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use)
            shell_sync_window_geometry(&shell_windows[i]);
}

/* Remember the normal bounds before maximize/full-screen so restore has
   something to go back to. */
static void shell_save_restore_bounds(struct shell_window_geometry *geometry)
{
    if (geometry->restore_valid || geometry->maximized ||
        geometry->fullscreen)
        return;
    geometry->restore_x = geometry->x;
    geometry->restore_y = geometry->y;
    geometry->restore_width = geometry->width;
    geometry->restore_height = geometry->height;
    geometry->restore_valid = 1;
}

static void shell_restore_bounds(struct shell_window_geometry *geometry)
{
    if (geometry->maximized || geometry->fullscreen ||
        !geometry->restore_valid)
        return;
    geometry->x = geometry->restore_x;
    geometry->y = geometry->restore_y;
    geometry->width = geometry->restore_width;
    geometry->height = geometry->restore_height;
    geometry->restore_valid = 0;
}

/* Tab-separated numeric fields after the window id; an empty field leaves
   its value untouched and returns 0 for that slot. */
static int parse_geometry_fields(const char *payload, LONG *values,
    int count, int *given)
{
    const char *cursor = strchr(payload, '\t');
    int i;

    for (i = 0; i < count; ++i)
        given[i] = 0;
    for (i = 0; i < count && cursor != NULL; ++i)
    {
        char *end;
        long value;

        ++cursor;
        if (*cursor != '\t' && *cursor != '\0')
        {
            value = strtol(cursor, &end, 10);
            if (end != cursor)
            {
                values[i] = (LONG)value;
                given[i] = 1;
            }
        }
        cursor = strchr(cursor, '\t');
    }
    return i;
}

/*
 * Does this string already name a URL scheme?
 *
 * "SYS:Developer/App/index.html" and "data:text/html,..." are both a name, a
 * colon and a tail, so the colon alone cannot tell an AROS volume from a URL
 * scheme - testing for one used to turn every schemed URL without an authority
 * ("data:", "about:") into "file:///data:..." and CEF rejected it as
 * ERR_INVALID_URL.  Schemes are a closed set here, volume names are not, so
 * match the schemes.
 */
static int url_has_known_scheme(const char *url)
{
    static const char *const schemes[] =
    {
        "http:", "https:", "file:", "data:", "about:", "blob:", "chrome:",
        "chrome-extension:", "devtools:", "javascript:", "ws:", "wss:",
        "vscode-file:", "vscode-webview:"
    };
    size_t i;

    for (i = 0; i < sizeof(schemes) / sizeof(schemes[0]); ++i)
    {
        size_t length = strlen(schemes[i]);

        if (strncasecmp(url, schemes[i], length) == 0)
            return 1;
    }
    return 0;
}

static int handle_window_geometry_request(struct ShellWindow *window,
    const char *method, const char *payload)
{
    struct shell_window_geometry *geometry = &window->geometry;
    LONG values[4];
    int given[4];

    if (strcmp(method, "browser-window.set-bounds") == 0)
    {
        values[0] = geometry->x;
        values[1] = geometry->y;
        values[2] = geometry->width;
        values[3] = geometry->height;
        parse_geometry_fields(payload, values, 4, given);
        geometry->x = values[0];
        geometry->y = values[1];
        if (given[0] && given[1])
            geometry->has_position = 1;
        if (values[2] > 0)
            geometry->width = values[2];
        if (values[3] > 0)
            geometry->height = values[3];
        geometry->restore_valid = 0;
        geometry->bounds_dirty = 1;
    }
    else if (strcmp(method, "browser-window.set-minimum-size") == 0)
    {
        values[0] = geometry->min_width;
        values[1] = geometry->min_height;
        parse_geometry_fields(payload, values, 2, given);
        geometry->min_width = values[0] > 0 ? values[0] : 0;
        geometry->min_height = values[1] > 0 ? values[1] : 0;
        geometry->limits_dirty = 1;
    }
    else if (strcmp(method, "browser-window.maximize") == 0)
    {
        shell_save_restore_bounds(geometry);
        geometry->maximized = 1;
        geometry->bounds_dirty = 1;
    }
    else if (strcmp(method, "browser-window.unmaximize") == 0 ||
        strcmp(method, "browser-window.restore") == 0)
    {
        geometry->maximized = 0;
        shell_restore_bounds(geometry);
        geometry->bounds_dirty = 1;
    }
    else if (strcmp(method, "browser-window.set-full-screen") == 0)
    {
        const char *value = strchr(payload, '\t');
        int fullscreen = value != NULL && strcmp(value + 1, "true") == 0;

        if (fullscreen)
            shell_save_restore_bounds(geometry);
        geometry->fullscreen = fullscreen;
        if (!fullscreen)
            shell_restore_bounds(geometry);
        geometry->bounds_dirty = 1;
    }
    else if (strcmp(method, "browser-window.set-title") == 0)
    {
        const char *value = strchr(payload, '\t');

        if (value != NULL)
        {
            strncpy(geometry->title, value + 1, sizeof(geometry->title) - 1);
            geometry->title[sizeof(geometry->title) - 1] = '\0';
            geometry->title_dirty = 1;
        }
    }
    else if (strcmp(method, "browser-window.show") == 0 ||
        strcmp(method, "browser-window.focus") == 0)
        geometry->activate_dirty = 1;
    else
        return 0;
    return 1;
}

/* The URL a window shows, as CEF needs it: BrowserWindow accepts native
   paths as well as URLs, and AROS volume syntax looks like a URL scheme to
   CEF, so a path becomes a file URL before crossing the Electron/CEF seam. */
static void shell_set_window_url(struct ShellWindow *window,
    const char *requested_url)
{
    size_t i;
    size_t output_offset = 0;

    if (!url_has_known_scheme(requested_url) &&
        strchr(requested_url, ':') != NULL)
    {
        static const char file_prefix[] = "file:///";

        for (i = 0; file_prefix[i] != '\0'; ++i)
            window->url[i] = (unsigned char)file_prefix[i];
        output_offset = i;
    }
    for (i = 0; requested_url[i] != '\0' &&
        output_offset + i + 1 < (sizeof(window->url) /
            sizeof(window->url[0])); ++i)
        window->url[output_offset + i] = (unsigned char)requested_url[i];
    window->url[output_offset + i] = 0;
    window->url_length = output_offset + i;
    window->url_dirty = 1;
    KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu requested URL '%s' "
        "routed through CEF URL length=%lu\n", window->slot, window->id,
        requested_url, (unsigned long)window->url_length);
}

static void shell_request_window_close(struct ShellWindow *window)
{
    cef_browser_host_t *host;

    if (window->close_requested)
        return;
    window->close_requested = 1;
    if (window->browser == NULL && window->creating)
        return;
    if (window->browser == NULL)
    {
        /* Never got a browser (creation failed, or closed before it
           existed): nothing will call on_before_close for it. */
        KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu closed before "
            "creation\n", window->slot, window->id);
        ElectronBindWindowBrowser(window->slot, window->id, NULL);
        window->in_use = 0;
        return;
    }
    host = window->browser->get_host(window->browser);
    if (host != NULL && host->close_browser != NULL)
        host->close_browser(host, 1);
    release_browser_host(host);
}

static void shell_request_all_windows_close(void)
{
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use)
            shell_request_window_close(&shell_windows[i]);
}

/* One app's windows: the ones created under its runtime slot. */
static int shell_slot_window_count(LONG slot)
{
    int count = 0;
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use && shell_windows[i].slot == slot)
            ++count;
    return count;
}

static void shell_request_slot_windows_close(LONG slot)
{
    int i;

    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        if (shell_windows[i].in_use && shell_windows[i].slot == slot)
            shell_request_window_close(&shell_windows[i]);
}

static struct EngineApp *engine_find_app(LONG slot)
{
    int i;

    if (slot <= 0)
        return NULL;
    for (i = 0; i < MAX_ENGINE_APPS; ++i)
        if (engine_apps[i].in_use && engine_apps[i].slot == slot)
            return &engine_apps[i];
    return NULL;
}

/*
 * The registry just emptied.  The engine stays initialised and idle: CEF can
 * only be initialised again in this resident image once cef-blink restores
 * its pristine data, so a full lights-out (CEF shutdown, state back to
 * Uninitialized) belongs here when it can.
 */
static void engine_lights_out(void)
{
    KPrintF("[ELECTRON-ENGINE] registry empty, engine idle (CEF stays "
        "initialised)\n");
}

static void engine_request_app_exit(struct EngineApp *app, LONG code)
{
    if (app->exit_requested)
        return;
    app->exit_requested = 1;
    app->exit_code = code;
    app->exit_tick = pump_tick;
    shell_request_slot_windows_close(app->slot);
}

/*
 * Ends one app: its Node runtime is detached, its slot released, its windows
 * (if any still open) no longer belong to a slot the next app may get, and
 * its launcher gets the exit code - unless the launcher is gone.
 */
static void engine_finish_app(struct EngineApp *app, LONG code)
{
    BPTR old_dir = CurrentDir(app->current_dir);
    int i;

    if (ElectronSelectAppRuntime(app->slot) == 0)
    {
        ElectronDetachNodeEmbedder();
        ElectronSelectAppRuntime(0);
    }
    CurrentDir(old_dir);
    for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
    {
        struct ShellWindow *window = &shell_windows[i];

        if (window->in_use && window->slot == app->slot)
        {
            shell_request_window_close(window);
            window->slot = -1;
        }
    }
    ElectronReleaseAppRuntime(app->slot);
    if (app->current_dir != BNULL)
        UnLock(app->current_dir);
    FreeVec(app->default_url);
    --engine_app_count;
    KPrintF("[ELECTRON-ENGINE] registry -app slot=%ld index=%ld launcher=%lu "
        "code=%ld%s count=%ld\n", app->slot, app->index, app->launcher_pid,
        code, app->launcher_gone ? " (launcher gone, no reply)" : "",
        (LONG)engine_app_count);
    if (!app->launcher_gone)
    {
        app->request->result = code;
        ReplyMsg(&app->request->msg);
    }
    memset(app, 0, sizeof(*app));
    if (engine_app_count == 0)
        engine_lights_out();
}

/* Runs once per service pass: the per-app exit rules and launcher liveness. */
static void engine_service_apps(void)
{
    int check_launchers = (pump_tick % ENGINE_LIVENESS_TICKS) == 0;
    int i;

    for (i = 0; i < MAX_ENGINE_APPS; ++i)
    {
        struct EngineApp *app = &engine_apps[i];
        int windows;

        if (!app->in_use)
            continue;
        if (check_launchers && !app->launcher_gone &&
            FindTaskByPID(app->launcher_pid) == NULL)
        {
            KPrintF("[ELECTRON-ENGINE] launcher %lu of slot %ld is gone\n",
                app->launcher_pid, app->slot);
            app->launcher_gone = 1;
            engine_request_app_exit(app, RETURN_FAIL);
        }
        windows = shell_slot_window_count(app->slot);
        if (windows > 0)
        {
            app->has_window = 1;
            app->windows_gone = 0;
        }
        if (app->exit_requested)
        {
            if (windows == 0)
                engine_finish_app(app, app->exit_code);
            else if (pump_tick > app->exit_tick + CLOSE_TIMEOUT_TICKS)
            {
                KPrintF("[ELECTRON-ENGINE] slot %ld: %ld window(s) still "
                    "open %ld ticks after exit, finishing anyway\n",
                    app->slot, (LONG)windows, (LONG)CLOSE_TIMEOUT_TICKS);
                engine_finish_app(app, app->exit_code);
            }
            continue;
        }
        if (app->probe_mode)
        {
            if (node_marker_seen && app->first_window_seen &&
                pump_tick > app->first_window_tick + CLOSE_AFTER_PAINT_TICKS)
                engine_request_app_exit(app, RETURN_OK);
            else if (pump_tick > app->start_tick + CREATE_TIMEOUT_TICKS)
                engine_request_app_exit(app, RETURN_FAIL);
            continue;
        }
        if (app->has_window && windows == 0)
        {
            if (!app->windows_gone)
            {
                app->windows_gone = 1;
                app->windows_gone_tick = pump_tick;
            }
            else if (pump_tick > app->windows_gone_tick +
                APP_CLOSE_GRACE_TICKS)
            {
                KPrintF("[ELECTRON-ENGINE] slot %ld quit without app.exit "
                    "(last window closed %ld ticks ago)\n", app->slot,
                    (LONG)(pump_tick - app->windows_gone_tick));
                engine_finish_app(app, RETURN_OK);
            }
        }
    }
}

/* Create the CEF browser for a window whose script asked for one. */
static void shell_create_window(struct ShellWindow *window,
    cef_window_info_t *window_info, cef_browser_settings_t *browser_settings,
    cef_char_t *default_url, size_t default_url_length)
{
    cef_string_t url;
    LONG x, y, width, height;
    int has_position;
    LONG rc;

    /* Everything queued before creation shapes the window CEF opens. */
    shell_resolve_bounds(window, NULL, &x, &y, &width, &height,
        &has_position);
    window_info->bounds.x = 0;
    window_info->bounds.y = 0;
    if (has_position)
    {
        window_info->bounds.x = x;
        window_info->bounds.y = y;
    }
    else if (!window->geometry.maximized && !window->geometry.fullscreen)
    {
        /* No position asked for: every such window lands at the work
           area's origin, so a second app's window sits exactly under the
           first's and the screen shows one window where two are open
           (hosted user-22, 2026-09-16). Cascade by the windows already
           open instead; each app still gets whatever it asked for. */
        LONG area_x, area_y, area_width, area_height;
        LONG open = 0;
        int i;

        for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
            if (shell_windows[i].in_use && &shell_windows[i] != window &&
                (shell_windows[i].browser != NULL ||
                    shell_windows[i].creating))
                ++open;
        if (open > 0)
        {
            shell_work_area(NULL, 0, &area_x, &area_y, &area_width,
                &area_height);
            window_info->bounds.x = clamp_long(area_x + open * 32, area_x,
                area_x + area_width - width);
            window_info->bounds.y = clamp_long(area_y + open * 32, area_y,
                area_y + area_height - height);
        }
    }
    window_info->bounds.width = width;
    window_info->bounds.height = height;
    window->geometry.bounds_dirty = 0;
    KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu initial bounds "
        "%ld,%ld %ldx%ld (maximized=%ld)\n", window->slot, window->id,
        (LONG)window_info->bounds.x, (LONG)window_info->bounds.y, width,
        height, (LONG)window->geometry.maximized);
    memset(&url, 0, sizeof(url));
    if (window->url_dirty)
    {
        set_static_cef_string(&url, window->url, window->url_length);
        window->url_dirty = 0;
    }
    else
        set_static_cef_string(&url, default_url, default_url_length);
    window->creating = 1;
    window->create_order = ++shell_create_counter;
    rc = ElectronCreateCEFBrowser(window_info, &client, &url,
        browser_settings, NULL, NULL);
    KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu create rc=%ld\n",
        window->slot, window->id, rc);
    if (rc == 0)
    {
        window->creating = 0;
        ElectronBindWindowBrowser(window->slot, window->id, NULL);
        window->in_use = 0;
    }
}

/* A load-url after creation navigates the live browser. */
static void shell_navigate_window(struct ShellWindow *window)
{
    cef_frame_t *frame;
    cef_string_t url;

    if (window->browser == NULL || !window->url_dirty)
        return;
    window->url_dirty = 0;
    frame = window->browser->get_main_frame(window->browser);
    if (frame == NULL)
        return;
    memset(&url, 0, sizeof(url));
    set_static_cef_string(&url, window->url, window->url_length);
    frame->load_url(frame, &url);
    frame->base.release(&frame->base);
}

static void service_native_window_requests(cef_window_info_t *window_info,
    cef_browser_settings_t *browser_settings)
{
    char method[64];
    char payload[2048];
    LONG slot;
    LONG rc;

    while ((rc = ElectronPollAppRequest(method, sizeof(method),
        payload, sizeof(payload), &slot)) > 0)
    {
        struct ShellWindow *window = NULL;
        struct EngineApp *app = engine_find_app(slot);

        KPrintF("[ELECTRON-CONTEXT] native request slot=%ld method='%s' "
            "payload='%s'\n", slot, method, payload);
        if (app == NULL)
        {
            KPrintF("[ELECTRON-ENGINE] dropped request of finished slot "
                "%ld\n", slot);
            continue;
        }
        if (strncmp(method, "browser-window.", 15) == 0)
        {
            /* Every browser-window request names its window first. */
            ULONG id = (ULONG)strtoul(payload, NULL, 10);

            window = strcmp(method, "browser-window.close") == 0 ?
                shell_find_window(slot, id) :
                shell_find_or_create_window(slot, id);
            if (window == NULL)
                continue;
        }
        if (strcmp(method, "browser-window.create") == 0)
        {
            /* Created below, once this pass has drained the ring: the
               BrowserWindow constructor queues create, set-bounds and
               load-url in one JS turn, and creating on the first of them
               opened every window at the 800x600 default (hosted user-23,
               2026-09-16) where the single-window shell honoured the
               requested size. */
            if (window->browser == NULL && !window->creating)
                window->create_requested = 1;
        }
        else if (strcmp(method, "browser-window.load-url") == 0)
        {
            const char *requested_url = strchr(payload, '\t');

            if (requested_url != NULL && requested_url[1] != '\0')
                shell_set_window_url(window, requested_url + 1);
            if (window->browser == NULL && !window->creating)
                window->create_requested = 1;
            else
                shell_navigate_window(window);
        }
        else if (window != NULL &&
            handle_window_geometry_request(window, method, payload))
            ;
        else if (strcmp(method, "browser-window.close") == 0)
            shell_request_window_close(window);
        else if (strcmp(method, "app.exit") == 0)
        {
            /* app.exit ends the app that asked: its windows close, then
               engine_service_apps() finishes it and replies to its
               launcher. */
            LONG code = strtol(payload, NULL, 10);

            KPrintF("[ELECTRON-CONTEXT] app.exit requested by slot %ld "
                "code=%ld\n", slot, code);
            engine_request_app_exit(app, code);
        }
    }
    engine_service_apps();

    {
        int i;

        for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        {
            struct ShellWindow *window = &shell_windows[i];

            if (window->in_use && window->create_requested)
            {
                struct EngineApp *owner = engine_find_app(window->slot);

                window->create_requested = 0;
                if (owner != NULL && window->browser == NULL &&
                    !window->creating)
                    shell_create_window(window, window_info,
                        browser_settings, owner->default_url,
                        owner->default_url_length);
            }
        }
    }
    shell_sync_all_window_geometry();
}

/* Each app's Node runs in its own slot, in its launcher's current
   directory. */
static void engine_pump_apps(void)
{
    int probe_apps = 0;
    int i;

    for (i = 0; i < MAX_ENGINE_APPS; ++i)
    {
        struct EngineApp *app = &engine_apps[i];
        BPTR old_dir;

        if (!app->in_use)
            continue;
        probe_apps |= app->probe_mode;
        old_dir = CurrentDir(app->current_dir);
        if (ElectronSelectAppRuntime(app->slot) == 0)
        {
            ElectronPumpNodeEmbedder();
            ElectronSelectAppRuntime(0);
        }
        CurrentDir(old_dir);
    }
    if (probe_apps && !node_marker_seen && marker_exists())
        node_marker_seen = 1;
}
#endif

static int CEF_CALLBACK execute_node_pump(cef_v8handler_t *self,
    const cef_string_t *name, cef_v8value_t *object, size_t arguments_count,
    cef_v8value_t *const *arguments, cef_v8value_t **retval,
    cef_string_t *exception)
{
    (void)self;
    (void)name;
    (void)object;
    (void)arguments_count;
    (void)arguments;
    (void)retval;
    (void)exception;

    pump_node_on_context_task();
    if (node_detached && !completion_queued && ui_pump_port != NULL)
    {
        completion_queued = 1;
        PutMsg(ui_pump_port, &ui_pump_message);
        KPrintF("[ELECTRON-CONTEXT] renderer queued UI completion\n");
    }
    return 1;
}

static void CEF_CALLBACK on_web_kit_initialized(
    cef_render_process_handler_t *self)
{
    cef_string_t name;
    cef_string_t code;
    LONG rc;

    (void)self;
    memset(&name, 0, sizeof(name));
    memset(&code, 0, sizeof(code));
    set_static_cef_string(&name, extension_name,
        (sizeof(extension_name) / sizeof(extension_name[0])) - 1);
    set_static_cef_string(&code, extension_code,
        (sizeof(extension_code) / sizeof(extension_code[0])) - 1);
    rc = ElectronRegisterCEFExtension(&name, &code, &node_pump_handler);
    KPrintF("[ELECTRON-CONTEXT] register renderer extension rc=%ld task=%p\n",
        rc, FindTask(NULL));
#ifdef ELECTRON_SHELL_WINDOWED
    /*
     * The BrowserWindow renderer talks back to the Electron main script
     * through electron.library's own native extension (ipcRenderer.send /
     * invoke).  Extensions can only be registered here, before any V8
     * context exists.
     */
    rc = ElectronRegisterRendererExtension();
    KPrintF("[ELECTRON-CONTEXT] register electron renderer seam rc=%ld\n",
        rc);
#endif
}

#ifdef ELECTRON_SHELL_WINDOWED
/*
 * Electron's own switch contract for custom schemes: the main script cannot
 * run before CefInitialize, but protocol.registerSchemesAsPrivileged must be
 * honoured at CefInitialize time.  The launcher therefore passes the scheme
 * lists as switches (--standard-schemes=a,b --secure-schemes=... etc.), the
 * same way Electron forwards them to its renderer processes.
 */
#define ELECTRON_SHELL_MAX_SCHEMES 8
#define ELECTRON_SHELL_MAX_SCHEME_NAME 32

static struct
{
    char name[ELECTRON_SHELL_MAX_SCHEME_NAME];
    cef_char_t name16[ELECTRON_SHELL_MAX_SCHEME_NAME];
    int options;
} declared_schemes[ELECTRON_SHELL_MAX_SCHEMES];
static int declared_scheme_count;

static void declare_scheme_list(const char *list, int options)
{
    while (list != NULL && *list != '\0')
    {
        const char *end = strchr(list, ',');
        size_t length = end != NULL ? (size_t)(end - list) : strlen(list);
        int slot;

        if (length == 0 || length >= ELECTRON_SHELL_MAX_SCHEME_NAME)
        {
            if (end == NULL)
                break;
            list = end + 1;
            continue;
        }
        for (slot = 0; slot < declared_scheme_count; ++slot)
        {
            if (strlen(declared_schemes[slot].name) == length &&
                strncmp(declared_schemes[slot].name, list, length) == 0)
                break;
        }
        if (slot == declared_scheme_count)
        {
            size_t i;

            if (declared_scheme_count >= ELECTRON_SHELL_MAX_SCHEMES)
            {
                KPrintF("[ELECTRON-CONTEXT] too many custom schemes, "
                    "dropping '%s'\n", list);
                break;
            }
            memcpy(declared_schemes[slot].name, list, length);
            declared_schemes[slot].name[length] = '\0';
            for (i = 0; i < length; ++i)
                declared_schemes[slot].name16[i] =
                    (cef_char_t)(unsigned char)list[i];
            declared_schemes[slot].name16[length] = 0;
            declared_schemes[slot].options = 0;
            declared_scheme_count++;
        }
        declared_schemes[slot].options |= options;
        if (end == NULL)
            break;
        list = end + 1;
    }
}

/* The option bit a scheme switch grants, with its list; -1: not one. */
static int scheme_switch_options(const char *arg, const char **list)
{
    static const struct
    {
        const char *prefix;
        int options;
    } scheme_switches[] = {
        { "--standard-schemes=", CEF_SCHEME_OPTION_STANDARD },
        { "--secure-schemes=", CEF_SCHEME_OPTION_SECURE },
        { "--cors-schemes=", CEF_SCHEME_OPTION_CORS_ENABLED },
        { "--fetch-schemes=", CEF_SCHEME_OPTION_FETCH_ENABLED },
        /* Electron's allowServiceWorkers privilege (AROS CEF extension). */
        { "--service-worker-schemes=", CEF_SCHEME_OPTION_SERVICE_WORKER },
        /* Accepted for command-line compatibility; no CEF option bit. */
        { "--code-cache-schemes=", CEF_SCHEME_OPTION_NONE },
        { "--streaming-schemes=", CEF_SCHEME_OPTION_NONE },
    };
    size_t i;

    for (i = 0; i < sizeof(scheme_switches) / sizeof(scheme_switches[0]);
        ++i)
    {
        size_t prefix_length = strlen(scheme_switches[i].prefix);

        if (strncmp(arg, scheme_switches[i].prefix, prefix_length) == 0)
        {
            *list = arg + prefix_length;
            return scheme_switches[i].options;
        }
    }
    return -1;
}

static int parse_scheme_switch(const char *arg)
{
    const char *list;
    int options = scheme_switch_options(arg, &list);

    if (options < 0)
        return 0;
    declare_scheme_list(list, options);
    return 1;
}

/* The first scheme of a switch's list the engine did not declare with that
   option, copied into missing; 0 when all are. */
static int scheme_switch_missing(const char *arg, char *missing,
    size_t size)
{
    const char *list;
    int options = scheme_switch_options(arg, &list);

    if (options <= 0)
        return 0;
    while (*list != '\0')
    {
        const char *end = strchr(list, ',');
        size_t length = end != NULL ? (size_t)(end - list) : strlen(list);
        int slot;

        for (slot = 0; slot < declared_scheme_count; ++slot)
            if (strlen(declared_schemes[slot].name) == length &&
                strncmp(declared_schemes[slot].name, list, length) == 0 &&
                (declared_schemes[slot].options & options) == options)
                break;
        if (length > 0 && slot == declared_scheme_count)
        {
            snprintf(missing, size, "%.*s", (int)length, list);
            return 1;
        }
        if (end == NULL)
            break;
        list = end + 1;
    }
    return 0;
}

static void CEF_CALLBACK on_register_custom_schemes(cef_app_t *self,
    cef_scheme_registrar_t *registrar)
{
    int slot;

    (void)self;
    for (slot = 0; slot < declared_scheme_count; ++slot)
    {
        cef_string_t name;
        int added = 0;
        LONG rc;

        memset(&name, 0, sizeof(name));
        set_static_cef_string(&name, declared_schemes[slot].name16,
            strlen(declared_schemes[slot].name));
        if (registrar != NULL && registrar->add_custom_scheme != NULL)
            added = registrar->add_custom_scheme(registrar, &name,
                declared_schemes[slot].options);
        rc = ElectronDeclareCustomScheme(declared_schemes[slot].name,
            (ULONG)declared_schemes[slot].options);
        KPrintF("[ELECTRON-CONTEXT] custom scheme '%s' options=0x%lx "
            "added=%ld declared rc=%ld\n", declared_schemes[slot].name,
            (ULONG)declared_schemes[slot].options, (LONG)added, rc);
    }
}
#endif

static cef_render_process_handler_t *CEF_CALLBACK
get_render_process_handler(cef_app_t *self)
{
    (void)self;
    return &render_process_handler;
}

static int CEF_CALLBACK on_console_message(
    cef_display_handler_t *self,
    cef_browser_t *cef_browser,
    cef_log_severity_t level,
    const cef_string_t *message,
    const cef_string_t *source,
    int line)
{
    (void)self;
    (void)cef_browser;
    KPrintF("[ELECTRON-RENDERER] console level=%ld line=%ld\n",
        (LONG)level, (LONG)line);
    log_cef_string("renderer message", message);
    log_cef_string("renderer source", source);
    return 0;
}

static cef_display_handler_t *CEF_CALLBACK get_display_handler(
    cef_client_t *self)
{
    (void)self;
    return &display_handler;
}

static cef_life_span_handler_t *CEF_CALLBACK get_life_span_handler(
    cef_client_t *self)
{
    (void)self;
    return &life_span_handler;
}

static cef_load_handler_t *CEF_CALLBACK get_load_handler(cef_client_t *self)
{
    (void)self;
    return &load_handler;
}

static cef_render_handler_t *CEF_CALLBACK get_render_handler(
    cef_client_t *self)
{
    (void)self;
    return &render_handler;
}

static void CEF_CALLBACK on_loading_state_change(
    cef_load_handler_t *self, cef_browser_t *cef_browser, int is_loading,
    int can_go_back, int can_go_forward)
{
    (void)self;
    (void)cef_browser;
    KPrintF("[ELECTRON-CONTEXT] loading state loading=%ld back=%ld "
        "forward=%ld\n", (LONG)is_loading, (LONG)can_go_back,
        (LONG)can_go_forward);
}

static void CEF_CALLBACK on_load_start(cef_load_handler_t *self,
    cef_browser_t *cef_browser, cef_frame_t *frame,
    cef_transition_type_t transition_type)
{
    int is_main = frame != NULL && frame->is_main != NULL
        ? frame->is_main(frame) : 0;

    (void)self;
    (void)cef_browser;
    KPrintF("[ELECTRON-CONTEXT] load start main=%ld transition=%lu\n",
        (LONG)is_main, (ULONG)transition_type);
}

static void CEF_CALLBACK on_load_end(cef_load_handler_t *self,
    cef_browser_t *cef_browser, cef_frame_t *frame, int http_status_code)
{
    int is_main = frame != NULL && frame->is_main != NULL
        ? frame->is_main(frame) : 0;

    (void)self;
    (void)cef_browser;
    KPrintF("[ELECTRON-CONTEXT] load end main=%ld HTTP=%ld\n",
        (LONG)is_main, (LONG)http_status_code);

    /* Tell the Electron object model the page finished loading.
     *
     * Electron apps routinely open their window with show:false and only
     * show() it once the content is up, so a did-finish-load that never
     * arrives leaves a fully working window permanently invisible: GitHub
     * Desktop creates its BrowserWindow, loads its bundle, waits for this
     * event plus a renderer-ready IPC, and shows nothing (probe: windows=1,
     * no visible window, 2026-09-16). Only the main frame counts - a
     * sub-frame finishing does not mean the document is ready. */
    if (is_main)
    {
#ifdef ELECTRON_SHELL_WINDOWED
        struct ShellWindow *window = shell_find_window_by_browser(cef_browser);

        if (window != NULL)
        {
            ElectronPostAppWindowEvent(window->slot, window->id,
                "did-finish-load", "");
            ElectronPostAppWindowEvent(window->slot, window->id,
                "did-stop-loading", "");
            return;
        }
#endif
        ElectronPostWindowEvent("did-finish-load", "");
        ElectronPostWindowEvent("did-stop-loading", "");
    }
}

static void CEF_CALLBACK on_load_error(cef_load_handler_t *self,
    cef_browser_t *cef_browser, cef_frame_t *frame,
    cef_errorcode_t error_code, const cef_string_t *error_text,
    const cef_string_t *failed_url)
{
    int is_main = frame != NULL && frame->is_main != NULL
        ? frame->is_main(frame) : 0;

    (void)self;
    (void)cef_browser;
    KPrintF("[ELECTRON-CONTEXT] load error main=%ld code=%ld\n",
        (LONG)is_main, (LONG)error_code);
    log_cef_string("load error text", error_text);
    log_cef_string("load error URL", failed_url);
}

static void CEF_CALLBACK get_view_rect(cef_render_handler_t *self,
    cef_browser_t *cef_browser, cef_rect_t *rect)
{
    (void)self;
    rect->x = 0;
    rect->y = 0;
    rect->width = VIEW_WIDTH;
    rect->height = VIEW_HEIGHT;
    release_browser_reference(cef_browser);
}

static void CEF_CALLBACK on_paint(cef_render_handler_t *self,
    cef_browser_t *cef_browser, cef_paint_element_type_t type,
    size_t dirty_rect_count, const cef_rect_t *dirty_rects,
    const void *buffer, int width, int height)
{
    (void)self;
    (void)type;
    (void)dirty_rect_count;
    (void)dirty_rects;
    (void)buffer;
    if (!first_paint && width > 0 && height > 0)
    {
        first_paint = 1;
        first_paint_tick = pump_tick;
        KPrintF("[ELECTRON-CONTEXT] first paint=%ldx%ld tick=%lu\n",
            (LONG)width, (LONG)height, first_paint_tick);
    }
    release_browser_reference(cef_browser);
}

static void CEF_CALLBACK on_after_created(cef_life_span_handler_t *self,
    cef_browser_t *created_browser)
{
    (void)self;
    browser = created_browser;
    KPrintF("[ELECTRON-CONTEXT] browser created=%p\n", browser);
#ifdef ELECTRON_SHELL_WINDOWED
    {
        /* The oldest window still waiting for its browser is the one this
           is; the callback's reference becomes the record's. */
        struct ShellWindow *window = NULL;
        int i;

        for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
            if (shell_windows[i].in_use && shell_windows[i].creating &&
                (window == NULL ||
                    shell_windows[i].create_order < window->create_order))
                window = &shell_windows[i];
        if (window != NULL)
        {
            window->creating = 0;
            window->browser = created_browser;
            window->browser_identifier =
                created_browser->get_identifier(created_browser);
            ElectronBindWindowBrowser(window->slot, window->id,
                created_browser);
            {
                struct EngineApp *owner = engine_find_app(window->slot);

                if (owner != NULL && !owner->first_window_seen)
                {
                    owner->first_window_seen = 1;
                    owner->first_window_tick = pump_tick;
                }
            }
            KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu bound to "
                "browser %p identifier=%ld\n", window->slot, window->id,
                created_browser, (LONG)window->browser_identifier);
            if (window->close_requested)
            {
                window->close_requested = 0;
                shell_request_window_close(window);
            }
        }
        else
        {
            KPrintF("[ELECTRON-CONTEXT] browser %p belongs to no "
                "BrowserWindow record\n", created_browser);
            ElectronSetActiveBrowser(created_browser);
        }
        browser_closed = 0;
        first_paint = 1;
        first_paint_tick = pump_tick;
        KPrintF("[ELECTRON-CONTEXT] visible browser ready tick=%lu\n",
            first_paint_tick);
        /* Title/limits/activation queued before creation apply to the
           window now that it exists. */
        if (window != NULL)
            shell_sync_window_geometry(window);
    }
#endif
}

static void CEF_CALLBACK on_before_close(cef_life_span_handler_t *self,
    cef_browser_t *closing_browser)
{
#ifdef ELECTRON_SHELL_WINDOWED
    struct ShellWindow *window = shell_find_window_by_browser(closing_browser);
    LONG slot = 0;
    ULONG id = 0;
    int windows_left;

    (void)self;
    if (window != NULL)
    {
        slot = window->slot;
        id = window->id;
        ElectronBindWindowBrowser(slot, id, NULL);
        release_browser_reference(window->browser);
        if (browser == window->browser)
            browser = NULL;
        window->browser = NULL;
        window->in_use = 0;
    }
    else
    {
        ElectronSetActiveBrowser(NULL);
        release_browser_reference(browser);
        browser = NULL;
    }
    release_browser_reference(closing_browser);
    windows_left = shell_live_window_count();
    KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu closed, %ld left\n",
        slot, id, (LONG)windows_left);
    if (windows_left == 0)
    {
        browser_closed = 1;
        browser_closed_tick = pump_tick;
    }
    /* The script's BrowserWindow learns it is gone from here ('closed',
       then 'window-all-closed' on the app once no window is left). */
    if (slot > 0)
        ElectronPostAppWindowEvent(slot, id, "closed", "");
#else
    (void)self;
    release_browser_reference(closing_browser);
    release_browser_reference(browser);
    browser = NULL;
    browser_closed = 1;
    browser_closed_tick = pump_tick;
    KPrintF("[ELECTRON-CONTEXT] browser closed\n");
#endif
}

static void CEF_CALLBACK on_context_created(
    cef_render_process_handler_t *self,
    cef_browser_t *cef_browser,
    cef_frame_t *frame,
    cef_v8context_t *context)
{
    LONG rc;

#ifdef ELECTRON_SHELL_WINDOWED
    (void)self;
    /*
     * Run the BrowserWindow preload (require('electron'), process shim,
     * contextBridge) in the freshly created main-frame context, exactly
     * where Electron's sandboxed preload would run.  Subframes are ignored
     * inside the library.
     */
    rc = ElectronInjectRendererContext(cef_browser, frame, context);
    KPrintF("[ELECTRON-CONTEXT] renderer context created browser=%p "
        "frame=%p inject rc=%ld\n", cef_browser, frame, rc);
    return;
#else
    (void)self;
    (void)cef_browser;
    (void)frame;
    (void)context;

    if (node_attached)
        return;
    KPrintF("[ELECTRON-CONTEXT] attach current V8 context begin\n");
    rc = ElectronAttachNodeIsolatedV8ContextWithModule(node_script);
    KPrintF("[ELECTRON-CONTEXT] attach current V8 context end rc=%ld\n", rc);
    if (rc == 0)
    {
        cef_string_t code;
        cef_string_t script_url;
        cef_v8value_t *retval = NULL;
        cef_v8exception_t *exception = NULL;
        int eval_rc = 0;

        node_attached = 1;
        node_context_task = FindTask(NULL);
        node_context = context;
        if (node_context->base.add_ref != NULL)
            node_context->base.add_ref(&node_context->base);
        KPrintF("[ELECTRON-CONTEXT] context task=%p\n", node_context_task);
        memset(&code, 0, sizeof(code));
        memset(&script_url, 0, sizeof(script_url));
        set_static_cef_string(&code, timer_code,
            (sizeof(timer_code) / sizeof(timer_code[0])) - 1);
        set_static_cef_string(&script_url, timer_script_url,
            (sizeof(timer_script_url) / sizeof(timer_script_url[0])) - 1);
        if (node_context->eval != NULL)
            eval_rc = node_context->eval(node_context, &code, &script_url, 1,
                &retval, &exception);
        KPrintF("[ELECTRON-CONTEXT] start renderer timer rc=%ld "
            "exception=%p\n", (LONG)eval_rc, exception);
        if (retval != NULL && retval->base.release != NULL)
            retval->base.release(&retval->base);
        if (exception != NULL && exception->base.release != NULL)
            exception->base.release(&exception->base);
    }
#endif
}

static void CEF_CALLBACK on_context_released(
    cef_render_process_handler_t *self,
    cef_browser_t *cef_browser,
    cef_frame_t *frame,
    cef_v8context_t *context)
{
    (void)self;
    (void)cef_browser;
    (void)frame;
    (void)context;

#ifdef ELECTRON_SHELL_WINDOWED
    /*
     * BrowserWindow mode owns Node on the browser/UI task. Renderer and
     * subframe V8 contexts may come and go for the lifetime of that main
     * environment, so their release must not tear it down from a renderer
     * callback. The browser task detaches Node after the window closes.
     */
    return;
#endif

    if (!node_attached || node_detached)
        return;
    KPrintF("[ELECTRON-CONTEXT] detach current V8 context begin\n");
    ElectronDetachNodeEmbedder();
    node_detached = 1;
    release_node_context();
    KPrintF("[ELECTRON-CONTEXT] detach current V8 context end\n");
}

static void initialize_handlers(void)
{
    memset(&app, 0, sizeof(app));
    memset(&client, 0, sizeof(client));
    memset(&display_handler, 0, sizeof(display_handler));
    memset(&life_span_handler, 0, sizeof(life_span_handler));
    memset(&load_handler, 0, sizeof(load_handler));
    memset(&render_handler, 0, sizeof(render_handler));
    memset(&render_process_handler, 0, sizeof(render_process_handler));
    memset(&node_pump_handler, 0, sizeof(node_pump_handler));

    initialize_base(&app.base, sizeof(app));
    app.get_render_process_handler = get_render_process_handler;
#ifdef ELECTRON_SHELL_WINDOWED
    app.on_register_custom_schemes = on_register_custom_schemes;
#endif

    initialize_base(&client.base, sizeof(client));
    client.get_display_handler = get_display_handler;
    client.get_life_span_handler = get_life_span_handler;
    client.get_load_handler = get_load_handler;
#ifndef ELECTRON_SHELL_WINDOWED
    client.get_render_handler = get_render_handler;
#endif

    initialize_base(&display_handler.base, sizeof(display_handler));
    display_handler.on_console_message = on_console_message;

    initialize_base(&life_span_handler.base, sizeof(life_span_handler));
    life_span_handler.on_after_created = on_after_created;
    life_span_handler.on_before_close = on_before_close;

    initialize_base(&load_handler.base, sizeof(load_handler));
    load_handler.on_loading_state_change = on_loading_state_change;
    load_handler.on_load_start = on_load_start;
    load_handler.on_load_end = on_load_end;
    load_handler.on_load_error = on_load_error;

    initialize_base(&render_handler.base, sizeof(render_handler));
    render_handler.get_view_rect = get_view_rect;
    render_handler.on_paint = on_paint;

    initialize_base(&render_process_handler.base,
        sizeof(render_process_handler));
    render_process_handler.on_web_kit_initialized =
        on_web_kit_initialized;
    render_process_handler.on_context_created = on_context_created;
    render_process_handler.on_context_released = on_context_released;

    initialize_base(&node_pump_handler.base, sizeof(node_pump_handler));
    node_pump_handler.execute = execute_node_pump;
}

static int ensure_profile_directory(CONST_STRPTR path)
{
    BPTR lock = Lock(path, ACCESS_READ);

    if (lock == BNULL)
        lock = CreateDir(path);
    if (lock == BNULL)
    {
        KPrintF("[ELECTRON-CONTEXT] unable to create profile directory "
            "'%s' IoErr=%ld\n", path, IoErr());
        return 0;
    }

    UnLock(lock);
    KPrintF("[ELECTRON-CONTEXT] profile directory ready '%s'\n", path);
    return 1;
}

static int real_main(int argc, char **argv);

/*
 * Chromium's browser main, Node's bootstrap and V8's parser/compiler all
 * recurse on the calling task's stack, and a Shell hands a command
 * AROS_STACKSIZE (40 KiB) of it.  On pc-x86_64 the overrun landed in the
 * hunk allocated just below the stack - ElectronShell's own shell_windows[]
 * (.lbss) - and the next window service pass called through the V8 frames
 * it found there ("Illegal instruction at 0x10003", fix16, 2026-09-17).
 * Run everything on a stack of our own unless the caller already gave us a
 * big one, the way CrashReporter does.
 */
#define SHELL_STACK_MIN  (8 * 1024 * 1024)
#define SHELL_STACK_SIZE (16 * 1024 * 1024)

struct shell_main_args { int argc; char **argv; };

static IPTR shell_stack_entry(struct shell_main_args *a)
{
    return (IPTR)real_main(a->argc, a->argv);
}

int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    IPTR have = (IPTR)me->tc_SPUpper - (IPTR)me->tc_SPLower;
    struct StackSwapStruct sss;
    struct StackSwapArgs args;
    struct shell_main_args a = { argc, argv };
    APTR stack;
    int rc;

    if (have >= SHELL_STACK_MIN)
        return real_main(argc, argv);

    stack = AllocMem(SHELL_STACK_SIZE, MEMF_ANY);
    if (stack == NULL)
    {
        KPrintF("[ELECTRON-CONTEXT] no %lu KiB stack, running on the "
            "caller's %lu KiB\n", (ULONG)(SHELL_STACK_SIZE / 1024),
            (ULONG)(have / 1024));
        return real_main(argc, argv);
    }

    args.Args[0] = (IPTR)&a;
    sss.stk_Lower   = stack;
    sss.stk_Upper   = (APTR)((IPTR)stack + SHELL_STACK_SIZE);
    sss.stk_Pointer = sss.stk_Upper;
    rc = (int)NewStackSwap(&sss, (APTR)shell_stack_entry, &args);
    FreeMem(stack, SHELL_STACK_SIZE);
    return rc;
}

static void fill_cef_settings(cef_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->size = sizeof(*settings);
    settings->no_sandbox = 1;
#ifndef ELECTRON_SHELL_WINDOWED
    settings->windowless_rendering_enabled = 1;
#endif
    set_static_cef_string(&settings->root_cache_path, root_cache_path,
        (sizeof(root_cache_path) / sizeof(root_cache_path[0])) - 1);
    set_static_cef_string(&settings->cache_path, cache_path,
        (sizeof(cache_path) / sizeof(cache_path[0])) - 1);
    set_static_cef_string(&settings->resources_dir_path, resources_path,
        (sizeof(resources_path) / sizeof(resources_path[0])) - 1);
    set_static_cef_string(&settings->locales_dir_path, locales_path,
        (sizeof(locales_path) / sizeof(locales_path[0])) - 1);
}

#ifdef ELECTRON_SHELL_WINDOWED
/*
 * electron.library is opened by the engine host and CEF children only, never
 * by a launcher.  The libraries it opens on first use (cef-blink, node, v8,
 * the C runtime they link) bind the opening task's streams and per-task
 * bases for the rest of the boot, so the first opener has to be the engine,
 * not a launcher that exits after its app.
 */
struct Library *ElectronBase;

static int open_electron(void)
{
    ElectronBase = OpenLibrary(ELECTRONNAME, ELECTRONVERSION);
    if (ElectronBase == NULL)
        KPrintF("[ELECTRON-CONTEXT] cannot open " ELECTRONNAME "\n");
    return ElectronBase != NULL;
}

struct ScriptBuffer
{
    char *text;
    size_t length;
    size_t capacity;
    int overflow;
};

static void script_append(struct ScriptBuffer *buffer, const char *text,
    size_t length)
{
    if (buffer->overflow || buffer->length + length + 1 > buffer->capacity)
    {
        buffer->overflow = 1;
        return;
    }
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
}

static void script_append_text(struct ScriptBuffer *buffer, const char *text)
{
    script_append(buffer, text, strlen(text));
}

/* A JavaScript string literal; AROS text is Latin-1, so bytes above 0x7f
   are escaped as the code points they are. */
static void script_append_quoted(struct ScriptBuffer *buffer,
    const char *text)
{
    size_t i;

    script_append(buffer, "'", 1);
    for (i = 0; text[i] != '\0'; ++i)
    {
        unsigned char c = (unsigned char)text[i];
        char escape[8];

        if (c == '\'' || c == '\\')
        {
            escape[0] = '\\';
            escape[1] = (char)c;
            script_append(buffer, escape, 2);
        }
        else if (c < 0x20 || c >= 0x7f)
        {
            snprintf(escape, sizeof(escape), "\\x%02x", c);
            script_append(buffer, escape, 4);
        }
        else
            script_append(buffer, &text[i], 1);
    }
    script_append(buffer, "'", 1);
}

/*
 * The app's Node wrapper.  Electron hands the whole command line to the main
 * script as process.argv, with the app directory first: VS Code's dev-mode
 * argv parser (parseMainProcessArgv: stripAppPath) removes the first
 * non-option entry.  The launcher's local shell variables become the app's
 * own process.env entries, and its stdout/stderr are the launcher's.
 * ELECTRON_APP_SLOT is the app's index in its launch (0 = main script) so a
 * script serving as several apps of one launch can tell them apart.
 */
static char *engine_build_script(const struct EngineRequest *request)
{
    static const char argv_prefix[] =
        "process.argv=['AROS-Electron','--no-sandbox',"
        "'--disable-gpu','--disable-gpu-compositing'";
    static const char stdio_code[] =
        "(()=>{const b=process._linkedBinding('aros_electron');"
        "const {Writable}=require('stream');"
        "const mk=(n)=>{const w=new Writable({decodeStrings:false,"
        "write(c,e,cb){b.invoke('stdio.write',n+'\\t'+String(c));cb();}});"
        "w.fd=n;w.isTTY=false;return w;};"
        "const o=mk(1),e=mk(2);"
        "Object.defineProperty(process,'stdout',{configurable:true,"
        "enumerable:true,get:()=>o});"
        "Object.defineProperty(process,'stderr',{configurable:true,"
        "enumerable:true,get:()=>e});"
        "try{console._stdout=o;console._stderr=e;}catch(_){}})();";
    static const char middle[] =
        "process.env.VSCODE_DEV='1';"
        "process._rawDebug('[ELECTRON-MODULE] loading main entry "
        MAIN_SCRIPT_OPTION "');"
        "const __arosElectronMain=";
    /*
     * A synchronous throw from the main script never reaches the module
     * bootstrap's uncaughtException hook (it leaves LoadEnvironment directly)
     * and node.library only reports it through NodeBridgeLog(), which is
     * compiled out of a non-debug tree - so it came back as a bare rc=20.
     * Report it through the bootstrap's console.error hook (the [ELECTRON-JS]
     * line in the debug log), then rethrow so the attach fails as before.
     */
    static const char suffix[] =
        ";const __arosRequire=require('module').createRequire("
        "__arosElectronMain);"
        "globalThis.__arosElectronMainModuleRequest=__arosElectronMain;"
        "try{__arosRequire(__arosElectronMain);}catch(e){"
        "console.error('[ELECTRON-MODULE] main threw: '+"
        "((e&&e.stack)||e));throw e;}";
    struct ScriptBuffer buffer;
    const char *cursor;
    char text[160];
    size_t capacity = 16384 + sizeof(node_script);
    int arg_index;

    if (request->env != NULL)
    {
        for (cursor = request->env; *cursor != '\0';
            cursor += strlen(cursor) + 1)
            capacity += 4 * strlen(cursor) + 32;
    }
    for (arg_index = 1; arg_index < request->argc; ++arg_index)
        if (request->argv[arg_index] != NULL)
            capacity += 4 * strlen(request->argv[arg_index]) + 4;
    if (request->main_script != NULL)
        capacity += 8 * strlen(request->main_script);

    memset(&buffer, 0, sizeof(buffer));
    buffer.capacity = capacity;
    buffer.text = AllocVec(capacity, MEMF_ANY);
    if (buffer.text == NULL)
        return NULL;
    buffer.text[0] = '\0';

    if (request->main_script != NULL)
    {
        const char *path = request->main_script;
        size_t app_length = strlen(path);
        char app_path[512];

        while (app_length > 0 && path[app_length - 1] != '/' &&
            path[app_length - 1] != ':')
            --app_length;
        if (app_length == 0 || app_length >= sizeof(app_path))
        {
            FreeVec(buffer.text);
            return NULL;
        }
        memcpy(app_path, path, app_length);
        app_path[app_length] = '\0';
        script_append_text(&buffer, argv_prefix);
        script_append_text(&buffer, ",");
        script_append_quoted(&buffer, app_path);
        for (arg_index = 1; arg_index < request->argc; ++arg_index)
        {
            const char *argument = request->argv[arg_index];

            if (argument == NULL || argument[0] == '\0' ||
                strncmp(argument, MAIN_SCRIPT_OPTION,
                    sizeof(MAIN_SCRIPT_OPTION) - 1) == 0 ||
                strncmp(argument, APP_SCRIPT_OPTION,
                    sizeof(APP_SCRIPT_OPTION) - 1) == 0 ||
                (argument[0] == '-' && argument[1] == '-' &&
                    strstr(argument, "-schemes=") != NULL))
                continue;
            script_append_text(&buffer, ",");
            script_append_quoted(&buffer, argument);
        }
        script_append_text(&buffer, "];");
    }
    snprintf(text, sizeof(text), "process.env.ELECTRON_APP_SLOT='%ld';",
        (long)request->app_index);
    script_append_text(&buffer, text);
    if (request->env != NULL)
    {
        for (cursor = request->env; *cursor != '\0'; )
        {
            const char *name = cursor;
            const char *value = name + strlen(name) + 1;

            cursor = value + strlen(value) + 1;
            script_append_text(&buffer, "process.env[");
            script_append_quoted(&buffer, name);
            script_append_text(&buffer, "]=");
            script_append_quoted(&buffer, value);
            script_append_text(&buffer, ";");
        }
    }
    if (request->output != BNULL || request->error != BNULL)
        script_append_text(&buffer, stdio_code);
    if (request->main_script != NULL)
    {
        script_append_text(&buffer, middle);
        script_append_quoted(&buffer, request->main_script);
        script_append_text(&buffer, suffix);
    }
    else
        script_append_text(&buffer, (const char *)node_script);

    if (buffer.overflow)
    {
        FreeVec(buffer.text);
        return NULL;
    }
    return buffer.text;
}

/*
 * The engine's CEF switches are fixed when it starts, but it serves every
 * app of the boot.  So it takes the union of its launcher's switches and of
 * the registry: each installed app leaves the switches it needs from the
 * engine in ENVARC:Electron/Engine/<app>, one per line.
 */
#define ENGINE_REGISTRY_DIR "ENV:Electron/Engine"
#define MAX_ENGINE_SWITCHES 48
#define ENABLE_FEATURES_OPTION "--enable-features="
#define DISABLE_FEATURES_OPTION "--disable-features="

static char *engine_switches[MAX_ENGINE_SWITCHES];
static int engine_switch_count;
static char *engine_features[2];

static char *engine_strdup(const char *text, size_t length)
{
    char *copy = AllocVec(length + 1, MEMF_ANY);

    if (copy != NULL)
    {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

static int feature_listed(const char *list, const char *name, size_t length)
{
    while (list != NULL && *list != '\0')
    {
        const char *end = strchr(list, ',');
        size_t item = end != NULL ? (size_t)(end - list) : strlen(list);

        if (item == length && strncmp(list, name, length) == 0)
            return 1;
        if (end == NULL)
            break;
        list = end + 1;
    }
    return 0;
}

static void engine_merge_features(int which, const char *list)
{
    while (*list != '\0')
    {
        const char *end = strchr(list, ',');
        size_t length = end != NULL ? (size_t)(end - list) : strlen(list);

        if (length > 0 && !feature_listed(engine_features[which], list,
            length))
        {
            size_t old = engine_features[which] != NULL ?
                strlen(engine_features[which]) : 0;
            char *merged = AllocVec(old + length + 2, MEMF_ANY);

            if (merged == NULL)
                return;
            if (old > 0)
            {
                memcpy(merged, engine_features[which], old);
                merged[old++] = ',';
                FreeVec(engine_features[which]);
            }
            memcpy(merged + old, list, length);
            merged[old + length] = '\0';
            engine_features[which] = merged;
        }
        if (end == NULL)
            break;
        list = end + 1;
    }
}

static void engine_add_switch(const char *arg, const char *origin)
{
    int i;

    if (arg[0] != '-' || arg[1] != '-' ||
        strcmp(arg, ENGINE_HOST_OPTION) == 0 ||
        strncmp(arg, MAIN_SCRIPT_OPTION, sizeof(MAIN_SCRIPT_OPTION) - 1) == 0 ||
        strncmp(arg, APP_SCRIPT_OPTION, sizeof(APP_SCRIPT_OPTION) - 1) == 0)
        return;
    KPrintF("[ELECTRON-ENGINE] switch '%s' from %s\n", arg, origin);
    if (parse_scheme_switch(arg))
        return;
    if (strncmp(arg, ENABLE_FEATURES_OPTION,
        sizeof(ENABLE_FEATURES_OPTION) - 1) == 0)
    {
        engine_merge_features(0, arg + sizeof(ENABLE_FEATURES_OPTION) - 1);
        return;
    }
    if (strncmp(arg, DISABLE_FEATURES_OPTION,
        sizeof(DISABLE_FEATURES_OPTION) - 1) == 0)
    {
        engine_merge_features(1, arg + sizeof(DISABLE_FEATURES_OPTION) - 1);
        return;
    }
    for (i = 0; i < engine_switch_count; ++i)
        if (strcmp(engine_switches[i], arg) == 0)
            return;
    if (engine_switch_count >= MAX_ENGINE_SWITCHES)
    {
        KPrintF("[ELECTRON-ENGINE] too many switches, dropping '%s'\n", arg);
        return;
    }
    engine_switches[engine_switch_count] = engine_strdup(arg, strlen(arg));
    if (engine_switches[engine_switch_count] != NULL)
        ++engine_switch_count;
}

static void engine_read_registry_file(const char *name)
{
    char path[256];
    char line[512];
    BPTR file;

    snprintf(path, sizeof(path), ENGINE_REGISTRY_DIR "/%s", name);
    file = Open(path, MODE_OLDFILE);
    if (file == BNULL)
        return;
    while (FGets(file, line, sizeof(line)) != NULL)
    {
        char *start = line;
        char *end = line + strlen(line);

        while (*start == ' ' || *start == '\t')
            ++start;
        while (end > start && (end[-1] == '\n' || end[-1] == '\r' ||
            end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        if (*start != '\0' && *start != ';' && *start != '#')
            engine_add_switch(start, name);
    }
    Close(file);
}

static void engine_read_registry(void)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock(ENGINE_REGISTRY_DIR, SHARED_LOCK);

    if (fib != NULL && dir != BNULL && Examine(dir, fib) &&
        fib->fib_DirEntryType > 0)
    {
        while (ExNext(dir, fib))
            if (fib->fib_DirEntryType < 0)
                engine_read_registry_file(fib->fib_FileName);
    }
    if (dir != BNULL)
        UnLock(dir);
    if (fib != NULL)
        FreeDosObject(DOS_FIB, fib);
}

static void engine_tell_launcher(struct EngineRequest *request,
    const char *text)
{
    KPrintF("[ELECTRON-ENGINE] launcher %lu: %s", request->launcher_pid,
        text);
    if (request->error != BNULL)
        Write(request->error, (APTR)text, strlen(text));
}

/* An app needing a scheme the running engine lacks would come up broken
   (blank windows, failed loads); it is refused with the remedy instead. */
static int engine_check_switches(struct EngineRequest *request)
{
    char message[512];
    char missing[ELECTRON_SHELL_MAX_SCHEME_NAME];
    int i;

    for (i = 1; i < request->argc; ++i)
    {
        const char *arg = request->argv[i];

        if (arg == NULL)
            continue;
        if (scheme_switch_missing(arg, missing, sizeof(missing)))
        {
            snprintf(message, sizeof(message),
                "ElectronShell: the running Electron engine was started "
                "without the URL scheme '%s' this app needs (%s).\n"
                "Put the app's scheme switches in ENVARC:Electron/Engine/"
                "<app>, one per line, and reboot.\n", missing, arg);
            engine_tell_launcher(request, message);
            return 0;
        }
        if (strncmp(arg, ENABLE_FEATURES_OPTION,
            sizeof(ENABLE_FEATURES_OPTION) - 1) == 0)
        {
            const char *list = arg + sizeof(ENABLE_FEATURES_OPTION) - 1;

            while (*list != '\0')
            {
                const char *end = strchr(list, ',');
                size_t length = end != NULL ? (size_t)(end - list) :
                    strlen(list);

                if (length > 0 && !feature_listed(engine_features[0], list,
                    length))
                {
                    snprintf(message, sizeof(message),
                        "ElectronShell: warning: the running Electron engine "
                        "was started without the feature '%.*s'.\n",
                        (int)length, list);
                    engine_tell_launcher(request, message);
                }
                if (end == NULL)
                    break;
                list = end + 1;
            }
        }
    }
    return 1;
}

static int engine_set_default_url(struct EngineApp *app, const char *url)
{
    size_t length = url != NULL ? strlen(url) :
        (sizeof(test_url) / sizeof(test_url[0])) - 1;
    size_t i;

    app->default_url = AllocVec((length + 1) * sizeof(cef_char_t), MEMF_ANY);
    if (app->default_url == NULL)
        return 0;
    for (i = 0; i < length; ++i)
        app->default_url[i] = url != NULL ? (unsigned char)url[i] :
            test_url[i];
    app->default_url[length] = 0;
    app->default_url_length = length;
    return 1;
}

static void engine_reject(struct EngineRequest *request, LONG code)
{
    if (request->current_dir != BNULL)
        UnLock(request->current_dir);
    request->current_dir = BNULL;
    request->result = code;
    ReplyMsg(&request->msg);
}

/* A launcher's app joins the engine: registry entry, runtime slot, Node. */
static void engine_start_app(struct EngineRequest *request)
{
    struct EngineApp *app = NULL;
    char *script;
    BPTR old_dir;
    LONG rc;
    int i;

    if (!engine_check_switches(request))
    {
        engine_reject(request, RETURN_FAIL);
        return;
    }
    for (i = 0; i < MAX_ENGINE_APPS; ++i)
    {
        if (!engine_apps[i].in_use)
        {
            app = &engine_apps[i];
            break;
        }
    }
    if (app == NULL)
    {
        KPrintF("[ELECTRON-ENGINE] registry full (%ld apps), launcher %lu "
            "refused\n", (LONG)MAX_ENGINE_APPS, request->launcher_pid);
        engine_reject(request, RETURN_FAIL);
        return;
    }
    memset(app, 0, sizeof(*app));
    app->slot = ElectronCreateAppRuntime();
    if (app->slot <= 0 || !engine_set_default_url(app, request->url))
    {
        KPrintF("[ELECTRON-ENGINE] no runtime slot for launcher %lu\n",
            request->launcher_pid);
        if (app->slot > 0)
            ElectronReleaseAppRuntime(app->slot);
        memset(app, 0, sizeof(*app));
        engine_reject(request, RETURN_FAIL);
        return;
    }
    app->in_use = 1;
    app->request = request;
    app->index = request->app_index;
    app->launcher_pid = request->launcher_pid;
    app->current_dir = request->current_dir;
    request->current_dir = BNULL;
    ElectronSetAppStdio(app->slot, request->output, request->error);
    DateStamp(&app->started);
    app->start_tick = pump_tick;
    app->probe_mode = request->main_script == NULL && request->url == NULL;
    ++engine_app_count;
    KPrintF("[ELECTRON-ENGINE] registry +app slot=%ld index=%ld launcher=%lu "
        "script='%s' count=%ld\n", app->slot, app->index, app->launcher_pid,
        request->main_script != NULL ? request->main_script : "(built-in)",
        (LONG)engine_app_count);

    script = engine_build_script(request);
    if (script == NULL)
    {
        KPrintF("[ELECTRON-ENGINE] slot %ld: cannot build the main script "
            "wrapper\n", app->slot);
        engine_finish_app(app, RETURN_FAIL);
        return;
    }
    KPrintF("[ELECTRON-CONTEXT] Node main wrapper '%s'\n", script);
    old_dir = CurrentDir(app->current_dir);
    rc = ElectronSelectAppRuntime(app->slot);
    if (rc == 0)
    {
        rc = ElectronPrepareNodeEmbedder();
        if (rc == 0)
            rc = ElectronAttachNodeOwnedV8ContextWithModule(
                (CONST_STRPTR)script);
        ElectronSelectAppRuntime(0);
    }
    CurrentDir(old_dir);
    FreeVec(script);
    KPrintF("[ELECTRON-ENGINE] slot %ld attach rc=%ld\n", app->slot, rc);
    if (rc != 0)
        engine_finish_app(app, RETURN_FAIL);
}

static void engine_cancel_launcher(ULONG launcher_pid)
{
    int i;

    for (i = 0; i < MAX_ENGINE_APPS; ++i)
    {
        struct EngineApp *app = &engine_apps[i];

        if (app->in_use && app->launcher_pid == launcher_pid)
        {
            KPrintF("[ELECTRON-ENGINE] launcher %lu broke off, closing slot "
                "%ld\n", launcher_pid, app->slot);
            engine_request_app_exit(app, APP_CANCELLED_CODE);
        }
    }
}

static void engine_service_port(void)
{
    struct EngineRequest *request;

    while ((request = (struct EngineRequest *)GetMsg(engine_port)) != NULL)
    {
        if (request->magic != ENGINE_REQUEST_MAGIC)
        {
            ReplyMsg(&request->msg);
            continue;
        }
        if (request->kind == ENGINE_CANCEL)
        {
            engine_cancel_launcher(request->launcher_pid);
            request->result = 0;
            ReplyMsg(&request->msg);
            continue;
        }
        engine_start_app(request);
    }
}

/* The engine could not start: every launcher already queued gets a
   failure, and the port goes so the next launcher starts a new host. */
static void engine_close_port(void)
{
    struct EngineRequest *request;

    Forbid();
    RemPort(engine_port);
    Permit();
    while ((request = (struct EngineRequest *)GetMsg(engine_port)) != NULL)
    {
        if (request->magic == ENGINE_REQUEST_MAGIC &&
            request->kind == ENGINE_RUN_APP)
            engine_reject(request, RETURN_FAIL);
        else
            ReplyMsg(&request->msg);
    }
    DeleteMsgPort(engine_port);
    engine_port = NULL;
}

static int engine_main(int argc, char **argv)
{
    char *cef_argv[MAX_ENGINE_SWITCHES + 6];
    char *features[2] = { NULL, NULL };
    cef_main_args_t main_args;
    cef_settings_t settings;
    cef_window_info_t window_info;
    cef_browser_settings_t browser_settings;
    BPTR sys_dir;
    BPTR start_dir = BNULL;
    int cef_argc = 0;
    int arg_index;
    LONG rc;

    engine_read_registry();
    for (arg_index = 1; arg_index < argc; ++arg_index)
        if (argv[arg_index] != NULL)
            engine_add_switch(argv[arg_index], "the launcher");
    cef_argv[cef_argc++] = argv[0];
    cef_argv[cef_argc++] = (char *)"--disable-gpu";
    cef_argv[cef_argc++] = (char *)"--disable-gpu-compositing";
    for (arg_index = 0; arg_index < engine_switch_count; ++arg_index)
        cef_argv[cef_argc++] = engine_switches[arg_index];
    for (arg_index = 0; arg_index < 2; ++arg_index)
    {
        const char *option = arg_index == 0 ? ENABLE_FEATURES_OPTION :
            DISABLE_FEATURES_OPTION;
        size_t size;

        if (engine_features[arg_index] == NULL)
            continue;
        size = strlen(option) + strlen(engine_features[arg_index]) + 1;
        features[arg_index] = AllocVec(size, MEMF_ANY);
        if (features[arg_index] == NULL)
            continue;
        snprintf(features[arg_index], size, "%s%s", option,
            engine_features[arg_index]);
        cef_argv[cef_argc++] = features[arg_index];
    }
    cef_argv[cef_argc] = NULL;
    for (arg_index = 1; arg_index < cef_argc; ++arg_index)
        KPrintF("[ELECTRON-ENGINE] CEF switch '%s'\n", cef_argv[arg_index]);
    main_args.argc = cef_argc;
    main_args.argv = cef_argv;

    engine_port = CreateMsgPort();
    if (engine_port == NULL)
        return RETURN_FAIL;
    engine_port->mp_Node.ln_Name = (char *)ENGINE_PORT_NAME;
    engine_port->mp_Node.ln_Pri = 0;
    Forbid();
    if (FindPort(ENGINE_PORT_NAME) != NULL)
    {
        Permit();
        DeleteMsgPort(engine_port);
        KPrintF("[ELECTRON-ENGINE] another engine host owns the port\n");
        return RETURN_OK;
    }
    AddPort(engine_port);
    Permit();
    KPrintF("[ELECTRON-ENGINE] engine host %lu started, port '%s'\n",
        GetETaskID(FindTask(NULL)), ENGINE_PORT_NAME);

    if (!open_electron())
    {
        engine_close_port();
        return RETURN_FAIL;
    }

    /* The host outlives the launcher that started it: it must not hold
       that launcher's directory. */
    sys_dir = Lock("SYS:", SHARED_LOCK);
    if (sys_dir != BNULL)
        start_dir = CurrentDir(sys_dir);

    initialize_handlers();
    ui_pump_port = CreateMsgPort();
    if (ui_pump_port == NULL ||
        !ensure_profile_directory("T:ElectronContext") ||
        !ensure_profile_directory("T:ElectronContext/Default"))
        goto failed;
    memset(&ui_pump_message, 0, sizeof(ui_pump_message));
    ui_pump_message.mn_Node.ln_Type = NT_MESSAGE;
    ui_pump_message.mn_Length = sizeof(ui_pump_message);
    fill_cef_settings(&settings);

    rc = ElectronPrepareNodeEmbedder();
    KPrintF("[ELECTRON-CONTEXT] prepare Node rc=%ld\n", rc);
    if (rc != 0)
        goto failed;
    rc = ElectronExecuteCEFProcess(&main_args, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] CEF execute rc=%ld\n", rc);
    if (rc >= 0)
        goto failed;
    rc = ElectronInitializeCEF(&main_args, &settings, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] CEF initialize rc=%ld\n", rc);
    if (rc == ELECTRON_CEF_ENGINE_RUNNING)
    {
        KPrintF("[ELECTRON-ENGINE] CEF is already running in this boot "
            "under another host; this host cannot serve apps\n");
        goto failed;
    }
    if (rc == 0)
        goto failed;

    memset(&window_info, 0, sizeof(window_info));
    window_info.bounds.width = VIEW_WIDTH;
    window_info.bounds.height = VIEW_HEIGHT;
    set_static_cef_string(&window_info.window_name, window_title,
        (sizeof(window_title) / sizeof(window_title[0])) - 1);
    memset(&browser_settings, 0, sizeof(browser_settings));
    browser_settings.size = sizeof(browser_settings);

    /* Chromium installs its browser-task V8 platform before Node creates
       the first owned main-process isolate in the same resident engine. */
    ElectronDoCEFMessageLoopWork();
    KPrintF("[ELECTRON-ENGINE] engine running\n");

    for (pump_tick = 0; ; ++pump_tick)
    {
        ElectronDoCEFMessageLoopWork();
        service_renderer_pump_port();
        engine_service_port();
        engine_pump_apps();
        service_native_window_requests(&window_info, &browser_settings);
        Delay(1);
    }

failed:
    KPrintF("[ELECTRON-ENGINE] engine host failed to start\n");
    engine_close_port();
    if (ui_pump_port != NULL)
        DeleteMsgPort(ui_pump_port);
    if (sys_dir != BNULL)
    {
        CurrentDir(start_dir);
        UnLock(sys_dir);
    }
    return RETURN_FAIL;
}

/* Renderer and other CEF child processes of a multi-process engine. */
static int child_main(int argc, char **argv)
{
    char *cef_argv[argc + 1];
    cef_main_args_t main_args;
    int arg_index;
    LONG rc;

    for (arg_index = 0; arg_index < argc; ++arg_index)
        cef_argv[arg_index] = argv[arg_index];
    cef_argv[argc] = NULL;
    main_args.argc = argc;
    main_args.argv = cef_argv;
    if (!open_electron())
        return RETURN_FAIL;
    initialize_handlers();
    rc = ElectronExecuteCEFProcess(&main_args, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] child CEF execute rc=%ld\n", rc);
    return rc >= 0 ? (int)rc : RETURN_FAIL;
}

/* The launcher's local shell variables, "name\0value\0...\0". */
static char *launcher_local_env(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    struct LocalVar *var;
    size_t size = 1;
    char *block;
    char *out;

    ForeachNode((struct List *)&process->pr_LocalVars, var)
    {
        if (var->lv_Node.ln_Type == LV_VAR &&
            !(var->lv_Flags & GVF_BINARY_VAR) && var->lv_Value != NULL &&
            memchr(var->lv_Value, '\0', var->lv_Len) == NULL)
            size += strlen(var->lv_Node.ln_Name) + 1 + var->lv_Len + 1;
    }
    block = AllocVec(size, MEMF_ANY);
    if (block == NULL)
        return NULL;
    out = block;
    ForeachNode((struct List *)&process->pr_LocalVars, var)
    {
        if (var->lv_Node.ln_Type == LV_VAR &&
            !(var->lv_Flags & GVF_BINARY_VAR) && var->lv_Value != NULL &&
            memchr(var->lv_Value, '\0', var->lv_Len) == NULL)
        {
            size_t name_length = strlen(var->lv_Node.ln_Name);

            memcpy(out, var->lv_Node.ln_Name, name_length + 1);
            out += name_length + 1;
            memcpy(out, var->lv_Value, var->lv_Len);
            out += var->lv_Len;
            *out++ = '\0';
        }
    }
    *out = '\0';
    return block;
}

/* Starts an engine host from this program's own file, detached from the
   launcher: NIL: for its stdio, the launcher's CEF switches on its command
   line. */
static int launcher_spawn_engine(int argc, char **argv)
{
    char program[512];
    char name[256];
    char *command;
    size_t size;
    BPTR input;
    BPTR output;
    BPTR error;
    LONG rc;
    int arg_index;

    if (!NameFromLock(GetProgramDir(), program, sizeof(program)) ||
        !GetProgramName(name, sizeof(name)) ||
        !AddPart(program, FilePart(name), sizeof(program)))
    {
        KPrintF("[ELECTRON-LAUNCHER] cannot name this program to start the "
            "engine\n");
        return 0;
    }
    size = strlen(program) + sizeof(ENGINE_HOST_OPTION) + 8;
    for (arg_index = 1; arg_index < argc; ++arg_index)
        size += strlen(argv[arg_index]) + 4;
    command = AllocVec(size, MEMF_ANY);
    if (command == NULL)
        return 0;
    snprintf(command, size, "\"%s\" " ENGINE_HOST_OPTION, program);
    for (arg_index = 1; arg_index < argc; ++arg_index)
    {
        const char *argument = argv[arg_index];

        if (argument[0] != '-' || argument[1] != '-' ||
            strncmp(argument, MAIN_SCRIPT_OPTION,
                sizeof(MAIN_SCRIPT_OPTION) - 1) == 0 ||
            strncmp(argument, APP_SCRIPT_OPTION,
                sizeof(APP_SCRIPT_OPTION) - 1) == 0 ||
            strchr(argument, '"') != NULL)
            continue;
        strcat(command, " \"");
        strcat(command, argument);
        strcat(command, "\"");
    }
    /* All three streams explicitly: left out, the error stream is a
       duplicate of this launcher's, which dies with it. */
    input = Open("NIL:", MODE_OLDFILE);
    output = Open("NIL:", MODE_NEWFILE);
    error = Open("NIL:", MODE_NEWFILE);
    rc = -1;
    if (input != BNULL && output != BNULL && error != BNULL)
        rc = SystemTags((CONST_STRPTR)command,
            SYS_Input, (IPTR)input,
            SYS_Output, (IPTR)output,
            SYS_Error, (IPTR)error,
            SYS_Asynch, TRUE,
            TAG_END);
    KPrintF("[ELECTRON-LAUNCHER] started engine host rc=%ld: %s\n", rc,
        command);
    FreeVec(command);
    if (rc == -1)
    {
        if (input != BNULL)
            Close(input);
        if (output != BNULL)
            Close(output);
        if (error != BNULL)
            Close(error);
        return 0;
    }
    return 1;
}

static int launcher_post(struct EngineRequest *requests, int count)
{
    struct MsgPort *port;
    int i;

    Forbid();
    port = FindPort(ENGINE_PORT_NAME);
    if (port != NULL)
        for (i = 0; i < count; ++i)
            PutMsg(port, &requests[i].msg);
    Permit();
    return port != NULL;
}

static int launcher_main(int argc, char **argv)
{
    struct EngineRequest requests[1 + MAX_EXTRA_APPS];
    struct EngineRequest cancel;
    struct MsgPort *reply_port;
    const char *main_script = NULL;
    const char *url = NULL;
    const char *app_scripts[MAX_EXTRA_APPS];
    int app_script_count = 0;
    char *env;
    ULONG pid = GetETaskID(FindTask(NULL));
    ULONG waited = 0;
    int count;
    int replies = 0;
    int spawned = 0;
    int posted = 0;
    int cancel_state = 0;
    int i;
    LONG result;

    for (i = 1; i < argc; ++i)
    {
        if (argv[i] == NULL || argv[i][0] == '\0')
            continue;
        if (strncmp(argv[i], MAIN_SCRIPT_OPTION,
            sizeof(MAIN_SCRIPT_OPTION) - 1) == 0)
            main_script = argv[i] + sizeof(MAIN_SCRIPT_OPTION) - 1;
        else if (strncmp(argv[i], APP_SCRIPT_OPTION,
            sizeof(APP_SCRIPT_OPTION) - 1) == 0)
        {
            if (app_script_count < MAX_EXTRA_APPS)
                app_scripts[app_script_count++] =
                    argv[i] + sizeof(APP_SCRIPT_OPTION) - 1;
            else
                KPrintF("[ELECTRON-LAUNCHER] too many " APP_SCRIPT_OPTION
                    " options, ignoring '%s'\n", argv[i]);
        }
        else if (!(argv[i][0] == '-' && argv[i][1] == '-') && url == NULL &&
            main_script == NULL)
            url = argv[i];
    }
    if (main_script != NULL)
        url = NULL;
    if (main_script != NULL && (main_script[0] == '\0' ||
        strchr(main_script, ':') == NULL && strchr(main_script, '/') == NULL))
    {
        KPrintF("[ELECTRON-LAUNCHER] invalid main script path '%s'\n",
            main_script);
        return RETURN_FAIL;
    }
    if (main_script == NULL)
        app_script_count = 0;

    reply_port = CreateMsgPort();
    env = launcher_local_env();
    if (reply_port == NULL || env == NULL)
    {
        if (reply_port != NULL)
            DeleteMsgPort(reply_port);
        FreeVec(env);
        return RETURN_FAIL;
    }
    count = 1 + app_script_count;
    memset(requests, 0, sizeof(requests));
    for (i = 0; i < count; ++i)
    {
        struct EngineRequest *request = &requests[i];

        request->msg.mn_Node.ln_Type = NT_MESSAGE;
        request->msg.mn_Length = sizeof(*request);
        request->msg.mn_ReplyPort = reply_port;
        request->magic = ENGINE_REQUEST_MAGIC;
        request->kind = ENGINE_RUN_APP;
        request->launcher_pid = pid;
        request->app_index = i;
        request->main_script = i == 0 ? main_script : app_scripts[i - 1];
        request->url = i == 0 ? url : NULL;
        request->argc = argc;
        request->argv = argv;
        request->env = env;
        request->current_dir = DupLock(((struct Process *)FindTask(NULL))->
            pr_CurrentDir);
        request->output = Output();
        request->error = ((struct Process *)FindTask(NULL))->pr_CES != BNULL ?
            ((struct Process *)FindTask(NULL))->pr_CES : Output();
        request->result = RETURN_FAIL;
    }

    for (;;)
    {
        posted = launcher_post(requests, count);
        if (posted)
            break;
        if (!spawned)
        {
            if (!launcher_spawn_engine(argc, argv))
                break;
            spawned = 1;
        }
        if (waited >= ENGINE_START_TIMEOUT_TICKS ||
            CheckSignal(SIGBREAKF_CTRL_C))
            break;
        Delay(5);
        waited += 5;
    }
    if (!posted)
    {
        KPrintF("[ELECTRON-LAUNCHER] no engine to run the app\n");
        for (i = 0; i < count; ++i)
            if (requests[i].current_dir != BNULL)
                UnLock(requests[i].current_dir);
        DeleteMsgPort(reply_port);
        FreeVec(env);
        return RETURN_FAIL;
    }

    /* CTRL-C asks the engine to close this launcher's apps; their replies
       still come back before the launcher may go. */
    memset(&cancel, 0, sizeof(cancel));
    while (replies < count || cancel_state == 1)
    {
        struct Message *message;
        ULONG signals = Wait((1UL << reply_port->mp_SigBit) |
            SIGBREAKF_CTRL_C);

        while ((message = GetMsg(reply_port)) != NULL)
        {
            if (message == &cancel.msg)
                cancel_state = 2;
            else
            {
                ++replies;
                KPrintF("[ELECTRON-LAUNCHER] %lu: app %ld exited code=%ld\n",
                    pid, ((struct EngineRequest *)message)->app_index,
                    ((struct EngineRequest *)message)->result);
            }
        }
        if ((signals & SIGBREAKF_CTRL_C) && cancel_state == 0 &&
            replies < count)
        {
            cancel.msg.mn_Node.ln_Type = NT_MESSAGE;
            cancel.msg.mn_Length = sizeof(cancel);
            cancel.msg.mn_ReplyPort = reply_port;
            cancel.magic = ENGINE_REQUEST_MAGIC;
            cancel.kind = ENGINE_CANCEL;
            cancel.launcher_pid = pid;
            if (!launcher_post(&cancel, 1))
            {
                KPrintF("[ELECTRON-LAUNCHER] engine port gone while %ld "
                    "app(s) were running\n", (LONG)(count - replies));
                return RETURN_FAIL;
            }
            cancel_state = 1;
        }
    }
    result = requests[0].result;
    DeleteMsgPort(reply_port);
    FreeVec(env);
    return (int)result;
}

static int real_main(int argc, char **argv)
{
    int arg_index;

    for (arg_index = 1; arg_index < argc; ++arg_index)
    {
        if (argv[arg_index] == NULL)
            continue;
        if (strncmp(argv[arg_index], "--type=", 7) == 0)
            return child_main(argc, argv);
        if (strcmp(argv[arg_index], ENGINE_HOST_OPTION) == 0)
            return engine_main(argc, argv);
    }
    return launcher_main(argc, argv);
}
#else
static int real_main(int argc, char **argv)
{
    char *cef_argv[4];
    cef_main_args_t main_args;
    cef_settings_t settings;
    cef_window_info_t window_info;
    cef_browser_settings_t browser_settings;
    cef_string_t url;
    LONG rc;
    ULONG ticks;
    ULONG close_ticks;
    int result = 20;

    (void)argc;
    initialize_handlers();
    ui_pump_port = CreateMsgPort();
    if (ui_pump_port == NULL)
    {
        KPrintF("[ELECTRON-CONTEXT] unable to create UI pump port\n");
        return 20;
    }
    memset(&ui_pump_message, 0, sizeof(ui_pump_message));
    ui_pump_message.mn_Node.ln_Type = NT_MESSAGE;
    ui_pump_message.mn_Length = sizeof(ui_pump_message);

    if (!ensure_profile_directory("T:ElectronContext") ||
        !ensure_profile_directory("T:ElectronContext/Default"))
    {
        DeleteMsgPort(ui_pump_port);
        return 20;
    }

    cef_argv[0] = argv[0];
    cef_argv[1] = (char *)"--disable-gpu";
    cef_argv[2] = (char *)"--disable-gpu-compositing";
    cef_argv[3] = NULL;
    main_args.argc = 3;
    main_args.argv = cef_argv;
    fill_cef_settings(&settings);

    rc = ElectronPrepareNodeEmbedder();
    KPrintF("[ELECTRON-CONTEXT] prepare Node rc=%ld\n", rc);
    if (rc != 0)
    {
        DeleteMsgPort(ui_pump_port);
        return (int)rc;
    }

    rc = ElectronExecuteCEFProcess(&main_args, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] CEF execute rc=%ld\n", rc);
    if (rc >= 0)
    {
        DeleteMsgPort(ui_pump_port);
        return (int)rc;
    }

    rc = ElectronInitializeCEF(&main_args, &settings, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] CEF initialize rc=%ld\n", rc);
    if (rc == ELECTRON_CEF_ENGINE_RUNNING)
        KPrintF("[ELECTRON-CONTEXT] CEF already runs in this boot; the "
            "probe needs a fresh engine\n");
    if (rc == 0 || rc == ELECTRON_CEF_ENGINE_RUNNING)
        goto detach_node;

    memset(&window_info, 0, sizeof(window_info));
    window_info.bounds.width = VIEW_WIDTH;
    window_info.bounds.height = VIEW_HEIGHT;
    window_info.windowless_rendering_enabled = 1;
    memset(&browser_settings, 0, sizeof(browser_settings));
    browser_settings.size = sizeof(browser_settings);
    memset(&url, 0, sizeof(url));
    set_static_cef_string(&url, test_url,
        (sizeof(test_url) / sizeof(test_url[0])) - 1);

    rc = ElectronCreateCEFBrowser(&window_info, &client, &url,
        &browser_settings, NULL, NULL);
    KPrintF("[ELECTRON-CONTEXT] create browser rc=%ld\n", rc);
    if (rc == 0)
        goto shutdown_cef;

    for (ticks = 0; ticks < CREATE_TIMEOUT_TICKS && !browser_closed; ++ticks)
    {
        pump_tick = ticks;
        if (ticks == 0)
            KPrintF("[ELECTRON-CONTEXT] entering first CEF message-loop pump\n");
        ElectronDoCEFMessageLoopWork();
        if (ticks == 0)
            KPrintF("[ELECTRON-CONTEXT] completed first CEF message-loop pump\n");
        service_renderer_pump_port();
        if (!node_marker_seen && marker_exists())
            node_marker_seen = 1;
        if (node_attached && node_detached && node_marker_seen &&
            !node_task_mismatch && first_paint &&
            ticks > first_paint_tick + CLOSE_AFTER_PAINT_TICKS)
            request_browser_close();
        Delay(1);
    }

    if (!browser_closed)
        request_browser_close();
    for (close_ticks = 0;
        close_ticks < CLOSE_TIMEOUT_TICKS &&
            (!browser_closed || !node_detached);
        ++close_ticks)
    {
        ElectronDoCEFMessageLoopWork();
        service_renderer_pump_port();
        Delay(1);
    }

    if (node_attached && node_detached && node_marker_seen &&
        !node_task_mismatch && node_pump_ticks >= NODE_PUMP_TICKS &&
        first_paint && browser_closed)
    {
        KPrintF("[ELECTRON-CONTEXT] PASS persistent Node main context drove "
            "BrowserWindow requests pumps=%lu\n", node_pump_ticks);
        result = 0;
    }
    else
    {
        KPrintF("[ELECTRON-CONTEXT] FAIL attached=%ld detached=%ld "
            "marker=%ld task_mismatch=%ld pumps=%lu painted=%ld closed=%ld\n",
            (LONG)node_attached, (LONG)node_detached, (LONG)node_marker_seen,
            (LONG)node_task_mismatch, node_pump_ticks, (LONG)first_paint,
            (LONG)browser_closed);
    }

    drain_cef_before_shutdown();

shutdown_cef:
    ElectronShutdownCEF();
detach_node:
    if (!node_detached)
        ElectronDetachNodeEmbedder();
    release_node_context();
    DeleteMsgPort(ui_pump_port);
    return result;
}
#endif
