/** @file
 *
 * VBoxGuest -- VirtualBox Win 2000/XP guest display driver
 *
 * VBox support functions.
 *
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
 *
 */

#include "driver.h"

#include <VBox/VBoxGuest.h>
#include <VBox/err.h>
#include <iprt/asm.h>

/*
 * There is a hardware ring buffer in the VBox VMMDev PCI memory space.
 * All graphics commands go there serialized by vboxHwBufferBeginUpdate.
 * and vboxHwBufferEndUpdate.
 *
 * off32Free is writing position. off32Data is reading position.
 * off32Free == off32Data means buffer is empty.
 * There must be always gap between off32Data and off32Free when data
 * are in the buffer.
 * Guest only changes off32Free, host changes off32Data.
 */

/* Forward declarations of internal functions. */
static void vboxHwBufferFlush (PPDEV ppdev);
static void vboxHwBufferPlaceDataAt (PPDEV ppdev, void *p, uint32_t cb, uint32_t offset);
static BOOL vboxHwBufferWrite (PPDEV ppdev, const void *p, uint32_t cb);

/*
 * Public hardware buffer methods.
 */
BOOL vboxVbvaEnable (PPDEV ppdev)
{
    BOOL bRc = FALSE;

    ULONG returnedDataLength;
    ULONG ulEnable = TRUE;

    DISPDBG((1, "VBoxDisp::vboxVbvaEnable called\n"));
    
    if (!ghsemHwBuffer)
    {
        return FALSE;
    }

    if (EngDeviceIoControl(ppdev->hDriver,
                           IOCTL_VIDEO_VBVA_ENABLE,
                           &ulEnable,
                           sizeof (ulEnable),
                           &ppdev->vbva,
                           sizeof (ppdev->vbva),
                           &returnedDataLength) == 0)
    {
        DISPDBG((1, "VBoxDisp::vboxVbvaEnable: vbva: pVbvaMemory = %p, pfnFlush = %p, pvFlush = %p.\n",
                     ppdev->vbva.pVbvaMemory, ppdev->vbva.pfnFlush, ppdev->vbva.pvFlush));

        if (ppdev->vbva.pVbvaMemory
            && ppdev->vbva.pfnFlush
            && ppdev->vbva.pvFlush)
        {
            ppdev->fHwBufferOverflow = FALSE;
            ppdev->pRecord           = NULL;

            /* All have been initialized. */
            bRc = TRUE;
        }
    }

    if (!bRc)
    {
        vboxVbvaDisable (ppdev);
    }

    return bRc;
}

void vboxVbvaDisable (PPDEV ppdev)
{
    DISPDBG((1, "VBoxDisp::vbvaDisable called.\n"));

    RtlZeroMemory (&ppdev->vbva, sizeof (ppdev->vbva));

    ppdev->fHwBufferOverflow = FALSE;
    ppdev->pRecord           = NULL;

    return;
}

