/* $Id$ */
/** @file
 * VMM DBGF - Debugger Facility.
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


/** @page   pg_dbgf     DBGC - The Debugger Facility
 *
 * The purpose of the DBGC is to provide an interface for debuggers to manipulate
 * the VMM without having to mess up the source code for each of them. The DBGF
 * is always built in and will always work when a debugger attaches to the VM.
 * The DBGF provides the basic debugger features, such as halting execution,
 * handling breakpoints and single step execution.
 *
 * The interface is working in a manner similar to the win32, linux and os2
 * debugger interfaces. It interface has an asynchronous nature. This comes from
 * the fact that the VMM and the Debugger are running in different threads.
 * They are refered to as the "emulation thread" and the "debugger thread",
 * or as the "ping thread" and the "pong thread, respectivly. (The last set
 * of names comes from the use of the Ping-Pong synchronization construct from
 * the RTSem API.)
 *
 *
 *
 * @section sec_dbgf_scenario   Debugger Scenario
 *
 * The debugger starts by attaching to the VM. For pratical reasons we limit the
 * number of concurrently attached debuggers to 1 per VM. The action of attaching
 * to the VM causes the VM to check and generate debug events.
 *
 * The debugger then will wait/poll for debug events and issue commands.
 *
 * The waiting and polling is done by the DBGFEventWait() function. It will wait
 * for the emulation thread to send a ping, thus indicating that there is an
 * event waiting to be processed.
 *
 * An event can be a respons to an command issued previously, the hitting of a
 * breakpoint, or running into a bad/fatal VMM condition. The debugger now have
 * the ping and must respond to the event at hand - the VMM is waiting.  This
 * usually means that the user of the debugger must do something, but it doesn't
 * have to. The debugger is free to call any DBGF function (nearly at least)
 * while processing the event.
 *
 * Typically the user will issue a request for the execution to be resumed, so
 * the debugger calls DBGFResume() and goes back to waiting/polling for events.
 *
 * When the user eventually terminates the debugging session or selects another
 * VM, the debugger detaches from the VM. This means that breakpoints are disabled
 * and that the emulation thread no longer polls for debugger commands.
 *
 */



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/dbgf.h>
#include <VBox/selm.h>
#include <VBox/rem.h>
#include <VBox/em.h>
#include "DBGFInternal.h"
#include <VBox/vm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/stream.h>


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int dbgfr3VMMWait(PVM pVM);
static int dbgfr3VMMCmd(PVM pVM, DBGFCMD enmCmd, PDBGFCMDDATA pCmdData, bool *pfResumeExecution);


/**
 * Sets the VMM Debug Command variable.
 *
 * @returns Previous command.
 * @param   pVM     VM Handle.
 * @param   enmCmd  The command.
 */
DECLINLINE(DBGFCMD) dbgfr3SetCmd(PVM pVM, DBGFCMD enmCmd)
{
    DBGFCMD rc;
    if (enmCmd == DBGFCMD_NO_COMMAND)
    {
        Log2(("DBGF: Setting command to %d (DBGFCMD_NO_COMMAND)\n", enmCmd));
        rc = (DBGFCMD)ASMAtomicXchgU32((uint32_t volatile *)(void *)&pVM->dbgf.s.enmVMMCmd, enmCmd);
        VM_FF_CLEAR(pVM, VM_FF_DBGF);
    }
    else
    {
        Log2(("DBGF: Setting command to %d\n", enmCmd));
        AssertMsg(pVM->dbgf.s.enmVMMCmd == DBGFCMD_NO_COMMAND, ("enmCmd=%d enmVMMCmd=%d\n", enmCmd, pVM->dbgf.s.enmVMMCmd));
        rc = (DBGFCMD)ASMAtomicXchgU32((uint32_t volatile *)(void *)&pVM->dbgf.s.enmVMMCmd, enmCmd);
        VM_FF_SET(pVM, VM_FF_DBGF);
        VMR3NotifyFF(pVM, false /* didn't notify REM */);
    }
    return rc;
}


