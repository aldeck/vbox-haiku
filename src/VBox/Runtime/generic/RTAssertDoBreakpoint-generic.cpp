/* $Id$ */
/** @file
 * innotek Portable Runtime - Assertions, generic RTAssertDoBreakpoint.
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/assert.h>


/**
 * Overridable function that decides whether assertions executes the breakpoint or not. 
 * 
 * The generic implementation will return true.
 * 
 * @returns true if the breakpoint should be hit, false if it should be ignored.
 * @remark  The RTDECL() makes this a bit difficult to override on windows. Sorry.
 */
RTDECL(bool)    RTAssertDoBreakpoint(void)
{
    return true;
}

