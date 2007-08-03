/** @file
 *
 * VirtualBox Driver interface to VMM device
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

#ifndef ____H_VMMDEV
#define ____H_VMMDEV

#include "VirtualBoxBase.h"
#include <VBox/pdmdrv.h>

class Console;

class VMMDev
{
public:
    VMMDev(Console *console);
    virtual ~VMMDev();
    static const PDMDRVREG  DrvReg;
    /** Pointer to the associated VMMDev driver. */
    struct DRVMAINVMMDEV *mpDrv;

    bool fSharedFolderActive;
    bool isShFlActive()
    {
        return fSharedFolderActive;
    }

    Console *getParent()
    {
        return mParent;
    }

    int WaitCredentialsJudgement (uint32_t u32Timeout, uint32_t *pu32GuestFlags);
    int SetCredentialsJudgementResult (uint32_t u32Flags);

    PPDMIVMMDEVPORT getVMMDevPort();

    int hgcmLoadService (const char *pszServiceName, const char *pszServiceLibrary);
    int hgcmHostCall (const char *pszServiceName, uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms);

private:
    static DECLCALLBACK(void *) drvQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface);
    static DECLCALLBACK(int)    drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle);
    static DECLCALLBACK(void)   drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)   drvReset(PPDMDRVINS pDrvIns);

    ComObjPtr <Console, ComWeakRef> mParent;

    RTSEMEVENT mCredentialsEvent;
    uint32_t mu32CredentialsFlags;
};

#endif // ____H_VMMDEV
