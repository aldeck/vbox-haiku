/** @file
 *
 * VirtualBox COM class implementation
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ____H_MACHINEDEBUGGER
#define ____H_MACHINEDEBUGGER

#include "VirtualBoxBase.h"

class Console;

class ATL_NO_VTABLE MachineDebugger :
    public VirtualBoxSupportErrorInfoImpl <MachineDebugger, IMachineDebugger>,
    public VirtualBoxSupportTranslation <MachineDebugger>,
    public VirtualBoxBase,
    public IMachineDebugger
{
public:

    DECLARE_NOT_AGGREGATABLE(MachineDebugger)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(MachineDebugger)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMachineDebugger)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (Console *parent);
    void uninit();

    // IMachineDebugger properties
    STDMETHOD(COMGETTER(Singlestep))(BOOL *enabled);
    STDMETHOD(COMSETTER(Singlestep))(BOOL enable);
    STDMETHOD(COMGETTER(RecompileUser))(BOOL *enabled);
    STDMETHOD(COMSETTER(RecompileUser))(BOOL enable);
    STDMETHOD(COMGETTER(RecompileSupervisor))(BOOL *enabled);
    STDMETHOD(COMSETTER(RecompileSupervisor))(BOOL enable);
    STDMETHOD(COMGETTER(PATMEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(PATMEnabled))(BOOL enable);
    STDMETHOD(COMGETTER(CSAMEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(CSAMEnabled))(BOOL enable);
    STDMETHOD(COMGETTER(LogEnabled))(BOOL *enabled);
    STDMETHOD(COMSETTER(LogEnabled))(BOOL enable);
    STDMETHOD(COMGETTER(HWVirtExEnabled))(BOOL *enabled);
    STDMETHOD(COMGETTER(HWVirtExNestedPagingEnabled))(BOOL *enabled);
    STDMETHOD(COMGETTER(HWVirtExVPIDEnabled))(BOOL *enabled);
    STDMETHOD(COMGETTER(PAEEnabled))(BOOL *enabled);
    STDMETHOD(COMGETTER(VirtualTimeRate))(ULONG *pct);
    STDMETHOD(COMSETTER(VirtualTimeRate))(ULONG pct);
    STDMETHOD(COMGETTER(VM))(ULONG64 *vm);
    STDMETHOD(InjectNMI)();

    // IMachineDebugger methods
    STDMETHOD(ResetStats(INPTR BSTR aPattern));
    STDMETHOD(DumpStats(INPTR BSTR aPattern));
    STDMETHOD(GetStats(INPTR BSTR aPattern, BOOL aWithDescriptions, BSTR *aStats));


    // "public-private methods"
    void flushQueuedSettings();

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName() { return L"MachineDebugger"; }

private:
    ComObjPtr <Console, ComWeakRef> mParent;
    // flags whether settings have been queued because
    // they could not be sent to the VM (not up yet, etc.)
    int singlestepQueued;
    int recompileUserQueued;
    int recompileSupervisorQueued;
    int patmEnabledQueued;
    int csamEnabledQueued;
    int mLogEnabledQueued;
    uint32_t mVirtualTimeRateQueued;
    bool fFlushMode;
};

#endif // ____H_MACHINEDEBUGGER
