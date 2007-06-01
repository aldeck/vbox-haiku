/** @file
 *
 * VBoxGuestLib - A support library for VirtualBox guest additions:
 * Guest Logging facility
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

#ifndef __VBOXGUESTLOG__H
#define __VBOXGUESTLOG__H

/* Save LOG_ENABLED state, because "VBox/rt/log.h"
 * may undefine it for IN_RING0 code.
 */
#if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
# define __LOG_ENABLED_SAVED__
#endif

#if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
# ifdef VBOX_GUEST
#  include <VBox/log.h>
#  undef Log
#  define Log(a)  RTLogBackdoorPrintf a
# else
#  define Log(a)  DbgPrint a
# endif
#else
# define Log(a)
#endif

#ifdef __LOG_ENABLED_SAVED__
# define LOG_ENABLED
# undef __LOG_ENABLED_SAVED__
#endif

#endif /* __VBOXGUESTLOG__H */
