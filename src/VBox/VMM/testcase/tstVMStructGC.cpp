/* $Id$ */
/** @file
 * tstVMMStructGC - Generate structure member and size checks from the GC perspective.
 *
 * This is built using the VBOXGC template but linked into a host
 * ring-3 executable, rather hacky.
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


/*
 * Sanity checks.
 */
#ifndef IN_GC
# error Incorrect template!
#endif
#if defined(IN_RING3) || defined(IN_RING0)
# error Incorrect template!
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/cfgm.h>
#include <VBox/cpum.h>
#include <VBox/mm.h>
#include <VBox/pgm.h>
#include <VBox/selm.h>
#include <VBox/trpm.h>
#include <VBox/vmm.h>
#include <VBox/stam.h>
#include "PDMInternal.h"
#include <VBox/pdm.h>
#include "CFGMInternal.h"
#include "CPUMInternal.h"
#include "MMInternal.h"
#include "PGMInternal.h"
#include "SELMInternal.h"
#include "TRPMInternal.h"
#include "TMInternal.h"
#include "IOMInternal.h"
#include "REMInternal.h"
#include "HWACCMInternal.h"
#ifdef VBOX_WITH_PATM
# include "PATMInternal.h"
#endif
#include "VMMInternal.h"
#include "DBGFInternal.h"
#include "STAMInternal.h"
#ifdef VBOX_WITH_CSAM
# include "CSAMInternal.h"
#endif
#include "EMInternal.h"
#include "REMInternal.h"
#ifdef VBOX_WITH_RRM
# include "RRMInternal.h"
#endif
#include <VBox/vm.h>
#include <VBox/param.h>
#include <VBox/x86.h>

/* we don't use iprt here because we're pretending to be in GC! */
#include <stdio.h>

#define GEN_CHECK_SIZE(s)   printf("    CHECK_SIZE(%s, %d);\n", #s, sizeof(s))
#define GEN_CHECK_OFF(s, m) printf("    CHECK_OFF(%s, %d, %s);\n", #s, RT_OFFSETOF(s, m), #m)

