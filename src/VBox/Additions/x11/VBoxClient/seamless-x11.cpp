/** @file
 * X11 Seamless mode.
 */

/*
 * Copyright (C) 2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*****************************************************************************
*   Header files                                                             *
*****************************************************************************/

#include <iprt/err.h>
#include <iprt/assert.h>
#include <VBox/log.h>

#include "seamless-guest.h"

#include <X11/Xatom.h>
#include <X11/Xmu/WinUtil.h>

#include <limits.h>

#ifdef TESTCASE
#undef DefaultRootWindow
#define DefaultRootWindow XDefaultRootWindow
#endif

/*****************************************************************************
* Static functions                                                           *
*****************************************************************************/

static unsigned char *XXGetProperty (Display *aDpy, Window aWnd, Atom aPropType,
                                    const char *aPropName, unsigned long *nItems)
{
    LogRelFlowFunc(("\n"));
    Atom propNameAtom = XInternAtom (aDpy, aPropName,
                                     True /* only_if_exists */);
    if (propNameAtom == None)
    {
        return NULL;
    }

    Atom actTypeAtom = None;
    int actFmt = 0;
    unsigned long nBytesAfter = 0;
    unsigned char *propVal = 0;
    int rc = XGetWindowProperty (aDpy, aWnd, propNameAtom,
                                 0, LONG_MAX, False /* delete */,
                                 aPropType, &actTypeAtom, &actFmt,
                                 nItems, &nBytesAfter, &propVal);
    if (rc != Success)
        return NULL;

    LogRelFlowFunc(("returning\n"));
    return propVal;
}

/**
  * Initialise the guest and ensure that it is capable of handling seamless mode
  *
  * @returns true if it can handle seamless, false otherwise
  */
int VBoxGuestSeamlessX11::init(VBoxGuestSeamlessObserver *pObserver)
{
    int rc = VINF_SUCCESS;

    LogRelFlowFunc(("\n"));
    if (0 != mObserver)  /* Assertion */
    {
        LogRel(("VBoxClient: ERROR: attempt to initialise seamless guest object twice!\n"));
        return VERR_INTERNAL_ERROR;
    }
    if (!mDisplay.init())
    {
        LogRel(("VBoxClient: seamless guest object failed to acquire a connection to the display.\n"));
        return VERR_ACCESS_DENIED;
    }
    mObserver = pObserver;
    LogRelFlowFunc(("returning %Rrc\n", rc));
    return rc;
}

/**
 * Read information about currently visible windows in the guest and subscribe to X11
 * events about changes to this information.
 *
 * @note This class does not contain its own event thread, so an external thread must
 *       call nextEvent() for as long as events are wished.
 * @todo This function should switch the guest to fullscreen mode.
 */
int VBoxGuestSeamlessX11::start(void)
{
    int rc = VINF_SUCCESS;
    /** Dummy values for XShapeQueryExtension */
    int error, event;

    LogRelFlowFunc(("\n"));
    mSupportsShape = XShapeQueryExtension(mDisplay, &event, &error);
    mEnabled = true;
    monitorClientList();
    rebuildWindowTree();
    LogRelFlowFunc(("returning %Rrc\n", rc));
    return rc;
}

/** Stop reporting seamless events to the host.  Free information about guest windows
    and stop requesting updates. */
void VBoxGuestSeamlessX11::stop(void)
{
    LogRelFlowFunc(("\n"));
    mEnabled = false;
    unmonitorClientList();
    freeWindowTree();
    LogRelFlowFunc(("returning\n"));
}

void VBoxGuestSeamlessX11::monitorClientList(void)
{
    LogRelFlowFunc(("called\n"));
    XSelectInput(mDisplay, DefaultRootWindow(mDisplay.get()), SubstructureNotifyMask);
}

void VBoxGuestSeamlessX11::unmonitorClientList(void)
{
    LogRelFlowFunc(("called\n"));
    XSelectInput(mDisplay, DefaultRootWindow(mDisplay.get()), 0);
}

