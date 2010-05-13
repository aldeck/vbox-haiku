/** @file
 *
 * VBox Client callback COM Class implementation
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "VirtualBoxCallbackImpl.h"
#include "Logging.h"

HRESULT CallbackWrapper::FinalConstruct()
{
    return S_OK;
}

void CallbackWrapper::FinalRelease()
{
}

// public initializers/uninitializers only for internal purposes
HRESULT CallbackWrapper::init()
{
    return S_OK;
}

void CallbackWrapper::uninit(bool aFinalRelease)
{
}

// ILocalOwner methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CallbackWrapper::SetLocalObject(IUnknown *aCallback)
{
    if (aCallback == NULL)
    {
        mVBoxCallback.setNull();
        mConsoleCallback.setNull();
        return S_OK;
    }

    if (!VALID_PTR (aCallback))
        return E_POINTER;

    mVBoxCallback = aCallback;
    mConsoleCallback = aCallback;

    // or some other error code?
    AssertReturn(!!mVBoxCallback || !!mConsoleCallback, E_FAIL);

    return S_OK;
}

// IVirtualBoxCallback methods
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CallbackWrapper::OnMachineStateChange(IN_BSTR machineId, MachineState_T state)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnMachineStateChange(machineId, state);
}

STDMETHODIMP CallbackWrapper::OnMachineDataChange(IN_BSTR machineId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnMachineDataChange(machineId);
}


STDMETHODIMP CallbackWrapper::OnExtraDataCanChange(IN_BSTR machineId, IN_BSTR key, IN_BSTR value,
                                           BSTR *error, BOOL *changeAllowed)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnExtraDataCanChange(machineId, key, value, error, changeAllowed);
}

STDMETHODIMP CallbackWrapper::OnExtraDataChange(IN_BSTR machineId, IN_BSTR key, IN_BSTR value)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnExtraDataChange(machineId, key, value);
}

STDMETHODIMP CallbackWrapper::OnMediumRegistered(IN_BSTR mediaId, DeviceType_T mediaType,
                              BOOL registered)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnMediumRegistered(mediaId, mediaType, registered);
}


STDMETHODIMP CallbackWrapper::OnMachineRegistered(IN_BSTR aMachineId, BOOL registered)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnMachineRegistered(aMachineId, registered);
}

STDMETHODIMP CallbackWrapper::OnSessionStateChange(IN_BSTR aMachineId, SessionState_T state)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnSessionStateChange(aMachineId, state);
}

STDMETHODIMP CallbackWrapper::OnSnapshotTaken(IN_BSTR aMachineId, IN_BSTR aSnapshotId)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnSnapshotTaken(aMachineId, aSnapshotId);
}

STDMETHODIMP CallbackWrapper::OnSnapshotDeleted(IN_BSTR aMachineId, IN_BSTR aSnapshotId)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnSnapshotDeleted(aMachineId, aSnapshotId);
}

STDMETHODIMP CallbackWrapper::OnSnapshotChange(IN_BSTR aMachineId, IN_BSTR aSnapshotId)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnSnapshotChange(aMachineId, aSnapshotId);
}

STDMETHODIMP CallbackWrapper::OnGuestPropertyChange(IN_BSTR aMachineId, IN_BSTR key, IN_BSTR value, IN_BSTR flags)
{
    if (mVBoxCallback.isNull())
        return S_OK;

    return mVBoxCallback->OnGuestPropertyChange(aMachineId, key, value, flags);
}


// IConsoleCallback methods
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CallbackWrapper::OnMousePointerShapeChange(BOOL visible, BOOL alpha, ULONG xHot, ULONG yHot,
                                                        ULONG width, ULONG height, BYTE *shape)
{
    if (mConsoleCallback.isNull())
        return S_OK;

#if defined(RT_OS_WINDOWS) && defined(RT_ARCH_AMD64)
    /* Letting this thru crashes in VariantInit, probably because the last
       parameter is a BYTE pointer.  For now, don't let it thru to python or
       what other users this class might have. */
    /** @todo Figure if this applies to 32-bit hosts as well. */
    /** @todo Find better solution? */
    return VBOX_E_DONT_CALL_AGAIN;
#else
    return mConsoleCallback->OnMousePointerShapeChange(visible, alpha, xHot, yHot, width, height, shape);
#endif
}


STDMETHODIMP CallbackWrapper::OnMouseCapabilityChange(BOOL supportsAbsolute, BOOL supportsRelative, BOOL needsHostCursor)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnMouseCapabilityChange(supportsAbsolute, supportsRelative, needsHostCursor);
}

STDMETHODIMP  CallbackWrapper::OnKeyboardLedsChange(BOOL fNumLock, BOOL fCapsLock, BOOL fScrollLock)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnKeyboardLedsChange(fNumLock, fCapsLock, fScrollLock);
}

STDMETHODIMP CallbackWrapper::OnStateChange(MachineState_T machineState)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnStateChange(machineState);
}

STDMETHODIMP CallbackWrapper::OnAdditionsStateChange()
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnAdditionsStateChange();
}

STDMETHODIMP CallbackWrapper::OnNetworkAdapterChange(INetworkAdapter *aNetworkAdapter)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnNetworkAdapterChange(aNetworkAdapter);
}

STDMETHODIMP CallbackWrapper::OnSerialPortChange(ISerialPort *aSerialPort)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnSerialPortChange(aSerialPort);
}

STDMETHODIMP CallbackWrapper::OnParallelPortChange(IParallelPort *aParallelPort)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnParallelPortChange(aParallelPort);
}

STDMETHODIMP CallbackWrapper::OnRemoteDisplayInfoChange()
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnRemoteDisplayInfoChange();
}

STDMETHODIMP CallbackWrapper::OnVRDPServerChange()
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnVRDPServerChange();
}

STDMETHODIMP CallbackWrapper::OnUSBControllerChange()
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnUSBControllerChange();
}

STDMETHODIMP CallbackWrapper::OnUSBDeviceStateChange(IUSBDevice *aDevice,
                                                        BOOL aAttached,
                                                        IVirtualBoxErrorInfo *aError)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnUSBDeviceStateChange(aDevice, aAttached, aError);
}

STDMETHODIMP CallbackWrapper::OnSharedFolderChange(Scope_T aScope)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnSharedFolderChange(aScope);
}

STDMETHODIMP CallbackWrapper::OnStorageControllerChange()
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnStorageControllerChange();
}

STDMETHODIMP CallbackWrapper::OnMediumChange(IMediumAttachment *aMediumAttachment)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnMediumChange(aMediumAttachment);
}

STDMETHODIMP CallbackWrapper::OnCPUChange(ULONG aCPU, BOOL aRemove)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnCPUChange(aCPU, aRemove);
}

STDMETHODIMP CallbackWrapper::OnRuntimeError(BOOL fFatal, IN_BSTR id, IN_BSTR message)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnRuntimeError(fFatal, id, message);
}

STDMETHODIMP CallbackWrapper::OnCanShowWindow(BOOL *canShow)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnCanShowWindow(canShow);
}

STDMETHODIMP CallbackWrapper::OnShowWindow(ULONG64 *winId)
{
    if (mConsoleCallback.isNull())
        return S_OK;

    return mConsoleCallback->OnShowWindow(winId);
}