/**
 * Initializes the DBGF.
 *
 * @returns VBox status code.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(int) DBGFR3Init(PVM pVM)
{
    int rc = dbgfR3InfoInit(pVM);
    if (VBOX_SUCCESS(rc))
        rc = dbgfR3SymInit(pVM);
    if (VBOX_SUCCESS(rc))
        rc = dbgfR3BpInit(pVM);
    return rc;
}


/**
 * Termiantes and cleans up resources allocated by the DBGF.
 *
 * @returns VBox status code.
 * @param   pVM     VM Handle.
 */
DBGFR3DECL(int) DBGFR3Term(PVM pVM)
{
    int rc;

    /*
     * Send a termination event to any attached debuggers.
     */
    /* wait for any pending events or whatever */
    if (    pVM->dbgf.s.fAttached
        &&  pVM->dbgf.s.PingPong.enmSpeaker == RTPINGPONGSPEAKER_PONG)
    {
        do  rc = RTSemPingWait(&pVM->dbgf.s.PingPong, 5000);
        while (pVM->dbgf.s.fAttached);
    }

    /* now, send the event */
    if (    pVM->dbgf.s.fAttached
        &&  pVM->dbgf.s.PingPong.enmSpeaker != RTPINGPONGSPEAKER_PONG)
    {
        pVM->dbgf.s.DbgEvent.enmType = DBGFEVENT_TERMINATING;
        pVM->dbgf.s.DbgEvent.enmCtx  = DBGFEVENTCTX_OTHER;
        rc = RTSemPing(&pVM->dbgf.s.PingPong);
        if (VBOX_SUCCESS(rc))
        {
            /*
             * Waits for the debugger to detach.
             */
            while (pVM->dbgf.s.fAttached)
            {
                /*
                 * Wait.
                 */
                rc = RTSemPingWait(&pVM->dbgf.s.PingPong, 1000);

                /*
                 * Process the command.
                 */
                DBGFCMDDATA     CmdData = pVM->dbgf.s.VMMCmdData;
                DBGFCMD         enmCmd = dbgfr3SetCmd(pVM, DBGFCMD_NO_COMMAND);
                if (enmCmd != DBGFCMD_NO_COMMAND)
                {
                    bool fResumeExecution = false;
                    rc = dbgfr3VMMCmd(pVM, enmCmd, &CmdData, &fResumeExecution);
                    if (enmCmd == DBGFCMD_DETACH_DEBUGGER)
                        break;
                }
                else if (VBOX_FAILURE(rc))
                    break;
            }
        }
    }

    /*
     * Terminate the other bits.
     */
    dbgfR3InfoTerm(pVM);
    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM         VM handle.
 * @param   offDelta    Relocation delta relative to old location.
 */
DBGFR3DECL(void) DBGFR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
}


/**
 * Waits a little while for a debuggger to attach.
 *
 * @returns True is a debugger have attached.
 * @param   pVM         VM handle.
 * @param   enmEvent    Event.
 */
bool dbgfR3WaitForAttach(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    /*
     * First a message.
     */
#ifndef __L4__

    RTStrmPrintf(g_pStdErr, "DBGF: No debugger attached, waiting 15 seconds for one to attach (event=%d)\n", enmEvent);
    RTStrmFlush(g_pStdErr);
# ifdef DEBUG_sandervl
    int cWait = 10;
# else
    int cWait = 150;
# endif
    while (cWait > 0)
    {
        RTThreadSleep(100);
        if (pVM->dbgf.s.fAttached)
        {
            RTStrmPrintf(g_pStdErr, "Attached!\n");
            RTStrmFlush(g_pStdErr);
            return true;
        }

        /* next */
        if (!(cWait % 10))
        {
            RTStrmPrintf(g_pStdErr, "%d.", cWait / 10);
            RTStrmFlush(g_pStdErr);
        }
        cWait--;
    }
#endif

    RTStrmPrintf(g_pStdErr, "Stopping the VM!\n");
    RTStrmFlush(g_pStdErr);
    return false;
}


