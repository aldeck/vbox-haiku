/* $Id$ */
/** @file
 * Shared Clipboard: Some helper function for converting between the various eol.
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>//
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <VBox/GuestHost/clipboard-helper.h>

/** @todo use const where appropriate; delinuxify the code (*Lin* -> *Host*); use AssertLogRel*. */

int vboxClipboardUtf16GetWinSize(PRTUTF16 pwszSrc, size_t cwSrc, size_t *pcwDest)
{
    size_t cwDest, i;

    LogFlowFunc(("pwszSrc=%.*ls, cwSrc=%u\n", cwSrc, pwszSrc, cwSrc));
    AssertLogRelMsgReturn(pwszSrc != NULL, ("vboxClipboardUtf16GetWinSize: received a null Utf16 string, returning VERR_INVALID_PARAMETER\n"), VERR_INVALID_PARAMETER);
    if (cwSrc == 0)
    {
        *pcwDest = 0;
        LogFlowFunc(("empty source string, returning\n"));
        return VINF_SUCCESS;
    }
/** @todo convert the remainder of the Assert stuff to AssertLogRel. */
    /* We only take little endian Utf16 */
    if (pwszSrc[0] == UTF16BEMARKER)
    {
        LogRel(("vboxClipboardUtf16GetWinSize: received a big endian Utf16 string, returning VERR_INVALID_PARAMETER\n"));
        AssertReturn(pwszSrc[0] != UTF16BEMARKER, VERR_INVALID_PARAMETER);
    }
    cwDest = 0;
    /* Calculate the size of the destination text string. */
    /* Is this Utf16 or Utf16-LE? */
    for (i = (pwszSrc[0] == UTF16LEMARKER ? 1 : 0); i < cwSrc; ++i, ++cwDest)
    {
        /* Check for a single line feed */
        if (pwszSrc[i] == LINEFEED)
            ++cwDest;
#ifdef RT_OS_DARWIN
        /* Check for a single carriage return (MacOS) */
        if (pwszSrc[i] == CARRIAGERETURN)
            ++cwDest;
#endif
        if (pwszSrc[i] == 0)
        {
            /* Don't count this, as we do so below. */
            break;
        }
    }
    /* Count the terminating null byte. */
    ++cwDest;
    LogFlowFunc(("returning VINF_SUCCESS, %d 16bit words\n", cwDest));
    *pcwDest = cwDest;
    return VINF_SUCCESS;
}

int vboxClipboardUtf16LinToWin(PRTUTF16 pwszSrc, size_t cwSrc, PRTUTF16 pu16Dest,
                               size_t cwDest)
{
    size_t i, j;
    LogFlowFunc(("pwszSrc=%.*ls, cwSrc=%u\n", cwSrc, pwszSrc, cwSrc));
    if (!VALID_PTR(pwszSrc) || !VALID_PTR(pu16Dest))
    {
        LogRel(("vboxClipboardUtf16LinToWin: received an invalid pointer, pwszSrc=%p, pu16Dest=%p, returning VERR_INVALID_PARAMETER\n", pwszSrc, pu16Dest));
        AssertReturn(VALID_PTR(pwszSrc) && VALID_PTR(pu16Dest), VERR_INVALID_PARAMETER);
    }
    if (cwSrc == 0)
    {
        if (cwDest == 0)
        {
            LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
            return VERR_BUFFER_OVERFLOW;
        }
        pu16Dest[0] = 0;
        LogFlowFunc(("empty source string, returning\n"));
        return VINF_SUCCESS;
    }
    /* We only take little endian Utf16 */
    if (pwszSrc[0] == UTF16BEMARKER)
    {
        LogRel(("vboxClipboardUtf16LinToWin: received a big endian Utf16 string, returning VERR_INVALID_PARAMETER\n"));
        AssertReturn(pwszSrc[0] != UTF16BEMARKER, VERR_INVALID_PARAMETER);
    }
    /* Don't copy the endian marker. */
    for (i = (pwszSrc[0] == UTF16LEMARKER ? 1 : 0), j = 0; i < cwSrc; ++i, ++j)
    {
        /* Don't copy the null byte, as we add it below. */
        if (pwszSrc[i] == 0)
            break;
        if (j == cwDest)
        {
            LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
            return VERR_BUFFER_OVERFLOW;
        }
        if (pwszSrc[i] == LINEFEED)
        {
            pu16Dest[j] = CARRIAGERETURN;
            ++j;
            if (j == cwDest)
            {
                LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
                return VERR_BUFFER_OVERFLOW;
            }
        }
#ifdef RT_OS_DARWIN
        /* Check for a single carriage return (MacOS) */
        else if (pwszSrc[i] == CARRIAGERETURN)
        {
            /* set cr */
            pu16Dest[j] = CARRIAGERETURN;
            ++j;
            if (j == cwDest)
            {
                LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
                return VERR_BUFFER_OVERFLOW;
            }
            /* add the lf */
            pu16Dest[j] = LINEFEED;
            continue;
        }
#endif
        pu16Dest[j] = pwszSrc[i];
    }
    /* Add the trailing null. */
    if (j == cwDest)
    {
        LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
        return VERR_BUFFER_OVERFLOW;
    }
    pu16Dest[j] = 0;
    LogFlowFunc(("rc=VINF_SUCCESS, pu16Dest=%ls\n", pu16Dest));
    return VINF_SUCCESS;
}

