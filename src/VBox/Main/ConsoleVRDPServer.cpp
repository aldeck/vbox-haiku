/* $Id$ */
/** @file
 * VBox Console VRDP Helper class
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "ConsoleVRDPServer.h"
#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#include "KeyboardImpl.h"
#include "MouseImpl.h"

#include "AutoCaller.h"
#include "Logging.h"

#include <iprt/asm.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/alloca.h>
#include <iprt/cpp/utils.h>

#include <VBox/err.h>
#ifdef VBOX_WITH_VRDP
#include <VBox/VRDPOrders.h>
#endif /* VBOX_WITH_VRDP */

class VRDPConsoleListener :
    VBOX_SCRIPTABLE_IMPL(IEventListener)
{
public:
    VRDPConsoleListener(ConsoleVRDPServer *server)
        : m_server(server)
    {
#ifndef VBOX_WITH_XPCOM
        refcnt = 0;
#endif /* !VBOX_WITH_XPCOM */
    }

    virtual ~VRDPConsoleListener() {}

    NS_DECL_ISUPPORTS

#ifndef VBOX_WITH_XPCOM
    STDMETHOD_(ULONG, AddRef)() {
        return ::InterlockedIncrement(&refcnt);
    }
    STDMETHOD_(ULONG, Release)()
    {
        long cnt = ::InterlockedDecrement(&refcnt);
        if (cnt == 0)
            delete this;
        return cnt;
    }
    STDMETHOD(QueryInterface)(REFIID riid , void **ppObj)
    {
        if (riid == IID_IUnknown) {
            *ppObj = (IUnknown*)this;
            AddRef();
            return S_OK;
        }
        if (riid == IID_IEventListener) {
            *ppObj = (IEventListener*)this;
            AddRef();
            return S_OK;
        }
        *ppObj = NULL;
        return E_NOINTERFACE;
    }
#endif /* !VBOX_WITH_XPCOM */


    STDMETHOD(HandleEvent)(IEvent * aEvent)
    {
        VBoxEventType_T aType = VBoxEventType_Invalid;

        aEvent->COMGETTER(Type)(&aType);
        switch (aType)
        {
            case VBoxEventType_OnMousePointerShapeChanged:
            {
                ComPtr<IMousePointerShapeChangedEvent> mpscev = aEvent;
                Assert(mpscev);
                BOOL    visible,  alpha;
                ULONG   xHot, yHot, width, height;
                com::SafeArray <BYTE> shape;

                mpscev->COMGETTER(Visible)(&visible);
                mpscev->COMGETTER(Alpha)(&alpha);
                mpscev->COMGETTER(Xhot)(&xHot);
                mpscev->COMGETTER(Yhot)(&yHot);
                mpscev->COMGETTER(Width)(&width);
                mpscev->COMGETTER(Height)(&height);
                mpscev->COMGETTER(Shape)(ComSafeArrayAsOutParam(shape));

                OnMousePointerShapeChange(visible, alpha, xHot, yHot, width, height, ComSafeArrayAsInParam(shape));
                break;
            }
            case VBoxEventType_OnMouseCapabilityChanged:
            {
                ComPtr<IMouseCapabilityChangedEvent> mccev = aEvent;
                Assert(mccev);
                if (m_server)
                {
                    BOOL fAbsoluteMouse;
                    mccev->COMGETTER(SupportsAbsolute)(&fAbsoluteMouse);
                    m_server->NotifyAbsoluteMouse(!!fAbsoluteMouse);
                }
                break;
            }
            case VBoxEventType_OnKeyboardLedsChanged:
            {
                ComPtr<IKeyboardLedsChangedEvent> klcev = aEvent;
                Assert(klcev);

                if (m_server)
                {
                    BOOL fNumLock, fCapsLock, fScrollLock;
                    klcev->COMGETTER(NumLock)(&fNumLock);
                    klcev->COMGETTER(CapsLock)(&fCapsLock);
                    klcev->COMGETTER(ScrollLock)(&fScrollLock);
                    m_server->NotifyKeyboardLedsChange(fNumLock, fCapsLock, fScrollLock);
                }
                 break;
            }

            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
    STDMETHOD(OnMousePointerShapeChange)(BOOL visible, BOOL alpha, ULONG xHot, ULONG yHot,
                                         ULONG width, ULONG height, ComSafeArrayIn(BYTE,shape));
    ConsoleVRDPServer *m_server;
#ifndef VBOX_WITH_XPCOM
    long refcnt;
#endif /* !VBOX_WITH_XPCOM */
};

#ifdef VBOX_WITH_XPCOM
#include <nsMemory.h>
NS_DECL_CLASSINFO(VRDPConsoleListener)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(VRDPConsoleListener, IEventListener)
#endif /* VBOX_WITH_XPCOM */

#ifdef DEBUG_sunlover
#define LOGDUMPPTR Log
void dumpPointer(const uint8_t *pu8Shape, uint32_t width, uint32_t height, bool fXorMaskRGB32)
{
    unsigned i;

    const uint8_t *pu8And = pu8Shape;

    for (i = 0; i < height; i++)
    {
        unsigned j;
        LOGDUMPPTR(("%p: ", pu8And));
        for (j = 0; j < (width + 7) / 8; j++)
        {
            unsigned k;
            for (k = 0; k < 8; k++)
            {
                LOGDUMPPTR(("%d", ((*pu8And) & (1 << (7 - k)))? 1: 0));
            }

            pu8And++;
        }
        LOGDUMPPTR(("\n"));
    }

    if (fXorMaskRGB32)
    {
        uint32_t *pu32Xor = (uint32_t*)(pu8Shape + ((((width + 7) / 8) * height + 3) & ~3));

        for (i = 0; i < height; i++)
        {
            unsigned j;
            LOGDUMPPTR(("%p: ", pu32Xor));
            for (j = 0; j < width; j++)
            {
                LOGDUMPPTR(("%08X", *pu32Xor++));
            }
            LOGDUMPPTR(("\n"));
        }
    }
    else
    {
        /* RDP 24 bit RGB mask. */
        uint8_t *pu8Xor = (uint8_t*)(pu8Shape + ((((width + 7) / 8) * height + 3) & ~3));
        for (i = 0; i < height; i++)
        {
            unsigned j;
            LOGDUMPPTR(("%p: ", pu8Xor));
            for (j = 0; j < width; j++)
            {
                LOGDUMPPTR(("%02X%02X%02X", pu8Xor[2], pu8Xor[1], pu8Xor[0]));
                pu8Xor += 3;
            }
            LOGDUMPPTR(("\n"));
        }
    }
}
#else
#define dumpPointer(a, b, c, d) do {} while (0)
#endif /* DEBUG_sunlover */

static void findTopLeftBorder(const uint8_t *pu8AndMask, const uint8_t *pu8XorMask, uint32_t width, uint32_t height, uint32_t *pxSkip, uint32_t *pySkip)
{
    /*
     * Find the top border of the AND mask. First assign to special value.
     */
    uint32_t ySkipAnd = ~0;

    const uint8_t *pu8And = pu8AndMask;
    const uint32_t cbAndRow = (width + 7) / 8;
    const uint8_t maskLastByte = (uint8_t)( 0xFF << (cbAndRow * 8 - width) );

    Assert(cbAndRow > 0);

    unsigned y;
    unsigned x;

    for (y = 0; y < height && ySkipAnd == ~(uint32_t)0; y++, pu8And += cbAndRow)
    {
        /* For each complete byte in the row. */
        for (x = 0; x < cbAndRow - 1; x++)
        {
            if (pu8And[x] != 0xFF)
            {
                ySkipAnd = y;
                break;
            }
        }

        if (ySkipAnd == ~(uint32_t)0)
        {
            /* Last byte. */
            if ((pu8And[cbAndRow - 1] & maskLastByte) != maskLastByte)
            {
                ySkipAnd = y;
            }
        }
    }

    if (ySkipAnd == ~(uint32_t)0)
    {
        ySkipAnd = 0;
    }

    /*
     * Find the left border of the AND mask.
     */
    uint32_t xSkipAnd = ~0;

    /* For all bit columns. */
    for (x = 0; x < width && xSkipAnd == ~(uint32_t)0; x++)
    {
        pu8And = pu8AndMask + x/8;       /* Currently checking byte. */
        uint8_t mask = 1 << (7 - x%8); /* Currently checking bit in the byte. */

        for (y = ySkipAnd; y < height; y++, pu8And += cbAndRow)
        {
            if ((*pu8And & mask) == 0)
            {
                xSkipAnd = x;
                break;
            }
        }
    }

    if (xSkipAnd == ~(uint32_t)0)
    {
        xSkipAnd = 0;
    }

    /*
     * Find the XOR mask top border.
     */
    uint32_t ySkipXor = ~0;

    uint32_t *pu32XorStart = (uint32_t *)pu8XorMask;

    uint32_t *pu32Xor = pu32XorStart;

    for (y = 0; y < height && ySkipXor == ~(uint32_t)0; y++, pu32Xor += width)
    {
        for (x = 0; x < width; x++)
        {
            if (pu32Xor[x] != 0)
            {
                ySkipXor = y;
                break;
            }
        }
    }

    if (ySkipXor == ~(uint32_t)0)
    {
        ySkipXor = 0;
    }

    /*
     * Find the left border of the XOR mask.
     */
    uint32_t xSkipXor = ~(uint32_t)0;

    /* For all columns. */
    for (x = 0; x < width && xSkipXor == ~(uint32_t)0; x++)
    {
        pu32Xor = pu32XorStart + x;    /* Currently checking dword. */

        for (y = ySkipXor; y < height; y++, pu32Xor += width)
        {
            if (*pu32Xor != 0)
            {
                xSkipXor = x;
                break;
            }
        }
    }

    if (xSkipXor == ~(uint32_t)0)
    {
        xSkipXor = 0;
    }

    *pxSkip = RT_MIN(xSkipAnd, xSkipXor);
    *pySkip = RT_MIN(ySkipAnd, ySkipXor);
}

/* Generate an AND mask for alpha pointers here, because
 * guest driver does not do that correctly for Vista pointers.
 * Similar fix, changing the alpha threshold, could be applied
 * for the guest driver, but then additions reinstall would be
 * necessary, which we try to avoid.
 */
static void mousePointerGenerateANDMask(uint8_t *pu8DstAndMask, int cbDstAndMask, const uint8_t *pu8SrcAlpha, int w, int h)
{
    memset(pu8DstAndMask, 0xFF, cbDstAndMask);

    int y;
    for (y = 0; y < h; y++)
    {
        uint8_t bitmask = 0x80;

        int x;
        for (x = 0; x < w; x++, bitmask >>= 1)
        {
            if (bitmask == 0)
            {
                bitmask = 0x80;
            }

            /* Whether alpha channel value is not transparent enough for the pixel to be seen. */
            if (pu8SrcAlpha[x * 4 + 3] > 0x7f)
            {
                pu8DstAndMask[x / 8] &= ~bitmask;
            }
        }

        /* Point to next source and dest scans. */
        pu8SrcAlpha += w * 4;
        pu8DstAndMask += (w + 7) / 8;
    }
}

STDMETHODIMP VRDPConsoleListener::OnMousePointerShapeChange(BOOL visible,
                                                            BOOL alpha,
                                                            ULONG xHot,
                                                            ULONG yHot,
                                                            ULONG width,
                                                            ULONG height,
                                                            ComSafeArrayIn(BYTE,inShape))
{
    LogSunlover(("VRDPConsoleListener::OnMousePointerShapeChange: %d, %d, %lux%lu, @%lu,%lu\n", visible, alpha, width, height, xHot, yHot));

    if (m_server)
    {
        com::SafeArray <BYTE> aShape(ComSafeArrayInArg (inShape));
        if (aShape.size() == 0)
        {
            if (!visible)
            {
                m_server->MousePointerHide();
            }
        }
        else if (width != 0 && height != 0)
        {
            /* Pointer consists of 1 bpp AND and 24 BPP XOR masks.
             * 'shape' AND mask followed by XOR mask.
             * XOR mask contains 32 bit (lsb)BGR0(msb) values.
             *
             * We convert this to RDP color format which consist of
             * one bpp AND mask and 24 BPP (BGR) color XOR image.
             *
             * RDP clients expect 8 aligned width and height of
             * pointer (preferably 32x32).
             *
             * They even contain bugs which do not appear for
             * 32x32 pointers but would appear for a 41x32 one.
             *
             * So set pointer size to 32x32. This can be done safely
             * because most pointers are 32x32.
             */
            uint8_t* shape = aShape.raw();

            dumpPointer(shape, width, height, true);

            int cbDstAndMask = (((width + 7) / 8) * height + 3) & ~3;

            uint8_t *pu8AndMask = shape;
            uint8_t *pu8XorMask = shape + cbDstAndMask;

            if (alpha)
            {
                pu8AndMask = (uint8_t*)alloca(cbDstAndMask);

                mousePointerGenerateANDMask(pu8AndMask, cbDstAndMask, pu8XorMask, width, height);
            }

            /* Windows guest alpha pointers are wider than 32 pixels.
             * Try to find out the top-left border of the pointer and
             * then copy only meaningful bits. All complete top rows
             * and all complete left columns where (AND == 1 && XOR == 0)
             * are skipped. Hot spot is adjusted.
             */
            uint32_t ySkip = 0; /* How many rows to skip at the top. */
            uint32_t xSkip = 0; /* How many columns to skip at the left. */

            findTopLeftBorder(pu8AndMask, pu8XorMask, width, height, &xSkip, &ySkip);

            /* Must not skip the hot spot. */
            xSkip = RT_MIN(xSkip, xHot);
            ySkip = RT_MIN(ySkip, yHot);

            /*
             * Compute size and allocate memory for the pointer.
             */
            const uint32_t dstwidth = 32;
            const uint32_t dstheight = 32;

            VRDPCOLORPOINTER *pointer = NULL;

            uint32_t dstmaskwidth = (dstwidth + 7) / 8;

            uint32_t rdpmaskwidth = dstmaskwidth;
            uint32_t rdpmasklen = dstheight * rdpmaskwidth;

            uint32_t rdpdatawidth = dstwidth * 3;
            uint32_t rdpdatalen = dstheight * rdpdatawidth;

            pointer = (VRDPCOLORPOINTER *)RTMemTmpAlloc(sizeof(VRDPCOLORPOINTER) + rdpmasklen + rdpdatalen);

            if (pointer)
            {
                uint8_t *maskarray = (uint8_t*)pointer + sizeof(VRDPCOLORPOINTER);
                uint8_t *dataarray = maskarray + rdpmasklen;

                memset(maskarray, 0xFF, rdpmasklen);
                memset(dataarray, 0x00, rdpdatalen);

                uint32_t srcmaskwidth = (width + 7) / 8;
                uint32_t srcdatawidth = width * 4;

                /* Copy AND mask. */
                uint8_t *src = pu8AndMask + ySkip * srcmaskwidth;
                uint8_t *dst = maskarray + (dstheight - 1) * rdpmaskwidth;

                uint32_t minheight = RT_MIN(height - ySkip, dstheight);
                uint32_t minwidth = RT_MIN(width - xSkip, dstwidth);

                unsigned x, y;

                for (y = 0; y < minheight; y++)
                {
                    for (x = 0; x < minwidth; x++)
                    {
                        uint32_t byteIndex = (x + xSkip) / 8;
                        uint32_t bitIndex = (x + xSkip) % 8;

                        bool bit = (src[byteIndex] & (1 << (7 - bitIndex))) != 0;

                        if (!bit)
                        {
                            byteIndex = x / 8;
                            bitIndex = x % 8;

                            dst[byteIndex] &= ~(1 << (7 - bitIndex));
                        }
                    }

                    src += srcmaskwidth;
                    dst -= rdpmaskwidth;
                }

                /* Point src to XOR mask */
                src = pu8XorMask + ySkip * srcdatawidth;
                dst = dataarray + (dstheight - 1) * rdpdatawidth;

                for (y = 0; y < minheight ; y++)
                {
                    for (x = 0; x < minwidth; x++)
                    {
                        memcpy(dst + x * 3, &src[4 * (x + xSkip)], 3);
                    }

                    src += srcdatawidth;
                    dst -= rdpdatawidth;
                }

                pointer->u16HotX = (uint16_t)(xHot - xSkip);
                pointer->u16HotY = (uint16_t)(yHot - ySkip);

                pointer->u16Width = (uint16_t)dstwidth;
                pointer->u16Height = (uint16_t)dstheight;

                pointer->u16MaskLen = (uint16_t)rdpmasklen;
                pointer->u16DataLen = (uint16_t)rdpdatalen;

                dumpPointer((uint8_t*)pointer + sizeof(*pointer), dstwidth, dstheight, false);

                m_server->MousePointerUpdate(pointer);

                RTMemTmpFree(pointer);
            }
        }
    }

    return S_OK;
}


// ConsoleVRDPServer
////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_VRDP
RTLDRMOD ConsoleVRDPServer::mVRDPLibrary;

PFNVRDPCREATESERVER ConsoleVRDPServer::mpfnVRDPCreateServer = NULL;

VRDPENTRYPOINTS_1 *ConsoleVRDPServer::mpEntryPoints = NULL;

VRDPCALLBACKS_1 ConsoleVRDPServer::mCallbacks =
{
    { VRDP_INTERFACE_VERSION_1, sizeof(VRDPCALLBACKS_1) },
    ConsoleVRDPServer::VRDPCallbackQueryProperty,
    ConsoleVRDPServer::VRDPCallbackClientLogon,
    ConsoleVRDPServer::VRDPCallbackClientConnect,
    ConsoleVRDPServer::VRDPCallbackClientDisconnect,
    ConsoleVRDPServer::VRDPCallbackIntercept,
    ConsoleVRDPServer::VRDPCallbackUSB,
    ConsoleVRDPServer::VRDPCallbackClipboard,
    ConsoleVRDPServer::VRDPCallbackFramebufferQuery,
    ConsoleVRDPServer::VRDPCallbackFramebufferLock,
    ConsoleVRDPServer::VRDPCallbackFramebufferUnlock,
    ConsoleVRDPServer::VRDPCallbackInput,
    ConsoleVRDPServer::VRDPCallbackVideoModeHint
};

DECLCALLBACK(int)  ConsoleVRDPServer::VRDPCallbackQueryProperty(void *pvCallback, uint32_t index, void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    int rc = VERR_NOT_SUPPORTED;

    switch (index)
    {
        case VRDP_QP_NETWORK_PORT:
        {
            /* This is obsolete, the VRDP server uses VRDP_QP_NETWORK_PORT_RANGE instead. */
            ULONG port = 0;

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)port;
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDP_QP_NETWORK_ADDRESS:
        {
            com::Bstr bstr;
            server->mConsole->getVRDPServer()->COMGETTER(NetAddress)(bstr.asOutParam());

            /* The server expects UTF8. */
            com::Utf8Str address = bstr;

            size_t cbAddress = address.length() + 1;

            if (cbAddress >= 0x10000)
            {
                /* More than 64K seems to be an  invalid address. */
                rc = VERR_TOO_MUCH_DATA;
                break;
            }

            if ((size_t)cbBuffer >= cbAddress)
            {
                memcpy(pvBuffer, address.c_str(), cbAddress);
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = (uint32_t)cbAddress;
        } break;

        case VRDP_QP_NUMBER_MONITORS:
        {
            ULONG cMonitors = 1;

            server->mConsole->machine()->COMGETTER(MonitorCount)(&cMonitors);

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)cMonitors;
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDP_QP_NETWORK_PORT_RANGE:
        {
            com::Bstr bstr;
            HRESULT hrc = server->mConsole->getVRDPServer()->COMGETTER(Ports)(bstr.asOutParam());

            if (hrc != S_OK)
            {
                bstr = "";
            }

            if (bstr == "0")
            {
                bstr = "3389";
            }

            /* The server expects UTF8. */
            com::Utf8Str portRange = bstr;

            size_t cbPortRange = portRange.length () + 1;

            if (cbPortRange >= 0x10000)
            {
                /* More than 64K seems to be an  invalid port range string. */
                rc = VERR_TOO_MUCH_DATA;
                break;
            }

            if ((size_t)cbBuffer >= cbPortRange)
            {
                memcpy(pvBuffer, portRange.c_str(), cbPortRange);
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = (uint32_t)cbPortRange;
        } break;

#ifdef VBOX_WITH_VRDP_VIDEO_CHANNEL
        case VRDP_QP_VIDEO_CHANNEL:
        {
            BOOL fVideoEnabled = FALSE;

            server->mConsole->getVRDPServer()->COMGETTER(VideoChannel)(&fVideoEnabled);

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)fVideoEnabled;
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDP_QP_VIDEO_CHANNEL_QUALITY:
        {
            ULONG ulQuality = 0;

            server->mConsole->getVRDPServer()->COMGETTER(VideoChannelQuality)(&ulQuality);

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)ulQuality;
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDP_QP_VIDEO_CHANNEL_SUNFLSH:
        {
            ULONG ulSunFlsh = 1;

            com::Bstr bstr;
            HRESULT hrc = server->mConsole->machine ()->GetExtraData(Bstr("VRDP/SunFlsh"), bstr.asOutParam());
            if (hrc == S_OK && !bstr.isEmpty())
            {
                com::Utf8Str sunFlsh = bstr;
                if (!sunFlsh.isEmpty())
                {
                    ulSunFlsh = sunFlsh.toUInt32();
                }
            }

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)ulSunFlsh;
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;
#endif /* VBOX_WITH_VRDP_VIDEO_CHANNEL */

        case VRDP_QP_FEATURE:
        {
            if (cbBuffer < sizeof(VRDPFEATURE))
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }

            size_t cbInfo = cbBuffer - RT_OFFSETOF(VRDPFEATURE, achInfo);

            VRDPFEATURE *pFeature = (VRDPFEATURE *)pvBuffer;

            size_t cchInfo = 0;
            rc = RTStrNLenEx(pFeature->achInfo, cbInfo, &cchInfo);

            if (RT_FAILURE(rc))
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }

            /* features are mapped to "VRDP/Feature/NAME" extra data. */
            com::Utf8Str extraData("VRDP/Feature/");
            extraData += pFeature->achInfo;

            com::Bstr bstrValue;

            /* @todo these features should be per client. */
            NOREF(pFeature->u32ClientId);

            if (   RTStrICmp(pFeature->achInfo, "Client/DisableDisplay") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableInput") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableAudio") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableUSB") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableClipboard") == 0
               )
            {
                HRESULT hrc = server->mConsole->machine ()->GetExtraData(com::Bstr(extraData), bstrValue.asOutParam());
                if (hrc == S_OK && !bstrValue.isEmpty())
                {
                    rc = VINF_SUCCESS;
                }
            }
            else
            {
                rc = VERR_NOT_SUPPORTED;
            }

            /* Copy the value string to the callers buffer. */
            if (rc == VINF_SUCCESS)
            {
                com::Utf8Str value = bstrValue;

                size_t cb = value.length() + 1;

                if ((size_t)cbInfo >= cb)
                {
                    memcpy(pFeature->achInfo, value.c_str(), cb);
                }
                else
                {
                    rc = VINF_BUFFER_OVERFLOW;
                }

                *pcbOut = (uint32_t)cb;
            }
        } break;

        case VRDP_SP_NETWORK_BIND_PORT:
        {
            if (cbBuffer != sizeof(uint32_t))
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }

            ULONG port = *(uint32_t *)pvBuffer;

            server->mVRDPBindPort = port;

            rc = VINF_SUCCESS;

            if (pcbOut)
            {
                *pcbOut = sizeof(uint32_t);
            }

            server->mConsole->onRemoteDisplayInfoChange();
        } break;

        default:
            break;
    }

    return rc;
}

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackClientLogon(void *pvCallback, uint32_t u32ClientId, const char *pszUser, const char *pszPassword, const char *pszDomain)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    return server->mConsole->VRDPClientLogon (u32ClientId, pszUser, pszPassword, pszDomain);
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackClientConnect (void *pvCallback, uint32_t u32ClientId)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    server->mConsole->VRDPClientConnect (u32ClientId);
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackClientDisconnect (void *pvCallback, uint32_t u32ClientId, uint32_t fu32Intercepted)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    server->mConsole->VRDPClientDisconnect (u32ClientId, fu32Intercepted);
}