/**
 * Recreate the table of toplevel windows of clients on the default root window of the
 * X server.
 */
void VBoxGuestSeamlessX11::rebuildWindowTree(void)
{
    LogRelFlowFunc(("called\n"));
    freeWindowTree();
    addClients(DefaultRootWindow(mDisplay.get()));
    mChanged = true;
}


/**
 * Look at the list of children of a virtual root window and add them to the list of clients
 * if they belong to a client which is not a virtual root.
 *
 * @param hRoot the virtual root window to be examined
 */
void VBoxGuestSeamlessX11::addClients(const Window hRoot)
{
    /** Unused out parameters of XQueryTree */
    Window hRealRoot, hParent;
    /** The list of children of the root supplied, raw pointer */
    Window *phChildrenRaw;
    /** The list of children of the root supplied, auto-pointer */
    VBoxGuestX11Pointer<Window> phChildren;
    /** The number of children of the root supplied */
    unsigned cChildren;

    LogRelFlowFunc(("\n"));
    if (!XQueryTree(mDisplay.get(), hRoot, &hRealRoot, &hParent, &phChildrenRaw, &cChildren))
        return;
    phChildren = phChildrenRaw;
    for (unsigned i = 0; i < cChildren; ++i)
        addClientWindow(phChildren.get()[i]);
    LogRelFlowFunc(("returning\n"));
}


void VBoxGuestSeamlessX11::addClientWindow(const Window hWin)
{
    LogRelFlowFunc(("\n"));
    XWindowAttributes winAttrib;
    bool fAddWin = true;
    char *pszWinName = NULL;
    Window hClient = XmuClientWindow(mDisplay, hWin);

    if (isVirtualRoot(hClient))
        fAddWin = false;
    if (fAddWin && !XGetWindowAttributes(mDisplay, hWin, &winAttrib))
    {
        LogRelFunc(("VBoxClient: Failed to get the window attributes for window %d\n", hWin));
        fAddWin = false;
    }
    if (fAddWin && (winAttrib.map_state == IsUnmapped))
        fAddWin = false;
    XSizeHints dummyHints;
    long dummyLong;
    if (fAddWin && (!XGetWMNormalHints(mDisplay, hClient, &dummyHints,
                                       &dummyLong)))
    {
        LogRelFlowFunc(("window %lu, client window %lu has no size hints\n",
                     hWin, hClient));
        fAddWin = false;
    }
    if (fAddWin)
    {
        VBoxGuestX11Pointer<XRectangle> rects;
        int cRects = 0, iOrdering;
        bool hasShape = false;

        LogRelFlowFunc(("adding window %lu, client window %lu\n", hWin,
                     hClient));
        if (mSupportsShape)
        {
            XShapeSelectInput(mDisplay, hWin, ShapeNotifyMask);
            rects = XShapeGetRectangles(mDisplay, hWin, ShapeBounding, &cRects, &iOrdering);
            if (0 == rects.get())
                cRects = 0;
            else
            {
                if (   (cRects > 1)
                    || (rects.get()[0].x != 0)
                    || (rects.get()[0].y != 0)
                    || (rects.get()[0].width != winAttrib.width)
                    || (rects.get()[0].height != winAttrib.height)
                   )
                    hasShape = true;
            }
        }
        mGuestWindows.addWindow(hWin, hasShape, winAttrib.x, winAttrib.y,
                                winAttrib.width, winAttrib.height, cRects, rects);
    }
    LogRelFlowFunc(("returning\n"));
}


/**
 * Checks whether a window is a virtual root.
 * @returns true if it is, false otherwise
 * @param hWin the window to be examined
 */