int vboxClipboardUtf16GetLinSize(PRTUTF16 pwszSrc, size_t cwSrc, size_t *pcwDest)
{
    size_t cwDest;

    LogFlowFunc(("pwszSrc=%.*ls, cwSrc=%u\n", cwSrc, pwszSrc, cwSrc));
    if (!VALID_PTR(pwszSrc))
    {
        LogRel(("vboxClipboardUtf16GetLinSize: received an invalid Utf16 string %p.  Returning VERR_INVALID_PARAMETER.\n", pwszSrc));
        AssertReturn(VALID_PTR(pwszSrc), VERR_INVALID_PARAMETER);
    }
    if (cwSrc == 0)
    {
        LogFlowFunc(("empty source string, returning VINF_SUCCESS\n"));
        *pcwDest = 0;
        return VINF_SUCCESS;
    }
    /* We only take little endian Utf16 */
    if (pwszSrc[0] == UTF16BEMARKER)
    {
        LogRel(("vboxClipboardUtf16GetLinSize: received a big endian Utf16 string.  Returning VERR_INVALID_PARAMETER.\n"));
        AssertReturn(pwszSrc[0] != UTF16BEMARKER, VERR_INVALID_PARAMETER);
    }
    /* Calculate the size of the destination text string. */
    /* Is this Utf16 or Utf16-LE? */
    if (pwszSrc[0] == UTF16LEMARKER)
        cwDest = 0;
    else
        cwDest = 1;
    for (size_t i = 0; i < cwSrc; ++i, ++cwDest)
    {
        if (   (i + 1 < cwSrc)
            && (pwszSrc[i] == CARRIAGERETURN)
            && (pwszSrc[i + 1] == LINEFEED))
        {
            ++i;
        }
        if (pwszSrc[i] == 0)
        {
            break;
        }
    }
    /* Terminating zero */
    ++cwDest;
    LogFlowFunc(("returning %d\n", cwDest));
    *pcwDest = cwDest;
    return VINF_SUCCESS;
}

int vboxClipboardUtf16WinToLin(PRTUTF16 pwszSrc, size_t cwSrc, PRTUTF16 pu16Dest,
                               size_t cwDest)
{
    size_t cwDestPos;

    LogFlowFunc(("pwszSrc=%.*ls, cwSrc=%u, pu16Dest=%p, cwDest=%u\n",
                 cwSrc, pwszSrc, cwSrc, pu16Dest, cwDest));
    /* A buffer of size 0 may not be an error, but it is not a good idea either. */
    Assert(cwDest > 0);
    if (!VALID_PTR(pwszSrc) || !VALID_PTR(pu16Dest))
    {
        LogRel(("vboxClipboardUtf16WinToLin: received an invalid ptr, pwszSrc=%p, pu16Dest=%p, returning VERR_INVALID_PARAMETER\n", pwszSrc, pu16Dest));
        AssertReturn(VALID_PTR(pwszSrc) && VALID_PTR(pu16Dest), VERR_INVALID_PARAMETER);
    }
    /* We only take little endian Utf16 */
    if (pwszSrc[0] == UTF16BEMARKER)
    {
        LogRel(("vboxClipboardUtf16WinToLin: received a big endian Utf16 string, returning VERR_INVALID_PARAMETER\n"));
        AssertMsgFailedReturn(("received a big endian string\n"), VERR_INVALID_PARAMETER);
    }
    if (cwDest == 0)
    {
        LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
        return VERR_BUFFER_OVERFLOW;
    }
    if (cwSrc == 0)
    {
        pu16Dest[0] = 0;
        LogFlowFunc(("received empty string.  Returning VINF_SUCCESS\n"));
        return VINF_SUCCESS;
    }
    /* Prepend the Utf16 byte order marker if it is missing. */
    if (pwszSrc[0] == UTF16LEMARKER)
    {
        cwDestPos = 0;
    }
    else
    {
        pu16Dest[0] = UTF16LEMARKER;
        cwDestPos = 1;
    }
    for (size_t i = 0; i < cwSrc; ++i, ++cwDestPos)
    {
        if (pwszSrc[i] == 0)
        {
            break;
        }
        if (cwDestPos == cwDest)
        {
            LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
            return VERR_BUFFER_OVERFLOW;
        }
        if (   (i + 1 < cwSrc)
            && (pwszSrc[i] == CARRIAGERETURN)
            && (pwszSrc[i + 1] == LINEFEED))
        {
            ++i;
        }
        pu16Dest[cwDestPos] = pwszSrc[i];
    }
    /* Terminating zero */
    if (cwDestPos == cwDest)
    {
        LogFlowFunc(("returning VERR_BUFFER_OVERFLOW\n"));
        return VERR_BUFFER_OVERFLOW;
    }
    pu16Dest[cwDestPos] = 0;
    LogFlowFunc(("set string %ls.  Returning\n", pu16Dest + 1));
    return VINF_SUCCESS;
}

