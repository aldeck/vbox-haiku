/** @file
 * VBox host drivers - Ring-0 support drivers - Solaris host:
 * Solaris driver C code
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#undef u /* /usr/include/sys/user.h:249:1 is where this is defined to (curproc->p_user). very cool. */

#include "SUPDRV.h"
#include <iprt/spinlock.h>
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/alloc.h>
#include <iprt/string.h>


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** The module name. */
#define DEVICE_NAME              "vboxdrv"
/** The module description as seen in 'modinfo'. */
#define DEVICE_DESC              "VirtualBox Driver"
/** Maximum number of driver instances. */
#define DEVICE_MAXINSTANCES      16


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int VBoxDrvSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred);
static int VBoxDrvSolarisClose(dev_t Dev, int fFlag, int fType, cred_t *pCred);
static int VBoxDrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxDrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred);
static int VBoxDrvSolarisIOCtl (dev_t Dev, int Cmd, intptr_t pArgs, int mode, cred_t *pCred, int *pVal);

static int VBoxDrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t Cmd);
static int VBoxDrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t Cmd);

static int VBoxSupDrvErr2SolarisErr(int rc);
static int VBoxDrvSolarisIOCtlSlow(PSUPDRVSESSION pSession, int Cmd, int Mode, intptr_t pArgs);


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/**
 * cb_ops: for drivers that support char/block entry points
 */
static struct cb_ops g_VBoxDrvSolarisCbOps =
{
    VBoxDrvSolarisOpen,
    VBoxDrvSolarisClose,
    nodev,                  /* b strategy */
    nodev,                  /* b dump */
    nodev,                  /* b print */
    VBoxDrvSolarisRead,
    VBoxDrvSolarisWrite,
    VBoxDrvSolarisIOCtl,
    nodev,                  /* c devmap */
    nodev,                  /* c mmap */
    nodev,                  /* c segmap */
    nochpoll,               /* c poll */
    ddi_prop_op,            /* property ops */
    NULL,                   /* streamtab  */
    D_NEW | D_MP,           /* compat. flag */
    CB_REV                  /* revision */
};

/**
 * dev_ops: for driver device operations
 */
static struct dev_ops g_VBoxDrvSolarisDevOps =
{
    DEVO_REV,               /* driver build revision */
    0,                      /* ref count */
    nulldev,                /* get info */
    nulldev,                /* identify */
    nulldev,                /* probe */
    VBoxDrvSolarisAttach,
    VBoxDrvSolarisDetach,
    nodev,                  /* reset */
    &g_VBoxDrvSolarisCbOps,
    (struct bus_ops *)0,
    nodev                   /* power */
};

/**
 * modldrv: export driver specifics to the kernel
 */
static struct modldrv g_VBoxDrvSolarisModule =
{
    &mod_driverops,         /* extern from kernel */
    DEVICE_DESC,
    &g_VBoxDrvSolarisDevOps
};

/**
 * modlinkage: export install/remove/info to the kernel
 */
static struct modlinkage g_VBoxDrvSolarisModLinkage =
{
    MODREV_1,               /* loadable module system revision */
    &g_VBoxDrvSolarisModule,
    NULL                    /* terminate array of linkage structures */
};

/** State info. for each driver instance. */
typedef struct
{
    dev_info_t     *pDip;   /* Device handle */
} vbox_devstate_t;

/** Opaque pointer to state */
static void *g_pVBoxDrvSolarisState;

/** Device extention & session data association structure */
static SUPDRVDEVEXT         g_DevExt;

/* GCC C++ hack. */
unsigned __gxx_personality_v0 = 0xcccccccc;

/** Hash table */
static PSUPDRVSESSION       g_apSessionHashTab[19];
/** Spinlock protecting g_apSessionHashTab. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** Calculates bucket index into g_apSessionHashTab.*/
#define SESSION_HASH(sfn) ((sfn) % RT_ELEMENTS(g_apSessionHashTab))

/**
 * Kernel entry points
 */
