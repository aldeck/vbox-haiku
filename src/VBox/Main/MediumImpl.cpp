/* $Id$ */

/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2008-2009 Sun Microsystems, Inc.
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

#include "MediumImpl.h"
#include "ProgressImpl.h"
#include "SystemPropertiesImpl.h"
#include "VirtualBoxImpl.h"

#include "Logging.h"

#include <VBox/com/array.h>
#include <VBox/com/SupportErrorInfo.h>

#include <VBox/err.h>
#include <VBox/settings.h>

#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/file.h>
#include <iprt/tcp.h>

#include <VBox/VBoxHDD.h>

#include <algorithm>

////////////////////////////////////////////////////////////////////////////////
//
// Medium data definition
//
////////////////////////////////////////////////////////////////////////////////

/** Describes how a machine refers to this image. */
struct BackRef
{
    /** Equality predicate for stdc++. */
    struct EqualsTo : public std::unary_function <BackRef, bool>
    {
        explicit EqualsTo(const Guid &aMachineId) : machineId(aMachineId) {}

        bool operator()(const argument_type &aThat) const
        {
            return aThat.machineId == machineId;
        }

        const Guid machineId;
    };

    typedef std::list<Guid> GuidList;

    BackRef(const Guid &aMachineId,
            const Guid &aSnapshotId = Guid::Empty)
        : machineId(aMachineId),
          fInCurState(aSnapshotId.isEmpty())
    {
        if (!aSnapshotId.isEmpty())
            llSnapshotIds.push_back(aSnapshotId);
    }

    Guid machineId;
    bool fInCurState : 1;
    GuidList llSnapshotIds;
};

typedef std::list<BackRef> BackRefList;

struct Medium::Data
{
    Data()
        : state(MediumState_NotCreated),
          size(0),
          readers(0),
          preLockState(MediumState_NotCreated),
          queryInfoSem(NIL_RTSEMEVENTMULTI),
          queryInfoRunning(false),
          type(MediumType_Normal),
          devType(DeviceType_HardDisk),
          logicalSize(0),
          hddOpenMode(OpenReadWrite),
          autoReset(false),
          setImageId(false),
          setParentId(false),
          hostDrive(FALSE),
          implicit(false),
          numCreateDiffTasks(0),
          vdDiskIfaces(NULL)
    {}

    /** weak VirtualBox parent */
    const ComObjPtr<VirtualBox, ComWeakRef> pVirtualBox;

    const Guid id;
    Utf8Str strDescription;
    MediumState_T state;
    Utf8Str strLocation;
    Utf8Str strLocationFull;
    uint64_t size;
    Utf8Str strLastAccessError;

    // pParent and llChildren are protected by VirtualBox::hardDiskTreeLockHandle()
    ComObjPtr<Medium> pParent;
    MediaList llChildren;           // to add a child, just call push_back; to remove a child, call child->deparent() which does a lookup

    BackRefList backRefs;

    size_t readers;
    MediumState_T preLockState;

    RTSEMEVENTMULTI queryInfoSem;
    bool queryInfoRunning : 1;

    const Utf8Str strFormat;
    ComObjPtr<MediumFormat> formatObj;

    MediumType_T type;
    DeviceType_T devType;
    uint64_t logicalSize;   /*< In MBytes. */

    HDDOpenMode hddOpenMode;

    BOOL autoReset : 1;

    /** the following members are invalid after changing UUID on open */
    BOOL setImageId : 1;
    BOOL setParentId : 1;
    const Guid imageId;
    const Guid parentId;

    BOOL hostDrive : 1;

    typedef std::map <Bstr, Bstr> PropertyMap;
    PropertyMap properties;

    bool implicit : 1;

    uint32_t numCreateDiffTasks;

    Utf8Str vdError;        /*< Error remembered by the VD error callback. */

    VDINTERFACE vdIfError;
    VDINTERFACEERROR vdIfCallsError;

    VDINTERFACE vdIfConfig;
    VDINTERFACECONFIG vdIfCallsConfig;

    VDINTERFACE vdIfTcpNet;
    VDINTERFACETCPNET vdIfCallsTcpNet;

    PVDINTERFACE vdDiskIfaces;
};

////////////////////////////////////////////////////////////////////////////////
//
// Globals
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Asynchronous task thread parameter bucket.
 *
 * Note that instances of this class must be created using new() because the
 * task thread function will delete them when the task is complete!
 *
 * @note The constructor of this class adds a caller on the managed Medium
 *       object which is automatically released upon destruction.
 */
struct Medium::Task : public com::SupportErrorInfoBase
{
    enum Operation { CreateBase,
                     CreateDiff,
                     Merge,
                     Clone,
                     Delete,
                     Reset,
                     Compact
                   };

    Medium *that;
    VirtualBoxBaseProto::AutoCaller m_autoCaller;

    ComObjPtr<Progress> m_pProgress;
    Operation m_operation;

    /** Where to save the result when executed using #runNow(). */
    HRESULT m_rc;

    Task(Medium *aThat,
         Progress *aProgress,
         Operation aOperation)
        : that(aThat),
          m_autoCaller(aThat),
          m_pProgress(aProgress),
          m_operation(aOperation),
          m_rc(S_OK)
    {}

    ~Task();

    void setData(Medium *aTarget)
    {
        d.target = aTarget;
        HRESULT rc = d.target->addCaller();
        AssertComRC(rc);
    }

    void setData(Medium *aTarget, Medium *aParent)
    {
        d.target = aTarget;
        HRESULT rc = d.target->addCaller();
        AssertComRC(rc);
        d.parentDisk = aParent;
        if (aParent)
        {
            rc = d.parentDisk->addCaller();
            AssertComRC(rc);
        }
    }

    void setData(MergeChain *aChain)
    {
        AssertReturnVoid(aChain != NULL);
        d.chain.reset(aChain);
    }

    void setData(ImageChain *aSrcChain, ImageChain *aParentChain)
    {
        AssertReturnVoid(aSrcChain != NULL);
        AssertReturnVoid(aParentChain != NULL);
        d.source.reset(aSrcChain);
        d.parent.reset(aParentChain);
    }

    void setData(ImageChain *aImgChain)
    {
        AssertReturnVoid(aImgChain != NULL);
        d.images.reset(aImgChain);
    }

    HRESULT startThread();
    HRESULT runNow();

    struct Data
    {
        Data() : size(0) {}

        /* CreateBase */

        uint64_t size;

        /* CreateBase, CreateDiff, Clone */

        MediumVariant_T variant;

        /* CreateDiff, Clone */

        ComObjPtr<Medium> target;

        /* Clone */

        /** Media to open, in {parent,child} order */
        std::auto_ptr <ImageChain> source;
        /** Media which are parent of target, in {parent,child} order */
        std::auto_ptr <ImageChain> parent;
        /** The to-be parent medium object */
        ComObjPtr<Medium> parentDisk;

        /* Merge */

        /** Media to merge, in {parent,child} order */
        std::auto_ptr <MergeChain> chain;

        /* Compact */

        /** Media to open, in {parent,child} order */
        std::auto_ptr <ImageChain> images;
    }
    d;

protected:

    // SupportErrorInfoBase interface
    const GUID &mainInterfaceID() const { return COM_IIDOF(IMedium); }
    const char *componentName() const { return Medium::ComponentName(); }
};

Medium::Task::~Task()
{
    /* remove callers added by setData() */
    if (!d.target.isNull())
        d.target->releaseCaller();
}

/**
 * Starts a new thread driven by the Medium::taskThread() function and passes
 * this Task instance as an argument.
 *
 * Note that if this method returns success, this Task object becomes an ownee
 * of the started thread and will be automatically deleted when the thread
 * terminates.
 *
 * @note When the task is executed by this method, IProgress::notifyComplete()
 *       is automatically called for the progress object associated with this
 *       task when the task is finished to signal the operation completion for
 *       other threads asynchronously waiting for it.
 */
HRESULT Medium::Task::startThread()
{
    int vrc = RTThreadCreate(NULL, Medium::taskThread, this,
                             0, RTTHREADTYPE_MAIN_HEAVY_WORKER, 0,
                             "Medium::Task");
    ComAssertMsgRCRet(vrc,
                      ("Could not create Medium::Task thread (%Rrc)\n", vrc),
                      E_FAIL);

    return S_OK;
}

/**
 * Runs Medium::taskThread() by passing it this Task instance as an argument
 * on the current thread instead of creating a new one.
 *
 * This call implies that it is made on another temporary thread created for
 * some asynchronous task. Avoid calling it from a normal thread since the task
 * operatinos are potentially lengthy and will block the calling thread in this
 * case.
 *
 * Note that this Task object will be deleted by taskThread() when this method
 * returns!
 *
 * @note When the task is executed by this method, IProgress::notifyComplete()
 *       is not called for the progress object associated with this task when
 *       the task is finished. Instead, the result of the operation is returned
 *       by this method directly and it's the caller's responsibility to
 *       complete the progress object in this case.
 */
HRESULT Medium::Task::runNow()
{
    /* NIL_RTTHREAD indicates synchronous call. */
    Medium::taskThread(NIL_RTTHREAD, this);

    return m_rc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Merge chain class
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Helper class for merge operations.
 *
 * @note It is assumed that when modifying methods of this class are called,
 *       Medium::getTreeLock() is held in read mode.
 */
class Medium::MergeChain : public MediaList,
                           public com::SupportErrorInfoBase
{
public:

    MergeChain(bool aForward, bool aIgnoreAttachments)
        : mForward(aForward)
        , mIgnoreAttachments(aIgnoreAttachments) {}

    ~MergeChain()
    {
        for (iterator it = mChildren.begin(); it != mChildren.end(); ++ it)
        {
            HRESULT rc = (*it)->UnlockWrite(NULL);
            AssertComRC(rc);

            (*it)->releaseCaller();
        }

        for (iterator it = begin(); it != end(); ++ it)
        {
            AutoWriteLock alock(*it);
            Assert((*it)->m->state == MediumState_LockedWrite ||
                   (*it)->m->state == MediumState_Deleting);
            if ((*it)->m->state == MediumState_LockedWrite)
                (*it)->UnlockWrite(NULL);
            else
                (*it)->m->state = MediumState_Created;

            (*it)->releaseCaller();
        }

        if (!mParent.isNull())
            mParent->releaseCaller();
    }

    HRESULT addSource(Medium *aMedium)
    {
        HRESULT rc = aMedium->addCaller();
        if (FAILED(rc)) return rc;

        AutoWriteLock alock(aMedium);

        if (mForward)
        {
            rc = checkChildrenAndAttachmentsAndImmutable(aMedium);
            if (FAILED(rc))
            {
                aMedium->releaseCaller();
                return rc;
            }
        }

        /* We have to fetch the state with the COM method, cause it's possible
           that the medium isn't fully initialized yet. */
        MediumState_T m;
        rc = aMedium->RefreshState(&m);
        if (FAILED(rc)) return rc;
        /* go to Deleting */
        switch (m)
        {
            case MediumState_Created:
                aMedium->m->state = MediumState_Deleting;
                break;
            default:
                aMedium->releaseCaller();
                return aMedium->setStateError();
        }

        push_front(aMedium);

        if (mForward)
        {
            /* we will need parent to reparent target */
            if (!aMedium->m->pParent.isNull())
            {
                rc = aMedium->m->pParent->addCaller();
                if (FAILED(rc)) return rc;

                mParent = aMedium->m->pParent;
            }
        }
        else
        {
            /* we will need to reparent children */
            for (MediaList::const_iterator it = aMedium->getChildren().begin();
                 it != aMedium->getChildren().end();
                 ++it)
            {
                ComObjPtr<Medium> pMedium = *it;
                rc = pMedium->addCaller();
                if (FAILED(rc)) return rc;

                rc = pMedium->LockWrite(NULL);
                if (FAILED(rc))
                {
                    pMedium->releaseCaller();
                    return rc;
                }

                mChildren.push_back(pMedium);
            }
        }

        return S_OK;
    }

    HRESULT addTarget(Medium *aMedium)
    {
        HRESULT rc = aMedium->addCaller();
        if (FAILED(rc)) return rc;

        AutoWriteLock alock(aMedium);

        if (!mForward)
        {
            rc = checkChildrenAndImmutable(aMedium);
            if (FAILED(rc))
            {
                aMedium->releaseCaller();
                return rc;
            }
        }

        /* go to LockedWrite */
        rc = aMedium->LockWrite(NULL);
        if (FAILED(rc))
        {
            aMedium->releaseCaller();
            return rc;
        }

        push_front(aMedium);

        return S_OK;
    }

    HRESULT addIntermediate(Medium *aMedium)
    {
        HRESULT rc = aMedium->addCaller();
        if (FAILED(rc)) return rc;

        AutoWriteLock alock(aMedium);

        rc = checkChildrenAndAttachments(aMedium);
        if (FAILED(rc))
        {
            aMedium->releaseCaller();
            return rc;
        }

        /* go to Deleting */
        switch (aMedium->m->state)
        {
            case MediumState_Created:
                aMedium->m->state = MediumState_Deleting;
                break;
            default:
                aMedium->releaseCaller();
                return aMedium->setStateError();
        }

        push_front(aMedium);

        return S_OK;
    }

    bool isForward() const { return mForward; }
    Medium *parent() const { return mParent; }
    const MediaList& children() const { return mChildren; }

    Medium *source() const
    { AssertReturn(size() > 0, NULL); return mForward ? front() : back(); }

    Medium *target() const
    { AssertReturn(size() > 0, NULL); return mForward ? back() : front(); }

protected:

    // SupportErrorInfoBase interface
    const GUID &mainInterfaceID() const { return COM_IIDOF(IMedium); }
    const char *componentName() const { return Medium::ComponentName(); }

private:

    HRESULT check(Medium *aMedium, bool aChildren, bool aAttachments,
                  bool aImmutable)
    {
        if (aChildren)
        {
            /* not going to multi-merge as it's too expensive */
            if (aMedium->getChildren().size() > 1)
            {
                return setError(E_FAIL,
                                tr("Medium '%s' involved in the merge operation has more than one child medium (%d)"),
                                aMedium->m->strLocationFull.raw(),
                                aMedium->getChildren().size());
            }
        }

        if (aAttachments && !mIgnoreAttachments)
        {
            if (aMedium->m->backRefs.size() != 0)
                return setError(E_FAIL,
                                tr("Medium '%s' is attached to %d virtual machines"),
                                aMedium->m->strLocationFull.raw(),
                                aMedium->m->backRefs.size());
        }

        if (aImmutable)
        {
            if (aMedium->m->type == MediumType_Immutable)
                return setError(E_FAIL,
                                tr("Medium '%s' is immutable"),
                                aMedium->m->strLocationFull.raw());
        }

        return S_OK;
    }

    HRESULT checkChildren(Medium *aMedium)
    { return check(aMedium, true, false, false); }

    HRESULT checkChildrenAndImmutable(Medium *aMedium)
    { return check(aMedium, true, false, true); }

    HRESULT checkChildrenAndAttachments(Medium *aMedium)
    { return check(aMedium, true, true, false); }

    HRESULT checkChildrenAndAttachmentsAndImmutable(Medium *aMedium)
    { return check(aMedium, true, true, true); }

    /** true if forward merge, false if backward */
    bool mForward : 1;
    /** true to not perform attachment checks */
    bool mIgnoreAttachments : 1;

    /** Parent of the source when forward merge (if any) */
    ComObjPtr <Medium> mParent;
    /** Children of the source when backward merge (if any) */
    MediaList mChildren;
};

////////////////////////////////////////////////////////////////////////////////
//
// ImageChain class
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Helper class for image operations involving the entire parent chain.
 *
 * @note It is assumed that when modifying methods of this class are called,
 *       Medium::getTreeLock() is held in read mode.
 */
class Medium::ImageChain : public MediaList,
                           public com::SupportErrorInfoBase
{
public:

    ImageChain() {}

    ~ImageChain()
    {
        /* empty? */
        if (begin() != end())
        {
            MediaList::const_iterator last = end();
            last--;
            for (MediaList::const_iterator it = begin(); it != end(); ++ it)
            {
                AutoWriteLock alock(*it);
                if (it == last)
                {
                    Assert(   (*it)->m->state == MediumState_LockedRead
                           || (*it)->m->state == MediumState_LockedWrite);
                    if ((*it)->m->state == MediumState_LockedRead)
                        (*it)->UnlockRead(NULL);
                    else if ((*it)->m->state == MediumState_LockedWrite)
                        (*it)->UnlockWrite(NULL);
                }
                else
                {
                    Assert((*it)->m->state == MediumState_LockedRead);
                    if ((*it)->m->state == MediumState_LockedRead)
                        (*it)->UnlockRead(NULL);
                }

                (*it)->releaseCaller();
            }
        }
    }

    HRESULT addImage(Medium *aMedium)
    {
        HRESULT rc = aMedium->addCaller();
        if (FAILED(rc)) return rc;

        push_front(aMedium);

        return S_OK;
    }

    HRESULT lockImagesRead()
    {
        /* Lock all disks in the chain in {parent, child} order,
         * and make sure they are accessible. */
        /// @todo code duplication with SessionMachine::lockMedia, see below
        ErrorInfoKeeper eik(true /* aIsNull */);
        MultiResult mrc(S_OK);
        for (MediaList::const_iterator it = begin(); it != end(); ++ it)
        {
            HRESULT rc = S_OK;
            MediumState_T mediumState = (*it)->getState();

            /* accessibility check must be first, otherwise locking
             * interferes with getting the medium state. */
            if (mediumState == MediumState_Inaccessible)
            {
                rc = (*it)->RefreshState(&mediumState);
                if (FAILED(rc)) return rc;

                if (mediumState == MediumState_Inaccessible)
                {
                    Bstr error;
                    rc = (*it)->COMGETTER(LastAccessError)(error.asOutParam());
                    if (FAILED(rc)) return rc;

                    Bstr loc;
                    rc = (*it)->COMGETTER(Location)(loc.asOutParam());
                    if (FAILED(rc)) return rc;

                    /* collect multiple errors */
                    eik.restore();

                    /* be in sync with Medium::setStateError() */
                    Assert(!error.isEmpty());
                    mrc = setError(E_FAIL,
                                   tr("Medium '%ls' is not accessible. %ls"),
                                   loc.raw(), error.raw());

                    eik.fetch();
                }
            }

            rc = (*it)->LockRead(&mediumState);
            if (FAILED(rc)) return rc;
        }

        eik.restore();
        HRESULT rc2 = (HRESULT)mrc;
        if (FAILED(rc2)) return rc2;

        return S_OK;
    }

    HRESULT lockImagesReadAndLastWrite()
    {
        /* Lock all disks in the chain in {parent, child} order,
         * and make sure they are accessible. */
        /// @todo code duplication with SessionMachine::lockMedia, see below
        ErrorInfoKeeper eik(true /* aIsNull */);
        MultiResult mrc(S_OK);
        MediaList::const_iterator last = end();
        last--;
        for (MediaList::const_iterator it = begin(); it != end(); ++ it)
        {
            HRESULT rc = S_OK;
            MediumState_T mediumState = (*it)->getState();

            /* accessibility check must be first, otherwise locking
             * interferes with getting the medium state. */
            if (mediumState == MediumState_Inaccessible)
            {
                rc = (*it)->RefreshState(&mediumState);
                if (FAILED(rc)) return rc;

                if (mediumState == MediumState_Inaccessible)
                {
                    Bstr error;
                    rc = (*it)->COMGETTER(LastAccessError)(error.asOutParam());
                    if (FAILED(rc)) return rc;

                    Bstr loc;
                    rc = (*it)->COMGETTER(Location)(loc.asOutParam());
                    if (FAILED(rc)) return rc;

                    /* collect multiple errors */
                    eik.restore();

                    /* be in sync with Medium::setStateError() */
                    Assert(!error.isEmpty());
                    mrc = setError(E_FAIL,
                                   tr("Medium '%ls' is not accessible. %ls"),
                                   loc.raw(), error.raw());

                    eik.fetch();
                }
            }

            if (it == last)
                rc = (*it)->LockWrite(&mediumState);
            else
                rc = (*it)->LockRead(&mediumState);
        }

        eik.restore();
        HRESULT rc2 = (HRESULT)mrc;
        if (FAILED(rc2)) return rc2;

        return S_OK;
    }

protected:

    // SupportErrorInfoBase interface
    const GUID &mainInterfaceID() const { return COM_IIDOF(IMedium); }
    const char *componentName() const { return Medium::ComponentName(); }

private:

};


////////////////////////////////////////////////////////////////////////////////
//
// Medium constructor / destructor
//
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(Medium)

HRESULT Medium::FinalConstruct()
{
    m = new Data;

    /* Initialize the callbacks of the VD error interface */
    m->vdIfCallsError.cbSize = sizeof(VDINTERFACEERROR);
    m->vdIfCallsError.enmInterface = VDINTERFACETYPE_ERROR;
    m->vdIfCallsError.pfnError = vdErrorCall;
    m->vdIfCallsError.pfnMessage = NULL;

    /* Initialize the callbacks of the VD config interface */
    m->vdIfCallsConfig.cbSize = sizeof(VDINTERFACECONFIG);
    m->vdIfCallsConfig.enmInterface = VDINTERFACETYPE_CONFIG;
    m->vdIfCallsConfig.pfnAreKeysValid = vdConfigAreKeysValid;
    m->vdIfCallsConfig.pfnQuerySize = vdConfigQuerySize;
    m->vdIfCallsConfig.pfnQuery = vdConfigQuery;

    /* Initialize the callbacks of the VD TCP interface (we always use the host
     * IP stack for now) */
    m->vdIfCallsTcpNet.cbSize = sizeof(VDINTERFACETCPNET);
    m->vdIfCallsTcpNet.enmInterface = VDINTERFACETYPE_TCPNET;
    m->vdIfCallsTcpNet.pfnClientConnect = RTTcpClientConnect;
    m->vdIfCallsTcpNet.pfnClientClose = RTTcpClientClose;
    m->vdIfCallsTcpNet.pfnSelectOne = RTTcpSelectOne;
    m->vdIfCallsTcpNet.pfnRead = RTTcpRead;
    m->vdIfCallsTcpNet.pfnWrite = RTTcpWrite;
    m->vdIfCallsTcpNet.pfnFlush = RTTcpFlush;

    /* Initialize the per-disk interface chain */
    int vrc;
    vrc = VDInterfaceAdd(&m->vdIfError,
                         "Medium::vdInterfaceError",
                         VDINTERFACETYPE_ERROR,
                         &m->vdIfCallsError, this, &m->vdDiskIfaces);
    AssertRCReturn(vrc, E_FAIL);

    vrc = VDInterfaceAdd(&m->vdIfConfig,
                         "Medium::vdInterfaceConfig",
                         VDINTERFACETYPE_CONFIG,
                         &m->vdIfCallsConfig, this, &m->vdDiskIfaces);
    AssertRCReturn(vrc, E_FAIL);

    vrc = VDInterfaceAdd(&m->vdIfTcpNet,
                         "Medium::vdInterfaceTcpNet",
                         VDINTERFACETYPE_TCPNET,
                         &m->vdIfCallsTcpNet, this, &m->vdDiskIfaces);
    AssertRCReturn(vrc, E_FAIL);

    vrc = RTSemEventMultiCreate(&m->queryInfoSem);
    AssertRCReturn(vrc, E_FAIL);
    vrc = RTSemEventMultiSignal(m->queryInfoSem);
    AssertRCReturn(vrc, E_FAIL);

    return S_OK;
}

void Medium::FinalRelease()
{
    uninit();

    delete m;
}

/**
 * Initializes the hard disk object without creating or opening an associated
 * storage unit.
 *
 * For hard disks that don't have the VD_CAP_CREATE_FIXED or
 * VD_CAP_CREATE_DYNAMIC capability (and therefore cannot be created or deleted
 * with the means of VirtualBox) the associated storage unit is assumed to be
 * ready for use so the state of the hard disk object will be set to Created.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aLocation     Storage unit location.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     CBSTR aFormat,
                     CBSTR aLocation)
{
    AssertReturn(aVirtualBox != NULL, E_FAIL);
    AssertReturn(aFormat != NULL && *aFormat != '\0', E_FAIL);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT rc = S_OK;

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(m->pVirtualBox) = aVirtualBox;

    /* no storage yet */
    m->state = MediumState_NotCreated;

    /* cannot be a host drive */
    m->hostDrive = FALSE;

    /* No storage unit is created yet, no need to queryInfo() */

    rc = setFormat(aFormat);
    if (FAILED(rc)) return rc;

    if (m->formatObj->capabilities() & MediumFormatCapabilities_File)
    {
        rc = setLocation(aLocation);
        if (FAILED(rc)) return rc;
    }
    else
    {
        rc = setLocation(aLocation);
        if (FAILED(rc)) return rc;

        /// @todo later we may want to use a pfnComposeLocation backend info
        /// callback to generate a well-formed location value (based on the hard
        /// disk properties we have) rather than allowing each caller to invent
        /// its own (pseudo-)location.
    }

    if (!(m->formatObj->capabilities() &
          (MediumFormatCapabilities_CreateFixed |
           MediumFormatCapabilities_CreateDynamic)))
    {
        /* storage for hard disks of this format can neither be explicitly
         * created by VirtualBox nor deleted, so we place the hard disk to
         * Created state here and also add it to the registry */
        m->state = MediumState_Created;
        unconst(m->id).create();
        rc = m->pVirtualBox->registerHardDisk(this);

        /// @todo later we may want to use a pfnIsConfigSufficient backend info
        /// callback that would tell us when we have enough properties to work
        /// with the hard disk and this information could be used to actually
        /// move such hard disks from NotCreated to Created state. Instead of
        /// pfnIsConfigSufficient we can use MediumFormat property
        /// descriptions to see which properties are mandatory
    }

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(rc))
        autoInitSpan.setSucceeded();

    return rc;
}

