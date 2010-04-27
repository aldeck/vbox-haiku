/** @file
 * VBox Remote Desktop Protocol - External Authentication Library Interface.
 * (VRDP)
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
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
 */

#ifndef ___VBox_vrdpauth_h
#define ___VBox_vrdpauth_h

/* The following 2 enums are 32 bits values.*/
typedef enum _VRDPAuthResult
{
    VRDPAuthAccessDenied    = 0,
    VRDPAuthAccessGranted   = 1,
    VRDPAuthDelegateToGuest = 2,
    VRDPAuthSizeHack        = 0x7fffffff
} VRDPAuthResult;

typedef enum _VRDPAuthGuestJudgement
{
    VRDPAuthGuestNotAsked      = 0,
    VRDPAuthGuestAccessDenied  = 1,
    VRDPAuthGuestNoJudgement   = 2,
    VRDPAuthGuestAccessGranted = 3,
    VRDPAuthGuestNotReacted    = 4,
    VRDPAuthGuestSizeHack      = 0x7fffffff
} VRDPAuthGuestJudgement;

/* UUID memory representation. Array of 16 bytes. */
typedef unsigned char VRDPAUTHUUID[16];
typedef VRDPAUTHUUID *PVRDPAUTHUUID;
/*
Note: VirtualBox uses a consistent binary representation of UUIDs on all platforms. For this reason
the integer fields comprising the UUID are stored as little endian values. If you want to pass such
UUIDs to code which assumes that the integer fields are big endian (often also called network byte
order), you need to adjust the contents of the UUID to e.g. achieve the same string representation.
The required changes are:
 * reverse the order of byte 0, 1, 2 and 3
 * reverse the order of byte 4 and 5
 * reverse the order of byte 6 and 7.
Using this conversion you will get identical results when converting the binary UUID to the string
representation.
*/

/* The library entry point calling convention. */
#ifdef _MSC_VER
# define VRDPAUTHCALL __cdecl
#elif defined(__GNUC__)
# define VRDPAUTHCALL
#else
# error "Unsupported compiler"
#endif


/**
 * Authentication library entry point. Decides whether to allow
 * a client connection.
 *
 * Parameters:
 *
 *   pUuid            Pointer to the UUID of the virtual machine
 *                    which the client connected to.
 *   guestJudgement   Result of the guest authentication.
 *   szUser           User name passed in by the client (UTF8).
 *   szPassword       Password passed in by the client (UTF8).
 *   szDomain         Domain passed in by the client (UTF8).
 *
 * Return code:
 *
 *   VRDPAuthAccessDenied    Client access has been denied.
 *   VRDPAuthAccessGranted   Client has the right to use the
 *                           virtual machine.
 *   VRDPAuthDelegateToGuest Guest operating system must
 *                           authenticate the client and the
 *                           library must be called again with
 *                           the result of the guest
 *                           authentication.
 */
typedef VRDPAuthResult VRDPAUTHCALL VRDPAUTHENTRY(PVRDPAUTHUUID pUuid,
                                                  VRDPAuthGuestJudgement guestJudgement,
                                                  const char *szUser,
                                                  const char *szPassword,
                                                  const char *szDomain);


typedef VRDPAUTHENTRY *PVRDPAUTHENTRY;

/**
 * Authentication library entry point version 2. Decides whether to allow
 * a client connection.
 *
 * Parameters:
 *
 *   pUuid            Pointer to the UUID of the virtual machine
 *                    which the client connected to.
 *   guestJudgement   Result of the guest authentication.
 *   szUser           User name passed in by the client (UTF8).
 *   szPassword       Password passed in by the client (UTF8).
 *   szDomain         Domain passed in by the client (UTF8).
 *   fLogon           Boolean flag. Indicates whether the entry point is called
 *                    for a client logon or the client disconnect.
 *   clientId         Server side unique identifier of the client.
 *
 * Return code:
 *
 *   VRDPAuthAccessDenied    Client access has been denied.
 *   VRDPAuthAccessGranted   Client has the right to use the
 *                           virtual machine.
 *   VRDPAuthDelegateToGuest Guest operating system must
 *                           authenticate the client and the
 *                           library must be called again with
 *                           the result of the guest
 *                           authentication.
 *
 * Note: When 'fLogon' is 0, only pUuid and clientId are valid and the return
 *       code is ignored.
 */
typedef VRDPAuthResult VRDPAUTHCALL VRDPAUTHENTRY2(PVRDPAUTHUUID pUuid,
                                                   VRDPAuthGuestJudgement guestJudgement,
                                                   const char *szUser,
                                                   const char *szPassword,
                                                   const char *szDomain,
                                                   int fLogon,
                                                   unsigned clientId);


typedef VRDPAUTHENTRY2 *PVRDPAUTHENTRY2;

#endif
