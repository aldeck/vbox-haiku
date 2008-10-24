/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxDiskImageManagerDlg class implementation
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

#include "VBoxDiskImageManagerDlg.h"
#include "VBoxToolBar.h"
#include "QILabel.h"
#include "VBoxNewHDWzd.h"
#include "VBoxProblemReporter.h"

/* Qt includes */
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenuBar>
#include <QPushButton>
#include <QUrl>
#include <QProgressBar>


class AddVDMUrlsEvent: public QEvent
{
public:

    AddVDMUrlsEvent (const QList<QUrl> &aUrls)
        : QEvent (static_cast<QEvent::Type> (VBoxDefs::AddVDMUrlsEventType))
        , mUrls (aUrls)
    {}

    const QList<QUrl> &urls() const { return mUrls; }

private:

    const QList<QUrl> mUrls;
};

class DiskImageItem : public QTreeWidgetItem
{
public:

    DiskImageItem (DiskImageItem *aParent) :
        QTreeWidgetItem (aParent, QITreeWidget::BasicItemType), mStatus (VBoxMedia::Unknown) {}

    DiskImageItem (QTreeWidget *aParent) :
        QTreeWidgetItem (aParent, QITreeWidget::BasicItemType), mStatus (VBoxMedia::Unknown) {}

    void setMedia (const VBoxMedia &aMedia) { mMedia = aMedia; }
    const VBoxMedia &media() const { return mMedia; }

    void setPath (const QString &aPath) { mPath = aPath; }
    const QString &path() const { return mPath; }

    void setUsage (const QString &aUsage) { mUsage = aUsage; }
    const QString &usage() const { return mUsage; }

    void setSnapshotUsage (const QString &aSnapshotUsage) { mSnapshotUsage = aSnapshotUsage; }
    const QString &snapshotUsage() const { return mSnapshotUsage; }

    QString totalUsage() const
    {
        /* Should correlate with VBoxDiskImageManagerDlg::compose[Cd/Fd]Tooltip */
        return mSnapshotUsage.isNull() ? mUsage :
            QString ("%1 (%2)").arg (mUsage, mSnapshotUsage);
    }

    void setSnapshotName (const QString &aSnapshotName) { mSnapshotName = aSnapshotName; }
    const QString &snapshotName() const { return mSnapshotName; }

    void setDiskType (const QString &aDiskType) { mDiskType = aDiskType; }
    const QString &diskType() const { return mDiskType; }

    void setStorageType (const QString &aStorageType) { mStorageType = aStorageType; }
    const QString &storageType() const { return mStorageType; }

    void setVirtualSize (const QString &aVirtualSize) { mVirtualSize = aVirtualSize; }
    const QString &virtualSize() const { return mVirtualSize; }

    void setActualSize (const QString &aActualSize) { mActualSize = aActualSize; }
    const QString &ActualSize() const { return mActualSize; }

    void setUuid (const QUuid &aUuid) { mUuid = aUuid; }
    const QUuid &uuid() const { return mUuid; }

    void setMachineId (const QUuid &aMachineId) { mMachineId = aMachineId; }
    const QUuid &machineId() const { return mMachineId; }

    void setStatus (VBoxMedia::Status aStatus) { mStatus = aStatus; }
    VBoxMedia::Status status() const { return mStatus; }

    void setToolTip (const QString& aToolTip)
    {
        mToolTip = aToolTip;
        for (int i = 0; i < treeWidget()->columnCount(); ++ i)
            QTreeWidgetItem::setToolTip (i, mToolTip);
    }
    const QString &toolTip() const { return mToolTip; }

    QString information (const QString &aInfo, bool aCompact = true,
                         const QString &aElipsis = "middle")
    {
        QString compactString = QString ("<compact elipsis=\"%1\">").arg (aElipsis);
        QString info = QString ("<nobr>%1%2%3</nobr>")
                       .arg (aCompact ? compactString : "")
                       .arg (aInfo.isEmpty() ?
                             QApplication::translate ("VBoxDiskImageManagerDlg", "--", "no info") :
                             aInfo)
                       .arg (aCompact ? "</compact>" : "");
        return info;
    }

    bool operator< (const QTreeWidgetItem &aOther) const
    {
        int column = treeWidget()->sortColumn();
        ULONG64 thisValue = vboxGlobal().parseSize (       text (column));
        ULONG64 thatValue = vboxGlobal().parseSize (aOther.text (column));
        if (thisValue && thatValue)
            return thisValue < thatValue;
        else
            return QTreeWidgetItem::operator< (aOther);
    }

//    void paintCell (QPainter *aPainter, const QColorGroup &aColorGroup,
//                    int aColumn, int aWidth, int aSlign)
//    {
//        QColorGroup cGroup (aColorGroup);
//        if (mStatus == VBoxMedia::Unknown)
//            cGroup.setColor (QColorGroup::Text, cGroup.mid());
//        Q3ListViewItem::paintCell (aPainter, cGroup, aColumn, aWidth, aSlign);
//    }

protected:

    /* Protected member vars */
    VBoxMedia mMedia;

    QString mName;
    QString mPath;
    QString mUsage;
    QString mSnapshotUsage;
    QString mSnapshotName;
    QString mDiskType;
    QString mStorageType;
    QString mVirtualSize;
    QString mActualSize;

    QUuid mUuid;
    QUuid mMachineId;

    QString mToolTip;

    VBoxMedia::Status mStatus;
};


class DiskImageItemIterator : public QTreeWidgetItemIterator
{
public:

    DiskImageItemIterator (QTreeWidget* aTree)
        : QTreeWidgetItemIterator (aTree) {}

    DiskImageItem* operator*()
    {
        QTreeWidgetItem *item = QTreeWidgetItemIterator::operator*();
        return item && item->type() == QITreeWidget::BasicItemType ?
            static_cast<DiskImageItem*> (item) : 0;
    }

    DiskImageItemIterator& operator++()
    {
        return static_cast<DiskImageItemIterator&> (QTreeWidgetItemIterator::operator++());
    }
};


class VBoxProgressBar: public QWidget
{
    Q_OBJECT;

public:

    VBoxProgressBar (QWidget *aParent)
        : QWidget (aParent)
    {
        mText = new QLabel (this);
        mProgressBar = new QProgressBar (this);
        mProgressBar->setTextVisible (false);

        QHBoxLayout *layout = new QHBoxLayout (this);
        VBoxGlobal::setLayoutMargin (layout, 0);
        layout->addWidget (mText);
        layout->addWidget (mProgressBar);
    }

    void setText (const QString &aText) { mText->setText (aText); }
    void setValue (int aValue) { mProgressBar->setValue (aValue); }
    void setMaximum (int aValue) { mProgressBar->setMaximum (aValue); }

private:

    QLabel *mText;
    QProgressBar *mProgressBar;
};


VBoxDiskImageManagerDlg* VBoxDiskImageManagerDlg::mModelessDialog = 0;