/**
 * Forced action callback.
 * The VMM will call this from it's main loop when VM_FF_DBGF is set.
 *
 * The function checks and executes pending commands from the debugger.
 *
 * @returns VINF_SUCCESS normally.
 * @returns VERR_DBGF_RAISE_FATAL_ERROR to pretend a fatal error happend.
 * @param   pVM         VM Handle.
 */
DBGFR3DECL(int) DBGFR3VMMForcedAction(PVM pVM)
{
    /*
     * Clear the FF DBGF request flag.
     */
    Assert(pVM->fForcedActions & VM_FF_DBGF);
    VM_FF_CLEAR(pVM, VM_FF_DBGF);

    /*
     * Commands?
     */
    int rc = VINF_SUCCESS;
    if (pVM->dbgf.s.enmVMMCmd != DBGFCMD_NO_COMMAND)
    {
        /** @todo stupid GDT/LDT sync hack. go away! */
        SELMR3UpdateFromCPUM(pVM);

        /*
         * Process the command.
         */
        bool            fResumeExecution;
        DBGFCMDDATA     CmdData = pVM->dbgf.s.VMMCmdData;
        DBGFCMD         enmCmd = dbgfr3SetCmd(pVM, DBGFCMD_NO_COMMAND);
        rc = dbgfr3VMMCmd(pVM, enmCmd, &CmdData, &fResumeExecution);
        if (!fResumeExecution)
            rc = dbgfr3VMMWait(pVM);
    }
    return rc;
}


/**
 * Flag whether the event implies that we're stopped in the hypervisor code
 * and have to block certain operations.
 *
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event.
 */
static void dbgfR3EventSetStoppedInHyperFlag(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    switch (enmEvent)
    {
        case DBGFEVENT_STEPPED_HYPER:
        case DBGFEVENT_ASSERTION_HYPER:
        case DBGFEVENT_BREAKPOINT_HYPER:
            pVM->dbgf.s.fStoppedInHyper = true;
            break;
        default:
            pVM->dbgf.s.fStoppedInHyper = false;
            break;
    }
}


/**
 * Try determin the event context.
 *
 * @returns debug event context.
 * @param   pVM         The VM handle.
 */
static DBGFEVENTCTX dbgfR3FigureEventCtx(PVM pVM)
{
    switch (EMGetState(pVM))
    {
        case EMSTATE_RAW:
        case EMSTATE_DEBUG_GUEST_RAW:
            return DBGFEVENTCTX_RAW;

        case EMSTATE_REM:
        case EMSTATE_DEBUG_GUEST_REM:
            return DBGFEVENTCTX_REM;

        case EMSTATE_DEBUG_HYPER:
        case EMSTATE_GURU_MEDITATION:
            return DBGFEVENTCTX_HYPER;

        default:
            return DBGFEVENTCTX_OTHER;
    }
}

/**
 * The common event prologue code.
 * It will set the 'stopped-in-hyper' flag, make sure someone's attach,
 * and perhaps process any high priority pending actions (none yet).
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event to be sent.
 */
static int dbgfR3EventPrologue(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    /*
     * Check if a debugger is attached.
     */
    if (    !pVM->dbgf.s.fAttached
        &&  !dbgfR3WaitForAttach(pVM, enmEvent))
    {
        Log(("DBGFR3VMMEventSrc: enmEvent=%d - debugger not attached\n", enmEvent));
        return VERR_DBGF_NOT_ATTACHED;
    }

    /*
     * Sync back the state from the REM.
     */
    dbgfR3EventSetStoppedInHyperFlag(pVM, enmEvent);
    if (!pVM->dbgf.s.fStoppedInHyper)
        REMR3StateUpdate(pVM);

    /*
     * Look thru pending commands and finish those which make sense now.
     */
    /** @todo Process/purge pending commands. */
    //int rc = DBGFR3VMMForcedAction(pVM);
    return VINF_SUCCESS;
}


/**
 * Sends the event in the event buffer.
 *
 * @returns VBox status code.
 * @param   pVM     The VM handle.
 */
static int dbgfR3SendEvent(PVM pVM)
{
    int rc = RTSemPing(&pVM->dbgf.s.PingPong);
    if (VBOX_SUCCESS(rc))
        rc = dbgfr3VMMWait(pVM);

    pVM->dbgf.s.fStoppedInHyper = false;
    /** @todo sync VMM -> REM after exitting the debugger. everything may change while in the debugger! */
    return rc;
}