bool VBoxGuestSeamlessX11::isVirtualRoot(Window hWin)
{
    unsigned char *windowTypeRaw;
    VBoxGuestX11Pointer<Atom> windowType;
    unsigned long ulCount;
    bool rc = false;

    LogRelFlowFunc(("\n"));
    windowTypeRaw = XXGetProperty(mDisplay, hWin, XA_ATOM, WM_TYPE_PROP, &ulCount);
    if (windowTypeRaw != NULL)
    {
        windowType = reinterpret_cast<Atom *>(windowTypeRaw);
        if (   (ulCount != 0)
            && (*windowType == XInternAtom(mDisplay, WM_TYPE_DESKTOP_PROP, True)))
            rc = true;
    }
    LogRelFlowFunc(("returning %s\n", rc ? "true" : "false"));
    return rc;
}


/**
 * Free all information in the tree of visible windows
 */
void VBoxGuestSeamlessX11::freeWindowTree(void)
{
    /* We use post-increment in the operation to prevent the iterator from being invalidated. */
    LogRelFlowFunc(("\n"));
    for (VBoxGuestWindowList::iterator it = mGuestWindows.begin(); it != mGuestWindows.end();
                 mGuestWindows.removeWindow(it++))
    {
        XShapeSelectInput(mDisplay, it->first, 0);
    }
    LogRelFlowFunc(("returning\n"));
}


/**
 * Waits for a position or shape-related event from guest windows
 *
 * @note Called from the guest event thread.
 */
void VBoxGuestSeamlessX11::nextEvent(void)
{
    XEvent event;

    LogRelFlowFunc(("\n"));
    /* Start by sending information about the current window setup to the host.  We do this
       here because we want to send all such information from a single thread. */
    if (mChanged)
        mObserver->notify();
    mChanged = false;
    XNextEvent(mDisplay, &event);
    switch (event.type)
    {
    case ConfigureNotify:
        doConfigureEvent(event.xconfigure.window);
        break;
    case MapNotify:
        doMapEvent(event.xmap.window);
        break;
    case VBoxShapeNotify:  /* This is defined wrong in my X11 header files! */
    /* the window member in xany is in the same place as in the shape event */
        doShapeEvent(event.xany.window);
        break;
    case UnmapNotify:
        doUnmapEvent(event.xunmap.window);
        break;
    default:
        break;
    }
    LogRelFlowFunc(("returning\n"));
}

/**
 * Handle a configuration event in the seamless event thread by setting the new position.
 *
 * @param event the X11 event structure
 */
void VBoxGuestSeamlessX11::doConfigureEvent(Window hWin)
{
    LogRelFlowFunc(("\n"));
    VBoxGuestWindowList::iterator iter;

    iter = mGuestWindows.find(hWin);
    if (iter != mGuestWindows.end())
    {
        XWindowAttributes winAttrib;

        if (!XGetWindowAttributes(mDisplay, hWin, &winAttrib))
            return;
        iter->second->mX = winAttrib.x;
        iter->second->mY = winAttrib.y;
        iter->second->mWidth = winAttrib.width;
        iter->second->mHeight = winAttrib.height;
        if (iter->second->mhasShape)
        {
            VBoxGuestX11Pointer<XRectangle> rects;
            int cRects = 0, iOrdering;

            rects = XShapeGetRectangles(mDisplay, hWin, ShapeBounding,
                                        &cRects, &iOrdering);
            if (rects.get() == NULL)
                cRects = 0;
            iter->second->mcRects = cRects;
            iter->second->mapRects = rects;
        }
        mChanged = true;
    }
    LogRelFlowFunc(("returning\n"));
}

/**
 * Handle a map event in the seamless event thread.
 *
 * @param event the X11 event structure
 */
void VBoxGuestSeamlessX11::doMapEvent(Window hWin)
{
    LogRelFlowFunc(("\n"));
    VBoxGuestWindowList::iterator iter;

    iter = mGuestWindows.find(hWin);
    if (mGuestWindows.end() == iter)
    {
        addClientWindow(hWin);
        mChanged = true;
    }
    LogRelFlowFunc(("returning\n"));
}


