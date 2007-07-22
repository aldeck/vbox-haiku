/** @file
 *
 * VBox OpenGL
 *
 * Simple buffered OpenGL functions
 *
 * Contributor: Alexander Eichner
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

#include "vboxgl.h"
#define LOG_GROUP LOG_GROUP_SHARED_OPENGL
#include <VBox/log.h>
#include <string.h>
#include <stdio.h>

/*static int (*X_handler)(Display *, XErrorEvent *) = NULL;
static int x_errhandler(Display *d, XErrorEvent *e)
{
    return (X_handler(d, e));
}

static int (*XIO_handler)(Display *) = NULL;
static int xio_errhandler(Display *d)
{
    return (XIO_handler(d));
}*/

/* X11 server connection for all OpenGL clients.
 * Neccessary because Mesa and DRI cannot handle more than one
 * connection per thread (only hardware acceleration, software rendering
 * runs fine with more than one connection). 
 * Would crash in vboxglDisconnect if on every vboxglConnect
 * a new Display is created */
Display *glXDisplay = NULL;

static Bool WaitForNotify( Display *dpy, XEvent *event, XPointer arg ) {
    return (event->type == MapNotify) && (event->xmap.window == (Window) arg);
}

/* from http://www.mesa3d.org/brianp/sig97/exten.htm */
GLboolean vboxglCheckExtension(Display *dpy, int screenNum, const char *extName )
{
   /*
    ** Search for extName in the extensions string.  Use of strstr()
    ** is not sufficient because extension names can be prefixes of
    ** other extension names.  Could use strtok() but the constant
    ** string returned by glGetString can be in read-only memory.
    */
    char *p = (char *) glXQueryExtensionsString(dpy, screenNum);
    char *end;
    int extNameLen;

    extNameLen = strlen(extName);
    end = p + strlen(p);

    while (p < end) {
        int n = strcspn(p, " ");
        if ((extNameLen == n) && (strncmp(extName, p, n) == 0)) {
            return GL_TRUE;
        }
        p += (n + 1);
    }
    return GL_FALSE;
}


/**
 * Print parameters for a GLXFBConfig to stdout.
 * Input:  dpy - the X display
 *         screen - the X screen number
 *         fbConfig - the fbconfig handle
 *         horizFormat - if true, print in horizontal format
 */
