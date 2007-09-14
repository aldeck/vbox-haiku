/* $Id$ */
/** @file
 * VirtualBox Support Library - Internal header.
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

#ifndef __SupInternal_h__
#define __SupInternal_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>



/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The negotiated interrupt number. */
extern uint8_t          g_uchInterruptNo;
/** The negotiated cookie. */
extern uint32_t         g_u32Cookie;
/** The negotiated cookie. */
extern uint32_t         g_u32CookieSession;



/*******************************************************************************
*   OS Specific Function                                                       *
*******************************************************************************/
__BEGIN_DECLS
int     suplibOsInstall(void);
int     suplibOsUninstall(void);
int     suplibOsInit(size_t cbReserve);
int     suplibOsTerm(void);
int     suplibOsIOCtl(uintptr_t uFunction, void *pvReq, size_t cbReq);
int     suplibOSIOCtlFast(uintptr_t uFunction);
int     suplibOsPageAlloc(size_t cPages, void **ppvPages);
int     suplibOsPageFree(void *pvPages, size_t cPages);
__END_DECLS


#endif

