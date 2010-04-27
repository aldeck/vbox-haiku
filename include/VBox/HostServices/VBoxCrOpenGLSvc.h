/** @file
 * OpenGL:
 * Common header for host service and guest clients.
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

#ifndef ___VBox_HostService_VBoxCrOpenGLSvc_h
#define ___VBox_HostService_VBoxCrOpenGLSvc_h

#include <VBox/types.h>
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest2.h>
#include <VBox/hgcmsvc.h>

/* crOpenGL host functions */
#define SHCRGL_HOST_FN_SET_CONSOLE (1)
#define SHCRGL_HOST_FN_SET_VISIBLE_REGION (5)
#define SHCRGL_HOST_FN_SET_VM (7)
#define SHCRGL_HOST_FN_SCREEN_CHANGED (8)
/* crOpenGL guest functions */
#define SHCRGL_GUEST_FN_WRITE       (2)
#define SHCRGL_GUEST_FN_READ        (3)
#define SHCRGL_GUEST_FN_WRITE_READ  (4)
#define SHCRGL_GUEST_FN_SET_VERSION (6)

/* Parameters count */
#define SHCRGL_CPARMS_SET_CONSOLE (1)
#define SHCRGL_CPARMS_SET_VM (1)
#define SHCRGL_CPARMS_SET_VISIBLE_REGION (2)
#define SHCRGL_CPARMS_WRITE      (1)
#define SHCRGL_CPARMS_READ       (2)
#define SHCRGL_CPARMS_WRITE_READ (3)
#define SHCRGL_CPARMS_SET_VERSION (2)
#define SHCRGL_CPARMS_SCREEN_CHANGED (1)

/**
 * SHCRGL_GUEST_FN_WRITE
 */

/** GUEST_FN_WRITE Parameters structure. */
typedef struct
{
    VBoxGuestHGCMCallInfo   hdr;

    /** pointer, in
     *  Data buffer
     */
    HGCMFunctionParameter   pBuffer;
} CRVBOXHGCMWRITE;

/** GUEST_FN_READ Parameters structure. */
typedef struct
{
    VBoxGuestHGCMCallInfo   hdr;

    /** pointer, in/out
     *  Data buffer
     */
    HGCMFunctionParameter   pBuffer;

    /** 32bit, out
     * Count of bytes written to buffer
     */
    HGCMFunctionParameter   cbBuffer;

} CRVBOXHGCMREAD;

/** GUEST_FN_WRITE_READ Parameters structure. */
typedef struct
{
    VBoxGuestHGCMCallInfo   hdr;

    /** pointer, in
     *  Data buffer
     */
    HGCMFunctionParameter   pBuffer;

    /** pointer, out
     *  Writeback buffer
     */
    HGCMFunctionParameter   pWriteback;

    /** 32bit, out
     * Count of bytes written to writeback buffer
     */
    HGCMFunctionParameter   cbWriteback;

} CRVBOXHGCMWRITEREAD;

/** GUEST_FN_SET_VERSION Parameters structure. */
typedef struct
{
    VBoxGuestHGCMCallInfo   hdr;

    /** 32bit, in
     *  Major version
     */
    HGCMFunctionParameter   vMajor;

    /** 32bit, in
     *  Minor version
     */
    HGCMFunctionParameter   vMinor;

} CRVBOXHGCMSETVERSION;

#endif