/**
 * Initializes the medium object by opening the storage unit at the specified
 * location. The enOpenMode parameter defines whether the image will be opened
 * read/write or read-only.
 *
 * Note that the UUID, format and the parent of this medium will be
 * determined when reading the medium storage unit, unless new values are
 * specified by the parameters. If the detected or set parent is
 * not known to VirtualBox, then this method will fail.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aLocation     Storage unit location.
 * @param enOpenMode    Whether to open the image read/write or read-only.
 * @param aDeviceType   Device type of medium.
 * @param aSetImageId   Whether to set the image UUID or not.
 * @param aImageId      New image UUID if @aSetId is true. Empty string means
 *                      create a new UUID, and a zero UUID is invalid.
 * @param aSetParentId  Whether to set the parent UUID or not.
 * @param aParentId     New parent UUID if @aSetParentId is true. Empty string
 *                      means create a new UUID, and a zero UUID is valid.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     CBSTR aLocation,
                     HDDOpenMode enOpenMode,
                     DeviceType_T aDeviceType,
                     BOOL aSetImageId,
                     const Guid &aImageId,
                     BOOL aSetParentId,
                     const Guid &aParentId)
{
    AssertReturn(aVirtualBox, E_INVALIDARG);
    AssertReturn(aLocation, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT rc = S_OK;

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(m->pVirtualBox) = aVirtualBox;

    /* there must be a storage unit */
    m->state = MediumState_Created;

    /* remember device type for correct unregistering later */
    m->devType = aDeviceType;

    /* cannot be a host drive */
    m->hostDrive = FALSE;

    /* remember the open mode (defaults to ReadWrite) */
    m->hddOpenMode = enOpenMode;

    if (aDeviceType == DeviceType_HardDisk)
        rc = setLocation(aLocation);
    else
        rc = setLocation(aLocation, "RAW");
    if (FAILED(rc)) return rc;

    /* save the new uuid values, will be used by queryInfo() */
    m->setImageId = aSetImageId;
    unconst(m->imageId) = aImageId;
    m->setParentId = aSetParentId;
    unconst(m->parentId) = aParentId;

    /* get all the information about the medium from the storage unit */
    rc = queryInfo();

    if (SUCCEEDED(rc))
    {
        /* if the storage unit is not accessible, it's not acceptable for the
         * newly opened media so convert this into an error */
        if (m->state == MediumState_Inaccessible)
        {
            Assert(!m->strLastAccessError.isEmpty());
            rc = setError(E_FAIL, m->strLastAccessError);
        }
        else
        {
            AssertReturn(!m->id.isEmpty(), E_FAIL);

            /* storage format must be detected by queryInfo() if the medium is accessible */
            AssertReturn(!m->strFormat.isEmpty(), E_FAIL);
        }
    }

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(rc))
        autoInitSpan.setSucceeded();

    return rc;
}

/**
 * Initializes the medium object by loading its data from the given settings
 * node. In this mode, the image will always be opened read/write.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aParent       Parent medium disk or NULL for a root (base) medium.
 * @param aDeviceType   Device type of the medium.
 * @param aNode         Configuration settings.
 *
 * @note Locks VirtualBox lock for writing, getTreeLock() for writing.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     Medium *aParent,
                     DeviceType_T aDeviceType,
                     const settings::Medium &data)
{
    using namespace settings;

    AssertReturn(aVirtualBox, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    HRESULT rc = S_OK;

    /* share VirtualBox and parent weakly */
    unconst(m->pVirtualBox) = aVirtualBox;

    /* register with VirtualBox/parent early, since uninit() will
     * unconditionally unregister on failure */
    if (aParent)
    {
        // differencing image: add to parent
        AutoWriteLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());
        m->pParent = aParent;
        aParent->m->llChildren.push_back(this);
    }

    /* see below why we don't call queryInfo() (and therefore treat the medium
     * as inaccessible for now */
    m->state = MediumState_Inaccessible;
    m->strLastAccessError = tr("Accessibility check was not yet performed");

    /* required */
    unconst(m->id) = data.uuid;

    /* assume not a host drive */
    m->hostDrive = FALSE;

    /* optional */
    m->strDescription = data.strDescription;

    /* required */
    if (aDeviceType == DeviceType_HardDisk)
    {
        AssertReturn(!data.strFormat.isEmpty(), E_FAIL);
        rc = setFormat(Bstr(data.strFormat));
        if (FAILED(rc)) return rc;
    }
    else
    {
        /// @todo handle host drive settings here as well?
        if (!data.strFormat.isEmpty())
            rc = setFormat(Bstr(data.strFormat));
        else
            rc = setFormat(Bstr("RAW"));
        if (FAILED(rc)) return rc;
    }

    /* optional, only for diffs, default is false;
     * we can only auto-reset diff images, so they
     * must not have a parent */
    if (aParent != NULL)
        m->autoReset = data.fAutoReset;
    else
        m->autoReset = false;

    /* properties (after setting the format as it populates the map). Note that
     * if some properties are not supported but preseint in the settings file,
     * they will still be read and accessible (for possible backward
     * compatibility; we can also clean them up from the XML upon next
     * XML format version change if we wish) */
    for (settings::PropertiesMap::const_iterator it = data.properties.begin();
         it != data.properties.end(); ++ it)
    {
        const Utf8Str &name = it->first;
        const Utf8Str &value = it->second;
        m->properties[Bstr(name)] = Bstr(value);
    }

    /* required */
    rc = setLocation(data.strLocation);
    if (FAILED(rc)) return rc;

    if (aDeviceType == DeviceType_HardDisk)
    {
        /* type is only for base hard disks */
        if (m->pParent.isNull())
            m->type = data.hdType;
    }
    else
        m->type = MediumType_Writethrough;

    /* remember device type for correct unregistering later */
    m->devType = aDeviceType;

    LogFlowThisFunc(("m->locationFull='%s', m->format=%s, m->id={%RTuuid}\n",
                     m->strLocationFull.raw(), m->strFormat.raw(), m->id.raw()));

    /* Don't call queryInfo() for registered media to prevent the calling
     * thread (i.e. the VirtualBox server startup thread) from an unexpected
     * freeze but mark it as initially inaccessible instead. The vital UUID,
     * location and format properties are read from the registry file above; to
     * get the actual state and the rest of the data, the user will have to call
     * COMGETTER(State). */

    /* load all children */
    for (settings::MediaList::const_iterator it = data.llChildren.begin();
         it != data.llChildren.end();
         ++it)
    {
        const settings::Medium &med = *it;

        ComObjPtr<Medium> pHD;
        pHD.createObject();
        rc = pHD->init(aVirtualBox,
                       this,            // parent
                       aDeviceType,
                       med);              // child data
        if (FAILED(rc)) break;

        rc = m->pVirtualBox->registerHardDisk(pHD, false /* aSaveRegistry */);
        if (FAILED(rc)) break;
    }

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(rc))
        autoInitSpan.setSucceeded();

    return rc;
}

/**
 * Initializes the medium object by providing the host drive information.
 * Not used for anything but the host floppy/host DVD case.
 *
 * @todo optimize all callers to avoid reconstructing objects with the same
 * information over and over again - in the typical case each VM referring to
 * a particular host drive has its own instance.
 *
 * @param aVirtualBox   VirtualBox object.
 * @param aDeviceType   Device type of the medium.
 * @param aLocation     Location of the host drive.
 * @param aDescription  Comment for this host drive.
 *
 * @note Locks VirtualBox lock for writing, getTreeLock() for writing.
 */
HRESULT Medium::init(VirtualBox *aVirtualBox,
                     DeviceType_T aDeviceType,
                     CBSTR aLocation,
                     CBSTR aDescription)
{
    ComAssertRet(aDeviceType == DeviceType_DVD || aDeviceType == DeviceType_Floppy, E_INVALIDARG);
    ComAssertRet(aLocation, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* share VirtualBox weakly (parent remains NULL so far) */
    unconst(m->pVirtualBox) = aVirtualBox;

    /* fake up a UUID which is unique, but also reproducible */
    RTUUID uuid;
    RTUuidClear(&uuid);
    if (aDeviceType == DeviceType_DVD)
        memcpy(&uuid.au8[0], "DVD", 3);
    else
        memcpy(&uuid.au8[0], "FD", 2);
    /* use device name, adjusted to the end of uuid, shortened if necessary */
    Utf8Str loc(aLocation);
    size_t cbLocation = strlen(loc.raw());
    if (cbLocation > 12)
        memcpy(&uuid.au8[4], loc.raw() + (cbLocation - 12), 12);
    else
        memcpy(&uuid.au8[4 + 12 - cbLocation], loc.raw(), cbLocation);
    unconst(m->id) = uuid;

    m->type = MediumType_Writethrough;
    m->devType = aDeviceType;
    m->state = MediumState_Created;
    m->hostDrive = true;
    HRESULT rc = setFormat(Bstr("RAW"));
    if (FAILED(rc)) return rc;
    rc = setLocation(aLocation);
    if (FAILED(rc)) return rc;
    m->strDescription = aDescription;

/// @todo generate uuid (similarly to host network interface uuid) from location and device type

    autoInitSpan.setSucceeded();
    return S_OK;
}

/**
 * Uninitializes the instance.
 *
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 *
 * @note All children of this hard disk get uninitialized by calling their
 *       uninit() methods.
 *
 * @note Locks getTreeLock() for writing, VirtualBox for writing.
 */
void Medium::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    if (!m->formatObj.isNull())
    {
        /* remove the caller reference we added in setFormat() */
        m->formatObj->releaseCaller();
        m->formatObj.setNull();
    }

    if (m->state == MediumState_Deleting)
    {
        /* we are being uninitialized after've been deleted by merge.
         * Reparenting has already been done so don't touch it here (we are
         * now orphans and remoeDependentChild() will assert) */
        Assert(m->pParent.isNull());
    }
    else
    {
        /* we uninit children and reset mParent
         * and VirtualBox::removeDependentChild() needs a write lock */
        AutoWriteLock alock1(m->pVirtualBox->lockHandle());
        AutoWriteLock alock2(m->pVirtualBox->hardDiskTreeLockHandle());

        MediaList::iterator it;
        for (it = m->llChildren.begin();
            it != m->llChildren.end();
            ++it)
        {
            Medium *pChild = *it;
            pChild->m->pParent.setNull();
            pChild->uninit();
        }
        m->llChildren.clear();          // this unsets all the ComPtrs and probably calls delete

        if (m->pParent)
        {
            // this is a differencing disk: then remove it from the parent's children list
            deparent();
        }
    }

    RTSemEventMultiSignal(m->queryInfoSem);
    RTSemEventMultiDestroy(m->queryInfoSem);
    m->queryInfoSem = NIL_RTSEMEVENTMULTI;

    unconst(m->pVirtualBox).setNull();
}

/**
 * Internal helper that removes "this" from the list of children of its
 * parent. Used in uninit() and other places when reparenting is necessary.
 *
 * The caller must hold the hard disk tree lock!
 */
void Medium::deparent()
{
    MediaList &llParent = m->pParent->m->llChildren;
    for (MediaList::iterator it = llParent.begin();
         it != llParent.end();
         ++it)
    {
        Medium *pParentsChild = *it;
        if (this == pParentsChild)
        {
            llParent.erase(it);
            break;
        }
    }
    m->pParent.setNull();
}

////////////////////////////////////////////////////////////////////////////////
//
// IMedium public methods
//
////////////////////////////////////////////////////////////////////////////////

STDMETHODIMP Medium::COMGETTER(Id)(BSTR *aId)
{
    CheckComArgOutPointerValid(aId);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    m->id.toUtf16().cloneTo(aId);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Description)(BSTR *aDescription)
{
    CheckComArgOutPointerValid(aDescription);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    if (m->strDescription.isEmpty())
        Bstr("").cloneTo(aDescription);
    else
        m->strDescription.cloneTo(aDescription);

    return S_OK;
}

STDMETHODIMP Medium::COMSETTER(Description)(IN_BSTR aDescription)
{
    CheckComArgNotNull(aDescription);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    /// @todo update m->description and save the global registry (and local
    /// registries of portable VMs referring to this medium), this will also
    /// require to add the mRegistered flag to data

    ReturnComNotImplemented();
}

STDMETHODIMP Medium::COMGETTER(State)(MediumState_T *aState)
{
    CheckComArgOutPointerValid(aState);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);
    *aState = m->state;

    return S_OK;
}


STDMETHODIMP Medium::COMGETTER(Location)(BSTR *aLocation)
{
    CheckComArgOutPointerValid(aLocation);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    m->strLocationFull.cloneTo(aLocation);

    return S_OK;
}

STDMETHODIMP Medium::COMSETTER(Location)(IN_BSTR aLocation)
{
    CheckComArgNotNull(aLocation);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    /// @todo NEWMEDIA for file names, add the default extension if no extension
    /// is present (using the information from the VD backend which also implies
    /// that one more parameter should be passed to setLocation() requesting
    /// that functionality since it is only allwed when called from this method

    /// @todo NEWMEDIA rename the file and set m->location on success, then save
    /// the global registry (and local registries of portable VMs referring to
    /// this medium), this will also require to add the mRegistered flag to data

    ReturnComNotImplemented();
}