BOOL vboxHwBufferBeginUpdate (PPDEV ppdev)
{
    BOOL bRc = FALSE;

    VBVAMEMORY *pVbvaMemory = ppdev->vbva.pVbvaMemory;

    DISPDBG((1, "VBoxDisp::vboxHwBufferBeginUpdate called flags = 0x%08X\n", pVbvaMemory? pVbvaMemory->fu32ModeFlags: -1));

    if (   pVbvaMemory
        && (pVbvaMemory->fu32ModeFlags & VBVA_F_MODE_ENABLED))
    {
        uint32_t indexRecordNext;

        EngAcquireSemaphore (ghsemHwBuffer);

        VBVA_ASSERT (!ppdev->fHwBufferOverflow);
        VBVA_ASSERT (ppdev->pRecord == NULL);

        indexRecordNext = (pVbvaMemory->indexRecordFree + 1) % VBVA_MAX_RECORDS;

        if (indexRecordNext == pVbvaMemory->indexRecordFirst)
        {
            /* All slots in the records queue are used. */
            vboxHwBufferFlush (ppdev);
        }

        if (indexRecordNext == pVbvaMemory->indexRecordFirst)
        {
            /* Even after flush there is no place. Fail the request. */
            DISPDBG((1, "VBoxDisp::vboxHwBufferBeginUpdate no space in the queue of records!!! first %d, last %d\n",
                     pVbvaMemory->indexRecordFirst, pVbvaMemory->indexRecordFree));
            EngReleaseSemaphore (ghsemHwBuffer);
        }
        else
        {
            /* Initialize the record. */
            VBVARECORD *pRecord = &pVbvaMemory->aRecords[pVbvaMemory->indexRecordFree];

            pRecord->cbRecord = VBVA_F_RECORD_PARTIAL;

            pVbvaMemory->indexRecordFree = indexRecordNext;

            DISPDBG((1, "VBoxDisp::vboxHwBufferBeginUpdate indexRecordNext = %d\n", indexRecordNext));

            /* Remember which record we are using. */
            ppdev->pRecord = pRecord;

            bRc = TRUE;
        }
    }

    return bRc;
}

