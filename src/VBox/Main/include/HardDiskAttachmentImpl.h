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

#ifndef ____H_HARDDISKATTACHMENTIMPL
#define ____H_HARDDISKATTACHMENTIMPL

#include "VirtualBoxBase.h"

#include "HardDisk2Impl.h"

class ATL_NO_VTABLE HardDisk2Attachment :
    public VirtualBoxBaseNEXT,
    public com::SupportErrorInfoImpl <HardDisk2Attachment, IHardDisk2Attachment>,
    public VirtualBoxSupportTranslation <HardDisk2Attachment>,
    public IHardDisk2Attachment
{
public:

    /** Equality predicate for stdc++. */
    struct EqualsTo
        : public std::unary_function <ComObjPtr <HardDisk2Attachment>, bool>
    {
        explicit EqualsTo (StorageBus_T aBus, LONG aChannel, LONG aDevice)
            : bus (aBus), channel (aChannel), device (aDevice) {}

        bool operator() (const argument_type &aThat) const
        {
            return aThat->bus() == bus && aThat->channel() == channel &&
                   aThat->device() == device;
        }

        const StorageBus_T bus;
        const LONG channel;
        const LONG device;
    };

    /** Hard disk reference predicate for stdc++. */
    struct RefersTo
        : public std::unary_function <ComObjPtr <HardDisk2Attachment>, bool>
    {
        explicit RefersTo (HardDisk2 *aHardDisk) : hardDisk (aHardDisk) {}

        bool operator() (const argument_type &aThat) const
        {
            return aThat->hardDisk().equalsTo (hardDisk);
        }

        const ComObjPtr <HardDisk2> hardDisk;
    };

    DECLARE_NOT_AGGREGATABLE(HardDisk2Attachment)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP (HardDisk2Attachment)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IHardDisk2Attachment)
    END_COM_MAP()

    NS_DECL_ISUPPORTS

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init (HardDisk2 *aHD, StorageBus_T aBus, LONG aChannel,
                  LONG aDevice, bool aImplicit = false);
    void uninit();

    // IHardDisk2Attachment properties
    STDMETHOD(COMGETTER(HardDisk))   (IHardDisk2 **aHardDisk);
    STDMETHOD(COMGETTER(Bus))        (StorageBus_T *aBus);
    STDMETHOD(COMGETTER(Channel))    (LONG *aChannel);
    STDMETHOD(COMGETTER(Device))     (LONG *aDevice);

    // unsafe inline public methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)

    bool isImplicit() const { return m.implicit; }
    void setImplicit (bool aImplicit) { m.implicit = aImplicit; }

    const ComObjPtr <HardDisk2> &hardDisk() const { return m.hardDisk; }
    StorageBus_T bus() const { return m.bus; }
    LONG channel() const { return m.channel; }
    LONG device() const { return m.device; }

    /** Must be called from under this object's write lock.  */
    void updateHardDisk (const ComObjPtr <HardDisk2> &aHardDisk, bool aImplicit)
    {
        m.hardDisk = aHardDisk;
        m.implicit = aImplicit;
    }

    /** For com::SupportErrorInfoImpl. */
    static const char *ComponentName() { return "HardDisk2Attachment"; }

private:

    struct Data
    {
        Data() : bus (StorageBus_Null), channel (0), device (0)
               , implicit (false) {}

        /// @todo NEWMEDIA shouldn't it be constant too? It'd be nice to get
        /// rid of locks at all in this simple readonly structure-like interface
        ComObjPtr <HardDisk2> hardDisk;
        const StorageBus_T bus;
        const LONG channel;
        const LONG device;

        bool implicit : 1;
    };

    Data m;
};

#endif // ____H_HARDDISKATTACHMENTIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
