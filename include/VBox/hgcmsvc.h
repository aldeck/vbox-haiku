/** @file
 * VBox - Host-Guest Communication Manager (HGCM):
 * Service library definitions.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBox_hgcm_h
#define ___VBox_hgcm_h

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/err.h>

/** @todo proper comments. */

/**
 * Service interface version.
 *
 * Includes layout of both VBOXHGCMSVCFNTABLE and VBOXHGCMSVCHELPERS.
 *
 * A service can work with these structures if major version
 * is equal and minor version of service is <= version of the
 * structures.
 *
 * For example when a new helper is added at the end of helpers
 * structure, then the minor version will be increased. All older
 * services still can work because they have their old helpers
 * unchanged.
 *
 * Revision history.
 * 1.1->2.1 Because the pfnConnect now also has the pvClient parameter.
 * 2.1->2.2 Because pfnSaveState and pfnLoadState were added
 * 2.2->3.1 Because pfnHostCall is now synchronous, returns rc, and parameters were changed
 * 3.1->3.2 Because pfnRegisterExtension was added
 * 3.2->3.3 Because pfnDisconnectClient helper was added
 * 3.3->4.1 Because the pvService entry and parameter was added
 */
#define VBOX_HGCM_SVC_VERSION_MAJOR (0x0004)
#define VBOX_HGCM_SVC_VERSION_MINOR (0x0001)
#define VBOX_HGCM_SVC_VERSION ((VBOX_HGCM_SVC_VERSION_MAJOR << 16) + VBOX_HGCM_SVC_VERSION_MINOR)


/** Typed pointer to distinguish a call to service. */
struct VBOXHGCMCALLHANDLE_TYPEDEF;
typedef struct VBOXHGCMCALLHANDLE_TYPEDEF *VBOXHGCMCALLHANDLE;

/** Service helpers pointers table. */
typedef struct _VBOXHGCMSVCHELPERS
{
    /** The service has processed the Call request. */
    DECLR3CALLBACKMEMBER(void, pfnCallComplete, (VBOXHGCMCALLHANDLE callHandle, int32_t rc));

    void *pvInstance;

    /** The service disconnects the client. */
    DECLR3CALLBACKMEMBER(void, pfnDisconnectClient, (void *pvInstance, uint32_t u32ClientID));
} VBOXHGCMSVCHELPERS;

typedef VBOXHGCMSVCHELPERS *PVBOXHGCMSVCHELPERS;


#define VBOX_HGCM_SVC_PARM_INVALID (0U)
#define VBOX_HGCM_SVC_PARM_32BIT (1U)
#define VBOX_HGCM_SVC_PARM_64BIT (2U)
#define VBOX_HGCM_SVC_PARM_PTR   (3U)

typedef struct VBOXHGCMSVCPARM
{
    /** VBOX_HGCM_SVC_PARM_* values. */
    uint32_t type;

    union
    {
        uint32_t uint32;
        uint64_t uint64;
        struct
        {
            uint32_t size;
            void *addr;
        } pointer;
    } u;
#ifdef __cplusplus
    /** Extract a uint32_t value from an HGCM parameter structure */
    int getUInt32 (uint32_t *u32)
    {
        int rc = VINF_SUCCESS;
        if (type != VBOX_HGCM_SVC_PARM_32BIT)
            rc = VERR_INVALID_PARAMETER;
        if (RT_SUCCESS(rc))
            *u32 = u.uint32;
        return rc;
    }

    /** Extract a uint64_t value from an HGCM parameter structure */
    int getUInt64 (uint64_t *u64)
    {
        int rc = VINF_SUCCESS;
        if (type != VBOX_HGCM_SVC_PARM_64BIT)
            rc = VERR_INVALID_PARAMETER;
        if (RT_SUCCESS(rc))
            *u64 = u.uint64;
        return rc;
    }

    /** Extract a pointer value from an HGCM parameter structure */
    int getPointer (void **ppv, uint32_t *pcb)
    {
        if (type == VBOX_HGCM_SVC_PARM_PTR)
        {
            *ppv = u.pointer.addr;
            *pcb = u.pointer.size;
            return VINF_SUCCESS;
        }

        return VERR_INVALID_PARAMETER;
    }

    /** Extract a constant pointer value from an HGCM parameter structure */
    int getPointer (const void **ppv, uint32_t *pcb)
    {
        if (type == VBOX_HGCM_SVC_PARM_PTR)
        {
            *ppv = u.pointer.addr;
            *pcb = u.pointer.size;
            return VINF_SUCCESS;
        }

        return VERR_INVALID_PARAMETER;
    }

    /** Set a uint32_t value to an HGCM parameter structure */
    void setUInt32(uint32_t u32)
    {
        type = VBOX_HGCM_SVC_PARM_32BIT;
        u.uint32 = u32;
    }

    /** Set a uint64_t value to an HGCM parameter structure */
    void setUInt64(uint64_t u64)
    {
        type = VBOX_HGCM_SVC_PARM_64BIT;
        u.uint64 = u64;
    }

    /** Set a pointer value to an HGCM parameter structure */
    void setPointer(void *pv, uint32_t cb)
    {
        type = VBOX_HGCM_SVC_PARM_PTR;
        u.pointer.addr = pv;
        u.pointer.size = cb;
    }

    VBOXHGCMSVCPARM() : type(VBOX_HGCM_SVC_PARM_INVALID) {}
#endif
} VBOXHGCMSVCPARM;

