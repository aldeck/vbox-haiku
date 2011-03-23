/* $Id$ */
/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * UIMachineSettingsGeneral class implementation
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "UIMachineSettingsGeneral.h"
#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "QIWidgetValidator.h"

#include <QDir>

UIMachineSettingsGeneral::UIMachineSettingsGeneral()
    : mValidator(0)
{
    /* Apply UI decorations */
    Ui::UIMachineSettingsGeneral::setupUi (this);

    /* Setup validators */
    mLeName->setValidator (new QRegExpValidator (QRegExp (".+"), this));

    /* Shared Clipboard mode */
    mCbClipboard->addItem (""); /* KClipboardMode_Disabled */
    mCbClipboard->addItem (""); /* KClipboardMode_HostToGuest */
    mCbClipboard->addItem (""); /* KClipboardMode_GuestToHost */
    mCbClipboard->addItem (""); /* KClipboardMode_Bidirectional */

#ifdef Q_WS_MAC
    mTeDescription->setMinimumHeight (150);
#endif /* Q_WS_MAC */

    /* Applying language settings */
    retranslateUi();
}

bool UIMachineSettingsGeneral::is64BitOSTypeSelected() const
{
    return mOSTypeSelector->type().GetIs64Bit();
}

#ifdef VBOX_WITH_VIDEOHWACCEL
bool UIMachineSettingsGeneral::isWindowsOSTypeSelected() const
{
    return mOSTypeSelector->type().GetFamilyId() == "Windows";
}
#endif

#ifdef VBOX_WITH_CRHGSMI
bool UIMachineSettingsGeneral::isWddmSupportedForOSType() const
{
    const QString &strOsId = mOSTypeSelector->type().GetId();
    return strOsId == "WindowsVista" || strOsId == "Windows7";
}
#endif

/* Load data to cashe from corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void UIMachineSettingsGeneral::loadToCacheFrom(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Fill internal variables with corresponding values: */
    m_cache.m_strName = m_machine.GetName();
    m_cache.m_strGuestOsTypeId = m_machine.GetOSTypeId();
    QString strSaveMountedAtRuntime = m_machine.GetExtraData(VBoxDefs::GUI_SaveMountedAtRuntime);
    m_cache.m_fSaveMountedAtRuntime = strSaveMountedAtRuntime != "no";
    QString strShowMiniToolBar = m_machine.GetExtraData(VBoxDefs::GUI_ShowMiniToolBar);
    m_cache.m_fShowMiniToolBar = strShowMiniToolBar != "no";
    QString strMiniToolBarAlignment = m_machine.GetExtraData(VBoxDefs::GUI_MiniToolBarAlignment);
    m_cache.m_fMiniToolBarAtTop = strMiniToolBarAlignment == "top";
    m_cache.m_strSnapshotsFolder = m_machine.GetSnapshotFolder();
    m_cache.m_strSnapshotsHomeDir = QFileInfo(m_machine.GetSettingsFilePath()).absolutePath();
    m_cache.m_clipboardMode = m_machine.GetClipboardMode();
    m_cache.m_strDescription = m_machine.GetDescription();

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

/* Load data to corresponding widgets from cache,
 * this task SHOULD be performed in GUI thread only: */
void UIMachineSettingsGeneral::getFromCache()
{
    /* Apply internal variables data to QWidget(s): */
    mLeName->setText(m_cache.m_strName);
    mOSTypeSelector->setType(vboxGlobal().vmGuestOSType(m_cache.m_strGuestOsTypeId));
    mCbSaveMounted->setChecked(m_cache.m_fSaveMountedAtRuntime);
    mCbShowToolBar->setChecked(m_cache.m_fShowMiniToolBar);
    mCbToolBarAlignment->setChecked(m_cache.m_fMiniToolBarAtTop);
    mCbToolBarAlignment->setEnabled(mCbShowToolBar->isChecked());
    mPsSnapshot->setPath(m_cache.m_strSnapshotsFolder);
    mPsSnapshot->setHomeDir(m_cache.m_strSnapshotsHomeDir);
    mCbClipboard->setCurrentIndex(m_cache.m_clipboardMode);
    mTeDescription->setPlainText(m_cache.m_strDescription);

    /* Revalidate if possible: */
    if (mValidator) mValidator->revalidate();
}

/* Save data from corresponding widgets to cache,
 * this task SHOULD be performed in GUI thread only: */
void UIMachineSettingsGeneral::putToCache()
{
    /* Gather internal variables data from QWidget(s): */
    m_cache.m_strName = mLeName->text();
    m_cache.m_strGuestOsTypeId = mOSTypeSelector->type().GetId();
    m_cache.m_fSaveMountedAtRuntime = mCbSaveMounted->isChecked();
    m_cache.m_fShowMiniToolBar = mCbShowToolBar->isChecked();
    m_cache.m_fMiniToolBarAtTop = mCbToolBarAlignment->isChecked();
    m_cache.m_strSnapshotsFolder = mPsSnapshot->path();
    m_cache.m_clipboardMode = (KClipboardMode)mCbClipboard->currentIndex();
    m_cache.m_strDescription = mTeDescription->toPlainText().isEmpty() ?
                               QString::null : mTeDescription->toPlainText();
}