/**
 * Send a generic debugger event which takes no data.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event to send.
 */
DBGFR3DECL(int) DBGFR3Event(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    int rc = dbgfR3EventPrologue(pVM, enmEvent);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    pVM->dbgf.s.DbgEvent.enmType = enmEvent;
    pVM->dbgf.s.DbgEvent.enmCtx  = dbgfR3FigureEventCtx(pVM);
    return dbgfR3SendEvent(pVM);
}


/**
 * Send a debugger event which takes the full source file location.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event to send.
 * @param   pszFile     Source file.
 * @param   uLine       Line number in source file.
 * @param   pszFunction Function name.
 * @param   pszFormat   Message which accompanies the event.
 * @param   ...         Message arguments.
 */
DBGFR3DECL(int) DBGFR3EventSrc(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszFile, unsigned uLine, const char *pszFunction, const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int rc = DBGFR3EventSrcV(pVM, enmEvent, pszFile, uLine, pszFunction, pszFormat, args);
    va_end(args);
    return rc;
}


/**
 * Send a debugger event which takes the full source file location.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event to send.
 * @param   pszFile     Source file.
 * @param   uLine       Line number in source file.
 * @param   pszFunction Function name.
 * @param   pszFormat   Message which accompanies the event.
 * @param   args        Message arguments.
 */
DBGFR3DECL(int) DBGFR3EventSrcV(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszFile, unsigned uLine, const char *pszFunction, const char *pszFormat, va_list args)
{
    int rc = dbgfR3EventPrologue(pVM, enmEvent);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Format the message.
     */
    char   *pszMessage = NULL;
    char    szMessage[8192];
    if (pszFormat && *pszFormat)
    {
        pszMessage = &szMessage[0];
        RTStrPrintfV(szMessage, sizeof(szMessage), pszFormat, args);
    }

    /*
     * Send the event and process the reply communication.
     */
    pVM->dbgf.s.DbgEvent.enmType = enmEvent;
    pVM->dbgf.s.DbgEvent.enmCtx  = dbgfR3FigureEventCtx(pVM);
    pVM->dbgf.s.DbgEvent.u.Src.pszFile      = pszFile;
    pVM->dbgf.s.DbgEvent.u.Src.uLine        = uLine;
    pVM->dbgf.s.DbgEvent.u.Src.pszFunction  = pszFunction;
    pVM->dbgf.s.DbgEvent.u.Src.pszMessage   = pszMessage;
    return dbgfR3SendEvent(pVM);
}


/**
 * Send a debugger event which takes the two assertion messages.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    The event to send.
 * @param   pszMsg1     First assertion message.
 * @param   pszMsg2     Second assertion message.
 */
DBGFR3DECL(int) DBGFR3EventAssertion(PVM pVM, DBGFEVENTTYPE enmEvent, const char *pszMsg1, const char *pszMsg2)
{
    int rc = dbgfR3EventPrologue(pVM, enmEvent);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    pVM->dbgf.s.DbgEvent.enmType = enmEvent;
    pVM->dbgf.s.DbgEvent.enmCtx  = dbgfR3FigureEventCtx(pVM);
    pVM->dbgf.s.DbgEvent.u.Assert.pszMsg1 = pszMsg1;
    pVM->dbgf.s.DbgEvent.u.Assert.pszMsg2 = pszMsg2;
    return dbgfR3SendEvent(pVM);
}


/**
 * Breakpoint was hit somewhere.
 * Figure out which breakpoint it is and notify the debugger.
 *
 * @returns VBox status.
 * @param   pVM         The VM handle.
 * @param   enmEvent    DBGFEVENT_BREAKPOINT_HYPER or DBGFEVENT_BREAKPOINT.
 */
