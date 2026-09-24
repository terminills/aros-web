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
#include <proto/debug.h>
#include <proto/dos.h>
#include <proto/electron.h>
#include <proto/exec.h>
#ifdef ELECTRON_SHELL_WINDOWED
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
/*
 * A second (third, ...) Electron app hosted by this same process.
 *
 * CEF keeps one browser context and one UI thread per process, so two apps
 * started as two AROS processes cannot share the engine - the second one
 * page-faults inside cef-blink.library while the first keeps its window.
 * Hosting them together is CEF's own model: one
 * CefInitialize, one UI thread, a window and a Node runtime each. Each extra
 * app gets a runtime slot from node.library and its turn in the pump.
 */
#define APP_SCRIPT_OPTION "--app-script="
#define MAX_EXTRA_APPS 4

struct ExtraApp
{
    const char *path;
    LONG slot;
    int attached;
    int exit_requested;         /* app.exit seen; windows closing */
    LONG exit_code;
    int finished;               /* runtime detached, slot released */
};

static struct ExtraApp extra_apps[MAX_EXTRA_APPS];
static int extra_app_count;
/* The primary app asked to exit; the process follows once no extra app is
   left. */
static int primary_exit_requested;
static ULONG primary_exit_tick;
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
static int exit_requested;
static LONG requested_exit_code;
static int node_attached;
static int node_detached;
static int node_task_mismatch;
static int node_marker_seen;
static int completion_queued;
static int external_url_mode;
/* --main-script= runs the script as the application: no probe tick cap, no
   auto-close after first paint, exit when the script asks (app.exit) or
   its last window has been gone for APP_CLOSE_GRACE_TICKS. */
static int app_mode;
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
static cef_char_t command_url[2048];
static cef_char_t window_title[] = u"AROS Electron Shell";
static unsigned char command_node_script[4096];
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

static struct ExtraApp *shell_find_extra_app(LONG slot)
{
    int i;

    for (i = 0; i < extra_app_count; ++i)
        if (extra_apps[i].attached && extra_apps[i].slot == slot)
            return &extra_apps[i];
    return NULL;
}

/* Extra apps still running a Node runtime. */
static int shell_live_extra_app_count(void)
{
    int count = 0;
    int i;

    for (i = 0; i < extra_app_count; ++i)
        if (extra_apps[i].attached && !extra_apps[i].finished)
            ++count;
    return count;
}

/*
 * Ends one extra app: its windows are gone, so detach its Node runtime and
 * give the slot back. The process goes on with whatever apps remain - a
 * GitHub Desktop quit must not take VS Code with it. Node events
 * the seam still posts to the released slot ('closed' for a late
 * on_before_close) go to a queue nothing drains, which is harmless.
 */
static void shell_finish_extra_app(struct ExtraApp *extra)
{
    if (extra->finished)
        return;
    extra->finished = 1;
    if (ElectronSelectAppRuntime(extra->slot) == 0)
    {
        ElectronDetachNodeEmbedder();
        ElectronSelectAppRuntime(0);
    }
    ElectronReleaseAppRuntime(extra->slot);
    KPrintF("[ELECTRON-CONTEXT] app slot %ld exited code=%ld, %ld app(s) "
        "still running\n", extra->slot, extra->exit_code,
        (LONG)(shell_live_extra_app_count() +
            (node_attached && !node_detached ? 1 : 0)));
}

/*
 * The primary app is done: its windows are closed, its runtime comes down
 * now rather than at process end, and the process itself ends when the last
 * extra app has gone. Its exit code stays the process's.
 */
static void shell_finish_primary_app(void)
{
    if (node_attached && !node_detached)
    {
        ElectronSelectAppRuntime(0);
        ElectronDetachNodeEmbedder();
        node_detached = 1;
    }
    KPrintF("[ELECTRON-CONTEXT] primary app exited code=%ld, %ld extra "
        "app(s) still running\n", requested_exit_code,
        (LONG)shell_live_extra_app_count());
    if (shell_live_extra_app_count() == 0)
        exit_requested = 1;
}

/* Runs once per service pass: apps whose exit is pending finish as soon as
   their last window has closed. */
