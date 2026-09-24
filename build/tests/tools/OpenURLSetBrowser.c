/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * OpenURLSetBrowser - register (or remove) a browser in openurl.library prefs.
 *
 *   OpenURLSetBrowser NAME/A,PATH,PORT/K,OPENCMD/K,OPENWCMD/K,SHOWCMD/K,
 *                     TOFRONTCMD/K,REMOVE/S,LIST/S
 *
 * Puts a URL_BrowserNode called NAME with command line PATH at the HEAD of the
 * openurl.library browser list (so C:OpenURL tries it first), and saves the
 * prefs to ENV: and ENVARC:.  A "%u" in PATH is replaced by the URL when the
 * browser is started; without one openurl.library expects an ARexx port, so
 * Chromium needs the "%u" form:
 *
 *   OpenURLSetBrowser Chromium "SYS:Developer/Chromium/chrome \"%u\"" PORT CHROMIUM
 *
 * PORT names the browser's ARexx-style command port.  openurl.library looks
 * that port up BEFORE it starts anything: when the browser is already running
 * the URL is sent to the port (OPENCMD, default `OPENURL "%u"`; OPENWCMD for
 * new-window requests, default `NEWWINDOW "%u"`) after SHOWCMD (default SHOW)
 * and TOFRONTCMD (default TOFRONT), and no second browser process is started.
 * Chromium publishes the CHROMIUM port (chrome/browser/process_singleton_aros.cc)
 * and answers exactly those commands.  Without PORT the entry has no port and
 * every URL starts a new browser process.
 *
 * REMOVE drops the entry called NAME; LIST prints the browser list and exits.
 * Prefs are read in URL_GetPrefs_Mode_InUse so whatever is already configured
 * (other browsers, mailers, flags) is kept.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <libraries/openurl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/openurl.h>
#include <stdio.h>
#include <string.h>

struct Library *OpenURLBase;

static struct URL_BrowserNode *findBrowser(struct URL_Prefs *p, const char *name)
{
    struct URL_BrowserNode *bn;

    for (bn = (struct URL_BrowserNode *)p->up_BrowserList.mlh_Head;
         bn->ubn_Node.mln_Succ != NULL;
         bn = (struct URL_BrowserNode *)bn->ubn_Node.mln_Succ)
    {
        if (strcmp(bn->ubn_Name, name) == 0)
            return bn;
    }
    return NULL;
}

static void listBrowsers(struct URL_Prefs *p)
{
    struct URL_BrowserNode *bn;

    for (bn = (struct URL_BrowserNode *)p->up_BrowserList.mlh_Head;
         bn->ubn_Node.mln_Succ != NULL;
         bn = (struct URL_BrowserNode *)bn->ubn_Node.mln_Succ)
    {
        printf("%c %-12s %s\n", (bn->ubn_Flags & UNF_DISABLED) ? '-' : '*',
               bn->ubn_Name, bn->ubn_Path);
        if (bn->ubn_Port[0] != '\0')
            printf("  port %s: show \"%s\" tofront \"%s\" open \"%s\" openw \"%s\"\n",
                   bn->ubn_Port, bn->ubn_ShowCmd, bn->ubn_ToFrontCmd,
                   bn->ubn_OpenURLCmd, bn->ubn_OpenURLWCmd);
    }
}

int main(void)
{
    struct { STRPTR name; STRPTR path; STRPTR port; STRPTR opencmd; STRPTR openwcmd;
             STRPTR showcmd; STRPTR tofrontcmd; IPTR remove; IPTR list; } args = { 0 };
    struct RDArgs *rda;
    struct URL_Prefs *p;
    struct URL_BrowserNode *bn, node;
    int rc = RETURN_FAIL;

    if ((rda = ReadArgs("NAME/A,PATH,PORT/K,OPENCMD/K,OPENWCMD/K,SHOWCMD/K,TOFRONTCMD/K,REMOVE/S,LIST/S",
                        (IPTR *)&args, NULL)) == NULL)
    {
        PrintFault(IoErr(), "OpenURLSetBrowser");
        return RETURN_ERROR;
    }

    if ((OpenURLBase = OpenLibrary(OPENURLNAME, 0)) == NULL)
    {
        fprintf(stderr, "OpenURLSetBrowser: cannot open %s\n", OPENURLNAME);
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    p = URL_GetPrefs(URL_GetPrefs_Mode, URL_GetPrefs_Mode_InUse,
                     URL_GetPrefs_FallBack, TRUE, TAG_DONE);
    if (p == NULL)
    {
        fprintf(stderr, "OpenURLSetBrowser: URL_GetPrefs failed\n");
        goto out;
    }

    if (args.list)
    {
        listBrowsers(p);
        rc = RETURN_OK;
        goto out;
    }

    /* Drop any existing entry of that name; the list is ours to edit until
     * URL_FreePrefs, and URL_SetPrefs copies whatever it finds. */
    while ((bn = findBrowser(p, args.name)) != NULL)
        Remove((struct Node *)bn);

    if (!args.remove)
    {
        if (args.path == NULL)
        {
            fprintf(stderr, "OpenURLSetBrowser: PATH required unless REMOVE\n");
            goto out;
        }
        memset(&node, 0, sizeof(node));
        node.ubn_Flags = 0;   /* enabled */
        strncpy(node.ubn_Name, args.name, NAME_LEN - 1);
        strncpy(node.ubn_Path, args.path, PATH_LEN - 1);
        if (args.port != NULL)
        {
            strncpy(node.ubn_Port, args.port, PORT_LEN - 1);
            strncpy(node.ubn_OpenURLCmd, args.opencmd ? args.opencmd : (STRPTR)"OPENURL \"%u\"",
                    OPENURLCMD_LEN - 1);
            strncpy(node.ubn_OpenURLWCmd, args.openwcmd ? args.openwcmd : (STRPTR)"NEWWINDOW \"%u\"",
                    OPENURLWCMD_LEN - 1);
            strncpy(node.ubn_ShowCmd, args.showcmd ? args.showcmd : (STRPTR)"SHOW",
                    SHOWCMD_LEN - 1);
            strncpy(node.ubn_ToFrontCmd, args.tofrontcmd ? args.tofrontcmd : (STRPTR)"TOFRONT",
                    TOFRONTCMD_LEN - 1);
        }
        AddHead((struct List *)&p->up_BrowserList, (struct Node *)&node);
    }

    if (URL_SetPrefs(p, URL_SetPrefs_Save, TRUE, TAG_DONE))
    {
        printf("OpenURLSetBrowser: %s \"%s\"%s%s%s%s, saved to ENV: and ENVARC:\n",
               args.remove ? "removed" : "registered", args.name,
               args.remove ? "" : " -> ", args.remove ? "" : (const char *)args.path,
               (!args.remove && args.port) ? " port " : "",
               (!args.remove && args.port) ? (const char *)args.port : "");
        rc = RETURN_OK;
    }
    else
        fprintf(stderr, "OpenURLSetBrowser: URL_SetPrefs failed\n");

    if (!args.remove)
        Remove((struct Node *)&node);   /* stack node must not be freed by URL_FreePrefs */

out:
    if (p)
        URL_FreePrefs(p, TAG_DONE);
    CloseLibrary(OpenURLBase);
    FreeArgs(rda);
    return rc;
}
