/** @file
 *
 * supc++ -- VirtualBox Guest Additions for Linux:
 * VirtualBox timesync daemon for Linux
 *
 * Support for linking a C++ static library into a C programme
 *
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

#include <stdio.h>

/* C++ hack */
int __gxx_personality_v0 = 0xdeadbeef;

/* long long hacks (as far as i can see, gcc emits the refs to those
   symbols, notwithstanding the fact that those aren't referenced
   anywhere) */
/* I will have to think of something better than that printf sometime. */
void __divdi3 (void);

void __divdi3 (void)
{
        printf ("__divdi3 called from %p\n", __builtin_return_address (0));
        /* exit(1); */  /* Should we do this? */
}

void __moddi3 (void);

void __moddi3 (void)
{
        printf ("__moddi3 called from %p\n", __builtin_return_address (0));
        /* exit(1); */
}
