/* $Id$ */
/** @file
 *
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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

#ifndef ____H_PROGRESSIMPL
#define ____H_PROGRESSIMPL

#include "VirtualBoxBase.h"

#include <VBox/com/SupportErrorInfo.h>

#include <iprt/semaphore.h>

#include <vector>

class VirtualBox;

////////////////////////////////////////////////////////////////////////////////

/**
 * Base component class for progress objects.
 */
class ATL_NO_VTABLE ProgressBase :
    public VirtualBoxBase,
    public com::SupportErrorInfoBase,
    public VirtualBoxSupportTranslation<ProgressBase>,
    VBOX_SCRIPTABLE_IMPL(IProgress)
{
protected:

    VIRTUALBOXBASE_ADD_ERRORINFO_SUPPORT (ProgressBase)

    DECLARE_EMPTY_CTOR_DTOR (ProgressBase)

    HRESULT FinalConstruct();

    // protected initializer/uninitializer for internal purposes only
    HRESULT protectedInit (AutoInitSpan &aAutoInitSpan,
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription, OUT_GUID aId = NULL);
    HRESULT protectedInit (AutoInitSpan &aAutoInitSpan);
    void protectedUninit (AutoUninitSpan &aAutoUninitSpan);

public:

    // IProgress properties
    STDMETHOD(COMGETTER(Id)) (BSTR *aId);
    STDMETHOD(COMGETTER(Description)) (BSTR *aDescription);
    STDMETHOD(COMGETTER(Initiator)) (IUnknown **aInitiator);

    // IProgress properties
    STDMETHOD(COMGETTER(Cancelable)) (BOOL *aCancelable);
    STDMETHOD(COMGETTER(Percent)) (ULONG *aPercent);
    STDMETHOD(COMGETTER(TimeRemaining)) (LONG *aTimeRemaining);
    STDMETHOD(COMGETTER(Completed)) (BOOL *aCompleted);
    STDMETHOD(COMGETTER(Canceled)) (BOOL *aCanceled);
    STDMETHOD(COMGETTER(ResultCode)) (LONG *aResultCode);
    STDMETHOD(COMGETTER(ErrorInfo)) (IVirtualBoxErrorInfo **aErrorInfo);
    STDMETHOD(COMGETTER(OperationCount)) (ULONG *aOperationCount);
    STDMETHOD(COMGETTER(Operation)) (ULONG *aCount);
    STDMETHOD(COMGETTER(OperationDescription)) (BSTR *aOperationDescription);
    STDMETHOD(COMGETTER(OperationPercent)) (ULONG *aOperationPercent);
    STDMETHOD(COMSETTER(Timeout)) (ULONG aTimeout);
    STDMETHOD(COMGETTER(Timeout)) (ULONG *aTimeout);

    // public methods only for internal purposes

    static HRESULT setErrorInfoOnThread (IProgress *aProgress);
    bool setCancelCallback(void (*pfnCallback)(void *), void *pvUser);


    // unsafe inline public methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)

    BOOL getCompleted() const { return mCompleted; }
    HRESULT getResultCode() const { return mResultCode; }
    double calcTotalPercent();

protected:
    void checkForAutomaticTimeout(void);

#if !defined (VBOX_COM_INPROC)
    /** Weak parent. */
    const ComObjPtr<VirtualBox, ComWeakRef> mParent;
#endif

    const ComPtr<IUnknown> mInitiator;

    const Guid mId;
    const Bstr mDescription;

    uint64_t m_ullTimestamp;                        // progress object creation timestamp, for ETA computation

    void (*m_pfnCancelCallback)(void *);
    void *m_pvCancelUserArg;

    /* The fields below are to be properly initalized by subclasses */

    BOOL mCompleted;
    BOOL mCancelable;
    BOOL mCanceled;
    HRESULT mResultCode;
    ComPtr<IVirtualBoxErrorInfo> mErrorInfo;

    ULONG m_cOperations;                            // number of operations (so that progress dialog can display something like 1/3)
    ULONG m_ulTotalOperationsWeight;                // sum of weights of all operations, given to constructor

    ULONG m_ulOperationsCompletedWeight;            // summed-up weight of operations that have been completed; initially 0

    ULONG m_ulCurrentOperation;                     // operations counter, incremented with each setNextOperation()
    Bstr m_bstrOperationDescription;                // name of current operation; initially from constructor, changed with setNextOperation()
    ULONG m_ulCurrentOperationWeight;               // weight of current operation, given to setNextOperation()
    ULONG m_ulOperationPercent;                     // percentage of current operation, set with setCurrentOperationProgress()
    ULONG m_cMsTimeout;                             /**< Automatic timeout value. 0 means none. */
};

////////////////////////////////////////////////////////////////////////////////

/**
 * Normal progress object.
 */