void
PrintFBConfigInfo(Display *dpy, int screen, GLXFBConfig config)
{
   int bufferSize, level, doubleBuffer, stereo, auxBuffers;
   int redSize, greenSize, blueSize, alphaSize;
   int depthSize, stencilSize;
   int accumRedSize, accumBlueSize, accumGreenSize, accumAlphaSize;
//   int sampleBuffers, samples;
   int drawableType, renderType, xRenderable, xVisual, id;

   /* do queries using the GLX 1.3 tokens (same as the SGIX tokens) */
   glXGetFBConfigAttrib(dpy, config, GLX_BUFFER_SIZE, &bufferSize);
   glXGetFBConfigAttrib(dpy, config, GLX_LEVEL, &level);
   glXGetFBConfigAttrib(dpy, config, GLX_DOUBLEBUFFER, &doubleBuffer);
   glXGetFBConfigAttrib(dpy, config, GLX_STEREO, &stereo);
   glXGetFBConfigAttrib(dpy, config, GLX_AUX_BUFFERS, &auxBuffers);
   glXGetFBConfigAttrib(dpy, config, GLX_RED_SIZE, &redSize);
   glXGetFBConfigAttrib(dpy, config, GLX_GREEN_SIZE, &greenSize);
   glXGetFBConfigAttrib(dpy, config, GLX_BLUE_SIZE, &blueSize);
   glXGetFBConfigAttrib(dpy, config, GLX_ALPHA_SIZE, &alphaSize);
   glXGetFBConfigAttrib(dpy, config, GLX_DEPTH_SIZE, &depthSize);
   glXGetFBConfigAttrib(dpy, config, GLX_STENCIL_SIZE, &stencilSize);
   glXGetFBConfigAttrib(dpy, config, GLX_ACCUM_RED_SIZE, &accumRedSize);
   glXGetFBConfigAttrib(dpy, config, GLX_ACCUM_GREEN_SIZE, &accumGreenSize);
   glXGetFBConfigAttrib(dpy, config, GLX_ACCUM_BLUE_SIZE, &accumBlueSize);
   glXGetFBConfigAttrib(dpy, config, GLX_ACCUM_ALPHA_SIZE, &accumAlphaSize);
//   glXGetFBConfigAttrib(dpy, config, GLX_SAMPLE_BUFFERS, &sampleBuffers);
//   glXGetFBConfigAttrib(dpy, config, GLX_SAMPLES, &samples);
   glXGetFBConfigAttrib(dpy, config, GLX_DRAWABLE_TYPE, &drawableType);
   glXGetFBConfigAttrib(dpy, config, GLX_RENDER_TYPE, &renderType);
   glXGetFBConfigAttrib(dpy, config, GLX_X_RENDERABLE, &xRenderable);
   glXGetFBConfigAttrib(dpy, config, GLX_X_VISUAL_TYPE, &xVisual);
   if (!xRenderable || !(drawableType & GLX_WINDOW_BIT_SGIX))
      xVisual = -1;

   glXGetFBConfigAttrib(dpy, config, GLX_FBCONFIG_ID, &id);

      printf("Id 0x%x\n", id);
      printf("  Buffer Size: %d\n", bufferSize);
      printf("  Level: %d\n", level);
      printf("  Double Buffer: %s\n", doubleBuffer ? "yes" : "no");
      printf("  Stereo: %s\n", stereo ? "yes" : "no");
      printf("  Aux Buffers: %d\n", auxBuffers);
      printf("  Red Size: %d\n", redSize);
      printf("  Green Size: %d\n", greenSize);
      printf("  Blue Size: %d\n", blueSize);
      printf("  Alpha Size: %d\n", alphaSize);
      printf("  Depth Size: %d\n", depthSize);
      printf("  Stencil Size: %d\n", stencilSize);
      printf("  Accum Red Size: %d\n", accumRedSize);
      printf("  Accum Green Size: %d\n", accumGreenSize);
      printf("  Accum Blue Size: %d\n", accumBlueSize);
      printf("  Accum Alpha Size: %d\n", accumAlphaSize);
//      printf("  Sample Buffers: %d\n", sampleBuffers);
//      printf("  Samples/Pixel: %d\n", samples);
      printf("  Drawable Types: ");
      if (drawableType & GLX_WINDOW_BIT)  printf("Window ");
      if (drawableType & GLX_PIXMAP_BIT)  printf("Pixmap ");
      if (drawableType & GLX_PBUFFER_BIT)  printf("PBuffer");
      printf("\n");
      printf("  Render Types: ");
      if (renderType & GLX_RGBA_BIT_SGIX)  printf("RGBA ");
      if (renderType & GLX_COLOR_INDEX_BIT_SGIX)  printf("CI ");
      printf("\n");
      printf("  X Renderable: %s\n", xRenderable ? "yes" : "no");

}

/**
 * Global init of VBox OpenGL for windows
 *
 * @returns VBox error code
 */
int vboxglGlobalInit()
{
    Log(("vboxglGlobalInit\n"));

    glXDisplay = XOpenDisplay(NULL);

    /*vboxInitOpenGLExtensions();*/
    return VINF_SUCCESS;
}


/**
 * Enable OpenGL
 *
 * @returns VBox error code
 * @param   pClient         Client context
 */
int vboxglEnableOpenGL(PVBOXOGLCTX pClient)
{
    static int attribs[] = {
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DOUBLEBUFFER,  True,  /* Request a double-buffered color buffer with */
        GLX_RED_SIZE,      1,     /* the maximum number of bits per component    */
        GLX_GREEN_SIZE,    1, 
        GLX_BLUE_SIZE,     1,
        None
    };
    int screen_num;
    XSetWindowAttributes attr;
    unsigned long mask;
    int returnedFBConfigs;

    if (!pClient->glxContext) 
    {
        /* we have to set up a rendering context to be able to use glGetString
         * a window is created but is not mapped to screen (so it's not visible') 
         * and a GLXContext is bound to it */
        screen_num = DefaultScreen(pClient->dpy);
        pClient->enable.fbConfig = pClient->glxChooseFBConfig(pClient->dpy, screen_num, attribs, &returnedFBConfigs);
        Log(("vboxglGetString: returned FBConfigs: %d\n", returnedFBConfigs));
        pClient->enable.visinfo = pClient->glxGetVisualFromFBConfig(pClient->dpy, pClient->enable.fbConfig[0]);
        /* Create Window */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap = XCreateColormap(pClient->dpy, RootWindow(pClient->dpy, screen_num), pClient->enable.visinfo->visual, AllocNone);
        attr.event_mask = StructureNotifyMask | ExposureMask;
        mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
        pClient->enable.win = XCreateWindow(pClient->dpy, RootWindow(pClient->dpy, screen_num), 0, 0, 100, 100,
		           0, pClient->enable.visinfo->depth, InputOutput,
		           pClient->enable.visinfo->visual, mask, &attr);
        /* Create Context */
        pClient->enable.ctx = pClient->glxCreateNewContext(pClient->dpy, pClient->enable.fbConfig[0], GLX_RGBA_TYPE, NULL, True);

        glXMakeCurrent(pClient->dpy, pClient->enable.win, pClient->enable.ctx);
    } 
    else 
    {
        glXMakeCurrent(pClient->dpy, pClient->xWindow, pClient->glxContext);
    }

    return VINF_SUCCESS;
}