int _init (void)
{
    cmn_err(CE_CONT, "VBoxDrvSolaris _init");

    int e = ddi_soft_state_init(&g_pVBoxDrvSolarisState, sizeof (vbox_devstate_t), 1);
    if (e != 0)
        return e;

    e = mod_install(&g_VBoxDrvSolarisModLinkage);
    if (e != 0)
        ddi_soft_state_fini(&g_pVBoxDrvSolarisState);

    cmn_err(CE_CONT, "VBoxDrvSolaris _init returns %d", e);
    return e;
}

int _fini (void)
{
    cmn_err(CE_CONT, "VBoxDrvSolaris _fini");

    int e = mod_remove(&g_VBoxDrvSolarisModLinkage);
    if (e != 0)
        return e;

    ddi_soft_state_fini(&g_pVBoxDrvSolarisState);
    return e;
}

int _info (struct modinfo *pModInfo)
{
    cmn_err(CE_CONT, "VBoxDrvSolaris _info");
    int e = mod_info (&g_VBoxDrvSolarisModLinkage, pModInfo);
    cmn_err(CE_CONT, "VBoxDrvSolaris _info returns %d", e);
    return e;
}

/**
 * User context entry points
 */
static int VBoxDrvSolarisOpen(dev_t *pDev, int fFlag, int fType, cred_t *pCred)
{
    cmn_err(CE_CONT, "VBoxDrvSolarisOpen");

    int                 rc;
    PSUPDRVSESSION      pSession;

    /*
     * Create a new session.
     * Sessions in Solaris driver are mostly useless. It's however needed
     * in VBoxDrvSolarisIOCtlSlow() while calling supdrvIOCtl()
     */
    rc = supdrvCreateSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
    {
        RTSPINLOCKTMP   Tmp = RTSPINLOCKTMP_INITIALIZER;
        unsigned        iHash;

        pSession->Uid       = crgetuid(pCred);
        pSession->Gid       = crgetgid(pCred);
        pSession->Process   = RTProcSelf();
        pSession->R0Process = RTR0ProcHandleSelf();

        /*
         * Insert it into the hash table.
         */
        iHash = SESSION_HASH(pSession->Process);
        RTSpinlockAcquireNoInts(g_Spinlock, &Tmp);
        pSession->pNextHash = g_apSessionHashTab[iHash];
        g_apSessionHashTab[iHash] = pSession;
        RTSpinlockReleaseNoInts(g_Spinlock, &Tmp);
        cmn_err(CE_NOTE,"VBoxDrvSolarisOpen success");
    }

    int instance;
    for (instance = 0; instance < DEVICE_MAXINSTANCES; instance++)
    {
        vbox_devstate_t *pState = ddi_get_soft_state (g_pVBoxDrvSolarisState, instance);
        if (pState)
            break;
    }

    if (instance >= DEVICE_MAXINSTANCES)
    {
        cmn_err(CE_NOTE, "VBoxDrvSolarisOpen: All instances exhausted\n");
        return ENXIO;
    }

    *pDev = makedevice(getmajor(*pDev), instance);

    return VBoxSupDrvErr2SolarisErr(rc);
}

static int VBoxDrvSolarisClose(dev_t pDev, int flag, int otyp, cred_t *cred)
{
    cmn_err(CE_CONT, "VBoxDrvSolarisClose");

    RTSPINLOCKTMP   Tmp = RTSPINLOCKTMP_INITIALIZER;
    const RTPROCESS Process = RTProcSelf();
    const unsigned  iHash = SESSION_HASH(Process);
    PSUPDRVSESSION  pSession;

    /*
     * Remove from the hash table.
     */
    RTSpinlockAcquireNoInts(g_Spinlock, &Tmp);
    pSession = g_apSessionHashTab[iHash];
    if (pSession)
    {
        if (pSession->Process == Process)
        {
            g_apSessionHashTab[iHash] = pSession->pNextHash;
            pSession->pNextHash = NULL;
        }
        else
        {
            PSUPDRVSESSION pPrev = pSession;
            pSession = pSession->pNextHash;
            while (pSession)
            {
                if (pSession->Process == Process)
                {
                    pPrev->pNextHash = pSession->pNextHash;
                    pSession->pNextHash = NULL;
                    break;
                }

                /* next */
                pPrev = pSession;
                pSession = pSession->pNextHash;
            }
        }
    }
    RTSpinlockReleaseNoInts(g_Spinlock, &Tmp);
    if (!pSession)
    {
        OSDBGPRINT(("VBoxDrvSolarisClose: WHAT?!? pSession == NULL! This must be a mistake... pid=%d (close)\n",
                    (int)Process));
        return DDI_FAILURE;
    }

    /*
     * Close the session.
     */
    supdrvCloseSession(&g_DevExt, pSession);
    return DDI_SUCCESS;
}