DBGFR3DECL(int) DBGFR3EventBreakpoint(PVM pVM, DBGFEVENTTYPE enmEvent)
{
    int rc = dbgfR3EventPrologue(pVM, enmEvent);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Send the event and process the reply communication.
     */
    pVM->dbgf.s.DbgEvent.enmType = enmEvent;
    RTUINT iBp = pVM->dbgf.s.DbgEvent.u.Bp.iBp = pVM->dbgf.s.iActiveBp;
    pVM->dbgf.s.iActiveBp = ~0U;
    if (iBp != ~0U)
        pVM->dbgf.s.DbgEvent.enmCtx = DBGFEVENTCTX_RAW;
    else
    {
        /* REM breakpoints has be been searched for. */
#if 0   /** @todo get flat PC api! */
        uint32_t eip = CPUMGetGuestEIP(pVM);
#else
        PCPUMCTX pCtx;
        CPUMQueryGuestCtxPtr(pVM, &pCtx);
        uint32_t eip = pCtx->eip + pCtx->csHid.u32Base;
#endif
        for (iBp = 0; iBp < ELEMENTS(pVM->dbgf.s.aBreakpoints); iBp++)
            if (    pVM->dbgf.s.aBreakpoints[iBp].enmType == DBGFBPTYPE_REM
                &&  pVM->dbgf.s.aBreakpoints[iBp].GCPtr == eip)
            {
                pVM->dbgf.s.DbgEvent.u.Bp.iBp = iBp;
                break;
            }
        AssertMsg(pVM->dbgf.s.DbgEvent.u.Bp.iBp != ~0U, ("eip=%08x\n", eip));
        pVM->dbgf.s.DbgEvent.enmCtx = DBGFEVENTCTX_REM;
    }
    return dbgfR3SendEvent(pVM);
}


/**
 * Waits for the debugger to respond.
 *
 * @returns VBox status. (clearify)
 * @param   pVM     VM handle.
 */
static int dbgfr3VMMWait(PVM pVM)
{
    LogFlow(("dbgfr3VMMWait:\n"));

    /** @todo stupid GDT/LDT sync hack. go away! */
    SELMR3UpdateFromCPUM(pVM);
    int rcRet = VINF_SUCCESS;

    /*
     * Waits for the debugger to reply (i.e. issue an command).
     */
    for (;;)
    {
        /*
         * Wait.
         */
        for (;;)
        {
            int rc = RTSemPingWait(&pVM->dbgf.s.PingPong, 250);
            if (VBOX_SUCCESS(rc))
                break;
            if (rc != VERR_TIMEOUT)
            {
                LogFlow(("dbgfr3VMMWait: returns %Vrc\n", rc));
                return rc;
            }

            if (VM_FF_ISSET(pVM, VM_FF_REQUEST))
            {
                LogFlow(("dbgfr3VMMWait: Processes requests...\n"));
                rc = VMR3ReqProcess(pVM);
                LogFlow(("dbgfr3VMMWait: VMR3ReqProcess -> %Vrc rcRet=%Vrc\n", rc, rcRet));
                if (rc >= VINF_EM_FIRST && rc <= VINF_EM_LAST)
                {
                    switch (rc)
                    {
                        case VINF_EM_DBG_BREAKPOINT:
                        case VINF_EM_DBG_STEPPED:
                        case VINF_EM_DBG_STEP:
                        case VINF_EM_DBG_STOP:
                            AssertMsgFailed(("rc=%Vrc\n", rc));
                            break;

                        /* return straight away */
                        case VINF_EM_TERMINATE:
                        case VINF_EM_OFF:
                            LogFlow(("dbgfr3VMMWait: returns %Vrc\n", rc));
                            return rc;

                        /* remember return code. */
                        default:
                            AssertReleaseMsgFailed(("rc=%Vrc is not in the switch!\n", rc));
                        case VINF_EM_RESET:
                        case VINF_EM_SUSPEND:
                        case VINF_EM_HALT:
                        case VINF_EM_RESUME:
                        case VINF_EM_RESCHEDULE:
                        case VINF_EM_RESCHEDULE_REM:
                        case VINF_EM_RESCHEDULE_RAW:
                            if (rc < rcRet || rcRet == VINF_SUCCESS)
                                rcRet = rc;
                            break;
                    }
                }
                else if (VBOX_FAILURE(rc))
                {
                    LogFlow(("dbgfr3VMMWait: returns %Vrc\n", rc));
                    return rc;
                }
            }
        }

        /*
         * Process the command.
         */
        bool            fResumeExecution;
        DBGFCMDDATA     CmdData = pVM->dbgf.s.VMMCmdData;
        DBGFCMD         enmCmd = dbgfr3SetCmd(pVM, DBGFCMD_NO_COMMAND);
        int rc = dbgfr3VMMCmd(pVM, enmCmd, &CmdData, &fResumeExecution);
        if (fResumeExecution)
        {
            if (VBOX_FAILURE(rc))
                rcRet = rc;
            else if (    rc >= VINF_EM_FIRST
                     &&  rc <= VINF_EM_LAST
                     &&  (rc < rcRet || rcRet == VINF_SUCCESS))
                rcRet = rc;
            LogFlow(("dbgfr3VMMWait: returns %Vrc\n", rcRet));
            return rcRet;
        }
    }
}