/**
 * Disable OpenGL
 *
 * @returns VBox error code
 * @param   pClient         Client context
 */
int vboxglDisableOpenGL(PVBOXOGLCTX pClient)
{
    /* Free all data */
    glFlush();
    if (!pClient->glxContext) 
    {
        glXMakeCurrent(pClient->dpy, 0, NULL);
        XDestroyWindow(pClient->dpy, pClient->enable.win);
        glXDestroyContext(pClient->dpy, pClient->enable.ctx);
        XFree(pClient->enable.visinfo);
        XFree(pClient->enable.fbConfig);
    }
    return VINF_SUCCESS;
}

/**
 * Client connect init
 *
 * @returns VBox error code
 * @param   pClient         Client context
 */
int vboxglConnect(PVBOXOGLCTX pClient)
{
    int rc = VERR_NOT_IMPLEMENTED;
    Log(("vboxglConnect\n"));
    //pClient->getContextIDPtr = NULL;
    //pClient->importContextEXTPtr = NULL;
    pClient->PixelFormatToFBConfigMapper = NULL;
    pClient->xWindow = 0;

    pClient->dpy = glXDisplay;

    if (pClient->dpy) {
        int  screenNum, major, minor;

        screenNum = DefaultScreen(pClient->dpy);
        glXQueryVersion(pClient->dpy, &major, &minor);

        if ((major == 1) && (minor >= 3)) {
            Log(("Server GLX 1.3 supported\n"));
            pClient->glxChooseFBConfig = (PFNGLXCHOOSEFBCONFIGSGIXPROC) glXGetProcAddress( 
                                          (GLubyte *) "glXChooseFBConfig");
            pClient->glxGetVisualFromFBConfig = (PFNGLXGETVISUALFROMFBCONFIGSGIXPROC) glXGetProcAddress( 
                                                 (GLubyte *) "glXGetVisualFromFBConfig");
            pClient->glxCreateNewContext = (PFNGLXCREATECONTEXTWITHCONFIGSGIXPROC) glXGetProcAddress(
                                            (GLubyte *) "glXCreateNewContext");
        } else if (vboxglCheckExtension(pClient->dpy, screenNum, "GLX_SGIX_fbconfig")) {
            Log(("GLX_SGIX_fbconfig extension supported\n"));
            pClient->glxChooseFBConfig = (PFNGLXCHOOSEFBCONFIGSGIXPROC) glXGetProcAddress( 
                                          (GLubyte *) "glXChooseFBConfigSGIX");
            pClient->glxGetVisualFromFBConfig = (PFNGLXGETVISUALFROMFBCONFIGSGIXPROC) glXGetProcAddress( 
                                                 (GLubyte *) "glXGetVisualFromFBConfigSGIX");
            pClient->glxCreateNewContext = (PFNGLXCREATECONTEXTWITHCONFIGSGIXPROC) glXGetProcAddress(
                                            (GLubyte *) "glXCreateContextWithConfigSGIX");
        } else {
                Log(("Error no FBConfig supported\n"));
                rc = VERR_NOT_IMPLEMENTED;
        }
        if (pClient->glxChooseFBConfig && pClient->glxGetVisualFromFBConfig && pClient->glxCreateNewContext)
            rc = VINF_SUCCESS;
    }

    return rc;
}

/**
 * Client disconnect cleanup
 *
 * @returns VBox error code
 * @param   pClient         Client context
 */
