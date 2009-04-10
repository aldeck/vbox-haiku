/* $Revision$ */
/** @file tstXPCOMCGlue.c
 * Demonstrator program to illustrate use of C bindings of Main API.
 *
 * Linux only at the moment due to shared library magic in the Makefile.
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "VBoxXPCOMCGlue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static char *nsIDToString(nsID *guid);
static void listVMs(IVirtualBox *virtualBox, ISession *session);

int volatile g_refcount = 0;

/**
 * Helper function to convert an nsID into a human readable string.
 *
 * @returns result string, allocated. Has to be freed using free()
 * @param   guid Pointer to nsID that will be converted.
 */
static char *nsIDToString(nsID *guid)
{
    /* Avoid magic number 39. Yes, sizeof "literal" includes the NUL byte. */
    char *res = malloc(sizeof "{12345678-1234-1234-1234-123456789012}");

    if (res != NULL)
    {
        sprintf(res, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                (unsigned)guid->m0, (unsigned)guid->m1, (unsigned)guid->m2,
                (unsigned)guid->m3[0], (unsigned)guid->m3[1],
                (unsigned)guid->m3[2], (unsigned)guid->m3[3],
                (unsigned)guid->m3[4], (unsigned)guid->m3[5],
                (unsigned)guid->m3[6], (unsigned)guid->m3[7]);
    }
    return res;
}

static const char *GetStateName(PRUint32 machineState)
{
    switch (machineState)
    {
        case MachineState_Null:                return "<null>";
        case MachineState_PoweredOff:          return "PoweredOff";
        case MachineState_Saved:               return "Saved";
        case MachineState_Aborted:             return "Aborted";
        case MachineState_Running:             return "Running";
        case MachineState_Paused:              return "Paused";
        case MachineState_Stuck:               return "Stuck";
        case MachineState_Starting:            return "Starting";
        case MachineState_Stopping:            return "Stopping";
        case MachineState_Saving:              return "Saving";
        case MachineState_Restoring:           return "Restoring";
        case MachineState_Discarding:          return "Discarding";
        case MachineState_SettingUp:           return "SettingUp";
        default:                               return "no idea";
    }
}