/**
 * Executes command from debugger.
 * The caller is responsible for waiting or resuming execution based on the
 * value returned in the *pfResumeExecution indicator.
 *
 * @returns VBox status. (clearify!)
 * @param   pVM                 VM Handle.
 * @param   enmCmd              The command in question.
 * @param   pCmdData            Pointer to the command data.
 * @param   pfResumeExecution   Where to store the resume execution / continue waiting indicator.
 */
static int dbgfr3VMMCmd(PVM pVM, DBGFCMD enmCmd, PDBGFCMDDATA pCmdData, bool *pfResumeExecution)
{
    bool    fSendEvent;
    bool    fResume;
    int     rc = VINF_SUCCESS;

    switch (enmCmd)
    {
        /*
         * Halt is answered by an event say that we've halted.
         */
        case DBGFCMD_HALT:
        {
            pVM->dbgf.s.DbgEvent.enmType = DBGFEVENT_HALT_DONE;
            pVM->dbgf.s.DbgEvent.enmCtx  = dbgfR3FigureEventCtx(pVM);
            fSendEvent = true;
            fResume = false;
            break;
        }


        /*
         * Resume is not answered we'll just resume execution.
         */
        case DBGFCMD_GO:
        {
            fSendEvent = false;
            fResume = true;
            break;
        }

        /** @todo implement (and define) the rest of the commands. */

        /*
         * Disable breakpoints and stuff.
         * Send an everythings cool event to the debugger thread and resume execution.
         */
        case DBGFCMD_DETACH_DEBUGGER:
        {
            pVM->dbgf.s.DbgEvent.enmType = DBGFEVENT_DETACH_DONE;
            pVM->dbgf.s.DbgEvent.enmCtx  = DBGFEVENTCTX_OTHER;
            VM_FF_CLEAR(pVM, VM_FF_DBGF);
            pVM->dbgf.s.fAttached = false;
            fSendEvent = true;
            fResume = true;
            break;
        }

        /*
         * Single step, with trace into.
         */
        case DBGFCMD_SINGLE_STEP:
        {
            Log2(("Single step\n"));
            rc = VINF_EM_DBG_STEP;
            pVM->dbgf.s.fSingleSteppingRaw = true;
            fSendEvent = false;
            fResume = true;
            break;
        }

        /*
         * Default is to send an invalid command event.
         */
        default:
        {
            pVM->dbgf.s.DbgEvent.enmType = DBGFEVENT_INVALID_COMMAND;
            pVM->dbgf.s.DbgEvent.enmCtx  = dbgfR3FigureEventCtx(pVM);
            fSendEvent = true;
            fResume = false;
            break;
        }
    }

    /*
     * Send pending event.
     */
    if (fSendEvent)
    {
        Log2(("DBGF: Emulation thread: sending event %d\n", pVM->dbgf.s.DbgEvent.enmType));
        int rc2 = RTSemPing(&pVM->dbgf.s.PingPong);
        if (VBOX_FAILURE(rc2))
        {
            AssertRC(rc2);
            *pfResumeExecution = true;
            return rc2;
        }
    }

    /*
     * Return.
     */
    *pfResumeExecution = fResume;
    return rc;
}




