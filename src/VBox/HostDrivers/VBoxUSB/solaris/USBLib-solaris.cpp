/** $Id$ */
/** @file
 * USBLib - Library for wrapping up the VBoxUSB functionality, Solaris flavor.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/usblib.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/env.h>
#include <iprt/path.h>
#include <iprt/string.h>

# include <sys/types.h>
# include <sys/stat.h>
# include <errno.h>
# include <unistd.h>
# include <string.h>
# include <limits.h>
# include <strings.h>

/** -XXX- Remove this hackery eventually */
#ifdef DEBUG_ramshankar
# undef Log
# undef LogFlow
# define Log        LogRel
# define LogFlow    LogRel
#endif

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Logging class. */
#define USBLIBR3    "USBLibR3"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Reference counter. */
static uint32_t volatile g_cUsers = 0;
/** VBoxUSB Device handle. */
static RTFILE g_File = NIL_RTFILE;
/** List of tasks handled by the USB helper. */
typedef enum USBHELPER_OP
{
    ADD_ALIAS = 0,
    DEL_ALIAS,
    RESET
};

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int usblibDoIOCtl(unsigned iFunction, void *pvData, size_t cbData);
static int usblibRunHelper(USBHELPER_OP HelperOp, void *pvData);


USBLIB_DECL(int) USBLibInit(void)
{
    LogFlow((USBLIBR3 ":USBLibInit\n"));

    /*
     * Already open?
     * This isn't properly serialized, but we'll be fine with the current usage.
     */
    if (g_cUsers)
    {
        ASMAtomicIncU32(&g_cUsers);
        return VINF_SUCCESS;
    }

    RTFILE File;
    int rc = RTFileOpen(&File, VBOXUSB_DEVICE_NAME, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
    {
        LogRel((USBLIBR3 ":RTFileOpen failed to open VBoxUSB device.rc=%d\n", rc));
        return rc;
    }
    g_File = File;

    ASMAtomicIncU32(&g_cUsers);
#ifdef VBOX_WITH_NEW_USB_CODE_ON_SOLARIS
    /*
     * Check the USBMonitor version.
     */
    VBOXUSBREQ_GET_VERSION Req;
    bzero(&Req, sizeof(Req));
    rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_GET_VERSION, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
    {
        if (   Req.u32Major != VBOXUSBMON_VERSION_MAJOR
            || Req.u32Minor < VBOXUSBMON_VERSION_MINOR)
        {
            rc = VERR_VERSION_MISMATCH;
            LogRel((USBLIBR3 ":USBMonitor version mismatch! driver v%d.%d, expecting ~v%d.%d\n",
                        Req.u32Major, Req.u32Minor, VBOXUSBMON_VERSION_MAJOR, VBOXUSBMON_VERSION_MINOR));

            RTFileClose(File);
            g_File = NIL_RTFILE;
            ASMAtomicDecU32(&g_cUsers);
            return rc;
        }
    }
    else
    {
        LogRel((USBLIBR3 ":USBMonitor driver version query failed. rc=%Rrc\n", rc));
        RTFileClose(File);
        g_File = NIL_RTFILE;
        ASMAtomicDecU32(&g_cUsers);
        return rc;
    }
#endif

    return VINF_SUCCESS;
}


USBLIB_DECL(int) USBLibTerm(void)
{
    LogFlow((USBLIBR3 ":USBLibTerm\n"));

    if (!g_cUsers)
        return VERR_WRONG_ORDER;
    if (ASMAtomicDecU32(&g_cUsers) != 0)
        return VINF_SUCCESS;

    /*
     * We're the last guy, close down the connection.
     */
    RTFILE File = g_File;
    g_File = NIL_RTFILE;
    if (File == NIL_RTFILE)
        return VERR_INTERNAL_ERROR;

    int rc = RTFileClose(File);
    AssertRC(rc);
    return rc;
}


USBLIB_DECL(void *) USBLibAddFilter(PCUSBFILTER pFilter)
{
    LogFlow((USBLIBR3 ":USBLibAddFilter pFilter=%p\n", pFilter));

    VBOXUSBREQ_ADD_FILTER Req;
    Req.Filter = *pFilter;
    Req.uId = 0;

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_ADD_FILTER, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        return (void *)Req.uId;

    AssertMsgFailed((USBLIBR3 ":VBOXUSBMON_IOCTL_ADD_FILTER  failed! rc=%Rrc\n", rc));
    return NULL;
}


USBLIB_DECL(void) USBLibRemoveFilter(void *pvId)
{
    LogFlow((USBLIBR3 ":USBLibRemoveFilter pvId=%p\n", pvId));

    VBOXUSBREQ_REMOVE_FILTER Req;
    Req.uId = (uintptr_t)pvId;

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_REMOVE_FILTER, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        return;

    AssertMsgFailed((USBLIBR3 ":VBOXUSBMON_IOCTL_REMOVE_FILTER failed! rc=%Rrc\n", rc));
}


#ifdef VBOX_WITH_NEW_USB_CODE_ON_SOLARIS
USBLIB_DECL(int) USBLibGetClientInfo(char *pszDeviceIdent, char **ppszClientPath, int *pInstance)
{
    LogFlow((USBLIBR3 ":USBLibGetClientInfo pszDeviceIdent=%s ppszClientPath=%p pInstance=%p\n",
                pszDeviceIdent, ppszClientPath, pInstance));

    AssertPtrReturn(pInstance, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszClientPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDeviceIdent, VERR_INVALID_PARAMETER);

    VBOXUSBREQ_CLIENT_INFO Req;
    bzero(&Req, sizeof(Req));
    RTStrPrintf(Req.achDeviceIdent, sizeof(Req.achDeviceIdent), "%s", pszDeviceIdent);

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_CLIENT_INFO, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
    {
        *pInstance = Req.Instance;
        rc = RTStrAPrintf(ppszClientPath, "%s", Req.achClientPath);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        LogRel((USBLIBR3 ":USBLibGetClientInfo RTStrAPrintf failed! rc=%Rrc achClientPath=%s\n", rc, Req.achClientPath));
    }
    else
        LogRel((USBLIBR3 ":USBLibGetClientInfo VBOXUSBMON_IOCTL_CLIENTPATH failed! rc=%Rrc\n", rc));

    return rc;
}

#else

USBLIB_DECL(int) USBLibDeviceInstance(char *pszDevicePath, int *pInstance)
{
    LogFlow((USBLIBR3 ":USBLibDeviceInstance pszDevicePath=%s pInstance=%p\n", pszDevicePath, pInstance));

    size_t cbReq = sizeof(VBOXUSBREQ_DEVICE_INSTANCE) + strlen(pszDevicePath);
    VBOXUSBREQ_DEVICE_INSTANCE *pReq = (VBOXUSBREQ_DEVICE_INSTANCE *)RTMemTmpAllocZ(cbReq);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    pReq->pInstance = pInstance;
    strncpy(pReq->szDevicePath, pszDevicePath, strlen(pszDevicePath));

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_DEVICE_INSTANCE, pReq, cbReq);
    if (RT_FAILURE(rc))
        LogRel((USBLIBR3 ":VBOXUSBMON_IOCTL_DEVICE_INSTANCE failed! rc=%Rrc\n", rc));

    RTMemFree(pReq);
    return rc;
}