int vboxglDisconnect(PVBOXOGLCTX pClient)
{
    Log(("vboxglDisconnect\n"));

#ifdef VBOX_OGL_DEBUG_WINDOW_OUTPUT
    if (pClient->xWindow != 0) {
        XUnmapWindow(pClient->dpy, pClient->xWindow);
        XDestroyWindow(pClient->dpy, pClient->xWindow);
    }
    if (pClient->PixelFormatToFBConfigMapper) {
        XFree(pClient->PixelFormatToFBConfigMapper);
    }

    pClient->dpy = NULL;
    pClient->xWindow = 0;
    pClient->actFBConfig = NULL;
#endif
    return VINF_SUCCESS;
}

/* Driver functions */
void vboxglDrvCreateContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    XSetWindowAttributes attr;
    XVisualInfo *visinfo = NULL;
    unsigned long mask;
    //GLXContext ctx;
    GLXFBConfig fbConfig;
    GLXContextID glrc;
    int screen_num;
    XEvent event;
    OGL_CMD(DrvCreateContext, 1);
    OGL_PARAM(HDC, hdc);

    Log(("DrvCreateContext %x\n", hdc));
#ifdef VBOX_OGL_DEBUG_WINDOW_OUTPUT

    screen_num = DefaultScreen(pClient->dpy);
    fbConfig = pClient->actFBConfig;

#if 0
    if (!fbConfig) {
        /* Create a standard fbconfig */
        int returnedNumFBConfigs;
        GLXFBConfig *returnedFBConfigs;
        static int attribs[] = {
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_RENDER_TYPE,   GLX_RGBA_BIT,
            GLX_DOUBLEBUFFER,  True,  /* Request a double-buffered color buffer with */
            GLX_RED_SIZE,      1,     /* the maximum number of bits per component    */
            GLX_GREEN_SIZE,    1, 
            GLX_BLUE_SIZE,     1,
            None
        };

        Log(("Warning: no GLXFBConfig set creating standard one\n"));
        returnedFBConfigs = pClient->glxChooseFBConfig(pClient->dpy, screen_num, attribs, &returnedNumFBConfigs);
        if (!returnedNumFBConfigs) {
            pClient->lastretval = 0;
            pClient->fHasLastError = true;
            pClient->ulLastError   = glGetError();
            return;
        }
        fbConfig = returnedFBConfigs[0];
    }
#endif

    visinfo = pClient->glxGetVisualFromFBConfig(pClient->dpy, fbConfig);

    if (pClient->xWindow == 0) {

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(pClient->dpy, RootWindow(pClient->dpy, screen_num ), visinfo->visual, AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask;
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
	pClient->xWindow = XCreateWindow(pClient->dpy, 
	                                RootWindow(pClient->dpy, screen_num), 
	                                0, 0, pClient->winWidth, pClient->winHeight, 0,
	                                visinfo->depth, InputOutput,
	                                visinfo->visual, mask, &attr);
    }
    XResizeWindow(pClient->dpy, pClient->xWindow, pClient->winWidth, pClient->winHeight);
    pClient->glxContext = pClient->glxCreateNewContext(pClient->dpy, fbConfig, GLX_RGBA_TYPE, NULL, True);

    XMapWindow(pClient->dpy, pClient->xWindow);
    XIfEvent(pClient->dpy, &event, WaitForNotify, (XPointer)pClient->xWindow );
    //glrc = pClient->getContextIDPtr(ctx);
    glrc = 1;
    Assert(glrc);
#else
    AssertFailed();
    glrc = 0;
#endif

    pClient->lastretval = (uint64_t)glrc;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvDeleteContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    //GLXContext ctx;
    OGL_CMD(DrvDeleteContext, 1);
    OGL_PARAM(HGLRC, hglrc);
    Log(("DrvDeleteContext %x\n", hglrc));
    //ctx = pClient->importContextEXTPtr(pClient->dpy, hglrc);
    glXDestroyContext(pClient->dpy, VBOX_OGL_GUEST_TO_HOST_HDC(hglrc));
    pClient->lastretval = 1;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvSetContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    //GLXContext ctx;
    OGL_CMD(DrvSetContext, 2);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(HGLRC, hglrc);
    Log(("DrvSetContext %x %x\n", hdc, hglrc));
#ifdef VBOX_OGL_DEBUG_WINDOW_OUTPUT
    //ctx = pClient->importContextEXTPtr(pClient->dpy, hglrc);
    pClient->lastretval = glXMakeCurrent(pClient->dpy, pClient->xWindow, 
                                         VBOX_OGL_GUEST_TO_HOST_HDC(hglrc));
    if (!pClient->lastretval)
        Log(("glXMakeCurrent failed\n"));
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
#else
    AssertFailed();
#endif
}

void vboxglDrvCopyContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    //GLXContext ctxSrc, ctxDst;
    OGL_CMD(DrvDeleteContext, 3);
    OGL_PARAM(HGLRC, hglrcSrc);
    OGL_PARAM(HGLRC, hglrcDst);
    OGL_PARAM(UINT,  mask);
    Log(("DrvCopyContext %x %x %x\n", hglrcSrc, hglrcDst, mask));
    //ctxSrc = pClient->importContextEXTPtr(pClient->dpy, hglrcSrc);
    //ctxDst = pClient->importContextEXTPtr(pClient->dpy, hglrcDst);
    glXCopyContext(pClient->dpy, VBOX_OGL_GUEST_TO_HOST_HDC(hglrc), VBOX_OGL_GUEST_TO_HOST_HDC(hglrc), mask);
    pClient->lastretval = 1;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvReleaseContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvReleaseContext, 1);
    OGL_PARAM(HGLRC, hglrc);
    Log(("DrvReleaseContext %x\n", hglrc));
    /* clear current selection */
    pClient->lastretval = glXMakeCurrent(pClient->dpy, 0, NULL);

    if (!pClient->lastretval)
        Log(("glXMakeCurrent failed\n"));
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvCreateLayerContext(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvCreateLayerContext, 2);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iLayerPlane);

    Log(("DrvCreateLayerContext %x\n", hdc));
#ifdef VBOX_OGL_DEBUG_WINDOW_OUTPUT
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
#else
    AssertFailed();
#endif
}

void vboxglDrvShareLists(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvShareLists, 3);
    OGL_PARAM(HGLRC, hglrc1);
    OGL_PARAM(HGLRC, hglrc2);
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}


void vboxglDrvRealizeLayerPalette(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvRealizeLayerPalette, 3);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iLayerPlane);
    OGL_PARAM(BOOL, bRealize);
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvSwapLayerBuffers(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvSwapLayerBuffers, 2);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(UINT, fuPlanes);
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvSetPixelFormat(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    int screen_num;
    OGL_CMD(DrvSetPixelFormat, 4);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iPixelFormat);
    OGL_PARAM(uint32_t, cx);
    OGL_PARAM(uint32_t, cy);

    Log(("vboxDrvSetPixelFormat %d\n", iPixelFormat));

    /* Get GLXFBConfig based on the given ID */
    pClient->actFBConfig = pClient->PixelFormatToFBConfigMapper[iPixelFormat-1];
    screen_num = DefaultScreen(pClient->dpy);
    PrintFBConfigInfo(pClient->dpy, screen_num, pClient->actFBConfig);
    Log(("Window width: %d Window height: %d\n", cx, cy));
    pClient->winWidth = cx;
    pClient->winHeight = cy;
    pClient->lastretval = true;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvSwapBuffers(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvSwapBuffers, 1);
    OGL_PARAM(HDC, hdc);

    glXSwapBuffers(pClient->dpy, pClient->xWindow);
    pClient->lastretval = 1;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvDescribeLayerPlane(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    PLAYERPLANEDESCRIPTOR plpd;

    OGL_CMD(DrvDescribeLayerPlane, 4);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iPixelFormat);
    OGL_PARAM(int, iLayerPlane);
    OGL_PARAM(UINT, nBytes);
    Assert(pClient->cbLastParam == nBytes);
    plpd = (PLAYERPLANEDESCRIPTOR)pClient->pLastParam;

    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvSetLayerPaletteEntries(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    OGL_CMD(DrvSetLayerPaletteEntries, 5);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iLayerPlane);
    OGL_PARAM(int, iStart);
    OGL_PARAM(int, cEntries);
    OGL_MEMPARAM(COLORREF, pcr);
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvGetLayerPaletteEntries(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    COLORREF *pcr;

    OGL_CMD(DrvGetLayerPaletteEntries, 4);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iLayerPlane);
    OGL_PARAM(int, iStart);
    OGL_PARAM(int, cEntries);

    Assert(pClient->cbLastParam == sizeof(COLORREF)*cEntries);
    pcr = (COLORREF *)pClient->pLastParam;
    pClient->lastretval = 0; /** @todo */
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

