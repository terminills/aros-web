/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Inject - feed synthetic mouse and key events to input.device
    (IND_WRITEEVENT), the stream Intuition reads, so they reach a window or
    a requester exactly as real input would.  Written for the hosted
    Chromium runner: the host side (xdotool/XTEST) cannot press a gadget in
    the hosted AROS window under Xwayland, so the runner asks the guest to
    press it instead.  Coordinates are screen pixels.

        Inject move  <x> <y>
        Inject click <x> <y> [button]     button: 0 left (default), 1 right, 2 middle
        Inject down  <x> <y> [button]
        Inject up    <x> <y> [button]
        Inject key   <rawcode> [UP|DOWN] [CTRL] [SHIFT] [ALT] [RALT]
        Inject text  <string>

    "text" types a string: keymap.library MapANSI() turns each character
    into the rawkey + qualifier pairs the current keymap needs (dead keys
    take two) and each pair is sent like a "key" with those qualifiers.
    Newlines are not typed; follow with "key 0x44" (Return) when needed.

    A bare "key" presses and releases the key; UP or DOWN sends only that
    half.  CTRL/SHIFT/ALT/RALT wrap the key in the qualifier key's own
    press/release AND set the matching ie_Qualifier bits on the key event,
    which is what a real keyboard produces (input.device does not derive
    the qualifiers of a written event itself), so "key 0x11 CTRL" is Ctrl+W.

    Motion is sent the way the hosted X11 mouse driver sends it: a RAWMOUSE
    event WITHOUT IEQUALIFIER_RELATIVEMOUSE carries an absolute screen
    position, which Intuition's input handler takes as-is.  Relative motion
    goes through the acceleration and PointerTicks division (default
    PointerTicks is 2, rom/intuition/intuition_misc.c), so single-pixel
    relative steps put the pointer at half the requested coordinates -- a
    click meant for a requester gadget at (368,320) landed on the desktop at
    (184,160).  Button events carry the same absolute position, as the
    gameport stream does.
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/alib.h>
#include <proto/keymap.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct MsgPort *port;
static struct IOStdReq *io;

static void write_event(struct InputEvent *ie)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Flags = 0;
    io->io_Length = sizeof(struct InputEvent);
    io->io_Data = ie;
    DoIO((struct IORequest *)io);
}

static WORD cur_x, cur_y;

static void mouse_event(UWORD code, UWORD qualifier)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = code;
    ie.ie_Qualifier = qualifier;
    ie.ie_X = cur_x;
    ie.ie_Y = cur_y;
    write_event(&ie);
}

static void move_to(WORD x, WORD y)
{
    cur_x = x;
    cur_y = y;
    mouse_event(IECODE_NOBUTTON, 0);
}

static void button(int btn, int pressed)
{
    static const UWORD code[] = { IECODE_LBUTTON, IECODE_RBUTTON, IECODE_MBUTTON };
    static const UWORD qual[] = { IEQUALIFIER_LEFTBUTTON, IEQUALIFIER_RBUTTON, IEQUALIFIER_MIDBUTTON };

    if (btn < 0 || btn > 2)
        btn = 0;
    mouse_event(code[btn] | (pressed ? 0 : IECODE_UP_PREFIX), pressed ? qual[btn] : 0);
}

static void raw_key(UWORD rawcode, int up, UWORD qualifier)
{
    struct InputEvent ie;

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = rawcode | (up ? IECODE_UP_PREFIX : 0);
    ie.ie_Qualifier = qualifier;
    write_event(&ie);
}

/* Qualifier keys by name: rawcode + the ie_Qualifier bit they hold down. */
static const struct { const char *name; UWORD rawcode; UWORD bit; } quals[] =
{
    { "CTRL",  0x63, IEQUALIFIER_CONTROL },
    { "SHIFT", 0x60, IEQUALIFIER_LSHIFT  },
    { "ALT",   0x64, IEQUALIFIER_LALT    },
    { "RALT",  0x65, IEQUALIFIER_RALT    },
};

/* Press (down), release (up) or tap (both) rawcode under the qualifier
 * bits: each qualifier key goes down before, and comes up after, with the
 * accumulated bits carried on every event in between.  ie_Qualifier is the
 * state AFTER the event, as a real keyboard reports it: a qualifier key's
 * own down event already carries its bit, its up event no longer does.
 * Consumers that track modifiers from the events (Chromium's ozone layer
 * does) otherwise keep Ctrl/Shift "held" after the key - a later click
 * becomes Ctrl+click (link opens in a new tab, buttons ignore it). */