#endif


#if 1
USBLIB_DECL(int) USBLibResetDevice(char *pszDevicePath, bool fReattach)
{
    LogFlow((USBLIBR3 ":USBLibResetDevice pszDevicePath=%s\n", pszDevicePath));

    size_t cbReq = sizeof(VBOXUSBREQ_RESET_DEVICE) + strlen(pszDevicePath);
    VBOXUSBREQ_RESET_DEVICE *pReq = (VBOXUSBREQ_RESET_DEVICE *)RTMemTmpAllocZ(cbReq);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    pReq->fReattach = fReattach;
    strcpy(pReq->szDevicePath, pszDevicePath);

    int rc = usblibDoIOCtl(VBOXUSBMON_IOCTL_RESET_DEVICE, pReq, cbReq);
    if (RT_FAILURE(rc))
        LogRel((USBLIBR3 ":VBOXUSBMON_IOCTL_RESET_DEVICE failed! rc=%Rrc\n", rc));

    RTMemFree(pReq);
    return rc;
}
#else

USBLIB_DECL(int) USBLibResetDevice(char *pszDevicePath, bool fReattach)
{
    VBOXUSBHELPERDATA_RESET Data;
    Data.pszDevicePath = pszDevicePath;
    Data.fHardReset = fReattach;
    return usblibRunHelper(RESET, &Data);
}
#endif

USBLIB_DECL(int) USBLibAddDeviceAlias(PUSBDEVICE pDevice)
{
    VBOXUSBHELPERDATA_ALIAS Data;
    Data.idVendor = pDevice->idVendor;
    Data.idProduct = pDevice->idProduct;
    Data.bcdDevice = pDevice->bcdDevice;
    Data.pszDevicePath = pDevice->pszDevicePath;
    return usblibRunHelper(ADD_ALIAS, &Data);
}


USBLIB_DECL(int) USBLibRemoveDeviceAlias(PUSBDEVICE pDevice)
{
    VBOXUSBHELPERDATA_ALIAS Data;
    Data.idVendor = pDevice->idVendor;
    Data.idProduct = pDevice->idProduct;
    Data.bcdDevice = pDevice->bcdDevice;
    Data.pszDevicePath = pDevice->pszDevicePath;
    return usblibRunHelper(DEL_ALIAS, &Data);
}

#if 0
USBLIB_DECL(int) USBLibConfigureDevice(PUSBDEVICE pDevice)
{
    return usblibRunHelper(pDevice, CONFIGURE);
}
#endif

