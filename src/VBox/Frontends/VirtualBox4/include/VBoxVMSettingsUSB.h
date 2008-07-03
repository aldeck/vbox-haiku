/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsUSB class declaration
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

#ifndef __VBoxVMSettingsUSB_h__
#define __VBoxVMSettingsUSB_h__

#include "VBoxSettingsPage.h"
#include "VBoxVMSettingsUSB.gen.h"
#include "COMDefs.h"

class VBoxUSBMenu;

class VBoxVMSettingsUSB : public VBoxSettingsPage,
                          public Ui::VBoxVMSettingsUSB
{
    Q_OBJECT;

public:

    enum FilterType
    {
        WrongType = 0,
        HostType = 1,
        MachineType = 2
    };

    VBoxVMSettingsUSB (FilterType aType);

protected:

    void getFrom (const CSystemProperties &aProps,
                  const VBoxGlobalSettings &aGs);
    void putBackTo (CSystemProperties &aProps,
                    VBoxGlobalSettings &aGs);

    void getFrom (const CMachine &aMachine);
    void putBackTo();

    void setValidator (QIWidgetValidator *aVal);

    void setOrderAfter (QWidget *aWidget);

    void retranslateUi();

private slots:

    void usbAdapterToggled (bool aOn);
    void currentChanged (QTreeWidgetItem *aItem = 0,
                         QTreeWidgetItem *aPrev = 0);
    void setCurrentText (const QString &aText);
    void newClicked();
    void addClicked();
    void addConfirmed (QAction *aAction);
    void delClicked();
    void mupClicked();
    void mdnClicked();
    void showContextMenu (const QPoint &aPos);

private:

    void addUSBFilter (const CUSBDeviceFilter &aFilter, bool isNew);

    CMachine mMachine;
    QIWidgetValidator *mValidator;
    FilterType mType;
    QAction *mNewAction;
    QAction *mAddAction;
    QAction *mDelAction;
    QAction *mMupAction;
    QAction *mMdnAction;
    QMenu *mMenu;
    VBoxUSBMenu *mUSBDevicesMenu;
    bool mUSBFilterListModified;
    QList<CUSBDeviceFilter> mFilters;

    QString mUSBFilterName;
};

#endif // __VBoxVMSettingsUSB_h__