static void shell_finish_exited_apps(void)
{
    int i;

    for (i = 0; i < extra_app_count; ++i)
    {
        struct ExtraApp *extra = &extra_apps[i];

        if (extra->attached && extra->exit_requested && !extra->finished &&
            shell_slot_window_count(extra->slot) == 0)
            shell_finish_extra_app(extra);
    }
    if (primary_exit_requested && !exit_requested &&
        shell_slot_window_count(0) == 0)
    {
        if (node_attached && !node_detached)
            shell_finish_primary_app();
        else if (shell_live_extra_app_count() == 0)
            exit_requested = 1;
    }
    else if (primary_exit_requested && !exit_requested &&
        pump_tick > primary_exit_tick + CLOSE_TIMEOUT_TICKS)
    {
        /* A window that never reports on_before_close must not keep the
           process alive forever; the close loop below the pump drains it. */
        KPrintF("[ELECTRON-CONTEXT] primary app.exit: %ld window(s) still "
            "open after %ld ticks, exiting anyway\n",
            (LONG)shell_slot_window_count(0), (LONG)CLOSE_TIMEOUT_TICKS);
        exit_requested = 1;
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
    cef_browser_settings_t *browser_settings,
    cef_char_t *browser_url, size_t browser_url_length)
{
    char method[64];
    char payload[2048];
    LONG slot;
    LONG rc;

    while ((rc = ElectronPollAppRequest(method, sizeof(method),
        payload, sizeof(payload), &slot)) > 0)
    {
        struct ShellWindow *window = NULL;

        KPrintF("[ELECTRON-CONTEXT] native request slot=%ld method='%s' "
            "payload='%s'\n", slot, method, payload);
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
            /* app.exit ends the app that asked, not the process: its
               windows close, then shell_finish_exited_apps() takes its
               runtime down. The process ends with its last app; the exit
               status is the primary app's. */
            LONG code = strtol(payload, NULL, 10);
            struct ExtraApp *extra = shell_find_extra_app(slot);

            KPrintF("[ELECTRON-CONTEXT] app.exit requested by slot %ld "
                "code=%ld\n", slot, code);
            if (extra != NULL)
            {
                extra->exit_requested = 1;
                extra->exit_code = code;
            }
            else
            {
                requested_exit_code = code;
                primary_exit_requested = 1;
                primary_exit_tick = pump_tick;
            }
            if (shell_slot_window_count(slot) > 0)
                shell_request_slot_windows_close(slot);
        }
    }
    shell_finish_exited_apps();

    {
        int i;

        for (i = 0; i < MAX_SHELL_WINDOWS; ++i)
        {
            struct ShellWindow *window = &shell_windows[i];

            if (window->in_use && window->create_requested)
            {
                window->create_requested = 0;
                if (window->browser == NULL && !window->creating)
                    shell_create_window(window, window_info,
                        browser_settings, browser_url, browser_url_length);
            }
        }
    }
    shell_sync_all_window_geometry();
}

static void pump_node_on_main_task(void)
{
    LONG rc;

    if (!node_attached || node_detached)
        return;
    rc = ElectronPumpNodeEmbedder();
    ++node_pump_ticks;
    if ((node_pump_ticks & 7UL) == 0)
        KPrintF("[ELECTRON-CONTEXT] main Node pump=%lu rc=%ld\n",
            node_pump_ticks, rc);
    if (!node_marker_seen && marker_exists())
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

static int parse_scheme_switch(const char *arg)
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
            declare_scheme_list(arg + prefix_length,
                scheme_switches[i].options);
            return 1;
        }
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
            KPrintF("[ELECTRON-CONTEXT] BrowserWindow %ld/%lu bound to "
                "browser %p identifier=%ld\n", window->slot, window->id,
                created_browser, (LONG)window->browser_identifier);
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
    if (app_mode)
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

#ifdef ELECTRON_SHELL_WINDOWED
/*
 * Append one command-line argument to the process.argv literal being built
 * in command_node_script.  Returns the new length, or 0 if it does not fit.
 */
static size_t append_node_argv_entry(size_t length, const char *argument)
{
    size_t i;

    if (length + 3 >= sizeof(command_node_script))
        return 0;
    command_node_script[length++] = ',';
    command_node_script[length++] = '\'';
    for (i = 0; argument[i] != '\0'; ++i)
    {
        if (length + 3 >= sizeof(command_node_script))
            return 0;
        if (argument[i] == '\'' || argument[i] == '\\')
            command_node_script[length++] = '\\';
        command_node_script[length++] = (unsigned char)argument[i];
    }
    command_node_script[length++] = '\'';
    return length;
}

