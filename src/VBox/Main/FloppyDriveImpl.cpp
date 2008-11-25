/* $Id$ */

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

#include "FloppyDriveImpl.h"

#include "MachineImpl.h"
#include "HostImpl.h"
#include "HostFloppyDriveImpl.h"
#include "VirtualBoxImpl.h"

#include "Logging.h"

#include <iprt/string.h>
#include <iprt/cpputils.h>

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR (FloppyDrive)

HRESULT FloppyDrive::FinalConstruct()
{
    return S_OK;
}

void FloppyDrive::FinalRelease()
{
    uninit();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the Floppy drive object.
 *
 *  @param aParent  Handle of the parent object.
 */
HRESULT FloppyDrive::init (Machine *aParent)
{
    LogFlowThisFunc (("aParent=%p\n", aParent));

    ComAssertRet (aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan (this);
    AssertReturn (autoInitSpan.isOk(), E_FAIL);

    unconst (mParent) = aParent;
    /* mPeer is left null */

    mData.allocate();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the Floppy drive object given another Floppy drive object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT FloppyDrive::init (Machine *aParent, FloppyDrive *aThat)
{
    LogFlowThisFunc (("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet (aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan (this);
    AssertReturn (autoInitSpan.isOk(), E_FAIL);

    unconst (mParent) = aParent;
    unconst (mPeer) = aThat;

    AutoCaller thatCaller (aThat);
    AssertComRCReturnRC (thatCaller.rc());

    AutoReadLock thatLock (aThat);
    mData.share (aThat->mData);

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 *
 *  @note Locks @a aThat object for reading.
 */
HRESULT FloppyDrive::initCopy (Machine *aParent, FloppyDrive *aThat)
{
    LogFlowThisFunc (("aParent=%p, aThat=%p\n", aParent, aThat));

    ComAssertRet (aParent && aThat, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan (this);
    AssertReturn (autoInitSpan.isOk(), E_FAIL);

    unconst (mParent) = aParent;
    /* mPeer is left null */

    AutoCaller thatCaller (aThat);
    AssertComRCReturnRC (thatCaller.rc());

    AutoReadLock thatLock (aThat);
    mData.attachCopy (aThat->mData);

    /* at present, this must be a snapshot machine */
    Assert (!aParent->snapshotId().isEmpty());

    if (mData->mState == DriveState_ImageMounted)
    {
        /* associate the DVD image media with the snapshot */
        HRESULT rc = mData->mImage->attachTo (aParent->id(),
                                              aParent->snapshotId());
        AssertComRC (rc);
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void FloppyDrive::uninit()
{
    LogFlowThisFunc (("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan (this);
    if (autoUninitSpan.uninitDone())
        return;

    if ((mParent->type() == Machine::IsMachine ||
         mParent->type() == Machine::IsSnapshotMachine) &&
        mData->mState == DriveState_ImageMounted)
    {
        /* Deassociate the DVD image (only when mParent is a real Machine or a
         * SnapshotMachine instance; SessionMachine instances
         * refer to real Machine hard disks). This is necessary for a clean
         * re-initialization of the VM after successfully re-checking the
         * accessibility state. */
        HRESULT rc = mData->mImage->detachFrom (mParent->id(),
                                                mParent->snapshotId());
        AssertComRC (rc);
    }

    mData.free();

    unconst (mPeer).setNull();
    unconst (mParent).setNull();
}

// IFloppyDrive properties
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP FloppyDrive::COMGETTER(Enabled) (BOOL *aEnabled)
{
    if (!aEnabled)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    *aEnabled = mData->mEnabled;

    return S_OK;
}

STDMETHODIMP FloppyDrive::COMSETTER(Enabled) (BOOL aEnabled)
{
    LogFlowThisFunc (("aEnabled=%RTbool\n", aEnabled));

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* the machine needs to be mutable */
    Machine::AutoMutableStateDependency adep (mParent);
    CheckComRCReturnRC (adep.rc());

    AutoWriteLock alock (this);

    if (mData->mEnabled != aEnabled)
    {
        mData.backup();
        mData->mEnabled = aEnabled;

        /* leave the lock before informing callbacks */
        alock.unlock();

        mParent->onFloppyDriveChange();
    }

    return S_OK;
}

STDMETHODIMP FloppyDrive::COMGETTER(State) (DriveState_T *aState)
{
    if (!aState)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    *aState = mData->mState;

    return S_OK;
}

// IFloppyDrive methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP FloppyDrive::MountImage (INPTR GUIDPARAM aImageId)
{
    Guid imageId = aImageId;
    if (imageId.isEmpty())
        return E_INVALIDARG;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* the machine needs to be mutable */
    Machine::AutoMutableStateDependency adep (mParent);
    CheckComRCReturnRC (adep.rc());

    AutoWriteLock alock (this);

    HRESULT rc = E_FAIL;

    /* Our lifetime is bound to mParent's lifetime, so we don't add caller.
     * We also don't lock mParent since its mParent field is const. */

    ComObjPtr <FloppyImage2> image;
    rc = mParent->virtualBox()->findFloppyImage2 (&imageId, NULL,
                                                  true /* aSetError */, &image);

    if (SUCCEEDED (rc))
    {
        if (mData->mState != DriveState_ImageMounted ||
            !mData->mImage.equalsTo (image))
        {
            rc = image->attachTo (mParent->id(), mParent->snapshotId());
            if (SUCCEEDED (rc))
            {
                mData.backup();

                unmount();

                mData->mImage = image;
                mData->mState = DriveState_ImageMounted;

                /* leave the lock before informing callbacks */
                alock.unlock();

                mParent->onFloppyDriveChange();
            }
        }
    }

    return rc;
}

STDMETHODIMP FloppyDrive::CaptureHostDrive (IHostFloppyDrive *aHostFloppyDrive)
{
    if (!aHostFloppyDrive)
        return E_INVALIDARG;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* the machine needs to be mutable */
    Machine::AutoMutableStateDependency adep (mParent);
    CheckComRCReturnRC (adep.rc());

    AutoWriteLock alock (this);

    if (mData->mState != DriveState_HostDriveCaptured ||
        !mData->mHostDrive.equalsTo (aHostFloppyDrive))
    {
        mData.backup();

        unmount();

        mData->mHostDrive = aHostFloppyDrive;
        mData->mState = DriveState_HostDriveCaptured;

        /* leave the lock before informing callbacks */
        alock.unlock();

        mParent->onFloppyDriveChange();
    }

    return S_OK;
}

STDMETHODIMP FloppyDrive::Unmount()
{
    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    /* the machine needs to be mutable */
    Machine::AutoMutableStateDependency adep (mParent);
    CheckComRCReturnRC (adep.rc());

    AutoWriteLock alock (this);

    if (mData->mState != DriveState_NotMounted)
    {
        mData.backup();

        unmount();

        mData->mState = DriveState_NotMounted;

        /* leave the lock before informing callbacks */
        alock.unlock();

        mParent->onFloppyDriveChange();
    }

    return S_OK;
}

STDMETHODIMP FloppyDrive::GetImage (IFloppyImage2 **aFloppyImage)
{
    if (!aFloppyImage)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    mData->mImage.queryInterfaceTo (aFloppyImage);

    return S_OK;
}

STDMETHODIMP FloppyDrive::GetHostDrive (IHostFloppyDrive **aHostDrive)
{
    if (!aHostDrive)
        return E_POINTER;

    AutoCaller autoCaller (this);
    CheckComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    mData->mHostDrive.queryInterfaceTo (aHostDrive);

    return S_OK;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 *  Loads settings from the given machine node.
 *  May be called once right after this object creation.
 *
 *  @param aMachineNode <Machine> node.
 *
 *  @note Locks this object for writing.
 */
HRESULT FloppyDrive::loadSettings (const settings::Key &aMachineNode)
{
    using namespace settings;

    AssertReturn (!aMachineNode.isNull(), E_FAIL);

    AutoCaller autoCaller (this);
    AssertComRCReturnRC (autoCaller.rc());

    AutoWriteLock alock (this);

    /* Note: we assume that the default values for attributes of optional
     * nodes are assigned in the Data::Data() constructor and don't do it
     * here. It implies that this method may only be called after constructing
     * a new BIOSSettings object while all its data fields are in the default
     * values. Exceptions are fields whose creation time defaults don't match
     * values that should be applied when these fields are not explicitly set
     * in the settings file (for backwards compatibility reasons). This takes
     * place when a setting of a newly created object must default to A while
     * the same setting of an object loaded from the old settings file must
     * default to B. */

    HRESULT rc = S_OK;

    /* Floppy drive (required, contains either Image or HostDrive or nothing) */
    Key floppyDriveNode = aMachineNode.key ("FloppyDrive");

    /* optional, defaults to true */
    mData->mEnabled = floppyDriveNode.value <bool> ("enabled");

    Key typeNode;

    if (!(typeNode = floppyDriveNode.findKey ("Image")).isNull())
    {
        Guid uuid = typeNode.value <Guid> ("uuid");
        rc = MountImage (uuid);
        CheckComRCReturnRC (rc);
    }
    else if (!(typeNode = floppyDriveNode.findKey ("HostDrive")).isNull())
    {

        Bstr src = typeNode.stringValue ("src");

        /* find the correspoding object */
        ComObjPtr <Host> host = mParent->virtualBox()->host();

        ComPtr <IHostFloppyDriveCollection> coll;
        rc = host->COMGETTER(FloppyDrives) (coll.asOutParam());
        AssertComRC (rc);

        ComPtr <IHostFloppyDrive> drive;
        rc = coll->FindByName (src, drive.asOutParam());
        if (SUCCEEDED (rc))
        {
            rc = CaptureHostDrive (drive);
            CheckComRCReturnRC (rc);
        }
        else if (rc == E_INVALIDARG)
        {
            /* the host DVD drive is not currently available. we
             * assume it will be available later and create an
             * extra object now */
            ComObjPtr <HostFloppyDrive> hostDrive;
            hostDrive.createObject();
            rc = hostDrive->init (src);
            AssertComRC (rc);
            rc = CaptureHostDrive (hostDrive);
            CheckComRCReturnRC (rc);
        }
        else
            AssertComRC (rc);
    }

    return S_OK;
}

/**
 *  Saves settings to the given machine node.
 *
 *  @param aMachineNode <Machine> node.
 *
 *  @note Locks this object for reading.
 */
HRESULT FloppyDrive::saveSettings (settings::Key &aMachineNode)
{
    using namespace settings;

    AssertReturn (!aMachineNode.isNull(), E_FAIL);

    AutoCaller autoCaller (this);
    AssertComRCReturnRC (autoCaller.rc());

    AutoReadLock alock (this);

    Key node = aMachineNode.createKey ("FloppyDrive");

    node.setValue <bool> ("enabled", !!mData->mEnabled);

    switch (mData->mState)
    {
        case DriveState_ImageMounted:
        {
            Assert (!mData->mImage.isNull());

            Guid id;
            HRESULT rc = mData->mImage->COMGETTER(Id) (id.asOutParam());
            AssertComRC (rc);
            Assert (!id.isEmpty());

            Key imageNode = node.createKey ("Image");
            imageNode.setValue <Guid> ("uuid", id);
            break;
        }
        case DriveState_HostDriveCaptured:
        {
            Assert (!mData->mHostDrive.isNull());

            Bstr name;
            HRESULT  rc = mData->mHostDrive->COMGETTER(Name) (name.asOutParam());
            AssertComRC (rc);
            Assert (!name.isEmpty());

            Key hostDriveNode = node.createKey ("HostDrive");
            hostDriveNode.setValue <Bstr> ("src", name);
            break;
        }
        case DriveState_NotMounted:
            /* do nothing, i.e.leave the drive node empty */
            break;
        default:
            ComAssertMsgFailedRet (("Invalid drive state: %d\n", mData->mState),
                                    E_FAIL);
    }

    return S_OK;
}

/**
 *  @note Locks this object for writing.
 */
bool FloppyDrive::rollback()
{
    /* sanity */
    AutoCaller autoCaller (this);
    AssertComRCReturn (autoCaller.rc(), false);

    AutoWriteLock alock (this);

    bool changed = false;

    if (mData.isBackedUp())
    {
        /* we need to check all data to see whether anything will be changed
         * after rollback */
        changed = mData.hasActualChanges();

        if (changed)
        {
            Data *oldData = mData.backedUpData();

            if (!mData->mImage.isNull() &&
                !oldData->mImage.equalsTo (mData->mImage))
            {
                /* detach the current image that will go away after rollback */
                mData->mImage->detachFrom (mParent->id(), mParent->snapshotId());
            }
        }

        mData.rollback();
    }

    return changed;
}

/**
 *  @note Locks this object for writing, together with the peer object (also
 *  for writing) if there is one.
 */
void FloppyDrive::commit()
{
    /* sanity */
    AutoCaller autoCaller (this);
    AssertComRCReturnVoid (autoCaller.rc());

    /* sanity too */
    AutoCaller peerCaller (mPeer);
    AssertComRCReturnVoid (peerCaller.rc());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock (mPeer, this);

    if (mData.isBackedUp())
    {
        Data *oldData = mData.backedUpData();

        if (!oldData->mImage.isNull() &&
            !oldData->mImage.equalsTo (mData->mImage))
        {
            /* detach the old image that will go away after commit */
            oldData->mImage->detachFrom (mParent->id(), mParent->snapshotId());
        }

        mData.commit();
        if (mPeer)
        {
            /* attach new data to the peer and reshare it */
            mPeer->mData.attach (mData);
        }
    }
}

/**
 *  @note Locks this object for writing, together with the peer object (locked
 *  for reading) if there is one.
 */
void FloppyDrive::copyFrom (FloppyDrive *aThat)
{
    /* sanity */
    AutoCaller autoCaller (this);
    AssertComRCReturnVoid (autoCaller.rc());

    /* sanity too */
    AutoCaller thatCaller (aThat);
    AssertComRCReturnVoid (thatCaller.rc());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoMultiLock2 alock (aThat->rlock(), this->wlock());

    /* this will back up current data */
    mData.assignCopy (aThat->mData);
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 *  Helper to unmount a drive.
 *
 *  @return COM status code
 */
HRESULT FloppyDrive::unmount()
{
    AssertReturn (isWriteLockOnCurrentThread(), E_FAIL);

    if (mData->mImage)
        mData->mImage.setNull();
    if (mData->mHostDrive)
        mData->mHostDrive.setNull();

    return S_OK;
}
