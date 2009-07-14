/* $Id$ */
/** @file
 * IPRT - Process, Win32.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP RTLOGGROUP_PROCESS

#include <Windows.h>
#include <process.h>
#include <errno.h>

#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/env.h>


/*
 * This is from Winternl.h. It has been copied here
 * because the header does not define a calling convention for
 * its prototypes and just assumes that _stdcall is the standard
 * calling convention.
 */
typedef struct _PEB {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[229];
    PVOID Reserved3[59];
    ULONG SessionId;
} PEB, *PPEB;

typedef struct _PROCESS_BASIC_INFORMATION {
    PVOID Reserved1;
    PPEB PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
} PROCESS_BASIC_INFORMATION;

typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation = 0,
    ProcessWow64Information = 26
} PROCESSINFOCLASS;

extern "C" LONG WINAPI
NtQueryInformationProcess (
    IN HANDLE ProcessHandle,
    IN PROCESSINFOCLASS ProcessInformationClass,
    OUT PVOID ProcessInformation,
    IN ULONG ProcessInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );

/** @todo r=michael This function currently does not work correctly if the arguments
                    contain spaces. */
RTR3DECL(int)   RTProcCreate(const char *pszExec, const char * const *papszArgs, RTENV Env, unsigned fFlags, PRTPROCESS pProcess)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszExec, VERR_INVALID_POINTER);
    AssertReturn(*pszExec, VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(Env != NIL_RTENV, VERR_INVALID_PARAMETER);
    const char * const *papszEnv = RTEnvGetExecEnvP(Env);
    AssertPtrReturn(papszEnv, VERR_INVALID_HANDLE);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    AssertPtrReturn(*papszArgs, VERR_INVALID_PARAMETER);
    /* later: path searching. */

    /*
     * Spawn the child.
     */
    /** @todo utf-8 considerations! */
    HANDLE hProcess = (HANDLE)_spawnve(_P_NOWAITO, pszExec, papszArgs, papszEnv);
    if (hProcess != 0 && hProcess != INVALID_HANDLE_VALUE)
    {
        if (pProcess)
        {
            /*
             * GetProcessId requires XP SP1 or later
             */
#if defined(RT_ARCH_AMD64)
            *pProcess = GetProcessId(hProcess);
#else /* !RT_ARCH_AMD64 */
            static bool           fInitialized = false;
            static DWORD (WINAPI *pfnGetProcessId)(HANDLE Thread) = NULL;
            if (!fInitialized)
            {
                HMODULE hmodKernel32 = GetModuleHandle("KERNEL32.DLL");
                if (hmodKernel32)
                    pfnGetProcessId = (DWORD (WINAPI*)(HANDLE))GetProcAddress(hmodKernel32, "GetProcessId");
                fInitialized = true;
            }
            if (pfnGetProcessId)
            {
                *pProcess = pfnGetProcessId(hProcess);
                if (!*pProcess)
                {
                    int rc = RTErrConvertFromWin32(GetLastError());
                    AssertMsgFailed(("failed to get pid from hProcess=%#x rc=%Rrc\n", hProcess, rc));
                    return rc;
                }
            }
            else
            {
                /*
                 * Fall back to the NT api for older versions.
                 */
                PROCESS_BASIC_INFORMATION ProcInfo = {0};
                ULONG Status = NtQueryInformationProcess(hProcess, ProcessBasicInformation,
                                                         &ProcInfo, sizeof(ProcInfo), NULL);
                if (Status != 0)
                {
                    int rc = ERROR_INTERNAL_ERROR; /* (we don't have a valid conversion here, but this shouldn't happen anyway.) */
                    AssertMsgFailed(("failed to get pid from hProcess=%#x rc=%Rrc Status=%#x\n", hProcess, rc, Status));
                    return rc;
                }
                *pProcess = ProcInfo.UniqueProcessId;
            }
#endif  /* !RT_ARCH_AMD64 */
        }
        return VINF_SUCCESS;
    }

    int rc = RTErrConvertFromErrno(errno);
    AssertMsgFailed(("spawn/exec failed rc=%Rrc\n", rc)); /* this migth be annoying... */
    return rc;
}


RTR3DECL(int) RTProcWait(RTPROCESS Process, unsigned fFlags, PRTPROCSTATUS pProcStatus)
{
    AssertReturn(!(fFlags & ~(RTPROCWAIT_FLAGS_BLOCK | RTPROCWAIT_FLAGS_NOBLOCK)), VERR_INVALID_PARAMETER);

    /*
     * Open the process.
     */
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, Process);
    if (hProcess != NULL)
    {
        /*
         * Wait for it to terminate.
         */
        DWORD Millies = fFlags == RTPROCWAIT_FLAGS_BLOCK ? INFINITE : 0;
        DWORD WaitRc = WaitForSingleObjectEx(hProcess, Millies, TRUE);
        while (WaitRc == WAIT_IO_COMPLETION)
            WaitRc = WaitForSingleObjectEx(hProcess, Millies, TRUE);
        switch (WaitRc)
        {
            /*
             * It has terminated.
             */
            case WAIT_OBJECT_0:
            {
                DWORD dwExitCode;
                if (GetExitCodeProcess(hProcess, &dwExitCode))
                {
                    if (pProcStatus)
                    {
                        pProcStatus->enmReason = RTPROCEXITREASON_NORMAL;
                        pProcStatus->iStatus = (int)dwExitCode;
                    }
                    return VINF_SUCCESS;
                }
                break;
            }

            /*
             * It hasn't terminated just yet.
             */
            case WAIT_TIMEOUT:
                return VERR_PROCESS_RUNNING;

            /*
             * Something went wrong...
             */
            case WAIT_FAILED:
                break;
            case WAIT_ABANDONED:
                AssertFailed();
                return VERR_GENERAL_FAILURE;
            default:
                AssertMsgFailed(("WaitRc=%RU32\n", WaitRc));
                return VERR_GENERAL_FAILURE;
        }
    }
    DWORD dwErr = GetLastError();
    return RTErrConvertFromWin32(dwErr);
}


RTR3DECL(int) RTProcTerminate(RTPROCESS Process)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, Process);
    if (hProcess != NULL)
    {
        BOOL fRc = TerminateProcess(hProcess, 127);
        CloseHandle(hProcess);
        if (fRc)
            return VINF_SUCCESS;
    }
    DWORD dwErr = GetLastError();
    return RTErrConvertFromWin32(dwErr);
}


RTR3DECL(uint64_t) RTProcGetAffinityMask(void)
{
    DWORD_PTR dwProcessAffinityMask = 0xffffffff;
    DWORD_PTR dwSystemAffinityMask;

    BOOL fRc = GetProcessAffinityMask(GetCurrentProcess(), &dwProcessAffinityMask, &dwSystemAffinityMask);
    Assert(fRc);

    return dwProcessAffinityMask;
}

