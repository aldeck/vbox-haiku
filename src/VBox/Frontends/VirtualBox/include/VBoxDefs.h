/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * Header with common definitions and global functions
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
 */

#ifndef __VBoxDefs_h__
#define __VBoxDefs_h__

#include <qevent.h>

#define LOG_GROUP LOG_GROUP_GUI
#include <VBox/log.h>
#include <iprt/assert.h>

#include <iprt/alloc.h>
#include <iprt/asm.h>

#ifdef VBOX_GUI_DEBUG

#define AssertWrapperOk(w)      \
    AssertMsg (w.isOk(), (#w " is not okay (RC=0x%08X)", w.lastRC()))
#define AssertWrapperOkMsg(w, m)      \
    AssertMsg (w.isOk(), (#w ": " m " (RC=0x%08X)", w.lastRC()))

#else // !VBOX_GUI_DEBUG

#define AssertWrapperOk(w)          do {} while (0)
#define AssertWrapperOkMsg(w, m)    do {} while (0)

#endif // !VBOX_GUI_DEBUG

#ifndef SIZEOF_ARRAY
#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof(a[0]))
#endif

#if defined (VBOX_GUI_USE_QIMAGE) || \
    defined (VBOX_GUI_USE_SDL) || \
    defined (VBOX_GUI_USE_DDRAW)
  #if !defined (VBOX_GUI_USE_EXT_FRAMEBUFFER)
    #define VBOX_GUI_USE_EXT_FRAMEBUFFER
  #endif
#else
  #if defined (VBOX_GUI_USE_EXT_FRAMEBUFFER)
    #undef VBOX_GUI_USE_EXT_FRAMEBUFFER
  #endif
  #if !defined (VBOX_GUI_USE_REFRESH_TIMER)
    #define VBOX_GUI_USE_REFRESH_TIMER
  #endif
#endif

/////////////////////////////////////////////////////////////////////////////

#if defined (VBOX_GUI_DEBUG)

#include <VBox/types.h> // for uint64_t type

#include <qthread.h>
#include <qdatetime.h>

/**
 * A class to measure intervals using rdtsc instruction.
 */
class VMCPUTimer : public QThread // for crossplatform msleep()
{
public:
    inline static uint64_t ticks() {
        return ASMReadTSC();
    }
    inline static uint64_t msecs( uint64_t tcks ) {
        return tcks / ticks_per_msec;
    }
    inline static uint64_t msecsSince( uint64_t tcks ) {
        tcks = ticks() - tcks;
        return tcks / ticks_per_msec;
    }
    inline static void calibrate( int ms )
    {
        QTime t;
        uint64_t tcks = ticks();
        t.start();
        msleep( ms );
        tcks = ticks() - tcks;
        int msecs = t.elapsed();
        ticks_per_msec = tcks / msecs;
    }
    inline static uint64_t ticksPerMsec() {
        return ticks_per_msec;
    }
private:
    static uint64_t ticks_per_msec;
};

#endif // VBOX_GUI_DEBUG

/* A common namespace for all enums */
struct VBoxDefs
{
    /** Disk image type. */
    enum DiskType { InvalidType, HD = 0x01, CD = 0x02, FD = 0x04 };

    /** VM display rendering mode. */
    enum RenderMode {
        InvalidRenderMode, TimerMode, QImageMode, SDLMode, DDRAWMode
    };

    /** Additional Qt event types. */
    enum {
        ResizeEventType = QEvent::User,
        RepaintEventType,
        SetRegionEventType,
        MouseCapabilityEventType,
        MousePointerChangeEventType,
        MachineStateChangeEventType,
        AdditionsStateChangeEventType,
        MachineDataChangeEventType,
        MachineRegisteredEventType,
        SessionStateChangeEventType,
        SnapshotEventType,
        USBDeviceStateChangeEventType,
        RuntimeErrorEventType,
        ModifierKeyChangeEventType,
        EnumerateMediaEventType = QEvent::User + 100,
#if defined (Q_WS_WIN)
        ShellExecuteEventType,
#endif
        ActivateMenuEventType,
#if defined (Q_WS_MAC)
        ShowWindowEventType,
#endif
    };

    static const char* GUI_LastWindowPosition;
    static const char* GUI_LastWindowPosition_Max;
    static const char* GUI_Fullscreen;
    static const char* GUI_Seamless;
    static const char* GUI_AutoresizeGuest;
    static const char* GUI_FirstRun;
    static const char* GUI_SaveMountedAtRuntime;
    static const char* GUI_LastCloseAction;
    static const char* GUI_SuppressMessages;
    static const char* GUI_PermanentSharedFoldersAtRuntime;
};

#endif // __VBoxDefs_h__

