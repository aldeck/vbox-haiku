/** @file
 *
 * VBox frontends: Framebuffer (FB, DirectFB):
 * Implementation of VBoxDirectFB class
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

#include "VBoxFB.h"
#include "Framebuffer.h"

NS_IMPL_ISUPPORTS1_CI(VBoxDirectFB, IFramebuffer)
NS_DECL_CLASSINFO(VBoxDirectFB)

VBoxDirectFB::VBoxDirectFB(IDirectFB *aDFB, IDirectFBSurface *aSurface)
{
    dfb = aDFB;
    surface = aSurface;
    fbInternalSurface = NULL;
    fbBufferAddress = NULL;
    // initialize screen dimensions
    DFBCHECK(surface->GetSize(surface, (int*)&screenWidth, (int*)&screenHeight));
    fbWidth = 640;
    fbHeight = 480;
    if ((screenWidth != fbWidth) || (screenHeight != fbHeight))
    {
        createSurface(fbWidth, fbHeight);
    }
    fbSurfaceLocked = 0;
    uint32_t colorDepth;
    GetColorDepth(&colorDepth);
    fbPitch = fbWidth * (colorDepth / 8);
}

VBoxDirectFB::~VBoxDirectFB()
{
    // free our internal surface
    if (fbInternalSurface)
    {
        DFBCHECK(fbInternalSurface->Release(fbInternalSurface));
        fbInternalSurface = NULL;
    }
}

NS_IMETHODIMP VBoxDirectFB::GetWidth(uint32 *width)
{
    if (!width)
        return NS_ERROR_INVALID_POINTER;
    *width = fbWidth;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetHeight(uint32_t *height)
{
    if (!height)
        return NS_ERROR_INVALID_POINTER;
    *height = fbHeight;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::Lock()
{
    // do we have an internal framebuffer?
    if (fbInternalSurface)
    {
       if (fbSurfaceLocked)
       {
           printf("internal surface already locked!\n");
       } else
       {
           DFBCHECK(fbInternalSurface->Lock(fbInternalSurface,
                                            (DFBSurfaceLockFlags)(DSLF_WRITE | DSLF_READ),
                                            &fbBufferAddress, (int*)&fbPitch));
           fbSurfaceLocked = 1;
       }
    } else
    {
        if (fbSurfaceLocked)
        {
            printf("surface already locked!\n");
        } else
        {
            DFBCHECK(surface->Lock(surface, (DFBSurfaceLockFlags)(DSLF_WRITE | DSLF_READ),
                                   &fbBufferAddress, (int*)&fbPitch));
            fbSurfaceLocked = 1;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::Unlock()
{
    // do we have an internal framebuffer?
    if (fbInternalSurface)
    {
        if (!fbSurfaceLocked)
        {
            printf("internal surface not locked!\n");
        } else
        {
            DFBCHECK(fbInternalSurface->Unlock(fbInternalSurface));
            fbSurfaceLocked = 0;
        }
    } else
    {
        if (!fbSurfaceLocked)
        {
            printf("surface not locked!\n");
        } else
        {
            DFBCHECK(surface->Unlock(surface));
            fbSurfaceLocked = 0;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetAddress(uint32_t *address)
{
    if (!address)
        return NS_ERROR_INVALID_POINTER;
    *address = (uint32_t)fbBufferAddress;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetColorDepth(uint32_t *colorDepth)
{
    if (!colorDepth)
        return NS_ERROR_INVALID_POINTER;
    DFBSurfacePixelFormat pixelFormat;
    DFBCHECK(surface->GetPixelFormat(surface, &pixelFormat));
    switch (pixelFormat)
    {
        case DSPF_RGB16:
            *colorDepth = 16;
            break;
        case DSPF_RGB24:
            *colorDepth = 24;
            break;
        case DSPF_RGB32:
            *colorDepth = 32;
            break;
        default:
            // not good! @@@AH do something!
            *colorDepth = 16;
    }
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::GetLineSize(uint32_t *lineSize)
{
    if (!lineSize)
        return NS_ERROR_INVALID_POINTER;
    *lineSize = fbPitch;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::COMGETTER(PixelFormat) (FramebufferPixelFormat_T *pixelFormat)
{
    if (!lineSize)
        return NS_ERROR_INVALID_POINTER;
    *pixelFormat = FramebufferPixelFormat_PixelFormatOpaque;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::NotifyUpdate(uint32_t x, uint32_t y,
                                         uint32_t w, uint32_t h, PRBool *finished)
{
    // we only need to take action if we have a memory framebuffer
    if (fbInternalSurface)
    {
        //printf("blitting %u %u %u %u...\n", x, y, w, h);
        DFBRectangle blitRectangle;
        blitRectangle.x = x;
        blitRectangle.y = y;
        blitRectangle.w = w;
        blitRectangle.h = h;
        if (scaleGuest)
        {
            DFBRectangle hostRectangle;
            float factorX = (float)screenWidth / (float)fbWidth;
            float factorY = (float)screenHeight / (float)fbHeight;
            hostRectangle.x = (int)((float)blitRectangle.x * factorX);
            hostRectangle.y = (int)((float)blitRectangle.y * factorY);
            hostRectangle.w = (int)((float)blitRectangle.w * factorX);
            hostRectangle.h = (int)((float)blitRectangle.h * factorY);
            DFBCHECK(surface->StretchBlit(surface, fbInternalSurface,
                                          &blitRectangle, &hostRectangle));
        } else
        {
            DFBCHECK(surface->Blit(surface, fbInternalSurface, &blitRectangle,
                                   x + ((screenWidth - fbWidth) / 2),
                                   y + (screenHeight - fbHeight) / 2));
        }
    }
    if (finished)
        *finished = true;
    return NS_OK;
}

NS_IMETHODIMP VBoxDirectFB::RequestResize(ULONG aScreenId, FramebufferPixelFormat_T pixelFormat, uint32_t vram, uint32_t lineSize, uint32_t w, uint32_t h,
                                           PRBool *finished)
{
    uint32_t needsLocking = fbSurfaceLocked;
    uint32_t colorDepth;

    GetColorDepth(&colorDepth);
    printf("RequestResize: w = %d, h = %d, fbSurfaceLocked = %d\n", w, h, fbSurfaceLocked);

    // we can't work with a locked surface
    if (needsLocking)
    {
        Unlock();
    }

    // in any case we gotta free a possible internal framebuffer
    if (fbInternalSurface)
    {
        printf("freeing internal surface\n");
        fbInternalSurface->Release(fbInternalSurface);
        fbInternalSurface = NULL;
    }

    // check if we have a fixed host video mode
    if (useFixedVideoMode)
    {
        // does the current video mode differ from what the guest wants?
        if ((screenWidth == w) && (screenHeight == h))
        {
            printf("requested guest mode matches current host mode!\n");
        } else
        {
            createSurface(w, h);
        }
    } else
    {
        // we adopt to the guest resolution or the next higher that is available
        int32_t bestMode = getBestVideoMode(w, h, colorDepth);
        if (bestMode == -1)
        {
            // oh oh oh oh
            printf("RequestResize: no suitable mode found!\n");
            return NS_OK;
        }

        // does the mode differ from what we wanted?
        if ((videoModes[bestMode].width != w) || (videoModes[bestMode].height != h) ||
            (videoModes[bestMode].bpp != colorDepth))
        {
            printf("The mode does not fit exactly!\n");
            createSurface(w, h);
        } else
        {
            printf("The mode fits exactly!\n");
        }
        // switch to this mode
        DFBCHECK(dfb->SetVideoMode(dfb, videoModes[bestMode].width, videoModes[bestMode].height,
                                   videoModes[bestMode].bpp));
    }

    // update dimensions to the new size
    fbWidth = w;
    fbHeight = h;

    // clear the screen
    DFBCHECK(surface->Clear(surface, 0, 0, 0, 0));

    // if it was locked before the resize, obtain the lock again
    if (needsLocking)
    {
        Lock();
    }

    if (finished)
        *finished = true;
    return NS_OK;
}

int VBoxDirectFB::createSurface(uint32_t w, uint32_t h)
{
    printf("creating a new internal surface, w = %u, h = %u...\n", w, h);
    // create a surface
    DFBSurfaceDescription dsc;
    DFBSurfacePixelFormat pixelFormat;
    dsc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT);
    dsc.width = w;
    dsc.height = h;
    DFBCHECK(surface->GetPixelFormat(surface, &pixelFormat));
    dsc.pixelformat = pixelFormat;
    DFBCHECK(dfb->CreateSurface(dfb, &dsc, &fbInternalSurface));
    return 0;
}