DECLCALLBACK(int)  ConsoleVRDPServer::VRDPCallbackIntercept (void *pvCallback, uint32_t u32ClientId, uint32_t fu32Intercept, void **ppvIntercept)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    LogFlowFunc(("%x\n", fu32Intercept));

    int rc = VERR_NOT_SUPPORTED;

    switch (fu32Intercept)
    {
        case VRDP_CLIENT_INTERCEPT_AUDIO:
        {
            server->mConsole->VRDPInterceptAudio (u32ClientId);
            if (ppvIntercept)
            {
                *ppvIntercept = server;
            }
            rc = VINF_SUCCESS;
        } break;

        case VRDP_CLIENT_INTERCEPT_USB:
        {
            server->mConsole->VRDPInterceptUSB (u32ClientId, ppvIntercept);
            rc = VINF_SUCCESS;
        } break;

        case VRDP_CLIENT_INTERCEPT_CLIPBOARD:
        {
            server->mConsole->VRDPInterceptClipboard (u32ClientId);
            if (ppvIntercept)
            {
                *ppvIntercept = server;
            }
            rc = VINF_SUCCESS;
        } break;

        default:
            break;
    }

    return rc;
}

DECLCALLBACK(int)  ConsoleVRDPServer::VRDPCallbackUSB (void *pvCallback, void *pvIntercept, uint32_t u32ClientId, uint8_t u8Code, const void *pvRet, uint32_t cbRet)
{
#ifdef VBOX_WITH_USB
    return USBClientResponseCallback (pvIntercept, u32ClientId, u8Code, pvRet, cbRet);
#else
    return VERR_NOT_SUPPORTED;
#endif
}