VBoxDiskImageManagerDlg::VBoxDiskImageManagerDlg (QWidget *aParent /* = 0 */,
                                                  Qt::WindowFlags aFlags /* = 0 */)
    : QIWithRetranslateUI2<QIMainDialog> (aParent, aFlags)
    , mType (VBoxDefs::InvalidType)
{
    /* Apply UI decorations */
    Ui::VBoxDiskImageManagerDlg::setupUi (this);

    /* Apply window icons */
    setWindowIcon (vboxGlobal().iconSetFull (QSize (32, 32), QSize (16, 16),
                                             ":/diskimage_32px.png", ":/diskimage_16px.png"));

    mVBox = vboxGlobal().virtualBox();
    Assert (!mVBox.isNull());

    mType = VBoxDefs::InvalidType;

    mIconInaccessible = vboxGlobal().standardIcon (QStyle::SP_MessageBoxWarning, this);
    mIconErroneous = vboxGlobal().standardIcon (QStyle::SP_MessageBoxCritical, this);
    mIconHD = VBoxGlobal::iconSet (":/hd_16px.png", ":/hd_disabled_16px.png");
    mIconCD = VBoxGlobal::iconSet (":/cd_16px.png", ":/cd_disabled_16px.png");
    mIconFD = VBoxGlobal::iconSet (":/fd_16px.png", ":/fd_disabled_16px.png");

    /* Setup tab widget icons */
    mTwImages->setTabIcon (HDTab, mIconHD);
    mTwImages->setTabIcon (CDTab, mIconCD);
    mTwImages->setTabIcon (FDTab, mIconFD);

    connect (mTwImages, SIGNAL (currentChanged (int)),
             this, SLOT (processCurrentChanged (int)));

    /* Setup the tree view widgets */
    mHdsTree->header()->setResizeMode (0, QHeaderView::Stretch);
    mHdsTree->header()->setResizeMode (1, QHeaderView::ResizeToContents);
    mHdsTree->header()->setResizeMode (2, QHeaderView::ResizeToContents);
    mHdsTree->setSortingEnabled (true);
    mHdsTree->setSupportedDropActions (Qt::LinkAction);
    mHdsTree->installEventFilter (this);
    connect (mHdsTree, SIGNAL (currentItemChanged (QTreeWidgetItem *, QTreeWidgetItem *)),
             this, SLOT (processCurrentChanged (QTreeWidgetItem *, QTreeWidgetItem *)));
    connect (mHdsTree, SIGNAL (itemDoubleClicked (QTreeWidgetItem *, int)),
             this, SLOT (processDoubleClick (QTreeWidgetItem *, int)));
    connect (mHdsTree, SIGNAL (customContextMenuRequested (const QPoint &)),
             this, SLOT (showContextMenu (const QPoint &)));

    mCdsTree->header()->setResizeMode (0, QHeaderView::Stretch);
    mCdsTree->header()->setResizeMode (1, QHeaderView::ResizeToContents);
    mCdsTree->setSortingEnabled (true);
    mCdsTree->setSupportedDropActions (Qt::LinkAction);
    mCdsTree->installEventFilter (this);
    connect (mCdsTree, SIGNAL (currentItemChanged (QTreeWidgetItem *, QTreeWidgetItem *)),
             this, SLOT (processCurrentChanged (QTreeWidgetItem *, QTreeWidgetItem *)));
    connect (mCdsTree, SIGNAL (itemDoubleClicked (QTreeWidgetItem *, int)),
             this, SLOT (processDoubleClick (QTreeWidgetItem *, int)));
    connect (mCdsTree, SIGNAL (customContextMenuRequested (const QPoint &)),
             this, SLOT (showContextMenu (const QPoint &)));

    mFdsTree->header()->setResizeMode (0, QHeaderView::Stretch);
    mFdsTree->header()->setResizeMode (1, QHeaderView::ResizeToContents);
    mFdsTree->setSortingEnabled (true);
    mFdsTree->setSupportedDropActions (Qt::LinkAction);
    mFdsTree->installEventFilter (this);
    connect (mFdsTree, SIGNAL (currentItemChanged (QTreeWidgetItem *, QTreeWidgetItem *)),
             this, SLOT (processCurrentChanged (QTreeWidgetItem *, QTreeWidgetItem *)));
    connect (mFdsTree, SIGNAL (itemDoubleClicked (QTreeWidgetItem *, int)),
             this, SLOT (processDoubleClick (QTreeWidgetItem *, int)));
    connect (mFdsTree, SIGNAL (customContextMenuRequested (const QPoint &)),
             this, SLOT (showContextMenu (const QPoint &)));

    /* Context menu composing */
    mActionsContextMenu = new QMenu (this);

    mNewAction = new QAction (this);
    mAddAction = new QAction (this);
    // mEditAction = new QAction (this);
    mRemoveAction = new QAction (this);
    mReleaseAction = new QAction (this);
    mRefreshAction = new QAction (this);

    connect (mNewAction, SIGNAL (triggered()),
             this, SLOT (newImage()));
    connect (mAddAction, SIGNAL (triggered()),
             this, SLOT (addImage()));
    // connect (mEditAction, SIGNAL (triggered()),
    //          this, SLOT (editImage()));
    connect (mRemoveAction, SIGNAL (triggered()),
             this, SLOT (removeImage()));
    connect (mReleaseAction, SIGNAL (triggered()),
             this, SLOT (releaseImage()));
    connect (mRefreshAction, SIGNAL (triggered()),
             this, SLOT (refreshAll()));

    mNewAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (22, 22), QSize (16, 16),
        ":/vdm_new_22px.png", ":/vdm_new_16px.png",
        ":/vdm_new_disabled_22px.png", ":/vdm_new_disabled_16px.png"));
    mAddAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (22, 22), QSize (16, 16),
        ":/vdm_add_22px.png", ":/vdm_add_16px.png",
        ":/vdm_add_disabled_22px.png", ":/vdm_add_disabled_16px.png"));
    // mEditAction->setIcon (VBoxGlobal::iconSet (":/guesttools_16px.png", ":/guesttools_disabled_16px.png"));
    mRemoveAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (22, 22), QSize (16, 16),
        ":/vdm_remove_22px.png", ":/vdm_remove_16px.png",
        ":/vdm_remove_disabled_22px.png", ":/vdm_remove_disabled_16px.png"));
    mReleaseAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (22, 22), QSize (16, 16),
        ":/vdm_release_22px.png", ":/vdm_release_16px.png",
        ":/vdm_release_disabled_22px.png", ":/vdm_release_disabled_16px.png"));
    mRefreshAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (22, 22), QSize (16, 16),
        ":/refresh_22px.png", ":/refresh_16px.png",
        ":/refresh_disabled_22px.png", ":/refresh_disabled_16px.png"));

    // mActionsMenu->addAction (mEditAction);
    mActionsContextMenu->addAction (mRemoveAction);
    mActionsContextMenu->addAction (mReleaseAction);

    /* Toolbar composing */
    mActionsToolBar = new VBoxToolBar (this);
    mActionsToolBar->setIconSize (QSize (22, 22));
    mActionsToolBar->setToolButtonStyle (Qt::ToolButtonTextUnderIcon);
    mActionsToolBar->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);

    /* Really messy what the uic produce here: a vbox layout in an hbox layout */
    QHBoxLayout *centralLayout = qobject_cast<QHBoxLayout*> (centralWidget()->layout());
    Assert (VALID_PTR (centralLayout));
    QVBoxLayout *mainLayout = static_cast<QVBoxLayout*> (centralLayout->itemAt(0));
    Assert (VALID_PTR (mainLayout));
#if MAC_LEOPARD_STYLE
    /* Enable unified toolbars on Mac OS X. Available on Qt >= 4.3 */
    addToolBar (mActionsToolBar);
    mActionsToolBar->setMacToolbar();
    /* No spacing/margin on the mac */
    VBoxGlobal::setLayoutMargin (centralLayout, 0);
    mainLayout->insertSpacing (0, 10);
    VBoxGlobal::setLayoutMargin (mainLayout, 0);
#else /* MAC_LEOPARD_STYLE */
    /* Add the toolbar */
    mainLayout->insertWidget (0, mActionsToolBar);
    /* Set spacing/margin like in the selector window */
    centralLayout->setSpacing (0);
    VBoxGlobal::setLayoutMargin (centralLayout, 0);
    mainLayout->setSpacing (5);
    VBoxGlobal::setLayoutMargin (mainLayout, 5);
#endif /* MAC_LEOPARD_STYLE */

    mActionsToolBar->addAction (mNewAction);
    mActionsToolBar->addAction (mAddAction);
    mActionsToolBar->addSeparator();
    // mActionsToolBar->addAction (mEditAction);
    mActionsToolBar->addAction (mRemoveAction);
    mActionsToolBar->addAction (mReleaseAction);
    mActionsToolBar->addSeparator();
    mActionsToolBar->addAction (mRefreshAction);

    /* Menu bar */
    mActionsMenu = menuBar()->addMenu (QString::null);
    mActionsMenu->addAction (mNewAction);
    mActionsMenu->addAction (mAddAction);
    mActionsMenu->addSeparator();
    // mActionsMenu->addAction (mEditAction);
    mActionsMenu->addAction (mRemoveAction);
    mActionsMenu->addAction (mReleaseAction);
    mActionsMenu->addSeparator();
    mActionsMenu->addAction (mRefreshAction);

    /* Setup information pane */
    QList<QILabel*> paneList = findChildren<QILabel*>();
    foreach (QILabel *infoPane, paneList)
        infoPane->setFullSizeSelection (true);

    /* Enumeration progressbar creation */
    mProgressBar = new VBoxProgressBar (this);
    /* Add to the dialog button box */
    mButtonBox->addExtraWidget (mProgressBar);
    /* Default is invisible */
    mProgressBar->setVisible (false);

    /* Set the default button */
    mButtonBox->button (QDialogButtonBox::Ok)->setDefault (true);

    /* Connects for the button box */
    connect (mButtonBox, SIGNAL (accepted()),
             this, SLOT (accept()));
    connect (mButtonBox, SIGNAL (rejected()),
             this, SLOT (reject()));
    connect (mButtonBox, SIGNAL (helpRequested()),
             &vboxProblem(), SLOT (showHelpHelpDialog()));
}

void VBoxDiskImageManagerDlg::setup (int aType, bool aDoSelect,
                                     const QUuid &aTargetVMId /* = QUuid() */,
                                     bool aRefresh /* = true */,
                                     CMachine aMachine /* = CMachine() */,
                                     const QUuid &aHdId,
                                     const QUuid &aCdId,
                                     const QUuid &aFdId)
{
    mMachine = aMachine;
    mHdSelectedId = aHdId;
    mCdSelectedId = aCdId;
    mFdSelectedId = aFdId;

    mType = aType;
    mTwImages->setTabEnabled (HDTab, mType & VBoxDefs::HD);
    mTwImages->setTabEnabled (CDTab, mType & VBoxDefs::CD);
    mTwImages->setTabEnabled (FDTab, mType & VBoxDefs::FD);

    mDoSelect = aDoSelect;
    if (!aTargetVMId.isNull())
        mTargetVMId = aTargetVMId;

    mButtonBox->button (QDialogButtonBox::Cancel)->setVisible (mDoSelect);

    /* Listen to "media enumeration started" signals */
    connect (&vboxGlobal(), SIGNAL (mediaEnumStarted()),
             this, SLOT (mediaEnumStarted()));
    /* Listen to "media enumeration" signals */
    connect (&vboxGlobal(), SIGNAL (mediaEnumerated (const VBoxMedia &, int)),
             this, SLOT (mediaEnumerated (const VBoxMedia &, int)));
    /* Listen to "media enumeration finished" signals */
    connect (&vboxGlobal(), SIGNAL (mediaEnumFinished (const VBoxMediaList &)),
             this, SLOT (mediaEnumFinished (const VBoxMediaList &)));

    /* Listen to "media add" signals */
    connect (&vboxGlobal(), SIGNAL (mediaAdded (const VBoxMedia &)),
             this, SLOT (mediaAdded (const VBoxMedia &)));
    /* Listen to "media update" signals */
    connect (&vboxGlobal(), SIGNAL (mediaUpdated (const VBoxMedia &)),
             this, SLOT (mediaUpdated (const VBoxMedia &)));
    /* Listen to "media remove" signals */
    connect (&vboxGlobal(), SIGNAL (mediaRemoved (VBoxDefs::DiskType, const QUuid &)),
             this, SLOT (mediaRemoved (VBoxDefs::DiskType, const QUuid &)));

    if (aRefresh && !vboxGlobal().isMediaEnumerationStarted())
        vboxGlobal().startEnumeratingMedia();
    else
    {
        /* Insert already enumerated media */
        const VBoxMediaList &list = vboxGlobal().currentMediaList();
        prepareToRefresh (list.size());
        VBoxMediaList::const_iterator it;
        int index = 0;
        for (it = list.begin(); it != list.end(); ++ it)
        {
            mediaAdded (*it);
            if ((*it).status != VBoxMedia::Unknown)
            {
                mProgressBar->setValue (++ index);
                qApp->processEvents();
            }
        }

        /* Emulate the finished signal to reuse the code */
        if (!vboxGlobal().isMediaEnumerationStarted())
            mediaEnumFinished (list);
    }

    /* For a newly opened dialog, select the first item */
    if (mHdsTree->selectedItems().isEmpty())
        if (QTreeWidgetItem *item = mHdsTree->topLevelItem (0))
            setCurrentItem (mHdsTree, item);
    if (mCdsTree->selectedItems().isEmpty())
        if (QTreeWidgetItem *item = mCdsTree->topLevelItem (0))
            setCurrentItem (mCdsTree, item);
    if (mFdsTree->selectedItems().isEmpty())
        if (QTreeWidgetItem *item = mFdsTree->topLevelItem (0))
            setCurrentItem (mFdsTree, item);

    /* Applying language settings */
    retranslateUi();
}

