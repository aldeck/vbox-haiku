/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#ifndef ___VBoxVideoVidPn_h___
#define ___VBoxVideoVidPn_h___

#include "../VBoxVideo.h"

#define VBOXVDPN_C_DISPLAY_HBLANK_SIZE 200
#define VBOXVDPN_C_DISPLAY_VBLANK_SIZE 180

NTSTATUS vboxVidPnCheckTopology(struct _DEVICE_EXTENSION* pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn,
        D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        BOOLEAN *pbSupported);

NTSTATUS vboxVidPnCheckSourceModeInfo(const D3DKMDT_HVIDPN hDesiredVidPn,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo,
        BOOLEAN *pbSupported);

NTSTATUS vboxVidPnCheckSourceModeSet(const D3DKMDT_HVIDPN hDesiredVidPn,
        D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        BOOLEAN *pbSupported);

NTSTATUS vboxVidPnCheckTargetModeInfo(const D3DKMDT_HVIDPN hDesiredVidPn,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo,
        BOOLEAN *pbSupported);

NTSTATUS vboxVidPnCheckTargetModeSet(const D3DKMDT_HVIDPN hDesiredVidPn,
        D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        BOOLEAN *pbSupported);

typedef struct VBOXVIDPNCOFUNCMODALITY
{
    NTSTATUS Status;
    struct _DEVICE_EXTENSION* pDevExt;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
    CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* pEnumCofuncModalityArg;
    PVBOXWDDM_VIDEOMODES_INFO pInfos;
} VBOXVIDPNCOFUNCMODALITY, *PVBOXVIDPNCOFUNCMODALITY;

typedef struct VBOXVIDPNCOMMIT
{
    NTSTATUS Status;
    struct _DEVICE_EXTENSION* pDevExt;
    const DXGK_VIDPN_INTERFACE* pVidPnInterface;
    CONST DXGKARG_COMMITVIDPN* pCommitVidPnArg;
} VBOXVIDPNCOMMIT, *PVBOXVIDPNCOMMIT;

/* !!!NOTE: The callback is responsible for releasing the path */
typedef DECLCALLBACK(BOOLEAN) FNVBOXVIDPNENUMPATHS(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo, PVOID pContext);
typedef FNVBOXVIDPNENUMPATHS *PFNVBOXVIDPNENUMPATHS;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACK(BOOLEAN) FNVBOXVIDPNENUMSOURCEMODES(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext);
typedef FNVBOXVIDPNENUMSOURCEMODES *PFNVBOXVIDPNENUMSOURCEMODES;

/* !!!NOTE: The callback is responsible for releasing the target mode info */
typedef DECLCALLBACK(BOOLEAN) FNVBOXVIDPNENUMTARGETMODES(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext);
typedef FNVBOXVIDPNENUMTARGETMODES *PFNVBOXVIDPNENUMTARGETMODES;

/* !!!NOTE: The callback is responsible for releasing the source mode info */
typedef DECLCALLBACK(BOOLEAN) FNVBOXVIDPNENUMMONITORSOURCEMODES(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        CONST D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSMI, PVOID pContext);
typedef FNVBOXVIDPNENUMMONITORSOURCEMODES *PFNVBOXVIDPNENUMMONITORSOURCEMODES;

typedef DECLCALLBACK(BOOLEAN) FNVBOXVIDPNENUMTARGETSFORSOURCE(PDEVICE_EXTENSION pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, SIZE_T cTgtPaths, PVOID pContext);
typedef FNVBOXVIDPNENUMTARGETSFORSOURCE *PFNVBOXVIDPNENUMTARGETSFORSOURCE;

DECLCALLBACK(BOOLEAN) vboxVidPnCofuncModalityPathEnum(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo, PVOID pContext);

DECLCALLBACK(BOOLEAN) vboxVidPnCofuncModalitySourceModeEnum(struct _DEVICE_EXTENSION* pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        const D3DKMDT_VIDPN_SOURCE_MODE *pNewVidPnSourceModeInfo, PVOID pContext);