DECLCALLBACK(int)  ConsoleVRDPServer::VRDPCallbackClipboard (void *pvCallback, void *pvIntercept, uint32_t u32ClientId, uint32_t u32Function, uint32_t u32Format, const void *pvData, uint32_t cbData)
{
    return ClipboardCallback (pvIntercept, u32ClientId, u32Function, u32Format, pvData, cbData);
}

DECLCALLBACK(bool) ConsoleVRDPServer::VRDPCallbackFramebufferQuery (void *pvCallback, unsigned uScreenId, VRDPFRAMEBUFFERINFO *pInfo)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    bool fAvailable = false;

    IFramebuffer *pfb = NULL;
    LONG xOrigin = 0;
    LONG yOrigin = 0;

    server->mConsole->getDisplay ()->GetFramebuffer (uScreenId, &pfb, &xOrigin, &yOrigin);

    if (pfb)
    {
        pfb->Lock ();

        /* Query framebuffer parameters. */
        ULONG lineSize = 0;
        pfb->COMGETTER(BytesPerLine) (&lineSize);

        ULONG bitsPerPixel = 0;
        pfb->COMGETTER(BitsPerPixel) (&bitsPerPixel);

        BYTE *address = NULL;
        pfb->COMGETTER(Address) (&address);

        ULONG height = 0;
        pfb->COMGETTER(Height) (&height);

        ULONG width = 0;
        pfb->COMGETTER(Width) (&width);

        /* Now fill the information as requested by the caller. */
        pInfo->pu8Bits = address;
        pInfo->xOrigin = xOrigin;
        pInfo->yOrigin = yOrigin;
        pInfo->cWidth = width;
        pInfo->cHeight = height;
        pInfo->cBitsPerPixel = bitsPerPixel;
        pInfo->cbLine = lineSize;

        pfb->Unlock ();

        fAvailable = true;
    }

    if (server->maFramebuffers[uScreenId])
    {
        server->maFramebuffers[uScreenId]->Release ();
    }
    server->maFramebuffers[uScreenId] = pfb;

    return fAvailable;
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackFramebufferLock (void *pvCallback, unsigned uScreenId)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    if (server->maFramebuffers[uScreenId])
    {
        server->maFramebuffers[uScreenId]->Lock ();
    }
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackFramebufferUnlock (void *pvCallback, unsigned uScreenId)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    if (server->maFramebuffers[uScreenId])
    {
        server->maFramebuffers[uScreenId]->Unlock ();
    }
}