STDMETHODIMP Medium::COMGETTER(Name)(BSTR *aName)
{
    CheckComArgOutPointerValid(aName);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    getName().cloneTo(aName);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(DeviceType)(DeviceType_T *aDeviceType)
{
    CheckComArgOutPointerValid(aDeviceType);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    *aDeviceType = m->devType;

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(HostDrive)(BOOL *aHostDrive)
{
    CheckComArgOutPointerValid(aHostDrive);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    *aHostDrive = m->hostDrive;

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Size)(ULONG64 *aSize)
{
    CheckComArgOutPointerValid(aSize);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    *aSize = m->size;

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Format)(BSTR *aFormat)
{
    if (aFormat == NULL)
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* no need to lock, m->format is const */
    m->strFormat.cloneTo(aFormat);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Type)(MediumType_T *aType)
{
    if (aType == NULL)
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    *aType = m->type;

    return S_OK;
}

STDMETHODIMP Medium::COMSETTER(Type)(MediumType_T aType)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* VirtualBox::saveSettings() needs a write lock */
    AutoMultiWriteLock2 alock(m->pVirtualBox, this);

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return setStateError();
    }

    if (m->type == aType)
    {
        /* Nothing to do */
        return S_OK;
    }

    /* we access mParent & children() */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    /* cannot change the type of a differencing hard disk */
    if (m->pParent)
        return setError(E_FAIL,
                        tr("Cannot change the type of hard disk '%s' because it is a differencing hard disk"),
                        m->strLocationFull.raw());

    /* cannot change the type of a hard disk being in use */
    if (m->backRefs.size() != 0)
        return setError(E_FAIL,
                        tr("Cannot change the type of hard disk '%s' because it is attached to %d virtual machines"),
                        m->strLocationFull.raw(), m->backRefs.size());

    switch (aType)
    {
        case MediumType_Normal:
        case MediumType_Immutable:
        {
            /* normal can be easily converted to immutable and vice versa even
             * if they have children as long as they are not attached to any
             * machine themselves */
            break;
        }
        case MediumType_Writethrough:
        {
            /* cannot change to writethrough if there are children */
            if (getChildren().size() != 0)
                return setError(E_FAIL,
                                tr("Cannot change type for hard disk '%s' since it has %d child hard disk(s)"),
                                m->strLocationFull.raw(), getChildren().size());
            break;
        }
        default:
            AssertFailedReturn(E_FAIL);
    }

    m->type = aType;

    HRESULT rc = m->pVirtualBox->saveSettings();

    return rc;
}

STDMETHODIMP Medium::COMGETTER(Parent)(IMedium **aParent)
{
    if (aParent == NULL)
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* we access mParent */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    m->pParent.queryInterfaceTo(aParent);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Children)(ComSafeArrayOut(IMedium *, aChildren))
{
    if (ComSafeArrayOutIsNull(aChildren))
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* we access children */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    SafeIfaceArray<IMedium> children(this->getChildren());
    children.detachTo(ComSafeArrayOutArg(aChildren));

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(Base)(IMedium **aBase)
{
    if (aBase == NULL)
        return E_POINTER;

    /* base() will do callers/locking */

    getBase().queryInterfaceTo(aBase);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(ReadOnly)(BOOL *aReadOnly)
{
    if (aReadOnly == NULL)
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* isRadOnly() will do locking */

    *aReadOnly = isReadOnly();

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(LogicalSize)(ULONG64 *aLogicalSize)
{
    CheckComArgOutPointerValid(aLogicalSize);

    {
        AutoCaller autoCaller(this);
        if (FAILED(autoCaller.rc())) return autoCaller.rc();

        AutoReadLock alock(this);

        /* we access mParent */
        AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

        if (m->pParent.isNull())
        {
            *aLogicalSize = m->logicalSize;

            return S_OK;
        }
    }

    /* We assume that some backend may decide to return a meaningless value in
     * response to VDGetSize() for differencing hard disks and therefore
     * always ask the base hard disk ourselves. */

    /* base() will do callers/locking */

    return getBase()->COMGETTER(LogicalSize)(aLogicalSize);
}

STDMETHODIMP Medium::COMGETTER(AutoReset)(BOOL *aAutoReset)
{
    CheckComArgOutPointerValid(aAutoReset);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    if (m->pParent)
        *aAutoReset = FALSE;

    *aAutoReset = m->autoReset;

    return S_OK;
}

STDMETHODIMP Medium::COMSETTER(AutoReset)(BOOL aAutoReset)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* VirtualBox::saveSettings() needs a write lock */
    AutoMultiWriteLock2 alock(m->pVirtualBox, this);

    if (m->pParent.isNull())
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Hard disk '%s' is not differencing"),
                        m->strLocationFull.raw());

    if (m->autoReset != aAutoReset)
    {
        m->autoReset = aAutoReset;

        return m->pVirtualBox->saveSettings();
    }

    return S_OK;
}
STDMETHODIMP Medium::COMGETTER(LastAccessError)(BSTR *aLastAccessError)
{
    CheckComArgOutPointerValid(aLastAccessError);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    if (m->strLastAccessError.isEmpty())
        Bstr("").cloneTo(aLastAccessError);
    else
        m->strLastAccessError.cloneTo(aLastAccessError);

    return S_OK;
}

STDMETHODIMP Medium::COMGETTER(MachineIds)(ComSafeArrayOut(BSTR,aMachineIds))
{
    if (ComSafeGUIDArrayOutIsNull(aMachineIds))
        return E_POINTER;

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    com::SafeArray<BSTR> machineIds;

    if (m->backRefs.size() != 0)
    {
        machineIds.reset(m->backRefs.size());

        size_t i = 0;
        for (BackRefList::const_iterator it = m->backRefs.begin();
             it != m->backRefs.end(); ++ it, ++ i)
        {
             it->machineId.toUtf16().detachTo(&machineIds [i]);
        }
    }

    machineIds.detachTo(ComSafeArrayOutArg(aMachineIds));

    return S_OK;
}

STDMETHODIMP Medium::RefreshState(MediumState_T *aState)
{
    CheckComArgOutPointerValid(aState);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* queryInfo() locks this for writing. */
    AutoWriteLock alock(this);

    HRESULT rc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        {
            rc = queryInfo();
            break;
        }
        default:
            break;
    }

    *aState = m->state;

    return rc;
}

STDMETHODIMP Medium::GetSnapshotIds(IN_BSTR aMachineId,
                                    ComSafeArrayOut(BSTR, aSnapshotIds))
{
    CheckComArgExpr(aMachineId, Guid(aMachineId).isEmpty() == false);
    CheckComArgOutSafeArrayPointerValid(aSnapshotIds);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    com::SafeArray<BSTR> snapshotIds;

    Guid id(aMachineId);
    for (BackRefList::const_iterator it = m->backRefs.begin();
         it != m->backRefs.end(); ++ it)
    {
        if (it->machineId == id)
        {
            size_t size = it->llSnapshotIds.size();

            /* if the medium is attached to the machine in the current state, we
             * return its ID as the first element of the array */
            if (it->fInCurState)
                ++size;

            if (size > 0)
            {
                snapshotIds.reset(size);

                size_t j = 0;
                if (it->fInCurState)
                    it->machineId.toUtf16().detachTo(&snapshotIds[j++]);

                for (BackRef::GuidList::const_iterator jt = it->llSnapshotIds.begin();
                     jt != it->llSnapshotIds.end();
                     ++jt, ++j)
                {
                     (*jt).toUtf16().detachTo(&snapshotIds[j]);
                }
            }

            break;
        }
    }

    snapshotIds.detachTo(ComSafeArrayOutArg(aSnapshotIds));

    return S_OK;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
STDMETHODIMP Medium::LockRead(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    /* Wait for a concurrently running queryInfo() to complete */
    while (m->queryInfoRunning)
    {
        alock.leave();
        RTSemEventMultiWait(m->queryInfoSem, RT_INDEFINITE_WAIT);
        alock.enter();
    }

    /* return the current state before */
    if (aState)
        *aState = m->state;

    HRESULT rc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        {
            ++m->readers;

            ComAssertMsgBreak(m->readers != 0, ("Counter overflow"), rc = E_FAIL);

            /* Remember pre-lock state */
            if (m->state != MediumState_LockedRead)
                m->preLockState = m->state;

            LogFlowThisFunc(("Okay - prev state=%d readers=%d\n", m->state, m->readers));
            m->state = MediumState_LockedRead;

            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d\n", m->state));
            rc = setStateError();
            break;
        }
    }

    return rc;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
STDMETHODIMP Medium::UnlockRead(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    HRESULT rc = S_OK;

    switch (m->state)
    {
        case MediumState_LockedRead:
        {
            Assert(m->readers != 0);
            --m->readers;

            /* Reset the state after the last reader */
            if (m->readers == 0)
                m->state = m->preLockState;

            LogFlowThisFunc(("new state=%d\n", m->state));
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d\n", m->state));
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr ("Medium '%s' is not locked for reading"),
                          m->strLocationFull.raw());
            break;
        }
    }

    /* return the current state after */
    if (aState)
        *aState = m->state;

    return rc;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
STDMETHODIMP Medium::LockWrite(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    /* Wait for a concurrently running queryInfo() to complete */
    while (m->queryInfoRunning)
    {
        alock.leave();
        RTSemEventMultiWait(m->queryInfoSem, RT_INDEFINITE_WAIT);
        alock.enter();
    }

    /* return the current state before */
    if (aState)
        *aState = m->state;

    HRESULT rc = S_OK;

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        {
            m->preLockState = m->state;

            LogFlowThisFunc(("Okay - prev state=%d locationFull=%s\n", m->state, getLocationFull().c_str()));
            m->state = MediumState_LockedWrite;
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d locationFull=%s\n", m->state, getLocationFull().c_str()));
            rc = setStateError();
            break;
        }
    }

    return rc;
}

/**
 * @note @a aState may be NULL if the state value is not needed (only for
 *       in-process calls).
 */
STDMETHODIMP Medium::UnlockWrite(MediumState_T *aState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    HRESULT rc = S_OK;

    switch (m->state)
    {
        case MediumState_LockedWrite:
        {
            m->state = m->preLockState;
            LogFlowThisFunc(("new state=%d locationFull=%s\n", m->state, getLocationFull().c_str()));
            break;
        }
        default:
        {
            LogFlowThisFunc(("Failing - state=%d locationFull=%s\n", m->state, getLocationFull().c_str()));
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr ("Medium '%s' is not locked for writing"),
                          m->strLocationFull.raw());
            break;
        }
    }

    /* return the current state after */
    if (aState)
        *aState = m->state;

    return rc;
}

STDMETHODIMP Medium::Close()
{
    AutoMayUninitSpan mayUninitSpan(this);
    if (FAILED(mayUninitSpan.rc())) return mayUninitSpan.rc();

    if (mayUninitSpan.alreadyInProgress())
        return S_OK;

    /* unregisterWithVirtualBox() is assumed to always need a write mVirtualBox
     * lock as it is intenede to modify its internal structires. Also, we want
     * to unregister ourselves atomically after detecting that closure is
     * possible to make sure that we don't do that after another thread has done
     * VirtualBox::find*() but before it starts using us (provided that it holds
     * a mVirtualBox lock of course). */

    AutoWriteLock vboxLock(m->pVirtualBox);

    bool wasCreated = true;

    switch (m->state)
    {
        case MediumState_NotCreated:
            wasCreated = false;
            break;
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return setStateError();
    }

    if (m->backRefs.size() != 0)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Medium '%s' is attached to %d virtual machines"),
                        m->strLocationFull.raw(), m->backRefs.size());

    /* perform extra media-dependent close checks */
    HRESULT rc = canClose();
    if (FAILED(rc)) return rc;

    if (wasCreated)
    {
        /* remove from the list of known media before performing actual
         * uninitialization (to keep the media registry consistent on
         * failure to do so) */
        rc = unregisterWithVirtualBox();
        if (FAILED(rc)) return rc;
    }

    /* cause uninit() to happen on success */
    mayUninitSpan.acceptUninit();

    return S_OK;
}

STDMETHODIMP Medium::GetProperty(IN_BSTR aName, BSTR *aValue)
{
    CheckComArgStrNotEmptyOrNull(aName);
    CheckComArgOutPointerValid(aValue);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    Data::PropertyMap::const_iterator it = m->properties.find(Bstr(aName));
    if (it == m->properties.end())
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                        tr("Property '%ls' does not exist"), aName);

    if (it->second.isEmpty())
        Bstr("").cloneTo(aValue);
    else
        it->second.cloneTo(aValue);

    return S_OK;
}

STDMETHODIMP Medium::SetProperty(IN_BSTR aName, IN_BSTR aValue)
{
    CheckComArgStrNotEmptyOrNull(aName);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* VirtualBox::saveSettings() needs a write lock */
    AutoMultiWriteLock2 alock(m->pVirtualBox, this);

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return setStateError();
    }

    Data::PropertyMap::iterator it = m->properties.find(Bstr(aName));
    if (it == m->properties.end())
        return setError(VBOX_E_OBJECT_NOT_FOUND,
                        tr("Property '%ls' does not exist"),
                        aName);

    if (aValue && !*aValue)
        it->second = (const char *)NULL;
    else
        it->second = aValue;

    HRESULT rc = m->pVirtualBox->saveSettings();

    return rc;
}

STDMETHODIMP Medium::GetProperties(IN_BSTR aNames,
                                   ComSafeArrayOut(BSTR, aReturnNames),
                                   ComSafeArrayOut(BSTR, aReturnValues))
{
    CheckComArgOutSafeArrayPointerValid(aReturnNames);
    CheckComArgOutSafeArrayPointerValid(aReturnValues);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    /// @todo make use of aNames according to the documentation
    NOREF(aNames);

    com::SafeArray<BSTR> names(m->properties.size());
    com::SafeArray<BSTR> values(m->properties.size());
    size_t i = 0;

    for (Data::PropertyMap::const_iterator it = m->properties.begin();
         it != m->properties.end();
         ++it)
    {
        it->first.cloneTo(&names[i]);
        if (it->second.isEmpty())
            Bstr("").cloneTo(&values [i]);
        else
            it->second.cloneTo(&values [i]);
        ++ i;
    }

    names.detachTo(ComSafeArrayOutArg(aReturnNames));
    values.detachTo(ComSafeArrayOutArg(aReturnValues));

    return S_OK;
}

STDMETHODIMP Medium::SetProperties(ComSafeArrayIn(IN_BSTR, aNames),
                                   ComSafeArrayIn(IN_BSTR, aValues))
{
    CheckComArgSafeArrayNotNull(aNames);
    CheckComArgSafeArrayNotNull(aValues);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /* VirtualBox::saveSettings() needs a write lock */
    AutoMultiWriteLock2 alock(m->pVirtualBox, this);

    com::SafeArray<IN_BSTR> names(ComSafeArrayInArg(aNames));
    com::SafeArray<IN_BSTR> values(ComSafeArrayInArg(aValues));

    /* first pass: validate names */
    for (size_t i = 0;
         i < names.size();
         ++i)
    {
        if (m->properties.find(Bstr(names[i])) == m->properties.end())
            return setError(VBOX_E_OBJECT_NOT_FOUND,
                            tr("Property '%ls' does not exist"), names[i]);
    }

    /* second pass: assign */
    for (size_t i = 0;
         i < names.size();
         ++i)
    {
        Data::PropertyMap::iterator it = m->properties.find(Bstr(names[i]));
        AssertReturn(it != m->properties.end(), E_FAIL);

        if (values[i] && !*values[i])
            it->second = (const char *)NULL;
        else
            it->second = values [i];
    }

    HRESULT rc = m->pVirtualBox->saveSettings();

    return rc;
}

STDMETHODIMP Medium::CreateBaseStorage(ULONG64 aLogicalSize,
                                       MediumVariant_T aVariant,
                                       IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    aVariant = (MediumVariant_T)((unsigned)aVariant & (unsigned)~MediumVariant_Diff);
    if (    !(aVariant & MediumVariant_Fixed)
        &&  !(m->formatObj->capabilities() & MediumFormatCapabilities_CreateDynamic))
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Hard disk format '%s' does not support dynamic storage creation"),
                        m->strFormat.raw());
    if (    (aVariant & MediumVariant_Fixed)
        &&  !(m->formatObj->capabilities() & MediumFormatCapabilities_CreateDynamic))
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Hard disk format '%s' does not support fixed storage creation"),
                        m->strFormat.raw());

    switch (m->state)
    {
        case MediumState_NotCreated:
            break;
        default:
            return setStateError();
    }

    ComObjPtr <Progress> progress;
    progress.createObject();
    /// @todo include fixed/dynamic
    HRESULT rc = progress->init(m->pVirtualBox,
                                static_cast<IMedium*>(this),
                                (aVariant & MediumVariant_Fixed)
                                        ? BstrFmt(tr("Creating fixed hard disk storage unit '%s'"), m->strLocationFull.raw())
                                        : BstrFmt(tr("Creating dynamic hard disk storage unit '%s'"), m->strLocationFull.raw()),
                                TRUE /* aCancelable */);
    if (FAILED(rc)) return rc;

    /* setup task object and thread to carry out the operation
     * asynchronously */

    std::auto_ptr <Task> task(new Task(this, progress, Task::CreateBase));
    AssertComRCReturnRC(task->m_autoCaller.rc());

    task->d.size = aLogicalSize;
    task->d.variant = aVariant;

    rc = task->startThread();
    if (FAILED(rc)) return rc;

    /* go to Creating state on success */
    m->state = MediumState_Creating;

    /* task is now owned by taskThread() so release it */
    task.release();

    /* return progress to the caller */
    progress.queryInterfaceTo(aProgress);

    return S_OK;
}

STDMETHODIMP Medium::DeleteStorage(IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    ComObjPtr <Progress> progress;

    HRESULT rc = deleteStorageNoWait(progress);
    if (SUCCEEDED(rc))
    {
        /* return progress to the caller */
        progress.queryInterfaceTo(aProgress);
    }

    return rc;
}

STDMETHODIMP Medium::CreateDiffStorage(IMedium *aTarget,
                                       MediumVariant_T aVariant,
                                       IProgress **aProgress)
{
    CheckComArgNotNull(aTarget);
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    ComObjPtr<Medium> diff = static_cast<Medium*>(aTarget);

    AutoWriteLock alock(this);

    if (m->type == MediumType_Writethrough)
        return setError(E_FAIL,
                        tr("Hard disk '%s' is Writethrough"),
                        m->strLocationFull.raw());

    /* We want to be locked for reading as long as our diff child is being
     * created */
    HRESULT rc = LockRead(NULL);
    if (FAILED(rc)) return rc;

    ComObjPtr <Progress> progress;

    rc = createDiffStorageNoWait(diff, aVariant, progress);
    if (FAILED(rc))
    {
        HRESULT rc2 = UnlockRead(NULL);
        AssertComRC(rc2);
        /* Note: on success, taskThread() will unlock this */
    }
    else
    {
        /* return progress to the caller */
        progress.queryInterfaceTo(aProgress);
    }

    return rc;
}

STDMETHODIMP Medium::MergeTo(IN_BSTR /* aTargetId */, IProgress ** /* aProgress */)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    ReturnComNotImplemented();
}

STDMETHODIMP Medium::CloneTo(IMedium *aTarget,
                              MediumVariant_T aVariant,
                              IMedium *aParent,
                              IProgress **aProgress)
{
    CheckComArgNotNull(aTarget);
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    ComObjPtr<Medium> target = static_cast<Medium*>(aTarget);
    ComObjPtr<Medium> parent;
    if (aParent)
        parent = static_cast<Medium*>(aParent);

    AutoMultiWriteLock3 alock(this, target, parent);

    ComObjPtr<Progress> progress;
    HRESULT rc = S_OK;

    try
    {
        if (    target->m->state != MediumState_NotCreated
            &&  target->m->state != MediumState_Created)
            throw target->setStateError();

        /** @todo separate out creating/locking an image chain from
         * SessionMachine::lockMedia and use it from here too.
         * logically this belongs into Medium functionality. */

        /* Build the source chain and lock images in the proper order. */
        std::auto_ptr <ImageChain> srcChain(new ImageChain());

        /* we walk the source tree */
        AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());
        for (Medium *hd = this;
             hd;
             hd = hd->m->pParent)
        {
            rc = srcChain->addImage(hd);
            if (FAILED(rc)) throw rc;
        }
        rc = srcChain->lockImagesRead();
        if (FAILED(rc)) throw rc;

        /* Build the parent chain and lock images in the proper order. */
        std::auto_ptr <ImageChain> parentChain(new ImageChain());

        for (Medium *hd = parent;
             hd;
             hd = hd->m->pParent)
        {
            rc = parentChain->addImage(hd);
            if (FAILED(rc)) throw rc;
        }
        if (target->m->state == MediumState_Created)
        {
            /* If we're cloning to an existing image the parent chain also
             * contains the target image, and it gets locked for writing. */
            rc = parentChain->addImage(target);
            if (FAILED(rc)) throw rc;
            rc = parentChain->lockImagesReadAndLastWrite();
            if (FAILED(rc)) throw rc;
        }
        else
        {
            rc = parentChain->lockImagesRead();
            if (FAILED(rc)) throw rc;
        }

        progress.createObject();
        rc = progress->init(m->pVirtualBox,
                            static_cast <IMedium *>(this),
                            BstrFmt(tr("Creating clone hard disk '%s'"), target->m->strLocationFull.raw()),
                            TRUE /* aCancelable */);
        if (FAILED(rc)) throw rc;

        /* setup task object and thread to carry out the operation
         * asynchronously */

        std::auto_ptr<Task> task(new Task(this, progress, Task::Clone));
        AssertComRCThrowRC(task->m_autoCaller.rc());

        task->setData(target, parent);
        task->d.variant = aVariant;
        task->setData(srcChain.release(), parentChain.release());

        rc = task->startThread();
        if (FAILED(rc)) throw rc;

        if (target->m->state == MediumState_NotCreated)
        {
            /* go to Creating state before leaving the lock */
            target->m->state = MediumState_Creating;
        }

        /* task is now owned (or already deleted) by taskThread() so release it */
        task.release();
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
    {
        /* return progress to the caller */
        progress.queryInterfaceTo(aProgress);
    }

    return rc;
}

