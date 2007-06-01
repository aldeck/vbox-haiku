/* $Id$ */
/** @file
 * Our minimal stdio
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

#ifndef __stdio_h__
#define __stdio_h__

#ifndef LOG_GROUP
# define UNDO_LOG_GROUP
#endif

#include <VBox/log.h>

#ifdef UNDO_LOG_GROUP
# undef UNDO_LOG_GROUP
# undef LOG_GROUP
#endif

#ifndef LOG_USE_C99
# error "LOG_USE_C99 isn't defined."
#endif

__BEGIN_DECLS

typedef struct FILE FILE;

DECLINLINE(int) fprintf(FILE *ignored, const char *pszFormat, ...)
{
/** @todo We don't support wrapping calls taking a va_list yet. It's not worth it yet,
 * since there are only a couple of cases where this fprintf implementation is used.
 * (The macro below will deal with the majority of the fprintf calls.) */
#if 0 /*def LOG_ENABLED*/
    if (LogIsItEnabled(NULL, 0, LOG_GROUP_REM_PRINTF))
    {
        va_list va;
        va_start(va, pszFormat);
        RTLogLoggerExV(NULL, 0, LOG_GROUP_REM_PRINTF, pszFormat, va);
        va_end(va);
    }
#endif
    return 0;
}

#define fflush(file)            RTLogFlush(NULL)
#define printf(...)             LogIt(LOG_INSTANCE, 0, LOG_GROUP_REM_PRINTF, (__VA_ARGS__))
#define fprintf(logfile, ...)   LogIt(LOG_INSTANCE, 0, LOG_GROUP_REM_PRINTF, (__VA_ARGS__))


__END_DECLS

#endif

