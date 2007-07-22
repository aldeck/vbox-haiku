/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef ____H_FRAMEBUFFERIMPL
#define ____H_FRAMEBUFFERIMPL

#include "VirtualBoxBase.h"


class ATL_NO_VTABLE InternalFramebuffer :
    public VirtualBoxBase,
    public IFramebuffer
{
public:
    InternalFramebuffer();
    virtual ~InternalFramebuffer();

    DECLARE_NOT_AGGREGATABLE(InternalFramebuffer)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(InternalFramebuffer)
        COM_INTERFACE_ENTRY(IFramebuffer)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    // public methods only for internal purposes
    HRESULT init (ULONG width, ULONG height, ULONG depth);

    // IFramebuffer properties
    STDMETHOD(COMGETTER(Address)) (BYTE **address);
    STDMETHOD(COMGETTER(Width)) (ULONG *width);
    STDMETHOD(COMGETTER(Height)) (ULONG *height);
    STDMETHOD(COMGETTER(BitsPerPixel)) (ULONG *bitsPerPixel);
    STDMETHOD(COMGETTER(BytesPerLine)) (ULONG *bytesPerLine);
    STDMETHOD(COMGETTER(PixelFormat)) (ULONG *pixelFormat);
    STDMETHOD(COMGETTER(UsesGuestVRAM)) (BOOL *usesGuestVRAM);
    STDMETHOD(COMGETTER(HeightReduction)) (ULONG *heightReduction);
    STDMETHOD(COMGETTER(Overlay)) (IFramebufferOverlay **aOverlay);

    // IFramebuffer methods
    STDMETHOD(Lock)();
    STDMETHOD(Unlock)();
    STDMETHOD(NotifyUpdate)(ULONG x, ULONG y,
                            ULONG w, ULONG h,
                            BOOL *finished);
    STDMETHOD(RequestResize)(ULONG uScreenId, ULONG pixelFormat, BYTE *vram,
                             ULONG bpp, ULONG bpl, ULONG w, ULONG h,
                             BOOL *finished);
    STDMETHOD(OperationSupported)(FramebufferAccelerationOperation_T operation,
                                  BOOL *supported);
    STDMETHOD(VideoModeSupported)(ULONG width, ULONG height, ULONG bpp, BOOL *supported);
    STDMETHOD(SolidFill)(ULONG x, ULONG y, ULONG width, ULONG height,
                         ULONG color, BOOL *handled);
    STDMETHOD(CopyScreenBits)(ULONG xDst, ULONG yDst, ULONG xSrc, ULONG ySrc,
                              ULONG width, ULONG height, BOOL *handled);

    STDMETHOD(GetVisibleRegion)(BYTE *aRectangles, ULONG aCount, ULONG *aCountCopied);
    STDMETHOD(SetVisibleRegion)(BYTE *aRectangles, ULONG aCount);

private:
    int mWidth;
    int mHeight;
    int mBitsPerPixel;
    int mBytesPerLine;
    uint8_t *mData;
    RTSEMMUTEX mMutex;
};


#endif // ____H_FRAMEBUFFERIMPL
