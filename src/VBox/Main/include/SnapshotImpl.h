/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2010 Sun Microsystems, Inc.
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

#ifndef ____H_SNAPSHOTIMPL
#define ____H_SNAPSHOTIMPL

#include "VirtualBoxBase.h"

#include <iprt/time.h>

class SnapshotMachine;

namespace settings
{
    struct Snapshot;
}

class ATL_NO_VTABLE Snapshot :
    public VirtualBoxSupportErrorInfoImpl<Snapshot, ISnapshot>,
    public VirtualBoxSupportTranslation<Snapshot>,
    public VirtualBoxBase, // WithTypedChildren<Snapshot>,
    VBOX_SCRIPTABLE_IMPL(ISnapshot)
{
public:
    DECLARE_NOT_AGGREGATABLE(Snapshot)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(Snapshot)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (ISnapshot)
        COM_INTERFACE_ENTRY2 (IDispatch, ISnapshot)
    END_COM_MAP()

    Snapshot()
        : m(NULL)
    { };
    ~Snapshot()
    { };

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer only for internal purposes
    HRESULT init(VirtualBox *aVirtualBox,
                 const Guid &aId,
                 const Utf8Str &aName,
                 const Utf8Str &aDescription,
                 const RTTIMESPEC &aTimeStamp,
                 SnapshotMachine *aMachine,
                 Snapshot *aParent);
    void uninit();

    void beginSnapshotDelete();

    void deparent();

    // ISnapshot properties
    STDMETHOD(COMGETTER(Id)) (BSTR *aId);
    STDMETHOD(COMGETTER(Name)) (BSTR *aName);
    STDMETHOD(COMSETTER(Name)) (IN_BSTR aName);
    STDMETHOD(COMGETTER(Description)) (BSTR *aDescription);
    STDMETHOD(COMSETTER(Description)) (IN_BSTR aDescription);
    STDMETHOD(COMGETTER(TimeStamp)) (LONG64 *aTimeStamp);
    STDMETHOD(COMGETTER(Online)) (BOOL *aOnline);
    STDMETHOD(COMGETTER(Machine)) (IMachine **aMachine);
    STDMETHOD(COMGETTER(Parent)) (ISnapshot **aParent);
    STDMETHOD(COMGETTER(Children)) (ComSafeArrayOut (ISnapshot *, aChildren));

    // ISnapshot methods

    // public methods only for internal purposes

    /**
     * Simple run-time type identification without having to enable C++ RTTI.
     * The class IDs are defined in VirtualBoxBase.h.
     * @return
     */
    virtual VBoxClsID getClassID() const
    {
        return clsidSnapshot;
    }

    /**
     * Override of the default locking class to be used for validating lock
     * order with the standard member lock handle.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        return LOCKCLASS_SNAPSHOTOBJECT;
    }

    const ComObjPtr<Snapshot>& getParent() const;

    const Utf8Str& stateFilePath() const;
    HRESULT deleteStateFile();

    ULONG getChildrenCount();
    ULONG getAllChildrenCount();
    ULONG getAllChildrenCountImpl();

    const ComObjPtr<SnapshotMachine>& getSnapshotMachine() const;

    Guid getId() const;
    const Utf8Str& getName() const;
    RTTIMESPEC getTimeStamp() const;

    ComObjPtr<Snapshot> findChildOrSelf(IN_GUID aId);
    ComObjPtr<Snapshot> findChildOrSelf(const Utf8Str &aName);

    void updateSavedStatePaths(const char *aOldPath,
                               const char *aNewPath);
    void updateSavedStatePathsImpl(const char *aOldPath,
                                   const char *aNewPath);

    HRESULT saveSnapshot(settings::Snapshot &data, bool aAttrsOnly);
    HRESULT saveSnapshotImpl(settings::Snapshot &data, bool aAttrsOnly);

    // for VirtualBoxSupportErrorInfoImpl
    static const wchar_t *getComponentName()
    {
        return L"Snapshot";
    }

private:
    struct Data;            // opaque, defined in SnapshotImpl.cpp
    Data *m;
};

#endif // ____H_SNAPSHOTIMPL

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
