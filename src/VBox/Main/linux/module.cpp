/** @file
 *
 * XPCOM module implementation functions
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

#include <nsIGenericFactory.h>
#include <VirtualBox_XPCOM.h>

#include <GuestImpl.h>
#include <KeyboardImpl.h>
#include <MouseImpl.h>
#include <DisplayImpl.h>
#include <MachineDebuggerImpl.h>
#include <USBDeviceImpl.h>
#include <RemoteUSBDeviceImpl.h>
#include <SharedFolderImpl.h>
#include <FramebufferImpl.h>
#include <ProgressImpl.h>
#include <NetworkAdapterImpl.h>

#include <SessionImpl.h>
#include <ConsoleImpl.h>
#include <ConsoleVRDPServer.h>

// XPCOM glue code unfolding

NS_DECL_CLASSINFO(Guest)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Guest, IGuest)
NS_DECL_CLASSINFO(Keyboard)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Keyboard, IKeyboard)
NS_DECL_CLASSINFO(Mouse)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Mouse, IMouse)
NS_DECL_CLASSINFO(Display)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Display, IDisplay)
NS_DECL_CLASSINFO(MachineDebugger)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(MachineDebugger, IMachineDebugger)
NS_DECL_CLASSINFO(InternalFramebuffer)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(InternalFramebuffer, IFramebuffer)
NS_DECL_CLASSINFO(Progress)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Progress, IProgress)
NS_DECL_CLASSINFO(CombinedProgress)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(CombinedProgress, IProgress)
NS_DECL_CLASSINFO(USBDevice)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(USBDevice, IUSBDevice)
NS_DECL_CLASSINFO(RemoteUSBDevice)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(RemoteUSBDevice, IHostUSBDevice)
NS_DECL_CLASSINFO(SharedFolder)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(SharedFolder, ISharedFolder)
NS_DECL_CLASSINFO(RemoteDisplayInfo)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(RemoteDisplayInfo, IRemoteDisplayInfo)

NS_DECL_CLASSINFO(Session)
NS_IMPL_THREADSAFE_ISUPPORTS2_CI(Session, ISession, IInternalSessionControl)
NS_DECL_CLASSINFO(Console)
NS_IMPL_THREADSAFE_ISUPPORTS1_CI(Console, IConsole)

COM_IMPL_READONLY_ENUM_AND_COLLECTION(USBDevice)
COM_IMPL_READONLY_ENUM_AND_COLLECTION_FOR(RemoteUSBDevice, IHostUSBDevice)
COM_IMPL_READONLY_ENUM_AND_COLLECTION(SharedFolder)

/**
 *  Singleton class factory that holds the reference to the created instance
 *  (preventing it from being destroyed) until the module is explicitly
 *  unloaded by the XPCOM shutdown code.
 *
 *  Suitable for IN-PROC components.
 */
class SessionClassFactory : public Session
{
public:
    virtual ~SessionClassFactory() {
        FinalRelease();
        instance = 0;
    }
    static nsresult getInstance (Session **inst) {
        int rv = NS_OK;
        if (instance == 0) {
            instance = new SessionClassFactory();
            if (instance) {
                instance->AddRef(); // protect FinalConstruct()
                rv = instance->FinalConstruct();
                if (NS_FAILED(rv))
                    instance->Release();
                else
                    instance->AddRef(); // self-reference
            } else {
                rv = NS_ERROR_OUT_OF_MEMORY;
            }
        } else {
            instance->AddRef();
        }
        *inst = instance;
        return rv;
    }
    static nsresult releaseInstance () {
        if (instance)
            instance->Release();
        return NS_OK;
    }

private:
    static Session *instance;
};

/** @note this is for singleton; disabled for now */
//
//Session *SessionClassFactory::instance = 0;
//
//NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR_WITH_RC (
//    Session, SessionClassFactory::getInstance
//)

NS_GENERIC_FACTORY_CONSTRUCTOR_WITH_RC (Session)

static const nsModuleComponentInfo components[] =
{
    {
        "Session component", NS_SESSION_CID, NS_SESSION_CONTRACTID,
        SessionConstructor,
        NULL, // registration function
        NULL, // deregistration function
/** @note this is for singleton; disabled for now */
//        SessionClassFactory::releaseInstance,
        NULL, // destructor function
        NS_CI_INTERFACE_GETTER_NAME(Session),
        NULL, // language helper
        &NS_CLASSINFO_NAME(Session)
    }
};

NS_IMPL_NSGETMODULE(SessionModule, components)

