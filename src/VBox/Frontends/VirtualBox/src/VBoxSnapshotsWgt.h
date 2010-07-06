/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxSnapshotsWgt class declaration
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBoxSnapshotsWgt_h__
#define __VBoxSnapshotsWgt_h__

/* Global includes */
#include <QTimer>

/* Local includes */
#include "VBoxSnapshotsWgt.gen.h"
#include "VBoxGlobal.h"
#include "QIWithRetranslateUI.h"

/* Local forwards */
class SnapshotWgtItem;

/* Snapshot age format */
enum SnapshotAgeFormat
{
    AgeInSeconds,
    AgeInMinutes,
    AgeInHours,
    AgeInDays,
    AgeMax
};

class VBoxSnapshotsWgt : public QIWithRetranslateUI <QWidget>, public Ui::VBoxSnapshotsWgt
{
    Q_OBJECT;

public:

    VBoxSnapshotsWgt (QWidget *aParent);

    void setMachine (const CMachine &aMachine);

protected:

    void retranslateUi();

private slots:

    void onCurrentChanged (QTreeWidgetItem *aItem = 0);
    void onContextMenuRequested (const QPoint &aPoint);
    void onItemChanged (QTreeWidgetItem *aItem);

    void restoreSnapshot();
    void deleteSnapshot();
    void showSnapshotDetails();
    void takeSnapshot();

    void machineDataChanged(QString strId);
    void machineStateChanged(QString strId, KMachineState state);
    void sessionStateChanged(QString strId, KSessionState state);

    void updateSnapshotsAge();

private:

    void refreshAll();
    SnapshotWgtItem* findItem (const QString &aSnapshotId);
    SnapshotWgtItem* curStateItem();
    void populateSnapshots (const CSnapshot &aSnapshot, QTreeWidgetItem *aItem);
    SnapshotAgeFormat traverseSnapshotAge (QTreeWidgetItem *aParentItem);

    CMachine         mMachine;
    QString          mMachineId;
    KSessionState    mSessionState;
    SnapshotWgtItem *mCurSnapshotItem;
    bool             mEditProtector;

    QActionGroup    *mSnapshotActionGroup;
    QActionGroup    *mCurStateActionGroup;

    QAction         *mRestoreSnapshotAction;
    QAction         *mDeleteSnapshotAction;
    QAction         *mShowSnapshotDetailsAction;
    QAction         *mTakeSnapshotAction;

    QTimer          mAgeUpdateTimer;
};

#endif // __VBoxSnapshotsWgt_h__