void vboxglDrvDescribePixelFormat(VBOXOGLCTX *pClient, uint8_t *pCmdBuffer)
{
    LPPIXELFORMATDESCRIPTOR ppfd;
    GLXFBConfig *allFBConfigs, matchingFBConfig;
    int screenNum, glxReturnValue;

    OGL_CMD(DrvDescribePixelFormat, 3);
    OGL_PARAM(HDC, hdc);
    OGL_PARAM(int, iPixelFormat);
    OGL_PARAM(UINT, nBytes);
    Assert(pClient->cbLastParam == nBytes);
    ppfd = (LPPIXELFORMATDESCRIPTOR)pClient->pLastParam;

    Log(("iPixelFormat: %d\n", iPixelFormat));

    if (!pClient->PixelFormatToFBConfigMapper) {
        /* First get number of all visuals for the return value */
        screenNum = DefaultScreen(pClient->dpy);
        allFBConfigs = glXGetFBConfigs(pClient->dpy, screenNum, 
                                            &pClient->numFBConfigs);
        pClient->PixelFormatToFBConfigMapper = allFBConfigs;
    }

    if (nBytes == sizeof(PIXELFORMATDESCRIPTOR)) {
        int redSize, greenSize, blueSize, alphaSize, xVisual, xRenderable;
        /* Get GLXFBConfig which matches iPixelFormat */
        matchingFBConfig = pClient->PixelFormatToFBConfigMapper[iPixelFormat-1];

        Log(("Filling values into PIXELFORMATDESCRIPTOR\n"));
        /* translate all values to theire corresponding Windows ones */
        ppfd->nSize = sizeof(PIXELFORMATDESCRIPTOR);
        ppfd->nVersion = 1;
        ppfd->iLayerType = PFD_MAIN_PLANE;
        ppfd->dwFlags =  0;

        /* Set cColorBits */
        glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_RED_SIZE, &redSize);
        glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_GREEN_SIZE, &greenSize);
        glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_BLUE_SIZE, &blueSize);
        glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_ALPHA_SIZE, &alphaSize);
        ppfd->cColorBits = redSize + greenSize + blueSize;
        ppfd->cRedBits = redSize;
        ppfd->cBlueBits = blueSize;
        ppfd->cGreenBits = greenSize;
        ppfd->cAlphaBits = alphaSize;

        /* Set dwFlags */
        if (!glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_DRAWABLE_TYPE, &glxReturnValue)) {
            glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_VISUAL_ID, &xVisual);
            glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_X_RENDERABLE, &xRenderable);
            if ((glxReturnValue & GLX_WINDOW_BIT) && xVisual)
                ppfd->dwFlags |= (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL);
        }
        if (!glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_DOUBLEBUFFER, &glxReturnValue)) {
            if (glxReturnValue)
                ppfd->dwFlags |= PFD_DOUBLEBUFFER;
        }
        /* Set iPixelType */
        if (!glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_RENDER_TYPE, &glxReturnValue)) {
            if (glxReturnValue & GLX_RGBA_BIT)
                ppfd->iPixelType = PFD_TYPE_RGBA;
            else if ((glxReturnValue & GLX_COLOR_INDEX_BIT) & !(glxReturnValue & GLX_RGBA_BIT))
                ppfd->iPixelType = PFD_TYPE_COLORINDEX;
        }
        /* Set cDepthBits */
        if (!glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_DEPTH_SIZE, &glxReturnValue)) {
            ppfd->cDepthBits = glxReturnValue;
        } else {
            ppfd->cDepthBits = 0;
        }
        /* Set cStencilBits */
        if (!glXGetFBConfigAttrib(pClient->dpy, matchingFBConfig, GLX_STENCIL_SIZE, &glxReturnValue)) {
            ppfd->cStencilBits = glxReturnValue;
        } else {
            ppfd->cStencilBits = 0;
        }
        /** @todo Fill in the rest */
    }

    pClient->lastretval = pClient->numFBConfigs;
    pClient->fHasLastError = true;
    pClient->ulLastError   = glGetError();
}

RTUINTPTR vboxDrvIsExtensionAvailable(char *pszExtFunctionName)
{
    RTUINTPTR pfnProc = (RTUINTPTR)glXGetProcAddress((const GLubyte *)pszExtFunctionName);
    Log(("vboxDrvIsExtensionAvailable %s -> %d\n", pszExtFunctionName, !!pfnProc));
    return pfnProc;
}

