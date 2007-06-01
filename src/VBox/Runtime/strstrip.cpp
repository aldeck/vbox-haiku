/* $Id$ */
/** @file
 * innotek Portable Runtime - String Stripping and Trimming.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>
#include <iprt/ctype.h>
#include <iprt/string.h>


/**
 * Strips blankspaces from both ends of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStrip(char *psz)
{
    /* left */
    while (isspace(*psz))
        psz++;

    /* right */
    char *pszEnd = strchr(psz, '\0');
    while (--pszEnd > psz && isspace(*pszEnd))
        *pszEnd = '\0';

    return psz;
}


/**
 * Strips blankspaces from the start of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStripL(const char *psz)
{
    /* left */
    while (isspace(*psz))
        psz++;

    return (char *)psz;
}


/**
 * Strips blankspaces from the end of the string.
 *
 * @returns psz.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStripR(char *psz)
{
    /* right */
    char *pszEnd = strchr(psz, '\0');
    while (--pszEnd > psz && isspace(*pszEnd))
        *pszEnd = '\0';

    return psz;
}

