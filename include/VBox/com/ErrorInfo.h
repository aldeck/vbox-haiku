/** @file
 *
 * MS COM / XPCOM Abstraction Layer:
 * ErrorInfo class declaration
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

#ifndef __VBox_com_ErrorInfo_h__
#define __VBox_com_ErrorInfo_h__

#include "VBox/com/ptr.h"
#include "VBox/com/string.h"
#include "VBox/com/Guid.h"
#include "VBox/com/assert.h"

#include <iprt/memory> // for auto_copy_ptr

struct IProgress;
struct IVirtualBoxErrorInfo;

namespace com
{

/**
 *  The ErrorInfo class provides a convenient way to retrieve error
 *  information set by the most recent interface method, that was invoked on
 *  the current thread and returned an unsuccessful result code.
 *
 *  Once the instance of this class is created, the error information for
 *  the current thread is cleared.
 *
 *  There is no sence to use instances of this class after the last
 *  invoked interface method returns a success.
 *
 *  The class usage pattern is as follows:
 *  <code>
 *      IFoo *foo;
 *      ...
 *      HRESULT rc = foo->SomeMethod();
 *      if (FAILED (rc)) {
 *          ErrorInfo info (foo);
 *          if (info.isFullAvailable()) {
 *              printf ("error message = %ls\n", info.getText().raw());
 *          }
 *      }
 *  </code>
 *
 *  This class fetches error information using the IErrorInfo interface on
 *  Win32 (MS COM) or the nsIException interface on other platforms (XPCOM),
 *  or the extended IVirtualBoxErrorInfo interface when when it is available
 *  (i.e. a given IErrorInfo or nsIException instance implements it).
 *  Currently, IVirtualBoxErrorInfo is only available for VirtualBox components.
 *
 *  ErrorInfo::isFullAvailable() and ErrorInfo::isBasicAvailable() determine
 *  what level of error information is available. If #isBasicAvailable()
 *  returns true, it means that only IErrorInfo or nsIException is available as
 *  the source of information (depending on the platform), but not
 *  IVirtualBoxErrorInfo. If #isFullAvailable() returns true, it means that all
 *  three interfaces are available. If both methods return false, no error info
 *  is available at all.
 *
 *  Here is a table of correspondence between this class methods and
 *  and IErrorInfo/nsIException/IVirtualBoxErrorInfo attributes/methods:
 *
 *  ErrorInfo       IErrorInfo      nsIException    IVirtualBoxErrorInfo
 *  --------------------------------------------------------------------
 *  getResultCode   --              result          resultCode
 *  getIID          GetGUID         --              interfaceID
 *  getComponent    GetSource       --              component
 *  getText         GetDescription  message         text
 *
 *  '--' means that this interface does not provide the corresponding portion
 *  of information, therefore it is useless to query it if only
 *  #isBasicAvailable() returns true. As it can be seen, the amount of
 *  information provided at the basic level, depends on the platform
 *  (MS COM or XPCOM).
 */
class ErrorInfo
{
public:

    /**
     *  Constructs a new, "interfaceless" ErrorInfo instance that takes
     *  the error information possibly set on the current thread by an
     *  interface method of some COM component or by the COM subsystem.
     *
     *  This constructor is useful, for example, after an unsuccessful attempt
     *  to instantiate (create) a component, so there is no any valid interface
     *  pointer available.
     */
    explicit ErrorInfo()
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        { init(); }

    /**
     *  Constructs a new, "interfaceless" ErrorInfo instance that takes
     *  the error information possibly set on the current thread by an
     *  interface method of the given interface pointer.

     *  If the given interface does not support providing error information or,
     *  for some reason didn't set any error information, both
     *  #isFullAvailable() and #isBasicAvailable() will return |false|.
     *
     *  @param aPtr pointer to the interface whose method returned an
     *              error
     */
    template <class I> ErrorInfo (I *aPtr)
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        { init (aPtr, COM_IIDOF(I)); }

    /**
     *  Constructs a new ErrorInfo instance from the smart interface pointer.
     *  See template <class I> ErrorInfo (I *aPtr) for details
     *
     *  @param aPtr smart pointer to the interface whose method returned
     *              an error
     */
    template <class I> ErrorInfo (const ComPtr <I> &aPtr)
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        { init (static_cast <I*> (aPtr), COM_IIDOF(I)); }

    /** Specialization for the IVirtualBoxErrorInfo smart pointer */
    ErrorInfo (const ComPtr <IVirtualBoxErrorInfo> &aPtr)
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        { init (aPtr); }

    /**
     *  Constructs a new ErrorInfo instance from the IVirtualBoxErrorInfo
     *  interface pointer. If this pointer is not NULL, both #isFullAvailable()
     *  and #isBasicAvailable() will return |true|.
     *
     *  @param aInfo    pointer to the IVirtualBoxErrorInfo interface that
     *                  holds error info to be fetched by this instance
     */
    ErrorInfo (IVirtualBoxErrorInfo *aInfo)
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        { init (aInfo); }

    virtual ~ErrorInfo();

    /**
     *  Returns whether basic error info is actually available for the current
     *  thread. If the instance was created from an interface pointer that
     *  supports basic error info and successfully provided it, or if it is an
     *  "interfaceless" instance and there is some error info for the current
     *  thread, the returned value will be true.
     *
     *  See the class description for details about the basic error info level.
     *
     *  The appropriate methods of this class provide meaningful info only when
     *  this method returns true (otherwise they simply return NULL-like values).
     */
    bool isBasicAvailable() const { return mIsBasicAvailable; }