/* static */
void VBoxDiskImageManagerDlg::showModeless (QWidget *aCenterWidget /* = NULL */,
                                            bool aRefresh /* = true */)
{
    if (!mModelessDialog)
    {
#ifdef Q_WS_MAC
        mModelessDialog = new VBoxDiskImageManagerDlg (aCenterWidget, Qt::Window);
#else /* Q_WS_MAC */
        mModelessDialog = new VBoxDiskImageManagerDlg (0, Qt::Window);
#endif /* Q_WS_MAC */
        mModelessDialog->centerAccording (aCenterWidget);
        connect (aCenterWidget, SIGNAL (closing()), mModelessDialog, SLOT (close()));
        mModelessDialog->setAttribute (Qt::WA_DeleteOnClose);
        mModelessDialog->setup (VBoxDefs::HD | VBoxDefs::CD | VBoxDefs::FD,
                                false, QUuid(), aRefresh);

        /* listen to events that may change the media status and refresh
         * the contents of the modeless dialog */
        /// @todo refreshAll() may be slow, so it may be better to analyze
        //  event details and update only what is changed */
        connect (&vboxGlobal(), SIGNAL (machineDataChanged (const VBoxMachineDataChangeEvent &)),
                 mModelessDialog, SLOT (refreshAll()));
        connect (&vboxGlobal(), SIGNAL (machineRegistered (const VBoxMachineRegisteredEvent &)),
                 mModelessDialog, SLOT (refreshAll()));
        connect (&vboxGlobal(), SIGNAL (snapshotChanged (const VBoxSnapshotEvent &)),
                 mModelessDialog, SLOT (refreshAll()));
    }

    mModelessDialog->show();
    mModelessDialog->setWindowState (mModelessDialog->windowState() &
                                     ~Qt::WindowMinimized);
    mModelessDialog->activateWindow();
}

QUuid VBoxDiskImageManagerDlg::selectedUuid() const
{
    QTreeWidget *tree = currentTreeWidget();
    QUuid uuid;

    DiskImageItem *item = toDiskImageItem (selectedItem (tree));
    if (item)
        uuid = item->uuid();

    return uuid;
}

QString VBoxDiskImageManagerDlg::selectedPath() const
{
    QTreeWidget *tree = currentTreeWidget();
    QString path;

    DiskImageItem *item = toDiskImageItem (selectedItem (tree));
    if (item)
        path = item->path().trimmed();

    return path;
}

/* static */
QString VBoxDiskImageManagerDlg::composeHdToolTip (CHardDisk &aHd,
                                                   VBoxMedia::Status aStatus,
                                                   DiskImageItem *aItem)
{
    CVirtualBox vbox = vboxGlobal().virtualBox();

    QUuid machineId = aItem ? aItem->machineId() : aHd.GetMachineId();

    QString src = aItem ? aItem->path() : aHd.GetLocation();
    QString location = aItem || aHd.GetStorageType() == KHardDiskStorageType_ISCSIHardDisk ? src :
        QDir::convertSeparators (QFileInfo (src).absoluteFilePath());

    QString storageType = aItem ? aItem->storageType() :
        vboxGlobal().toString (aHd.GetStorageType());
    QString hardDiskType = aItem ? aItem->diskType() :
        vboxGlobal().hardDiskTypeString (aHd);

    QString usage;
    if (aItem)
        usage = aItem->usage();
    else if (!machineId.isNull())
        usage = vbox.GetMachine (machineId).GetName();

    QUuid snapshotId = aItem ? aItem->uuid() : aHd.GetSnapshotId();
    QString snapshotName;
    if (aItem)
        snapshotName = aItem->snapshotName();
    else if (!machineId.isNull() && !snapshotId.isNull())
    {
        CSnapshot snapshot = vbox.GetMachine (machineId).
                                  GetSnapshot (aHd.GetSnapshotId());
        if (!snapshot.isNull())
            snapshotName = snapshot.GetName();
    }

    /* Compose tool-tip information */
    QString tip;
    switch (aStatus)
    {
        case VBoxMedia::Unknown:
        {
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Checking accessibility...", "HDD")
                      .arg (location);
            break;
        }
        case VBoxMedia::Ok:
        {
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "<nobr>Disk type:&nbsp;&nbsp;%2</nobr><br>"
                      "<nobr>Storage type:&nbsp;&nbsp;%3</nobr>")
                      .arg (location)
                      .arg (hardDiskType)
                      .arg (storageType);

            if (!usage.isNull())
                tip += tr ("<br><nobr>Attached to:&nbsp;&nbsp;%1</nobr>", "HDD")
                           .arg (usage);
            if (!snapshotName.isNull())
                tip += tr ("<br><nobr>Snapshot:&nbsp;&nbsp;%5</nobr>", "HDD")
                           .arg (snapshotName);
            break;
        }
        case VBoxMedia::Error:
        {
            /// @todo (r=dmik) pass a complete VBoxMedia instance here
            //  to get the result of blabla.GetAccessible() call from CUnknown
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Error checking media accessibility", "HDD")
                      .arg (location);
            break;
        }
        case VBoxMedia::Inaccessible:
        {
            tip = tr ("<nobr><b>%1</b></nobr><br>%2", "HDD")
                      .arg (location)
                      .arg (VBoxGlobal::highlight (aHd.GetLastAccessError(),
                                                   true /* aToolTip */));
            break;
        }
        default:
            AssertFailed();
    }
    return tip;
}

/* static */
QString VBoxDiskImageManagerDlg::composeCdToolTip (CDVDImage &aCd,
                                                   VBoxMedia::Status aStatus,
                                                   DiskImageItem *aItem)
{
    QString location = aItem ? aItem->path() :
        QDir::convertSeparators (QFileInfo (aCd.GetFilePath()).absoluteFilePath());
    QUuid uuid = aItem ? aItem->uuid() : aCd.GetId();
    QString usage;
    if (aItem)
        usage = aItem->totalUsage();
    else
    {
        QString snapshotUsage;
        usage = DVDImageUsage (uuid, snapshotUsage);
        /* Should correlate with DiskImageItem::getTotalUsage() */
        if (!snapshotUsage.isNull())
            usage = QString ("%1 (%2)").arg (usage, snapshotUsage);
    }

    /* Compose tool-tip information */
    QString tip;
    switch (aStatus)
    {
        case VBoxMedia::Unknown:
        {
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Checking accessibility...", "CD/DVD/Floppy")
                      .arg (location);
            break;
        }
        case VBoxMedia::Ok:
        {
            tip = tr ("<nobr><b>%1</b></nobr>", "CD/DVD/Floppy")
                      .arg (location);

            if (!usage.isNull())
                tip += tr ("<br><nobr>Attached to:&nbsp;&nbsp;%1</nobr>",
                           "CD/DVD/Floppy")
                           .arg (usage);
            break;
        }
        case VBoxMedia::Error:
        {
            /// @todo (r=dmik) pass a complete VBoxMedia instance here
            //  to get the result of blabla.GetAccessible() call from CUnknown
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Error checking media accessibility", "CD/DVD/Floppy")
                      .arg (location);
            break;
        }
        case VBoxMedia::Inaccessible:
        {
            /// @todo (r=dmik) correct this when GetLastAccessError() is
            //  implemented for IDVDImage
            tip = tr ("<nobr><b>%1</b></nobr><br>%2")
                      .arg (location)
                      .arg (tr ("The image file is not accessible",
                                "CD/DVD/Floppy"));
            break;
        }
        default:
            AssertFailed();
    }
    return tip;
}