STDMETHODIMP Medium::Compact(IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    ComObjPtr <Progress> progress;

    HRESULT rc = S_OK;

    try
    {
        /** @todo separate out creating/locking an image chain from
         * SessionMachine::lockMedia and use it from here too.
         * logically this belongs into Medium functionality. */

        /* Build the image chain and lock images in the proper order. */
        std::auto_ptr <ImageChain> imgChain(new ImageChain());

        /* we walk the image tree */
        AutoReadLock srcTreeLock(m->pVirtualBox->hardDiskTreeLockHandle());
        for (Medium *hd = this;
             hd;
             hd = hd->m->pParent)
        {
            rc = imgChain->addImage(hd);
            if (FAILED(rc)) throw rc;
        }
        rc = imgChain->lockImagesReadAndLastWrite();
        if (FAILED(rc)) throw rc;

        progress.createObject();
        rc = progress->init(m->pVirtualBox,
                            static_cast <IMedium *>(this),
                            BstrFmt(tr("Compacting hard disk '%s'"), m->strLocationFull.raw()),
                            TRUE /* aCancelable */);
        if (FAILED(rc)) throw rc;

        /* setup task object and thread to carry out the operation
         * asynchronously */

        std::auto_ptr <Task> task(new Task(this, progress, Task::Compact));
        AssertComRCThrowRC(task->m_autoCaller.rc());

        task->setData(imgChain.release());

        rc = task->startThread();
        if (FAILED(rc)) throw rc;

        /* task is now owned (or already deleted) by taskThread() so release it */
        task.release();
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (SUCCEEDED(rc))
    {
        /* return progress to the caller */
        progress.queryInterfaceTo(aProgress);
    }

    return rc;
}

STDMETHODIMP Medium::Resize(ULONG64 aLogicalSize, IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    NOREF(aLogicalSize);
    NOREF(aProgress);
    ReturnComNotImplemented();
}

STDMETHODIMP Medium::Reset(IProgress **aProgress)
{
    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    LogFlowThisFunc(("ENTER for medium %s\n", m->strLocationFull.c_str()));

    if (m->pParent.isNull())
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr ("Hard disk '%s' is not differencing"),
                        m->strLocationFull.raw());

    HRESULT rc = canClose();
    if (FAILED(rc)) return rc;

    rc = LockWrite(NULL);
    if (FAILED(rc)) return rc;

    ComObjPtr <Progress> progress;

    try
    {
        progress.createObject();
        rc = progress->init(m->pVirtualBox,
                            static_cast <IMedium *>(this),
                            BstrFmt(tr("Resetting differencing hard disk '%s'"), m->strLocationFull.raw()),
                            FALSE /* aCancelable */);
        if (FAILED(rc)) throw rc;

        /* setup task object and thread to carry out the operation
         * asynchronously */

        std::auto_ptr<Task> task(new Task(this, progress, Task::Reset));
        AssertComRCThrowRC(task->m_autoCaller.rc());

        rc = task->startThread();
        if (FAILED(rc)) throw rc;

        /* task is now owned (or already deleted) by taskThread() so release it */
        task.release();
    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    if (FAILED (rc))
    {
        HRESULT rc2 = UnlockWrite(NULL);
        AssertComRC(rc2);
        /* Note: on success, taskThread() will unlock this */
    }
    else
    {
        /* return progress to the caller */
        progress.queryInterfaceTo(aProgress);
    }

    LogFlowThisFunc(("LEAVE, rc=%Rhrc\n", rc));

    return rc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Medium internal methods
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Internal method to return the medium's parent medium. Must have caller + locking!
 * @return
 */
const ComObjPtr<Medium>& Medium::getParent() const
{
    return m->pParent;
}

/**
 * Internal method to return the medium's list of child media. Must have caller + locking!
 * @return
 */
const MediaList& Medium::getChildren() const
{
    return m->llChildren;
}

/**
 * Internal method to return the medium's GUID. Must have caller + locking!
 * @return
 */
const Guid& Medium::getId() const
{
    return m->id;
}

/**
 * Internal method to return the medium's GUID. Must have caller + locking!
 * @return
 */
MediumState_T Medium::getState() const
{
    return m->state;
}

/**
 * Internal method to return the medium's location. Must have caller + locking!
 * @return
 */
const Utf8Str& Medium::getLocation() const
{
    return m->strLocation;
}

/**
 * Internal method to return the medium's full location. Must have caller + locking!
 * @return
 */
const Utf8Str& Medium::getLocationFull() const
{
    return m->strLocationFull;
}

/**
 * Internal method to return the medium's size. Must have caller + locking!
 * @return
 */
uint64_t Medium::getSize() const
{
    return m->size;
}

/**
 * Adds the given machine and optionally the snapshot to the list of the objects
 * this image is attached to.
 *
 * @param aMachineId    Machine ID.
 * @param aSnapshotId   Snapshot ID; when non-empty, adds a snapshot attachment.
 */
HRESULT Medium::attachTo(const Guid &aMachineId,
                         const Guid &aSnapshotId /*= Guid::Empty*/)
{
    AssertReturn(!aMachineId.isEmpty(), E_FAIL);

    LogFlowThisFunc(("ENTER, aMachineId: {%RTuuid}, aSnapshotId: {%RTuuid}\n", aMachineId.raw(), aSnapshotId.raw()));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    AutoWriteLock alock(this);

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
        case MediumState_LockedRead:
        case MediumState_LockedWrite:
            break;

        default:
            return setStateError();
    }

    if (m->numCreateDiffTasks > 0)
        return setError(E_FAIL,
                        tr("Cannot attach hard disk '%s' {%RTuuid}: %u differencing child hard disk(s) are being created"),
                        m->strLocationFull.raw(),
                        m->id.raw(),
                        m->numCreateDiffTasks);

    BackRefList::iterator it =
        std::find_if(m->backRefs.begin(), m->backRefs.end(),
                     BackRef::EqualsTo(aMachineId));
    if (it == m->backRefs.end())
    {
        BackRef ref(aMachineId, aSnapshotId);
        m->backRefs.push_back(ref);

        return S_OK;
    }

    // if the caller has not supplied a snapshot ID, then we're attaching
    // to a machine a medium which represents the machine's current state,
    // so set the flag
    if (aSnapshotId.isEmpty())
    {
        /* sanity: no duplicate attachments */
        AssertReturn(!it->fInCurState, E_FAIL);
        it->fInCurState = true;

        return S_OK;
    }

    // otherwise: a snapshot medium is being attached

    /* sanity: no duplicate attachments */
    for (BackRef::GuidList::const_iterator jt = it->llSnapshotIds.begin();
         jt != it->llSnapshotIds.end();
         ++jt)
    {
        const Guid &idOldSnapshot = *jt;

        if (idOldSnapshot == aSnapshotId)
            return setError(E_FAIL,
                            tr("Cannot attach medium '%s' {%RTuuid} from snapshot '%RTuuid': medium is already in use by snapshot '%RTuuid'"),
                            m->strLocationFull.raw(),
                            m->id.raw(),
                            aSnapshotId.raw(),
                            idOldSnapshot.raw());
    }

    it->llSnapshotIds.push_back(aSnapshotId);
    it->fInCurState = false;

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 * Removes the given machine and optionally the snapshot from the list of the
 * objects this image is attached to.
 *
 * @param aMachineId    Machine ID.
 * @param aSnapshotId   Snapshot ID; when non-empty, removes the snapshot
 *                      attachment.
 */
HRESULT Medium::detachFrom(const Guid &aMachineId,
                           const Guid &aSnapshotId /*= Guid::Empty*/)
{
    AssertReturn(!aMachineId.isEmpty(), E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    AutoWriteLock alock(this);

    BackRefList::iterator it =
        std::find_if(m->backRefs.begin(), m->backRefs.end(),
                     BackRef::EqualsTo(aMachineId));
    AssertReturn(it != m->backRefs.end(), E_FAIL);

    if (aSnapshotId.isEmpty())
    {
        /* remove the current state attachment */
        it->fInCurState = false;
    }
    else
    {
        /* remove the snapshot attachment */
        BackRef::GuidList::iterator jt =
            std::find(it->llSnapshotIds.begin(), it->llSnapshotIds.end(), aSnapshotId);

        AssertReturn(jt != it->llSnapshotIds.end(), E_FAIL);
        it->llSnapshotIds.erase(jt);
    }

    /* if the backref becomes empty, remove it */
    if (it->fInCurState == false && it->llSnapshotIds.size() == 0)
        m->backRefs.erase(it);

    return S_OK;
}

/**
 * Internal method to return the medium's list of backrefs. Must have caller + locking!
 * @return
 */
const Guid* Medium::getFirstMachineBackrefId() const
{
    if (!m->backRefs.size())
        return NULL;

    return &m->backRefs.front().machineId;
}

const Guid* Medium::getFirstMachineBackrefSnapshotId() const
{
    if (!m->backRefs.size())
        return NULL;

    const BackRef &ref = m->backRefs.front();
    if (!ref.llSnapshotIds.size())
        return NULL;

    return &ref.llSnapshotIds.front();
}

#ifdef DEBUG
/**
 * Debugging helper that gets called after VirtualBox initialization that writes all
 * machine backreferences to the debug log.
 */
void Medium::dumpBackRefs()
{
    AutoCaller autoCaller(this);
    AutoReadLock(this);

    LogFlowThisFunc(("Dumping backrefs for medium '%s':\n", m->strLocationFull.raw()));

    for (BackRefList::iterator it2 = m->backRefs.begin();
         it2 != m->backRefs.end();
         ++it2)
    {
        const BackRef &ref = *it2;
        LogFlowThisFunc(("  Backref from machine {%RTuuid} (fInCurState: %d)\n", ref.machineId.raw(), ref.fInCurState));

        for (BackRef::GuidList::const_iterator jt2 = it2->llSnapshotIds.begin();
             jt2 != it2->llSnapshotIds.end();
             ++jt2)
        {
            const Guid &id = *jt2;
            LogFlowThisFunc(("  Backref from snapshot {%RTuuid}\n", id.raw()));
        }
    }
}
#endif

/**
 * Checks if the given change of \a aOldPath to \a aNewPath affects the location
 * of this media and updates it if necessary to reflect the new location.
 *
 * @param aOldPath  Old path (full).
 * @param aNewPath  New path (full).
 *
 * @note Locks this object for writing.
 */
HRESULT Medium::updatePath(const char *aOldPath, const char *aNewPath)
{
    AssertReturn(aOldPath, E_FAIL);
    AssertReturn(aNewPath, E_FAIL);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this);

    LogFlowThisFunc(("locationFull.before='%s'\n", m->strLocationFull.raw()));

    Utf8Str path = m->strLocationFull;

    if (RTPathStartsWith(path.c_str(), aOldPath))
    {
        Utf8Str newPath = Utf8StrFmt("%s%s", aNewPath,
                                     path.raw() + strlen(aOldPath));
        path = newPath;

        m->pVirtualBox->calculateRelativePath(path, path);

        unconst(m->strLocationFull) = newPath;
        unconst(m->strLocation) = path;

        LogFlowThisFunc(("locationFull.after='%s'\n", m->strLocationFull.raw()));
    }

    return S_OK;
}

/**
 * Checks if the given change of \a aOldPath to \a aNewPath affects the location
 * of this hard disk or any its child and updates the paths if necessary to
 * reflect the new location.
 *
 * @param aOldPath  Old path (full).
 * @param aNewPath  New path (full).
 *
 * @note Locks getTreeLock() for reading, this object and all children for writing.
 */
void Medium::updatePaths(const char *aOldPath, const char *aNewPath)
{
    AssertReturnVoid(aOldPath);
    AssertReturnVoid(aNewPath);

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.rc());

    AutoWriteLock alock(this);

    /* we access children() */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    updatePath(aOldPath, aNewPath);

    /* update paths of all children */
    for (MediaList::const_iterator it = getChildren().begin();
         it != getChildren().end();
         ++ it)
    {
        (*it)->updatePaths(aOldPath, aNewPath);
    }
}

/**
 * Returns the base hard disk of the hard disk chain this hard disk is part of.
 *
 * The base hard disk is found by walking up the parent-child relationship axis.
 * If the hard disk doesn't have a parent (i.e. it's a base hard disk), it
 * returns itself in response to this method.
 *
 * @param aLevel    Where to store the number of ancestors of this hard disk
 *                  (zero for the base), may be @c NULL.
 *
 * @note Locks getTreeLock() for reading.
 */
ComObjPtr<Medium> Medium::getBase(uint32_t *aLevel /*= NULL*/)
{
    ComObjPtr<Medium> pBase;
    uint32_t level;

    AutoCaller autoCaller(this);
    AssertReturn(autoCaller.isOk(), pBase);

    /* we access mParent */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    pBase = this;
    level = 0;

    if (m->pParent)
    {
        for (;;)
        {
            AutoCaller baseCaller(pBase);
            AssertReturn(baseCaller.isOk(), pBase);

            if (pBase->m->pParent.isNull())
                break;

            pBase = pBase->m->pParent;
            ++level;
        }
    }

    if (aLevel != NULL)
        *aLevel = level;

    return pBase;
}

/**
 * Returns @c true if this hard disk cannot be modified because it has
 * dependants (children) or is part of the snapshot. Related to the hard disk
 * type and posterity, not to the current media state.
 *
 * @note Locks this object and getTreeLock() for reading.
 */
bool Medium::isReadOnly()
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.rc(), false);

    AutoReadLock alock(this);

    /* we access children */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    switch (m->type)
    {
        case MediumType_Normal:
        {
            if (getChildren().size() != 0)
                return true;

            for (BackRefList::const_iterator it = m->backRefs.begin();
                 it != m->backRefs.end(); ++ it)
                if (it->llSnapshotIds.size() != 0)
                    return true;

            return false;
        }
        case MediumType_Immutable:
        {
            return true;
        }
        case MediumType_Writethrough:
        {
            return false;
        }
        default:
            break;
    }

    AssertFailedReturn(false);
}

/**
 * Saves hard disk data by appending a new <HardDisk> child node to the given
 * parent node which can be either <HardDisks> or <HardDisk>.
 *
 * @param data      Settings struct to be updated.
 *
 * @note Locks this object, getTreeLock() and children for reading.
 */
HRESULT Medium::saveSettings(settings::Medium &data)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this);

    /* we access mParent */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    data.uuid = m->id;
    data.strLocation = m->strLocation;
    data.strFormat = m->strFormat;

    /* optional, only for diffs, default is false */
    if (m->pParent)
        data.fAutoReset = !!m->autoReset;
    else
        data.fAutoReset = false;

    /* optional */
    data.strDescription = m->strDescription;

    /* optional properties */
    data.properties.clear();
    for (Data::PropertyMap::const_iterator it = m->properties.begin();
         it != m->properties.end();
         ++it)
    {
        /* only save properties that have non-default values */
        if (!it->second.isNull())
        {
            Utf8Str name = it->first;
            Utf8Str value = it->second;
            data.properties[name] = value;
        }
    }

    /* only for base hard disks */
    if (m->pParent.isNull())
        data.hdType = m->type;

    /* save all children */
    for (MediaList::const_iterator it = getChildren().begin();
         it != getChildren().end();
         ++it)
    {
        settings::Medium med;
        HRESULT rc = (*it)->saveSettings(med);
        AssertComRCReturnRC(rc);
        data.llChildren.push_back(med);
    }

    return S_OK;
}

/**
 * Compares the location of this hard disk to the given location.
 *
 * The comparison takes the location details into account. For example, if the
 * location is a file in the host's filesystem, a case insensitive comparison
 * will be performed for case insensitive filesystems.
 *
 * @param aLocation     Location to compare to (as is).
 * @param aResult       Where to store the result of comparison: 0 if locations
 *                      are equal, 1 if this object's location is greater than
 *                      the specified location, and -1 otherwise.
 */
HRESULT Medium::compareLocationTo(const char *aLocation, int &aResult)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    AutoReadLock alock(this);

    Utf8Str locationFull(m->strLocationFull);

    /// @todo NEWMEDIA delegate the comparison to the backend?

    if (m->formatObj->capabilities() & MediumFormatCapabilities_File)
    {
        Utf8Str location(aLocation);

        /* For locations represented by files, append the default path if
         * only the name is given, and then get the full path. */
        if (!RTPathHavePath(aLocation))
        {
            location = Utf8StrFmt("%s%c%s",
                                  m->pVirtualBox->getDefaultHardDiskFolder().raw(),
                                  RTPATH_DELIMITER,
                                  aLocation);
        }

        int vrc = m->pVirtualBox->calculateFullPath(location, location);
        if (RT_FAILURE(vrc))
            return setError(E_FAIL,
                            tr("Invalid hard disk storage file location '%s' (%Rrc)"),
                            location.raw(),
                            vrc);

        aResult = RTPathCompare(locationFull.c_str(), location.c_str());
    }
    else
        aResult = locationFull.compare(aLocation);

    return S_OK;
}

/**
 * Checks that this hard disk may be discarded and performs necessary state
 * changes.
 *
 * This method is to be called prior to calling the #discard() to perform
 * necessary consistency checks and place involved hard disks to appropriate
 * states. If #discard() is not called or fails, the state modifications
 * performed by this method must be undone by #cancelDiscard().
 *
 * See #discard() for more info about discarding hard disks.
 *
 * @param aChain        Where to store the created merge chain (may return NULL
 *                      if no real merge is necessary).
 *
 * @note Locks getTreeLock() for reading. Locks this object, aTarget and all
 *       intermediate hard disks for writing.
 */
