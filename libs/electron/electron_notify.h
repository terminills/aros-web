#ifndef ELECTRON_NOTIFY_H
#define ELECTRON_NOTIFY_H

/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: Electron Notification on application.library, for the binding.
*/

#include <exec/types.h>

BOOL electron_notification_supported(void);
CONST_STRPTR electron_notification_show(CONST_STRPTR payload);
CONST_STRPTR electron_notification_close(CONST_STRPTR payload);
CONST_STRPTR electron_notification_poll(CONST_STRPTR payload);
CONST_STRPTR electron_notification_release(CONST_STRPTR payload);

#endif /* ELECTRON_NOTIFY_H */
