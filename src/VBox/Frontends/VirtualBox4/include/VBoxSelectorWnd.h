/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxSelectorWnd class declaration
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

#ifndef __VBoxSelectorWnd_h__
#define __VBoxSelectorWnd_h__

#include "COMDefs.h"

#include "QIWithRetranslateUI.h"
#include "VBoxGlobal.h"

/* Qt includes */
#include <QMainWindow>

class VBoxSnapshotsWgt;
class VBoxVMDetailsView;
class VBoxVMDescriptionPage;
class VBoxVMLogViewer;
class VBoxVMListView;
class VBoxVMModel;
class VBoxVMItem;

class QTabWidget;
class QListView;
class QEvent;
class QUuid;

class VBoxSelectorWnd: public QIWithRetranslateUI2<QMainWindow>
{
    Q_OBJECT;

public:

    VBoxSelectorWnd (VBoxSelectorWnd **aSelf,
                     QWidget* aParent = 0,
                     Qt::WindowFlags aFlags = Qt::Window);
    virtual ~VBoxSelectorWnd();

    bool startMachine (const QUuid &id);

public slots:

    void fileDiskMgr();
    void fileSettings();
    void fileExit();

    void vmNew();
    void vmSettings (const QString &aCategory = QString::null,
                     const QString &aControl = QString::null);
    void vmDelete();
    void vmStart();
    void vmDiscard();
    void vmPause (bool);
    void vmRefresh();
    void vmShowLogs();

    void refreshVMList();
    void refreshVMItem (const QUuid &aID, bool aDetails,
                                          bool aSnapshots,
                                          bool aDescription);

    void showContextMenu (VBoxVMItem *aItem, const QPoint &aPoint);

protected:

    /* events */
    bool event (QEvent *e);

    void retranslateUi();

private slots:

    void vmListViewCurrentChanged (bool aRefreshDetails = true,
                                   bool aRefreshSnapshots = true,
                                   bool aRefreshDescription = true);

    void mediaEnumStarted();
    void mediaEnumFinished (const VBoxMediaList &);

    /* VirtualBox callback events we're interested in */

    void machineStateChanged (const VBoxMachineStateChangeEvent &e);
    void machineDataChanged (const VBoxMachineDataChangeEvent &e);
    void machineRegistered (const VBoxMachineRegisteredEvent &e);
    void sessionStateChanged (const VBoxSessionStateChangeEvent &e);
    void snapshotChanged (const VBoxSnapshotEvent &e);

private:
    /* Main menus */
    QMenu *mFileMenu;
    QMenu *mVMMenu;
    QMenu *mHelpMenu;

    /* VM list context menu */
    QMenu *mVMCtxtMenu;

    /* actions */
    QAction *fileDiskMgrAction;
    QAction *fileSettingsAction;
    QAction *fileExitAction;
    QAction *vmNewAction;
    QAction *vmConfigAction;
    QAction *vmDeleteAction;
    QAction *vmStartAction;
    QAction *vmDiscardAction;
    QAction *vmPauseAction;
    QAction *vmRefreshAction;
    QAction *vmShowLogsAction;
    QAction *helpContentsAction;
    QAction *helpWebAction;
    QAction *helpRegisterAction;
    QAction *helpAboutAction;
    QAction *helpResetMessagesAction;

    /* The vm list view/model */
    VBoxVMListView *mVMListView;
    VBoxVMModel *mVMModel;

    /* The right information widgets */
    QTabWidget *vmTabWidget;
    VBoxVMDetailsView *vmDetailsView;
    VBoxSnapshotsWgt *vmSnapshotsWgt;
    VBoxVMDescriptionPage *vmDescriptionPage;

    QPoint normal_pos;
    QSize normal_size;

    bool doneInaccessibleWarningOnce : 1;
};

#endif // __VBoxSelectorWnd_h__
