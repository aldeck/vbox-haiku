/** @file
 * VBoxGraphics - VirtualBox Win 2000/XP guest video driver.
 *
 * Display driver entry points.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBOXIOCTL__H
#define __VBOXIOCTL__H

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>

#include <VBox/HGSMI/HGSMI.h>
#include "VBoxHGSMI.h"

#define IOCTL_VIDEO_INTERPRET_DISPLAY_MEMORY \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x420, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_QUERY_DISPLAY_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x421, METHOD_BUFFERED, FILE_ANY_ACCESS)

/** Called by the display driver when it is ready to
 *  switch to VBVA operation mode.
 *  Successful return means that VBVA can be used and
 *  output buffer contains VBVAENABLERESULT data.
 *  An error means that VBVA can not be used
 *  (disabled or not supported by the host).
 */
#define IOCTL_VIDEO_VBVA_ENABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x400, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_QUERY_HGSMI_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x430, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x431, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_HGSMI_HANDLER_ENABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x432, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_HGSMI_HANDLER_DISABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x433, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x434, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_VHWA_QUERY_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x435, METHOD_BUFFERED, FILE_ANY_ACCESS)


#pragma pack(1)
/**
 * Data returned by IOCTL_VIDEO_VBVA_ENABLE.
 *
 */
typedef struct _VBVAENABLERESULT
{
    /** Pointer to VBVAMemory part of VMMDev memory region. */
    VBVAMEMORY *pVbvaMemory;

    /** Called to force the host to process VBVA memory,
     *  when there is no more free space in VBVA memory.
     *  Normally this never happens.
     *
     *  The other purpose is to perform a synchronous command.
     *  But the goal is to have no such commands at all.
     */
    DECLR0CALLBACKMEMBER(void, pfnFlush, (void *pvFlush));

    /** Pointer required by the pfnFlush callback. */
    void *pvFlush;

} VBVAENABLERESULT;

/**
 * Data returned by IOCTL_VIDEO_QUERY_DISPLAY_INFO.
 *
 */
typedef struct _QUERYDISPLAYINFORESULT
{
    /* Device index (0 for primary) */
    ULONG iDevice;

    /* Size of the display information area. */
    uint32_t u32DisplayInfoSize;
} QUERYDISPLAYINFORESULT;

/**
 * Data returned by IOCTL_VIDEO_QUERY_HGSMI_INFO.
 *
 */
typedef struct _QUERYHGSMIRESULT
{
    /* Device index (0 for primary) */
    ULONG iDevice;

    /* Flags. Currently none are defined and the field must be initialized to 0. */
    ULONG ulFlags;

    /* Describes VRAM chunk for this display device. */
    HGSMIAREA areaDisplay;

    /* Size of the display information area. */
    uint32_t u32DisplayInfoSize;

    /* Minimum size of the VBVA buffer. */
    uint32_t u32MinVBVABufferSize;

    /* IO port to submit guest HGSMI commands. */
    RTIOPORT IOPortGuestCommand;
} QUERYHGSMIRESULT;

/**
 * Data returned by IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS.
 *
 */
typedef struct _HGSMIQUERYCALLBACKS
{
    HVBOXVIDEOHGSMI hContext;
    PFNVBOXVIDEOHGSMICOMPLETION pfnCompletionHandler;
    PFNVBOXVIDEOHGSMICOMMANDS   pfnRequestCommandsHandler;
} HGSMIQUERYCALLBACKS;

/**
 * Data returned by IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS
 */
typedef struct _HGSMIQUERYCPORTPROCS
{
    PVOID pContext;
    VBOXVIDEOPORTPROCS VideoPortProcs;
} HGSMIQUERYCPORTPROCS;

/**
 * Data returned by IOCTL_VIDEO_HGSMI_HANDLER_ENABLE.
 *
 */
typedef struct _HGSMIHANDLERENABLE
{
    uint8_t u8Channel;
} HGSMIHANDLERENABLE;

/**
 * Data passed by IOCTL_VIDEO_HGSMI_HANDLER_DISABLE.
 *
 */
typedef struct _HGSMIHANDLERDISABLE
{
    uint8_t u8Channel;
} HGSMIHANDLERDISABLE;

#ifdef VBOX_WITH_VIDEOHWACCEL
typedef struct _VHWAQUERYINFO
{
    ULONG_PTR offVramBase;
} VHWAQUERYINFO;
#endif

#pragma pack()

#endif /* __VBOXIOCTL__H */
