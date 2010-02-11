/* $Id$ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Seamless mode.
 */

/*
 * Copyright (C) 2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/assert.h>
#include <iprt/string.h>

#include <VBox/VMMDev.h>
#include <VBox/log.h>

#include "VBGLR3Internal.h"


/**
 * Tell the host that we support (or no longer support) seamless mode.
 *
 * @returns IPRT status value
 * @param   fState whether or not we support seamless mode
 */
VBGLR3DECL(int) VbglR3SeamlessSetCap(bool fState)
{
    if (fState)
        return VbglR3SetGuestCaps(VMMDEV_GUEST_SUPPORTS_SEAMLESS, 0);
    return VbglR3SetGuestCaps(0, VMMDEV_GUEST_SUPPORTS_SEAMLESS);
}

/**
 * Wait for a seamless mode change event.
 *
 * @returns IPRT status value
 * @retval  pMode on success, the seamless mode to switch into (i.e. disabled, visible region
 *                or host window)
 */
VBGLR3DECL(int) VbglR3SeamlessWaitEvent(VMMDevSeamlessMode *pMode)
{
    VBoxGuestWaitEventInfo waitEvent;
    int rc;

    AssertPtrReturn(pMode, VERR_INVALID_PARAMETER);
    waitEvent.u32TimeoutIn = RT_INDEFINITE_WAIT;
    waitEvent.u32EventMaskIn = VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;
    waitEvent.u32Result = VBOXGUEST_WAITEVENT_ERROR;
    waitEvent.u32EventFlagsOut = 0;
    rc = vbglR3DoIOCtl(VBOXGUEST_IOCTL_WAITEVENT, &waitEvent, sizeof(waitEvent));
    if (RT_SUCCESS(rc))
    {
        /* did we get the right event? */
        if (waitEvent.u32EventFlagsOut & VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST)
        {
            VMMDevSeamlessChangeRequest seamlessChangeRequest;

            /* get the seamless change request */
            vmmdevInitRequest(&seamlessChangeRequest.header, VMMDevReq_GetSeamlessChangeRequest);
            seamlessChangeRequest.eventAck = VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;
            rc = vbglR3GRPerform(&seamlessChangeRequest.header);
            if (RT_SUCCESS(rc))
            {
                *pMode = seamlessChangeRequest.mode;
                return VINF_SUCCESS;
            }
        }
        else
            rc = VERR_TRY_AGAIN;
    }
    return rc;
}

/**
 * Inform the host about the visible region
 *
 * @returns IPRT status code
 * @param   cRects number of rectangles in the list of visible rectangles
 * @param   pRects list of visible rectangles on the guest display
 *
 * @todo A scatter-gather version of vbglR3GRPerform would be nice, so that we don't have
 *       to copy our rectangle and header data into a single structure and perform an
 *       additional allocation.
 */
VBGLR3DECL(int) VbglR3SeamlessSendRects(uint32_t cRects, PRTRECT pRects)
{
    VMMDevVideoSetVisibleRegion *pReq;
    int rc;

    if (!cRects || !pRects)
        return VINF_SUCCESS;
    rc = vbglR3GRAlloc((VMMDevRequestHeader **)&pReq,
                       sizeof(VMMDevVideoSetVisibleRegion) + (cRects - 1) * sizeof(RTRECT),
                       VMMDevReq_VideoSetVisibleRegion);
    if (RT_SUCCESS(rc))
    {
        pReq->cRect = cRects;
        memcpy(&pReq->Rect, pRects, cRects * sizeof(RTRECT));
        rc = vbglR3GRPerform(&pReq->header);
        if (RT_SUCCESS(rc))
            rc = pReq->header.rc;
        vbglR3GRFree(&pReq->header);
    }
    return rc;
}