int vboxClipboardDibToBmp(const void *pSrc, size_t cbSrc, void **ppDest, size_t *pcbDest)
{
    size_t cb = sizeof(BITMAPFILEHEADER) + cbSrc;
	PBITMAPFILEHEADER pFileHeader;
	void *dest;
	size_t pixelOffset;

	PBITMAPINFOHEADER pBitmapInfoHeader = (PBITMAPINFOHEADER)pSrc;
	if (cbSrc < sizeof(BITMAPINFOHEADER) ||
		pBitmapInfoHeader->u32Size < sizeof(BITMAPINFOHEADER) ||
	/** @todo Support all the many versions of the DIB headers. */
		pBitmapInfoHeader->u32Size != sizeof(BITMAPINFOHEADER))
	{
fprintf(stderr, "vboxClipboardDibToBmp: invalid or unsupported bitmap data.\n");
        Log (("vboxClipboardDibToBmp: invalid or unsupported bitmap data.\n"));
	    return VERR_INVALID_PARAMETER;
	}
fprintf(stderr, "DIB: u32Size = %d\n", pBitmapInfoHeader->u32Size);
fprintf(stderr, "DIB: u32Width = %d\n", pBitmapInfoHeader->u32Width);
fprintf(stderr, "DIB: u32Height = %d\n", pBitmapInfoHeader->u32Height);
fprintf(stderr, "DIB: u16Planes = %d\n", pBitmapInfoHeader->u16Planes);
fprintf(stderr, "DIB: u16BitCount = %d\n", pBitmapInfoHeader->u16BitCount);
fprintf(stderr, "DIB: u32Compression = %d\n", pBitmapInfoHeader->u32Compression);
fprintf(stderr, "DIB: u32SizeImage = %d\n", pBitmapInfoHeader->u32SizeImage);
fprintf(stderr, "DIB: u32XBitsPerMeter = %d\n", pBitmapInfoHeader->u32XBitsPerMeter);
fprintf(stderr, "DIB: u32YBitsPerMeter = %d\n", pBitmapInfoHeader->u32YBitsPerMeter);
fprintf(stderr, "DIB: u32ClrUsed = %d\n", pBitmapInfoHeader->u32ClrUsed);
fprintf(stderr, "DIB: u32ClrImportant = %d\n", pBitmapInfoHeader->u32ClrImportant);
	pixelOffset = sizeof(BITMAPFILEHEADER) + pBitmapInfoHeader->u32Size + pBitmapInfoHeader->u32ClrUsed * sizeof(uint32_t);
	if (cbSrc < pixelOffset)
	{
fprintf(stderr, "vboxClipboardDibToBmp: invalid bitmap data.\n");
        Log (("vboxClipboardDibToBmp: invalid bitmap data.\n"));
	    return VERR_INVALID_PARAMETER;
	}

	dest = RTMemAlloc(cb);
	if (dest == NULL)
	{
fprintf(stderr, "writeToPasteboard: cannot allocate memory for bitmap.\n");
        Log (("writeToPasteboard: cannot allocate memory for bitmap.\n"));
	    return VERR_NO_MEMORY;
	}

	pFileHeader = (PBITMAPFILEHEADER)dest;
	pFileHeader->u16Type = BITMAPHEADERMAGIC;
	pFileHeader->u32Size = RT_H2LE_U32(cb);
	pFileHeader->u16Reserved1 = pFileHeader->u16Reserved2 = 0;
	pFileHeader->u32OffBits = pixelOffset;
	memcpy((uint8_t *)dest + sizeof(BITMAPFILEHEADER), pSrc, cbSrc);
	*ppDest = dest;
	*pcbDest = cb;
	return VINF_SUCCESS;
}

int vboxClipboardBmpGetDib(const void *pSrc, size_t cbSrc, const void **pDest, size_t *pcbDest)
{
	PBITMAPFILEHEADER pFileHeader = (PBITMAPFILEHEADER)pSrc;

fprintf(stderr, "vboxClipboardBmpGetDib %04x %d %d\n", pFileHeader->u16Type, cbSrc, pFileHeader->u32Size);
	if (pFileHeader == NULL || cbSrc < sizeof(BITMAPFILEHEADER) ||
	    pFileHeader->u16Type != BITMAPHEADERMAGIC ||
		pFileHeader->u32Size != cbSrc)
	{
        Log (("vboxClipboardBmpGetDib: invalid bitmap data.\n"));
	    return VERR_INVALID_PARAMETER;
	}
	*pDest = ((uint8_t *)pSrc) + sizeof(BITMAPFILEHEADER);
    *pcbDest = cbSrc - sizeof(BITMAPFILEHEADER);
	return VINF_SUCCESS;
}