static void fixKbdLockStatus (VRDPInputSynch *pInputSynch, IKeyboard *pKeyboard)
{
    if (   pInputSynch->cGuestNumLockAdaptions
        && (pInputSynch->fGuestNumLock != pInputSynch->fClientNumLock))
    {
        pInputSynch->cGuestNumLockAdaptions--;
        pKeyboard->PutScancode(0x45);
        pKeyboard->PutScancode(0x45 | 0x80);
    }
    if (   pInputSynch->cGuestCapsLockAdaptions
        && (pInputSynch->fGuestCapsLock != pInputSynch->fClientCapsLock))
    {
        pInputSynch->cGuestCapsLockAdaptions--;
        pKeyboard->PutScancode(0x3a);
        pKeyboard->PutScancode(0x3a | 0x80);
    }
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackInput (void *pvCallback, int type, const void *pvInput, unsigned cbInput)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);
    Console *pConsole = server->mConsole;

    switch (type)
    {
        case VRDP_INPUT_SCANCODE:
        {
            if (cbInput == sizeof (VRDPINPUTSCANCODE))
            {
                IKeyboard *pKeyboard = pConsole->getKeyboard ();

                const VRDPINPUTSCANCODE *pInputScancode = (VRDPINPUTSCANCODE *)pvInput;

                /* Track lock keys. */
                if (pInputScancode->uScancode == 0x45)
                {
                    server->m_InputSynch.fClientNumLock = !server->m_InputSynch.fClientNumLock;
                }
                else if (pInputScancode->uScancode == 0x3a)
                {
                    server->m_InputSynch.fClientCapsLock = !server->m_InputSynch.fClientCapsLock;
                }
                else if (pInputScancode->uScancode == 0x46)
                {
                    server->m_InputSynch.fClientScrollLock = !server->m_InputSynch.fClientScrollLock;
                }
                else if ((pInputScancode->uScancode & 0x80) == 0)
                {
                    /* Key pressed. */
                    fixKbdLockStatus (&server->m_InputSynch, pKeyboard);
                }

                pKeyboard->PutScancode((LONG)pInputScancode->uScancode);
            }
        } break;

        case VRDP_INPUT_POINT:
        {
            if (cbInput == sizeof (VRDPINPUTPOINT))
            {
                const VRDPINPUTPOINT *pInputPoint = (VRDPINPUTPOINT *)pvInput;

                int mouseButtons = 0;
                int iWheel = 0;

                if (pInputPoint->uButtons & VRDP_INPUT_POINT_BUTTON1)
                {
                    mouseButtons |= MouseButtonState_LeftButton;
                }
                if (pInputPoint->uButtons & VRDP_INPUT_POINT_BUTTON2)
                {
                    mouseButtons |= MouseButtonState_RightButton;
                }
                if (pInputPoint->uButtons & VRDP_INPUT_POINT_BUTTON3)
                {
                    mouseButtons |= MouseButtonState_MiddleButton;
                }
                if (pInputPoint->uButtons & VRDP_INPUT_POINT_WHEEL_UP)
                {
                    mouseButtons |= MouseButtonState_WheelUp;
                    iWheel = -1;
                }
                if (pInputPoint->uButtons & VRDP_INPUT_POINT_WHEEL_DOWN)
                {
                    mouseButtons |= MouseButtonState_WheelDown;
                    iWheel = 1;
                }

                if (server->m_fGuestWantsAbsolute)
                {
                    pConsole->getMouse()->PutMouseEventAbsolute (pInputPoint->x + 1, pInputPoint->y + 1, iWheel, 0 /* Horizontal wheel */, mouseButtons);
                } else
                {
                    pConsole->getMouse()->PutMouseEvent (pInputPoint->x - server->m_mousex,
                                                         pInputPoint->y - server->m_mousey,
                                                         iWheel, 0 /* Horizontal wheel */, mouseButtons);
                    server->m_mousex = pInputPoint->x;
                    server->m_mousey = pInputPoint->y;
                }
            }
        } break;

        case VRDP_INPUT_CAD:
        {
            pConsole->getKeyboard ()->PutCAD();
        } break;

        case VRDP_INPUT_RESET:
        {
            pConsole->Reset();
        } break;

        case VRDP_INPUT_SYNCH:
        {
            if (cbInput == sizeof (VRDPINPUTSYNCH))
            {
                IKeyboard *pKeyboard = pConsole->getKeyboard ();

                const VRDPINPUTSYNCH *pInputSynch = (VRDPINPUTSYNCH *)pvInput;

                server->m_InputSynch.fClientNumLock    = (pInputSynch->uLockStatus & VRDP_INPUT_SYNCH_NUMLOCK) != 0;
                server->m_InputSynch.fClientCapsLock   = (pInputSynch->uLockStatus & VRDP_INPUT_SYNCH_CAPITAL) != 0;
                server->m_InputSynch.fClientScrollLock = (pInputSynch->uLockStatus & VRDP_INPUT_SYNCH_SCROLL) != 0;

                /* The client initiated synchronization. Always make the guest to reflect the client state.
                 * Than means, when the guest changes the state itself, it is forced to return to the client
                 * state.
                 */
                if (server->m_InputSynch.fClientNumLock != server->m_InputSynch.fGuestNumLock)
                {
                    server->m_InputSynch.cGuestNumLockAdaptions = 2;
                }

                if (server->m_InputSynch.fClientCapsLock != server->m_InputSynch.fGuestCapsLock)
                {
                    server->m_InputSynch.cGuestCapsLockAdaptions = 2;
                }

                fixKbdLockStatus (&server->m_InputSynch, pKeyboard);
            }
        } break;

        default:
            break;
    }
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackVideoModeHint (void *pvCallback, unsigned cWidth, unsigned cHeight, unsigned cBitsPerPixel, unsigned uScreenId)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    server->mConsole->getDisplay()->SetVideoModeHint(cWidth, cHeight, cBitsPerPixel, uScreenId);
}
#endif /* VBOX_WITH_VRDP */