/**
 * Attaches a debugger to the specified VM.
 *
 * Only one debugger at a time.
 *
 * @returns VBox status code.
 * @param   pVM     VM Handle.
 */
DBGFR3DECL(int) DBGFR3Attach(PVM pVM)
{
    /*
     * Check if already attached.
     */
    if (pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Already attached\n"));
        return VERR_DBGF_ALREADY_ATTACHED;
    }

    /*
     * Create the Ping-Pong structure.
     */
    int rc = RTSemPingPongInit(&pVM->dbgf.s.PingPong);
    if (rc)
        return rc;

    /*
     * Set the attached flag.
     */
    pVM->dbgf.s.fAttached = true;
    return VINF_SUCCESS;
}


/**
 * Detaches a debugger from the specified VM.
 *
 * Caller must be attached to the VM.
 *
 * @returns VBox status code.
 * @param   pVM     VM Handle.
 */
DBGFR3DECL(int) DBGFR3Detach(PVM pVM)
{
    /*
     * Check if attached.
     */
    if (!pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Not attached to VM!\n"));
        return VERR_DBGF_NOT_ATTACHED;
    }

    /*
     * Send detach command.
     */
    if (pVM->dbgf.s.PingPong.enmSpeaker == RTPINGPONGSPEAKER_PONG)
    {
        dbgfr3SetCmd(pVM, DBGFCMD_DETACH_DEBUGGER);
        int rc = RTSemPong(&pVM->dbgf.s.PingPong);
        if (VBOX_FAILURE(rc))
        {
            AssertMsgFailed(("Failed to signal emulation thread. rc=%d\n", rc));
            return rc;
        }
    }
    else
        dbgfr3SetCmd(pVM, DBGFCMD_DETACH_DEBUGGER);

    /*
     * Wait for the ok event.
     */
    PCDBGFEVENT pEvent;
    int rc = DBGFR3EventWait(pVM, RT_INDEFINITE_WAIT, &pEvent);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Wait on detach command failed, rc=%d\n", rc));
        return rc;
    }

    /*
     * Destroy the ping-pong construct and return.
     */
    pVM->dbgf.s.fAttached = false;
    RTThreadSleep(10);
    rc = RTSemPingPongDestroy(&pVM->dbgf.s.PingPong);
    AssertRC(rc);

    return VINF_SUCCESS;
}



/**
 * Wait for a debug event.
 *
 * @returns VBox status. Will not return VBOX_INTERRUPTED.
 * @param   pVM         VM handle.
 * @param   cMillies    Number of millies to wait.
 * @param   ppEvent     Where to store the event pointer.
 */
DBGFR3DECL(int) DBGFR3EventWait(PVM pVM, unsigned cMillies, PCDBGFEVENT *ppEvent)
{
    /*
     * Check state.
     */
    if (!pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Not attached to VM!\n"));
        return VERR_DBGF_NOT_ATTACHED;
    }
    *ppEvent = NULL;

    /*
     * Wait.
     */
    int rc = RTSemPongWait(&pVM->dbgf.s.PingPong, cMillies);
    if (VBOX_SUCCESS(rc))
    {
        *ppEvent = &pVM->dbgf.s.DbgEvent;
        Log2(("DBGF: Debugger thread: receiving event %d\n", (*ppEvent)->enmType));
        return VINF_SUCCESS;
    }

    return rc;
}


/**
 * Halts VM execution.
 *
 * After calling this the VM isn't actually halted till an DBGFEVENT_HALT_DONE
 * arrives. Until that time it's not possible to issue any new commands.
 *
 * @returns VBox status.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(int) DBGFR3Halt(PVM pVM)
{
    /*
     * Check state.
     */
    if (!pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Not attached to VM!\n"));
        return VERR_DBGF_NOT_ATTACHED;
    }
    if (    pVM->dbgf.s.PingPong.enmSpeaker == RTPINGPONGSPEAKER_PONG
        ||  pVM->dbgf.s.PingPong.enmSpeaker == RTPINGPONGSPEAKER_PONG_SIGNALED)
        return VWRN_DBGF_ALREADY_HALTED;

    /*
     * Send command.
     */
    dbgfr3SetCmd(pVM, DBGFCMD_HALT);

    return VINF_SUCCESS;
}


