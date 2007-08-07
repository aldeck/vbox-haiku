/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * Declarations of Linux-specific keyboard functions
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __XKeyboard_h__
#define __XKeyboard_h__

#ifndef RT_OS_LINUX
#error This file is X11/Linux specific!
#endif

// our structure used to return keyboard event information
typedef struct _WINEKEYBOARDINFO
{
    unsigned short wVk;
    unsigned short wScan;
    unsigned long dwFlags;
    unsigned long time;
} WINEKEYBOARDINFO;

// initialize the X keyboard subsystem
bool initXKeyboard(Display *dpy);
// our custom keyboard handler
void handleXKeyEvent(Display *dpy, XEvent *event, WINEKEYBOARDINFO *wineKbdInfo);
// returns the number of keysyms per keycode (only valid after initXKeyboard())
int getKeysymsPerKeycode();

#endif // __XKeyboard_h__