class ATL_NO_VTABLE Progress :
    public com::SupportErrorInfoDerived<ProgressBase, Progress, IProgress>,
    public VirtualBoxSupportTranslation<Progress>
{

public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE (Progress)

    DECLARE_NOT_AGGREGATABLE (Progress)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP (Progress)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IProgress)
        COM_INTERFACE_ENTRY2 (IDispatch, IProgress)
    END_COM_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only

    /**
     * Simplified constructor for progress objects that have only one
     * operation as a task.
     * @param aParent
     * @param aInitiator
     * @param aDescription
     * @param aCancelable
     * @param aId
     * @return
     */
    HRESULT init(
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription,
                  BOOL aCancelable,
                  OUT_GUID aId = NULL)
    {
        return init(
#if !defined (VBOX_COM_INPROC)
            aParent,
#endif
            aInitiator,
            aDescription,
            aCancelable,
            1,      // cOperations
            1,      // ulTotalOperationsWeight
            aDescription, // bstrFirstOperationDescription
            1,      // ulFirstOperationWeight
            aId);
    }

    /**
     * Not quite so simplified constructor for progress objects that have
     * more than one operation, but all sub-operations are weighed the same.
     * @param aParent
     * @param aInitiator
     * @param aDescription
     * @param aCancelable
     * @param cOperations
     * @param bstrFirstOperationDescription
     * @param aId
     * @return
     */
    HRESULT init(
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription, BOOL aCancelable,
                  ULONG cOperations,
                  CBSTR bstrFirstOperationDescription,
                  OUT_GUID aId = NULL)
    {
        return init(
#if !defined (VBOX_COM_INPROC)
            aParent,
#endif
            aInitiator,
            aDescription,
            aCancelable,
            cOperations,      // cOperations
            cOperations,      // ulTotalOperationsWeight = cOperations
            bstrFirstOperationDescription, // bstrFirstOperationDescription
            1,      // ulFirstOperationWeight: weigh them all the same
            aId);
    }

    HRESULT init(
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription,
                  BOOL aCancelable,
                  ULONG cOperations,
                  ULONG ulTotalOperationsWeight,
                  CBSTR bstrFirstOperationDescription,
                  ULONG ulFirstOperationWeight,
                  OUT_GUID aId = NULL);

    HRESULT init(BOOL aCancelable,
                 ULONG aOperationCount,
                 CBSTR aOperationDescription);

    void uninit();

    // IProgress methods
    STDMETHOD(WaitForCompletion)(LONG aTimeout);
    STDMETHOD(WaitForOperationCompletion)(ULONG aOperation, LONG aTimeout);
    STDMETHOD(Cancel)();

    STDMETHOD(SetCurrentOperationProgress)(ULONG aPercent);
    STDMETHOD(SetNextOperation)(IN_BSTR bstrNextOperationDescription, ULONG ulNextOperationsWeight);

    // public methods only for internal purposes

    HRESULT setResultCode(HRESULT aResultCode);

    HRESULT notifyComplete(HRESULT aResultCode);
    HRESULT notifyComplete(HRESULT aResultCode,
                           const GUID &aIID,
                           const Bstr &aComponent,
                           const char *aText, ...);
    bool notifyPointOfNoReturn(void);

    /** For com::SupportErrorInfoImpl. */
    static const char *ComponentName() { return "Progress"; }

private:

    RTSEMEVENTMULTI mCompletedSem;
    ULONG mWaitersCount;
};

////////////////////////////////////////////////////////////////////////////////

/**
 * The CombinedProgress class allows to combine several progress objects to a
 * single progress component. This single progress component will treat all
 * operations of individual progress objects as a single sequence of operations
 * that follow each other in the same order as progress objects are passed to
 * the #init() method.
 *
 * Individual progress objects are sequentially combined so that this progress
 * object:
 *
 *  -   is cancelable only if all progresses are cancelable.
 *  -   is canceled once a progress that follows next to successfully completed
 *      ones reports it was canceled.
 *  -   is completed successfully only after all progresses are completed
 *      successfully.
 *  -   is completed unsuccessfully once a progress that follows next to
 *      successfully completed ones reports it was completed unsuccessfully;
 *      the result code and error info of the unsuccessful progress
 *      will be reported as the result code and error info of this progress.
 *  -   returns N as the operation number, where N equals to the number of
 *      operations in all successfully completed progresses starting from the
 *      first one plus the operation number of the next (not yet complete)
 *      progress; the operation description of the latter one is reported as
 *      the operation description of this progress object.
 *  -   returns P as the percent value, where P equals to the sum of percents
 *      of all successfully completed progresses starting from the
 *      first one plus the percent value of the next (not yet complete)
 *      progress, normalized to 100%.
 *
 * @note It's the respoisibility of the combined progress object creator to
 *       complete individual progresses in the right order: if, let's say, the
 *       last progress is completed before all previous ones,
 *       #WaitForCompletion(-1) will most likely give 100% CPU load because it
 *       will be in a loop calling a method that returns immediately.
 */