/* static */
QString VBoxDiskImageManagerDlg::composeFdToolTip (CFloppyImage &aFd,
                                                   VBoxMedia::Status aStatus,
                                                   DiskImageItem *aItem)
{
    QString location = aItem ? aItem->path() :
        QDir::convertSeparators (QFileInfo (aFd.GetFilePath()).absoluteFilePath());
    QUuid uuid = aItem ? aItem->uuid() : aFd.GetId();
    QString usage;
    if (aItem)
        usage = aItem->totalUsage();
    else
    {
        QString snapshotUsage;
        usage = FloppyImageUsage (uuid, snapshotUsage);
        /* Should correlate with DiskImageItem::getTotalUsage() */
        if (!snapshotUsage.isNull())
            usage = QString ("%1 (%2)").arg (usage, snapshotUsage);
    }

    /* Compose tool-tip information */
    QString tip;
    switch (aStatus)
    {
        case VBoxMedia::Unknown:
        {
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Checking accessibility...", "CD/DVD/Floppy")
                      .arg (location);
            break;
        }
        case VBoxMedia::Ok:
        {
            tip = tr ("<nobr><b>%1</b></nobr>", "CD/DVD/Floppy")
                      .arg (location);

            if (!usage.isNull())
                tip += tr ("<br><nobr>Attached to:&nbsp;&nbsp;%1</nobr>",
                           "CD/DVD/Floppy")
                           .arg (usage);
            break;
        }
        case VBoxMedia::Error:
        {
            /// @todo (r=dmik) pass a complete VBoxMedia instance here
            //  to get the result of blabla.GetAccessible() call from CUnknown
            tip = tr ("<nobr><b>%1</b></nobr><br>"
                      "Error checking media accessibility", "CD/DVD/Floppy")
                      .arg (location);
            break;
        }
        case VBoxMedia::Inaccessible:
        {
            /// @todo (r=dmik) correct this when GetLastAccessError() is
            //  implemented for IDVDImage
            tip = tr ("<nobr><b>%1</b></nobr><br>%2")
                      .arg (location)
                      .arg (tr ("The image file is not accessible",
                                "CD/DVD/Floppy"));
            break;
        }
        default:
            AssertFailed();
    }
    return tip;
}

void VBoxDiskImageManagerDlg::refreshAll()
{
    /* Start enumerating media */
    vboxGlobal().startEnumeratingMedia();
}

void VBoxDiskImageManagerDlg::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxDiskImageManagerDlg::retranslateUi (this);

    mActionsMenu->setTitle (tr ("&Actions"));

    mNewAction->setText (tr ("&New..."));
    mAddAction->setText (tr ("&Add..."));
    // mEditAction->setText (tr ("&Edit..."));
    mRemoveAction->setText (tr ("R&emove"));
    mReleaseAction->setText (tr ("Re&lease"));
    mRefreshAction->setText (tr ("Re&fresh"));

    mNewAction->setShortcut (QKeySequence ("Ctrl+N"));
    mAddAction->setShortcut (QKeySequence ("Ctrl+A"));
    // mEditAction->setShortcut (QKeySequence ("Ctrl+E"));
    mRemoveAction->setShortcut (QKeySequence ("Ctrl+D"));
    mReleaseAction->setShortcut (QKeySequence ("Ctrl+L"));
    mRefreshAction->setShortcut (QKeySequence ("Ctrl+R"));

    mNewAction->setStatusTip (tr ("Create a new virtual hard disk"));
    mAddAction->setStatusTip (tr ("Add (register) an existing image file"));
    // mEditAction->setStatusTip (tr ("Edit the properties of the selected item"));
    mRemoveAction->setStatusTip (tr ("Remove (unregister) the selected media"));
    mReleaseAction->setStatusTip (tr ("Release the selected media by detaching it from the machine"));
    mRefreshAction->setStatusTip (tr ("Refresh the media list"));

    mNewAction->setToolTip (mNewAction->text().remove ('&') +
        QString (" (%1)").arg (mNewAction->shortcut().toString()));
    mAddAction->setToolTip (mAddAction->text().remove ('&') +
        QString (" (%1)").arg (mAddAction->shortcut().toString()));
    // mEditAction->setToolTip (mEditAction->text().remove ('&') +
    //     QString (" (%1)").arg (mEditAction->shortcut().toString()));
    mRemoveAction->setToolTip (mRemoveAction->text().remove ('&') +
        QString (" (%1)").arg (mRemoveAction->shortcut().toString()));
    mReleaseAction->setToolTip (mReleaseAction->text().remove ('&') +
        QString (" (%1)").arg (mReleaseAction->shortcut().toString()));
    mRefreshAction->setToolTip (mRefreshAction->text().remove ('&') +
        QString (" (%1)").arg (mRefreshAction->shortcut().toString()));

    mProgressBar->setText (tr ("Checking accessibility"));
#ifdef Q_WS_MAC
    /* Make sure that the widgets aren't jumping around while the progress bar
     * get visible. */
    mProgressBar->adjustSize();
    int h = mProgressBar->height();
    mButtonBox->setMinimumHeight (h + 12);
#endif

    if (mDoSelect)
        mButtonBox->button (QDialogButtonBox::Ok)->setText (tr ("&Select"));

    if (mHdsTree->model()->rowCount() || mCdsTree->model()->rowCount() || mFdsTree->model()->rowCount())
        refreshAll();
}

void VBoxDiskImageManagerDlg::closeEvent (QCloseEvent *aEvent)
{
    mModelessDialog = 0;
    aEvent->accept();
}

