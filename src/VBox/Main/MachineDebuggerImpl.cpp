/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "MachineDebuggerImpl.h"

#include "Global.h"
#include "ConsoleImpl.h"

#include "AutoCaller.h"
#include "Logging.h"

#include <VBox/em.h>
#include <VBox/patm.h>
#include <VBox/csam.h>
#include <VBox/vm.h>
#include <VBox/tm.h>
#include <VBox/err.h>
#include <VBox/hwaccm.h>
#include <iprt/cpp/utils.h>

// defines
/////////////////////////////////////////////////////////////////////////////


// globals
/////////////////////////////////////////////////////////////////////////////


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

MachineDebugger::MachineDebugger()
    : mParent(NULL)
{
}

MachineDebugger::~MachineDebugger()
{
}

HRESULT MachineDebugger::FinalConstruct()
{
    unconst(mParent) = NULL;
    return S_OK;
}

void MachineDebugger::FinalRelease()
{
    uninit();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the machine debugger object.
 *
 * @returns COM result indicator
 * @param aParent handle of our parent object
 */
HRESULT MachineDebugger::init (Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    mSinglestepQueued = ~0;
    mRecompileUserQueued = ~0;
    mRecompileSupervisorQueued = ~0;
    mPatmEnabledQueued = ~0;
    mCsamEnabledQueued = ~0;
    mLogEnabledQueued = ~0;
    mVirtualTimeRateQueued = ~0;
    mFlushMode = false;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void MachineDebugger::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
    mFlushMode = false;
}

// IMachineDebugger properties
/////////////////////////////////////////////////////////////////////////////

/**
 * Returns the current singlestepping flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(Singlestep) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /** @todo */
    ReturnComNotImplemented();
}

/**
 * Sets the singlestepping flag.
 *
 * @returns COM status code
 * @param aEnable new singlestepping flag
 */
STDMETHODIMP MachineDebugger::COMSETTER(Singlestep) (BOOL aEnable)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /** @todo */
    ReturnComNotImplemented();
}

/**
 * Returns the current recompile user mode code flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(RecompileUser) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = !EMIsRawRing3Enabled (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Sets the recompile user mode code flag.
 *
 * @returns COM status
 * @param   aEnable new user mode code recompile flag.
 */
STDMETHODIMP MachineDebugger::COMSETTER(RecompileUser) (BOOL aEnable)
{
    LogFlowThisFunc(("enable=%d\n", aEnable));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mRecompileUserQueued = aEnable;
        return S_OK;
    }

    Console::SafeVMPtr pVM (mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    EMRAWMODE rawModeFlag = aEnable ? EMRAW_RING3_DISABLE : EMRAW_RING3_ENABLE;
    int rcVBox = VMR3ReqCallWait(pVM, VMCPUID_ANY, (PFNRT)EMR3RawSetMode, 2, pVM.raw(), rawModeFlag);
    if (RT_SUCCESS(rcVBox))
        return S_OK;

    AssertMsgFailed (("Could not set raw mode flags to %d, rcVBox = %Rrc\n",
                      rawModeFlag, rcVBox));
    return E_FAIL;
}

/**
 * Returns the current recompile supervisor code flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(RecompileSupervisor) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = !EMIsRawRing0Enabled (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Sets the new recompile supervisor code flag.
 *
 * @returns COM status code
 * @param   aEnable new recompile supervisor code flag
 */
STDMETHODIMP MachineDebugger::COMSETTER(RecompileSupervisor) (BOOL aEnable)
{
    LogFlowThisFunc(("enable=%d\n", aEnable));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mRecompileSupervisorQueued = aEnable;
        return S_OK;
    }

    Console::SafeVMPtr pVM (mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    EMRAWMODE rawModeFlag = aEnable ? EMRAW_RING0_DISABLE : EMRAW_RING0_ENABLE;
    int rcVBox = VMR3ReqCallWait(pVM, VMCPUID_ANY, (PFNRT)EMR3RawSetMode, 2, pVM.raw(), rawModeFlag);
    if (RT_SUCCESS(rcVBox))
        return S_OK;

    AssertMsgFailed (("Could not set raw mode flags to %d, rcVBox = %Rrc\n",
                      rawModeFlag, rcVBox));
    return E_FAIL;
}

/**
 * Returns the current patch manager enabled flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(PATMEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = PATMIsEnabled (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Set the new patch manager enabled flag.
 *
 * @returns COM status code
 * @param   aEnable new patch manager enabled flag
 */