class ATL_NO_VTABLE CombinedProgress :
    public com::SupportErrorInfoDerived<ProgressBase, CombinedProgress, IProgress>,
    public VirtualBoxSupportTranslation<CombinedProgress>
{

public:

    VIRTUALBOXSUPPORTTRANSLATION_OVERRIDE (CombinedProgress)

    DECLARE_NOT_AGGREGATABLE (CombinedProgress)

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    BEGIN_COM_MAP (CombinedProgress)
        COM_INTERFACE_ENTRY  (ISupportErrorInfo)
        COM_INTERFACE_ENTRY  (IProgress)
        COM_INTERFACE_ENTRY2 (IDispatch, IProgress)
    END_COM_MAP()

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only

    HRESULT init (
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription,
                  IProgress *aProgress1, IProgress *aProgress2,
                  OUT_GUID aId = NULL);

    /**
     * Initializes the combined progress object given the first and the last
     * normal progress object from the list.
     *
     * @param aParent       See ProgressBase::init().
     * @param aInitiator    See ProgressBase::init().
     * @param aDescription  See ProgressBase::init().
     * @param aFirstProgress Iterator of the first normal progress object.
     * @param aSecondProgress Iterator of the last normal progress object.
     * @param aId           See ProgressBase::init().
     */
    template <typename InputIterator>
    HRESULT init (
#if !defined (VBOX_COM_INPROC)
                  VirtualBox *aParent,
#endif
                  IUnknown *aInitiator,
                  CBSTR aDescription,
                  InputIterator aFirstProgress, InputIterator aLastProgress,
                  OUT_GUID aId = NULL)
    {
        /* Enclose the state transition NotReady->InInit->Ready */
        AutoInitSpan autoInitSpan (this);
        AssertReturn (autoInitSpan.isOk(), E_FAIL);

        mProgresses = ProgressVector (aFirstProgress, aLastProgress);

        HRESULT rc = protectedInit (autoInitSpan,
#if !defined (VBOX_COM_INPROC)
                                    aParent,
#endif
                                    aInitiator, aDescription, aId);

        /* Confirm a successful initialization when it's the case */
        if (SUCCEEDED (rc))
            autoInitSpan.setSucceeded();

        return rc;
    }

protected:

    HRESULT protectedInit (AutoInitSpan &aAutoInitSpan,
#if !defined (VBOX_COM_INPROC)
                           VirtualBox *aParent,
#endif
                           IUnknown *aInitiator,
                           CBSTR aDescription, OUT_GUID aId);

public:

    void uninit();

    // IProgress properties
    STDMETHOD(COMGETTER(Percent)) (ULONG *aPercent);
    STDMETHOD(COMGETTER(Completed)) (BOOL *aCompleted);
    STDMETHOD(COMGETTER(Canceled)) (BOOL *aCanceled);
    STDMETHOD(COMGETTER(ResultCode)) (LONG *aResultCode);
    STDMETHOD(COMGETTER(ErrorInfo)) (IVirtualBoxErrorInfo **aErrorInfo);
    STDMETHOD(COMGETTER(Operation)) (ULONG *aCount);
    STDMETHOD(COMGETTER(OperationDescription)) (BSTR *aOperationDescription);
    STDMETHOD(COMGETTER(OperationPercent)) (ULONG *aOperationPercent);
    STDMETHOD(COMSETTER(Timeout)) (ULONG aTimeout);
    STDMETHOD(COMGETTER(Timeout)) (ULONG *aTimeout);

    // IProgress methods
    STDMETHOD(WaitForCompletion) (LONG aTimeout);
    STDMETHOD(WaitForOperationCompletion) (ULONG aOperation, LONG aTimeout);
    STDMETHOD(Cancel)();

    STDMETHOD(SetCurrentOperationProgress)(ULONG aPercent)
    {
        NOREF(aPercent);
        return E_NOTIMPL;
    }

    STDMETHOD(SetNextOperation)(IN_BSTR bstrNextOperationDescription, ULONG ulNextOperationsWeight)
    {
        NOREF(bstrNextOperationDescription); NOREF(ulNextOperationsWeight);
        return E_NOTIMPL;
    }

    // public methods only for internal purposes

    /** For com::SupportErrorInfoImpl. */
    static const char *ComponentName() { return "CombinedProgress"; }

private:

    HRESULT checkProgress();

    typedef std::vector <ComPtr<IProgress> > ProgressVector;
    ProgressVector mProgresses;

    size_t mProgress;
    ULONG mCompletedOperations;
};

#endif /* ____H_PROGRESSIMPL */

