/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Electron's Notification class on application.library pop-ups.

    Each Electron app is registered once, under its app name, the first
    time it shows a notification. The pop-up carries "electron:<id>" as its
    back message: a double-click comes back as APPLIBMT_CustomMsg, and the
    close (timed out, dismissed, double-clicked or taken back) as
    APPLIBMT_ConfirmClose with the same string. The JavaScript side polls
    for those while it has notifications open.
*/

#include <aros/debug.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <libraries/application.h>
#include <proto/exec.h>
#include <proto/application.h>

#include <string.h>

#include "electron_notify.h"

#define NOTIFY_APPS     8
#define NOTIFY_BACKTAG  "electron:"
#define NOTIFY_RESULT   4096

struct NotifyApp
{
    ULONG          na_AppID;
    struct Task   *na_Owner;
    TEXT           na_Name[APPNOTIFY_TITLE_MAX];
};

static struct NotifyApp notify_apps[NOTIFY_APPS];
static char notify_result[NOTIFY_RESULT];

static ULONG notify_hex(char c)
{
    return c <= '9' ? c - '0' : (c & ~0x20) - 'A' + 10;
}

static BOOL notify_ishex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/* encodeURIComponent() undone, in place. */
static void notify_decode(char *text)
{
    char *read = text, *write = text;

    while (*read != '\0')
    {
        if (read[0] == '%' && notify_ishex(read[1]) && notify_ishex(read[2]))
        {
            *write++ = (char)((notify_hex(read[1]) << 4) | notify_hex(read[2]));
            read += 3;
        }
        else
            *write++ = *read++;
    }
    *write = '\0';
}

static char *notify_append(char *dst, const char *end, const char *src)
{
    while (*src && dst < end)
        *dst++ = *src++;
    *dst = '\0';
    return dst;
}

static void notify_backmsg(char *dst, ULONG size, const char *id)
{
    char *p = notify_append(dst, dst + size - 1, NOTIFY_BACKTAG);

    notify_append(p, dst + size - 1, id);
}