ConsoleVRDPServer::ConsoleVRDPServer (Console *console)
{
    mConsole = console;

    int rc = RTCritSectInit(&mCritSect);
    AssertRC(rc);

    mcClipboardRefs = 0;
    mpfnClipboardCallback = NULL;

#ifdef VBOX_WITH_USB
    mUSBBackends.pHead = NULL;
    mUSBBackends.pTail = NULL;

    mUSBBackends.thread = NIL_RTTHREAD;
    mUSBBackends.fThreadRunning = false;
    mUSBBackends.event = 0;
#endif

#ifdef VBOX_WITH_VRDP
    mhServer = 0;

    m_fGuestWantsAbsolute = false;
    m_mousex = 0;
    m_mousey = 0;

    m_InputSynch.cGuestNumLockAdaptions = 2;
    m_InputSynch.cGuestCapsLockAdaptions = 2;

    m_InputSynch.fGuestNumLock    = false;
    m_InputSynch.fGuestCapsLock   = false;
    m_InputSynch.fGuestScrollLock = false;

    m_InputSynch.fClientNumLock    = false;
    m_InputSynch.fClientCapsLock   = false;
    m_InputSynch.fClientScrollLock = false;

    memset (maFramebuffers, 0, sizeof (maFramebuffers));

    {
        ComPtr<IEventSource> es;
        console->COMGETTER(EventSource)(es.asOutParam());
        mConsoleListener = new VRDPConsoleListener(this);
        mConsoleListener->AddRef();
        com::SafeArray <VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnMousePointerShapeChanged);
        eventTypes.push_back(VBoxEventType_OnMouseCapabilityChanged);
        eventTypes.push_back(VBoxEventType_OnKeyboardLedsChanged);
        es->RegisterListener(mConsoleListener, ComSafeArrayAsInParam(eventTypes), true);
    }

    mVRDPBindPort = -1;
#endif /* VBOX_WITH_VRDP */

    mAuthLibrary = 0;
}

ConsoleVRDPServer::~ConsoleVRDPServer ()
{
    Stop ();

#ifdef VBOX_WITH_VRDP
    if (mConsoleListener)
    {
        ComPtr<IEventSource> es;
        mConsole->COMGETTER(EventSource)(es.asOutParam());
        es->UnregisterListener(mConsoleListener);
        mConsoleListener->Release();
        mConsoleListener = NULL;
    }

    unsigned i;
    for (i = 0; i < RT_ELEMENTS(maFramebuffers); i++)
    {
        if (maFramebuffers[i])
        {
            maFramebuffers[i]->Release ();
            maFramebuffers[i] = NULL;
        }
    }
#endif /* VBOX_WITH_VRDP */

    if (RTCritSectIsInitialized (&mCritSect))
    {
        RTCritSectDelete (&mCritSect);
        memset (&mCritSect, 0, sizeof (mCritSect));
    }
}

int ConsoleVRDPServer::Launch (void)
{
    LogFlowThisFunc(("\n"));
#ifdef VBOX_WITH_VRDP
    int rc = VINF_SUCCESS;
    IVRDPServer *vrdpserver = mConsole->getVRDPServer ();
    Assert(vrdpserver);
    BOOL vrdpEnabled = FALSE;

    HRESULT rc2 = vrdpserver->COMGETTER(Enabled) (&vrdpEnabled);
    AssertComRC(rc2);

    if (SUCCEEDED(rc2) && vrdpEnabled)
    {
        if (loadVRDPLibrary ())
        {
            rc = mpfnVRDPCreateServer (&mCallbacks.header, this, (VRDPINTERFACEHDR **)&mpEntryPoints, &mhServer);

            if (RT_SUCCESS(rc))
            {
#ifdef VBOX_WITH_USB
                remoteUSBThreadStart ();
#endif /* VBOX_WITH_USB */
            }
            else if (rc != VERR_NET_ADDRESS_IN_USE)
                AssertMsgFailed(("Could not start VRDP server: rc = %Rrc\n", rc));
        }
        else
        {
            AssertMsgFailed(("Could not load the VRDP library\n"));
            rc = VERR_FILE_NOT_FOUND;
        }
    }
#else
    int rc = VERR_NOT_SUPPORTED;
    LogRel(("VRDP: this version does not include the VRDP server.\n"));
#endif /* VBOX_WITH_VRDP */
    return rc;
}

void ConsoleVRDPServer::EnableConnections (void)
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPEnableConnections (mhServer, true);
    }
#endif /* VBOX_WITH_VRDP */
}

void ConsoleVRDPServer::DisconnectClient (uint32_t u32ClientId, bool fReconnect)
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPDisconnect (mhServer, u32ClientId, fReconnect);
    }
#endif /* VBOX_WITH_VRDP */
}

void ConsoleVRDPServer::MousePointerUpdate (const VRDPCOLORPOINTER *pPointer)
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPColorPointer (mhServer, pPointer);
    }
#endif /* VBOX_WITH_VRDP */
}

void ConsoleVRDPServer::MousePointerHide (void)
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPHidePointer (mhServer);
    }
#endif /* VBOX_WITH_VRDP */
}

void ConsoleVRDPServer::Stop (void)
{
    Assert(VALID_PTR(this)); /** @todo r=bird: there are(/was) some odd cases where this buster was invalid on
                              * linux. Just remove this when it's 100% sure that problem has been fixed. */
#ifdef VBOX_WITH_VRDP
    if (mhServer)
    {
        HVRDPSERVER hServer = mhServer;

        /* Reset the handle to avoid further calls to the server. */
        mhServer = 0;

        if (mpEntryPoints && hServer)
        {
            mpEntryPoints->VRDPDestroy (hServer);
        }
    }
#endif /* VBOX_WITH_VRDP */

#ifdef VBOX_WITH_USB
    remoteUSBThreadStop ();
#endif /* VBOX_WITH_USB */

    mpfnAuthEntry = NULL;
    mpfnAuthEntry2 = NULL;

    if (mAuthLibrary)
    {
        RTLdrClose(mAuthLibrary);
        mAuthLibrary = 0;
    }
}

/* Worker thread for Remote USB. The thread polls the clients for
 * the list of attached USB devices.
 * The thread is also responsible for attaching/detaching devices
 * to/from the VM.
 *
 * It is expected that attaching/detaching is not a frequent operation.
 *
 * The thread is always running when the VRDP server is active.
 *
 * The thread scans backends and requests the device list every 2 seconds.
 *
 * When device list is available, the thread calls the Console to process it.
 *
 */
#define VRDP_DEVICE_LIST_PERIOD_MS (2000)

#ifdef VBOX_WITH_USB
static DECLCALLBACK(int) threadRemoteUSB (RTTHREAD self, void *pvUser)
{
    ConsoleVRDPServer *pOwner = (ConsoleVRDPServer *)pvUser;

    LogFlow(("Console::threadRemoteUSB: start. owner = %p.\n", pOwner));

    pOwner->notifyRemoteUSBThreadRunning (self);

    while (pOwner->isRemoteUSBThreadRunning ())
    {
        RemoteUSBBackend *pRemoteUSBBackend = NULL;

        while ((pRemoteUSBBackend = pOwner->usbBackendGetNext (pRemoteUSBBackend)) != NULL)
        {
            pRemoteUSBBackend->PollRemoteDevices ();
        }

        pOwner->waitRemoteUSBThreadEvent (VRDP_DEVICE_LIST_PERIOD_MS);

        LogFlow(("Console::threadRemoteUSB: iteration. owner = %p.\n", pOwner));
    }

    return VINF_SUCCESS;
}

void ConsoleVRDPServer::notifyRemoteUSBThreadRunning (RTTHREAD thread)
{
    mUSBBackends.thread = thread;
    mUSBBackends.fThreadRunning = true;
    int rc = RTThreadUserSignal (thread);
    AssertRC(rc);
}

bool ConsoleVRDPServer::isRemoteUSBThreadRunning (void)
{
    return mUSBBackends.fThreadRunning;
}

void ConsoleVRDPServer::waitRemoteUSBThreadEvent (RTMSINTERVAL cMillies)
{
    int rc = RTSemEventWait (mUSBBackends.event, cMillies);
    Assert (RT_SUCCESS(rc) || rc == VERR_TIMEOUT);
    NOREF(rc);
}

void ConsoleVRDPServer::remoteUSBThreadStart (void)
{
    int rc = RTSemEventCreate (&mUSBBackends.event);

    if (RT_FAILURE(rc))
    {
        AssertFailed ();
        mUSBBackends.event = 0;
    }

    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate (&mUSBBackends.thread, threadRemoteUSB, this, 65536,
                             RTTHREADTYPE_VRDP_IO, RTTHREADFLAGS_WAITABLE, "remote usb");
    }

    if (RT_FAILURE(rc))
    {
        LogRel(("Warning: could not start the remote USB thread, rc = %Rrc!!!\n", rc));
        mUSBBackends.thread = NIL_RTTHREAD;
    }
    else
    {
        /* Wait until the thread is ready. */
        rc = RTThreadUserWait (mUSBBackends.thread, 60000);
        AssertRC(rc);
        Assert (mUSBBackends.fThreadRunning || RT_FAILURE(rc));
    }
}

