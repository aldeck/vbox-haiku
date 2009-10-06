/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMFirstRunWzd class declaration
 */

/*
 * Copyright (C) 2008-2009 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef __VBoxVMFirstRunWzd_h__
#define __VBoxVMFirstRunWzd_h__

/* Local includes */
#include "QIAbstractWizard.h"
#include "VBoxVMFirstRunWzd.gen.h"
#include "COMDefs.h"
#include "QIWithRetranslateUI.h"

/* Local forwardes */
class QIWidgetValidator;

class VBoxVMFirstRunWzd : public QIWithRetranslateUI <QIAbstractWizard>,
                          public Ui::VBoxVMFirstRunWzd
{
    Q_OBJECT;

public:

    VBoxVMFirstRunWzd (const CMachine &aMachine, QWidget *aParent = 0);

protected:

    void retranslateUi();

private slots:

    void accept();
    void revalidate (QIWidgetValidator *aValidator);
    void mediaTypeChanged();
    void openMediaManager();
    void enableNext (const QIWidgetValidator *aValidator);
    void onPageShow();

private:

    QIWidgetValidator *mValidator;
    CMachine           mMachine;
};

#endif // __VBoxVMFirstRunWzd_h__

