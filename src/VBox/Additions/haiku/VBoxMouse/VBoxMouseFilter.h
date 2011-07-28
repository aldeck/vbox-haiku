/** @file
 * VBoxMouse - Shared Clipboard
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
 */

#ifndef __VBOXMOUSE__H
#define __VBOXMOUSE__H

#include <InputServerFilter.h>

extern "C" _EXPORT BInputServerFilter* instantiate_input_filter();

class VBoxMouseFilter : public BInputServerFilter {
public:
	VBoxMouseFilter();
	virtual ~VBoxMouseFilter();
// 	virtual status_t InitCheck();
	virtual filter_result Filter(BMessage* message, BList* outList);

private:

static status_t	_ServiceThreadNub(void *_this);
	status_t	_ServiceThread();

	int			fDriverFD;
	thread_id	fServiceThreadID;
	bool		fExiting;
	bool		fEnabled;
	int32		fCurrentButtons;
};


#endif /* __VBOXMOUSE__H */