/**
 * Handle a window shape change event in the seamless event thread.
 *
 * @param event the X11 event structure
 */
void VBoxGuestSeamlessX11::doShapeEvent(Window hWin)
{
    LogRelFlowFunc(("\n"));
    VBoxGuestWindowList::iterator iter;

    iter = mGuestWindows.find(hWin);
    if (iter != mGuestWindows.end())
    {
        VBoxGuestX11Pointer<XRectangle> rects;
        int cRects = 0, iOrdering;

        rects = XShapeGetRectangles(mDisplay, hWin, ShapeBounding, &cRects,
                                    &iOrdering);
        if (rects.get() == NULL)
            cRects = 0;
        iter->second->mhasShape = true;
        iter->second->mcRects = cRects;
        iter->second->mapRects = rects;
        mChanged = true;
    }
    LogRelFlowFunc(("returning\n"));
}

/**
 * Handle an unmap event in the seamless event thread.
 *
 * @param event the X11 event structure
 */
void VBoxGuestSeamlessX11::doUnmapEvent(Window hWin)
{
    LogRelFlowFunc(("\n"));
    VBoxGuestWindowList::iterator iter;

    iter = mGuestWindows.find(hWin);
    if (mGuestWindows.end() != iter)
    {
        mGuestWindows.removeWindow(iter);
        mChanged = true;
    }
    LogRelFlowFunc(("returning\n"));
}

/**
 * Sends an updated list of visible rectangles to the host
 */
std::auto_ptr<std::vector<RTRECT> > VBoxGuestSeamlessX11::getRects(void)
{
    LogRelFlowFunc(("\n"));
    unsigned cRects = 0;
    std::auto_ptr<std::vector<RTRECT> > apRects(new std::vector<RTRECT>);

    if (0 != mcRects)
    {
            apRects.get()->reserve(mcRects * 2);
    }
    for (VBoxGuestWindowList::iterator it = mGuestWindows.begin();
         it != mGuestWindows.end(); ++it)
    {
        if (it->second->mhasShape)
        {
            for (int i = 0; i < it->second->mcRects; ++i)
            {
                RTRECT rect;
                rect.xLeft   =   it->second->mX
                                + it->second->mapRects.get()[i].x;
                rect.yBottom =   it->second->mY
                                + it->second->mapRects.get()[i].y
                                + it->second->mapRects.get()[i].height;
                rect.xRight  =   it->second->mX
                                + it->second->mapRects.get()[i].x
                                + it->second->mapRects.get()[i].width;
                rect.yTop    =   it->second->mY
                                + it->second->mapRects.get()[i].y;
                apRects.get()->push_back(rect);
            }
            cRects += it->second->mcRects;
        }
        else
        {
            RTRECT rect;
            rect.xLeft   =  it->second->mX;
            rect.yBottom =  it->second->mY
                          + it->second->mHeight;
            rect.xRight  =  it->second->mX
                          + it->second->mWidth;
            rect.yTop    =  it->second->mY;
            apRects.get()->push_back(rect);
            ++cRects;
        }
    }
    mcRects = cRects;
    LogRelFlowFunc(("returning\n"));
    return apRects;
}

/**
 * Send a client event to wake up the X11 seamless event loop prior to stopping it.
 *
 * @note This function should only be called from the host event thread.
 */
bool VBoxGuestSeamlessX11::interruptEvent(void)
{
    bool rc = false;

    LogRelFlowFunc(("\n"));
    /* Message contents set to zero. */
    XClientMessageEvent clientMessage = { ClientMessage, 0, 0, 0, 0, 0, 8 };

    if (0 != XSendEvent(mDisplay, DefaultRootWindow(mDisplay.get()), false, PropertyChangeMask,
                   reinterpret_cast<XEvent *>(&clientMessage)))
    {
        XFlush(mDisplay);
        rc = true;
    }
    LogRelFlowFunc(("returning %s\n", rc ? "true" : "false"));
    return rc;
}
