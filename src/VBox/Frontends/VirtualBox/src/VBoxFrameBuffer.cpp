/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxFrameBuffer class and subclasses implementation
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

#include "VBoxFrameBuffer.h"

#include "VBoxConsoleView.h"

#include <qapplication.h>

//
// VBoxFrameBuffer class
/////////////////////////////////////////////////////////////////////////////

/** @class VBoxFrameBuffer
 *
 *  Base class for all frame buffer implementations.
 */

#if !defined (Q_OS_WIN32)
NS_DECL_CLASSINFO (VBoxFrameBuffer)
NS_IMPL_ISUPPORTS1_CI (VBoxFrameBuffer, IFramebuffer)
#endif

VBoxFrameBuffer::VBoxFrameBuffer (VBoxConsoleView *aView)
    : mView (aView), mMutex (new QMutex (true))
    , mWdt (0), mHgt (0)
#if defined (Q_OS_WIN32)
    , refcnt (0)
#endif
{
    AssertMsg (mView, ("VBoxConsoleView must not be null\n"));

    /* Default framebuffer render mode is normal (draw the entire framebuffer) */
    mRenderMode = RenderModeNormal;
}

VBoxFrameBuffer::~VBoxFrameBuffer()
{
    delete mMutex;
}

// IFramebuffer implementation methods.
// Just forwarders to relevant class methods.