/**
 * Checks if the VM is halted by the debugger.
 *
 * @returns True if halted.
 * @returns False if not halted.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(bool) DBGFR3IsHalted(PVM pVM)
{
    AssertMsg(pVM->dbgf.s.fAttached, ("Not attached to VM!\n"));

    RTPINGPONGSPEAKER enmSpeaker = pVM->dbgf.s.PingPong.enmSpeaker;
    return pVM->dbgf.s.fAttached
        && (    enmSpeaker == RTPINGPONGSPEAKER_PONG_SIGNALED
            ||  enmSpeaker == RTPINGPONGSPEAKER_PONG);
}


/**
 * Checks if the the debugger can wait for events or not.
 *
 * This function is only used by lazy, multiplexing debuggers. :-)
 *
 * @returns True if waitable.
 * @returns False if not waitable.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(bool) DBGFR3CanWait(PVM pVM)
{
    AssertMsg(pVM->dbgf.s.fAttached, ("Not attached to VM!\n"));

    RTPINGPONGSPEAKER enmSpeaker = pVM->dbgf.s.PingPong.enmSpeaker;
    return pVM->dbgf.s.fAttached
        && (    enmSpeaker == RTPINGPONGSPEAKER_PING
            ||  enmSpeaker == RTPINGPONGSPEAKER_PING_SIGNALED
            ||  enmSpeaker == RTPINGPONGSPEAKER_PONG_SIGNALED);
}


/**
 * Resumes VM execution.
 *
 * There is no receipt event on this command.
 *
 * @returns VBox status.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(int) DBGFR3Resume(PVM pVM)
{
    /*
     * Check state.
     */
    if (!pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Not attached to VM!\n"));
        return VERR_DBGF_NOT_ATTACHED;
    }
    if (pVM->dbgf.s.PingPong.enmSpeaker != RTPINGPONGSPEAKER_PONG)
    {
        AssertMsgFailed(("Speaking out of turn!\n"));
        return VERR_SEM_OUT_OF_TURN;
    }

    /*
     * Send the ping back to the emulation thread telling it to run.
     */
    dbgfr3SetCmd(pVM, DBGFCMD_GO);
    int rc = RTSemPong(&pVM->dbgf.s.PingPong);
    AssertRC(rc);

    return rc;
}


/**
 * Step Into.
 *
 * A single step event is generated from this command.
 * The current implementation is not reliable, so don't rely on the event comming.
 *
 * @returns VBox status.
 * @param   pVM     VM handle.
 */
DBGFR3DECL(int) DBGFR3Step(PVM pVM)
{
    /*
     * Check state.
     */
    if (!pVM->dbgf.s.fAttached)
    {
        AssertMsgFailed(("Not attached to VM!\n"));
        return VERR_DBGF_NOT_ATTACHED;
    }
    if (pVM->dbgf.s.PingPong.enmSpeaker != RTPINGPONGSPEAKER_PONG)
    {
        AssertMsgFailed(("Speaking out of turn!\n"));
        return VERR_SEM_OUT_OF_TURN;
    }

    /*
     * Send the ping back to the emulation thread telling it to run.
     */
    dbgfr3SetCmd(pVM, DBGFCMD_SINGLE_STEP);
    int rc = RTSemPong(&pVM->dbgf.s.PingPong);
    AssertRC(rc);
    return rc;
}


/**
 * Call this to single step rawmode or recompiled mode.
 *
 * You must pass down the return code to the EM loop! That's
 * where the actual single stepping take place (at least in the
 * current implementation).
 *
 * @returns VINF_EM_DBG_STEP
 * @thread  EMT
 */
DBGFR3DECL(int) DBGFR3PrgStep(PVM pVM)
{
    VM_ASSERT_EMT(pVM);

    pVM->dbgf.s.fSingleSteppingRaw = true;
    return VINF_EM_DBG_STEP;
}

