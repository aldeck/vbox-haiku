/* $Id$ */
/** @file
 * VBoxCocoaApplication - NSApplication subclass for handling -sendEvent.
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___darwin_VBoxCocoaApplication_h
#define ___darwin_VBoxCocoaApplication_h

#include <iprt/cdefs.h>
#ifdef __OBJC__
# import <AppKit/NSApplication.h>
#endif
#include <Carbon/Carbon.h>

RT_C_DECLS_BEGIN

/** Event handler callback.
 * @returns true if handled, false if not.
 * @param   pvCocoaEvent    The Cocoa event.
 * @param   pvCarbonEvent   The Carbon event.
 * @param   pvUser          The user argument.
 */
typedef bool (*PFNVBOXCACALLBACK)(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);


#ifdef __OBJC__

/** Structure for tracking a callback. */
typedef struct VBOXCAENTRY
{
    /** Mask of events to send to this callback. */
    uint32_t            fMask;
    /** The callback. */
    PFNVBOXCACALLBACK   pfnCallback;
    /** The user argument. */
    void               *pvUser;
} VBOXCAENTRY;
typedef VBOXCAENTRY *PVBOXCAENTRY;
typedef VBOXCAENTRY const *PCVBOXCAENTRY;


/**
 * Subclass for intercepting sendEvent messages.
 */
@interface VBoxCocoaApplication : NSApplication
{
    /** The event mask for which there currently are callbacks. */
    uint32_t    m_fMask;
    /** The number of current callbacks. */
    uint32_t    m_cCallbacks;
    /** Array of callbacks. */
    VBOXCAENTRY m_aCallbacks[4];
}
- (void)sendEvent:(NSEvent *)theEvent;
- (void)setCallback:(uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser;
- (void)unsetCallback:(uint32_t)fMask :(PFNVBOXCACALLBACK)pfnCallback :(void *)pvUser;

@end /* @interface VBoxCocoaApplication */

extern VBoxCocoaApplication *g_pVBoxCocoaApp;

#endif /* __OBJC__ */

/** @name The C/C++ interface.
 *
 * @remarks This is a bit illogical as both NSApplication and NSEvent stuff
 *          ended up here...
 * @{
 */
void VBoxCocoaApplication_sharedApplication(void);
void VBoxCocoaApplication_setCallback(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);
void VBoxCocoaApplication_unsetCallback(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);
unsigned long VBoxCocoaApplication_getEventModifierFlags(const void *pvEvent);
uint32_t VBoxCocoaApplication_getEventModifierFlagsXlated(const void *pvEvent);
void VBoxCocoaApplication_setMouseCoalescingEnabled(bool fEnabled);
const char *VBoxCocoaApplication_eventTypeName(unsigned long eEvtType);
bool VBoxCocoaApplication_isApplicationCommand(const void *pvEvent);
void VBoxCocoaApplication_printEvent(const char *pszPrefix, const void *pvEvent);
/** @} */

RT_C_DECLS_END

#endif