int main()
{
    GEN_CHECK_SIZE(CFGM);

    GEN_CHECK_SIZE(CPUM); // has .mac
    GEN_CHECK_SIZE(CPUMHOSTCTX);
    GEN_CHECK_SIZE(CPUMCTX);
    GEN_CHECK_SIZE(CPUMCTXCORE);

    GEN_CHECK_SIZE(DBGF);
    GEN_CHECK_OFF(DBGF, offVM);
    GEN_CHECK_OFF(DBGF, fAttached);
    GEN_CHECK_OFF(DBGF, fStoppedInHyper);
    GEN_CHECK_OFF(DBGF, PingPong);
    GEN_CHECK_OFF(DBGF, DbgEvent);
    GEN_CHECK_OFF(DBGF, enmVMMCmd);
    GEN_CHECK_OFF(DBGF, VMMCmdData);
    GEN_CHECK_OFF(DBGF, pInfoFirst);
    GEN_CHECK_OFF(DBGF, InfoCritSect);
    GEN_CHECK_OFF(DBGF, SymbolTree);
    GEN_CHECK_OFF(DBGF, pSymbolSpace);
    GEN_CHECK_OFF(DBGF, fSymInited);
    GEN_CHECK_OFF(DBGF, cHwBreakpoints);
    GEN_CHECK_OFF(DBGF, cBreakpoints);
    GEN_CHECK_OFF(DBGF, aHwBreakpoints);
    GEN_CHECK_OFF(DBGF, aBreakpoints);
    GEN_CHECK_OFF(DBGF, iActiveBp);
    GEN_CHECK_OFF(DBGF, fSingleSteppingRaw);
    GEN_CHECK_SIZE(DBGFEVENT);

    GEN_CHECK_SIZE(EM);
    GEN_CHECK_OFF(EM, offVM);
    GEN_CHECK_OFF(EM, pCtx);
    GEN_CHECK_OFF(EM, enmState);
    GEN_CHECK_OFF(EM, fForceRAW);
    GEN_CHECK_OFF(EM, u.achPaddingFatalLongJump);
    GEN_CHECK_OFF(EM, StatForcedActions);
    GEN_CHECK_OFF(EM, StatTotalClis);
    GEN_CHECK_OFF(EM, pStatsHC);
    GEN_CHECK_OFF(EM, pStatsGC);
    GEN_CHECK_OFF(EM, pCliStatTree);

    GEN_CHECK_SIZE(IOM);

    GEN_CHECK_SIZE(MM);
    GEN_CHECK_OFF(MM, offVM);
    GEN_CHECK_OFF(MM, offHyperNextStatic);
    GEN_CHECK_OFF(MM, cbHyperArea);
    GEN_CHECK_OFF(MM, fPGMInitialized);
    GEN_CHECK_OFF(MM, offLookupHyper);
    GEN_CHECK_OFF(MM, pHyperHeapHC);
    GEN_CHECK_OFF(MM, pHyperHeapGC);
    GEN_CHECK_OFF(MM, pLockedMem);
    GEN_CHECK_OFF(MM, pPagePool);
    GEN_CHECK_OFF(MM, pPagePoolLow);
    GEN_CHECK_OFF(MM, pvDummyPage);
    GEN_CHECK_OFF(MM, HCPhysDummyPage);
    GEN_CHECK_OFF(MM, cbRAMSize);
    GEN_CHECK_OFF(MM, pvRamBaseHC);
    GEN_CHECK_OFF(MM, cbRamBase);
    GEN_CHECK_OFF(MM, pHeap);
    GEN_CHECK_SIZE(MMHYPERSTAT);
    GEN_CHECK_SIZE(MMHYPERCHUNK);
    GEN_CHECK_SIZE(MMHYPERCHUNKFREE);
    GEN_CHECK_SIZE(MMHYPERHEAP);
    GEN_CHECK_OFF(MMHYPERHEAP, u32Magic);
    GEN_CHECK_OFF(MMHYPERHEAP, cbHeap);
    GEN_CHECK_OFF(MMHYPERHEAP, pbHeapHC);
    GEN_CHECK_OFF(MMHYPERHEAP, pbHeapGC);
    GEN_CHECK_OFF(MMHYPERHEAP, pVMHC);
    GEN_CHECK_OFF(MMHYPERHEAP, pVMGC);
    GEN_CHECK_OFF(MMHYPERHEAP, cbFree);
    GEN_CHECK_OFF(MMHYPERHEAP, offFreeHead);
    GEN_CHECK_OFF(MMHYPERHEAP, offFreeTail);
    GEN_CHECK_OFF(MMHYPERHEAP, offPageAligned);
    GEN_CHECK_OFF(MMHYPERHEAP, HyperHeapStatTree);
    GEN_CHECK_SIZE(MMLOOKUPHYPER);
    GEN_CHECK_OFF(MMLOOKUPHYPER, offNext);
    GEN_CHECK_OFF(MMLOOKUPHYPER, off);
    GEN_CHECK_OFF(MMLOOKUPHYPER, cb);
    GEN_CHECK_OFF(MMLOOKUPHYPER, enmType);
    GEN_CHECK_OFF(MMLOOKUPHYPER, u.Locked.pvHC);
    GEN_CHECK_OFF(MMLOOKUPHYPER, u.Locked.pLockedMem);
    GEN_CHECK_OFF(MMLOOKUPHYPER, u.HCPhys.pvHC);
    GEN_CHECK_OFF(MMLOOKUPHYPER, u.HCPhys.HCPhys);
    GEN_CHECK_OFF(MMLOOKUPHYPER, u.GCPhys.GCPhys);
    GEN_CHECK_OFF(MMLOOKUPHYPER, pszDesc);

    GEN_CHECK_SIZE(PDM);
    GEN_CHECK_OFF(PDM, offVM);
    GEN_CHECK_OFF(PDM, pModules);
    GEN_CHECK_OFF(PDM, pDevs);
    GEN_CHECK_OFF(PDM, pDevInstances);
    GEN_CHECK_OFF(PDM, pDrvs);
    GEN_CHECK_OFF(PDM, pCritSects);
    GEN_CHECK_OFF(PDM, aPciBuses);
    GEN_CHECK_OFF(PDM, Pic);
    GEN_CHECK_OFF(PDM, Apic);
    GEN_CHECK_OFF(PDM, IoApic);
    GEN_CHECK_OFF(PDM, pDmac);
    GEN_CHECK_OFF(PDM, pRtc);
    GEN_CHECK_OFF(PDM, pDevHlpQueueGC);
    GEN_CHECK_OFF(PDM, pDevHlpQueueHC);
    GEN_CHECK_OFF(PDM, cQueuedCritSectLeaves);
    GEN_CHECK_OFF(PDM, apQueuedCritSectsLeaves);
    GEN_CHECK_OFF(PDM, pQueuesTimer);
    GEN_CHECK_OFF(PDM, pQueuesForced);
    GEN_CHECK_OFF(PDM, pQueueFlushGC);
    GEN_CHECK_OFF(PDM, pQueueFlushHC);
    GEN_CHECK_OFF(PDM, cPollers);
    GEN_CHECK_OFF(PDM, apfnPollers);
    GEN_CHECK_OFF(PDM, aDrvInsPollers);
    GEN_CHECK_OFF(PDM, pTimerPollers);
#ifdef VBOX_WITH_PDM_LOCK
    GEN_CHECK_OFF(PDM, CritSect);
#endif
    GEN_CHECK_OFF(PDM, StatQueuedCritSectLeaves);

    GEN_CHECK_SIZE(PGM);
    GEN_CHECK_OFF(PGM,  offVM);
    GEN_CHECK_OFF(PGM, paDynPageMap32BitPTEsGC);
    GEN_CHECK_OFF(PGM, paDynPageMapPaePTEsGC);
    GEN_CHECK_OFF(PGM, enmHostMode);
    GEN_CHECK_OFF(PGM, enmShadowMode);
    GEN_CHECK_OFF(PGM, enmGuestMode);
    GEN_CHECK_OFF(PGM, GCPhysCR3);
    GEN_CHECK_OFF(PGM, GCPtrCR3Mapping);
    GEN_CHECK_OFF(PGM, GCPhysGstCR3Monitored);
    GEN_CHECK_OFF(PGM, pGuestPDHC);
    GEN_CHECK_OFF(PGM, pGuestPDGC);
    GEN_CHECK_OFF(PGM, pGstPaePDPTRHC);
    GEN_CHECK_OFF(PGM, pGstPaePDPTRGC);
    GEN_CHECK_OFF(PGM, apGstPaePDsHC);
    GEN_CHECK_OFF(PGM, apGstPaePDsGC);
    GEN_CHECK_OFF(PGM, aGCPhysGstPaePDs);
    GEN_CHECK_OFF(PGM, aGCPhysGstPaePDsMonitored);
    GEN_CHECK_OFF(PGM, pHC32BitPD);
    GEN_CHECK_OFF(PGM, pGC32BitPD);
    GEN_CHECK_OFF(PGM, HCPhys32BitPD);
    GEN_CHECK_OFF(PGM, apHCPaePDs);
    GEN_CHECK_OFF(PGM, apGCPaePDs);
    GEN_CHECK_OFF(PGM, aHCPhysPaePDs);
    GEN_CHECK_OFF(PGM, pHCPaePDPTR);
    GEN_CHECK_OFF(PGM, pGCPaePDPTR);
    GEN_CHECK_OFF(PGM, HCPhysPaePDPTR);
    GEN_CHECK_OFF(PGM, pHCPaePML4);
    GEN_CHECK_OFF(PGM, pGCPaePML4);
    GEN_CHECK_OFF(PGM, HCPhysPaePML4);
    GEN_CHECK_OFF(PGM, pfnR3ShwRelocate);
    GEN_CHECK_OFF(PGM, pfnR3ShwExit);
    GEN_CHECK_OFF(PGM, pfnR3ShwGetPage);
    GEN_CHECK_OFF(PGM, pfnR3ShwModifyPage);
    GEN_CHECK_OFF(PGM, pfnR3ShwGetPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnR3ShwSetPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnR3ShwModifyPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnGCShwGetPage);
    GEN_CHECK_OFF(PGM, pfnGCShwModifyPage);
    GEN_CHECK_OFF(PGM, pfnGCShwGetPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnGCShwSetPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnGCShwModifyPDEByIndex);
    GEN_CHECK_OFF(PGM, pfnR3GstRelocate);
    GEN_CHECK_OFF(PGM, pfnR3GstExit);
    GEN_CHECK_OFF(PGM, pfnR3GstMonitorCR3);
    GEN_CHECK_OFF(PGM, pfnR3GstUnmonitorCR3);
    GEN_CHECK_OFF(PGM, pfnR3GstMapCR3);
    GEN_CHECK_OFF(PGM, pfnR3GstUnmapCR3);
    GEN_CHECK_OFF(PGM, pfnR3GstGetPage);
    GEN_CHECK_OFF(PGM, pfnR3GstModifyPage);
    GEN_CHECK_OFF(PGM, pfnR3GstGetPDE);
    GEN_CHECK_OFF(PGM, pfnGCGstGetPage);
    GEN_CHECK_OFF(PGM, pfnGCGstModifyPage);
    GEN_CHECK_OFF(PGM, pfnGCGstGetPDE);
    GEN_CHECK_OFF(PGM, pfnR3BthRelocate);
    GEN_CHECK_OFF(PGM, pfnR3BthSyncCR3);
    GEN_CHECK_OFF(PGM, pfnR3BthTrap0eHandler);
    GEN_CHECK_OFF(PGM, pfnR3BthInvalidatePage);
    GEN_CHECK_OFF(PGM, pfnR3BthSyncPage);
    GEN_CHECK_OFF(PGM, pfnR3BthPrefetchPage);
    GEN_CHECK_OFF(PGM, pfnR3BthVerifyAccessSyncPage);
    GEN_CHECK_OFF(PGM, pfnR3BthAssertCR3);
    GEN_CHECK_OFF(PGM, pfnGCBthTrap0eHandler);
    GEN_CHECK_OFF(PGM, pfnGCBthInvalidatePage);
    GEN_CHECK_OFF(PGM, pfnGCBthSyncPage);
    GEN_CHECK_OFF(PGM, pfnGCBthPrefetchPage);
    GEN_CHECK_OFF(PGM, pfnGCBthVerifyAccessSyncPage);
    GEN_CHECK_OFF(PGM, pfnGCBthAssertCR3);
    GEN_CHECK_OFF(PGM, pRamRangesHC);
    GEN_CHECK_OFF(PGM, pRamRangesGC);
    GEN_CHECK_OFF(PGM, cbRamSize);
    GEN_CHECK_OFF(PGM, pTreesHC);
    GEN_CHECK_OFF(PGM, pTreesGC);
    GEN_CHECK_OFF(PGM, pMappingsHC);
    GEN_CHECK_OFF(PGM, pMappingsGC);
    GEN_CHECK_OFF(PGM, fMappingsFixed);
    GEN_CHECK_OFF(PGM, GCPtrMappingFixed);
    GEN_CHECK_OFF(PGM, cbMappingFixed);
    GEN_CHECK_OFF(PGM, pInterPD);
    GEN_CHECK_OFF(PGM, apInterPTs);
    GEN_CHECK_OFF(PGM, apInterPaePTs);
    GEN_CHECK_OFF(PGM, apInterPaePDs);
    GEN_CHECK_OFF(PGM, pInterPaePDPTR);
    GEN_CHECK_OFF(PGM, pInterPaePDPTR64);
    GEN_CHECK_OFF(PGM, pInterPaePML4);
    GEN_CHECK_OFF(PGM, HCPhysInterPD);
    GEN_CHECK_OFF(PGM, HCPhysInterPaePDPTR);
    GEN_CHECK_OFF(PGM, HCPhysInterPaePML4);
    GEN_CHECK_OFF(PGM, pbDynPageMapBaseGC);
    GEN_CHECK_OFF(PGM, iDynPageMapLast);
    GEN_CHECK_OFF(PGM, aHCPhysDynPageMapCache);
    GEN_CHECK_OFF(PGM, GCPhysA20Mask);
    GEN_CHECK_OFF(PGM, fA20Enabled);
    GEN_CHECK_OFF(PGM, fSyncFlags);
    GEN_CHECK_OFF(PGM, CritSect);
#ifdef PGM_PD_CACHING_ENABLED
    GEN_CHECK_OFF(PGM, pdcache);
#endif
    GEN_CHECK_OFF(PGM, pgmphysreadcache);
    GEN_CHECK_OFF(PGM, pgmphyswritecache);
    GEN_CHECK_SIZE(PGMMAPPING);
    GEN_CHECK_OFF(PGMMAPPING, pNextHC);
    GEN_CHECK_OFF(PGMMAPPING, pNextGC);
    GEN_CHECK_OFF(PGMMAPPING, GCPtr);
    GEN_CHECK_OFF(PGMMAPPING, GCPtrLast);
    GEN_CHECK_OFF(PGMMAPPING, cb);
    GEN_CHECK_OFF(PGMMAPPING, pfnRelocate);
    GEN_CHECK_OFF(PGMMAPPING, pvUser);
    GEN_CHECK_OFF(PGMMAPPING, pszDesc);
    GEN_CHECK_OFF(PGMMAPPING, cPTs);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].HCPhysPT);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].pPTHC);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].pPTGC);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].HCPhysPaePT0);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].HCPhysPaePT1);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].paPaePTsHC);
    GEN_CHECK_OFF(PGMMAPPING, aPTs[1].paPaePTsGC);
    GEN_CHECK_SIZE(PGMPHYSHANDLER);
    GEN_CHECK_OFF(PGMPHYSHANDLER, Core);
    GEN_CHECK_OFF(PGMPHYSHANDLER, enmType);
    GEN_CHECK_OFF(PGMPHYSHANDLER, cPages);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pfnHandlerR3);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pvUserR3);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pfnHandlerR0);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pvUserR0);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pfnHandlerGC);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pvUserGC);
    GEN_CHECK_OFF(PGMPHYSHANDLER, pszDesc);
    GEN_CHECK_SIZE(PGMPHYS2VIRTHANDLER);
    GEN_CHECK_OFF(PGMPHYS2VIRTHANDLER, Core);
    GEN_CHECK_OFF(PGMPHYS2VIRTHANDLER, offVirtHandler);
    GEN_CHECK_SIZE(PGMVIRTHANDLER);
    GEN_CHECK_OFF(PGMVIRTHANDLER, Core);
    GEN_CHECK_OFF(PGMVIRTHANDLER, enmType);
    GEN_CHECK_OFF(PGMVIRTHANDLER, GCPtr);
    GEN_CHECK_OFF(PGMVIRTHANDLER, GCPtrLast);
    GEN_CHECK_OFF(PGMVIRTHANDLER, cb);
    GEN_CHECK_OFF(PGMVIRTHANDLER, pfnHandlerHC);
    GEN_CHECK_OFF(PGMVIRTHANDLER, pfnHandlerGC);
    GEN_CHECK_OFF(PGMVIRTHANDLER, pszDesc);
    GEN_CHECK_OFF(PGMVIRTHANDLER, cPages);
    GEN_CHECK_OFF(PGMVIRTHANDLER, aPhysToVirt);
    GEN_CHECK_SIZE(PGMRAMRANGE);
    GEN_CHECK_OFF(PGMRAMRANGE, pNextHC);
    GEN_CHECK_OFF(PGMRAMRANGE, pNextGC);
    GEN_CHECK_OFF(PGMRAMRANGE, GCPhys);
    GEN_CHECK_OFF(PGMRAMRANGE, GCPhysLast);
    GEN_CHECK_OFF(PGMRAMRANGE, cb);
    GEN_CHECK_OFF(PGMRAMRANGE, pvHC);
    GEN_CHECK_OFF(PGMRAMRANGE, aHCPhys);
    GEN_CHECK_SIZE(PGMTREES);
    GEN_CHECK_OFF(PGMTREES, PhysHandlers);
    GEN_CHECK_OFF(PGMTREES, VirtHandlers);
    GEN_CHECK_OFF(PGMTREES, PhysToVirtHandlers);
    GEN_CHECK_OFF(PGMTREES, auPadding);

    GEN_CHECK_SIZE(REM);

    GEN_CHECK_SIZE(SELM);
    GEN_CHECK_OFF(SELM, offVM);
    GEN_CHECK_OFF(SELM, SelCS);
    GEN_CHECK_OFF(SELM, SelDS);
    GEN_CHECK_OFF(SELM, SelCS64);
    GEN_CHECK_OFF(SELM, SelTSS);
    GEN_CHECK_OFF(SELM, SelTSSTrap08);
    GEN_CHECK_OFF(SELM, paGdtHC);
    GEN_CHECK_OFF(SELM, paGdtGC);
    GEN_CHECK_OFF(SELM, GuestGdtr);
    GEN_CHECK_OFF(SELM, cbEffGuestGdtLimit);
    GEN_CHECK_OFF(SELM, HCPtrLdt);
    GEN_CHECK_OFF(SELM, GCPtrLdt);
    GEN_CHECK_OFF(SELM, GCPtrGuestLdt);
    GEN_CHECK_OFF(SELM, cbLdtLimit);
    GEN_CHECK_OFF(SELM, offLdtHyper);
    GEN_CHECK_OFF(SELM, Tss);
    GEN_CHECK_OFF(SELM, TssTrap08);
    GEN_CHECK_OFF(SELM, TssTrap0a);
    GEN_CHECK_OFF(SELM, GCPtrTss);
    GEN_CHECK_OFF(SELM, GCPtrGuestTss);
    GEN_CHECK_OFF(SELM, cbGuestTss);
    GEN_CHECK_OFF(SELM, fGuestTss32Bit);
    GEN_CHECK_OFF(SELM, cbMonitoredGuestTss);
    GEN_CHECK_OFF(SELM, GCSelTss);
    GEN_CHECK_OFF(SELM, fGDTRangeRegistered);
    GEN_CHECK_OFF(SELM, StatUpdateFromCPUM);

    GEN_CHECK_SIZE(STAM);

    GEN_CHECK_SIZE(TM);
    GEN_CHECK_OFF(TM, offVM);
    GEN_CHECK_OFF(TM, pvGIPR3);
    //GEN_CHECK_OFF(TM, pvGIPR0);
    GEN_CHECK_OFF(TM, pvGIPGC);
    GEN_CHECK_OFF(TM, fTSCTicking);
    GEN_CHECK_OFF(TM, u64TSCOffset);
    GEN_CHECK_OFF(TM, u64TSC);
    GEN_CHECK_OFF(TM, cTSCTicksPerSecond);
    GEN_CHECK_OFF(TM, uReserved);
    GEN_CHECK_OFF(TM, fVirtualTicking);
    GEN_CHECK_OFF(TM, u64VirtualOffset);
    GEN_CHECK_OFF(TM, u64Virtual);
    GEN_CHECK_OFF(TM, u64VirtualSync);
    GEN_CHECK_OFF(TM, u32VirtualSyncCatchupPrecentage);
    GEN_CHECK_OFF(TM, u32VirtualSyncCatchupStopThreashold);
    GEN_CHECK_OFF(TM, u64VirtualSyncCatchupStartTreashold);
    GEN_CHECK_OFF(TM, u64VirtualSyncCatchupGiveUpTreashold);
    GEN_CHECK_OFF(TM, pTimer);
    GEN_CHECK_OFF(TM, u32TimerMillies);
    GEN_CHECK_OFF(TM, pFree);
    GEN_CHECK_OFF(TM, pCreated);
    GEN_CHECK_OFF(TM, paTimerQueuesR3);
    GEN_CHECK_OFF(TM, paTimerQueuesR0);
    GEN_CHECK_OFF(TM, paTimerQueuesGC);
    GEN_CHECK_OFF(TM, StatDoQueues);
    GEN_CHECK_SIZE(TMTIMER);
    GEN_CHECK_OFF(TMTIMER, u64Expire);
    GEN_CHECK_OFF(TMTIMER, enmClock);
    GEN_CHECK_OFF(TMTIMER, enmType);
    GEN_CHECK_OFF(TMTIMER, u.Dev.pfnTimer);
    GEN_CHECK_OFF(TMTIMER, u.Dev.pDevIns);
    GEN_CHECK_OFF(TMTIMER, u.Drv.pfnTimer);
    GEN_CHECK_OFF(TMTIMER, u.Drv.pDrvIns);
    GEN_CHECK_OFF(TMTIMER, u.Internal.pfnTimer);
    GEN_CHECK_OFF(TMTIMER, u.Internal.pvUser);
    GEN_CHECK_OFF(TMTIMER, u.External.pfnTimer);
    GEN_CHECK_OFF(TMTIMER, u.External.pvUser);
    GEN_CHECK_OFF(TMTIMER, enmState);
    GEN_CHECK_OFF(TMTIMER, offScheduleNext);
    GEN_CHECK_OFF(TMTIMER, offNext);
    GEN_CHECK_OFF(TMTIMER, offPrev);
    GEN_CHECK_OFF(TMTIMER, pBigNext);
    GEN_CHECK_OFF(TMTIMER, pBigPrev);
    GEN_CHECK_OFF(TMTIMER, pszDesc);
    GEN_CHECK_OFF(TMTIMER, pVMR0);
    GEN_CHECK_OFF(TMTIMER, pVMR3);
    GEN_CHECK_OFF(TMTIMER, pVMGC);
    GEN_CHECK_SIZE(TMTIMERQUEUE);
    GEN_CHECK_OFF(TMTIMERQUEUE, offActive);
    GEN_CHECK_OFF(TMTIMERQUEUE, offSchedule);
    GEN_CHECK_OFF(TMTIMERQUEUE, enmClock);

    GEN_CHECK_SIZE(TRPM); // has .mac
    GEN_CHECK_SIZE(VM);  // has .mac
    GEN_CHECK_SIZE(VMM);
    GEN_CHECK_OFF(VMM, offVM);
    GEN_CHECK_OFF(VMM, cbCoreCode);
    GEN_CHECK_OFF(VMM, HCPhysCoreCode);
    GEN_CHECK_OFF(VMM, pvHCCoreCodeR3);
    GEN_CHECK_OFF(VMM, pvHCCoreCodeR0);
    GEN_CHECK_OFF(VMM, pvGCCoreCode);
    GEN_CHECK_OFF(VMM, enmSwitcher);
    GEN_CHECK_OFF(VMM, aoffSwitchers);
    GEN_CHECK_OFF(VMM, aoffSwitchers[1]);
    GEN_CHECK_OFF(VMM, pfnR0HostToGuest);
    GEN_CHECK_OFF(VMM, pfnGCGuestToHost);
    GEN_CHECK_OFF(VMM, pfnGCCallTrampoline);
    GEN_CHECK_OFF(VMM, pfnCPUMGCResumeGuest);
    GEN_CHECK_OFF(VMM, pfnCPUMGCResumeGuestV86);
    GEN_CHECK_OFF(VMM, iLastGCRc);
    GEN_CHECK_OFF(VMM, pbHCStack);
    GEN_CHECK_OFF(VMM, pbGCStack);
    GEN_CHECK_OFF(VMM, pbGCStackBottom);
    GEN_CHECK_OFF(VMM, pLoggerGC);
    GEN_CHECK_OFF(VMM, pLoggerHC);
    GEN_CHECK_OFF(VMM, cbLoggerGC);
    GEN_CHECK_OFF(VMM, CritSectVMLock);
    GEN_CHECK_OFF(VMM, pYieldTimer);
    GEN_CHECK_OFF(VMM, cYieldResumeMillies);
    GEN_CHECK_OFF(VMM, cYieldEveryMillies);
    GEN_CHECK_OFF(VMM, StatRunGC);

    GEN_CHECK_SIZE(RTPINGPONG);
    GEN_CHECK_SIZE(RTCRITSECT);
    GEN_CHECK_OFF(RTCRITSECT, u32Magic);
    GEN_CHECK_OFF(RTCRITSECT, cLockers);
    GEN_CHECK_OFF(RTCRITSECT, NativeThreadOwner);
    GEN_CHECK_OFF(RTCRITSECT, cNestings);
    GEN_CHECK_OFF(RTCRITSECT, fFlags);
    GEN_CHECK_OFF(RTCRITSECT, EventSem);
    GEN_CHECK_OFF(RTCRITSECT, Strict.ThreadOwner);
    GEN_CHECK_OFF(RTCRITSECT, Strict.pszEnterFile);
    GEN_CHECK_OFF(RTCRITSECT, Strict.u32EnterLine);
    GEN_CHECK_OFF(RTCRITSECT, Strict.uEnterId);

    return 0;
}



