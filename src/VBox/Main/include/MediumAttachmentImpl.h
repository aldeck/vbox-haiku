/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ____H_MEDIUMATTACHMENTIMPL
#define ____H_MEDIUMATTACHMENTIMPL

#include "VirtualBoxBase.h"

class ATL_NO_VTABLE MediumAttachment :
    public VirtualBoxBase,
    public VirtualBoxSupportTranslation<MediumAttachment>,
    VBOX_SCRIPTABLE_IMPL(IMediumAttachment)
{
public:
    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT(MediumAttachment, IMediumAttachment)

    DECLARE_NOT_AGGREGATABLE(MediumAttachment)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP(MediumAttachment)
        COM_INTERFACE_ENTRY(ISupportErrorInfo)
        COM_INTERFACE_ENTRY(IMediumAttachment)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    MediumAttachment() { };
    ~MediumAttachment() { };

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent,
                 Medium *aMedium,
                 const Bstr &aControllerName,
                 LONG aPort,
                 LONG aDevice,
                 DeviceType_T aType,
                 bool fPassthrough);
    void uninit();

    HRESULT FinalConstruct();
    void FinalRelease();

    // IMediumAttachment properties
    STDMETHOD(COMGETTER(Medium))(IMedium **aMedium);
    STDMETHOD(COMGETTER(Controller))(BSTR *aController);
    STDMETHOD(COMGETTER(Port))(LONG *aPort);
    STDMETHOD(COMGETTER(Device))(LONG *aDevice);
    STDMETHOD(COMGETTER(Type))(DeviceType_T *aType);
    STDMETHOD(COMGETTER(Passthrough))(BOOL *aPassthrough);

    // public internal methods
    void rollback();
    void commit();

    // unsafe public methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)
    bool isImplicit() const;
    void setImplicit(bool aImplicit);

    const ComObjPtr<Medium>& getMedium() const;
    Bstr getControllerName() const;
    LONG getPort() const;
    LONG getDevice() const;
    DeviceType_T getType() const;
    bool getPassthrough() const;

    bool matches(CBSTR aControllerName, LONG aPort, LONG aDevice);

    /** Must be called from under this object's write lock. */
    void updateMedium(const ComObjPtr<Medium> &aMedium, bool aImplicit);

    /** Must be called from under this object's write lock. */
    void updatePassthrough(bool aPassthrough);

    /** Get a unique and somewhat descriptive name for logging. */
    const char* getLogName(void) const { return mLogName.c_str(); }

private:
    struct Data;
    Data *m;

    Utf8Str mLogName;                   /**< For logging purposes */
};

#endif // ____H_MEDIUMATTACHMENTIMPL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