/* Save data from cache to corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void UIMachineSettingsGeneral::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Save settings depending on dialog type: */
    switch (dialogType())
    {
        /* Here come the properties which could be changed only in offline state: */
        case VBoxDefs::SettingsDialogType_Offline:
        {
            /* Basic tab: */
            m_machine.SetOSTypeId(m_cache.m_strGuestOsTypeId);
            /* Advanced tab: */
            m_machine.SetSnapshotFolder(m_cache.m_strSnapshotsFolder);
            m_machine.SetClipboardMode(m_cache.m_clipboardMode);
            m_machine.SetExtraData(VBoxDefs::GUI_SaveMountedAtRuntime, m_cache.m_fSaveMountedAtRuntime ? "yes" : "no");
            m_machine.SetExtraData(VBoxDefs::GUI_ShowMiniToolBar, m_cache.m_fShowMiniToolBar ? "yes" : "no");
            m_machine.SetExtraData(VBoxDefs::GUI_MiniToolBarAlignment, m_cache.m_fMiniToolBarAtTop ? "top" : "bottom");
            /* Description tab: */
            m_machine.SetDescription(m_cache.m_strDescription);
            /* VM name must be last as otherwise its VM rename magic can collide with other settings in the config,
             * especially with the snapshot folder: */
            m_machine.SetName(m_cache.m_strName);
            break;
        }
        /* Here come the properties which could be changed at runtime too: */
        case VBoxDefs::SettingsDialogType_Runtime:
        {
            /* Advanced tab: */
            m_machine.SetClipboardMode(m_cache.m_clipboardMode);
            m_machine.SetExtraData(VBoxDefs::GUI_SaveMountedAtRuntime, m_cache.m_fSaveMountedAtRuntime ? "yes" : "no");
            m_machine.SetExtraData(VBoxDefs::GUI_ShowMiniToolBar, m_cache.m_fShowMiniToolBar ? "yes" : "no");
            m_machine.SetExtraData(VBoxDefs::GUI_MiniToolBarAlignment, m_cache.m_fMiniToolBarAtTop ? "top" : "bottom");
            /* Description tab: */
            m_machine.SetDescription(m_cache.m_strDescription);
            break;
        }
        default:
            break;
    }

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsGeneral::setValidator (QIWidgetValidator *aVal)
{
    mValidator = aVal;
    connect (mOSTypeSelector, SIGNAL (osTypeChanged()), mValidator, SLOT (revalidate()));
}

void UIMachineSettingsGeneral::setOrderAfter (QWidget *aWidget)
{
    /* Basic tab-order */
    setTabOrder (aWidget, mTwGeneral->focusProxy());
    setTabOrder (mTwGeneral->focusProxy(), mLeName);
    setTabOrder (mLeName, mOSTypeSelector);

    /* Advanced tab-order */
    setTabOrder (mOSTypeSelector, mPsSnapshot);
    setTabOrder (mPsSnapshot, mCbClipboard);
    setTabOrder (mCbClipboard, mCbSaveMounted);
    setTabOrder (mCbSaveMounted, mCbShowToolBar);
    setTabOrder (mCbShowToolBar, mCbToolBarAlignment);

    /* Description tab-order */
    setTabOrder (mCbToolBarAlignment, mTeDescription);
}

void UIMachineSettingsGeneral::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::UIMachineSettingsGeneral::retranslateUi (this);

    /* Path selector */
    mPsSnapshot->setWhatsThis (tr ("Displays the path where snapshots of this "
                                   "virtual machine will be stored. Be aware that "
                                   "snapshots can take quite a lot of disk "
                                   "space."));

    /* Shared Clipboard mode */
    mCbClipboard->setItemText (0, vboxGlobal().toString (KClipboardMode_Disabled));
    mCbClipboard->setItemText (1, vboxGlobal().toString (KClipboardMode_HostToGuest));
    mCbClipboard->setItemText (2, vboxGlobal().toString (KClipboardMode_GuestToHost));
    mCbClipboard->setItemText (3, vboxGlobal().toString (KClipboardMode_Bidirectional));
}

void UIMachineSettingsGeneral::polishPage()
{
    /* Polish page depending on dialog type: */
    switch (dialogType())
    {
        case VBoxDefs::SettingsDialogType_Offline:
            break;
        case VBoxDefs::SettingsDialogType_Runtime:
            /* Basic tab: */
            mLbName->setEnabled(false);
            mLeName->setEnabled(false);
            mOSTypeSelector->setEnabled(false);
            /* Advanced tab: */
            mLbSnapshot->setEnabled(false);
            mPsSnapshot->setEnabled(false);
            break;
        default:
            break;
    }
}

