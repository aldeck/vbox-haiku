/** @file
 *
 * VBox frontends: Basic Frontend (BFE):
 * Declaration of SDLConsole class
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

#ifndef __H_VBOXSDL
#define __H_VBOXSDL

/* include this first so Windows.h get's in before our stuff. */
#include <SDL.h>
#ifndef __DARWIN__
# include <SDL_syswm.h>
#endif
#if defined(__WIN__) /// @todo someone please explain why this is necessary. This breaks darwin solid.
// damn SDL redefines main!
#undef main
#endif 

#include "ConsoleImpl.h"
#include <iprt/string.h>

/** Pointer shape change event data strucure */
struct PointerShapeChangeData
{
    PointerShapeChangeData (BOOL aVisible, BOOL aAlpha, ULONG aXHot, ULONG aYHot,
                            ULONG aWidth, ULONG aHeight,
                            const uint8_t *aShape)
        : visible (aVisible), alpha (aAlpha), xHot (aXHot), yHot (aYHot)
        , width (aWidth), height (aHeight), shape (NULL)
    {
        // make a copy of the shape
        if (aShape) {
            uint32_t shapeSize = ((((aWidth + 7) / 8) * aHeight + 3) & ~3) + aWidth * 4 * aHeight;
            shape = new uint8_t [shapeSize];
            if (shape)
                memcpy ((void *) shape, (void *) aShape, shapeSize);
        }
    }

    ~PointerShapeChangeData()
    {
        if (shape) delete[] shape;
    }

    const BOOL visible;
    const BOOL alpha;
    const ULONG xHot;
    const ULONG yHot;
    const ULONG width;
    const ULONG height;
    const uint8_t *shape;
};

/** custom SDL event for display update handling */
#define SDL_USER_EVENT_UPDATERECT         (SDL_USEREVENT + 4)
/** custom SDL event for resize handling */
#define SDL_USER_EVENT_RESIZE             (SDL_USEREVENT + 5)
/** custom SDL for XPCOM event queue processing */
#define SDL_USER_EVENT_XPCOM_EVENTQUEUE   (SDL_USEREVENT + 6)


/** custom SDL for XPCOM event queue processing */
#define SDL_USER_EVENT_GRAB   (SDL_USEREVENT + 6)

/** custom SDL event for updating the titlebar */
#define SDL_USER_EVENT_UPDATE_TITLEBAR    (SDL_USEREVENT + 7)
/** custom SDL user event for terminating the session */
#define SDL_USER_EVENT_TERMINATE          (SDL_USEREVENT + 8)
/** custom SDL user event for secure label update notification */
#define SDL_USER_EVENT_SECURELABEL_UPDATE (SDL_USEREVENT + 9)
/** custom SDL user event for pointer shape change request */
#define SDL_USER_EVENT_POINTER_CHANGE     (SDL_USEREVENT + 10)

#define SDL_USER_


class SDLConsole : public Console
{
public:
    SDLConsole();
    ~SDLConsole();

    virtual void     updateTitlebar();
    virtual void     updateTitlebarSave(int iPercent);

    virtual void     inputGrabStart();
    virtual void     inputGrabEnd();

    virtual void     mouseSendEvent(int dz);
    virtual void     onMousePointerShapeChange(bool fVisible,
                                 bool fAlpha, uint32_t xHot,
                                 uint32_t yHot, uint32_t width,
                                 uint32_t height, void *pShape);

    virtual CONEVENT eventWait();
    virtual void     eventQuit();
    virtual void     resetCursor();

private:

    int     handleHostKey(const SDL_KeyboardEvent *pEv);
    uint8_t keyEventToKeyCode(const SDL_KeyboardEvent *ev);
    void    processKey(SDL_KeyboardEvent *ev);
    void    setPointerShape (const PointerShapeChangeData *data);

    void    resetKeys(void);

    /** flag whether keyboard/mouse events are grabbed */
    bool fInputGrab;

    /** modifier keypress status (scancode as index) */
    uint8_t gaModifiersState[256];

    SDL_Cursor *gpDefaultCursor;
    SDL_Cursor *gpCustomCursor;
    /** Custom window manager cursor? */
    WMcursor *gpCustomWMcursor;
#ifdef VBOXBFE_WITH_X11
    SDL_SysWMinfo gSdlInfo;
#endif

    /* Current event */
    SDL_Event ev1;
    SDL_Event EvHKeyDown;
};

#endif // __H_VBOXSDL