/*
 * Electron hands the whole command line to the main script as process.argv
 * (VS Code's --force-disable-user-env, --disable-extensions, a folder to
 * open...), so forward every argument the shell did not consume itself: the
 * main-script option and the custom-scheme switches are ours.
 *
 * `electron <app> ...` also exposes the app directory as the first
 * non-option entry, and VS Code's dev-mode argv parser
 * (parseMainProcessArgv: stripAppPath) REMOVES the first non-option entry -
 * with no app path present it drops every argument instead.  So the main
 * script's directory goes in first, exactly where Electron puts it.
 */
static int make_node_main_script(const char *path, LONG slot, int argc,
    char **argv)
{
    static const char prefix[] =
        "process.argv=['AROS-Electron','--no-sandbox',"
        "'--disable-gpu','--disable-gpu-compositing'";
    static const char argv_end[] = "];";
    static const char middle[] =
        "process.env.VSCODE_DEV='1';"
        "process._rawDebug('[ELECTRON-MODULE] loading main entry "
        MAIN_SCRIPT_OPTION "');"
        "const __arosElectronMain='";
    /*
     * A synchronous throw from the main script never reaches the module
     * bootstrap's uncaughtException hook (it leaves LoadEnvironment directly)
     * and node.library only reports it through NodeBridgeLog(), which is
     * compiled out of a non-debug tree - so it came back as a bare rc=20.
     * Report it through the bootstrap's console.error hook (the [ELECTRON-JS]
     * line in the debug log), then rethrow so the attach fails as before.
     */
    static const char suffix[] =
        "';const __arosRequire=require('module').createRequire("
        "__arosElectronMain);"
        "globalThis.__arosElectronMainModuleRequest=__arosElectronMain;"
        "try{__arosRequire(__arosElectronMain);}catch(e){"
        "console.error('[ELECTRON-MODULE] main threw: '+"
        "((e&&e.stack)||e));throw e;}";
    const size_t prefix_length = sizeof(prefix) - 1;
    const size_t argv_end_length = sizeof(argv_end) - 1;
    const size_t middle_length = sizeof(middle) - 1;
    const size_t suffix_length = sizeof(suffix) - 1;
    const size_t path_length = strlen(path);
    char slot_env[sizeof("process.env.ELECTRON_APP_SLOT='';") + 20];
    size_t length = prefix_length;
    int arg_index;

    if (path_length == 0 || strchr(path, '\'') != NULL ||
        prefix_length >= sizeof(command_node_script))
        return 0;

    memcpy(command_node_script, prefix, prefix_length);
    {
        /* App directory = the main script path up to its last separator. */
        char app_path[512];
        size_t app_length = path_length;

        while (app_length > 0 && path[app_length - 1] != '/' &&
            path[app_length - 1] != ':')
            --app_length;
        if (app_length == 0 || app_length >= sizeof(app_path))
            return 0;
        memcpy(app_path, path, app_length);
        app_path[app_length] = '\0';
        length = append_node_argv_entry(length, app_path);
        if (length == 0)
            return 0;
        KPrintF("[ELECTRON-CONTEXT] process.argv app path '%s'\n", app_path);
    }
    for (arg_index = 1; arg_index < argc; ++arg_index)
    {
        if (argv[arg_index] == NULL || argv[arg_index][0] == '\0')
            continue;
        if (strncmp(argv[arg_index], MAIN_SCRIPT_OPTION,
            sizeof(MAIN_SCRIPT_OPTION) - 1) == 0)
            continue;
        if (argv[arg_index][0] == '-' && argv[arg_index][1] == '-' &&
            strstr(argv[arg_index], "-schemes=") != NULL)
            continue;
        length = append_node_argv_entry(length, argv[arg_index]);
        if (length == 0)
        {
            KPrintF("[ELECTRON-CONTEXT] process.argv does not fit at '%s'\n",
                argv[arg_index]);
            return 0;
        }
        KPrintF("[ELECTRON-CONTEXT] forwarding process.argv entry '%s'\n",
            argv[arg_index]);
    }
    if (length + argv_end_length + sizeof(slot_env) + middle_length +
        path_length + suffix_length + 1 > sizeof(command_node_script))
        return 0;

    memcpy(command_node_script + length, argv_end, argv_end_length);
    length += argv_end_length;
    /* Which runtime slot the app runs in (0 = the primary app), so a script
       serving as more than one app in a process can tell its instances
       apart. */
    length += snprintf(command_node_script + length, sizeof(slot_env),
        "process.env.ELECTRON_APP_SLOT='%ld';", (long)slot);
    memcpy(command_node_script + length, middle, middle_length);
    length += middle_length;
    memcpy(command_node_script + length, path, path_length);
    length += path_length;
    memcpy(command_node_script + length, suffix, suffix_length + 1);
    return 1;
}
#endif

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