STDMETHODIMP MachineDebugger::COMSETTER(PATMEnabled) (BOOL aEnable)
{
    LogFlowThisFunc(("enable=%d\n", aEnable));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mPatmEnabledQueued = aEnable;
        return S_OK;
    }

    Console::SafeVMPtr pVM(mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    PATMR3AllowPatching (pVM, aEnable);

    return S_OK;
}

/**
 * Returns the current code scanner enabled flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(CSAMEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = CSAMIsEnabled (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Sets the new code scanner enabled flag.
 *
 * @returns COM status code
 * @param   aEnable new code scanner enabled flag
 */
STDMETHODIMP MachineDebugger::COMSETTER(CSAMEnabled) (BOOL aEnable)
{
    LogFlowThisFunc(("enable=%d\n", aEnable));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mCsamEnabledQueued = aEnable;
        return S_OK;
    }

    Console::SafeVMPtr pVM(mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    int vrc;
    if (aEnable)
        vrc = CSAMEnableScanning (pVM);
    else
        vrc = CSAMDisableScanning (pVM);

    if (RT_FAILURE(vrc))
    {
        /** @todo handle error case */
    }

    return S_OK;
}

/**
 * Returns the log enabled / disabled status.
 *
 * @returns COM status code
 * @param   aEnabled     address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(LogEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

#ifdef LOG_ENABLED
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    const PRTLOGGER pLogInstance = RTLogDefaultInstance();
    *aEnabled = pLogInstance && !(pLogInstance->fFlags & RTLOGFLAGS_DISABLED);
#else
    *aEnabled = false;
#endif

    return S_OK;
}

/**
 * Enables or disables logging.
 *
 * @returns COM status code
 * @param   aEnabled    The new code log state.
 */
STDMETHODIMP MachineDebugger::COMSETTER(LogEnabled) (BOOL aEnabled)
{
    LogFlowThisFunc(("aEnabled=%d\n", aEnabled));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mLogEnabledQueued = aEnabled;
        return S_OK;
    }

    Console::SafeVMPtr pVM(mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

#ifdef LOG_ENABLED
    int vrc = DBGFR3LogModifyFlags (pVM, aEnabled ? "enabled" : "disabled");
    if (RT_FAILURE(vrc))
    {
        /** @todo handle error code. */
    }
#endif

    return S_OK;
}

STDMETHODIMP MachineDebugger::COMGETTER(LogFlags)(BSTR *a_pbstrSettings)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::COMGETTER(LogGroups)(BSTR *a_pbstrSettings)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::COMGETTER(LogDestinations)(BSTR *a_pbstrSettings)
{
    ReturnComNotImplemented();
}

/**
 * Returns the current hardware virtualization flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(HWVirtExEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = HWACCMIsEnabled (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Returns the current nested paging flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(HWVirtExNestedPagingEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = HWACCMR3IsNestedPagingActive (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Returns the current VPID flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(HWVirtExVPIDEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aEnabled = HWACCMR3IsVPIDActive (pVM.raw());
    else
        *aEnabled = false;

    return S_OK;
}

STDMETHODIMP MachineDebugger::COMGETTER(OSName)(BSTR *a_pbstrName)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::COMGETTER(OSVersion)(BSTR *a_pbstrVersion)
{
    ReturnComNotImplemented();
}

/**
 * Returns the current PAE flag.
 *
 * @returns COM status code
 * @param   aEnabled address of result variable
 */
STDMETHODIMP MachineDebugger::COMGETTER(PAEEnabled) (BOOL *aEnabled)
{
    CheckComArgOutPointerValid(aEnabled);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
    {
        uint64_t cr4 = CPUMGetGuestCR4 (VMMGetCpu0(pVM.raw()));
        *aEnabled = !!(cr4 & X86_CR4_PAE);
    }
    else
        *aEnabled = false;

    return S_OK;
}

/**
 * Returns the current virtual time rate.
 *
 * @returns COM status code.
 * @param   aPct     Where to store the rate.
 */
STDMETHODIMP MachineDebugger::COMGETTER(VirtualTimeRate) (ULONG *aPct)
{
    CheckComArgOutPointerValid(aPct);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtrQuiet pVM (mParent);

    if (pVM.isOk())
        *aPct = TMGetWarpDrive (pVM);
    else
        *aPct = 100;

    return S_OK;
}