STDMETHODIMP VBoxFrameBuffer::COMGETTER(Address) (BYTE **aAddress)
{
    if (!aAddress)
        return E_POINTER;
    *aAddress = address();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(Width) (ULONG *aWidth)
{
    if (!aWidth)
        return E_POINTER;
    *aWidth = (ULONG) width();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(Height) (ULONG *aHeight)
{
    if (!aHeight)
        return E_POINTER;
    *aHeight = (ULONG) height();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(ColorDepth) (ULONG *aColorDepth)
{
    if (!aColorDepth)
        return E_POINTER;
    *aColorDepth = (ULONG) colorDepth();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(LineSize) (ULONG *aLineSize)
{
    if (!aLineSize)
        return E_POINTER;
    *aLineSize = (ULONG) lineSize();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(PixelFormat) (
    FramebufferPixelFormat_T *aPixelFormat)
{
    if (!aPixelFormat)
        return E_POINTER;
    *aPixelFormat = pixelFormat();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(HeightReduction) (ULONG *aHeightReduction)
{
    if (!aHeightReduction)
        return E_POINTER;
    /* no reduction */
    *aHeightReduction = 0;
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::COMGETTER(Overlay) (IFramebufferOverlay **aOverlay)
{
    if (!aOverlay)
        return E_POINTER;
    /* not yet implemented */
    *aOverlay = 0;
    return S_OK;
}

/**
 * Return the current framebuffer render mode
 *
 * @returns COM status code
 * @param   renderMode  framebuffer render mode
 */
STDMETHODIMP VBoxFrameBuffer::COMGETTER(RenderMode) (FramebufferRenderMode_T *renderMode)
{
    if (!renderMode)
        return E_POINTER;
    *renderMode = mRenderMode;
    return S_OK;
}

/**
 * Change the current framebuffer render mode
 *
 * @returns COM status code
 * @param   renderMode  framebuffer render mode
 */
STDMETHODIMP VBoxFrameBuffer::COMSETTER(RenderMode) (FramebufferRenderMode_T renderMode)
{
    if (!renderMode)
        return E_POINTER;
    mRenderMode = renderMode;
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::Lock()
{
    this->lock();
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::Unlock()
{
    this->unlock();
    return S_OK;
}

/** @note This method is called on EMT from under this object's lock */
STDMETHODIMP VBoxFrameBuffer::RequestResize (ULONG aScreenId, FramebufferPixelFormat_T aPixelFormat,
                                             BYTE *aVRAM, ULONG aLineSize,
                                             ULONG aWidth, ULONG aHeight,
                                             BOOL *aFinished)
{
    QApplication::postEvent (mView,
                             new VBoxResizeEvent (aPixelFormat, aVRAM,
                                                  aLineSize, aWidth, aHeight));

#ifdef DEBUG_sunlover
    Log(("VBoxFrameBuffer::RequestResize: pixelFormat %d, vram %p, lineSize %d, w %d, h %d\n",
          aPixelFormat, aVRAM, aLineSize, aWidth, aHeight));
#endif /* DEBUG_sunlover */

    /*
     *  the resize has been postponed, return FALSE
     *  to cause the VM thread to sleep until IDisplay::ResizeFinished()
     *  is called from the VBoxResizeEvent event handler.
     */

    *aFinished = FALSE;
    return S_OK;
}

STDMETHODIMP
VBoxFrameBuffer::OperationSupported (FramebufferAccelerationOperation_T aOperation,
                                     BOOL *aSupported)
{
    NOREF(aOperation);
    if (!aSupported)
        return E_POINTER;
    *aSupported = FALSE;
    return S_OK;
}

/**
 * Returns whether we like the given video mode.
 *
 * @returns COM status code
 * @param   width     video mode width in pixels
 * @param   height    video mode height in pixels
 * @param   bpp       video mode bit depth in bits per pixel
 * @param   supported pointer to result variable
 */
STDMETHODIMP VBoxFrameBuffer::VideoModeSupported (ULONG aWidth, ULONG aHeight,
                                                  ULONG aBPP, BOOL *aSupported)
{
    NOREF(aWidth);
    NOREF(aHeight);
    NOREF(aBPP);
    if (!aSupported)
        return E_POINTER;
    /* for now, we swallow everything */
    *aSupported = TRUE;
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::SolidFill (ULONG aX, ULONG aY,
                                         ULONG aWidth, ULONG aHeight,
                                         ULONG aColor, BOOL *aHandled)
{
    NOREF(aX);
    NOREF(aY);
    NOREF(aWidth);
    NOREF(aHeight);
    NOREF(aColor);
    if (!aHandled)
        return E_POINTER;
    *aHandled = FALSE;
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::CopyScreenBits (ULONG aXDst, ULONG aYDst,
                                              ULONG aXSrc, ULONG aYSrc,
                                              ULONG aWidth, ULONG aHeight,
                                              BOOL *aHandled)
{
    NOREF(aXDst);
    NOREF(aYDst);
    NOREF(aXSrc);
    NOREF(aYSrc);
    NOREF(aWidth);
    NOREF(aHeight);
    if (!aHandled)
        return E_POINTER;
    *aHandled = FALSE;
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::GetVisibleRegion(ULONG * aPcRect, BYTE * aPRect)
{
    PRTRECT paRect = (PRTRECT)aPRect;

    if (!aPcRect)
        return E_POINTER;

    /* @todo */
    return S_OK;
}

STDMETHODIMP VBoxFrameBuffer::SetVisibleRegion(ULONG aCRect, BYTE * aPRect)
{
    PRTRECT paRect = (PRTRECT)aPRect;

    if (!paRect)
        return E_POINTER;

    /* @todo */
    return S_OK;
}

//
// VBoxQImageFrameBuffer class
/////////////////////////////////////////////////////////////////////////////

#if defined (VBOX_GUI_USE_QIMAGE)

/** @class VBoxQImageFrameBuffer
 *
 *  The VBoxQImageFrameBuffer class is a class that implements the IFrameBuffer
 *  interface and uses QImage as the direct storage for VM display data. QImage
 *  is then converted to QPixmap and blitted to the console view widget.
 */

VBoxQImageFrameBuffer::VBoxQImageFrameBuffer (VBoxConsoleView *aView) :
    VBoxFrameBuffer (aView)
{
    resizeEvent (new VBoxResizeEvent (FramebufferPixelFormat_PixelFormatDefault,
                                      NULL, 0, 640, 480));
}

/** @note This method is called on EMT from under this object's lock */
STDMETHODIMP VBoxQImageFrameBuffer::NotifyUpdate (ULONG aX, ULONG aY,
                                                  ULONG aW, ULONG aH,
                                                  BOOL *aFinished)
{
#ifdef Q_WS_MAC
    /* we're not on the GUI thread and update() isn't thread safe on Qt 3.3.x
       on the Mac (4.2.x is), so post the event instead.  */
    QApplication::postEvent (mView,
                             new VBoxRepaintEvent (aX, aY, aW, aH));

#else /* !Q_WS_MAC */
    /* we're not on the GUI thread, so update() instead of repaint()! */
    mView->viewport()->update (aX - mView->contentsX(),
                               aY - mView->contentsY(),
                               aW, aH);
#endif /* !Q_WS_MAC */
    /* the update has been finished, return TRUE */
    *aFinished = TRUE;
    return S_OK;
}

void VBoxQImageFrameBuffer::paintEvent (QPaintEvent *pe)
{
    const QRect &r = pe->rect().intersect (mView->viewport()->rect());

    /*  some outdated rectangle during processing VBoxResizeEvent */
    if (r.isEmpty())
        return;

//    LogFlowFunc (("%d,%d-%d,%d (img: %d,%d)\n",
//                  r.x(), r.y(), r.width(), r.height(),
//                  img.width(), img.height()));

    FRAMEBUF_DEBUG_START (xxx);

    if (r.width() < mWdt * 2 / 3)
    {
        /* this method is faster for narrow updates */
        mPM.convertFromImage (mImg.copy (r.x() + mView->contentsX(),
                                         r.y() + mView->contentsY(),
                                         r.width(), r.height()));

        ::bitBlt (mView->viewport(), r.x(), r.y(),
                  &mPM, 0, 0,
                  r.width(), r.height(),
                  Qt::CopyROP, TRUE);
    }
    else
    {
        /* this method is faster for wide updates */
        mPM.convertFromImage (QImage (mImg.scanLine (r.y() + mView->contentsY()),
                                      mImg.width(), r.height(), mImg.depth(),
                                      0, 0, QImage::LittleEndian));

        ::bitBlt (mView->viewport(), r.x(), r.y(),
                  &mPM, r.x() + mView->contentsX(), 0,
                  r.width(), r.height(),
                  Qt::CopyROP, TRUE);
    }

    FRAMEBUF_DEBUG_STOP (xxx, r.width(), r.height());
}

void VBoxQImageFrameBuffer::resizeEvent (VBoxResizeEvent *re)
{
    mWdt = re->width();
    mHgt = re->height();

    mImg = QImage (mWdt, mHgt, 32, 0, QImage::LittleEndian);
}

#endif

//
// VBoxSDLFrameBuffer class
/////////////////////////////////////////////////////////////////////////////

#if defined (VBOX_GUI_USE_SDL)

/** @class VBoxSDLFrameBuffer
 *
 *  The VBoxSDLFrameBuffer class is a class that implements the IFrameBuffer
 *  interface and uses SDL to store and render VM display data.
 */

VBoxSDLFrameBuffer::VBoxSDLFrameBuffer (VBoxConsoleView *aView) :
    VBoxFrameBuffer (aView)
{
    mScreen = NULL;
    mPixelFormat = FramebufferPixelFormat_PixelFormatDefault;
    mPtrVRAM = NULL;
    mSurfVRAM = NULL;
    mLineSize = 0;

    resizeEvent (new VBoxResizeEvent (FramebufferPixelFormat_PixelFormatDefault,
                                      NULL, 0, 640, 480));
}

VBoxSDLFrameBuffer::~VBoxSDLFrameBuffer()
{
    if (mSurfVRAM)
    {
        SDL_FreeSurface(mSurfVRAM);
        mSurfVRAM = NULL;
    }
    SDL_QuitSubSystem (SDL_INIT_VIDEO);
}

/** @note This method is called on EMT from under this object's lock */
STDMETHODIMP VBoxSDLFrameBuffer::NotifyUpdate (ULONG aX, ULONG aY,
                                               ULONG aW, ULONG aH,
                                               BOOL *aFinished)
{
    /* we're not on the GUI thread, so update() instead of repaint()! */
    mView->viewport()->update (aX - mView->contentsX(),
                               aY - mView->contentsY(),
                               aW, aH);
    /* the update has been finished, return TRUE */
    *aFinished = TRUE;
    return S_OK;
}

void VBoxSDLFrameBuffer::paintEvent (QPaintEvent *pe)
{
    if (mScreen)
    {
#ifdef Q_WS_X11
        /* make sure we don't conflict with Qt's drawing */
        //XSync(QPaintDevice::x11Display(), FALSE);
#endif
        if (mScreen->pixels)
        {
            QRect r = pe->rect();

            if (mSurfVRAM)
            {
                SDL_Rect rect = { (Sint16) r.x(), (Sint16) r.y(),
                                  (Sint16) r.width(), (Sint16) r.height() };
                SDL_BlitSurface (mSurfVRAM, &rect, mScreen, &rect);
                /** @todo may be: if ((mScreen->flags & SDL_HWSURFACE) == 0) */
                SDL_UpdateRect (mScreen, r.x(), r.y(), r.width(), r.height());
            }
            else
            {
                SDL_UpdateRect (mScreen, r.x(), r.y(), r.width(), r.height());
            }
        }
    }
}

void VBoxSDLFrameBuffer::resizeEvent( VBoxResizeEvent *re )
{
    mWdt = re->width();
    mHgt = re->height();

    /* close SDL so we can init it again */
    if (mSurfVRAM)
    {
        SDL_FreeSurface(mSurfVRAM);
        mSurfVRAM = NULL;
    }
    if (mScreen)
    {
        SDL_QuitSubSystem (SDL_INIT_VIDEO);
        mScreen = NULL;
    }

    /*
     *  initialize the SDL library, use its super hack to integrate it with our
     *  client window
     */
    static char sdlHack[64];
    sprintf (sdlHack, "SDL_WINDOWID=0x%lx", mView->viewport()->winId());
    putenv (sdlHack);
    int rc = SDL_InitSubSystem (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
    AssertMsg (rc == 0, ("SDL initialization failed!\n"));
    NOREF(rc);

#ifdef Q_WS_X11
    /* undo signal redirections from SDL, it'd steal keyboard events from us! */
    signal (SIGINT, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
#endif

    LogFlowFunc (("Setting SDL video mode to %d x %d\n", mWdt, mHgt));

    mPixelFormat = re->pixelFormat();
    mPtrVRAM = re->vram();
    mLineSize = re->lineSize();

    int bitsPerPixel = 0;

    Uint32 Rmask = 0;
    Uint32 Gmask = 0;
    Uint32 Bmask = 0;
    Uint32 Amask = 0;

    switch (mPixelFormat)
    {
        case FramebufferPixelFormat_PixelFormatRGB32:
        {
            bitsPerPixel = 32;
            Rmask = 0x00FF0000;
            Gmask = 0x0000FF00;
            Bmask = 0x000000FF;
        } break;

        case FramebufferPixelFormat_PixelFormatRGB24:
        {
            bitsPerPixel = 24;
            Rmask = 0x00FF0000;
            Gmask = 0x0000FF00;
            Bmask = 0x000000FF;
        } break;

        case FramebufferPixelFormat_PixelFormatRGB16:
        {
            bitsPerPixel = 16;
            Rmask = 0xF800;
            Gmask = 0x07E0;
            Bmask = 0x001F;
        } break;

        default:
        {
            /* Unsupported format leads to use of the default format. */
            mPixelFormat = FramebufferPixelFormat_PixelFormatDefault;
        }
    }

    if (mPixelFormat != FramebufferPixelFormat_PixelFormatDefault)
    {
        /* Create a source surface from guest VRAM. */
        mSurfVRAM = SDL_CreateRGBSurfaceFrom(mPtrVRAM, mWdt, mHgt, bitsPerPixel,
                                             mLineSize, Rmask, Gmask, Bmask, Amask);
        LogFlowFunc (("Created VRAM surface %p\n", mSurfVRAM));
    }

    mScreen = SDL_SetVideoMode (mWdt, mHgt, 0,
                                SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL);
    AssertMsg (mScreen, ("SDL video mode could not be set!\n"));
}

#endif

//
// VBoxDDRAWFrameBuffer class
/////////////////////////////////////////////////////////////////////////////

#if defined (VBOX_GUI_USE_DDRAW)

/* The class is defined in VBoxFBDDRAW.cpp */

#endif