typedef VBOXHGCMSVCPARM *PVBOXHGCMSVCPARM;

/** Service specific extension callback.
 *  This callback is called by the service to perform service specific operation.
 *  
 * @param pvExtension The extension pointer.
 * @param u32Function What the callback is supposed to do.
 * @param pvParm      The function parameters.
 * @param cbParm      The size of the function parameters.
 */
typedef DECLCALLBACK(int) FNHGCMSVCEXT(void *pvExtension, uint32_t u32Function, void *pvParm, uint32_t cbParms);
typedef FNHGCMSVCEXT *PFNHGCMSVCEXT;

/** The Service DLL entry points.
 *
 *  HGCM will call the DLL "VBoxHGCMSvcLoad"
 *  function and the DLL must fill in the VBOXHGCMSVCFNTABLE
 *  with function pointers.
 */

/* The structure is used in separately compiled binaries so an explicit packing is required. */
#pragma pack(1)
typedef struct _VBOXHGCMSVCFNTABLE
{
    /** Filled by HGCM */

    /** Size of the structure. */
    uint32_t                 cbSize;

    /** Version of the structure, including the helpers. */
    uint32_t                 u32Version;

    PVBOXHGCMSVCHELPERS      pHelpers;

    /** Filled by the service. */

    /** Size of client information the service want to have. */
    uint32_t                 cbClient;
#if ARCH_BITS == 64
    /** Ensure that the following pointers are properly aligned on 64-bit system. */
    uint32_t                 u32Alignment0;
#endif

    /** Uninitialize service */
    DECLR3CALLBACKMEMBER(int, pfnUnload, (void *pvService));

    /** Inform the service about a client connection. */
    DECLR3CALLBACKMEMBER(int, pfnConnect, (void *pvService, uint32_t u32ClientID, void *pvClient));

    /** Inform the service that the client wants to disconnect. */
    DECLR3CALLBACKMEMBER(int, pfnDisconnect, (void *pvService, uint32_t u32ClientID, void *pvClient));

    /** Service entry point.
     *  Return code is passed to pfnCallComplete callback.
     */
    DECLR3CALLBACKMEMBER(void, pfnCall, (void *pvService, VBOXHGCMCALLHANDLE callHandle, uint32_t u32ClientID, void *pvClient, uint32_t function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]));

    /** Host Service entry point meant for privileged features invisible to the guest.
     *  Return code is passed to pfnCallComplete callback.
     */
    DECLR3CALLBACKMEMBER(int, pfnHostCall, (void *pvService, uint32_t function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]));
 
    /** Inform the service about a VM save operation. */
    DECLR3CALLBACKMEMBER(int, pfnSaveState, (void *pvService, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM));

    /** Inform the service about a VM load operation. */
    DECLR3CALLBACKMEMBER(int, pfnLoadState, (void *pvService, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM));

    /** Register a service extension callback. */
    DECLR3CALLBACKMEMBER(int, pfnRegisterExtension, (void *pvService, PFNHGCMSVCEXT pfnExtension, void *pvExtension));

    /** User/instance data pointer for the service. */
    void *pvService;

} VBOXHGCMSVCFNTABLE;
#pragma pack()


/** Service initialization entry point. */
typedef DECLCALLBACK(int) VBOXHGCMSVCLOAD(VBOXHGCMSVCFNTABLE *ptable);
typedef VBOXHGCMSVCLOAD *PFNVBOXHGCMSVCLOAD;
#define VBOX_HGCM_SVCLOAD_NAME "VBoxHGCMSvcLoad"

#endif