/**
 * Returns the current virtual time rate.
 *
 * @returns COM status code.
 * @param   aPct     Where to store the rate.
 */
STDMETHODIMP MachineDebugger::COMSETTER(VirtualTimeRate) (ULONG aPct)
{
    if (aPct < 2 || aPct > 20000)
        return E_INVALIDARG;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (queueSettings())
    {
        // queue the request
        mVirtualTimeRateQueued = aPct;
        return S_OK;
    }

    Console::SafeVMPtr pVM(mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    int vrc = TMR3SetWarpDrive (pVM, aPct);
    if (RT_FAILURE(vrc))
    {
        /** @todo handle error code. */
    }

    return S_OK;
}

/**
 * Hack for getting the VM handle.
 * This is only temporary (promise) while prototyping the debugger.
 *
 * @returns COM status code
 * @param   aVm      Where to store the vm handle.
 *                  Since there is no uintptr_t in COM, we're using the max integer.
 *                  (No, ULONG is not pointer sized!)
 */
STDMETHODIMP MachineDebugger::COMGETTER(VM) (LONG64 *aVm)
{
    CheckComArgOutPointerValid(aVm);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Console::SafeVMPtr pVM(mParent);
    if (FAILED(pVM.rc())) return pVM.rc();

    *aVm = (intptr_t)pVM.raw();

    /*
     *  Note: pVM protection provided by SafeVMPtr is no more effective
     *  after we return from this method.
     */

    return S_OK;
}

// IMachineDebugger methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP MachineDebugger::DumpGuestCore(IN_BSTR a_bstrFilename, IN_BSTR a_bstrCompression)
{
    CheckComArgStrNotEmptyOrNull(a_bstrFilename);
    Utf8Str strFilename(a_bstrFilename);
    if (a_bstrCompression && *a_bstrCompression)
        return setError(E_INVALIDARG, tr("The compression parameter must be empty"));

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.rc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Console::SafeVMPtr ptrVM(mParent);
        hrc = ptrVM.rc();
        if (SUCCEEDED(hrc))
        {
            int vrc = DBGFR3CoreWrite(ptrVM, strFilename.c_str(), false /*fReplaceFile*/);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = setError(E_FAIL, tr("DBGFR3CoreWrite failed with %Rrc"), vrc);
        }
    }

    return hrc;
}

STDMETHODIMP MachineDebugger::DumpHostProcessCore(IN_BSTR a_bstrFilename, IN_BSTR a_bstrCompression)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::Info(IN_BSTR a_bstrName, IN_BSTR a_bstrArgs, BSTR *a_pbstrInfo)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::InjectNMI()
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.rc();
    if (SUCCEEDED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Console::SafeVMPtr ptrVM(mParent);
        hrc = ptrVM.rc();
        if (SUCCEEDED(hrc))
        {
            int vrc = HWACCMR3InjectNMI(ptrVM);
            if (RT_SUCCESS(vrc))
                hrc = S_OK;
            else
                hrc = setError(E_FAIL, tr("HWACCMR3InjectNMI failed with %Rrc"), vrc);
        }
    }
    return hrc;
}

STDMETHODIMP MachineDebugger::ModifyLogFlags(IN_BSTR a_bstrSettings)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::ModifyLogGroups(IN_BSTR a_bstrSettings)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::ModifyLogDestinations(IN_BSTR a_bstrSettings)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::ReadPhysicalMemory(LONG64 a_Address, ULONG a_cbRead, ComSafeArrayOut(BYTE, a_abData))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::WritePhysicalMemory(LONG64 a_Address, ULONG a_cbRead, ComSafeArrayIn(BYTE, a_abData))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::ReadVirtualMemory(ULONG a_idCpu, LONG64 a_Address, ULONG a_cbRead, ComSafeArrayOut(BYTE, a_abData))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::WriteVirtualMemory(ULONG a_idCpu, LONG64 a_Address, ULONG a_cbRead, ComSafeArrayIn(BYTE, a_abData))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::DetectOS(BSTR *a_pbstrName)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::GetRegister(ULONG a_idCpu, IN_BSTR a_bstrName, BSTR *a_pbstrValue)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::GetRegisters(ULONG a_idCpu, ComSafeArrayOut(BSTR, a_bstrNames), ComSafeArrayOut(BSTR, a_bstrValues))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::SetRegister(ULONG a_idCpu, IN_BSTR a_bstrName, IN_BSTR a_bstrValue)
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::SetRegisters(ULONG a_idCpu, ComSafeArrayIn(IN_BSTR, a_bstrNames), ComSafeArrayIn(IN_BSTR, a_bstrValues))
{
    ReturnComNotImplemented();
}