HRESULT Medium::prepareDiscard(MergeChain * &aChain)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    aChain = NULL;

    AutoWriteLock alock(this);

    /* we access mParent & children() */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    AssertReturn(m->type == MediumType_Normal, E_FAIL);

    if (getChildren().size() == 0)
    {
        /* special treatment of the last hard disk in the chain: */

        if (m->pParent.isNull())
        {
            /* lock only, to prevent any usage; discard() will unlock */
            return LockWrite(NULL);
        }

        /* the differencing hard disk w/o children will be deleted, protect it
         * from attaching to other VMs (this is why Deleting) */

        switch (m->state)
        {
            case MediumState_Created:
                m->state = MediumState_Deleting;
                break;
            default:
                return setStateError();
        }

        /* aChain is intentionally NULL here */

        return S_OK;
    }

    /* not going multi-merge as it's too expensive */
    if (getChildren().size() > 1)
        return setError(E_FAIL,
                        tr ("Hard disk '%s' has more than one child hard disk (%d)"),
                        m->strLocationFull.raw(), getChildren().size());

    /* this is a read-only hard disk with children; it must be associated with
     * exactly one snapshot (when the snapshot is being taken, none of the
     * current VM's hard disks may be attached to other VMs). Note that by the
     * time when discard() is called, there must be no any attachments at all
     * (the code calling prepareDiscard() should detach). */
    AssertReturn(m->backRefs.size() == 1, E_FAIL);
    AssertReturn(!m->backRefs.front().fInCurState, E_FAIL);
    AssertReturn(m->backRefs.front().llSnapshotIds.size() == 1, E_FAIL);

    ComObjPtr<Medium> child = getChildren().front();

    /* we keep this locked, so lock the affected child to make sure the lock
     * order is correct when calling prepareMergeTo() */
    AutoWriteLock childLock(child);

    /* delegate the rest to the profi */
    if (m->pParent.isNull())
    {
        /* base hard disk, backward merge */
        Assert(child->m->backRefs.size() == 1);
        if (child->m->backRefs.front().machineId != m->backRefs.front().machineId)
        {
            /* backward merge is too tricky, we'll just detach on discard, so
             * lock only, to prevent any usage; discard() will only unlock
             * (since we return NULL in aChain) */
            return LockWrite(NULL);
        }

        return child->prepareMergeTo(this, aChain, true /* aIgnoreAttachments */);
    }
    else
    {
        /* forward merge */
        return prepareMergeTo(child, aChain, true /* aIgnoreAttachments */);
    }
}

/**
 * Discards this hard disk.
 *
 * Discarding the hard disk is merging its contents to its differencing child
 * hard disk (forward merge) or contents of its child hard disk to itself
 * (backward merge) if this hard disk is a base hard disk. If this hard disk is
 * a differencing hard disk w/o children, then it will be simply deleted.
 * Calling this method on a base hard disk w/o children will do nothing and
 * silently succeed. If this hard disk has more than one child, the method will
 * currently return an error (since merging in this case would be too expensive
 * and result in data duplication).
 *
 * When the backward merge takes place (i.e. this hard disk is a target) then,
 * on success, this hard disk will automatically replace the differencing child
 * hard disk used as a source (which will then be deleted) in the attachment
 * this child hard disk is associated with. This will happen only if both hard
 * disks belong to the same machine because otherwise such a replace would be
 * too tricky and could be not expected by the other machine. Same relates to a
 * case when the child hard disk is not associated with any machine at all. When
 * the backward merge is not applied, the method behaves as if the base hard
 * disk were not attached at all -- i.e. simply detaches it from the machine but
 * leaves the hard disk chain intact.
 *
 * This method is basically a wrapper around #mergeTo() that selects the correct
 * merge direction and performs additional actions as described above and.
 *
 * Note that this method will not return until the merge operation is complete
 * (which may be quite time consuming depending on the size of the merged hard
 * disks).
 *
 * Note that #prepareDiscard() must be called before calling this method. If
 * this method returns a failure, the caller must call #cancelDiscard(). On
 * success, #cancelDiscard() must not be called (this method will perform all
 * necessary steps such as resetting states of all involved hard disks and
 * deleting @a aChain).
 *
 * @param aChain        Merge chain created by #prepareDiscard() (may be NULL if
 *                      no real merge takes place).
 *
 * @note Locks the hard disks from the chain for writing. Locks the machine
 *       object when the backward merge takes place. Locks getTreeLock() lock for
 *       reading or writing.
 */
HRESULT Medium::discard(ComObjPtr<Progress> &aProgress, ULONG ulWeight, MergeChain *aChain)
{
    AssertReturn(!aProgress.isNull(), E_FAIL);

    ComObjPtr <Medium> hdFrom;

    HRESULT rc = S_OK;

    {
        AutoCaller autoCaller(this);
        AssertComRCReturnRC(autoCaller.rc());

        aProgress->SetNextOperation(BstrFmt(tr("Discarding hard disk '%s'"), getName().raw()),
                                    ulWeight);        // weight

        if (aChain == NULL)
        {
            AutoWriteLock alock(this);

            /* we access mParent & children() */
            AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

            Assert(getChildren().size() == 0);

            /* special treatment of the last hard disk in the chain: */

            if (m->pParent.isNull())
            {
                rc = UnlockWrite(NULL);
                AssertComRC(rc);
                return rc;
            }

            /* delete the differencing hard disk w/o children */

            Assert(m->state == MediumState_Deleting);

            /* go back to Created since deleteStorage() expects this state */
            m->state = MediumState_Created;

            hdFrom = this;

            // deleteStorageAndWait calls unregisterWithVirtualBox which gets
            // a write tree lock, so don't deadlock
            treeLock.unlock();
            alock.unlock();

            rc = deleteStorageAndWait(&aProgress);
        }
        else
        {
            hdFrom = aChain->source();

            rc = hdFrom->mergeToAndWait(aChain, &aProgress);
        }
    }

    if (SUCCEEDED(rc))
    {
        /* mergeToAndWait() cannot uninitialize the initiator because of
         * possible AutoCallers on the current thread, deleteStorageAndWait()
         * doesn't do it either; do it ourselves */
        hdFrom->uninit();
    }

    return rc;
}

/**
 * Undoes what #prepareDiscard() did. Must be called if #discard() is not called
 * or fails. Frees memory occupied by @a aChain.
 *
 * @param aChain        Merge chain created by #prepareDiscard() (may be NULL if
 *                      no real merge takes place).
 *
 * @note Locks the hard disks from the chain for writing. Locks getTreeLock() for
 *       reading.
 */
void Medium::cancelDiscard(MergeChain *aChain)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.rc());

    if (aChain == NULL)
    {
        AutoWriteLock alock(this);

        /* we access mParent & children() */
        AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

        Assert(getChildren().size() == 0);

        /* special treatment of the last hard disk in the chain: */

        if (m->pParent.isNull())
        {
            HRESULT rc = UnlockWrite(NULL);
            AssertComRC(rc);
            return;
        }

        /* the differencing hard disk w/o children will be deleted, protect it
         * from attaching to other VMs (this is why Deleting) */

        Assert(m->state == MediumState_Deleting);
        m->state = MediumState_Created;

        return;
    }

    /* delegate the rest to the profi */
    cancelMergeTo(aChain);
}

/**
 * Returns a preferred format for differencing hard disks.
 */
Bstr Medium::preferredDiffFormat()
{
    Utf8Str strFormat;

    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.rc(), strFormat);

    /* m->format is const, no need to lock */
    strFormat = m->strFormat;

    /* check that our own format supports diffs */
    if (!(m->formatObj->capabilities() & MediumFormatCapabilities_Differencing))
    {
        /* use the default format if not */
        AutoReadLock propsLock(m->pVirtualBox->systemProperties());
        strFormat = m->pVirtualBox->getDefaultHardDiskFormat();
    }

    return strFormat;
}

/**
 * Returns the medium type. Must have caller + locking!
 * @return
 */
MediumType_T Medium::getType() const
{
    return m->type;
}

// private methods
////////////////////////////////////////////////////////////////////////////////

/**
 * Returns a short version of the location attribute.
 *
 * @note Must be called from under this object's read or write lock.
 */
Utf8Str Medium::getName()
{
    Utf8Str name = RTPathFilename(m->strLocationFull.c_str());
    return name;
}

/**
 * Sets the value of m->strLocation and calculates the value of m->strLocationFull.
 *
 * Treats non-FS-path locations specially, and prepends the default hard disk
 * folder if the given location string does not contain any path information
 * at all.
 *
 * Also, if the specified location is a file path that ends with '/' then the
 * file name part will be generated by this method automatically in the format
 * '{<uuid>}.<ext>' where <uuid> is a fresh UUID that this method will generate
 * and assign to this medium, and <ext> is the default extension for this
 * medium's storage format. Note that this procedure requires the media state to
 * be NotCreated and will return a failure otherwise.
 *
 * @param aLocation Location of the storage unit. If the location is a FS-path,
 *                  then it can be relative to the VirtualBox home directory.
 * @param aFormat   Optional fallback format if it is an import and the format
 *                  cannot be determined.
 *
 * @note Must be called from under this object's write lock.
 */
HRESULT Medium::setLocation(const Utf8Str &aLocation, const Utf8Str &aFormat)
{
    AssertReturn(!aLocation.isEmpty(), E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    /* formatObj may be null only when initializing from an existing path and
     * no format is known yet */
    AssertReturn(    (!m->strFormat.isEmpty() && !m->formatObj.isNull())
                  || (    autoCaller.state() == InInit
                       && m->state != MediumState_NotCreated
                       && m->id.isEmpty()
                       && m->strFormat.isEmpty()
                       && m->formatObj.isNull()),
                 E_FAIL);

    /* are we dealing with a new medium constructed using the existing
     * location? */
    bool isImport = m->strFormat.isEmpty();

    if (   isImport
        || (   (m->formatObj->capabilities() & MediumFormatCapabilities_File)
            && !m->hostDrive))
    {
        Guid id;

        Utf8Str location(aLocation);

        if (m->state == MediumState_NotCreated)
        {
            /* must be a file (formatObj must be already known) */
            Assert(m->formatObj->capabilities() & MediumFormatCapabilities_File);

            if (RTPathFilename(location.c_str()) == NULL)
            {
                /* no file name is given (either an empty string or ends with a
                 * slash), generate a new UUID + file name if the state allows
                 * this */

                ComAssertMsgRet(!m->formatObj->fileExtensions().empty(),
                                ("Must be at least one extension if it is MediumFormatCapabilities_File\n"),
                                E_FAIL);

                Bstr ext = m->formatObj->fileExtensions().front();
                ComAssertMsgRet(!ext.isEmpty(),
                                ("Default extension must not be empty\n"),
                                E_FAIL);

                id.create();

                location = Utf8StrFmt("%s{%RTuuid}.%ls",
                                      location.raw(), id.raw(), ext.raw());
            }
        }

        /* append the default folder if no path is given */
        if (!RTPathHavePath(location.c_str()))
            location = Utf8StrFmt("%s%c%s",
                                  m->pVirtualBox->getDefaultHardDiskFolder().raw(),
                                  RTPATH_DELIMITER,
                                  location.raw());

        /* get the full file name */
        Utf8Str locationFull;
        int vrc = m->pVirtualBox->calculateFullPath(location, locationFull);
        if (RT_FAILURE(vrc))
            return setError(VBOX_E_FILE_ERROR,
                            tr("Invalid medium storage file location '%s' (%Rrc)"),
                            location.raw(), vrc);

        /* detect the backend from the storage unit if importing */
        if (isImport)
        {
            char *backendName = NULL;

            /* is it a file? */
            {
                RTFILE file;
                vrc = RTFileOpen(&file, locationFull.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
                if (RT_SUCCESS(vrc))
                    RTFileClose(file);
            }
            if (RT_SUCCESS(vrc))
            {
                vrc = VDGetFormat(NULL, locationFull.c_str(), &backendName);
            }
            else if (vrc != VERR_FILE_NOT_FOUND && vrc != VERR_PATH_NOT_FOUND)
            {
                /* assume it's not a file, restore the original location */
                location = locationFull = aLocation;
                vrc = VDGetFormat(NULL, locationFull.c_str(), &backendName);
            }

            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
                    return setError(VBOX_E_FILE_ERROR,
                                    tr("Could not find file for the medium '%s' (%Rrc)"),
                                    locationFull.raw(), vrc);
                else if (aFormat.isEmpty())
                    return setError(VBOX_E_IPRT_ERROR,
                                    tr("Could not get the storage format of the medium '%s' (%Rrc)"),
                                    locationFull.raw(), vrc);
                else
                {
                    HRESULT rc = setFormat(Bstr(aFormat));
                    /* setFormat() must not fail since we've just used the backend so
                     * the format object must be there */
                    AssertComRCReturnRC(rc);
                }
            }
            else
            {
                ComAssertRet(backendName != NULL && *backendName != '\0', E_FAIL);

                HRESULT rc = setFormat(Bstr(backendName));
                RTStrFree(backendName);

                /* setFormat() must not fail since we've just used the backend so
                 * the format object must be there */
                AssertComRCReturnRC(rc);
            }
        }

        /* is it still a file? */
        if (m->formatObj->capabilities() & MediumFormatCapabilities_File)
        {
            m->strLocation = location;
            m->strLocationFull = locationFull;

            if (m->state == MediumState_NotCreated)
            {
                /* assign a new UUID (this UUID will be used when calling
                 * VDCreateBase/VDCreateDiff as a wanted UUID). Note that we
                 * also do that if we didn't generate it to make sure it is
                 * either generated by us or reset to null */
                unconst(m->id) = id;
            }
        }
        else
        {
            m->strLocation = locationFull;
            m->strLocationFull = locationFull;
        }
    }
    else
    {
        m->strLocation = aLocation;
        m->strLocationFull = aLocation;
    }

    return S_OK;
}

/**
 * Queries information from the image file.
 *
 * As a result of this call, the accessibility state and data members such as
 * size and description will be updated with the current information.
 *
 * @note This method may block during a system I/O call that checks storage
 *       accessibility.
 *
 * @note Locks getTreeLock() for reading and writing (for new diff media checked
 *       for the first time). Locks mParent for reading. Locks this object for
 *       writing.
 */
HRESULT Medium::queryInfo()
{
    AutoWriteLock alock(this);

    if (   m->state != MediumState_Created
        && m->state != MediumState_Inaccessible
        && m->state != MediumState_LockedRead)
        return E_FAIL;

    HRESULT rc = S_OK;

    int vrc = VINF_SUCCESS;

    /* check if a blocking queryInfo() call is in progress on some other thread,
     * and wait for it to finish if so instead of querying data ourselves */
    if (m->queryInfoRunning)
    {
        Assert(   m->state == MediumState_LockedRead
               || m->state == MediumState_LockedWrite);

        alock.leave();

        vrc = RTSemEventMultiWait(m->queryInfoSem, RT_INDEFINITE_WAIT);

        alock.enter();

        AssertRC(vrc);

        return S_OK;
    }

    bool success = false;
    Utf8Str lastAccessError;

    /* are we dealing with a new medium constructed using the existing
     * location? */
    bool isImport = m->id.isEmpty();
    unsigned flags = VD_OPEN_FLAGS_INFO;

    /* Note that we don't use VD_OPEN_FLAGS_READONLY when opening new
     * media because that would prevent necessary modifications
     * when opening media of some third-party formats for the first
     * time in VirtualBox (such as VMDK for which VDOpen() needs to
     * generate an UUID if it is missing) */
    if (    (m->hddOpenMode == OpenReadOnly)
         || !isImport
       )
        flags |= VD_OPEN_FLAGS_READONLY;

    /* Lock the medium, which makes the behavior much more consistent */
    if (flags & VD_OPEN_FLAGS_READONLY)
        rc = LockRead(NULL);
    else
        rc = LockWrite(NULL);
    if (FAILED(rc)) return rc;

    /* Copies of the input state fields which are not read-only,
     * as we're dropping the lock. CAUTION: be extremely careful what
     * you do with the contents of this medium object, as you will
     * create races if there are concurrent changes. */
    Utf8Str format(m->strFormat);
    Utf8Str location(m->strLocationFull);
    ComObjPtr<MediumFormat> formatObj = m->formatObj;

    /* "Output" values which can't be set because the lock isn't held
     * at the time the values are determined. */
    Guid mediumId = m->id;
    uint64_t mediumSize = 0;
    uint64_t mediumLogicalSize = 0;

    /* leave the lock before a lengthy operation */
    vrc = RTSemEventMultiReset(m->queryInfoSem);
    ComAssertRCThrow(vrc, E_FAIL);
    m->queryInfoRunning = true;
    alock.leave();

    try
    {
        /* skip accessibility checks for host drives */
        if (m->hostDrive)
        {
            success = true;
            throw S_OK;
        }

        PVBOXHDD hdd;
        vrc = VDCreate(m->vdDiskIfaces, &hdd);
        ComAssertRCThrow(vrc, E_FAIL);

        try
        {
            /** @todo This kind of opening of images is assuming that diff
             * images can be opened as base images. Should be fixed ASAP. */
            vrc = VDOpen(hdd,
                         format.c_str(),
                         location.c_str(),
                         flags,
                         m->vdDiskIfaces);
            if (RT_FAILURE(vrc))
            {
                lastAccessError = Utf8StrFmt(tr("Could not open the medium '%s'%s"),
                                             location.c_str(), vdError(vrc).c_str());
                throw S_OK;
            }

            if (formatObj->capabilities() & MediumFormatCapabilities_Uuid)
            {
                /* Modify the UUIDs if necessary. The associated fields are
                 * not modified by other code, so no need to copy. */
                if (m->setImageId)
                {
                    vrc = VDSetUuid(hdd, 0, m->imageId);
                    ComAssertRCThrow(vrc, E_FAIL);
                }
                if (m->setParentId)
                {
                    vrc = VDSetParentUuid(hdd, 0, m->parentId);
                    ComAssertRCThrow(vrc, E_FAIL);
                }
                /* zap the information, these are no long-term members */
                m->setImageId = false;
                unconst(m->imageId).clear();
                m->setParentId = false;
                unconst(m->parentId).clear();

                /* check the UUID */
                RTUUID uuid;
                vrc = VDGetUuid(hdd, 0, &uuid);
                ComAssertRCThrow(vrc, E_FAIL);

                if (isImport)
                {
                    mediumId = uuid;

                    if (mediumId.isEmpty() && (m->hddOpenMode == OpenReadOnly))
                        // only when importing a VDMK that has no UUID, create one in memory
                        mediumId.create();
                }
                else
                {
                    Assert(!mediumId.isEmpty());

                    if (mediumId != uuid)
                    {
                        lastAccessError = Utf8StrFmt(
                            tr("UUID {%RTuuid} of the medium '%s' does not match the value {%RTuuid} stored in the media registry ('%s')"),
                            &uuid,
                            location.c_str(),
                            mediumId.raw(),
                            m->pVirtualBox->settingsFilePath().c_str());
                        throw S_OK;
                    }
                }
            }
            else
            {
                /* the backend does not support storing UUIDs within the
                 * underlying storage so use what we store in XML */

                /* generate an UUID for an imported UUID-less medium */
                if (isImport)
                {
                    if (m->setImageId)
                        mediumId = m->imageId;
                    else
                        mediumId.create();
                }
            }

            /* check the type */
            unsigned uImageFlags;
            vrc = VDGetImageFlags(hdd, 0, &uImageFlags);
            ComAssertRCThrow(vrc, E_FAIL);

            if (uImageFlags & VD_IMAGE_FLAGS_DIFF)
            {
                RTUUID parentId;
                vrc = VDGetParentUuid(hdd, 0, &parentId);
                ComAssertRCThrow(vrc, E_FAIL);

                if (isImport)
                {
                    /* the parent must be known to us. Note that we freely
                     * call locking methods of mVirtualBox and parent from the
                     * write lock (breaking the {parent,child} lock order)
                     * because there may be no concurrent access to the just
                     * opened hard disk on ther threads yet (and init() will
                     * fail if this method reporst MediumState_Inaccessible) */

                    Guid id = parentId;
                    ComObjPtr<Medium> parent;
                    rc = m->pVirtualBox->findHardDisk(&id, NULL,
                                                   false /* aSetError */,
                                                   &parent);
                    if (FAILED(rc))
                    {
                        lastAccessError = Utf8StrFmt(
                            tr("Parent hard disk with UUID {%RTuuid} of the hard disk '%s' is not found in the media registry ('%s')"),
                            &parentId, location.c_str(),
                            m->pVirtualBox->settingsFilePath().c_str());
                        throw S_OK;
                    }

                    /* we set mParent & children() */
                    AutoWriteLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

                    Assert(m->pParent.isNull());
                    m->pParent = parent;
                    m->pParent->m->llChildren.push_back(this);
                }
                else
                {
                    /* we access mParent */
                    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

                    /* check that parent UUIDs match. Note that there's no need
                     * for the parent's AutoCaller (our lifetime is bound to
                     * it) */

                    if (m->pParent.isNull())
                    {
                        lastAccessError = Utf8StrFmt(
                            tr("Hard disk '%s' is differencing but it is not associated with any parent hard disk in the media registry ('%s')"),
                            location.c_str(),
                            m->pVirtualBox->settingsFilePath().c_str());
                        throw S_OK;
                    }

                    AutoReadLock parentLock(m->pParent);
                    if (    m->pParent->getState() != MediumState_Inaccessible
                         && m->pParent->getId() != parentId)
                    {
                        lastAccessError = Utf8StrFmt(
                            tr("Parent UUID {%RTuuid} of the hard disk '%s' does not match UUID {%RTuuid} of its parent hard disk stored in the media registry ('%s')"),
                            &parentId, location.c_str(),
                            m->pParent->getId().raw(),
                            m->pVirtualBox->settingsFilePath().c_str());
                        throw S_OK;
                    }

                    /// @todo NEWMEDIA what to do if the parent is not
                    /// accessible while the diff is? Probably, nothing. The
                    /// real code will detect the mismatch anyway.
                }
            }

            mediumSize = VDGetFileSize(hdd, 0);
            mediumLogicalSize = VDGetSize(hdd, 0) / _1M;

            success = true;
        }
        catch (HRESULT aRC)
        {
            rc = aRC;
        }

        VDDestroy(hdd);

    }
    catch (HRESULT aRC)
    {
        rc = aRC;
    }

    alock.enter();

    if (isImport)
        unconst(m->id) = mediumId;

    if (success)
    {
        m->size = mediumSize;
        m->logicalSize = mediumLogicalSize;
        m->strLastAccessError.setNull();
    }
    else
    {
        m->strLastAccessError = lastAccessError;
        LogWarningFunc(("'%s' is not accessible (error='%s', rc=%Rhrc, vrc=%Rrc)\n",
                         location.c_str(), m->strLastAccessError.c_str(),
                         rc, vrc));
    }

    /* inform other callers if there are any */
    RTSemEventMultiSignal(m->queryInfoSem);
    m->queryInfoRunning = false;

    /* Set the proper state according to the result of the check */
    if (success)
        m->preLockState = MediumState_Created;
    else
        m->preLockState = MediumState_Inaccessible;

    if (flags & VD_OPEN_FLAGS_READONLY)
        rc = UnlockRead(NULL);
    else
        rc = UnlockWrite(NULL);
    if (FAILED(rc)) return rc;

    return rc;
}

/**
 * Sets the extended error info according to the current media state.
 *
 * @note Must be called from under this object's write or read lock.
 */
HRESULT Medium::setStateError()
{
    HRESULT rc = E_FAIL;

    switch (m->state)
    {
        case MediumState_NotCreated:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Storage for the medium '%s' is not created"),
                          m->strLocationFull.raw());
            break;
        }
        case MediumState_Created:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Storage for the medium '%s' is already created"),
                          m->strLocationFull.raw());
            break;
        }
        case MediumState_LockedRead:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Medium '%s' is locked for reading by another task"),
                          m->strLocationFull.raw());
            break;
        }
        case MediumState_LockedWrite:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Medium '%s' is locked for writing by another task"),
                          m->strLocationFull.raw());
            break;
        }
        case MediumState_Inaccessible:
        {
            /* be in sync with Console::powerUpThread() */
            if (!m->strLastAccessError.isEmpty())
                rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                              tr("Medium '%s' is not accessible. %s"),
                              m->strLocationFull.raw(), m->strLastAccessError.c_str());
            else
                rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                              tr("Medium '%s' is not accessible"),
                              m->strLocationFull.raw());
            break;
        }
        case MediumState_Creating:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Storage for the medium '%s' is being created"),
                          m->strLocationFull.raw());
            break;
        }
        case MediumState_Deleting:
        {
            rc = setError(VBOX_E_INVALID_OBJECT_STATE,
                          tr("Storage for the medium '%s' is being deleted"),
                          m->strLocationFull.raw());
            break;
        }
        default:
        {
            AssertFailed();
            break;
        }
    }

    return rc;
}