void ConsoleVRDPServer::remoteUSBThreadStop (void)
{
    mUSBBackends.fThreadRunning = false;

    if (mUSBBackends.thread != NIL_RTTHREAD)
    {
        Assert (mUSBBackends.event != 0);

        RTSemEventSignal (mUSBBackends.event);

        int rc = RTThreadWait (mUSBBackends.thread, 60000, NULL);
        AssertRC(rc);

        mUSBBackends.thread = NIL_RTTHREAD;
    }

    if (mUSBBackends.event)
    {
        RTSemEventDestroy (mUSBBackends.event);
        mUSBBackends.event = 0;
    }
}
#endif /* VBOX_WITH_USB */

VRDPAuthResult ConsoleVRDPServer::Authenticate (const Guid &uuid, VRDPAuthGuestJudgement guestJudgement,
                                                const char *pszUser, const char *pszPassword, const char *pszDomain,
                                                uint32_t u32ClientId)
{
    VRDPAUTHUUID rawuuid;

    memcpy (rawuuid, ((Guid &)uuid).ptr (), sizeof (rawuuid));

    LogFlow(("ConsoleVRDPServer::Authenticate: uuid = %RTuuid, guestJudgement = %d, pszUser = %s, pszPassword = %s, pszDomain = %s, u32ClientId = %d\n",
             rawuuid, guestJudgement, pszUser, pszPassword, pszDomain, u32ClientId));

    /*
     * Called only from VRDP input thread. So thread safety is not required.
     */

    if (!mAuthLibrary)
    {
        /* Load the external authentication library. */

        ComPtr<IMachine> machine;
        mConsole->COMGETTER(Machine)(machine.asOutParam());

        ComPtr<IVirtualBox> virtualBox;
        machine->COMGETTER(Parent)(virtualBox.asOutParam());

        ComPtr<ISystemProperties> systemProperties;
        virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());

        Bstr authLibrary;
        systemProperties->COMGETTER(RemoteDisplayAuthLibrary)(authLibrary.asOutParam());

        Utf8Str filename = authLibrary;

        LogRel(("VRDPAUTH: ConsoleVRDPServer::Authenticate: loading external authentication library '%ls'\n", authLibrary.raw()));

        int rc;
        if (RTPathHavePath(filename.c_str()))
            rc = RTLdrLoad(filename.c_str(), &mAuthLibrary);
        else
            rc = RTLdrLoadAppPriv(filename.c_str(), &mAuthLibrary);

        if (RT_FAILURE(rc))
            LogRel(("VRDPAUTH: Failed to load external authentication library. Error code: %Rrc\n", rc));

        if (RT_SUCCESS(rc))
        {
            /* Get the entry point. */
            mpfnAuthEntry2 = NULL;
            int rc2 = RTLdrGetSymbol(mAuthLibrary, "VRDPAuth2", (void**)&mpfnAuthEntry2);
            if (RT_FAILURE(rc2))
            {
                if (rc2 != VERR_SYMBOL_NOT_FOUND)
                {
                    LogRel(("VRDPAUTH: Could not resolve import '%s'. Error code: %Rrc\n", "VRDPAuth2", rc2));
                }
                rc = rc2;
            }

            /* Get the entry point. */
            mpfnAuthEntry = NULL;
            rc2 = RTLdrGetSymbol(mAuthLibrary, "VRDPAuth", (void**)&mpfnAuthEntry);
            if (RT_FAILURE(rc2))
            {
                if (rc2 != VERR_SYMBOL_NOT_FOUND)
                {
                    LogRel(("VRDPAUTH: Could not resolve import '%s'. Error code: %Rrc\n", "VRDPAuth", rc2));
                }
                rc = rc2;
            }

            if (mpfnAuthEntry2 || mpfnAuthEntry)
            {
                LogRel(("VRDPAUTH: Using entry point '%s'.\n", mpfnAuthEntry2? "VRDPAuth2": "VRDPAuth"));
                rc = VINF_SUCCESS;
            }
        }

        if (RT_FAILURE(rc))
        {
            mConsole->setError(E_FAIL,
                               mConsole->tr("Could not load the external authentication library '%s' (%Rrc)"),
                               filename.c_str(),
                               rc);

            mpfnAuthEntry = NULL;
            mpfnAuthEntry2 = NULL;

            if (mAuthLibrary)
            {
                RTLdrClose(mAuthLibrary);
                mAuthLibrary = 0;
            }

            return VRDPAuthAccessDenied;
        }
    }

    Assert (mAuthLibrary && (mpfnAuthEntry || mpfnAuthEntry2));

    VRDPAuthResult result = mpfnAuthEntry2?
                                mpfnAuthEntry2 (&rawuuid, guestJudgement, pszUser, pszPassword, pszDomain, true, u32ClientId):
                                mpfnAuthEntry (&rawuuid, guestJudgement, pszUser, pszPassword, pszDomain);

    switch (result)
    {
        case VRDPAuthAccessDenied:
            LogRel(("VRDPAUTH: external authentication module returned 'access denied'\n"));
            break;
        case VRDPAuthAccessGranted:
            LogRel(("VRDPAUTH: external authentication module returned 'access granted'\n"));
            break;
        case VRDPAuthDelegateToGuest:
            LogRel(("VRDPAUTH: external authentication module returned 'delegate request to guest'\n"));
            break;
        default:
            LogRel(("VRDPAUTH: external authentication module returned incorrect return code %d\n", result));
            result = VRDPAuthAccessDenied;
    }

    LogFlow(("ConsoleVRDPServer::Authenticate: result = %d\n", result));

    return result;
}

void ConsoleVRDPServer::AuthDisconnect (const Guid &uuid, uint32_t u32ClientId)
{
    VRDPAUTHUUID rawuuid;

    memcpy (rawuuid, ((Guid &)uuid).ptr (), sizeof (rawuuid));

    LogFlow(("ConsoleVRDPServer::AuthDisconnect: uuid = %RTuuid, u32ClientId = %d\n",
             rawuuid, u32ClientId));

    Assert (mAuthLibrary && (mpfnAuthEntry || mpfnAuthEntry2));

    if (mpfnAuthEntry2)
        mpfnAuthEntry2 (&rawuuid, VRDPAuthGuestNotAsked, NULL, NULL, NULL, false, u32ClientId);
}

int ConsoleVRDPServer::lockConsoleVRDPServer (void)
{
    int rc = RTCritSectEnter (&mCritSect);
    AssertRC(rc);
    return rc;
}

void ConsoleVRDPServer::unlockConsoleVRDPServer (void)
{
    RTCritSectLeave (&mCritSect);
}

DECLCALLBACK(int) ConsoleVRDPServer::ClipboardCallback (void *pvCallback,
                                                        uint32_t u32ClientId,
                                                        uint32_t u32Function,
                                                        uint32_t u32Format,
                                                        const void *pvData,
                                                        uint32_t cbData)
{
    LogFlowFunc(("pvCallback = %p, u32ClientId = %d, u32Function = %d, u32Format = 0x%08X, pvData = %p, cbData = %d\n",
                 pvCallback, u32ClientId, u32Function, u32Format, pvData, cbData));

    int rc = VINF_SUCCESS;

    ConsoleVRDPServer *pServer = static_cast <ConsoleVRDPServer *>(pvCallback);

    NOREF(u32ClientId);

    switch (u32Function)
    {
        case VRDP_CLIPBOARD_FUNCTION_FORMAT_ANNOUNCE:
        {
            if (pServer->mpfnClipboardCallback)
            {
                pServer->mpfnClipboardCallback (VBOX_CLIPBOARD_EXT_FN_FORMAT_ANNOUNCE,
                                                u32Format,
                                                (void *)pvData,
                                                cbData);
            }
        } break;

        case VRDP_CLIPBOARD_FUNCTION_DATA_READ:
        {
            if (pServer->mpfnClipboardCallback)
            {
                pServer->mpfnClipboardCallback (VBOX_CLIPBOARD_EXT_FN_DATA_READ,
                                                u32Format,
                                                (void *)pvData,
                                                cbData);
            }
        } break;

        default:
            rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

DECLCALLBACK(int) ConsoleVRDPServer::ClipboardServiceExtension (void *pvExtension,
                                                                uint32_t u32Function,
                                                                void *pvParms,
                                                                uint32_t cbParms)
{
    LogFlowFunc(("pvExtension = %p, u32Function = %d, pvParms = %p, cbParms = %d\n",
                 pvExtension, u32Function, pvParms, cbParms));

    int rc = VINF_SUCCESS;

#ifdef VBOX_WITH_VRDP
    ConsoleVRDPServer *pServer = static_cast <ConsoleVRDPServer *>(pvExtension);

    VBOXCLIPBOARDEXTPARMS *pParms = (VBOXCLIPBOARDEXTPARMS *)pvParms;

    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK:
        {
            pServer->mpfnClipboardCallback = pParms->u.pfnCallback;
        } break;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_ANNOUNCE:
        {
            /* The guest announces clipboard formats. This must be delivered to all clients. */
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDPClipboard (pServer->mhServer,
                                              VRDP_CLIPBOARD_FUNCTION_FORMAT_ANNOUNCE,
                                              pParms->u32Format,
                                              NULL,
                                              0,
                                              NULL);
            }
        } break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
        {
            /* The clipboard service expects that the pvData buffer will be filled
             * with clipboard data. The server returns the data from the client that
             * announced the requested format most recently.
             */
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDPClipboard (pServer->mhServer,
                                              VRDP_CLIPBOARD_FUNCTION_DATA_READ,
                                              pParms->u32Format,
                                              pParms->u.pvData,
                                              pParms->cbData,
                                              &pParms->cbData);
            }
        } break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        {
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDPClipboard (pServer->mhServer,
                                              VRDP_CLIPBOARD_FUNCTION_DATA_WRITE,
                                              pParms->u32Format,
                                              pParms->u.pvData,
                                              pParms->cbData,
                                              NULL);
            }
        } break;

        default:
            rc = VERR_NOT_SUPPORTED;
    }