STDMETHODIMP MachineDebugger::DumpGuestStack(ULONG a_idCpu, BSTR *a_pbstrStack)
{
    ReturnComNotImplemented();
}

/**
 * Resets VM statistics.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 */
STDMETHODIMP MachineDebugger::ResetStats(IN_BSTR aPattern)
{
    Console::SafeVMPtrQuiet pVM (mParent);

    if (!pVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, "Machine is not running");

    STAMR3Reset(pVM, Utf8Str(aPattern).c_str());

    return S_OK;
}

/**
 * Dumps VM statistics to the log.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 */
STDMETHODIMP MachineDebugger::DumpStats (IN_BSTR aPattern)
{
    Console::SafeVMPtrQuiet pVM (mParent);

    if (!pVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, "Machine is not running");

    STAMR3Dump(pVM, Utf8Str(aPattern).c_str());

    return S_OK;
}

/**
 * Get the VM statistics in an XML format.
 *
 * @returns COM status code.
 * @param   aPattern            The selection pattern. A bit similar to filename globbing.
 * @param   aWithDescriptions   Whether to include the descriptions.
 * @param   aStats              The XML document containing the statistics.
 */
STDMETHODIMP MachineDebugger::GetStats (IN_BSTR aPattern, BOOL aWithDescriptions, BSTR *aStats)
{
    Console::SafeVMPtrQuiet pVM (mParent);

    if (!pVM.isOk())
        return setError(VBOX_E_INVALID_VM_STATE, "Machine is not running");

    char *pszSnapshot;
    int vrc = STAMR3Snapshot(pVM, Utf8Str(aPattern).c_str(), &pszSnapshot, NULL,
                             !!aWithDescriptions);
    if (RT_FAILURE(vrc))
        return vrc == VERR_NO_MEMORY ? E_OUTOFMEMORY : E_FAIL;

    /** @todo this is horribly inefficient! And it's kinda difficult to tell whether it failed...
     * Must use UTF-8 or ASCII here and completely avoid these two extra copy operations.
     * Until that's done, this method is kind of useless for debugger statistics GUI because
     * of the amount statistics in a debug build. */
    Bstr(pszSnapshot).detachTo(aStats);

    return S_OK;
}


// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

void MachineDebugger::flushQueuedSettings()
{
    mFlushMode = true;
    if (mSinglestepQueued != ~0)
    {
        COMSETTER(Singlestep) (mSinglestepQueued);
        mSinglestepQueued = ~0;
    }
    if (mRecompileUserQueued != ~0)
    {
        COMSETTER(RecompileUser) (mRecompileUserQueued);
        mRecompileUserQueued = ~0;
    }
    if (mRecompileSupervisorQueued != ~0)
    {
        COMSETTER(RecompileSupervisor) (mRecompileSupervisorQueued);
        mRecompileSupervisorQueued = ~0;
    }
    if (mPatmEnabledQueued != ~0)
    {
        COMSETTER(PATMEnabled) (mPatmEnabledQueued);
        mPatmEnabledQueued = ~0;
    }
    if (mCsamEnabledQueued != ~0)
    {
        COMSETTER(CSAMEnabled) (mCsamEnabledQueued);
        mCsamEnabledQueued = ~0;
    }
    if (mLogEnabledQueued != ~0)
    {
        COMSETTER(LogEnabled) (mLogEnabledQueued);
        mLogEnabledQueued = ~0;
    }
    if (mVirtualTimeRateQueued != ~(uint32_t)0)
    {
        COMSETTER(VirtualTimeRate) (mVirtualTimeRateQueued);
        mVirtualTimeRateQueued = ~0;
    }
    mFlushMode = false;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

bool MachineDebugger::queueSettings() const
{
    if (!mFlushMode)
    {
        // check if the machine is running
        MachineState_T machineState;
        mParent->COMGETTER(State) (&machineState);
        switch (machineState)
        {
            // queue the request
            default:
                return true;

            case MachineState_Running:
            case MachineState_Paused:
            case MachineState_Stuck:
            case MachineState_LiveSnapshotting:
            case MachineState_Teleporting:
                break;
        }
    }
    return false;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