static nsresult OnMousePointerShapeChange(
    IConsoleCallback *pThis,
    PRBool visible,
    PRBool alpha,
    PRUint32 xHot,
    PRUint32 yHot,
    PRUint32 width,
    PRUint32 height,
    PRUint8 * shape
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnMouseCapabilityChange(
    IConsoleCallback *pThis,
    PRBool supportsAbsolute,
    PRBool needsHostCursor
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnKeyboardLedsChange(
    IConsoleCallback *pThis,
    PRBool numLock,
    PRBool capsLock,
    PRBool scrollLock
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnStateChange(
    IConsoleCallback *pThis,
    PRUint32 state
) {
    printf("%d here\n",__LINE__);
    printf("OnStateChange: %s\n", GetStateName(state));
    fflush(stdout);
    return 0;
}

static nsresult OnAdditionsStateChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnDVDDriveChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnFloppyDriveChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnNetworkAdapterChange(
    IConsoleCallback *pThis,
    INetworkAdapter * networkAdapter
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnSerialPortChange(
    IConsoleCallback *pThis,
    ISerialPort * serialPort
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnParallelPortChange(
    IConsoleCallback *pThis,
    IParallelPort * parallelPort
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnStorageControllerChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnVRDPServerChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnUSBControllerChange(IConsoleCallback *pThis )
{
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnUSBDeviceStateChange(
    IConsoleCallback *pThis,
    IUSBDevice * device,
    PRBool attached,
    IVirtualBoxErrorInfo * error
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnSharedFolderChange(
    IConsoleCallback *pThis,
    PRUint32 scope
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnRuntimeError(
    IConsoleCallback *pThis,
    PRBool fatal,
    PRUnichar * id,
    PRUnichar * message
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnCanShowWindow(
    IConsoleCallback *pThis,
    PRBool * canShow
) {
    printf("%d here\n",__LINE__);
    return 0;
}

static nsresult OnShowWindow(
    IConsoleCallback *pThis,
    PRUint64 * winId
) {
    printf("%d here\n",__LINE__);
    return 0;
}


static nsresult AddRef(nsISupports *pThis)
{
    nsresult c;

    printf("AddRef\n");
    c = g_refcount++;
    return c;
}

static nsresult Release(nsISupports *pThis)
{
    nsresult c;
    printf("Release\n");

    c = g_refcount--;
    if (c == 0)
    {
        /* delete object */
#if 1 /* test */
        free(pThis->vtbl);
        free(pThis);
#endif
    }
    return c;
}

static nsresult QueryInterface(nsISupports *pThis, const nsID *iid, void **resultp)
{
    IConsoleCallback *that = (IConsoleCallback *)pThis;

    printf("QueryInterface\n");
    /* match iid */
    g_refcount++;
    *resultp = that;
    return 0;
}

static void registerCallBack(IVirtualBox *virtualBox, ISession *session, nsID *machineId)
{
    IConsole *console = NULL;
    nsresult rc;

    rc = virtualBox->vtbl->OpenExistingSession(virtualBox, session, machineId);
    session->vtbl->GetConsole(session, &console);
    if (console) {
        IConsoleCallback *consoleCallback = NULL;

        consoleCallback = calloc(1, sizeof(IConsoleCallback));
        consoleCallback->vtbl = calloc(1, sizeof(struct IConsoleCallback_vtbl));

        if (consoleCallback && consoleCallback->vtbl) {

            consoleCallback->vtbl->nsisupports.AddRef = &AddRef;
            consoleCallback->vtbl->nsisupports.Release = &Release;
            consoleCallback->vtbl->nsisupports.QueryInterface = &QueryInterface;
            consoleCallback->vtbl->OnMousePointerShapeChange = &OnMousePointerShapeChange;
            consoleCallback->vtbl->OnMouseCapabilityChange = &OnMouseCapabilityChange;
            consoleCallback->vtbl->OnKeyboardLedsChange =&OnKeyboardLedsChange;
            consoleCallback->vtbl->OnStateChange = &OnStateChange;
            consoleCallback->vtbl->OnAdditionsStateChange = &OnAdditionsStateChange;
            consoleCallback->vtbl->OnDVDDriveChange = &OnDVDDriveChange;
            consoleCallback->vtbl->OnFloppyDriveChange = &OnFloppyDriveChange;
            consoleCallback->vtbl->OnNetworkAdapterChange = &OnNetworkAdapterChange;
            consoleCallback->vtbl->OnSerialPortChange = &OnSerialPortChange;
            consoleCallback->vtbl->OnParallelPortChange = &OnParallelPortChange;
            consoleCallback->vtbl->OnStorageControllerChange = &OnStorageControllerChange;
            consoleCallback->vtbl->OnVRDPServerChange = &OnVRDPServerChange;
            consoleCallback->vtbl->OnUSBControllerChange = &OnUSBControllerChange;
            consoleCallback->vtbl->OnUSBDeviceStateChange = &OnUSBDeviceStateChange;
            consoleCallback->vtbl->OnSharedFolderChange = &OnSharedFolderChange;
            consoleCallback->vtbl->OnRuntimeError = &OnRuntimeError;
            consoleCallback->vtbl->OnCanShowWindow = &OnCanShowWindow;
            consoleCallback->vtbl->OnShowWindow = &OnShowWindow;
            g_refcount = 1;

            printf("%d here\n",__LINE__);
            console->vtbl->RegisterCallback(console, consoleCallback);
            printf("%d here\n",__LINE__);

            {
                int run = 10;
                while (run-- > 0) {
                    sleep(1);
                    printf("waiting here:%d\n",run);
                    fflush(stdout);
                }
            }
            console->vtbl->UnregisterCallback(console, consoleCallback);
        }
        /*consoleCallback->vtbl->Release(consoleCallback);*/
    }
    session->vtbl->Close((void *)session);
}

/**
 * List the registered VMs.
 *
 * @param   virtualBox ptr to IVirtualBox object
 * @param   session    ptr to ISession object
 */
static void listVMs(IVirtualBox *virtualBox, ISession *session)
{
    nsresult rc;
    IMachine **machines = NULL;
    PRUint32 machineCnt = 0;
    PRUint32 i;
    unsigned start_id;

    /*
     * Get the list of all registered VMs.
     */

    rc = virtualBox->vtbl->GetMachines(virtualBox, &machineCnt, &machines);
    if (NS_FAILED(rc))
    {
        fprintf(stderr, "could not get list of machines, rc=%08x\n",
            (unsigned)rc);
        return;
    }

    if (machineCnt == 0)
    {
        printf("\tNo VMs\n");
        return;
    }

    printf("VM List:\n\n");

    /*
     * Iterate through the collection.
     */

    for (i = 0; i < machineCnt; ++i)
    {
        IMachine *machine      = machines[i];
        PRBool    isAccessible = PR_FALSE;

        printf("\tMachine #%u\n", (unsigned)i);

        if (!machine)
        {
            printf("\t(skipped, NULL)\n");
            continue;
        }

        machine->vtbl->GetAccessible(machine, &isAccessible);

        if (isAccessible)
        {
            PRUnichar *machineNameUtf16;
            char *machineName;

            machine->vtbl->GetName(machine, &machineNameUtf16);
            g_pVBoxFuncs->pfnUtf16ToUtf8(machineNameUtf16,&machineName);
            printf("\tName:        %s\n", machineName);

            g_pVBoxFuncs->pfnUtf8Free(machineName);
            g_pVBoxFuncs->pfnComUnallocMem(machineNameUtf16);
        }
        else
        {
            printf("\tName:        <inaccessible>\n");
        }


        {
            nsID  *iid = NULL;
            char  *uuidString;

            machine->vtbl->GetId(machine, &iid);
            uuidString = nsIDToString(iid);
            printf("\tUUID:        %s\n", uuidString);

            free(uuidString);
            g_pVBoxFuncs->pfnComUnallocMem(iid);
        }

        if (isAccessible)
        {
            {
                PRUnichar *configFile;
                char      *configFile1 = calloc((size_t)64, (size_t)1);

                machine->vtbl->GetSettingsFilePath(machine, &configFile);
                g_pVBoxFuncs->pfnUtf16ToUtf8(configFile, &configFile1);
                printf("\tConfig file: %s\n", configFile1);

                free(configFile1);
                g_pVBoxFuncs->pfnComUnallocMem(configFile);
            }

            {
                PRUint32 memorySize;

                machine->vtbl->GetMemorySize(machine, &memorySize);
                printf("\tMemory size: %uMB\n", memorySize);
            }

            {
                PRUnichar *typeId;
                PRUnichar *osNameUtf16;
                char *osName;
                IGuestOSType *osType = NULL;

                machine->vtbl->GetOSTypeId(machine, &typeId);
                virtualBox->vtbl->GetGuestOSType(virtualBox, typeId, &osType);
                osType->vtbl->GetDescription(osType, &osNameUtf16);
                g_pVBoxFuncs->pfnUtf16ToUtf8(osNameUtf16,&osName);
                printf("\tGuest OS:    %s\n\n", osName);

                osType->vtbl->nsisupports.Release((void *)osType);
                g_pVBoxFuncs->pfnUtf8Free(osName);
                g_pVBoxFuncs->pfnComUnallocMem(osNameUtf16);
                g_pVBoxFuncs->pfnComUnallocMem(typeId);
            }
        }
    }

    /*
     * Let the user chose a machine to start.
     */

    printf("Type Machine# to start (0 - %u) or 'quit' to do nothing: ",
        (unsigned)(machineCnt - 1));
    fflush(stdout);

    if (scanf("%u", &start_id) == 1 && start_id < machineCnt)
    {
        IMachine *machine = machines[start_id];

        if (machine)
        {
            nsID  *iid = NULL;

            machine->vtbl->GetId(machine, &iid);
            /*startVM(virtualBox, session, iid);*/
            registerCallBack(virtualBox, session, iid);

            g_pVBoxFuncs->pfnComUnallocMem(iid);
        }
    }

    /*
     * Don't forget to release the objects in the array.
     */

    for (i = 0; i < machineCnt; ++i)
    {
        IMachine *machine = machines[i];

        if (machine)
        {
            machine->vtbl->nsisupports.Release((nsISupports *)machine);
        }
    }
}

/**
 * Start a VM.
 *
 * @param   virtualBox ptr to IVirtualBox object
 * @param   session    ptr to ISession object
 * @param   id         identifies the machine to start
 */
#if 0
static void startVM(IVirtualBox *virtualBox, ISession *session, nsID *id)
{
    nsresult rc;
    IMachine  *machine    = NULL;
    IProgress *progress   = NULL;
    PRUnichar *env        = NULL;
    PRUnichar *sessionType;

    rc = virtualBox->vtbl->GetMachine(virtualBox, id, &machine);

    if (NS_FAILED(rc) || !machine)
    {
        fprintf(stderr, "Error: Couldn't get the machine handle.\n");
        return;
    }

    g_pVBoxFuncs->pfnUtf8ToUtf16("gui", &sessionType);

    rc = virtualBox->vtbl->OpenRemoteSession(
        virtualBox,
        session,
        id,
        sessionType,
        env,
        &progress
    );

    g_pVBoxFuncs->pfnUtf16Free(sessionType);

    if (NS_FAILED(rc))
    {
        fprintf(stderr, "Error: OpenRemoteSession failed.\n");
    }
    else
    {
        PRBool completed;
        nsresult resultCode;

        printf("Waiting for the remote session to open...\n");
        progress->vtbl->WaitForCompletion(progress, -1);

        rc = progress->vtbl->GetCompleted(progress, &completed);
        if (NS_FAILED(rc))
        {
            fprintf (stderr, "Error: GetCompleted status failed.\n");
        }

        progress->vtbl->GetResultCode(progress, &resultCode);
        if (NS_FAILED(resultCode))
        {
            IVirtualBoxErrorInfo *errorInfo;
            PRUnichar *textUtf16;
            char *text;

            progress->vtbl->GetErrorInfo(progress, &errorInfo);
            errorInfo->vtbl->GetText(errorInfo, &textUtf16);
            g_pVBoxFuncs->pfnUtf16ToUtf8(textUtf16, &text);
            printf("Error: %s\n", text);

            g_pVBoxFuncs->pfnComUnallocMem(textUtf16);
            g_pVBoxFuncs->pfnUtf8Free(text);
        }
        else
        {
            fprintf(stderr, "Remote session has been successfully opened.\n");
        }
        progress->vtbl->nsisupports.Release((void *)progress);
    }

    /* It's important to always release resources. */
    machine->vtbl->nsisupports.Release((void *)machine);
}
#endif

/* Main - Start the ball rolling. */

int main(int argc, char **argv)
{
    IVirtualBox *vbox           = NULL;
    ISession   *session          = NULL;
    PRUint32    revision         = 0;
    PRUnichar  *versionUtf16     = NULL;
    PRUnichar  *homefolderUtf16  = NULL;
    nsresult    rc;     /* Result code of various function (method) calls. */

    printf("Starting Main\n");

    /*
     * VBoxComInitialize does all the necessary startup action and
     * provides us with pointers to vbox and session handles.
     * It should be matched by a call to VBoxComUninitialize(vbox)
     * when done.
     */

    if (VBoxCGlueInit() != 0)
    {
        fprintf(stderr, "%s: FATAL: VBoxCGlueInit failed: %s\n",
                argv[0], g_szVBoxErrMsg);
        return EXIT_FAILURE;
    }

    g_pVBoxFuncs->pfnComInitialize(&vbox, &session);
    if (vbox == NULL)
    {
        fprintf(stderr, "%s: FATAL: could not get vbox handle\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (session == NULL)
    {
        fprintf(stderr, "%s: FATAL: could not get session handle\n", argv[0]);
        return EXIT_FAILURE;
    }

    /*
     * Now ask for revision, version and home folder information of
     * this vbox. Were not using fancy macros here so it
     * remains easy to see how we access C++'s vtable.
     */

    printf("----------------------------------------------------\n");

    /* 1. Revision */

    rc = vbox->vtbl->GetRevision(vbox, &revision);
    if (NS_SUCCEEDED(rc))
    {
        printf("\tRevision: %u\n", revision);
    }
    else
    {
        fprintf(stderr, "%s: GetRevision() returned %08x\n",
            argv[0], (unsigned)rc);
    }

    /* 2. Version */

    rc = vbox->vtbl->GetVersion(vbox, &versionUtf16);
    if (NS_SUCCEEDED(rc))
    {
        char *version = NULL;
        g_pVBoxFuncs->pfnUtf16ToUtf8(versionUtf16, &version);
        printf("\tVersion: %s\n", version);
        g_pVBoxFuncs->pfnUtf8Free(version);
        g_pVBoxFuncs->pfnComUnallocMem(versionUtf16);
    }
    else
    {
        fprintf(stderr, "%s: GetVersion() returned %08x\n",
            argv[0], (unsigned)rc);
    }

    /* 3. Home Folder */

    rc = vbox->vtbl->GetHomeFolder(vbox, &homefolderUtf16);
    if (NS_SUCCEEDED(rc))
    {
        char *homefolder = NULL;
        g_pVBoxFuncs->pfnUtf16ToUtf8(homefolderUtf16, &homefolder);
        printf("\tHomeFolder: %s\n", homefolder);
        g_pVBoxFuncs->pfnUtf8Free(homefolder);
        g_pVBoxFuncs->pfnComUnallocMem(homefolderUtf16);
    }
    else
    {
        fprintf(stderr, "%s: GetHomeFolder() returned %08x\n",
            argv[0], (unsigned)rc);
    }

    listVMs(vbox, session);
    session->vtbl->Close(session);

    printf("----------------------------------------------------\n");

    /*
     * Do as mom told us: always clean up after yourself.
     */

    g_pVBoxFuncs->pfnComUninitialize();
    VBoxCGlueTerm();
    printf("Finished Main\n");

    return 0;
}
/* vim: set ts=4 sw=4 et: */
