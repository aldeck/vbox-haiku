/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsCD class declaration
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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

#ifndef __VBoxVMSettingsCD_h__
#define __VBoxVMSettingsCD_h__

#include "VBoxSettingsPage.h"
#include "VBoxVMSettingsCD.gen.h"
#include "COMDefs.h"

class VBoxVMSettingsCD : public VBoxSettingsPage,
                         public Ui::VBoxVMSettingsCD
{
    Q_OBJECT;

public:

    VBoxVMSettingsCD();

signals:

    void cdChanged();

protected:

    void getFrom (const CMachine &aMachine);
    void putBackTo();

    void setValidator (QIWidgetValidator *aVal);
    bool revalidate (QString &aWarning, QString &aTitle);

    void setOrderAfter (QWidget *aWidget);

    void retranslateUi();

private slots:

    void onGbChange (bool aSwitchedOn);
    void onRbChange();
    void onCbChange();
    void showImageManager();

private:

    CMachine mMachine;
    QIWidgetValidator *mValidator;
    QVector<CHostDVDDrive> mHostCDs;
    QUuid mUuidIsoCD;
    QRadioButton *mLastSelected;
};

#endif // __VBoxVMSettingsCD_h__