/**
 * Deletes the hard disk storage unit.
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If Progress is NULL, then no
 * progress object is created/used at all.
 *
 * When @a aWait is @c false, this method will create a thread to perform the
 * delete operation asynchronously and will return immediately. Otherwise, it
 * will perform the operation on the calling thread and will not return to the
 * caller until the operation is completed. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 *
 * @note Locks mVirtualBox and this object for writing. Locks getTreeLock() for
 *       writing.
 */
HRESULT Medium::deleteStorage(ComObjPtr <Progress> *aProgress, bool aWait)
{
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    /* unregisterWithVirtualBox() needs a write lock. We want to unregister
     * ourselves atomically after detecting that deletion is possible to make
     * sure that we don't do that after another thread has done
     * VirtualBox::findHardDisk() but before it starts using us (provided that
     * it holds a mVirtualBox lock too of course). */

    AutoMultiWriteLock2 alock(m->pVirtualBox->lockHandle(), this->lockHandle());
    LogFlowThisFunc(("aWait=%RTbool locationFull=%s\n", aWait, getLocationFull().c_str() ));

    if (    !(m->formatObj->capabilities() & (   MediumFormatCapabilities_CreateDynamic
                                               | MediumFormatCapabilities_CreateFixed)))
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Hard disk format '%s' does not support storage deletion"),
                        m->strFormat.raw());

    /* Note that we are fine with Inaccessible state too: a) for symmetry with
     * create calls and b) because it doesn't really harm to try, if it is
     * really inaccessible, the delete operation will fail anyway. Accepting
     * Inaccessible state is especially important because all registered hard
     * disks are initially Inaccessible upon VBoxSVC startup until
     * COMGETTER(State) is called. */

    switch (m->state)
    {
        case MediumState_Created:
        case MediumState_Inaccessible:
            break;
        default:
            return setStateError();
    }

    if (m->backRefs.size() != 0)
    {
        Utf8Str strMachines;
        for (BackRefList::const_iterator it = m->backRefs.begin();
            it != m->backRefs.end();
            ++it)
        {
            const BackRef &b = *it;
            if (strMachines.length())
                strMachines.append(", ");
            strMachines.append(b.machineId.toString().c_str());
        }
#ifdef DEBUG
        dumpBackRefs();
#endif
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot delete storage: hard disk '%s' is still attached to the following %d virtual machine(s): %s"),
                        m->strLocationFull.c_str(),
                        m->backRefs.size(),
                        strMachines.c_str());
    }

    HRESULT rc = canClose();
    if (FAILED(rc)) return rc;

    /* go to Deleting state before leaving the lock */
    m->state = MediumState_Deleting;

    /* we need to leave this object's write lock now because of
     * unregisterWithVirtualBox() that locks getTreeLock() for writing */
    alock.leave();

    /* try to remove from the list of known hard disks before performing actual
     * deletion (we favor the consistency of the media registry in the first
     * place which would have been broken if unregisterWithVirtualBox() failed
     * after we successfully deleted the storage) */

    rc = unregisterWithVirtualBox();

    alock.enter();

    /* restore the state because we may fail below; we will set it later again*/
    m->state = MediumState_Created;

    if (FAILED(rc)) return rc;

    ComObjPtr <Progress> progress;

    if (aProgress != NULL)
    {
        /* use the existing progress object... */
        progress = *aProgress;

        /* ...but create a new one if it is null */
        if (progress.isNull())
        {
            progress.createObject();
            rc = progress->init(m->pVirtualBox,
                                static_cast<IMedium*>(this),
                                BstrFmt(tr("Deleting hard disk storage unit '%s'"), m->strLocationFull.raw()),
                                FALSE /* aCancelable */);
            if (FAILED(rc)) return rc;
        }
    }

    std::auto_ptr <Task> task(new Task(this, progress, Task::Delete));
    AssertComRCReturnRC(task->m_autoCaller.rc());

    if (aWait)
    {
        /* go to Deleting state before starting the task */
        m->state = MediumState_Deleting;

        rc = task->runNow();
    }
    else
    {
        rc = task->startThread();
        if (FAILED(rc)) return rc;

        /* go to Deleting state before leaving the lock */
        m->state = MediumState_Deleting;
    }

    /* task is now owned (or already deleted) by taskThread() so release it */
    task.release();

    if (aProgress != NULL)
    {
        /* return progress to the caller */
        *aProgress = progress;
    }

    return rc;
}

/**
 * Creates a new differencing storage unit using the given target hard disk's
 * format and the location. Note that @c aTarget must be NotCreated.
 *
 * As opposed to the CreateDiffStorage() method, this method doesn't try to lock
 * this hard disk for reading assuming that the caller has already done so. This
 * is used when taking an online snaopshot (where all original hard disks are
 * locked for writing and must remain such). Note however that if @a aWait is
 * @c false and this method returns a success then the thread started by
 * this method will unlock the hard disk (unless it is in
 * MediumState_LockedWrite state) so make sure the hard disk is either in
 * MediumState_LockedWrite or call #LockRead() before calling this method! If @a
 * aWait is @c true then this method neither locks nor unlocks the hard disk, so
 * make sure you do it yourself as needed.
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If @a aProgress is NULL, then no
 * progress object is created/used at all.
 *
 * When @a aWait is @c false, this method will create a thread to perform the
 * create operation asynchronously and will return immediately. Otherwise, it
 * will perform the operation on the calling thread and will not return to the
 * caller until the operation is completed. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aTarget       Target hard disk.
 * @param aVariant      Precise image variant to create.
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 *
 * @note Locks this object and @a aTarget for writing.
 */
HRESULT Medium::createDiffStorage(ComObjPtr<Medium> &aTarget,
                                  MediumVariant_T aVariant,
                                  ComObjPtr<Progress> *aProgress,
                                  bool aWait)
{
    AssertReturn(!aTarget.isNull(), E_FAIL);
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoCaller targetCaller(aTarget);
    if (FAILED(targetCaller.rc())) return targetCaller.rc();

    AutoMultiWriteLock2 alock(this, aTarget);

    AssertReturn(m->type != MediumType_Writethrough, E_FAIL);

    /* Note: MediumState_LockedWrite is ok when taking an online snapshot */
    AssertReturn(m->state == MediumState_LockedRead ||
                  m->state == MediumState_LockedWrite, E_FAIL);

    if (aTarget->m->state != MediumState_NotCreated)
        return aTarget->setStateError();

    HRESULT rc = S_OK;

    /* check that the hard disk is not attached to any VM in the current state*/
    for (BackRefList::const_iterator it = m->backRefs.begin();
         it != m->backRefs.end(); ++ it)
    {
        if (it->fInCurState)
        {
            /* Note: when a VM snapshot is being taken, all normal hard disks
             * attached to the VM in the current state will be, as an exception,
             * also associated with the snapshot which is about to create  (see
             * SnapshotMachine::init()) before deassociating them from the
             * current state (which takes place only on success in
             * Machine::fixupHardDisks()), so that the size of snapshotIds
             * will be 1 in this case. The given condition is used to filter out
             * this legal situatinon and do not report an error. */

            if (it->llSnapshotIds.size() == 0)
            {
                return setError(VBOX_E_INVALID_OBJECT_STATE,
                                tr("Hard disk '%s' is attached to a virtual machine with UUID {%RTuuid}. No differencing hard disks based on it may be created until it is detached"),
                                m->strLocationFull.raw(), it->machineId.raw());
            }

            Assert(it->llSnapshotIds.size() == 1);
        }
    }

    ComObjPtr <Progress> progress;

    if (aProgress != NULL)
    {
        /* use the existing progress object... */
        progress = *aProgress;

        /* ...but create a new one if it is null */
        if (progress.isNull())
        {
            progress.createObject();
            rc = progress->init(m->pVirtualBox,
                                static_cast<IMedium*>(this),
                                BstrFmt(tr("Creating differencing hard disk storage unit '%s'"), aTarget->m->strLocationFull.raw()),
                                TRUE /* aCancelable */);
            if (FAILED(rc)) return rc;
        }
    }

    /* setup task object and thread to carry out the operation
     * asynchronously */

    std::auto_ptr <Task> task(new Task(this, progress, Task::CreateDiff));
    AssertComRCReturnRC(task->m_autoCaller.rc());

    task->setData(aTarget);
    task->d.variant = aVariant;

    /* register a task (it will deregister itself when done) */
    ++m->numCreateDiffTasks;
    Assert(m->numCreateDiffTasks != 0); /* overflow? */

    if (aWait)
    {
        /* go to Creating state before starting the task */
        aTarget->m->state = MediumState_Creating;

        rc = task->runNow();
    }
    else
    {
        rc = task->startThread();
        if (FAILED(rc)) return rc;

        /* go to Creating state before leaving the lock */
        aTarget->m->state = MediumState_Creating;
    }

    /* task is now owned (or already deleted) by taskThread() so release it */
    task.release();

    if (aProgress != NULL)
    {
        /* return progress to the caller */
        *aProgress = progress;
    }

    return rc;
}

/**
 * Prepares this (source) hard disk, target hard disk and all intermediate hard
 * disks for the merge operation.
 *
 * This method is to be called prior to calling the #mergeTo() to perform
 * necessary consistency checks and place involved hard disks to appropriate
 * states. If #mergeTo() is not called or fails, the state modifications
 * performed by this method must be undone by #cancelMergeTo().
 *
 * Note that when @a aIgnoreAttachments is @c true then it's the caller's
 * responsibility to detach the source and all intermediate hard disks before
 * calling #mergeTo() (which will fail otherwise).
 *
 * See #mergeTo() for more information about merging.
 *
 * @param aTarget       Target hard disk.
 * @param aChain        Where to store the created merge chain.
 * @param aIgnoreAttachments    Don't check if the source or any intermediate
 *                              hard disk is attached to any VM.
 *
 * @note Locks getTreeLock() for reading. Locks this object, aTarget and all
 *       intermediate hard disks for writing.
 */
HRESULT Medium::prepareMergeTo(Medium *aTarget,
                               MergeChain * &aChain,
                               bool aIgnoreAttachments /*= false*/)
{
    AssertReturn(aTarget != NULL, E_FAIL);

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.rc());

    AutoCaller targetCaller(aTarget);
    AssertComRCReturnRC(targetCaller.rc());

    aChain = NULL;

    /* we walk the tree */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    HRESULT rc = S_OK;

    /* detect the merge direction */
    bool forward;
    {
        Medium *parent = m->pParent;
        while (parent != NULL && parent != aTarget)
            parent = parent->m->pParent;
        if (parent == aTarget)
            forward = false;
        else
        {
            parent = aTarget->m->pParent;
            while (parent != NULL && parent != this)
                parent = parent->m->pParent;
            if (parent == this)
                forward = true;
            else
            {
                Utf8Str tgtLoc;
                {
                    AutoReadLock alock(this);
                    tgtLoc = aTarget->getLocationFull();
                }

                AutoReadLock alock(this);
                return setError(E_FAIL,
                                tr("Hard disks '%s' and '%s' are unrelated"),
                                m->strLocationFull.raw(), tgtLoc.raw());
            }
        }
    }

    /* build the chain (will do necessary checks and state changes) */
    std::auto_ptr <MergeChain> chain(new MergeChain(forward,
                                                      aIgnoreAttachments));
    {
        Medium *last = forward ? aTarget : this;
        Medium *first = forward ? this : aTarget;

        for (;;)
        {
            if (last == aTarget)
                rc = chain->addTarget(last);
            else if (last == this)
                rc = chain->addSource(last);
            else
                rc = chain->addIntermediate(last);
            if (FAILED(rc)) return rc;

            if (last == first)
                break;

            last = last->m->pParent;
        }
    }

    aChain = chain.release();

    return S_OK;
}

/**
 * Merges this hard disk to the specified hard disk which must be either its
 * direct ancestor or descendant.
 *
 * Given this hard disk is SOURCE and the specified hard disk is TARGET, we will
 * get two varians of the merge operation:
 *
 *                forward merge
 *                ------------------------->
 *  [Extra] <- SOURCE <- Intermediate <- TARGET
 *  Any        Del       Del             LockWr
 *
 *
 *                            backward merge
 *                <-------------------------
 *             TARGET <- Intermediate <- SOURCE <- [Extra]
 *             LockWr    Del             Del       LockWr
 *
 * Each scheme shows the involved hard disks on the hard disk chain where
 * SOURCE and TARGET belong. Under each hard disk there is a state value which
 * the hard disk must have at a time of the mergeTo() call.
 *
 * The hard disks in the square braces may be absent (e.g. when the forward
 * operation takes place and SOURCE is the base hard disk, or when the backward
 * merge operation takes place and TARGET is the last child in the chain) but if
 * they present they are involved too as shown.
 *
 * Nor the source hard disk neither intermediate hard disks may be attached to
 * any VM directly or in the snapshot, otherwise this method will assert.
 *
 * The #prepareMergeTo() method must be called prior to this method to place all
 * involved to necessary states and perform other consistency checks.
 *
 * If @a aWait is @c true then this method will perform the operation on the
 * calling thread and will not return to the caller until the operation is
 * completed. When this method succeeds, all intermediate hard disk objects in
 * the chain will be uninitialized, the state of the target hard disk (and all
 * involved extra hard disks) will be restored and @a aChain will be deleted.
 * Note that this (source) hard disk is not uninitialized because of possible
 * AutoCaller instances held by the caller of this method on the current thread.
 * It's therefore the responsibility of the caller to call Medium::uninit()
 * after releasing all callers in this case!
 *
 * If @a aWait is @c false then this method will crea,te a thread to perform the
 * create operation asynchronously and will return immediately. If the operation
 * succeeds, the thread will uninitialize the source hard disk object and all
 * intermediate hard disk objects in the chain, reset the state of the target
 * hard disk (and all involved extra hard disks) and delete @a aChain. If the
 * operation fails, the thread will only reset the states of all involved hard
 * disks and delete @a aChain.
 *
 * When this method fails (regardless of the @a aWait mode), it is a caller's
 * responsiblity to undo state changes and delete @a aChain using
 * #cancelMergeTo().
 *
 * If @a aProgress is not NULL but the object it points to is @c null then a new
 * progress object will be created and assigned to @a *aProgress on success,
 * otherwise the existing progress object is used. If Progress is NULL, then no
 * progress object is created/used at all. Note that @a aProgress cannot be
 * NULL when @a aWait is @c false (this method will assert in this case).
 *
 * @param aChain        Merge chain created by #prepareMergeTo().
 * @param aProgress     Where to find/store a Progress object to track operation
 *                      completion.
 * @param aWait         @c true if this method should block instead of creating
 *                      an asynchronous thread.
 *
 * @note Locks the branch lock for writing. Locks the hard disks from the chain
 *       for writing.
 */
HRESULT Medium::mergeTo(MergeChain *aChain,
                        ComObjPtr <Progress> *aProgress,
                        bool aWait)
{
    AssertReturn(aChain != NULL, E_FAIL);
    AssertReturn(aProgress != NULL || aWait == true, E_FAIL);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    HRESULT rc = S_OK;

    ComObjPtr <Progress> progress;

    if (aProgress != NULL)
    {
        /* use the existing progress object... */
        progress = *aProgress;

        /* ...but create a new one if it is null */
        if (progress.isNull())
        {
            AutoReadLock alock(this);

            progress.createObject();
            rc = progress->init(m->pVirtualBox,
                                static_cast<IMedium*>(this),
                                BstrFmt(tr("Merging hard disk '%s' to '%s'"),
                                        getName().raw(),
                                        aChain->target()->getName().raw()),
                                TRUE /* aCancelable */);
            if (FAILED(rc)) return rc;
        }
    }

    /* setup task object and thread to carry out the operation
     * asynchronously */

    std::auto_ptr <Task> task(new Task(this, progress, Task::Merge));
    AssertComRCReturnRC(task->m_autoCaller.rc());

    task->setData(aChain);

    /* Note: task owns aChain (will delete it when not needed) in all cases
     * except when @a aWait is @c true and runNow() fails -- in this case
     * aChain will be left away because cancelMergeTo() will be applied by the
     * caller on it as it is required in the documentation above */

    if (aWait)
    {
        rc = task->runNow();
    }
    else
    {
        rc = task->startThread();
        if (FAILED(rc)) return rc;
    }

    /* task is now owned (or already deleted) by taskThread() so release it */
    task.release();

    if (aProgress != NULL)
    {
        /* return progress to the caller */
        *aProgress = progress;
    }

    return rc;
}

