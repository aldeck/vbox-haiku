/** @file
 * VBox - Host-Guest Communication Manager (HGCM):
 * Service library definitions.
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
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

#ifndef __VBox_hgcm_h__
#define __VBox_hgcm_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>

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
 */
#define VBOX_HGCM_SVC_VERSION_MAJOR (0x0003)
#define VBOX_HGCM_SVC_VERSION_MINOR (0x0002)
#define VBOX_HGCM_SVC_VERSION ((VBOX_HGCM_SVC_VERSION_MAJOR << 16) + VBOX_HGCM_SVC_VERSION_MINOR)


/** Typed pointer to distinguish a call to service. */
struct VBOXHGCMCALLHANDLE_TYPEDEF;
typedef struct VBOXHGCMCALLHANDLE_TYPEDEF *VBOXHGCMCALLHANDLE;

/** Service helpers pointers table. */
typedef struct _VBOXHGCMSVCHELPERS
{
    /** The service has processed the Call request. */
    DECLCALLBACKMEMBER(void, pfnCallComplete) (VBOXHGCMCALLHANDLE callHandle, int32_t rc);

    void *pvInstance;
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
    DECLCALLBACKMEMBER(int, pfnUnload) (void);

    /** Inform the service about a client connection. */
    DECLCALLBACKMEMBER(int, pfnConnect) (uint32_t u32ClientID, void *pvClient);

    /** Inform the service that the client wants to disconnect. */
    DECLCALLBACKMEMBER(int, pfnDisconnect) (uint32_t u32ClientID, void *pvClient);

    /** Service entry point.
     *  Return code is passed to pfnCallComplete callback.
     */
    DECLCALLBACKMEMBER(void, pfnCall) (VBOXHGCMCALLHANDLE callHandle, uint32_t u32ClientID, void *pvClient, uint32_t function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);

    /** Host Service entry point meant for privileged features invisible to the guest.
     *  Return code is passed to pfnCallComplete callback.
     */
    DECLCALLBACKMEMBER(int, pfnHostCall) (uint32_t function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]);
 
    /** Inform the service about a VM save operation. */
    DECLCALLBACKMEMBER(int, pfnSaveState) (uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM);

    /** Inform the service about a VM load operation. */
    DECLCALLBACKMEMBER(int, pfnLoadState) (uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM);

    /** Manage the service extension. */
    DECLCALLBACKMEMBER(int, pfnRegisterExtension) (PFNHGCMSVCEXT pfnExtension, void *pvExtension);

} VBOXHGCMSVCFNTABLE;
#pragma pack()


/** Service initialization entry point. */
typedef DECLCALLBACK(int) VBOXHGCMSVCLOAD(VBOXHGCMSVCFNTABLE *ptable);
typedef VBOXHGCMSVCLOAD *PFNVBOXHGCMSVCLOAD;
#define VBOX_HGCM_SVCLOAD_NAME "VBoxHGCMSvcLoad"

#endif /* !__VBox_hgcmsvc_h__ */
