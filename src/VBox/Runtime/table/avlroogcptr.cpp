/* $Id$ */
/** @file
 * innotek Portable Runtime - AVL tree, RTGCPTR, range, unique keys, overlapping ranges, offset pointers.
 */

/*
 * Copyright (C) 2001-2004 knut st. osmundsen (bird-src-spam@anduin.net)
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

#ifndef NOFILEID
static const char szFileId[] = "Id: kAVLULInt.c,v 1.4 2003/02/13 02:02:38 bird Exp $";
#endif

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/*
 * AVL configuration.
 */
#define KAVL_FN(a)                  RTAvlrooGCPtr##a
#define KAVL_MAX_STACK              27  /* Up to 2^24 nodes. */
#define KAVL_EQUAL_ALLOWED          1   /* Duplicate keys allowed */
#define KAVLNODECORE                AVLROOGCPTRNODECORE
#define PKAVLNODECORE               PAVLROOGCPTRNODECORE
#define PPKAVLNODECORE              PPAVLROOGCPTRNODECORE
#define KAVLKEY                     RTGCPTR
#define PKAVLKEY                    PRTGCPTR
#define KAVLENUMDATA                AVLROOGCPTRENUMDATA
#define PKAVLENUMDATA               PAVLROOGCPTRENUMDATA
#define PKAVLCALLBACK               PAVLROOGCPTRCALLBACK
#define KAVL_OFFSET                 1


/*
 * AVL Compare macros
 */
#define KAVL_G( key1, key2)         ( (RTGCUINTPTR)(key1) >  (RTGCUINTPTR)(key2) )
#define KAVL_E( key1, key2)         ( (RTGCUINTPTR)(key1) == (RTGCUINTPTR)(key2) )
#define KAVL_NE(key1, key2)         ( (RTGCUINTPTR)(key1) != (RTGCUINTPTR)(key2) )



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/avl.h>
#include <iprt/assert.h>

/*
 * Include the code.
 */
#define SSToDS(ptr) ptr
#define KMAX        RT_MAX
#define kASSERT     Assert
#include "avl_Base.cpp.h"
#include "avl_Get.cpp.h"
#include "avl_DoWithAll.cpp.h"
#include "avl_Destroy.cpp.h"
#include "avl_GetBestFit.cpp.h"
#include "avl_Enum.cpp.h"

/*
 * Defined only for the range functions as we allow for overlapping ranges.
 */
#undef KAVL_R_IS_IDENTICAL
#undef KAVL_R_IS_INTERSECTING
#undef KAVL_R_IS_IN_RANGE
#define KAVL_RANGE                                          1
#define KAVL_R_IS_IDENTICAL(   key1B, key2B, key1E, key2E)  ( (RTGCUINTPTR)(key1B) == (RTGCUINTPTR)(key2B) && (RTGCUINTPTR)(key1E) == (RTGCUINTPTR)(key2E) )
#define KAVL_R_IS_INTERSECTING(key1B, key2B, key1E, key2E)  ( (RTGCUINTPTR)(key1B) <= (RTGCUINTPTR)(key2E) && (RTGCUINTPTR)(key1E) >= (RTGCUINTPTR)(key2B) )
#define KAVL_R_IS_IN_RANGE(    key1B, key1E, key2)          KAVL_R_IS_INTERSECTING(key1B, key2, key1E, key2)
#include "avl_Range.cpp.h"