static int VBoxDrvSolarisRead(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    cmn_err(CE_CONT, "VBoxDrvSolarisRead");
    return DDI_SUCCESS;
}

static int VBoxDrvSolarisWrite(dev_t Dev, struct uio *pUio, cred_t *pCred)
{
    cmn_err(CE_CONT, "VBoxDrvSolarisWrite");
    return DDI_SUCCESS;
}

/**
 * Attach entry point, to attach a device to the system or resume it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisAttach(dev_info_t *pDip, ddi_attach_cmd_t enmCmd)
{
    cmn_err(CE_CONT, "VBoxDrvSolarisAttach");
    int rc = VINF_SUCCESS;
    int instance = 0;
    vbox_devstate_t *pState;

    switch (enmCmd)
    {
        case DDI_ATTACH:
        {
            instance = ddi_get_instance (pDip);

            if (ddi_soft_state_zalloc(g_pVBoxDrvSolarisState, instance) != DDI_SUCCESS)
            {
                cmn_err(CE_NOTE, "VBoxDrvSolarisAttach: state alloc failed");
                return DDI_FAILURE;
            }

            pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, instance);

            /*
             * Initialize IPRT R0 driver, which internally calls OS-specific r0 init.
             */
            rc = RTR0Init(0);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Initialize the device extension
                 */
                rc = supdrvInitDevExt(&g_DevExt);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Initialize the session hash table.
                     */
                    memset(g_apSessionHashTab, 0, sizeof(g_apSessionHashTab));
                    rc = RTSpinlockCreate(&g_Spinlock);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Register ourselves as a character device, pseudo-driver
                         */
                        if (ddi_create_minor_node(pDip, "0", S_IFCHR,
                                instance, DDI_PSEUDO, 0) == DDI_SUCCESS)
                        {
                            pState->pDip = pDip;
                            ddi_report_dev(pDip);
                            return DDI_SUCCESS;
                        }

                        /* Is this really necessary? */
                        ddi_remove_minor_node(pDip, NULL);
                        cmn_err(CE_NOTE,"VBoxDrvSolarisAttach: ddi_create_minor_node failed.");

                        RTSpinlockDestroy(g_Spinlock);
                        g_Spinlock = NIL_RTSPINLOCK;
                    }
                    else
                        cmn_err(CE_NOTE, "VBoxDrvSolarisAttach: RTSpinlockCreate failed");
                    supdrvDeleteDevExt(&g_DevExt);
                }
                else
                    cmn_err(CE_NOTE, "VBoxDrvSolarisAttach: supdrvInitDevExt failed");
                RTR0Term ();
            }
            else
                cmn_err(CE_NOTE, "VBoxDrvSolarisAttach: failed to init R0Drv");
            memset(&g_DevExt, 0, sizeof(g_DevExt));
            break;
        }

        default:
            return DDI_FAILURE;
    }

    return DDI_FAILURE;
}

/**
 * Detach entry point, to detach a device to the system or suspend it.
 *
 * @param   pDip            The module structure instance.
 * @param   enmCmd          Attach type (ddi_attach_cmd_t)
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisDetach(dev_info_t *pDip, ddi_detach_cmd_t enmCmd)
{
    int rc = VINF_SUCCESS;
    int instance;
    register vbox_devstate_t *pState;

    cmn_err(CE_CONT, "VBoxDrvSolarisDetach");
    switch (enmCmd)
    {
        case DDI_DETACH:
        {
            instance = ddi_get_instance(pDip);
            pState = ddi_get_soft_state(g_pVBoxDrvSolarisState, instance);

            ddi_remove_minor_node(pDip, NULL);
            ddi_soft_state_free(g_pVBoxDrvSolarisState, instance);

            supdrvDeleteDevExt(&g_DevExt);

            rc = RTSpinlockDestroy(g_Spinlock);
            AssertRC(rc);
            g_Spinlock = NIL_RTSPINLOCK;

            RTR0Term();

            memset(&g_DevExt, 0, sizeof(g_DevExt));
            cmn_err(CE_CONT, "VBoxDrvSolarisDetach: Clean Up Done.");
            return DDI_SUCCESS;
        }

        default:
            return DDI_FAILURE;
    }
}

/**
 * Driver ioctl, an alternate entry point for this character driver.
 *
 * @param   Dev             Device number
 * @param   Cmd             Operation identifier
 * @param   pArg            Arguments from user to driver
 * @param   Mode            Information bitfield (read/write, address space etc.)
 * @param   pCred           User credentials
 * @param   pVal            Return value for calling process.
 *
 * @return  corresponding solaris error code.
 */