/* Split a tab-separated payload in place and percent-decode each field. */
static ULONG notify_fields(char *copy, char **fields, ULONG max)
{
    ULONG count = 0;
    char *cursor = copy;

    fields[count++] = cursor;
    while (*cursor != '\0' && count < max)
    {
        if (*cursor == '\t')
        {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
        ++cursor;
    }
    for (cursor = (char *)""; count < max; )
        fields[count++] = cursor;
    for (count = 0; count < max; ++count)
        if (fields[count][0] != '\0')
            notify_decode(fields[count]);
    return max;
}

/*
 * The pop-up font is 8-bit: UTF-8 becomes ISO-8859-1, characters beyond it
 * a '?'. dst may be src.
 */
static void notify_latin1(char *dst, const char *src, ULONG size)
{
    const unsigned char *s = (const unsigned char *)src;
    ULONG n = 0;

    while (*s != '\0' && n + 1 < size)
    {
        ULONG code = *s++;
        ULONG more = 0;

        if (code >= 0xF0)       { code &= 0x07; more = 3; }
        else if (code >= 0xE0)  { code &= 0x0F; more = 2; }
        else if (code >= 0xC0)  { code &= 0x1F; more = 1; }
        else if (code >= 0x80)  code = '?';
        while (more-- && (*s & 0xC0) == 0x80)
            code = (code << 6) | (*s++ & 0x3F);
        dst[n++] = code < 256 ? (char)code : '?';
    }
    dst[n] = '\0';
}

/* Application names have no spaces. */
static void notify_appname(char *dst, const char *src)
{
    ULONG i;

    notify_latin1(dst, src[0] ? src : "Electron", APPNOTIFY_TITLE_MAX);
    for (i = 0; dst[i]; ++i)
        if (dst[i] == ' ')
            dst[i] = '_';
}

/* Caller has application.library open as ApplicationBase. */
static struct NotifyApp *notify_find(struct Library *ApplicationBase, const char *name, BOOL create)
{
    struct NotifyApp *na = NULL;
    ULONG i;

    Forbid();
    for (i = 0; i < NOTIFY_APPS; ++i)
    {
        if (notify_apps[i].na_AppID && strcmp(notify_apps[i].na_Name, name) == 0)
        {
            na = &notify_apps[i];
            break;
        }
    }
    Permit();
    if (na || !create)
        return na;

    for (i = 0; i < NOTIFY_APPS; ++i)
    {
        Forbid();
        if (!notify_apps[i].na_AppID && !notify_apps[i].na_Owner)
        {
            na = &notify_apps[i];
            na->na_Owner = FindTask(NULL);  /* claimed */
        }
        Permit();
        if (na)
            break;
    }
    if (!na)
        return NULL;

    strcpy(na->na_Name, name);
    na->na_AppID = RegisterApplication(name,
        REGAPP_URLIdentifier, (IPTR)"electron.aros.org",
        REGAPP_Description, (IPTR)"Electron application",
        REGAPP_NoIcon, TRUE,
        TAG_DONE);
    if (!na->na_AppID)
    {
        na->na_Owner = NULL;
        return NULL;
    }
    return na;
}

BOOL electron_notification_supported(void)
{
    struct Library *ApplicationBase = OpenLibrary("application.library", 1);

    if (!ApplicationBase)
        return FALSE;
    CloseLibrary(ApplicationBase);
    return TRUE;
}

/* payload: app, id, title, body, icon, urgency, timeoutType, sound */
CONST_STRPTR electron_notification_show(CONST_STRPTR payload)
{
    struct Library *ApplicationBase;
    struct NotifyApp *na;
    char *copy, *f[8];
    char name[APPNOTIFY_TITLE_MAX], title[APPNOTIFY_TITLE_MAX + 1], text[APPNOTIFY_TEXT_MAX + 1];
    char back[48];
    ULONG pri = 0, res;

    if (!payload || !(copy = AllocVec(strlen((const char *)payload) + 1, MEMF_ANY)))
        return "error:10";
    strcpy(copy, (const char *)payload);
    notify_fields(copy, f, 8);

    if (!(ApplicationBase = OpenLibrary("application.library", 1)))
    {
        FreeVec(copy);
        return "error:120";
    }

    notify_appname(name, f[0]);
    notify_latin1(title, f[2], sizeof(title));
    notify_latin1(text, f[3], sizeof(text));
    notify_backmsg(back, sizeof(back), f[1]);
    if (strcmp(f[5], "critical") == 0 || strcmp(f[6], "never") == 0)
        pri = 10;

    if (!(na = notify_find(ApplicationBase, name, TRUE)))
        res = APPNOTIFY_ERROR_APPNOTREGISTERED;
    else
        res = Notify(na->na_AppID,
            APPNOTIFY_Title,        (IPTR)(title[0] ? title : name),
            APPNOTIFY_Text,         (IPTR)text,
            f[4][0] ? APPNOTIFY_ImageFile : TAG_IGNORE, (IPTR)f[4],
            f[4][0] ? APPNOTIFY_IconFile : TAG_IGNORE, (IPTR)f[4],
            f[7][0] ? APPNOTIFY_SoundFile : TAG_IGNORE, (IPTR)f[7],
            APPNOTIFY_Pri,          pri,
            APPNOTIFY_BackMsg,      (IPTR)back,
            APPNOTIFY_CloseOnDC,    TRUE,
            APPNOTIFY_ConfirmClose, TRUE,
            TAG_DONE);

    CloseLibrary(ApplicationBase);
    FreeVec(copy);
    if (res == APPNOTIFY_OK_MSGQUEUED)
        return "ok";
    {
        char digits[12], *p = digits + sizeof(digits) - 1;

        *p = '\0';
        do
            *--p = '0' + res % 10;
        while (res /= 10);
        notify_append(notify_append(notify_result, notify_result + sizeof(notify_result) - 1, "error:"),
                      notify_result + sizeof(notify_result) - 1, p);
    }
    return notify_result;
}

/* payload: app, id */
CONST_STRPTR electron_notification_close(CONST_STRPTR payload)
{
    struct Library *ApplicationBase;
    struct NotifyApp *na;
    char *copy, *f[2], name[APPNOTIFY_TITLE_MAX], back[48];

    if (!payload || !(copy = AllocVec(strlen((const char *)payload) + 1, MEMF_ANY)))
        return "error";
    strcpy(copy, (const char *)payload);
    notify_fields(copy, f, 2);
    notify_appname(name, f[0]);
    notify_backmsg(back, sizeof(back), f[1]);

    if ((ApplicationBase = OpenLibrary("application.library", 1)))
    {
        if ((na = notify_find(ApplicationBase, name, FALSE)))
            Notify(na->na_AppID, APPNOTIFY_Withdraw, (IPTR)back, TAG_DONE);
        CloseLibrary(ApplicationBase);
    }
    FreeVec(copy);
    return "ok";
}

/*
 * payload: app. Returns one line per event: "c:<id>" for a double-click,
 * "x:<id>" for a close.
 */
CONST_STRPTR electron_notification_poll(CONST_STRPTR payload)
{
    struct Library *ApplicationBase;
    struct NotifyApp *na;
    struct MsgPort *port = NULL;
    char name[APPNOTIFY_TITLE_MAX];
    ULONG len = 0, taglen = strlen(NOTIFY_BACKTAG);

    notify_result[0] = '\0';
    notify_appname(name, payload ? (const char *)payload : "");
    if (!(ApplicationBase = OpenLibrary("application.library", 1)))
        return notify_result;

    if ((na = notify_find(ApplicationBase, name, FALSE))
        && GetApplicationAttrs(na->na_AppID, APPATTR_Port, (IPTR)&port, TAG_DONE) && port)
    {
        struct ApplicationMsg *am;

        while ((am = (struct ApplicationMsg *)GetMsg(port)))
        {
            const char *back = NULL;

            if (am->type == APPLIBMT_CustomMsg || am->type == APPLIBMT_ConfirmClose)
                back = ((struct ApplicationCustomMsg *)am)->customMsg;
            if (back && strncmp(back, NOTIFY_BACKTAG, taglen) == 0
                && len + strlen(back) + 4 < sizeof(notify_result))
            {
                char *p = notify_result + len, *end = notify_result + sizeof(notify_result) - 1;

                p = notify_append(p, end, am->type == APPLIBMT_CustomMsg ? "c:" : "x:");
                p = notify_append(p, end, back + taglen);
                p = notify_append(p, end, "\n");
                len = p - notify_result;
            }
            ReplyMsg(&am->msg);
        }
    }
    CloseLibrary(ApplicationBase);
    return notify_result;
}

/* payload: app. Called when the app quits. */
CONST_STRPTR electron_notification_release(CONST_STRPTR payload)
{
    struct Library *ApplicationBase;
    struct NotifyApp *na;
    char name[APPNOTIFY_TITLE_MAX];

    notify_appname(name, payload ? (const char *)payload : "");
    if ((ApplicationBase = OpenLibrary("application.library", 1)))
    {
        if ((na = notify_find(ApplicationBase, name, FALSE)))
        {
            UnregisterApplication(na->na_AppID, NULL);
            Forbid();
            na->na_AppID = 0;
            na->na_Owner = NULL;
            Permit();
        }
        CloseLibrary(ApplicationBase);
    }
    return "ok";
}
