/** @file
 * VBoxGuestDeskbarView - Guest Additions Deskbar Tray View
 */

/*
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __VBOXGUESTTRAYVIEW__H
#define __VBOXGUESTTRAYVIEW__H

#include <Bitmap.h>
#include <View.h>

#include <iprt/initterm.h>
#include <iprt/string.h>

#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/VBoxGuest.h> /** @todo use the VbglR3 interface! */
#include <VBox/VBoxGuestLib.h>

#include <VBoxGuestInternal.h>
#include "VBoxClipboard.h"
#include "VBoxDisplay.h"

#define REMOVE_FROM_DESKBAR_MSG 'vbqr'

class VBoxGuestDeskbarView : public BView {
public:
			VBoxGuestDeskbarView();
			VBoxGuestDeskbarView(BMessage *archive);
	virtual ~VBoxGuestDeskbarView();
	static  BArchivable	*Instantiate(BMessage *data);
	virtual	status_t	Archive(BMessage *data, bool deep = true) const;

			void		Draw(BRect rect);
			void		AttachedToWindow();
			void		DetachedFromWindow();

	virtual	void		MouseDown(BPoint point);
	virtual void		MessageReceived(BMessage* message);

	static status_t		AddToDeskbar(bool force=true);
	static status_t		RemoveFromDeskbar();
	
private:
	status_t			_Init(BMessage *archive=NULL);
	BBitmap				*fIcon;
	
	VBoxClipboardService *fClipboardService;
	VBoxDisplayService *fDisplayService;
};

#endif /* __VBOXGUESTTRAYVIEW__H */