    /**
     *  Returns whether full error info is actually available for the current
     *  thread. If the instance was created from an interface pointer that
     *  supports full error info and successfully provided it, or if it is an
     *  "interfaceless" instance and there is some error info for the current
     *  thread, the returned value will be true.
     *
     *  See the class description for details about the full error info level.
     *
     *  The appropriate methods of this class provide meaningful info only when
     *  this method returns true (otherwise they simply return NULL-like values).
     */
    bool isFullAvailable() const { return mIsFullAvailable; }

    /**
     *  Returns the COM result code of the failed operation.
     */
    HRESULT getResultCode() const { return mResultCode; }

    /**
     *  Returns the IID of the interface that defined the error.
     */
    const Guid &getInterfaceID() const { return mInterfaceID; }

    /**
     *  Returns the name of the component that generated the error.
     */
    const Bstr &getComponent() const { return mComponent; }

    /**
     *  Returns the textual description of the error.
     */
    const Bstr &getText() const { return mText; }

    /**
     *  Returns the next error information object or @c NULL if there is none.
     */
    const ErrorInfo *getNext() const { return mNext.get(); }

    /**
     *  Returns the name of the interface that defined the error
     */
    const Bstr &getInterfaceName() const { return mInterfaceName; }

    /**
     *  Returns the IID of the interface that returned the error.
     *
     *  This method returns a non-null IID only if the instance was created
     *  using #template <class I> ErrorInfo (I *i) or
     *  template <class I> ErrorInfo (const ComPtr <I> &i) constructor.
     */
    const Guid &getCalleeIID() const { return mCalleeIID; }

    /**
     *  Returns the name of the interface that returned the error
     *
     *  This method returns a non-null name only if the instance was created
     *  using #template <class I> ErrorInfo (I *i) or
     *  template <class I> ErrorInfo (const ComPtr <I> &i) constructor.
     */
    const Bstr &getCalleeName() const { return mCalleeName; }

    /**
     *  Prints error information stored in this instance to the console.
     *  Intended mainly for debugging and for simple command-line tools.
     *
     *  @param aPrefix  optional prefix
     */
    void print (const char *aPrefix = NULL);

protected:

    ErrorInfo (bool aDummy)
        : mIsBasicAvailable (false), mIsFullAvailable (false)
        , mResultCode (S_OK)
        {}

    void init (bool aKeepObj = false);
    void init (IUnknown *aUnk, const GUID &aIID, bool aKeepObj = false);
    void init (IVirtualBoxErrorInfo *aInfo);

    bool mIsBasicAvailable : 1;
    bool mIsFullAvailable : 1;

    HRESULT mResultCode;
    Guid mInterfaceID;
    Bstr mComponent;
    Bstr mText;

    cppx::auto_copy_ptr <ErrorInfo> mNext;

    Bstr mInterfaceName;
    Guid mCalleeIID;
    Bstr mCalleeName;

    ComPtr <IUnknown> mErrorInfo;
};

/**
 *  A convenience subclass of ErrorInfo that, given an IProgress interface
 *  pointer, reads its errorInfo attribute and uses the returned
 *  IVirtualBoxErrorInfo instance to construct itself.
 */
class ProgressErrorInfo : public ErrorInfo
{
public:

    /**
     *  Constructs a new instance by fetchig error information from the
     *  IProgress interface pointer. If the progress object is not NULL,
     *  its completed attribute is true, resultCode represents a failure,
     *  and the errorInfo attribute returns a valid IVirtualBoxErrorInfo pointer,
     *  both #isFullAvailable() and #isBasicAvailable() will return true.
     *
     *  @param  progress    the progress object representing a failed operation
     */
    ProgressErrorInfo (IProgress *progress);
};

/**
 *  A convenience subclass of ErrorInfo that allows to preserve the current
 *  error info. Instances of this class fetch an error info object set on the
 *  current thread and keep a reference to it, which allows to restore it
 *  later using the #restore() method. This is useful to preserve error
 *  information returned by some method for the duration of making another COM
 *  call that may set its own error info and overwrite the existing
 *  one. Preserving and restoring error information makes sense when some
 *  method wants to return error information set by other call as its own
 *  error information while it still needs to make another call before return.
 *
 *  The usage pattern is:
 *  <code>
 *      rc = foo->method();
 *      if (FAILED (rc))
 *      {
 *           ErrorInfoKeeper eik;
 *           ...
 *           // bar may return error info as well
 *           bar->method();
 *           ...
 *           // restore error info from foo to return it to the caller
 *           eik.restore();
 *           return rc;
 *      }
 *  </code>
 */
class ErrorInfoKeeper : public ErrorInfo
{
public:

    /** Constructs a new instance that will fetch the current error info. */
    ErrorInfoKeeper() : ErrorInfo (false), mForgot (false)
        { init (true /* aKeepObj */); }

    /**
     *  Destroys this instance and automatically calls #restore() which will
     *  either restorr error info fetched by the constructor or do nothing
     *  if #forget() was called before destruction. */
    ~ErrorInfoKeeper() { if (!mForgot) restore(); }

    /**
     *  Restores error info fetched by the constructor and forgets it
     *  afterwards.
     *
     *  @return COM result of the restore operation.
     */
    HRESULT restore();

    /**
     *  Forgets error info fetched by the constructor which prevents it from
     *  being restored by #restore() or by the destructor.
     */
    void forget() { mForgot = 0; }

private:

    bool mForgot : 1;
};

}; // namespace com

#endif // __VBox_com_ErrorInfo_h__