static int VBoxDrvSolarisIOCtl (dev_t Dev, int Cmd, intptr_t pArgs, int Mode, cred_t* pCred, int* pVal)
{
    RTSPINLOCKTMP       Tmp = RTSPINLOCKTMP_INITIALIZER;
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(Process);
    PSUPDRVSESSION      pSession;

    cmn_err(CE_CONT, "VBoxDrvSolarisIOCtl\n");
    /*
     * Find the session.
     */
    RTSpinlockAcquireNoInts(g_Spinlock, &Tmp);
    pSession = g_apSessionHashTab[iHash];
    if (pSession && pSession->Process != Process)
    {
        do pSession = pSession->pNextHash;
        while (pSession && pSession->Process != Process);
    }
    RTSpinlockReleaseNoInts(g_Spinlock, &Tmp);
    if (!pSession)
    {
        OSDBGPRINT(("VBoxSupDrvIOCtl: WHAT?!? pSession == NULL! This must be a mistake... pid=%d iCmd=%#x\n",
                    (int)Process, Cmd));
        return EINVAL;
    }

    /*
     * Deal with the two high-speed IOCtl that takes it's arguments from
     * the session and iCmd, and only returns a VBox status code.
     */
    if (    Cmd == SUP_IOCTL_FAST_DO_RAW_RUN
        ||  Cmd == SUP_IOCTL_FAST_DO_HWACC_RUN
        ||  Cmd == SUP_IOCTL_FAST_DO_NOP)
        return supdrvIOCtlFast(Cmd, &g_DevExt, pSession);

    return VBoxDrvSolarisIOCtlSlow(pSession, Cmd, Mode, pArgs);
}


/** @def IOCPARM_LEN
 * Gets the length from the ioctl number.
 * This is normally defined by sys/ioccom.h on BSD systems...
 */
#ifndef IOCPARM_LEN
# define IOCPARM_LEN(x)     ( ((x) >> 16) & IOCPARM_MASK )
#endif


/**
 * Worker for VBoxSupDrvIOCtl that takes the slow IOCtl functions.
 *
 * @returns Solaris errno.
 *
 * @param   pSession    The session.
 * @param   Cmd         The IOCtl command.
 * @param   Mode        Information bitfield (for specifying ownership of data)
 * @param   iArg        User space address of the request buffer.
 */