/**
 * Undoes what #prepareMergeTo() did. Must be called if #mergeTo() is not called
 * or fails. Frees memory occupied by @a aChain.
 *
 * @param aChain        Merge chain created by #prepareMergeTo().
 *
 * @note Locks the hard disks from the chain for writing.
 */
void Medium::cancelMergeTo(MergeChain *aChain)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.rc());

    AssertReturnVoid(aChain != NULL);

    /* the destructor will do the thing */
    delete aChain;
}

/**
 * Checks that the format ID is valid and sets it on success.
 *
 * Note that this method will caller-reference the format object on success!
 * This reference must be released somewhere to let the MediumFormat object be
 * uninitialized.
 *
 * @note Must be called from under this object's write lock.
 */
HRESULT Medium::setFormat(CBSTR aFormat)
{
    /* get the format object first */
    {
        AutoReadLock propsLock(m->pVirtualBox->systemProperties());

        unconst(m->formatObj)
            = m->pVirtualBox->systemProperties()->mediumFormat(aFormat);
        if (m->formatObj.isNull())
            return setError(E_INVALIDARG,
                            tr("Invalid hard disk storage format '%ls'"), aFormat);

        /* reference the format permanently to prevent its unexpected
         * uninitialization */
        HRESULT rc = m->formatObj->addCaller();
        AssertComRCReturnRC(rc);

        /* get properties (preinsert them as keys in the map). Note that the
         * map doesn't grow over the object life time since the set of
         * properties is meant to be constant. */

        Assert(m->properties.empty());

        for (MediumFormat::PropertyList::const_iterator it =
                m->formatObj->properties().begin();
             it != m->formatObj->properties().end();
             ++ it)
        {
            m->properties.insert(std::make_pair(it->name, Bstr::Null));
        }
    }

    unconst(m->strFormat) = aFormat;

    return S_OK;
}

/**
 * @note Called from this object's AutoMayUninitSpan and from under mVirtualBox
 *       write lock.
 *
 * @note Also reused by Medium::Reset().
 *
 * @note Locks getTreeLock() for reading.
 */
HRESULT Medium::canClose()
{
    /* we access children */
    AutoReadLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    if (getChildren().size() != 0)
        return setError(E_FAIL,
                        tr("Cannot close medium '%s' because it has %d child hard disk(s)"),
                        m->strLocationFull.raw(), getChildren().size());

    return S_OK;
}

/**
 * @note Called from within this object's AutoMayUninitSpan (or AutoCaller) and
 *       from under mVirtualBox write lock.
 *
 * @note Locks getTreeLock() for writing.
 */
HRESULT Medium::unregisterWithVirtualBox()
{
    /* Note that we need to de-associate ourselves from the parent to let
     * unregisterHardDisk() properly save the registry */

    /* we modify mParent and access children */
    AutoWriteLock treeLock(m->pVirtualBox->hardDiskTreeLockHandle());

    Medium *pParentBackup = m->pParent;

    AssertReturn(getChildren().size() == 0, E_FAIL);

    if (m->pParent)
        deparent();

    HRESULT rc = E_FAIL;
    switch (m->devType)
    {
        case DeviceType_DVD:
            rc = m->pVirtualBox->unregisterImage(this, DeviceType_DVD);
            break;
        case DeviceType_Floppy:
            rc = m->pVirtualBox->unregisterImage(this, DeviceType_Floppy);
            break;
        case DeviceType_HardDisk:
            rc = m->pVirtualBox->unregisterHardDisk(this);
            break;
        default:
            break;
    }

    if (FAILED(rc))
    {
        if (pParentBackup)
        {
            /* re-associate with the parent as we are still relatives in the
             * registry */
            m->pParent = pParentBackup;
            m->pParent->m->llChildren.push_back(this);
        }
    }

    return rc;
}

/**
 * Returns the last error message collected by the vdErrorCall callback and
 * resets it.
 *
 * The error message is returned prepended with a dot and a space, like this:
 * <code>
 *   ". <error_text> (%Rrc)"
 * </code>
 * to make it easily appendable to a more general error message. The @c %Rrc
 * format string is given @a aVRC as an argument.
 *
 * If there is no last error message collected by vdErrorCall or if it is a
 * null or empty string, then this function returns the following text:
 * <code>
 *   " (%Rrc)"
 * </code>
 *
 * @note Doesn't do any object locking; it is assumed that the caller makes sure
 *       the callback isn't called by more than one thread at a time.
 *
 * @param aVRC  VBox error code to use when no error message is provided.
 */
Utf8Str Medium::vdError(int aVRC)
{
    Utf8Str error;

    if (m->vdError.isEmpty())
        error = Utf8StrFmt(" (%Rrc)", aVRC);
    else
        error = Utf8StrFmt(".\n%s", m->vdError.raw());

    m->vdError.setNull();

    return error;
}

/**
 * Error message callback.
 *
 * Puts the reported error message to the m->vdError field.
 *
 * @note Doesn't do any object locking; it is assumed that the caller makes sure
 *       the callback isn't called by more than one thread at a time.
 *
 * @param   pvUser          The opaque data passed on container creation.
 * @param   rc              The VBox error code.
 * @param   RT_SRC_POS_DECL Use RT_SRC_POS.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments.
 */
/*static*/
DECLCALLBACK(void) Medium::vdErrorCall(void *pvUser, int rc, RT_SRC_POS_DECL,
                                       const char *pszFormat, va_list va)
{
    NOREF(pszFile); NOREF(iLine); NOREF(pszFunction); /* RT_SRC_POS_DECL */

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturnVoid(that != NULL);

    if (that->m->vdError.isEmpty())
        that->m->vdError =
            Utf8StrFmt("%s (%Rrc)", Utf8StrFmtVA(pszFormat, va).raw(), rc);
    else
        that->m->vdError =
            Utf8StrFmt("%s.\n%s (%Rrc)", that->m->vdError.raw(),
                       Utf8StrFmtVA(pszFormat, va).raw(), rc);
}

/**
 * PFNVMPROGRESS callback handler for Task operations.
 *
 * @param uPercent    Completetion precentage (0-100).
 * @param pvUser      Pointer to the Progress instance.
 */
/*static*/
DECLCALLBACK(int) Medium::vdProgressCall(PVM /* pVM */, unsigned uPercent,
                                         void *pvUser)
{
    Progress *that = static_cast<Progress *>(pvUser);

    if (that != NULL)
    {
        /* update the progress object, capping it at 99% as the final percent
         * is used for additional operations like setting the UUIDs and similar. */
        HRESULT rc = that->SetCurrentOperationProgress(uPercent * 99 / 100);
        if (FAILED(rc))
        {
            if (rc == E_FAIL)
                return VERR_CANCELLED;
            else
                return VERR_INVALID_STATE;
        }
    }

    return VINF_SUCCESS;
}

/* static */
DECLCALLBACK(bool) Medium::vdConfigAreKeysValid(void *pvUser,
                                                const char * /* pszzValid */)
{
    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, false);

    /* we always return true since the only keys we have are those found in
     * VDBACKENDINFO */
    return true;
}

/* static */
DECLCALLBACK(int) Medium::vdConfigQuerySize(void *pvUser, const char *pszName,
                                            size_t *pcbValue)
{
    AssertReturn(VALID_PTR(pcbValue), VERR_INVALID_POINTER);

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, VERR_GENERAL_FAILURE);

    Data::PropertyMap::const_iterator it =
        that->m->properties.find(Bstr(pszName));
    if (it == that->m->properties.end())
        return VERR_CFGM_VALUE_NOT_FOUND;

    /* we interpret null values as "no value" in Medium */
    if (it->second.isNull())
        return VERR_CFGM_VALUE_NOT_FOUND;

    *pcbValue = it->second.length() + 1 /* include terminator */;

    return VINF_SUCCESS;
}

/* static */
DECLCALLBACK(int) Medium::vdConfigQuery(void *pvUser, const char *pszName,
                                        char *pszValue, size_t cchValue)
{
    AssertReturn(VALID_PTR(pszValue), VERR_INVALID_POINTER);

    Medium *that = static_cast<Medium*>(pvUser);
    AssertReturn(that != NULL, VERR_GENERAL_FAILURE);

    Data::PropertyMap::const_iterator it =
        that->m->properties.find(Bstr(pszName));
    if (it == that->m->properties.end())
        return VERR_CFGM_VALUE_NOT_FOUND;

    Utf8Str value = it->second;
    if (value.length() >= cchValue)
        return VERR_CFGM_NOT_ENOUGH_SPACE;

    /* we interpret null values as "no value" in Medium */
    if (it->second.isNull())
        return VERR_CFGM_VALUE_NOT_FOUND;

    memcpy(pszValue, value.c_str(), value.length() + 1);

    return VINF_SUCCESS;
}

/**
 * Thread function for time-consuming tasks.
 *
 * The Task structure passed to @a pvUser must be allocated using new and will
 * be freed by this method before it returns.
 *
 * @param pvUser    Pointer to the Task instance.
 */
