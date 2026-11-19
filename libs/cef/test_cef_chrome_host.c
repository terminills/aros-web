/* Native Chrome UI probe using the resident host and shared engine.
 * Navigation and callback lifetime are checked by the host client; native
 * desktop screenshots separately establish whether Chrome UI is visible. */
#define CEF_BROWSER_CHROME_WINDOW 1
#include "test_cef_host_browser.c"