bool VBoxDiskImageManagerDlg::eventFilter (QObject *aObject, QEvent *aEvent)
{
    switch (aEvent->type())
    {
        case QEvent::DragEnter:
        {
            QDragEnterEvent *deEvent = static_cast<QDragEnterEvent*> (aEvent);
            if (deEvent->mimeData()->hasUrls())
            {
                QList<QUrl> urls = deEvent->mimeData()->urls();
                /* Sometimes urls has an empty Url entry. Filter them out. */
                urls.removeAll (QUrl());
                if (checkDndUrls (urls))
                {
                    deEvent->setDropAction (Qt::LinkAction);
                    deEvent->acceptProposedAction();
                }
            }
            return true;
            break;
        }
        case QEvent::Drop:
        {
            QDropEvent *dEvent = static_cast<QDropEvent*> (aEvent);
            if (dEvent->mimeData()->hasUrls())
            {
                QList<QUrl> urls = dEvent->mimeData()->urls();
                /* Sometimes urls has an empty Url entry. Filter them out. */
                urls.removeAll (QUrl());
                AddVDMUrlsEvent *event = new AddVDMUrlsEvent (urls);
                QApplication::postEvent (currentTreeWidget(), event);
                dEvent->acceptProposedAction();
            }
            return true;
            break;
        }
        case VBoxDefs::AddVDMUrlsEventType:
        {
            if (aObject == currentTreeWidget())
            {
                AddVDMUrlsEvent *addEvent = static_cast<AddVDMUrlsEvent*> (aEvent);
                addDndUrls (addEvent->urls());
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QIMainDialog::eventFilter (aObject, aEvent);
}

void VBoxDiskImageManagerDlg::mediaAdded (const VBoxMedia &aMedia)
{
    /* Ignore non-interesting aMedia */
    if (!(mType & aMedia.type))
        return;

    DiskImageItem *item = 0;
    switch (aMedia.type)
    {
        case VBoxDefs::HD:
            item = createHdItem (mHdsTree, aMedia);
            if (item->uuid() == mHdSelectedId)
            {
                setCurrentItem (mHdsTree, item);
                mHdSelectedId = QUuid();
            }
            break;
        case VBoxDefs::CD:
            item = createCdItem (mCdsTree, aMedia);
            if (item->uuid() == mCdSelectedId)
            {
                setCurrentItem (mCdsTree, item);
                mCdSelectedId = QUuid();
            }
            break;
        case VBoxDefs::FD:
            item = createFdItem (mFdsTree, aMedia);
            if (item->uuid() == mFdSelectedId)
            {
                setCurrentItem (mFdsTree, item);
                mFdSelectedId = QUuid();
            }
            break;
        default:
            AssertMsgFailed (("Invalid aMedia type\n"));
    }

    if (!item)
        return;

    if (!vboxGlobal().isMediaEnumerationStarted())
        setCurrentItem (treeWidget (aMedia.type), item);
    if (item == currentTreeWidget()->currentItem())
        processCurrentChanged (item);
}

void VBoxDiskImageManagerDlg::mediaUpdated (const VBoxMedia &aMedia)
{
    /* Ignore non-interesting aMedia */
    if (!(mType & aMedia.type))
        return;

    DiskImageItem *item = 0;
    switch (aMedia.type)
    {
        case VBoxDefs::HD:
        {
            CHardDisk hd = aMedia.disk;
            item = searchItem (mHdsTree, hd.GetId());
            updateHdItem (item, aMedia);
            break;
        }
        case VBoxDefs::CD:
        {
            CDVDImage cd = aMedia.disk;
            item = searchItem (mCdsTree, cd.GetId());
            updateCdItem (item, aMedia);
            break;
        }
        case VBoxDefs::FD:
        {
            CFloppyImage fd = aMedia.disk;
            item = searchItem (mFdsTree, fd.GetId());
            updateFdItem (item, aMedia);
            break;
        }
        default:
            AssertMsgFailed (("Invalid aMedia type\n"));
    }

    if (!item)
        return;

    /* Note: current items on invisible tabs are not updated because
     * it is always done in processCurrentChanged() when the user switches
     * to an invisible tab */
    if (item == currentTreeWidget()->currentItem())
        processCurrentChanged (item);
}

void VBoxDiskImageManagerDlg::mediaRemoved (VBoxDefs::DiskType aType,
                                            const QUuid &aId)
{
    QTreeWidget *tree = treeWidget (aType);
    DiskImageItem *item = searchItem (tree, aId);
    delete item;
    setCurrentItem (tree, tree->currentItem());
    /* Search the list for inaccessible media */
    if (!searchItem (tree, VBoxMedia::Inaccessible) &&
        !searchItem (tree, VBoxMedia::Error))
    {
        int index = aType == VBoxDefs::HD ? HDTab :
                    aType == VBoxDefs::CD ? CDTab :
                    aType == VBoxDefs::FD ? FDTab : -1;
        const QIcon &set = aType == VBoxDefs::HD ? mIconHD :
                           aType == VBoxDefs::CD ? mIconCD :
                           aType == VBoxDefs::FD ? mIconFD : QIcon();
        Assert (index != -1 && !set.isNull()); /* aType should be the correct one */
        mTwImages->setTabIcon (index, set);
    }
}

void VBoxDiskImageManagerDlg::mediaEnumStarted()
{
    /* Load default tab icons */
    mTwImages->setTabIcon (HDTab, mIconHD);
    mTwImages->setTabIcon (CDTab, mIconCD);
    mTwImages->setTabIcon (FDTab, mIconFD);

    /* Load current media list */
    const VBoxMediaList &list = vboxGlobal().currentMediaList();
    prepareToRefresh (list.size());
    VBoxMediaList::const_iterator it;
    for (it = list.begin(); it != list.end(); ++ it)
        mediaAdded (*it);

    /* Select the first item if the previous saved item is not found or no
     * current item at all */
    if (!mHdsTree->currentItem() || !mHdSelectedId.isNull())
        if (QTreeWidgetItem *item = mHdsTree->topLevelItem (0))
            setCurrentItem (mHdsTree, item);
    if (!mCdsTree->currentItem() || !mCdSelectedId.isNull())
        if (QTreeWidgetItem *item = mCdsTree->topLevelItem (0))
            setCurrentItem (mCdsTree, item);
    if (!mFdsTree->currentItem() || !mFdSelectedId.isNull())
        if (QTreeWidgetItem *item = mFdsTree->topLevelItem (0))
            setCurrentItem (mFdsTree, item);

    processCurrentChanged();
}

void VBoxDiskImageManagerDlg::mediaEnumerated (const VBoxMedia &aMedia,
                                               int aIndex)
{
    mediaUpdated (aMedia);
    Assert (aMedia.status != VBoxMedia::Unknown);
    if (aMedia.status != VBoxMedia::Unknown)
        mProgressBar->setValue (aIndex + 1);
}

void VBoxDiskImageManagerDlg::mediaEnumFinished (const VBoxMediaList &/* aList */)
{
    mProgressBar->setVisible (false);

    mRefreshAction->setEnabled (true);
    unsetCursor();

    processCurrentChanged();
}

void VBoxDiskImageManagerDlg::newImage()
{
    AssertReturnVoid (currentTreeWidgetType() == VBoxDefs::HD);

    VBoxNewHDWzd dlg (this);

    if (dlg.exec() == QDialog::Accepted)
    {
        CHardDisk hd = dlg.hardDisk();
        VBoxMedia::Status status =
            hd.GetAccessible() ? VBoxMedia::Ok :
            hd.isOk() ? VBoxMedia::Inaccessible :
            VBoxMedia::Error;
        vboxGlobal().addMedia (VBoxMedia (CUnknown (hd), VBoxDefs::HD, status));
    }
}

void VBoxDiskImageManagerDlg::addImage()
{
    QTreeWidget *tree = currentTreeWidget();
    DiskImageItem *item = toDiskImageItem (tree->currentItem());

    QString title;
    QString filter;
    VBoxDefs::DiskType type = currentTreeWidgetType();

    QString dir;
    if (item && item->status() == VBoxMedia::Ok)
        dir = QFileInfo (item->path().trimmed()).absolutePath ();

    if (dir.isEmpty())
        if (type == VBoxDefs::HD)
            dir = mVBox.GetSystemProperties().GetDefaultVDIFolder();

    if (dir.isEmpty() || !QFileInfo (dir).exists())
        dir = mVBox.GetHomeFolder();

    switch (type)
    {
        case VBoxDefs::HD:
        {
            QList < QPair<QString, QString> > filterList = vboxGlobal().HDDBackends();
            QStringList backends;
            QStringList allPrefix;
            for (int i = 0; i < filterList.count(); ++i)
            {
                QPair <QString, QString> item = filterList.at (i);
                /* Create one backend filter string */
                backends << QString ("%1 (%2)").arg (item.first). arg (item.second);
                /* Save the suffix's for the "All" entry */
                allPrefix << item.second;
            }
            if (!allPrefix.isEmpty())
                backends.insert (0, tr ("All hard disk images (%1)").arg (allPrefix.join (" ").trimmed()));
            backends << tr ("All files (*)");
            filter = backends.join (";;").trimmed();

            title = tr ("Select a hard disk image file");
            break;
        }
        case VBoxDefs::CD:
        {
            filter = tr ("CD/DVD-ROM images (*.iso);;"
                         "All files (*)");
            title = tr ("Select a CD/DVD-ROM disk image file");
            break;
        }
        case VBoxDefs::FD:
        {
            filter = tr ("Floppy images (*.img);;"
                         "All files (*)");
            title = tr ("Select a floppy disk image file");
            break;
        }
        default:
            AssertMsgFailed (("Selected tree should be equal to one item in VBoxDefs::DiskType.\n"));
            break;
    }

    QString src = VBoxGlobal::getOpenFileName (dir, filter, this, title);
    src =  QDir::convertSeparators (src);

    addImageToList (src, type);
    if (!mVBox.isOk())
        vboxProblem().cannotRegisterMedia (this, mVBox, type, src);
}

void VBoxDiskImageManagerDlg::removeImage()
{
    QTreeWidget *tree = currentTreeWidget();
    DiskImageItem *item = toDiskImageItem (tree->currentItem());
    AssertMsg (item, ("Current item must not be null\n"));

    QUuid uuid = item->uuid();
    AssertMsg (!uuid.isNull(), ("Current item must have uuid\n"));

    QString src = item->path().trimmed();
    VBoxDefs::DiskType type = currentTreeWidgetType();

    switch (type)
    {
        case VBoxDefs::HD:
        {
            bool deleteImage = false;

            /// @todo When creation of VMDK is implemented, we should
            /// enable image deletion for  them as well (use
            /// GetStorageType() to define the correct cast).
            CHardDisk disk = item->media().disk;
            if (disk.GetStorageType() == KHardDiskStorageType_VirtualDiskImage &&
                disk.GetParent().isNull() && /* must not be differencing (see below) */
                item->status() == VBoxMedia::Ok)
            {
                int rc = vboxProblem().confirmHardDiskImageDeletion (this, src);
                if (rc == QIMessageBox::Cancel)
                    return;
                deleteImage = rc == QIMessageBox::Yes;
            }
            else
            {
                /// @todo note that differencing images are always automatically
                /// deleted when unregistered, but the following message box
                /// doesn't mention it. I keep it as is for now because
                /// implementing the portability feature will most likely change
                /// this behavior (we'll update the message afterwards).
                if (!vboxProblem().confirmHardDiskUnregister (this, src))
                    return;
            }

            CHardDisk hd = mVBox.UnregisterHardDisk (uuid);
            if (!mVBox.isOk())
                vboxProblem().cannotUnregisterMedia (this, mVBox, type, src);
            else if (deleteImage)
            {
                /// @todo When creation of VMDK is implemented, we should
                /// enable image deletion for  them as well (use
                /// GetStorageType() to define the correct cast).
                CVirtualDiskImage vdi = CUnknown (hd);
                if (vdi.isOk())
                    vdi.DeleteImage();
                if (!vdi.isOk())
                    vboxProblem().cannotDeleteHardDiskImage (this, vdi);
            }
            break;
        }
        case VBoxDefs::CD:
            mVBox.UnregisterDVDImage (uuid);
            break;
        case VBoxDefs::FD:
            mVBox.UnregisterFloppyImage (uuid);
            break;
        default:
            AssertMsgFailed (("Selected tree should be equal to one item in VBoxDefs::DiskType.\n"));
            break;
    }

    if (mVBox.isOk())
        vboxGlobal().removeMedia (type, uuid);
    else
        vboxProblem().cannotUnregisterMedia (this, mVBox, type, src);
}

void VBoxDiskImageManagerDlg::releaseImage()
{
    QTreeWidget *tree = currentTreeWidget();
    DiskImageItem *item = toDiskImageItem (tree->currentItem());
    AssertMsg (item, ("Current item must not be null\n"));

    QUuid itemId = item->uuid();
    AssertMsg (!itemId.isNull(), ("Current item must have uuid\n"));

    switch (currentTreeWidgetType())
    {
        /* If it is a hard disk sub-item: */
        case VBoxDefs::HD:
        {
            CHardDisk hd = item->media().disk;
            QUuid machineId = hd.GetMachineId();
            if (vboxProblem().confirmReleaseImage (this,
                mVBox.GetMachine (machineId).GetName()))
            {
                releaseDisk (machineId, itemId, VBoxDefs::HD);
                VBoxMedia media (item->media());
                media.status = hd.GetAccessible() ? VBoxMedia::Ok :
                    hd.isOk() ? VBoxMedia::Inaccessible :
                    VBoxMedia::Error;
                vboxGlobal().updateMedia (media);
            }
            break;
        }
        /* If it is a cd/dvd sub-item: */
        case VBoxDefs::CD:
        {
            QString usage = item->totalUsage();
            if (vboxProblem().confirmReleaseImage (this, usage))
            {
                QStringList permMachines =
                    mVBox.GetDVDImageUsage (itemId,
                                           KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
                for (QStringList::Iterator it = permMachines.begin();
                     it != permMachines.end(); ++it)
                    releaseDisk (QUuid (*it), itemId, VBoxDefs::CD);

                CDVDImage cd = mVBox.GetDVDImage (itemId);
                VBoxMedia media (item->media());
                media.status = cd.GetAccessible() ? VBoxMedia::Ok :
                    cd.isOk() ? VBoxMedia::Inaccessible :
                    VBoxMedia::Error;
                vboxGlobal().updateMedia (media);
            }
            break;
        }
        /* If it is a floppy sub-item: */
        case VBoxDefs::FD:
        {
            QString usage = item->totalUsage();
            if (vboxProblem().confirmReleaseImage (this, usage))
            {
                QStringList permMachines =
                    mVBox.GetFloppyImageUsage (itemId,
                                              KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
                for (QStringList::Iterator it = permMachines.begin();
                     it != permMachines.end(); ++it)
                    releaseDisk (QUuid (*it), itemId, VBoxDefs::FD);

                CFloppyImage fd = mVBox.GetFloppyImage (itemId);
                VBoxMedia media (item->media());
                media.status = fd.GetAccessible() ? VBoxMedia::Ok :
                    fd.isOk() ? VBoxMedia::Inaccessible :
                    VBoxMedia::Error;
                vboxGlobal().updateMedia (media);
            }
            break;
        }
        default:
            AssertMsgFailed (("Selected tree should be equal to one item in VBoxDefs::DiskType.\n"));
            break;
    }
}

void VBoxDiskImageManagerDlg::releaseDisk (const QUuid &aMachineId,
                                           const QUuid &aItemId,
                                           VBoxDefs::DiskType aDiskType)
{
    CSession session;
    CMachine machine;
    /* Is this media image mapped to this VM: */
    if (!mMachine.isNull() && mMachine.GetId() == aMachineId)
        machine = mMachine;
    /* Or some other: */
    else
    {
        session = vboxGlobal().openSession (aMachineId);
        if (session.isNull()) return;
        machine = session.GetMachine();
    }
    /* Perform disk releasing: */
    switch (aDiskType)
    {
        case VBoxDefs::HD:
        {
            /* Releasing hd: */
            CHardDiskAttachmentEnumerator en =
                machine.GetHardDiskAttachments().Enumerate();
            while (en.HasMore())
            {
                CHardDiskAttachment hda = en.GetNext();
                if (hda.GetHardDisk().GetId() == aItemId)
                {
                    machine.DetachHardDisk (hda.GetBus(),
                                            hda.GetChannel(),
                                            hda.GetDevice());
                    if (!machine.isOk())
                        vboxProblem().cannotDetachHardDisk (this,
                            machine, hda.GetBus(), hda.GetChannel(), hda.GetDevice());
                    break;
                }
            }
            break;
        }
        case VBoxDefs::CD:
        {
            /* Releasing cd: */
            machine.GetDVDDrive().Unmount();
            break;
        }
        case VBoxDefs::FD:
        {
            /* Releasing fd: */
            machine.GetFloppyDrive().Unmount();
            break;
        }
        default:
            AssertMsgFailed (("Incorrect disk type."));
    }
    /* Save all setting changes: */
    machine.SaveSettings();
    if (!machine.isOk())
        vboxProblem().cannotSaveMachineSettings (machine);
    /* If local session was opened - close this session: */
    if (!session.isNull())
        session.Close();
}

QTreeWidget* VBoxDiskImageManagerDlg::treeWidget (VBoxDefs::DiskType aType) const
{
    QTreeWidget* tree = 0;
    switch (aType)
    {
        case VBoxDefs::HD:
            tree = mHdsTree;
            break;
        case VBoxDefs::CD:
            tree = mCdsTree;
            break;
        case VBoxDefs::FD:
            tree = mFdsTree;
            break;
        default:
            AssertMsgFailed (("Disk type %d unknown!\n", aType));
            break;
    }
    return tree;
}

VBoxDefs::DiskType VBoxDiskImageManagerDlg::currentTreeWidgetType() const
{
    VBoxDefs::DiskType type = VBoxDefs::InvalidType;
    switch (mTwImages->currentIndex ())
    {
        case HDTab:
            type = VBoxDefs::HD;
            break;
        case CDTab:
            type = VBoxDefs::CD;
            break;
        case FDTab:
            type = VBoxDefs::FD;
            break;
        default:
            AssertMsgFailed (("Page type %d unknown!\n", mTwImages->currentIndex ()));
            break;
    }
    return type;
}

QTreeWidget* VBoxDiskImageManagerDlg::currentTreeWidget() const
{
    return treeWidget (currentTreeWidgetType());
}


QTreeWidgetItem *VBoxDiskImageManagerDlg::selectedItem (const QTreeWidget *aTree) const
{
    /* Return the current selected item. The user can select one item at the
     * time only, so return the first element always. */
    QList<QTreeWidgetItem *> selItems = aTree->selectedItems();
    if (!selItems.isEmpty())
        return selItems.first();
    else
        return 0;
}


DiskImageItem *VBoxDiskImageManagerDlg::toDiskImageItem (QTreeWidgetItem *aItem) const
{
    /* Convert the QTreeWidgetItem to a DiskImageItem if it is valid. */
    DiskImageItem *item = 0;
    if (aItem &&
        aItem->type() == QITreeWidget::BasicItemType)
        item = static_cast <DiskImageItem*> (aItem);

    return item;
}

void VBoxDiskImageManagerDlg::setCurrentItem (QTreeWidget *aTree,
                                              QTreeWidgetItem *aItem)
{
    if (aTree && aItem)
    {
        aItem->setSelected (true);
        aTree->setCurrentItem (aItem);
        aTree->scrollToItem (aItem, QAbstractItemView::EnsureVisible);
    }
}

void VBoxDiskImageManagerDlg::processCurrentChanged (int /* index = -1 */)
{
    QTreeWidget *tree = currentTreeWidget();
    tree->setFocus();
    processCurrentChanged (tree->currentItem());
}

void VBoxDiskImageManagerDlg::processCurrentChanged (QTreeWidgetItem *aItem,
                                                     QTreeWidgetItem *aPrevItem /* = 0 */)
{
    DiskImageItem *item = toDiskImageItem (aItem);

    if (!item && aPrevItem)
    {
        DiskImageItem *itemOld = toDiskImageItem (aPrevItem);
        /* We have to make sure that one item is selected always. If the new
         * item is 0, set the old item again. */
        setCurrentItem (currentTreeWidget(), itemOld);
    }

    if (item)
    {
        /* Set the file for the proxy icon */
        setFileForProxyIcon (item->path());
        /* Ensures current item visible every time we are switching page */
        item->treeWidget()->scrollToItem (item, QAbstractItemView::EnsureVisible);
    }

    bool notInEnum      = !vboxGlobal().isMediaEnumerationStarted();
    /* If we are currently enumerating the media go out of here. We are coming
     * back here from the finished signal. */
    if (!notInEnum)
        return;
    bool modifyEnabled  = notInEnum &&
                          item &&  item->usage().isNull() &&
                          (item->childCount() == 0) && !item->path().isNull();
    bool releaseEnabled = item && !item->usage().isNull() &&
                          item->snapshotUsage().isNull() &&
                          checkImage (item) &&
                          !item->parent() && (item->childCount() == 0) &&
                          item->snapshotName().isNull();
    bool newEnabled     = notInEnum &&
                          currentTreeWidget() == mHdsTree ? true : false;
    bool addEnabled     = notInEnum;

    // mEditAction->setEnabled (modifyEnabled);
    mRemoveAction->setEnabled (modifyEnabled);
    mReleaseAction->setEnabled (releaseEnabled);
    mNewAction->setEnabled (newEnabled);
    mAddAction->setEnabled (addEnabled);

    if (mDoSelect)
    {
        bool selectEnabled = item && !item->parent() &&
                             (!newEnabled ||
                                (item->usage().isNull() ||
                                 item->machineId() == mTargetVMId));

        mButtonBox->button (QDialogButtonBox::Ok)->setEnabled (selectEnabled);
    }

    if (item)
    {
        if (item->treeWidget() == mHdsTree)
        {
            mHdsPane1->setText (item->information (item->path(), true, "end"));
            mHdsPane2->setText (item->information (item->diskType(), false));
            mHdsPane3->setText (item->information (item->storageType(), false));
            mHdsPane4->setText (item->information (item->usage()));
            mHdsPane5->setText (item->information (item->snapshotName()));
        }
        else if (item->treeWidget() == mCdsTree)
        {
            mCdsPane1->setText (item->information (item->path(), true, "end"));
            mCdsPane2->setText (item->information (item->totalUsage()));
        }
        else if (item->treeWidget() == mFdsTree)
        {
            mFdsPane1->setText (item->information (item->path(), true, "end"));
            mFdsPane2->setText (item->information (item->totalUsage()));
        }
    }
    else
        clearInfoPanes();

    mHdsContainer->setEnabled (item);
    mCdsContainer->setEnabled (item);
    mFdsContainer->setEnabled (item);
}

void VBoxDiskImageManagerDlg::processDoubleClick (QTreeWidgetItem * /* aItem */, int /* aColumn */)
{
    QTreeWidget *tree = currentTreeWidget();

    if (mDoSelect && selectedItem (tree) && mButtonBox->button (QDialogButtonBox::Ok)->isEnabled())
        accept();
}

void VBoxDiskImageManagerDlg::showContextMenu (const QPoint &aPos)
{
    QTreeWidget *curWidget = currentTreeWidget();
    QTreeWidgetItem *curItem = curWidget->itemAt (aPos);
    if (curItem)
    {
        /* Make sure the item is selected and current */
        setCurrentItem (curWidget, curItem);
        mActionsContextMenu->exec (curWidget->viewport()->mapToGlobal (aPos));
    }
}

void VBoxDiskImageManagerDlg::addImageToList (const QString &aSource,
                                              VBoxDefs::DiskType aDiskType)
{
    if (aSource.isEmpty())
        return;

    QUuid uuid;
    VBoxMedia media;
    switch (aDiskType)
    {
        case VBoxDefs::HD:
        {
            CHardDisk hd = mVBox.OpenHardDisk (aSource);
            if (mVBox.isOk())
            {
                mVBox.RegisterHardDisk (hd);
                if (mVBox.isOk())
                {
                    VBoxMedia::Status status =
                        hd.GetAccessible() ? VBoxMedia::Ok :
                        hd.isOk() ? VBoxMedia::Inaccessible :
                        VBoxMedia::Error;
                    media = VBoxMedia (CUnknown (hd), VBoxDefs::HD, status);
                }
            }
            break;
        }
        case VBoxDefs::CD:
        {
            CDVDImage cd = mVBox.OpenDVDImage (aSource, uuid);
            if (mVBox.isOk())
            {
                mVBox.RegisterDVDImage (cd);
                if (mVBox.isOk())
                {
                    VBoxMedia::Status status =
                        cd.GetAccessible() ? VBoxMedia::Ok :
                        cd.isOk() ? VBoxMedia::Inaccessible :
                        VBoxMedia::Error;
                    media = VBoxMedia (CUnknown (cd), VBoxDefs::CD, status);
                }
            }
            break;
        }
        case VBoxDefs::FD:
        {
            CFloppyImage fd = mVBox.OpenFloppyImage (aSource, uuid);
            if (mVBox.isOk())
            {
                mVBox.RegisterFloppyImage (fd);
                if (mVBox.isOk())
                {
                    VBoxMedia::Status status =
                        fd.GetAccessible() ? VBoxMedia::Ok :
                        fd.isOk() ? VBoxMedia::Inaccessible :
                        VBoxMedia::Error;
                    media = VBoxMedia (CUnknown (fd), VBoxDefs::FD, status);
                }
            }
            break;
        }
        default:
            AssertMsgFailed (("Invalid aDiskType type\n"));
    }
    if (media.type != VBoxDefs::InvalidType)
        vboxGlobal().addMedia (media);
}

DiskImageItem* VBoxDiskImageManagerDlg::createImageNode (QTreeWidget *aTree,
                                                         DiskImageItem *aRoot,
                                                         const VBoxMedia &aMedia) const
{
    Assert (!(aTree == 0 && aRoot == 0));

    DiskImageItem *item = 0;

    if (aRoot)
        item = new DiskImageItem (aRoot);
    else if (aTree)
        item = new DiskImageItem (aTree);

    item->setMedia (aMedia);

    return item;
}

DiskImageItem* VBoxDiskImageManagerDlg::createHdItem (QTreeWidget *aTree,
                                                      const VBoxMedia &aMedia) const
{
    CHardDisk hd = aMedia.disk;
    QUuid rootId = hd.GetParent().isNull() ? QUuid() : hd.GetParent().GetId();
    DiskImageItem *root = searchItem (aTree, rootId);
    DiskImageItem *item = createImageNode (aTree, root, aMedia);
    updateHdItem (item, aMedia);
    return item;
}

DiskImageItem* VBoxDiskImageManagerDlg::createCdItem (QTreeWidget *aTree,
                                                      const VBoxMedia &aMedia) const
{
    DiskImageItem *item = createImageNode (aTree, 0, aMedia);
    updateCdItem (item, aMedia);
    return item;
}

DiskImageItem* VBoxDiskImageManagerDlg::createFdItem (QTreeWidget *aTree,
                                                      const VBoxMedia &aMedia) const
{
    DiskImageItem *item = createImageNode (aTree, 0, aMedia);
    updateFdItem (item, aMedia);
    return item;
}

void VBoxDiskImageManagerDlg::updateHdItem (DiskImageItem *aItem,
                                            const VBoxMedia &aMedia) const
{
    if (!aItem)
        return;

    CHardDisk hd = aMedia.disk;
    VBoxMedia::Status status = aMedia.status;

    QUuid uuid = hd.GetId();
    QString src = hd.GetLocation();
    QUuid machineId = hd.GetMachineId();
    QString usage;
    if (!machineId.isNull())
        usage = vboxGlobal().virtualBox().GetMachine (machineId).GetName();
    QString storageType = vboxGlobal().toString (hd.GetStorageType());
    QString hardDiskType = vboxGlobal().hardDiskTypeString (hd);
    QString virtualSize = status == VBoxMedia::Ok ?
        vboxGlobal().formatSize ((ULONG64)hd.GetSize() * _1M) : QString ("--");
    QString actualSize = status == VBoxMedia::Ok ?
        vboxGlobal().formatSize (hd.GetActualSize()) : QString ("--");
    QString snapshotName;
    if (!machineId.isNull() && !hd.GetSnapshotId().isNull())
    {
        CSnapshot snapshot = vboxGlobal().virtualBox().GetMachine (machineId).
                                  GetSnapshot (hd.GetSnapshotId());
        if (!snapshot.isNull())
            snapshotName = QString ("%1").arg (snapshot.GetName());
    }
    QFileInfo fi (src);

    aItem->setText (0, fi.fileName());
    aItem->setText (1, virtualSize);
    aItem->setTextAlignment (1, Qt::AlignRight);
    aItem->setText (2, actualSize);
    aItem->setTextAlignment (2, Qt::AlignRight);
    aItem->setPath (hd.GetStorageType() == KHardDiskStorageType_ISCSIHardDisk ? src :
                    QDir::convertSeparators (fi.absoluteFilePath()));
    aItem->setUsage (usage);
    aItem->setSnapshotName (snapshotName);
    aItem->setDiskType (hardDiskType);
    aItem->setStorageType (storageType);
    aItem->setVirtualSize (virtualSize);
    aItem->setActualSize (actualSize);
    aItem->setUuid (uuid);
    aItem->setMachineId (machineId);
    aItem->setToolTip (composeHdToolTip (hd, status, aItem));
    aItem->setStatus (status);

    makeWarningMark (aItem, aMedia.status, VBoxDefs::HD);
}

void VBoxDiskImageManagerDlg::updateCdItem (DiskImageItem *aItem,
                                            const VBoxMedia &aMedia) const
{
    if (!aItem)
        return;

    CDVDImage cd = aMedia.disk;
    VBoxMedia::Status status = aMedia.status;

    QUuid uuid = cd.GetId();
    QString src = cd.GetFilePath();
    QString snapshotUsage;
    QString usage = DVDImageUsage (uuid, snapshotUsage);
    QString size = status == VBoxMedia::Ok ?
        vboxGlobal().formatSize (cd.GetSize()) : QString ("--");
    QFileInfo fi (src);

    aItem->setText (0, fi.fileName());
    aItem->setText (1, size);
    aItem->setTextAlignment (1, Qt::AlignRight);
    aItem->setPath (QDir::convertSeparators (fi.absoluteFilePath ()));
    aItem->setUsage (usage);
    aItem->setSnapshotUsage (snapshotUsage);
    aItem->setActualSize (size);
    aItem->setUuid (uuid);
    aItem->setToolTip (composeCdToolTip (cd, status, aItem));
    aItem->setStatus (status);

    makeWarningMark (aItem, aMedia.status, VBoxDefs::CD);
}

void VBoxDiskImageManagerDlg::updateFdItem (DiskImageItem *aItem,
                                            const VBoxMedia &aMedia) const
{
    if (!aItem)
        return;

    CFloppyImage fd = aMedia.disk;
    VBoxMedia::Status status = aMedia.status;

    QUuid uuid = fd.GetId();
    QString src = fd.GetFilePath();
    QString snapshotUsage;
    QString usage = FloppyImageUsage (uuid, snapshotUsage);
    QString size = status == VBoxMedia::Ok ?
        vboxGlobal().formatSize (fd.GetSize()) : QString ("--");
    QFileInfo fi (src);

    aItem->setText (0, fi.fileName());
    aItem->setText (1, size);
    aItem->setTextAlignment (1, Qt::AlignRight);
    aItem->setPath (QDir::convertSeparators (fi.absoluteFilePath ()));
    aItem->setUsage (usage);
    aItem->setSnapshotUsage (snapshotUsage);
    aItem->setActualSize (size);
    aItem->setUuid (uuid);
    aItem->setToolTip (composeFdToolTip (fd, status, aItem));
    aItem->setStatus (status);

    makeWarningMark (aItem, aMedia.status, VBoxDefs::FD);
}

DiskImageItem* VBoxDiskImageManagerDlg::searchItem (QTreeWidget *aTree,
                                                    const QUuid &aId) const
{
    if (aId.isNull())
        return 0;

    DiskImageItemIterator iterator (aTree);
    while (*iterator)
    {
        if ((*iterator)->uuid() == aId)
            return *iterator;
        ++ iterator;
    }
    return 0;
}

DiskImageItem* VBoxDiskImageManagerDlg::searchItem (QTreeWidget *aTree,
                                                    VBoxMedia::Status aStatus) const
{
    DiskImageItemIterator iterator (aTree);
    while (*iterator)
    {
        if ((*iterator)->status() == aStatus)
            return *iterator;
        ++ iterator;
    }
    return 0;
}

bool VBoxDiskImageManagerDlg::checkImage (DiskImageItem *aItem)
{
    QUuid itemId = aItem ? aItem->uuid() : QUuid();
    if (itemId.isNull())
        return false;

    switch (currentTreeWidgetType())
    {
        case VBoxDefs::HD:
        {
            CHardDisk hd = aItem->media().disk;
            if (!hd.isNull())
            {
                QUuid machineId = hd.GetMachineId();
                if (machineId.isNull() ||
                    (mVBox.GetMachine (machineId).GetState() != KMachineState_PoweredOff &&
                     mVBox.GetMachine (machineId).GetState() != KMachineState_Aborted))
                    return false;
            }else
                return false;
            break;
        }
        case VBoxDefs::CD:
        {
            /* Check if there is temporary usage: */
            QStringList tempMachines =
                mVBox.GetDVDImageUsage (itemId,
                                       KResourceUsage_Temporary).split (' ', QString::SkipEmptyParts);
            if (!tempMachines.isEmpty())
                return false;
            /* Only permanently mounted .iso could be released */
            QStringList permMachines =
                mVBox.GetDVDImageUsage (itemId,
                                       KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
            for (QStringList::Iterator it = permMachines.begin();
                 it != permMachines.end(); ++it)
                if (mVBox.GetMachine(QUuid (*it)).GetState() != KMachineState_PoweredOff &&
                    mVBox.GetMachine(QUuid (*it)).GetState() != KMachineState_Aborted)
                    return false;
            break;
        }
        case VBoxDefs::FD:
        {
            /* Check if there is temporary usage: */
            QStringList tempMachines =
                mVBox.GetFloppyImageUsage (itemId,
                                          KResourceUsage_Temporary).split (' ', QString::SkipEmptyParts);
            if (!tempMachines.isEmpty())
                return false;
            /* Only permanently mounted floppies could be released */
            QStringList permMachines =
                mVBox.GetFloppyImageUsage (itemId,
                                          KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
            for (QStringList::Iterator it = permMachines.begin();
                 it != permMachines.end(); ++it)
                if (mVBox.GetMachine(QUuid (*it)).GetState() != KMachineState_PoweredOff &&
                    mVBox.GetMachine(QUuid (*it)).GetState() != KMachineState_Aborted)
                    return false;
            break;
        }
        default:
        {
            return false;
            break;
        }
    }
    return true;
}

bool VBoxDiskImageManagerDlg::checkDndUrls (const QList<QUrl> &aUrls) const
{
    bool err = false;
    /* Check that all file extensions fit to the current
     * selected tree view and the files are valid. */
    foreach (QUrl u, aUrls)
    {
        QFileInfo fi (u.toLocalFile());
        /* Check dropped media type */
        /// @todo On OS/2 and windows (and mac?) extension checks should be case
        /// insensitive, as OPPOSED to linux and the rest where case matters.
        QString suffix = fi.suffix().toLower();
        switch (currentTreeWidgetType())
        {
            case VBoxDefs::HD:
                err |= (!(suffix == "vdi" || suffix == "vmdk"));
                break;
            case VBoxDefs::CD:
                err |= (suffix != "iso");
                break;
            case VBoxDefs::FD:
                err |= (suffix != "img");
                break;
            default:
                AssertMsgFailed (("Selected tree should be equal to one item in VBoxDefs::DiskType.\n"));
                break;
        }
    }
    return !err;
}

void VBoxDiskImageManagerDlg::addDndUrls (const QList<QUrl> &aUrls)
{
    foreach (QUrl u, aUrls)
    {
        QString file = u.toLocalFile();
        VBoxDefs::DiskType type = currentTreeWidgetType();
        addImageToList (file, type);
        if (!mVBox.isOk())
            vboxProblem().cannotRegisterMedia (this, mVBox, type, file);
    }
}

void VBoxDiskImageManagerDlg::clearInfoPanes()
{
    mHdsPane1->clear();
    mHdsPane2->clear(); mHdsPane3->clear();
    mHdsPane4->clear(); mHdsPane5->clear();
    mCdsPane1->clear(); mCdsPane2->clear();
    mFdsPane1->clear(); mFdsPane2->clear();
}

void VBoxDiskImageManagerDlg::prepareToRefresh (int aTotal)
{
    /* Info panel clearing */
    clearInfoPanes();

    /* Prepare progressbar */
    if (mProgressBar)
    {
        mProgressBar->setMaximum (aTotal);
        mProgressBar->setValue (0);
        mProgressBar->setVisible (true);
    }

    mRefreshAction->setEnabled (false);
    setCursor (QCursor (Qt::BusyCursor));

    /* Store the current list selections */
    DiskImageItem *di;

    di = toDiskImageItem (mHdsTree->currentItem());
    if (mHdSelectedId.isNull())
        mHdSelectedId = di ? di->uuid() : QUuid();

    di = toDiskImageItem (mCdsTree->currentItem());
    if (mCdSelectedId.isNull())
        mCdSelectedId = di ? di->uuid() : QUuid();

    di = toDiskImageItem (mFdsTree->currentItem());
    if (mFdSelectedId.isNull())
        mFdSelectedId = di ? di->uuid() : QUuid();

    /* Finally, clear all lists */
    mHdsTree->clear();
    mCdsTree->clear();
    mFdsTree->clear();
}

void VBoxDiskImageManagerDlg::makeWarningMark (DiskImageItem *aItem,
                                               VBoxMedia::Status aStatus,
                                               VBoxDefs::DiskType aType) const
{
    const QIcon &icon = aStatus == VBoxMedia::Inaccessible ? mIconInaccessible :
                        aStatus == VBoxMedia::Error ? mIconErroneous : QIcon();

    if (!icon.isNull())
    {
        aItem->setIcon (0, icon);
        treeWidget (aType);
        int index = aType == VBoxDefs::HD ? HDTab :
                    aType == VBoxDefs::CD ? CDTab :
                    aType == VBoxDefs::FD ? FDTab : -1;
        Assert (index != -1); /* aType should be correct */
        mTwImages->setTabIcon (index, icon);
        aItem->treeWidget()->scrollToItem (aItem, QAbstractItemView::EnsureVisible);
    }
}

/* static */
QString VBoxDiskImageManagerDlg::DVDImageUsage (const QUuid &aId, QString &aSnapshotUsage)
{
    CVirtualBox vbox = vboxGlobal().virtualBox();

    QStringList permMachines =
        vbox.GetDVDImageUsage (aId, KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
    QStringList tempMachines =
        vbox.GetDVDImageUsage (aId, KResourceUsage_Temporary).split (' ', QString::SkipEmptyParts);

    QString usage;

    for (QStringList::Iterator it = permMachines.begin();
         it != permMachines.end(); ++ it)
    {
        if (!usage.isEmpty())
            usage += ", ";
        CMachine machine = vbox.GetMachine (QUuid (*it));
        usage += machine.GetName();

        DVDImageSnapshotUsage (aId, machine.GetSnapshot (QUuid()),
                               aSnapshotUsage);
    }

    for (QStringList::Iterator it = tempMachines.begin();
         it != tempMachines.end(); ++ it)
    {
        /* Skip IDs that are in the permanent list */
        if (!permMachines.contains (*it))
        {
            if (!usage.isEmpty())
                usage += ", [";
            else
                usage += "[";
            CMachine machine = vbox.GetMachine (QUuid (*it));
            usage += machine.GetName() + "]";

            DVDImageSnapshotUsage (aId, machine.GetSnapshot (QUuid()),
                                   aSnapshotUsage);
        }
    }

    return usage;
}

/* static */
QString VBoxDiskImageManagerDlg::FloppyImageUsage (const QUuid &aId, QString &aSnapshotUsage)
{
    CVirtualBox vbox = vboxGlobal().virtualBox();

    QStringList permMachines =
        vbox.GetFloppyImageUsage (aId, KResourceUsage_Permanent).split (' ', QString::SkipEmptyParts);
    QStringList tempMachines =
        vbox.GetFloppyImageUsage (aId, KResourceUsage_Temporary).split (' ', QString::SkipEmptyParts);

    QString usage;

    for (QStringList::Iterator it = permMachines.begin();
         it != permMachines.end(); ++ it)
    {
        if (!usage.isEmpty())
            usage += ", ";
        CMachine machine = vbox.GetMachine (QUuid (*it));
        usage += machine.GetName();

        FloppyImageSnapshotUsage (aId, machine.GetSnapshot (QUuid()),
                                  aSnapshotUsage);
    }

    for (QStringList::Iterator it = tempMachines.begin();
         it != tempMachines.end(); ++ it)
    {
        /* Skip IDs that are in the permanent list */
        if (!permMachines.contains (*it))
        {
            if (!usage.isEmpty())
                usage += ", [";
            else
                usage += "[";
            CMachine machine = vbox.GetMachine (QUuid (*it));
            usage += machine.GetName() + "]";

            FloppyImageSnapshotUsage (aId, machine.GetSnapshot (QUuid()),
                                      aSnapshotUsage);
        }
    }

    return usage;
}

/* static */
void VBoxDiskImageManagerDlg::DVDImageSnapshotUsage (const QUuid &aId, const CSnapshot &aSnapshot, QString &aUsage)
{
    if (aSnapshot.isNull())
        return;

    if (!aSnapshot.GetMachine().GetDVDDrive().GetImage().isNull() &&
        aSnapshot.GetMachine().GetDVDDrive().GetImage().GetId() == aId)
    {
        if (!aUsage.isEmpty())
            aUsage += ", ";
        aUsage += aSnapshot.GetName();
    }

    CSnapshotEnumerator en = aSnapshot.GetChildren().Enumerate();
    while (en.HasMore())
        DVDImageSnapshotUsage (aId, en.GetNext(), aUsage);
}

/* static */
void VBoxDiskImageManagerDlg::FloppyImageSnapshotUsage (const QUuid &aId, const CSnapshot &aSnapshot, QString &aUsage)
{
    if (aSnapshot.isNull())
        return;

    if (!aSnapshot.GetMachine().GetFloppyDrive().GetImage().isNull() &&
        aSnapshot.GetMachine().GetFloppyDrive().GetImage().GetId() == aId)
    {
        if (!aUsage.isEmpty())
            aUsage += ", ";
        aUsage += aSnapshot.GetName();
    }

    CSnapshotEnumerator en = aSnapshot.GetChildren().Enumerate();
    while (en.HasMore())
        FloppyImageSnapshotUsage (aId, en.GetNext(), aUsage);
}

#include "VBoxDiskImageManagerDlg.moc"

