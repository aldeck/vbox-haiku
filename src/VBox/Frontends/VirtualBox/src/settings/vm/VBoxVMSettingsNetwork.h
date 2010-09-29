/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsNetwork class declaration
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBoxVMSettingsNetwork_h__
#define __VBoxVMSettingsNetwork_h__

/* VBox Includes */
#include "COMDefs.h"
#include "UISettingsPage.h"
#include "VBoxVMSettingsNetwork.gen.h"
#include "VBoxVMSettingsPortForwardingDlg.h"

/* VBox Forwardes */
class VBoxVMSettingsNetworkPage;
class QITabWidget;

class VBoxVMSettingsNetwork : public QIWithRetranslateUI <QWidget>,
                              public Ui::VBoxVMSettingsNetwork
{
    Q_OBJECT;

public:

    VBoxVMSettingsNetwork (VBoxVMSettingsNetworkPage *aParent, bool aDisableStaticControls = false);

    void getFromAdapter (const CNetworkAdapter &aAdapter);
    void putBackToAdapter();

    void setValidator (QIWidgetValidator *aValidator);
    bool revalidate (QString &aWarning, QString &aTitle);

    QWidget* setOrderAfter (QWidget *aAfter);

    QString pageTitle() const;
    KNetworkAttachmentType attachmentType() const;
    QString alternativeName (int aType = -1) const;

protected:

    void showEvent (QShowEvent *aEvent);

    void retranslateUi();

private slots:

    void updateAttachmentAlternative();
    void updateAlternativeName();
    void toggleAdvanced();
    void generateMac();
    void sltOpenPortForwardingDlg();

private:

    void populateComboboxes();

    VBoxVMSettingsNetworkPage *mParent;
    CNetworkAdapter mAdapter;
    QIWidgetValidator *mValidator;

    QString mBrgName;
    QString mIntName;
    QString mHoiName;
#ifdef VBOX_WITH_VDE
    QString mVDEName;
#endif

    bool mPolished;
    bool mDisableStaticControls;
    UIPortForwardingDataList mPortForwardingRules;
};

class VBoxVMSettingsNetworkPage : public UISettingsPage
{
    Q_OBJECT;

public:

    VBoxVMSettingsNetworkPage (bool aDisableStaticControls = false);

    QStringList brgList (bool aRefresh = false);
    QStringList intList (bool aRefresh = false);
    QStringList hoiList (bool aRefresh = false);
#ifdef VBOX_WITH_VDE
    QStringList vdeList (bool aRefresh = false);
#endif

protected:

    void getFrom (const CMachine &aMachine);
    void putBackTo();

    void setValidator (QIWidgetValidator *aValidator);
    bool revalidate (QString &aWarning, QString &aTitle);

    void retranslateUi();

private slots:

    void updatePages();

private:

    QIWidgetValidator *mValidator;
    QITabWidget *mTwAdapters;

    QStringList mBrgList;
    QStringList mIntList;
    QStringList mHoiList;

    bool mDisableStaticControls;
};

#endif // __VBoxVMSettingsNetwork_h__

