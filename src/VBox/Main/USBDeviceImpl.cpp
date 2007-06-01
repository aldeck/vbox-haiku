/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
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

#include "USBDeviceImpl.h"


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

OUSBDevice::OUSBDevice()
{
    mVendorId = 0;
    mProductId = 0;
    mRevision = 0;

    mPort = 0;
    mRemote = FALSE;
}

OUSBDevice::~OUSBDevice()
{
}


// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the USB device object.
 *
 * @returns COM result indicator
 * @param   aUSBDevice    The USB device (interface) to clone.
 */
HRESULT OUSBDevice::init(IUSBDevice *aUSBDevice)
{
    AutoLock lock(this);
    AssertReturn (!isReady(), E_UNEXPECTED);

    HRESULT hrc = aUSBDevice->COMGETTER(VendorId)(&mVendorId);
    ComAssertComRCRet (hrc, hrc);
    ComAssertRet (mVendorId, E_INVALIDARG);

    hrc = aUSBDevice->COMGETTER(ProductId)(&mProductId);
    ComAssertComRCRet (hrc, hrc);
    ComAssertRet (mProductId, E_INVALIDARG);

    hrc = aUSBDevice->COMGETTER(Revision)(&mRevision);
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Manufacturer)(mManufacturer.asOutParam());
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Product)(mProduct.asOutParam());
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(SerialNumber)(mSerialNumber.asOutParam());
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Address)(mAddress.asOutParam());
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Port)(&mPort);
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Remote)(&mRemote);
    ComAssertComRCRet (hrc, hrc);

    hrc = aUSBDevice->COMGETTER(Id)(mId.asOutParam());
    ComAssertComRCRet (hrc, hrc);

    setReady(true);
    return S_OK;
}


// IUSBDevice properties
/////////////////////////////////////////////////////////////////////////////

/**
 * Returns the GUID.
 *
 * @returns COM status code
 * @param   aId   Address of result variable.
 */
STDMETHODIMP OUSBDevice::COMGETTER(Id)(GUIDPARAMOUT aId)
{
    if (!aId)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    mId.cloneTo(aId);
    return S_OK;
}


/**
 * Returns the vendor Id.
 *
 * @returns COM status code
 * @param   aVendorId   Where to store the vendor id.
 */
STDMETHODIMP OUSBDevice::COMGETTER(VendorId)(USHORT *aVendorId)
{
    if (!aVendorId)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    *aVendorId = mVendorId;
    return S_OK;
}


/**
 * Returns the product Id.
 *
 * @returns COM status code
 * @param   aProductId  Where to store the product id.
 */
STDMETHODIMP OUSBDevice::COMGETTER(ProductId)(USHORT *aProductId)
{
    if (!aProductId)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    *aProductId = mProductId;
    return S_OK;
}


/**
 * Returns the revision BCD.
 *
 * @returns COM status code
 * @param   aRevision  Where to store the revision BCD.
 */
STDMETHODIMP OUSBDevice::COMGETTER(Revision)(USHORT *aRevision)
{
    if (!aRevision)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    *aRevision = mRevision;
    return S_OK;
}

/**
 * Returns the manufacturer string.
 *
 * @returns COM status code
 * @param   aManufacturer     Where to put the return string.
 */
STDMETHODIMP OUSBDevice::COMGETTER(Manufacturer)(BSTR *aManufacturer)
{
    if (!aManufacturer)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    mManufacturer.cloneTo(aManufacturer);
    return S_OK;
}


/**
 * Returns the product string.
 *
 * @returns COM status code
 * @param   aProduct          Where to put the return string.
 */
STDMETHODIMP OUSBDevice::COMGETTER(Product)(BSTR *aProduct)
{
    if (!aProduct)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    mProduct.cloneTo(aProduct);
    return S_OK;
}


/**
 * Returns the serial number string.
 *
 * @returns COM status code
 * @param   aSerialNumber     Where to put the return string.
 */
STDMETHODIMP OUSBDevice::COMGETTER(SerialNumber)(BSTR *aSerialNumber)
{
    if (!aSerialNumber)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    mSerialNumber.cloneTo(aSerialNumber);
    return S_OK;
}


/**
 * Returns the host specific device address.
 *
 * @returns COM status code
 * @param   aAddress          Where to put the return string.
 */
STDMETHODIMP OUSBDevice::COMGETTER(Address)(BSTR *aAddress)
{
    if (!aAddress)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    mAddress.cloneTo(aAddress);
    return S_OK;
}

STDMETHODIMP OUSBDevice::COMGETTER(Port)(USHORT *aPort)
{
    if (!aPort)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    *aPort = mPort;
    return S_OK;
}

STDMETHODIMP OUSBDevice::COMGETTER(Remote)(BOOL *aRemote)
{
    if (!aRemote)
        return E_POINTER;

    AutoLock lock(this);
    CHECK_READY();

    *aRemote = mRemote;
    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