static int real_main(int argc, char **argv)
{
    /*
     * Preserve every native child-process switch and leave room for the two
     * AROS defaults plus the terminating NULL.  Chromium appends transport
     * switches (including Mojo) after its ordinary process switches, so a
     * fixed bring-up array silently converted a valid multi-process launch
     * into an invalid child command line.
     */
    char *cef_argv[argc + 3];
    cef_main_args_t main_args;
    cef_settings_t settings;
    cef_window_info_t window_info;
    cef_browser_settings_t browser_settings;
    cef_string_t url;
    cef_char_t *selected_url = test_url;
    size_t selected_url_length =
        (sizeof(test_url) / sizeof(test_url[0])) - 1;
    int external_url = 0;
    int cef_child_process = 0;
    int cef_argc;
    int arg_index;
    LONG rc;
    ULONG ticks;
    int extra_index;
    ULONG close_ticks;
    int result = 20;
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR saved_window_ptr = NULL;
    int requesters_suppressed = 0;
    const unsigned char *selected_node_script = node_script;

#ifdef ELECTRON_SHELL_WINDOWED
    for (arg_index = 1; arg_index < argc; ++arg_index)
    {
        const size_t main_script_option_length =
            sizeof(MAIN_SCRIPT_OPTION) - 1;
        size_t i;

        if (argv[arg_index] == NULL || argv[arg_index][0] == '\0')
            continue;
        if (strncmp(argv[arg_index], "--type=", 7) == 0)
            cef_child_process = 1;
        if (strncmp(argv[arg_index], MAIN_SCRIPT_OPTION,
            main_script_option_length) == 0)
        {
            const char *main_script_path =
                argv[arg_index] + main_script_option_length;
            if (!make_node_main_script(main_script_path, 0, argc, argv))
            {
                KPrintF("[ELECTRON-CONTEXT] invalid main script path '%s'\n",
                    main_script_path);
                return 20;
            }
            selected_node_script = command_node_script;
            app_mode = 1;
            KPrintF("[ELECTRON-CONTEXT] loading Node main script '%s'\n",
                main_script_path);
            continue;
        }
        if (strncmp(argv[arg_index], APP_SCRIPT_OPTION,
            sizeof(APP_SCRIPT_OPTION) - 1) == 0)
        {
            if (extra_app_count < MAX_EXTRA_APPS)
            {
                extra_apps[extra_app_count].path =
                    argv[arg_index] + sizeof(APP_SCRIPT_OPTION) - 1;
                KPrintF("[ELECTRON-CONTEXT] extra app %ld script '%s'\n",
                    (LONG)extra_app_count,
                    extra_apps[extra_app_count].path);
                ++extra_app_count;
            }
            else
                KPrintF("[ELECTRON-CONTEXT] too many " APP_SCRIPT_OPTION
                    " options, ignoring '%s'\n", argv[arg_index]);
            continue;
        }
        if (parse_scheme_switch(argv[arg_index]))
        {
            KPrintF("[ELECTRON-CONTEXT] custom scheme switch '%s'\n",
                argv[arg_index]);
            continue;
        }
        if (argv[arg_index][0] == '-' && argv[arg_index][1] == '-')
            continue;
        if (external_url)
            continue;

        for (i = 0; argv[arg_index][i] != '\0' &&
            i + 1 < (sizeof(command_url) / sizeof(command_url[0])); ++i)
            command_url[i] = (unsigned char)argv[arg_index][i];
        command_url[i] = 0;
        selected_url = command_url;
        selected_url_length = i;
        external_url = 1;
        external_url_mode = 1;
        KPrintF("[ELECTRON-CONTEXT] loading command URL '%s'\n",
            argv[arg_index]);
    }
#else
    (void)argc;
#endif
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

    if (external_url && process != NULL)
    {
        saved_window_ptr = process->pr_WindowPtr;
        process->pr_WindowPtr = (APTR)(SIPTR)-1;
        requesters_suppressed = 1;
        KPrintF("[ELECTRON-CONTEXT] DOS requesters suppressed window=%p\n",
            saved_window_ptr);
    }

    cef_argv[0] = argv[0];
    /*
     * Native AROS child launch and Mojo transport are now available.  Keep
     * --single-process as an explicitly forwarded compatibility option, not
     * the default Electron process model.
     */
    cef_argv[1] = (char *)"--disable-gpu";
    cef_argv[2] = (char *)"--disable-gpu-compositing";
    cef_argc = 3;
#ifdef ELECTRON_SHELL_WINDOWED
    for (arg_index = 1; arg_index < argc &&
        cef_argc + 1 < (int)(sizeof(cef_argv) / sizeof(cef_argv[0]));
        ++arg_index)
    {
        if (argv[arg_index] != NULL && argv[arg_index][0] == '-' &&
            argv[arg_index][1] == '-' &&
            strncmp(argv[arg_index], MAIN_SCRIPT_OPTION,
                sizeof(MAIN_SCRIPT_OPTION) - 1) != 0 &&
            strstr(argv[arg_index], "-schemes=") == NULL)
        {
            cef_argv[cef_argc++] = argv[arg_index];
            KPrintF("[ELECTRON-CONTEXT] forwarding CEF switch '%s'\n",
                argv[arg_index]);
        }
    }
#else
    (void)arg_index;
#endif
    cef_argv[cef_argc] = NULL;
    main_args.argc = cef_argc;
    main_args.argv = cef_argv;

    memset(&settings, 0, sizeof(settings));
    settings.size = sizeof(settings);
    settings.no_sandbox = 1;
#ifndef ELECTRON_SHELL_WINDOWED
    settings.windowless_rendering_enabled = 1;
#endif
    set_static_cef_string(&settings.root_cache_path, root_cache_path,
        (sizeof(root_cache_path) / sizeof(root_cache_path[0])) - 1);
    set_static_cef_string(&settings.cache_path, cache_path,
        (sizeof(cache_path) / sizeof(cache_path[0])) - 1);
    set_static_cef_string(&settings.resources_dir_path, resources_path,
        (sizeof(resources_path) / sizeof(resources_path[0])) - 1);
    set_static_cef_string(&settings.locales_dir_path, locales_path,
        (sizeof(locales_path) / sizeof(locales_path[0])) - 1);

    if (cef_child_process)
    {
        rc = ElectronExecuteCEFProcess(&main_args, &app, NULL);
        KPrintF("[ELECTRON-CONTEXT] child CEF execute rc=%ld\n", rc);
        if (rc >= 0)
        {
            DeleteMsgPort(ui_pump_port);
            return (int)rc;
        }
    }

    /*
     * Renderer children share electron.library with the browser process, but
     * they are Chromium roles rather than Node embedders.  Dispatch them
     * before touching the process-global Node engine; otherwise they observe
     * the browser's prepared Node state, return rc=20, and exit before Blink
     * can execute the document's first script.  Other child roles retain the
     * established bring-up behavior until their service lifecycle is proven
     * independently.
     */
    rc = ElectronPrepareNodeEmbedder();
    KPrintF("[ELECTRON-CONTEXT] prepare Node rc=%ld\n", rc);
    if (rc != 0)
    {
        DeleteMsgPort(ui_pump_port);
        return (int)rc;
    }

    if (!cef_child_process)
    {
        rc = ElectronExecuteCEFProcess(&main_args, &app, NULL);
        KPrintF("[ELECTRON-CONTEXT] CEF execute rc=%ld\n", rc);
        if (rc >= 0)
        {
            if (requesters_suppressed)
            {
                process->pr_WindowPtr = saved_window_ptr;
                KPrintF("[ELECTRON-CONTEXT] DOS requester window restored=%p\n",
                    saved_window_ptr);
            }
            DeleteMsgPort(ui_pump_port);
            return (int)rc;
        }
    }

    rc = ElectronInitializeCEF(&main_args, &settings, &app, NULL);
    KPrintF("[ELECTRON-CONTEXT] CEF initialize rc=%ld\n", rc);
    if (rc == 0)
        goto detach_node;

    memset(&window_info, 0, sizeof(window_info));
    window_info.bounds.width = VIEW_WIDTH;
    window_info.bounds.height = VIEW_HEIGHT;
#ifdef ELECTRON_SHELL_WINDOWED
    set_static_cef_string(&window_info.window_name, window_title,
        (sizeof(window_title) / sizeof(window_title[0])) - 1);
#else
    window_info.windowless_rendering_enabled = 1;
#endif
    memset(&browser_settings, 0, sizeof(browser_settings));
    browser_settings.size = sizeof(browser_settings);
    memset(&url, 0, sizeof(url));
    set_static_cef_string(&url, selected_url, selected_url_length);

#ifdef ELECTRON_SHELL_WINDOWED
    /*
     * Let Chromium install its browser-task V8 platform before Node creates
     * the owned main-process isolate in the same resident engine.
     */
    KPrintF("[ELECTRON-CONTEXT] priming CEF UI loop before Node attach\n");
    ElectronDoCEFMessageLoopWork();
    KPrintF("[ELECTRON-CONTEXT] primed CEF UI loop before Node attach\n");
    KPrintF("[ELECTRON-CONTEXT] Node main wrapper '%s'\n",
        selected_node_script);
    rc = ElectronAttachNodeOwnedV8ContextWithModule(selected_node_script);
    KPrintF("[ELECTRON-CONTEXT] attach owned main V8 context rc=%ld\n", rc);
    if (rc != 0)
        goto shutdown_cef;
    node_attached = 1;
    /* Preserve the UI task captured before CEF/Node initialize their workers. */
    node_context_task = (struct Task *)process;
    KPrintF("[ELECTRON-CONTEXT] entering initial native request service\n");
    service_native_window_requests(&window_info, &browser_settings,
        selected_url, selected_url_length);
    KPrintF("[ELECTRON-CONTEXT] completed initial native request service\n");
#else
    rc = ElectronCreateCEFBrowser(&window_info, &client, &url,
        &browser_settings, NULL, NULL);
    KPrintF("[ELECTRON-CONTEXT] create browser rc=%ld\n", rc);
    if (rc == 0)
        goto shutdown_cef;
#endif

    /*
     * Extra apps join the engine the primary app just started: a runtime slot
     * each, their own Node environment, their own window. Selecting a slot
     * also tells the seam which app its native events belong to, so a
     * did-finish-load meant for one app's window is not swallowed by another
     * app's pump. Slot 0 puts the primary app back.
     *
     * An app needs a main script to be an app, and the main-script machinery
     * belongs to the windowed shell, so this is windowed-only - the headless
     * regression variant builds without it rather than against symbols that
     * are not there.
     */
#ifdef ELECTRON_SHELL_WINDOWED
    for (extra_index = 0; extra_index < extra_app_count; ++extra_index)
    {
        struct ExtraApp *extra = &extra_apps[extra_index];

        extra->slot = ElectronCreateAppRuntime();
        if (extra->slot <= 0)
        {
            KPrintF("[ELECTRON-CONTEXT] extra app %ld: no runtime slot\n",
                (LONG)extra_index);
            continue;
        }
        if (ElectronSelectAppRuntime(extra->slot) != 0)
        {
            KPrintF("[ELECTRON-CONTEXT] extra app %ld: slot %ld select failed\n",
                (LONG)extra_index, extra->slot);
            continue;
        }
        if (!make_node_main_script(extra->path, extra->slot, argc, argv))
        {
            KPrintF("[ELECTRON-CONTEXT] extra app %ld: bad script '%s'\n",
                (LONG)extra_index, extra->path);
            ElectronSelectAppRuntime(0);
            continue;
        }
        rc = ElectronPrepareNodeEmbedder();
        if (rc == 0)
            rc = ElectronAttachNodeOwnedV8ContextWithModule(
                command_node_script);
        KPrintF("[ELECTRON-CONTEXT] extra app %ld slot %ld attach rc=%ld\n",
            (LONG)extra_index, extra->slot, rc);
        /* This bridge reports success as rc == 0, the way the primary app's
           attach above does. */
        extra->attached = (rc == 0);
        ElectronSelectAppRuntime(0);
    }
#else
    if (extra_app_count > 0)
        KPrintF("[ELECTRON-CONTEXT] --app-script needs the windowed shell; "
            "%ld extra app(s) ignored\n", (LONG)extra_app_count);
#endif

    for (ticks = 0;
        !exit_requested &&
            (app_mode ?
                !(browser_closed &&
                    ticks > browser_closed_tick + APP_CLOSE_GRACE_TICKS) :
                (ticks < CREATE_TIMEOUT_TICKS && !browser_closed));
        ++ticks)
    {
        pump_tick = ticks;
        if (ticks == 0)
            KPrintF("[ELECTRON-CONTEXT] entering first CEF message-loop pump\n");
        ElectronDoCEFMessageLoopWork();
        if (ticks == 0)
            KPrintF("[ELECTRON-CONTEXT] completed first CEF message-loop pump\n");
        service_renderer_pump_port();
        for (extra_index = 0; extra_index < extra_app_count; ++extra_index)
        {
            struct ExtraApp *extra = &extra_apps[extra_index];

            if (!extra->attached || extra->finished)
                continue;
            if (ElectronSelectAppRuntime(extra->slot) == 0)
            {
                ElectronPumpNodeEmbedder();
                ElectronSelectAppRuntime(0);
            }
        }
#ifdef ELECTRON_SHELL_WINDOWED
        service_native_window_requests(&window_info, &browser_settings,
            selected_url, selected_url_length);
        pump_node_on_main_task();
#endif
        if (!node_marker_seen && marker_exists())
            node_marker_seen = 1;
#ifdef ELECTRON_SHELL_WINDOWED
        if (!external_url && !app_mode &&
            node_attached && node_marker_seen && !node_task_mismatch &&
            first_paint && ticks > first_paint_tick + CLOSE_AFTER_PAINT_TICKS)
            request_browser_close();
#else
        if (!external_url &&
            node_attached && node_detached && node_marker_seen &&
            !node_task_mismatch && first_paint &&
            ticks > first_paint_tick + CLOSE_AFTER_PAINT_TICKS)
            request_browser_close();
#endif
        Delay(1);
    }

#ifdef ELECTRON_SHELL_WINDOWED
    if (!browser_closed)
        shell_request_all_windows_close();
#else
    if (!browser_closed)
        request_browser_close();
#endif
    for (close_ticks = 0;
        close_ticks < CLOSE_TIMEOUT_TICKS &&
            (!browser_closed || !node_detached);
        ++close_ticks)
    {
        ElectronDoCEFMessageLoopWork();
        service_renderer_pump_port();
#ifdef ELECTRON_SHELL_WINDOWED
        pump_node_on_main_task();
#endif
        Delay(1);
    }

#ifdef ELECTRON_SHELL_WINDOWED
    if (node_attached && !node_detached)
    {
        ElectronDetachNodeEmbedder();
        node_detached = 1;
    }
    /* Extra apps still running when the process ends (primary app.exit
       timed out, or a shutdown request) go down the same way as one that
       exited on its own. */
    for (extra_index = 0; extra_index < extra_app_count; ++extra_index)
    {
        if (extra_apps[extra_index].attached && !extra_apps[extra_index].finished)
            shell_finish_extra_app(&extra_apps[extra_index]);
    }
#endif
    if (app_mode)
    {
        /* The application decides its own exit status; a script that just
           let its last window close (no app.exit) exited normally. */
        result = exit_requested ? (int)requested_exit_code : 0;
        if (browser_closed && !exit_requested)
            KPrintF("[ELECTRON-CONTEXT] app quit without app.exit "
                "(last window closed %lu ticks ago)\n",
                (unsigned long)(pump_tick - browser_closed_tick));
        KPrintF("[ELECTRON-CONTEXT] app exited code=%ld pumps=%lu "
            "painted=%ld closed=%ld\n", (LONG)result, node_pump_ticks,
            (LONG)first_paint, (LONG)browser_closed);
    }
    else if (node_attached && node_detached && node_marker_seen &&
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
    if (requesters_suppressed)
    {
        process->pr_WindowPtr = saved_window_ptr;
        KPrintF("[ELECTRON-CONTEXT] DOS requester window restored=%p\n",
            saved_window_ptr);
    }
    DeleteMsgPort(ui_pump_port);
    return result;
}