static int VBoxDrvSolarisIOCtlSlow(PSUPDRVSESSION pSession, int iCmd, int Mode, intptr_t iArg)
{
    int         rc;
    uint32_t    cbBuf = 0;
    SUPREQHDR   Hdr;
    PSUPREQHDR  pHdr;


    /*
     * Read the header.
     */
    if (RT_UNLIKELY(IOCPARM_LEN(iCmd) != sizeof(Hdr)))
    {
        OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: iCmd=%#x len %d expected %d\n", iCmd, IOCPARM_LEN(iCmd), sizeof(Hdr)));
        return EINVAL;
    }
    rc = ddi_copyin((void *)iArg, &Hdr, sizeof(Hdr), Mode);
    if (RT_UNLIKELY(rc))
    {
        OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: ddi_copyin(,%#lx,) failed; iCmd=%#x. rc=%d\n", iArg, iCmd, rc));
        return EFAULT;
    }
    if (RT_UNLIKELY((Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
    {
        OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: bad header magic %#x; iCmd=%#x\n", Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK, iCmd));
        return EINVAL;
    }
    cbBuf = RT_MAX(Hdr.cbIn, Hdr.cbOut);
    if (RT_UNLIKELY(    Hdr.cbIn < sizeof(Hdr)
                    ||  Hdr.cbOut < sizeof(Hdr)
                    ||  cbBuf > _1M*16))
    {
        OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: max(%#x,%#x); iCmd=%#x\n", Hdr.cbIn, Hdr.cbOut, iCmd));
        return EINVAL;
    }

    /*
     * Buffer the request.
     */
    pHdr = RTMemTmpAlloc(cbBuf);
    if (RT_UNLIKELY(!pHdr))
    {
        OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: failed to allocate buffer of %d bytes for iCmd=%#x.\n", cbBuf, iCmd));
        return ENOMEM;
    }
    rc = ddi_copyin((void *)iArg, pHdr, cbBuf, Mode);
    if (RT_UNLIKELY(rc))
    {
        dprintf(("VBoxDrvSolarisIOCtlSlow: copy_from_user(,%#lx, %#x) failed; iCmd=%#x. rc=%d\n", iArg, Hdr.cbIn, iCmd, rc));
        RTMemFree(pHdr);
        return EFAULT;
    }

    /*
     * Process the IOCtl.
     */
    rc = supdrvIOCtl(iCmd, &g_DevExt, pSession, pHdr);

    /*
     * Copy ioctl data and output buffer back to user space.
     */
    if (RT_LIKELY(!rc))
    {
        uint32_t cbOut = pHdr->cbOut;
        if (RT_UNLIKELY(cbOut > cbBuf))
        {
            OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: too much output! %#x > %#x; iCmd=%#x!\n", cbOut, cbBuf, iCmd));
            cbOut = cbBuf;
        }
        rc = ddi_copyout(pHdr, (void *)iArg, cbOut, Mode);
        if (RT_UNLIKELY(rc != 0))
        {
            /* this is really bad */
            OSDBGPRINT(("VBoxDrvSolarisIOCtlSlow: ddi_copyout(,%p,%d) failed. rc=%d\n", (void *)iArg, cbBuf, rc));
            rc = EFAULT;
        }
    }
    else
        rc = EINVAL;

    RTMemTmpFree(pHdr);
    return rc;
}


/**
 * Converts an supdrv error code to a solaris error code.
 *
 * @returns corresponding solaris error code.
 * @param   rc  supdrv error code (SUPDRV_ERR_* defines).
 */
static int VBoxSupDrvErr2SolarisErr(int rc)
{
    switch (rc)
    {
        case 0:                             return 0;
        case SUPDRV_ERR_GENERAL_FAILURE:    return EACCES;
        case SUPDRV_ERR_INVALID_PARAM:      return EINVAL;
        case SUPDRV_ERR_INVALID_MAGIC:      return EILSEQ;
        case SUPDRV_ERR_INVALID_HANDLE:     return ENXIO;
        case SUPDRV_ERR_INVALID_POINTER:    return EFAULT;
        case SUPDRV_ERR_LOCK_FAILED:        return ENOLCK;
        case SUPDRV_ERR_ALREADY_LOADED:     return EEXIST;
        case SUPDRV_ERR_PERMISSION_DENIED:  return EPERM;
        case SUPDRV_ERR_VERSION_MISMATCH:   return ENOSYS;
    }

    return EPERM;
}

/**
 * Initializes any OS specific object creator fields.
 */
void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession)
{
    NOREF(pObj);
    NOREF(pSession);
}


/**
 * Checks if the session can access the object.
 *
 * @returns true if a decision has been made.
 * @returns false if the default access policy should be applied.
 *
 * @param   pObj        The object in question.
 * @param   pSession    The session wanting to access the object.
 * @param   pszObjName  The object name, can be NULL.
 * @param   prc         Where to store the result when returning true.
 */
bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc)
{
    NOREF(pObj);
    NOREF(pSession);
    NOREF(pszObjName);
    NOREF(prc);
    return false;
}


RTDECL(int) SUPR0Printf(const char *pszFormat, ...)
{
    va_list     args;
    char        szMsg[512];

    va_start(args, pszFormat);
    RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, args);
    va_end(args);

    szMsg[sizeof(szMsg) - 1] = '\0';
    uprintf("SUPR0Printf: %s", szMsg);
    return 0;
}

