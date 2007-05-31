/** @file
 *
 * VirtualBox COM class implementation
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

#include "HostDVDDriveImpl.h"

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR (HostDVDDrive)

HRESULT HostDVDDrive::FinalConstruct()
{
    return S_OK;
}

void HostDVDDrive::FinalRelease()
{
    uninit();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the host DVD drive object.
 *
 * @param aName         Name of the drive.
 * @param aDescription  Human-readable drive description (may be NULL).
 *
 * @return COM result indicator.
 */
HRESULT HostDVDDrive::init (INPTR BSTR aName,
                            INPTR BSTR aUdi /* = NULL */,
                            INPTR BSTR aDescription /* = NULL */)
{
    ComAssertRet (aName, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan (this);
    AssertReturn (autoInitSpan.isOk(), E_UNEXPECTED);

    unconst (mName) = aName;
    unconst (mUdi) = aUdi;
    unconst (mDescription) = aDescription;

    /* Confirm the successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void HostDVDDrive::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan (this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst (mDescription).setNull();
    unconst (mName).setNull();
}

// IHostDVDDrive properties
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP HostDVDDrive::COMGETTER(Name) (BSTR *aName)
{
    if (!aName)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* mName is constant during life time, no need to lock */

    mName.cloneTo (aName);

    return S_OK;
}

/**
 * Returns a human readable description of the host drive
 *
 * @returns COM status code
 * @param driveDescription address of result pointer
 */
STDMETHODIMP HostDVDDrive::COMGETTER(Description) (BSTR *aDescription)
{
    if (!aDescription)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* mDescription is constant during life time, no need to lock */

    mDescription.cloneTo (aDescription);

    return S_OK;
}

/**
 * Returns the universal device identifier of the host drive
 *
 * @returns COM status code
 * @param driveDescription address of result pointer
 */
STDMETHODIMP HostDVDDrive::COMGETTER(Udi) (BSTR *aUdi)
{
    if (!aUdi)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* mDescription is constant during life time, no need to lock */

    mUdi.cloneTo (aUdi);

    return S_OK;
}