static int usblibRunHelper(USBHELPER_OP HelperOp, void *pvUsbHelperData)
{
    LogFlow((USBLIBR3 ":usblibRunHelper HelperOp=%d pvUSBHelperData=%p\n", HelperOp, pvUsbHelperData));

    /*
     * Find VBoxUSBHelper.
     */
    char szDriverCtl[PATH_MAX];
    int rc = RTPathExecDir(szDriverCtl, sizeof(szDriverCtl) - sizeof("/" VBOXUSB_HELPER_NAME));
    if (RT_SUCCESS(rc))
    {
        strcat(szDriverCtl, "/" VBOXUSB_HELPER_NAME);
        if (!RTPathExists(szDriverCtl))
        {
            LogRel(("USBProxy: path %s does not exist. Failed to run USB helper %s.\n", szDriverCtl, VBOXUSB_HELPER_NAME));
            return VERR_FILE_NOT_FOUND;
        }

        /*
         * Run VBoxUSBHelper task.
         */
        const char *pszArgs[5];
        if (HelperOp == RESET)
        {
            PVBOXUSBHELPERDATA_RESET pData = (PVBOXUSBHELPERDATA_RESET)pvUsbHelperData;
            pszArgs[0] = szDriverCtl;
            pszArgs[1] = pData->fHardReset ? "hardreset" : "softreset";
            pszArgs[2] = pData->pszDevicePath;
            pszArgs[3] = NULL;
        }
        else
        {
            PVBOXUSBHELPERDATA_ALIAS pData = (PVBOXUSBHELPERDATA_ALIAS)pvUsbHelperData;
            char szDriverAlias[128];

#if 0
            /*
             * USB vid.pid.rev driver binding alias.
             */
            RTStrPrintf(szDriverAlias, sizeof(szDriverAlias), "usb%x,%x.%x", pData->idVendor, pData->idProduct, pData->bcdDevice);
#else
            /*
             * Path based driver binding alias.
             */
            RTStrPrintf(szDriverAlias, sizeof(szDriverAlias), "%s", pData->pszDevicePath + sizeof("/devices"));
#endif
            pszArgs[0] = szDriverCtl;
            pszArgs[1] = HelperOp == ADD_ALIAS ? "add" : "del";
            pszArgs[2] = szDriverAlias;
            pszArgs[3] = VBOXUSB_DRIVER_NAME;
        }
        pszArgs[4] = NULL;
        RTPROCESS pid = NIL_RTPROCESS;
        rc = RTProcCreate(pszArgs[0], pszArgs, RTENV_DEFAULT, 0, &pid);
        if (RT_SUCCESS(rc))
        {
            RTPROCSTATUS Status;
            rc = RTProcWait(pid, 0, &Status);
            if (RT_SUCCESS(rc))
            {
                if (Status.enmReason == RTPROCEXITREASON_NORMAL)
                {
                    switch (Status.iStatus)
                    {
                        case  0: return VINF_SUCCESS;            /* @todo later maybe ignore -4 as well (see VBoxUSBHelper return codes). */
                        case -1: return VERR_PERMISSION_DENIED;
                        case -2: return VERR_INVALID_PARAMETER;
                        case -3: return VERR_GENERAL_FAILURE;
                        default: return VERR_INTERNAL_ERROR;
                    }
                }
                else
                    LogRel((USBLIBR3 ":abnormal termination of USB Helper. enmReason=%d\n", Status.enmReason));
                rc = VERR_GENERAL_FAILURE;
            }
            else
                LogRel((USBLIBR3 ":RTProcWait failed rc=%Rrc\n", rc));
        }
        else
        {
            /* Bad. RTProcCreate() failed! */
            LogRel((USBLIBR3 ":Failed to fork() process for running USB helper for device %s: rc=%Rrc\n", rc));
        }
    }
    return rc;
}


static int usblibDoIOCtl(unsigned iFunction, void *pvData, size_t cbData)
{
    if (g_File == NIL_RTFILE)
    {
        LogRel((USBLIBR3 ":IOCtl failed, device not open.\n"));
        return VERR_FILE_NOT_FOUND;
    }

    VBOXUSBREQ Hdr;
    Hdr.u32Magic = VBOXUSBMON_MAGIC;
    Hdr.cbData = cbData;    /* Don't include full size because the header size is fixed. */
    Hdr.pvDataR3 = pvData;

    int rc = ioctl((int)g_File, iFunction, &Hdr);
    if (rc < 0)
    {
        rc = errno;
        LogRel((USBLIBR3 ":IOCtl failed iFunction=%x errno=%d g_file=%d\n", iFunction, rc, (int)g_File));
        return RTErrConvertFromErrno(rc);
    }

    rc = Hdr.rc;
    if (RT_UNLIKELY(RT_FAILURE(rc)))
        LogRel((USBLIBR3 ":Function (%x) failed. rc=%Rrc\n", iFunction, rc));

    return rc;
}