#endif /* VBOX_WITH_VRDP */

    return rc;
}

void ConsoleVRDPServer::ClipboardCreate (uint32_t u32ClientId)
{
    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        if (mcClipboardRefs == 0)
        {
            rc = HGCMHostRegisterServiceExtension (&mhClipboard, "VBoxSharedClipboard", ClipboardServiceExtension, this);

            if (RT_SUCCESS(rc))
            {
                mcClipboardRefs++;
            }
        }

        unlockConsoleVRDPServer ();
    }
}

void ConsoleVRDPServer::ClipboardDelete (uint32_t u32ClientId)
{
    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        mcClipboardRefs--;

        if (mcClipboardRefs == 0)
        {
            HGCMHostUnregisterServiceExtension (mhClipboard);
        }

        unlockConsoleVRDPServer ();
    }
}

/* That is called on INPUT thread of the VRDP server.
 * The ConsoleVRDPServer keeps a list of created backend instances.
 */
void ConsoleVRDPServer::USBBackendCreate (uint32_t u32ClientId, void **ppvIntercept)
{
#ifdef VBOX_WITH_USB
    LogFlow(("ConsoleVRDPServer::USBBackendCreate: u32ClientId = %d\n", u32ClientId));

    /* Create a new instance of the USB backend for the new client. */
    RemoteUSBBackend *pRemoteUSBBackend = new RemoteUSBBackend (mConsole, this, u32ClientId);

    if (pRemoteUSBBackend)
    {
        pRemoteUSBBackend->AddRef (); /* 'Release' called in USBBackendDelete. */

        /* Append the new instance in the list. */
        int rc = lockConsoleVRDPServer ();

        if (RT_SUCCESS(rc))
        {
            pRemoteUSBBackend->pNext = mUSBBackends.pHead;
            if (mUSBBackends.pHead)
            {
                mUSBBackends.pHead->pPrev = pRemoteUSBBackend;
            }
            else
            {
                mUSBBackends.pTail = pRemoteUSBBackend;
            }

            mUSBBackends.pHead = pRemoteUSBBackend;

            unlockConsoleVRDPServer ();

            if (ppvIntercept)
            {
                *ppvIntercept = pRemoteUSBBackend;
            }
        }

        if (RT_FAILURE(rc))
        {
            pRemoteUSBBackend->Release ();
        }
    }
#endif /* VBOX_WITH_USB */
}

void ConsoleVRDPServer::USBBackendDelete (uint32_t u32ClientId)
{
#ifdef VBOX_WITH_USB
    LogFlow(("ConsoleVRDPServer::USBBackendDelete: u32ClientId = %d\n", u32ClientId));

    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        pRemoteUSBBackend = usbBackendFind (u32ClientId);

        if (pRemoteUSBBackend)
        {
            /* Notify that it will be deleted. */
            pRemoteUSBBackend->NotifyDelete ();
        }

        unlockConsoleVRDPServer ();
    }

    if (pRemoteUSBBackend)
    {
        /* Here the instance has been excluded from the list and can be dereferenced. */
        pRemoteUSBBackend->Release ();
    }
#endif
}

void *ConsoleVRDPServer::USBBackendRequestPointer (uint32_t u32ClientId, const Guid *pGuid)
{
#ifdef VBOX_WITH_USB
    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        pRemoteUSBBackend = usbBackendFind (u32ClientId);

        if (pRemoteUSBBackend)
        {
            /* Inform the backend instance that it is referenced by the Guid. */
            bool fAdded = pRemoteUSBBackend->addUUID (pGuid);

            if (fAdded)
            {
                /* Reference the instance because its pointer is being taken. */
                pRemoteUSBBackend->AddRef (); /* 'Release' is called in USBBackendReleasePointer. */
            }
            else
            {
                pRemoteUSBBackend = NULL;
            }
        }

        unlockConsoleVRDPServer ();
    }

    if (pRemoteUSBBackend)
    {
        return pRemoteUSBBackend->GetBackendCallbackPointer ();
    }

#endif
    return NULL;
}

void ConsoleVRDPServer::USBBackendReleasePointer (const Guid *pGuid)
{
#ifdef VBOX_WITH_USB
    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        pRemoteUSBBackend = usbBackendFindByUUID (pGuid);

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->removeUUID (pGuid);
        }

        unlockConsoleVRDPServer ();

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->Release ();
        }
    }
#endif
}

RemoteUSBBackend *ConsoleVRDPServer::usbBackendGetNext (RemoteUSBBackend *pRemoteUSBBackend)
{
    LogFlow(("ConsoleVRDPServer::usbBackendGetNext: pBackend = %p\n", pRemoteUSBBackend));

    RemoteUSBBackend *pNextRemoteUSBBackend = NULL;
#ifdef VBOX_WITH_USB

    int rc = lockConsoleVRDPServer ();

    if (RT_SUCCESS(rc))
    {
        if (pRemoteUSBBackend == NULL)
        {
            /* The first backend in the list is requested. */
            pNextRemoteUSBBackend = mUSBBackends.pHead;
        }
        else
        {
            /* Get pointer to the next backend. */
            pNextRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
        }

        if (pNextRemoteUSBBackend)
        {
            pNextRemoteUSBBackend->AddRef ();
        }

        unlockConsoleVRDPServer ();

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->Release ();
        }
    }
#endif

    return pNextRemoteUSBBackend;
}

#ifdef VBOX_WITH_USB
/* Internal method. Called under the ConsoleVRDPServerLock. */
RemoteUSBBackend *ConsoleVRDPServer::usbBackendFind (uint32_t u32ClientId)
{
    RemoteUSBBackend *pRemoteUSBBackend = mUSBBackends.pHead;

    while (pRemoteUSBBackend)
    {
        if (pRemoteUSBBackend->ClientId () == u32ClientId)
        {
            break;
        }

        pRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    return pRemoteUSBBackend;
}

/* Internal method. Called under the ConsoleVRDPServerLock. */
RemoteUSBBackend *ConsoleVRDPServer::usbBackendFindByUUID (const Guid *pGuid)
{
    RemoteUSBBackend *pRemoteUSBBackend = mUSBBackends.pHead;

    while (pRemoteUSBBackend)
    {
        if (pRemoteUSBBackend->findUUID (pGuid))
        {
            break;
        }

        pRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    return pRemoteUSBBackend;
}
#endif

/* Internal method. Called by the backend destructor. */
void ConsoleVRDPServer::usbBackendRemoveFromList (RemoteUSBBackend *pRemoteUSBBackend)
{
#ifdef VBOX_WITH_USB
    int rc = lockConsoleVRDPServer ();
    AssertRC(rc);

    /* Exclude the found instance from the list. */
    if (pRemoteUSBBackend->pNext)
    {
        pRemoteUSBBackend->pNext->pPrev = pRemoteUSBBackend->pPrev;
    }
    else
    {
        mUSBBackends.pTail = (RemoteUSBBackend *)pRemoteUSBBackend->pPrev;
    }

    if (pRemoteUSBBackend->pPrev)
    {
        pRemoteUSBBackend->pPrev->pNext = pRemoteUSBBackend->pNext;
    }
    else
    {
        mUSBBackends.pHead = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    pRemoteUSBBackend->pNext = pRemoteUSBBackend->pPrev = NULL;

    unlockConsoleVRDPServer ();
#endif
}


void ConsoleVRDPServer::SendUpdate (unsigned uScreenId, void *pvUpdate, uint32_t cbUpdate) const
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPUpdate (mhServer, uScreenId, pvUpdate, cbUpdate);
    }
#endif
}

void ConsoleVRDPServer::SendResize (void) const
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPResize (mhServer);
    }
#endif
}

void ConsoleVRDPServer::SendUpdateBitmap (unsigned uScreenId, uint32_t x, uint32_t y, uint32_t w, uint32_t h) const
{
#ifdef VBOX_WITH_VRDP
    VRDPORDERHDR update;
    update.x = x;
    update.y = y;
    update.w = w;
    update.h = h;
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPUpdate (mhServer, uScreenId, &update, sizeof (update));
    }