void vboxHwBufferEndUpdate (PPDEV ppdev)
{
    VBVAMEMORY *pVbvaMemory;
    VBVARECORD *pRecord;

    DISPDBG((1, "VBoxDisp::vboxHwBufferEndUpdate called\n"));

    pVbvaMemory = ppdev->vbva.pVbvaMemory;
    VBVA_ASSERT(pVbvaMemory);

    pRecord = ppdev->pRecord;
    VBVA_ASSERT (pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    /* Mark the record completed. */
    pRecord->cbRecord &= ~VBVA_F_RECORD_PARTIAL;

    ppdev->fHwBufferOverflow = FALSE;
    ppdev->pRecord = NULL;

    EngReleaseSemaphore (ghsemHwBuffer);

    return;
}

/*
 * Private operations.
 */
static uint32_t vboxHwBufferAvail (VBVAMEMORY *pVbvaMemory)
{
    int32_t i32Diff = pVbvaMemory->off32Data - pVbvaMemory->off32Free;

    return i32Diff > 0? i32Diff: VBVA_RING_BUFFER_SIZE + i32Diff;
}

static void vboxHwBufferFlush (PPDEV ppdev)
{
    VBVAMEMORY *pVbvaMemory = ppdev->vbva.pVbvaMemory;

    VBVA_ASSERT (pVbvaMemory);

    ppdev->vbva.pfnFlush (ppdev->vbva.pvFlush);

    return;
}

static void vboxHwBufferPlaceDataAt (PPDEV ppdev, void *p, uint32_t cb, uint32_t offset)
{
    VBVAMEMORY *pVbvaMemory = ppdev->vbva.pVbvaMemory;

    uint32_t u32BytesTillBoundary = VBVA_RING_BUFFER_SIZE - offset;
    uint8_t  *dst                 = &pVbvaMemory->au8RingBuffer[offset];
    int32_t i32Diff               = cb - u32BytesTillBoundary;

    if (i32Diff <= 0)
    {
        /* Chunk will not cross buffer boundary. */
        memcpy (dst, p, cb);
    }
    else
    {
        /* Chunk crosses buffer boundary. */
        memcpy (dst, p, u32BytesTillBoundary);
        memcpy (&pVbvaMemory->au8RingBuffer[0], (uint8_t *)p + u32BytesTillBoundary, i32Diff);
    }

    return;
}

static BOOL vboxHwBufferWrite (PPDEV ppdev, const void *p, uint32_t cb)
{
    VBVAMEMORY *pVbvaMemory;
    VBVARECORD *pRecord;
    uint32_t cbHwBufferAvail;

    uint32_t cbWritten = 0;

    VBVA_ASSERT(ppdev);

    if (ppdev->fHwBufferOverflow)
    {
        return FALSE;
    }

    pVbvaMemory = ppdev->vbva.pVbvaMemory;
    VBVA_ASSERT (pVbvaMemory->indexRecordFirst != pVbvaMemory->indexRecordFree);

    pRecord = ppdev->pRecord;
    VBVA_ASSERT (pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    DISPDBG((1, "VW %d\n", cb));

    cbHwBufferAvail = vboxHwBufferAvail (pVbvaMemory);

    while (cb > 0)
    {
        uint32_t cbChunk = cb;

//        DISPDBG((1, "VBoxDisp::vboxHwBufferWrite pVbvaMemory->off32Free %d, pRecord->cbRecord 0x%08X, cbHwBufferAvail %d, cb %d, cbWritten %d\n", pVbvaMemory->off32Free, pRecord->cbRecord, cbHwBufferAvail, cb, cbWritten));

        if (cbChunk >= cbHwBufferAvail)
        {
            DISPDBG((1, "VBoxDisp::vboxHwBufferWrite 1) avail %d, chunk %d\n", cbHwBufferAvail, cbChunk));

            vboxHwBufferFlush (ppdev);

            cbHwBufferAvail = vboxHwBufferAvail (pVbvaMemory);

            if (cbChunk >= cbHwBufferAvail)
            {
                DISPDBG((1, "VBoxDisp::vboxHwBufferWrite: no place for %d bytes. Only %d bytes available after flush. Going to partial writes.\n", cb, cbHwBufferAvail));

                if (cbHwBufferAvail <= VBVA_RING_BUFFER_THRESHOLD)
                {
                    DISPDBG((1, "VBoxDisp::vboxHwBufferWrite: Buffer overflow!!!\n"));
                    ppdev->fHwBufferOverflow = TRUE;
                    VBVA_ASSERT(FALSE);
                    return FALSE;
                }

                cbChunk = cbHwBufferAvail - VBVA_RING_BUFFER_THRESHOLD;
            }
        }

        VBVA_ASSERT(cbChunk <= cb);
        VBVA_ASSERT(cbChunk <= vboxHwBufferAvail (pVbvaMemory));

        vboxHwBufferPlaceDataAt (ppdev, (uint8_t *)p + cbWritten, cbChunk, pVbvaMemory->off32Free);

        pVbvaMemory->off32Free  = (pVbvaMemory->off32Free + cbChunk) % VBVA_RING_BUFFER_SIZE;
        pRecord->cbRecord      += cbChunk;
        cbHwBufferAvail -= cbChunk;

        cb        -= cbChunk;
        cbWritten += cbChunk;
    }

    return TRUE;
}

/*
 * Public writer to hardware buffer.
 */
BOOL vboxWrite (PPDEV ppdev, const void *pv, uint32_t cb)
{
    return vboxHwBufferWrite (ppdev, pv, cb);
}

BOOL vboxOrderSupported (PPDEV ppdev, unsigned code)
{
    VBVAMEMORY *pVbvaMemory;

    pVbvaMemory = ppdev->vbva.pVbvaMemory;

    if (pVbvaMemory->fu32ModeFlags & VBVA_F_MODE_VRDP_ORDER_MASK)
    {
        /* Order masking enabled. */
        if (pVbvaMemory->fu32SupportedOrders & (1 << code))
        {
            return TRUE;
        }
    }

    return FALSE;
}

void VBoxProcessDisplayInfo(PPDEV ppdev)
{
    DWORD returnedDataLength;

    DISPDBG((1, "Process: %d,%d\n", ppdev->ptlDevOrg.x, ppdev->ptlDevOrg.y));

    EngDeviceIoControl(ppdev->hDriver,
                       IOCTL_VIDEO_INTERPRET_DISPLAY_MEMORY,
                       NULL,
                       0,
                       NULL,
                       0,
                       &returnedDataLength);
}