static void key_with_qualifier(UWORD rawcode, UWORD qualifier, int down, int up)
{
    UWORD held = 0;
    int j;

    for (j = 0; j < (int)(sizeof(quals) / sizeof(quals[0])); j++)
        if (qualifier & quals[j].bit)
        {
            held |= quals[j].bit;
            raw_key(quals[j].rawcode, 0, held);
        }
    if (down)
        raw_key(rawcode, 0, qualifier);
    if (down && up)
        Delay(2);
    if (up)
        raw_key(rawcode, 1, qualifier);
    for (j = (int)(sizeof(quals) / sizeof(quals[0])) - 1; j >= 0; j--)
        if (qualifier & quals[j].bit)
        {
            held &= ~quals[j].bit;
            raw_key(quals[j].rawcode, 1, held);
        }
}

static void key_with_flags(UWORD rawcode, int argc, char **argv)
{
    UWORD qualifier = 0;
    int i, j, down = 1, up = 1;

    for (i = 0; i < argc; i++)
    {
        if (!strcmp(argv[i], "UP"))
            down = 0;
        else if (!strcmp(argv[i], "DOWN"))
            up = 0;
        else
            for (j = 0; j < (int)(sizeof(quals) / sizeof(quals[0])); j++)
                if (!strcmp(argv[i], quals[j].name))
                    qualifier |= quals[j].bit;
    }
    key_with_qualifier(rawcode, qualifier, down, up);
}

/* MapANSI() fills pairs of (rawkey, qualifier); a character reached through
 * a dead key needs two pairs.  The keymap's SHIFT/ALT/CTRL qualifier bits are
 * the ie_Qualifier ones, so each pair is a "key" with those flags. */
static int type_text(const char *text)
{
    UBYTE pairs[2 * 2];
    LONG n, i;

    for (; *text; text++)
    {
        if (*text == '\n' || *text == '\r')
            continue;
        /* MapANSI() resolves '(' and ')' to the Amiga keypad's own keys
         * (0x5A/0x5B); a PC keyboard, and the hosted X11 driver, produce
         * Shift+9 / Shift+0, which is what every layout table knows. */
        if (*text == '(' || *text == ')')
        {
            pairs[0] = *text == '(' ? 0x09 : 0x0A;
            pairs[1] = IEQUALIFIER_LSHIFT;
            n = 1;
        }
        else
            n = MapANSI((STRPTR)text, 1, (STRPTR)pairs, 2, NULL);
        if (n <= 0)
        {
            printf("Inject: no key for character 0x%02x\n", (unsigned)(UBYTE)*text);
            return 0;
        }
        for (i = 0; i < n; i++)
        {
            key_with_qualifier(pairs[2 * i], pairs[2 * i + 1], 1, 1);
            Delay(1);
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *cmd;
    int rc = 0, btn;

    if (argc < 2)
    {
        printf("usage: Inject move|click|down|up <x> <y> [button] | key <rawcode> [UP|DOWN] [CTRL] [SHIFT] [ALT] [RALT] | text <string>\n");
        return RETURN_FAIL;
    }

    port = CreateMsgPort();
    io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(struct IOStdReq)) : NULL;
    if (!io || OpenDevice("input.device", 0, (struct IORequest *)io, 0))
    {
        printf("Inject: cannot open input.device\n");
        if (io)
            DeleteIORequest((struct IORequest *)io);
        if (port)
            DeleteMsgPort(port);
        return RETURN_FAIL;
    }

    cmd = argv[1];
    btn = argc >= 5 ? atoi(argv[4]) : 0;
    if (!strcmp(cmd, "move") && argc >= 4)
    {
        move_to(atoi(argv[2]), atoi(argv[3]));
    }
    else if ((!strcmp(cmd, "click") || !strcmp(cmd, "down") || !strcmp(cmd, "up")) && argc >= 4)
    {
        move_to(atoi(argv[2]), atoi(argv[3]));
        Delay(2);
        if (strcmp(cmd, "up"))
            button(btn, 1);
        if (strcmp(cmd, "down"))
        {
            Delay(2);
            button(btn, 0);
        }
    }
    else if (!strcmp(cmd, "key") && argc >= 3)
    {
        key_with_flags(strtol(argv[2], NULL, 0), argc - 3, argv + 3);
    }
    else if (!strcmp(cmd, "text") && argc >= 3)
    {
        if (!type_text(argv[2]))
            rc = RETURN_FAIL;
    }
    else
    {
        printf("Inject: bad arguments for '%s'\n", cmd);
        rc = RETURN_FAIL;
    }
    if (!rc)
        printf("Inject: %s %s %s done\n", cmd, argv[2], argc >= 4 ? argv[3] : "");

    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return rc;
}