#endif
}

void ConsoleVRDPServer::SendAudioSamples (void *pvSamples, uint32_t cSamples, VRDPAUDIOFORMAT format) const
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPAudioSamples (mhServer, pvSamples, cSamples, format);
    }
#endif
}

void ConsoleVRDPServer::SendAudioVolume (uint16_t left, uint16_t right) const
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPAudioVolume (mhServer, left, right);
    }
#endif
}

void ConsoleVRDPServer::SendUSBRequest (uint32_t u32ClientId, void *pvParms, uint32_t cbParms) const
{
#ifdef VBOX_WITH_VRDP
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPUSBRequest (mhServer, u32ClientId, pvParms, cbParms);
    }
#endif
}

void ConsoleVRDPServer::QueryInfo (uint32_t index, void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut) const
{
#ifdef VBOX_WITH_VRDP
    if (index == VRDP_QI_PORT)
    {
        uint32_t cbOut = sizeof (int32_t);

        if (cbBuffer >= cbOut)
        {
            *pcbOut = cbOut;
            *(int32_t *)pvBuffer = (int32_t)mVRDPBindPort;
        }
    }
    else if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDPQueryInfo (mhServer, index, pvBuffer, cbBuffer, pcbOut);
    }
#endif
}

#ifdef VBOX_WITH_VRDP
/* note: static function now! */
bool ConsoleVRDPServer::loadVRDPLibrary (void)
{
    int rc = VINF_SUCCESS;

    if (!mVRDPLibrary)
    {
        rc = SUPR3HardenedLdrLoadAppPriv ("VBoxVRDP", &mVRDPLibrary);

        if (RT_SUCCESS(rc))
        {
            LogFlow(("VRDPServer::loadLibrary(): successfully loaded VRDP library.\n"));

            struct SymbolEntry
            {
                const char *name;
                void **ppfn;
            };

            #define DEFSYMENTRY(a) { #a, (void**)&mpfn##a }

            static const struct SymbolEntry symbols[] =
            {
                DEFSYMENTRY(VRDPCreateServer)
            };

            #undef DEFSYMENTRY

            for (unsigned i = 0; i < RT_ELEMENTS(symbols); i++)
            {
                rc = RTLdrGetSymbol(mVRDPLibrary, symbols[i].name, symbols[i].ppfn);

                AssertMsgRC(rc, ("Error resolving VRDP symbol %s\n", symbols[i].name));

                if (RT_FAILURE(rc))
                {
                    break;
                }
            }
        }
        else
        {
            LogRel(("VRDPServer::loadLibrary(): failed to load VRDP library! VRDP not available: rc = %Rrc\n", rc));
            mVRDPLibrary = NULL;
        }
    }

    // just to be safe
    if (RT_FAILURE(rc))
    {
        if (mVRDPLibrary)
        {
            RTLdrClose (mVRDPLibrary);
            mVRDPLibrary = NULL;
        }
    }

    return (mVRDPLibrary != NULL);
}
#endif /* VBOX_WITH_VRDP */

/*
 * IRemoteDisplayInfo implementation.
 */
// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

RemoteDisplayInfo::RemoteDisplayInfo()
    : mParent(NULL)
{
}

RemoteDisplayInfo::~RemoteDisplayInfo()
{
}


HRESULT RemoteDisplayInfo::FinalConstruct()
{
    return S_OK;
}

void RemoteDisplayInfo::FinalRelease()
{
    uninit ();
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the guest object.
 */
HRESULT RemoteDisplayInfo::init (Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void RemoteDisplayInfo::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
}

// IRemoteDisplayInfo properties
/////////////////////////////////////////////////////////////////////////////

#define IMPL_GETTER_BOOL(_aType, _aName, _aIndex)                         \
    STDMETHODIMP RemoteDisplayInfo::COMGETTER(_aName) (_aType *a##_aName) \
    {                                                                     \
        if (!a##_aName)                                                   \
            return E_POINTER;                                             \
                                                                          \
        AutoCaller autoCaller(this);                                      \
        if (FAILED(autoCaller.rc())) return autoCaller.rc();              \
                                                                          \
        /* todo: Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        uint32_t value;                                                   \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->consoleVRDPServer ()->QueryInfo                          \
            (_aIndex, &value, sizeof (value), &cbOut);                    \
                                                                          \
        *a##_aName = cbOut? !!value: FALSE;                               \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_BOOL_DUMMY(void)

#define IMPL_GETTER_SCALAR(_aType, _aName, _aIndex, _aValueMask)          \
    STDMETHODIMP RemoteDisplayInfo::COMGETTER(_aName) (_aType *a##_aName) \
    {                                                                     \
        if (!a##_aName)                                                   \
            return E_POINTER;                                             \
                                                                          \
        AutoCaller autoCaller(this);                                      \
        if (FAILED(autoCaller.rc())) return autoCaller.rc();              \
                                                                          \
        /* todo: Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        _aType value;                                                     \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->consoleVRDPServer ()->QueryInfo                          \
            (_aIndex, &value, sizeof (value), &cbOut);                    \
                                                                          \
        if (_aValueMask) value &= (_aValueMask);                          \
        *a##_aName = cbOut? value: 0;                                     \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_SCALAR_DUMMY(void)

#define IMPL_GETTER_BSTR(_aType, _aName, _aIndex)                         \
    STDMETHODIMP RemoteDisplayInfo::COMGETTER(_aName) (_aType *a##_aName) \
    {                                                                     \
        if (!a##_aName)                                                   \
            return E_POINTER;                                             \
                                                                          \
        AutoCaller autoCaller(this);                                      \
        if (FAILED(autoCaller.rc())) return autoCaller.rc();              \
                                                                          \
        /* todo: Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->consoleVRDPServer ()->QueryInfo                          \
            (_aIndex, NULL, 0, &cbOut);                                   \
                                                                          \
        if (cbOut == 0)                                                   \
        {                                                                 \
            Bstr str("");                                                 \
            str.cloneTo(a##_aName);                                       \
            return S_OK;                                                  \
        }                                                                 \
                                                                          \
        char *pchBuffer = (char *)RTMemTmpAlloc (cbOut);                  \
                                                                          \
        if (!pchBuffer)                                                   \
        {                                                                 \
            Log(("RemoteDisplayInfo::"                                    \
                 #_aName                                                  \
                 ": Failed to allocate memory %d bytes\n", cbOut));       \
            return E_OUTOFMEMORY;                                         \
        }                                                                 \
                                                                          \
        mParent->consoleVRDPServer ()->QueryInfo                          \
            (_aIndex, pchBuffer, cbOut, &cbOut);                          \
                                                                          \
        Bstr str(pchBuffer);                                              \
                                                                          \
        str.cloneTo(a##_aName);                                           \
                                                                          \
        RTMemTmpFree (pchBuffer);                                         \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_BSTR_DUMMY(void)

IMPL_GETTER_BOOL   (BOOL,    Active,             VRDP_QI_ACTIVE);
IMPL_GETTER_SCALAR (LONG,    Port,               VRDP_QI_PORT,                  0);
IMPL_GETTER_SCALAR (ULONG,   NumberOfClients,    VRDP_QI_NUMBER_OF_CLIENTS,     0);
IMPL_GETTER_SCALAR (LONG64,  BeginTime,          VRDP_QI_BEGIN_TIME,            0);
IMPL_GETTER_SCALAR (LONG64,  EndTime,            VRDP_QI_END_TIME,              0);
IMPL_GETTER_SCALAR (LONG64,  BytesSent,          VRDP_QI_BYTES_SENT,            INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesSentTotal,     VRDP_QI_BYTES_SENT_TOTAL,      INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesReceived,      VRDP_QI_BYTES_RECEIVED,        INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesReceivedTotal, VRDP_QI_BYTES_RECEIVED_TOTAL,  INT64_MAX);
IMPL_GETTER_BSTR   (BSTR,    User,               VRDP_QI_USER);
IMPL_GETTER_BSTR   (BSTR,    Domain,             VRDP_QI_DOMAIN);
IMPL_GETTER_BSTR   (BSTR,    ClientName,         VRDP_QI_CLIENT_NAME);
IMPL_GETTER_BSTR   (BSTR,    ClientIP,           VRDP_QI_CLIENT_IP);
IMPL_GETTER_SCALAR (ULONG,   ClientVersion,      VRDP_QI_CLIENT_VERSION,        0);
IMPL_GETTER_SCALAR (ULONG,   EncryptionStyle,    VRDP_QI_ENCRYPTION_STYLE,      0);

#undef IMPL_GETTER_BSTR
#undef IMPL_GETTER_SCALAR
#undef IMPL_GETTER_BOOL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
