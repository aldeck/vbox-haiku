/** @file
 * HWACCM - Intel/AMD VM Hardware Support Manager
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

#ifndef __VBox_hwaccm_h__
#define __VBox_hwaccm_h__

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/pgm.h>


/** @defgroup grp_hwaccm      The VM Hardware Manager API
 * @{
 */


__BEGIN_DECLS

/**
 * Query HWACCM state (enabled/disabled)
 *
 * @returns 0 - disabled, 1 - enabled
 * @param   pVM         The VM to operate on.
 */
#define HWACCMIsEnabled(a)    (a->fHWACCMEnabled)

#ifdef IN_RING0
/** @defgroup grp_hwaccm_r0    The VM Hardware Manager API
 * @ingroup grp_hwaccm
 * @{
 */

/**
 * Does Ring-0 HWACCM initialization.
 *
 * This is mainly to check that the Host CPU mode is compatible
 * with VMX or SVM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) HWACCMR0Init(PVM pVM);

/** @} */
#endif


#ifdef IN_RING3
/** @defgroup grp_hwaccm_r3    The VM Hardware Manager API
 * @ingroup grp_hwaccm
 * @{
 */

/**
 * Checks if internal events are pending
 *
 * @returns boolean
 * @param   pVM         The VM to operate on.
 */
HWACCMR3DECL(bool) HWACCMR3IsEventPending(PVM pVM);

/**
 * Initializes the HWACCM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR3DECL(int) HWACCMR3Init(PVM pVM);

/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The HWACCM will update the addresses used by the switcher.
 *
 * @param   pVM     The VM.
 */
HWACCMR3DECL(void) HWACCMR3Relocate(PVM pVM);

/**
 * Terminates the VMXM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR3DECL(int) HWACCMR3Term(PVM pVM);

/**
 * VMXM reset callback.
 *
 * @param   pVM     The VM which is reset.
 */
HWACCMR3DECL(void) HWACCMR3Reset(PVM pVM);


/**
 * Checks if we can currently use hardware accelerated raw mode.
 *
 * @returns boolean
 * @param   pVM         The VM to operate on.
 * @param   pCtx        Partial VM execution context
 */
HWACCMR3DECL(bool) HWACCMR3CanExecuteGuest(PVM pVM, PCPUMCTX pCtx);


/**
 * Checks if we are currently using hardware accelerated raw mode.
 *
 * @returns boolean
 * @param   pVM         The VM to operate on.
 */
HWACCMR3DECL(bool) HWACCMR3IsActive(PVM pVM);

/**
 * Checks hardware accelerated raw mode is allowed.
 *
 * @returns boolean
 * @param   pVM         The VM to operate on.
 */
HWACCMR3DECL(bool) HWACCMR3IsAllowed(PVM pVM);

/**
 * Notification callback which is called whenever there is a chance that a CR3
 * value might have changed.
 * This is called by PGM.
 *
 * @param   pVM            The VM to operate on.
 * @param   enmShadowMode  New paging mode.
 */
HWACCMR3DECL(void) HWACCMR3PagingModeChanged(PVM pVM, PGMMODE enmShadowMode);

/** @} */
#endif

#ifdef IN_RING0
/** @addtogroup grp_hwaccm_r0
 * @{
 */

/**
 * Does Ring-0 VMX initialization.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) HWACCMR0SetupVMX(PVM pVM);


/**
 * Runs guest code in a VMX/SVM VM.
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) HWACCMR0RunGuestCode(PVM pVM);

/**
 * Enable VMX or SVN
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) HWACCMR0Enable(PVM pVM);


/**
 * Disable VMX or SVN
 *
 * @returns VBox status code.
 * @param   pVM         The VM to operate on.
 */
HWACCMR0DECL(int) HWACCMR0Disable(PVM pVM);


/** @} */
#endif


/** @} */
__END_DECLS


#endif /* !__VBox_hwaccm_h__ */