/* static */
DECLCALLBACK(int) Medium::taskThread(RTTHREAD thread, void *pvUser)
{
    std::auto_ptr <Task> task(static_cast <Task *>(pvUser));
    AssertReturn(task.get(), VERR_GENERAL_FAILURE);

    bool isAsync = thread != NIL_RTTHREAD;

    Medium *that = task->that;

    /* Set up a per-operation progress interface, can be used freely (for
     * binary operations you can use it either on the source or target). */
    VDINTERFACEPROGRESS vdIfCallsProgress;
    vdIfCallsProgress.cbSize = sizeof(VDINTERFACEPROGRESS);
    vdIfCallsProgress.enmInterface = VDINTERFACETYPE_PROGRESS;
    vdIfCallsProgress.pfnProgress = Medium::vdProgressCall;
    VDINTERFACE vdIfProgress;
    PVDINTERFACE vdOperationIfaces = NULL;
    int vrc1 = VDInterfaceAdd(&vdIfProgress,
                             "Medium::vdInterfaceProgress",
                             VDINTERFACETYPE_PROGRESS,
                             &vdIfCallsProgress,
                             task->m_pProgress,
                             &vdOperationIfaces);
    AssertRCReturn(vrc1, E_FAIL);

    /// @todo ugly hack, fix ComAssert... later
    #define setError that->setError

    /* Note: no need in AutoCaller because Task does that */

    LogFlowFuncEnter();
    LogFlowFunc(("{%p}: operation=%d\n", that, task->m_operation));

    HRESULT rc = S_OK;

    switch (task->m_operation)
    {
        ////////////////////////////////////////////////////////////////////////

        case Task::CreateBase:
        {
            /* The lock is also used as a signal from the task initiator (which
             * releases it only after RTThreadCreate()) that we can start the job */
            AutoWriteLock thatLock(that);

            /* these parameters we need after creation */
            uint64_t size = 0, logicalSize = 0;

            /* The object may request a specific UUID (through a special form of
             * the setLocation() argument). Otherwise we have to generate it */
            Guid id = that->m->id;
            bool generateUuid = id.isEmpty();
            if (generateUuid)
            {
                id.create();
                /* VirtualBox::registerHardDisk() will need UUID */
                unconst(that->m->id) = id;
            }

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                Utf8Str format(that->m->strFormat);
                Utf8Str location(that->m->strLocationFull);
                /* uint64_t capabilities = */ that->m->formatObj->capabilities();

                /* unlock before the potentially lengthy operation */
                Assert(that->m->state == MediumState_Creating);
                thatLock.leave();

                try
                {
                    /* ensure the directory exists */
                    rc = VirtualBox::ensureFilePathExists(location);
                    if (FAILED(rc)) throw rc;

                    PDMMEDIAGEOMETRY geo = { 0 }; /* auto-detect */

                    vrc = VDCreateBase(hdd, format.c_str(), location.c_str(),
                                       task->d.size * _1M,
                                       task->d.variant,
                                       NULL, &geo, &geo, id.raw(),
                                       VD_OPEN_FLAGS_NORMAL,
                                       NULL, vdOperationIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not create the hard disk storage unit '%s'%s"),
                                       location.raw(), that->vdError(vrc).raw());
                    }

                    size = VDGetFileSize(hdd, 0);
                    logicalSize = VDGetSize(hdd, 0) / _1M;
                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            if (SUCCEEDED(rc))
            {
                /* register with mVirtualBox as the last step and move to
                 * Created state only on success (leaving an orphan file is
                 * better than breaking media registry consistency) */
                rc = that->m->pVirtualBox->registerHardDisk(that);
            }

            thatLock.maybeEnter();

            if (SUCCEEDED(rc))
            {
                that->m->state = MediumState_Created;

                that->m->size = size;
                that->m->logicalSize = logicalSize;
            }
            else
            {
                /* back to NotCreated on failure */
                that->m->state = MediumState_NotCreated;

                /* reset UUID to prevent it from being reused next time */
                if (generateUuid)
                    unconst(that->m->id).clear();
            }

            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case Task::CreateDiff:
        {
            ComObjPtr<Medium> &target = task->d.target;

            /* Lock both in {parent,child} order. The lock is also used as a
             * signal from the task initiator (which releases it only after
             * RTThreadCreate()) that we can start the job*/
            AutoMultiWriteLock2 thatLock(that, target);

            uint64_t size = 0, logicalSize = 0;

            /* The object may request a specific UUID (through a special form of
             * the setLocation() argument). Otherwise we have to generate it */
            Guid targetId = target->m->id;
            bool generateUuid = targetId.isEmpty();
            if (generateUuid)
            {
                targetId.create();
                /* VirtualBox::registerHardDisk() will need UUID */
                unconst(target->m->id) = targetId;
            }

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                Guid id = that->m->id;
                Utf8Str format(that->m->strFormat);
                Utf8Str location(that->m->strLocationFull);

                Utf8Str targetFormat(target->m->strFormat);
                Utf8Str targetLocation(target->m->strLocationFull);

                Assert(target->m->state == MediumState_Creating);

                /* Note: MediumState_LockedWrite is ok when taking an online
                 * snapshot */
                Assert(that->m->state == MediumState_LockedRead ||
                       that->m->state == MediumState_LockedWrite);

                /* unlock before the potentially lengthy operation */
                thatLock.leave();

                try
                {
                    vrc = VDOpen(hdd, format.c_str(), location.c_str(),
                                 VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO,
                                 that->m->vdDiskIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not open the hard disk storage unit '%s'%s"),
                                       location.raw(), that->vdError(vrc).raw());
                    }

                    /* ensure the target directory exists */
                    rc = VirtualBox::ensureFilePathExists(targetLocation);
                    if (FAILED(rc)) throw rc;

                    /** @todo add VD_IMAGE_FLAGS_DIFF to the image flags, to
                     * be on the safe side. */
                    vrc = VDCreateDiff(hdd, targetFormat.c_str(),
                                       targetLocation.c_str(),
                                       task->d.variant,
                                       NULL, targetId.raw(),
                                       id.raw(),
                                       VD_OPEN_FLAGS_NORMAL,
                                       target->m->vdDiskIfaces,
                                       vdOperationIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not create the differencing hard disk storage unit '%s'%s"),
                                       targetLocation.raw(), that->vdError(vrc).raw());
                    }

                    size = VDGetFileSize(hdd, 1);
                    logicalSize = VDGetSize(hdd, 1) / _1M;
                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            if (SUCCEEDED(rc))
            {
                /* we set mParent & children() (note that thatLock is released
                 * here), but lock VirtualBox first to follow the rule */
                AutoWriteLock alock1(that->m->pVirtualBox);
                AutoWriteLock alock2(that->m->pVirtualBox->hardDiskTreeLockHandle());

                Assert(target->m->pParent.isNull());

                /* associate the child with the parent */
                target->m->pParent = that;
                that->m->llChildren.push_back(target);

                /** @todo r=klaus neither target nor that->base() are locked,
                 * potential race! */
                /* diffs for immutable hard disks are auto-reset by default */
                target->m->autoReset =
                    that->getBase()->m->type == MediumType_Immutable ?
                    TRUE : FALSE;

                /* register with mVirtualBox as the last step and move to
                 * Created state only on success (leaving an orphan file is
                 * better than breaking media registry consistency) */
                rc = that->m->pVirtualBox->registerHardDisk(target);

                if (FAILED(rc))
                    /* break the parent association on failure to register */
                    that->deparent();
            }

            thatLock.maybeEnter();

            if (SUCCEEDED(rc))
            {
                target->m->state = MediumState_Created;

                target->m->size = size;
                target->m->logicalSize = logicalSize;
            }
            else
            {
                /* back to NotCreated on failure */
                target->m->state = MediumState_NotCreated;

                target->m->autoReset = FALSE;

                /* reset UUID to prevent it from being reused next time */
                if (generateUuid)
                    unconst(target->m->id).clear();
            }

            if (isAsync)
            {
                /* unlock ourselves when done (unless in MediumState_LockedWrite
                 * state because of taking the online snapshot*/
                if (that->m->state != MediumState_LockedWrite)
                {
                    HRESULT rc2 = that->UnlockRead(NULL);
                    AssertComRC(rc2);
                }
            }

            /* deregister the task registered in createDiffStorage() */
            Assert(that->m->numCreateDiffTasks != 0);
            --that->m->numCreateDiffTasks;

            /* Note that in sync mode, it's the caller's responsibility to
             * unlock the hard disk */

            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case Task::Merge:
        {
            /* The lock is also used as a signal from the task initiator (which
             * releases it only after RTThreadCreate()) that we can start the
             * job. We don't actually need the lock for anything else since the
             * object is protected by MediumState_Deleting and we don't modify
             * its sensitive fields below */
            {
                AutoWriteLock thatLock(that);
            }

            MergeChain *chain = task->d.chain.get();

#if 0
            LogFlow(("*** MERGE forward = %RTbool\n", chain->isForward()));
#endif

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                try
                {
                    /* Open all hard disks in the chain (they are in the
                     * {parent,child} order in there. Note that we don't lock
                     * objects in this chain since they must be in states
                     * (Deleting and LockedWrite) that prevent from changing
                     * their format and location fields from outside. */

                    for (MergeChain::const_iterator it = chain->begin();
                         it != chain->end(); ++ it)
                    {
                        /* complex sanity (sane complexity) */
                        Assert((chain->isForward() &&
                                ((*it != chain->back() &&
                                  (*it)->m->state == MediumState_Deleting) ||
                                 (*it == chain->back() &&
                                  (*it)->m->state == MediumState_LockedWrite))) ||
                               (!chain->isForward() &&
                                ((*it != chain->front() &&
                                  (*it)->m->state == MediumState_Deleting) ||
                                 (*it == chain->front() &&
                                  (*it)->m->state == MediumState_LockedWrite))));

                        Assert(*it == chain->target() ||
                               (*it)->m->backRefs.size() == 0);

                        /* open the first image with VDOPEN_FLAGS_INFO because
                         * it's not necessarily the base one */
                        vrc = VDOpen(hdd, (*it)->m->strFormat.c_str(),
                                      (*it)->m->strLocationFull.c_str(),
                                      it == chain->begin() ?
                                          VD_OPEN_FLAGS_INFO : 0,
                                      (*it)->m->vdDiskIfaces);
                        if (RT_FAILURE(vrc))
                            throw vrc;
#if 0
                        LogFlow(("*** MERGE disk = %s\n", (*it)->m->strLocationFull.raw()));
#endif
                    }

                    unsigned start = chain->isForward() ?
                        0 : (unsigned)chain->size() - 1;
                    unsigned end = chain->isForward() ?
                        (unsigned)chain->size() - 1 : 0;
#if 0
                    LogFlow(("*** MERGE from %d to %d\n", start, end));
#endif
                    vrc = VDMerge(hdd, start, end, vdOperationIfaces);
                    if (RT_FAILURE(vrc))
                        throw vrc;

                    /* update parent UUIDs */
                    /// @todo VDMerge should be taught to do so, including the
                    /// multiple children case
                    if (chain->isForward())
                    {
                        /* target's UUID needs to be updated (note that target
                         * is the only image in the container on success) */
                        vrc = VDSetParentUuid(hdd, 0, chain->parent()->m->id);
                        if (RT_FAILURE(vrc))
                            throw vrc;
                    }
                    else
                    {
                        /* we need to update UUIDs of all source's children
                         * which cannot be part of the container at once so
                         * add each one in there individually */
                        if (chain->children().size() > 0)
                        {
                            for (MediaList::const_iterator it = chain->children().begin();
                                 it != chain->children().end();
                                 ++it)
                            {
                                /* VD_OPEN_FLAGS_INFO since UUID is wrong yet */
                                vrc = VDOpen(hdd, (*it)->m->strFormat.c_str(),
                                             (*it)->m->strLocationFull.c_str(),
                                             VD_OPEN_FLAGS_INFO,
                                             (*it)->m->vdDiskIfaces);
                                if (RT_FAILURE(vrc))
                                    throw vrc;

                                vrc = VDSetParentUuid(hdd, 1,
                                                      chain->target()->m->id);
                                if (RT_FAILURE(vrc))
                                    throw vrc;

                                vrc = VDClose(hdd, false /* fDelete */);
                                if (RT_FAILURE(vrc))
                                    throw vrc;
                            }
                        }
                    }
                }
                catch (HRESULT aRC) { rc = aRC; }
                catch (int aVRC)
                {
                    throw setError(E_FAIL,
                                   tr("Could not merge the hard disk '%s' to '%s'%s"),
                                   chain->source()->m->strLocationFull.raw(),
                                   chain->target()->m->strLocationFull.raw(),
                                   that->vdError(aVRC).raw());
                }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            HRESULT rc2;

            bool saveSettingsFailed = false;

            if (SUCCEEDED(rc))
            {
                /* all hard disks but the target were successfully deleted by
                 * VDMerge; reparent the last one and uninitialize deleted */

                /* we set mParent & children() (note that thatLock is released
                 * here), but lock VirtualBox first to follow the rule */
                AutoWriteLock alock1(that->m->pVirtualBox);
                AutoWriteLock alock2(that->m->pVirtualBox->hardDiskTreeLockHandle());

                Medium *source = chain->source();
                Medium *target = chain->target();

                if (chain->isForward())
                {
                    /* first, unregister the target since it may become a base
                     * hard disk which needs re-registration */
                    rc2 = target->m->pVirtualBox->unregisterHardDisk(target, false /* aSaveSettings */);
                    AssertComRC(rc2);

                    /* then, reparent it and disconnect the deleted branch at
                     * both ends (chain->parent() is source's parent) */
                    target->deparent();
                    target->m->pParent = chain->parent();
                    if (target->m->pParent)
                    {
                        target->m->pParent->m->llChildren.push_back(target);
                        source->deparent();
                    }

                    /* then, register again */
                    rc2 = target->m->pVirtualBox->
                        registerHardDisk(target, false /* aSaveSettings */);
                    AssertComRC(rc2);
                }
                else
                {
                    Assert(target->getChildren().size() == 1);
                    Medium *targetChild = target->getChildren().front();

                    /* disconnect the deleted branch at the elder end */
                    targetChild->deparent();

                    const MediaList &children = chain->children();

                    /* reparent source's chidren and disconnect the deleted
                     * branch at the younger end m*/
                    if (children.size() > 0)
                    {
                        /* obey {parent,child} lock order */
                        AutoWriteLock sourceLock(source);

                        for (MediaList::const_iterator it = children.begin();
                             it != children.end();
                             ++it)
                        {
                            AutoWriteLock childLock(*it);

                            Medium *p = *it;
                            p->deparent();  // removes p from source
                            target->m->llChildren.push_back(p);
                            p->m->pParent = target;
                        }
                    }
                }

                /* try to save the hard disk registry */
                rc = that->m->pVirtualBox->saveSettings();

                if (SUCCEEDED(rc))
                {
                    /* unregister and uninitialize all hard disks in the chain
                     * but the target */

                    for (MergeChain::iterator it = chain->begin();
                         it != chain->end();)
                    {
                        if (*it == chain->target())
                        {
                            ++ it;
                            continue;
                        }

                        rc2 = (*it)->m->pVirtualBox->
                            unregisterHardDisk(*it, false /* aSaveSettings */);
                        AssertComRC(rc2);

                        /* now, uninitialize the deleted hard disk (note that
                         * due to the Deleting state, uninit() will not touch
                         * the parent-child relationship so we need to
                         * uninitialize each disk individually) */

                        /* note that the operation initiator hard disk (which is
                         * normally also the source hard disk) is a special case
                         * -- there is one more caller added by Task to it which
                         * we must release. Also, if we are in sync mode, the
                         * caller may still hold an AutoCaller instance for it
                         * and therefore we cannot uninit() it (it's therefore
                         * the caller's responsibility) */
                        if (*it == that)
                            task->m_autoCaller.release();

                        /* release the caller added by MergeChain before
                         * uninit() */
                        (*it)->releaseCaller();

                        if (isAsync || *it != that)
                            (*it)->uninit();

                        /* delete (to prevent uninitialization in MergeChain
                         * dtor) and advance to the next item */
                        it = chain->erase(it);
                    }

                    /* Note that states of all other hard disks (target, parent,
                     * children) will be restored by the MergeChain dtor */
                }
                else
                {
                    /* too bad if we fail, but we'll need to rollback everything
                     * we did above to at least keep the HD tree in sync with
                     * the current registry on disk */

                    saveSettingsFailed = true;

                    /// @todo NEWMEDIA implement a proper undo

                    AssertFailed();
                }
            }

            if (FAILED(rc))
            {
                /* Here we come if either VDMerge() failed (in which case we
                 * assume that it tried to do everything to make a further
                 * retry possible -- e.g. not deleted intermediate hard disks
                 * and so on) or VirtualBox::saveSettings() failed (where we
                 * should have the original tree but with intermediate storage
                 * units deleted by VDMerge()). We have to only restore states
                 * (through the MergeChain dtor) unless we are run synchronously
                 * in which case it's the responsibility of the caller as stated
                 * in the mergeTo() docs. The latter also implies that we
                 * don't own the merge chain, so release it in this case. */

                if (!isAsync)
                    task->d.chain.release();

                NOREF(saveSettingsFailed);
            }

            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case Task::Clone:
        {
            ComObjPtr<Medium> &target = task->d.target;
            ComObjPtr<Medium> &parent = task->d.parentDisk;

            /* Lock all in {parent,child} order. The lock is also used as a
             * signal from the task initiator (which releases it only after
             * RTThreadCreate()) that we can start the job. */
            AutoMultiWriteLock3 thatLock(that, target, parent);

            ImageChain *srcChain = task->d.source.get();
            ImageChain *parentChain = task->d.parent.get();

            uint64_t size = 0, logicalSize = 0;

            /* The object may request a specific UUID (through a special form of
             * the setLocation() argument). Otherwise we have to generate it */
            Guid targetId = target->m->id;
            bool generateUuid = targetId.isEmpty();
            if (generateUuid)
            {
                targetId.create();
                /* VirtualBox::registerHardDisk() will need UUID */
                unconst(target->m->id) = targetId;
            }

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                try
                {
                    /* Open all hard disk images in the source chain. */
                    for (MediaList::const_iterator it = srcChain->begin();
                         it != srcChain->end();
                         ++it)
                    {
                        /* sanity check */
                        Assert((*it)->m->state == MediumState_LockedRead);

                        /** Open all images in read-only mode. */
                        vrc = VDOpen(hdd, (*it)->m->strFormat.c_str(),
                                     (*it)->m->strLocationFull.c_str(),
                                     VD_OPEN_FLAGS_READONLY,
                                     (*it)->m->vdDiskIfaces);
                        if (RT_FAILURE(vrc))
                        {
                            throw setError(E_FAIL,
                                           tr("Could not open the hard disk storage unit '%s'%s"),
                                           (*it)->m->strLocationFull.raw(),
                                           that->vdError(vrc).raw());
                        }
                    }

                    Utf8Str targetFormat(target->m->strFormat);
                    Utf8Str targetLocation(target->m->strLocationFull);

                    Assert(    target->m->state == MediumState_Creating
                           ||  target->m->state == MediumState_LockedWrite);
                    Assert(that->m->state == MediumState_LockedRead);
                    Assert(parent.isNull() || parent->m->state == MediumState_LockedRead);

                    /* unlock before the potentially lengthy operation */
                    thatLock.leave();

                    /* ensure the target directory exists */
                    rc = VirtualBox::ensureFilePathExists(targetLocation);
                    if (FAILED(rc)) throw rc;

                    PVBOXHDD targetHdd;
                    vrc = VDCreate(that->m->vdDiskIfaces, &targetHdd);
                    ComAssertRCThrow(vrc, E_FAIL);

                    try
                    {
                        /* Open all hard disk images in the parent chain. */
                        for (MediaList::const_iterator it = parentChain->begin();
                             it != parentChain->end();
                             ++it)
                        {
                            /** @todo r=klaus (*it) is not locked, lots of
                             * race opportunities below */
                            /* sanity check */
                            Assert(    (*it)->m->state == MediumState_LockedRead
                                   ||  (*it)->m->state == MediumState_LockedWrite);

                            /* Open all images in appropriate mode. */
                            vrc = VDOpen(targetHdd, (*it)->m->strFormat.c_str(),
                                         (*it)->m->strLocationFull.c_str(),
                                         ((*it)->m->state == MediumState_LockedWrite) ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY,
                                         (*it)->m->vdDiskIfaces);
                            if (RT_FAILURE(vrc))
                            {
                                throw setError(E_FAIL,
                                               tr("Could not open the hard disk storage unit '%s'%s"),
                                               (*it)->m->strLocationFull.raw(),
                                               that->vdError(vrc).raw());
                            }
                        }

                        /** @todo r=klaus target isn't locked, race getting the state */
                        vrc = VDCopy(hdd, VD_LAST_IMAGE, targetHdd,
                                     targetFormat.c_str(),
                                     target->m->state == MediumState_Creating ? targetLocation.raw() : (char *)NULL,
                                     false, 0,
                                     task->d.variant, targetId.raw(), NULL,
                                     target->m->vdDiskIfaces,
                                     vdOperationIfaces);
                        if (RT_FAILURE(vrc))
                        {
                            throw setError(E_FAIL,
                                           tr("Could not create the clone hard disk '%s'%s"),
                                           targetLocation.raw(), that->vdError(vrc).raw());
                        }
                        size = VDGetFileSize(targetHdd, 0);
                        logicalSize = VDGetSize(targetHdd, 0) / _1M;
                    }
                    catch (HRESULT aRC) { rc = aRC; }

                    VDDestroy(targetHdd);
                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            /* Only do the parent changes for newly created images. */
            if (target->m->state == MediumState_Creating)
            {
                if (SUCCEEDED(rc))
                {
                    /* we set mParent & children() (note that thatLock is released
                     * here), but lock VirtualBox first to follow the rule */
                    AutoWriteLock alock1(that->m->pVirtualBox);
                    AutoWriteLock alock2(that->m->pVirtualBox->hardDiskTreeLockHandle());

                    Assert(target->m->pParent.isNull());

                    if (parent)
                    {
                        /* associate the clone with the parent and deassociate
                         * from VirtualBox */
                        target->m->pParent = parent;
                        parent->m->llChildren.push_back(target);

                        /* register with mVirtualBox as the last step and move to
                         * Created state only on success (leaving an orphan file is
                         * better than breaking media registry consistency) */
                        rc = parent->m->pVirtualBox->registerHardDisk(target);

                        if (FAILED(rc))
                            /* break parent association on failure to register */
                            target->deparent();     // removes target from parent
                    }
                    else
                    {
                        /* just register  */
                        rc = that->m->pVirtualBox->registerHardDisk(target);
                    }
                }
            }

            thatLock.maybeEnter();

            if (target->m->state == MediumState_Creating)
            {
                if (SUCCEEDED(rc))
                {
                    target->m->state = MediumState_Created;

                    target->m->size = size;
                    target->m->logicalSize = logicalSize;
                }
                else
                {
                    /* back to NotCreated on failure */
                    target->m->state = MediumState_NotCreated;

                    /* reset UUID to prevent it from being reused next time */
                    if (generateUuid)
                        unconst(target->m->id).clear();
                }
            }

            /* Everything is explicitly unlocked when the task exits,
             * as the task destruction also destroys the source chain. */

            /* Make sure the source chain is released early. It could happen
             * that we get a deadlock in Appliance::Import when Medium::Close
             * is called & the source chain is released at the same time. */
            task->d.source.reset();
            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case Task::Delete:
        {
            /* The lock is also used as a signal from the task initiator (which
             * releases it only after RTThreadCreate()) that we can start the job */
            AutoWriteLock thatLock(that);

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                Utf8Str format(that->m->strFormat);
                Utf8Str location(that->m->strLocationFull);

                /* unlock before the potentially lengthy operation */
                Assert(that->m->state == MediumState_Deleting);
                thatLock.leave();

                try
                {
                    vrc = VDOpen(hdd, format.c_str(), location.c_str(),
                                 VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO,
                                 that->m->vdDiskIfaces);
                    if (RT_SUCCESS(vrc))
                        vrc = VDClose(hdd, true /* fDelete */);

                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not delete the hard disk storage unit '%s'%s"),
                                       location.raw(), that->vdError(vrc).raw());
                    }

                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            thatLock.maybeEnter();

            /* go to the NotCreated state even on failure since the storage
             * may have been already partially deleted and cannot be used any
             * more. One will be able to manually re-open the storage if really
             * needed to re-register it. */
            that->m->state = MediumState_NotCreated;

            /* Reset UUID to prevent Create* from reusing it again */
            unconst(that->m->id).clear();

            break;
        }

        case Task::Reset:
        {
            /* The lock is also used as a signal from the task initiator (which
             * releases it only after RTThreadCreate()) that we can start the job */
            AutoWriteLock thatLock(that);

            /// @todo Below we use a pair of delete/create operations to reset
            /// the diff contents but the most efficient way will of course be
            /// to add a VDResetDiff() API call

            uint64_t size = 0, logicalSize = 0;

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                Guid id = that->m->id;
                Utf8Str format(that->m->strFormat);
                Utf8Str location(that->m->strLocationFull);

                Medium *pParent = that->m->pParent;
                Guid parentId = pParent->m->id;
                Utf8Str parentFormat(pParent->m->strFormat);
                Utf8Str parentLocation(pParent->m->strLocationFull);

                Assert(that->m->state == MediumState_LockedWrite);

                /* unlock before the potentially lengthy operation */
                thatLock.leave();

                try
                {
                    /* first, delete the storage unit */
                    vrc = VDOpen(hdd, format.c_str(), location.c_str(),
                                 VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO,
                                 that->m->vdDiskIfaces);
                    if (RT_SUCCESS(vrc))
                        vrc = VDClose(hdd, true /* fDelete */);

                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not delete the hard disk storage unit '%s'%s"),
                                       location.raw(), that->vdError(vrc).raw());
                    }

                    /* next, create it again */
                    vrc = VDOpen(hdd, parentFormat.c_str(), parentLocation.c_str(),
                                 VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO,
                                 that->m->vdDiskIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not open the hard disk storage unit '%s'%s"),
                                       parentLocation.raw(), that->vdError(vrc).raw());
                    }

                    vrc = VDCreateDiff(hdd, format.c_str(), location.c_str(),
                                       /// @todo use the same image variant as before
                                       VD_IMAGE_FLAGS_NONE,
                                       NULL, id.raw(),
                                       parentId.raw(),
                                       VD_OPEN_FLAGS_NORMAL,
                                       that->m->vdDiskIfaces,
                                       vdOperationIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        throw setError(E_FAIL,
                                       tr("Could not create the differencing hard disk storage unit '%s'%s"),
                                       location.raw(), that->vdError(vrc).raw());
                    }

                    size = VDGetFileSize(hdd, 1);
                    logicalSize = VDGetSize(hdd, 1) / _1M;
                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            thatLock.enter();

            that->m->size = size;
            that->m->logicalSize = logicalSize;

            if (isAsync)
            {
                /* unlock ourselves when done */
                HRESULT rc2 = that->UnlockWrite(NULL);
                AssertComRC(rc2);
            }

            /* Note that in sync mode, it's the caller's responsibility to
             * unlock the hard disk */

            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case Task::Compact:
        {
            /* Lock all in {parent,child} order. The lock is also used as a
             * signal from the task initiator (which releases it only after
             * RTThreadCreate()) that we can start the job. */
            AutoWriteLock thatLock(that);

            ImageChain *imgChain = task->d.images.get();

            try
            {
                PVBOXHDD hdd;
                int vrc = VDCreate(that->m->vdDiskIfaces, &hdd);
                ComAssertRCThrow(vrc, E_FAIL);

                try
                {
                    /* Open all hard disk images in the chain. */
                    MediaList::const_iterator last = imgChain->end();
                    last--;
                    for (MediaList::const_iterator it = imgChain->begin();
                         it != imgChain->end();
                         ++it)
                    {
                        /* sanity check */
                        if (it == last)
                            Assert((*it)->m->state == MediumState_LockedWrite);
                        else
                            Assert((*it)->m->state == MediumState_LockedRead);

                        /** Open all images but last in read-only mode. */
                        vrc = VDOpen(hdd, (*it)->m->strFormat.c_str(),
                                     (*it)->m->strLocationFull.c_str(),
                                     (it == last) ? VD_OPEN_FLAGS_NORMAL : VD_OPEN_FLAGS_READONLY,
                                     (*it)->m->vdDiskIfaces);
                        if (RT_FAILURE(vrc))
                        {
                            throw setError(E_FAIL,
                                           tr("Could not open the hard disk storage unit '%s'%s"),
                                           (*it)->m->strLocationFull.raw(),
                                           that->vdError(vrc).raw());
                        }
                    }

                    Assert(that->m->state == MediumState_LockedWrite);

                    Utf8Str location(that->m->strLocationFull);

                    /* unlock before the potentially lengthy operation */
                    thatLock.leave();

                    vrc = VDCompact(hdd, VD_LAST_IMAGE, vdOperationIfaces);
                    if (RT_FAILURE(vrc))
                    {
                        if (vrc == VERR_NOT_SUPPORTED)
                            throw setError(VBOX_E_NOT_SUPPORTED,
                                           tr("Compacting is not yet supported for hard disk '%s'"),
                                           location.raw());
                        else if (vrc == VERR_NOT_IMPLEMENTED)
                            throw setError(E_NOTIMPL,
                                           tr("Compacting is not implemented, hard disk '%s'"),
                                           location.raw());
                        else
                            throw setError(E_FAIL,
                                           tr("Could not compact hard disk '%s'%s"),
                                           location.raw(),
                                           that->vdError(vrc).raw());
                    }
                }
                catch (HRESULT aRC) { rc = aRC; }

                VDDestroy(hdd);
            }
            catch (HRESULT aRC) { rc = aRC; }

            /* Everything is explicitly unlocked when the task exits,
             * as the task destruction also destroys the image chain. */

            break;
        }

        default:
            AssertFailedReturn(VERR_GENERAL_FAILURE);
    }

    /* complete the progress if run asynchronously */
    if (isAsync)
    {
        if (!task->m_pProgress.isNull())
            task->m_pProgress->notifyComplete(rc);
    }
    else
    {
        task->m_rc = rc;
    }

    LogFlowFunc(("rc=%Rhrc\n", rc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;

    /// @todo ugly hack, fix ComAssert... later
    #undef setError
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
