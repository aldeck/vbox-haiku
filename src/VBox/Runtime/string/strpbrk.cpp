/* $Id$ */
/** @file
 * InnoTek Portable Runtime - strpbrk().
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/string.h>


/**
 * Find the first occurrence of a character in pszChars in pszStr.
 *
 * @returns
 */
#ifdef _MSC_VER
# if _MSC_VER >= 1400
_CRTIMP __checkReturn _CONST_RETURN char *  __cdecl strpbrk(__in_z const char *pszStr, __in_z const char *pszChars)
# else
_CRTIMP char * __cdecl strpbrk(const char *pszStr, const char *pszChars)
# endif
#else
char *strpbrk(const char *pszStr, const char *pszChars)
# if defined(__THROW) && !defined(__WIN__) && !defined(__OS2__)
    __THROW
# endif
#endif
{
    int chCur;
    while ((chCur = *pszStr++) != '\0')
    {
        int ch;
        const char *psz = pszChars;
        while ((ch = *psz++) != '\0')
            if (ch == chCur)
                return (char *)(pszStr - 1);

    }
    return NULL;
}

