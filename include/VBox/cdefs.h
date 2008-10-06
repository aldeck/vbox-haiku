/** @file
 * VirtualBox - Common C and C++ definition.
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

#ifndef ___VBox_cdefs_h
#define ___VBox_cdefs_h

#include <iprt/cdefs.h>


/** @def VBOX_WITH_STATISTICS
 * When defined all statistics will be included in the build.
 * This is enabled by default in all debug builds.
 */
#ifndef VBOX_WITH_STATISTICS
# ifdef DEBUG
#  define VBOX_WITH_STATISTICS
# endif
#endif

/** @def VBOX_STRICT
 * Alias for RT_STRICT.
 */
#ifdef RT_STRICT
# ifndef VBOX_STRICT
#  define VBOX_STRICT
# endif
#endif


/*
 * Shut up DOXYGEN warnings and guide it properly thru the code.
 */
#ifdef  __DOXYGEN__
#define VBOX_WITH_STATISTICS
#define VBOX_STRICT
#define IN_DIS
#define IN_INTNET_R0
#define IN_INTNET_R3
#define IN_REM_R3
#define IN_SUP_R0
#define IN_SUP_R3
#define IN_SUP_GC
#define IN_USBLIB
#define IN_VBOXDDU
#define IN_VMM_RC
#define IN_VMM_R0
#define IN_VMM_R3
/** @todo fixme */
#endif




/** @def VBOXCALL
 * The standard calling convention for VBOX interfaces.
 */
#define VBOXCALL   RTCALL



/** @def IN_DIS
 * Used to indicate whether we're inside the same link module as the
 * disassembler.
 */
/** @def DISDECL(type)
 * Disassembly export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_DIS)
# define DISDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define DISDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_DBG
 * Used to indicate whether we're inside the same link module as the debugger
 * console, gui, and related things (ring-3).
 */
/** @def DBGDECL(type)
 * Debugger module export or import declaration.
 * Functions declared using this exists only in R3 since the
 * debugger modules is R3 only.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_DBG_R3) || defined(IN_DBG)
# define DBGDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define DBGDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_INTNET_R3
 * Used to indicate whether we're inside the same link module as the Ring 3
 * Internal Networking Service.
 */
/** @def INTNETR3DECL(type)
 * Internal Networking Service export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_INTNET_R3
# define INTNETR3DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define INTNETR3DECL(type) DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_INTNET_R0
 * Used to indicate whether we're inside the same link module as the R0
 * Internal Network Service.
 */
/** @def INTNETR0DECL(type)
 * Internal Networking Service export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_INTNET_R0
# define INTNETR0DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define INTNETR0DECL(type) DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_REM_R3
 * Used to indicate whether we're inside the same link module as
 * the HC Ring-3 Recompiled Execution Manager.
 */
/** @def REMR3DECL(type)
 * Recompiled Execution Manager HC Ring-3 export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_REM_R3
# define REMR3DECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define REMR3DECL(type)    DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_SUP_R3
 * Used to indicate whether we're inside the same link module as the Ring 3 Support Library or not.
 */
/** @def SUPR3DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_R3
# define SUPR3DECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define SUPR3DECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_SUP_R0
 * Used to indicate whether we're inside the same link module as the Ring 0 Support Library or not.
 */
/** @def SUPR0DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_R0
# ifdef IN_SUP_STATIC
#  define SUPR0DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR0DECL(type)   DECLEXPORT(type) VBOXCALL
# endif
#else
# ifdef IN_SUP_STATIC
#  define SUPR0DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR0DECL(type)   DECLIMPORT(type) VBOXCALL
# endif
#endif

/** @def IN_SUP_GC
 * Used to indicate whether we're inside the same link module as the GC Support Library or not.
 */
/** @def SUPGCDECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_GC
# define SUPGCDECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define SUPGCDECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_SUP_R0
 * Used to indicate whether we're inside the same link module as the Ring 0 Support Library or not.
 */
/** @def SUPR0DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_SUP_R0) || defined(IN_SUP_R3) || defined(IN_SUP_GC)
# define SUPDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define SUPDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_USBLIB
 * Used to indicate whether we're inside the same link module as the USBLib.
 */
/** @def USBLIB_DECL
 * USBLIB export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_RING0
# define USBLIB_DECL(type)   type VBOXCALL
#elif defined(IN_USBLIB)
# define USBLIB_DECL(type)   DECLEXPORT(type) VBOXCALL
#else
# define USBLIB_DECL(type)   DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_VMM_R3
 * Used to indicate whether we're inside the same link module as the ring 3 part of the
 * virtual machine monitor or not.
 */
/** @def VMMR3DECL
 * Ring 3 VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R3
# define VMMR3DECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define VMMR3DECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_VMM_R0
 * Used to indicate whether we're inside the same link module as the ring 0 part of the
 * virtual machine monitor or not.
 */
/** @def VMMR0DECL
 * Ring 0 VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R0
# define VMMR0DECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define VMMR0DECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_VMM_RC
 * Used to indicate whether we're inside the same link module as the raw-mode
 * context part of the virtual machine monitor or not.
 */
/** @def VMMRCDECL
 * Guest context VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_RC
# define VMMRCDECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define VMMRCDECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def VMMDECL
 * VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_VMM_R3) || defined(IN_VMM_R0) || defined(IN_VMM_RC)
# define VMMDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define VMMDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_VBOXDDU
 * Used to indicate whether we're inside the VBoxDDU shared object.
 */
/** @def VBOXDDU_DECL(type)
 * VBoxDDU export or import (ring-3).
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VBOXDDU
# define VBOXDDU_DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define VBOXDDU_DECL(type) DECLIMPORT(type) VBOXCALL
#endif



/** @def NOT_DMIK(expr)
 * Turns the given expression into NOOP when DEBUG_dmik is defined. Evaluates
 * the expression normally otherwise.
 * @param expr  Expression to guard.
 */
#if defined(DEBUG_dmik)
# define NOT_DMIK(expr)     do { } while (0)
#else
# define NOT_DMIK(expr)     expr
#endif


#endif