DECLCALLBACK(BOOLEAN) vboxVidPnCofuncModalityTargetModeEnum(struct _DEVICE_EXTENSION* pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        const D3DKMDT_VIDPN_TARGET_MODE *pNewVidPnTargetModeInfo, PVOID pContext);

DECLCALLBACK(BOOLEAN) vboxVidPnCommitPathEnum(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        const D3DKMDT_VIDPN_PRESENT_PATH *pNewVidPnPresentPathInfo, PVOID pContext);

NTSTATUS vboxVidPnCommitSourceModeForSrcId(struct _DEVICE_EXTENSION* pDevExt, const D3DKMDT_HVIDPN hDesiredVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, struct VBOXWDDM_ALLOCATION *pAllocation);

NTSTATUS vboxVidPnEnumPaths(D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        PFNVBOXVIDPNENUMPATHS pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumSourceModes(D3DKMDT_HVIDPNSOURCEMODESET hNewVidPnSourceModeSet, const DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
        PFNVBOXVIDPNENUMSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumTargetModes(D3DKMDT_HVIDPNTARGETMODESET hNewVidPnTargetModeSet, const DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
        PFNVBOXVIDPNENUMTARGETMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumMonitorSourceModes(D3DKMDT_HMONITORSOURCEMODESET hMonitorSMS, CONST DXGK_MONITORSOURCEMODESET_INTERFACE *pMonitorSMSIf,
        PFNVBOXVIDPNENUMMONITORSOURCEMODES pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnEnumTargetsForSource(PDEVICE_EXTENSION pDevExt, D3DKMDT_HVIDPNTOPOLOGY hVidPnTopology, const DXGK_VIDPNTOPOLOGY_INTERFACE* pVidPnTopologyInterface,
        CONST D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
        PFNVBOXVIDPNENUMTARGETSFORSOURCE pfnCallback, PVOID pContext);

NTSTATUS vboxVidPnPopulateMonitorSourceModeInfoFromLegacy(struct _DEVICE_EXTENSION* pDevExt,
        D3DKMDT_MONITOR_SOURCE_MODE *pMonitorSourceMode,
        D3DKMDT_2DREGION *pResolution,
        D3DKMDT_MONITOR_CAPABILITIES_ORIGIN enmOrigin,
        BOOLEAN bPreferred);

NTSTATUS vboxVidPnCreatePopulateVidPnFromLegacy(PDEVICE_EXTENSION pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface,
        VIDEO_MODE_INFORMATION *pModes, uint32_t cModes, int iPreferredMode,
        D3DKMDT_2DREGION *pResolutions, uint32_t cResolutions,
        const D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId, const D3DDDI_VIDEO_PRESENT_TARGET_ID tgtId);

NTSTATUS vboxVidPnCheckAddMonitorModes(struct _DEVICE_EXTENSION* pDevExt,
        D3DDDI_VIDEO_PRESENT_TARGET_ID targetId, D3DKMDT_MONITOR_CAPABILITIES_ORIGIN enmOrigin,
        D3DKMDT_2DREGION *pResolutions, uint32_t cResolutions, int32_t iPreferred);

NTSTATUS vboxVidPnCofuncModalityForPath(PVBOXVIDPNCOFUNCMODALITY pCbContext,
        D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
        BOOLEAN bModesAllowed);

void vboxVidPnDumpVidPn(const char * pPrefix, PDEVICE_EXTENSION pDevExt, D3DKMDT_HVIDPN hVidPn, const DXGK_VIDPN_INTERFACE* pVidPnInterface, const char * pSuffix);
void vboxVidPnDumpCofuncModalityArg(const char *pPrefix, CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY* CONST  pEnumCofuncModalityArg, const char *pSuffix);
#endif /* #ifndef ___VBoxVideoVidPn_h___ */
