/** @file
 * helpers - Guest Additions Service helper functions header
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
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 *
 */

#ifndef __VBOXSERVICEHELPERS__H
#define __VBOXSERVICEHELPERS__H

// #define DEBUG_DISPLAY_CHANGE

#ifndef DDCLOG
void SvcDebugOut2(char *String, ...);
# ifdef DEBUG_DISPLAY_CHANGE
#  define DDCLOG(a) dprintf(a)
# else
#  define DDCLOG(a) do {} while (0)
# endif /* DEBUG_DISPLAY_CHANGE */
#endif /* DDCLOG */

#ifdef DEBUG
void WriteLog(char *String, ...);
#define dprintf(a) do { WriteLog a; } while (0)
#else
#define dprintf(a) do {} while (0)
#endif /* DEBUG */

void resizeRect(RECTL *paRects, unsigned nRects, unsigned iPrimary, unsigned iResized, int NewWidth, int NewHeight);

#endif /* __VBOXSERVICEHELPERS__H */
