/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxSelectorWnd class implementation
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

#include "VBoxProblemReporter.h"
#include "VBoxSelectorWnd.h"
#include "VBoxVMListView.h"
#include "VBoxConsoleWnd.h"
#include "VBoxToolBar.h"

#include "VBoxSnapshotsWgt.h"
#include "VBoxNewVMWzd.h"
#include "VBoxMediaManagerDlg.h"
#include "VBoxSettingsDialogSpecific.h"
#include "VBoxVMLogViewer.h"
#include "VBoxGlobal.h"
#include "VBoxUtils.h"

#ifdef Q_WS_X11
#include <iprt/env.h>
#endif

/* Qt includes */
#include <QTextBrowser>
#include <QMenuBar>
#include <QMenu>
#include <QMenuItem>
#include <QStackedWidget>
#include <QDesktopWidget>
#include <QToolButton>

// VBoxVMDetailsView class
////////////////////////////////////////////////////////////////////////////////

/**
 *  Two-page widget stack to represent VM details: one page for normal details
 *  and another one for inaccessibility errors.
 */
class VBoxVMDetailsView : public QIWithRetranslateUI<QStackedWidget>
{
    Q_OBJECT;

public:

    VBoxVMDetailsView (QWidget *aParent,
                       QAction *aRefreshAction = NULL);

    void setDetailsText (const QString &aText)
    {
        mDetailsText->setText (aText);
        setCurrentIndex (0);
    }

    void setErrorText (const QString &aText)
    {
        createErrPage();
        mErrText->setText (aText);
        setCurrentIndex (1);
    }

    void setEmpty()
    {
        mDetailsText->setText (QString::null);
        setCurrentIndex (0);
    }

signals:

    void linkClicked (const QString &aURL);

protected:

    void retranslateUi();

private slots:

    void gotLinkClicked (const QUrl &aURL)
    {
        emit linkClicked (aURL.toString());
    }

private:

    void createErrPage();

    QRichTextBrowser *mDetailsText;

    QWidget *mErrBox;
    QLabel *mErrLabel;
    QTextBrowser *mErrText;
    QToolButton *mRefreshButton;
    QAction *mRefreshAction;
};

VBoxVMDetailsView::VBoxVMDetailsView (QWidget *aParent,
                                      QAction *aRefreshAction /* = NULL */)
    : QIWithRetranslateUI<QStackedWidget> (aParent)
    , mErrBox (NULL), mErrLabel (NULL), mErrText (NULL)
    , mRefreshButton (NULL)
    , mRefreshAction (aRefreshAction)
{
    Assert (mRefreshAction);

    /* create normal details page */

    mDetailsText = new QRichTextBrowser (mErrBox);
    mDetailsText->setViewportMargins (10, 10, 10, 10);
    mDetailsText->setFocusPolicy (Qt::StrongFocus);
    mDetailsText->document()->setDefaultStyleSheet ("a { text-decoration: none; }");
    /* make "transparent" */
    mDetailsText->setFrameShape (QFrame::NoFrame);
    mDetailsText->viewport()->setAutoFillBackground (false);
    mDetailsText->setOpenLinks (false);

    connect (mDetailsText, SIGNAL (anchorClicked (const QUrl &)),
             this, SLOT (gotLinkClicked (const QUrl &)));

    addWidget (mDetailsText);
}

void VBoxVMDetailsView::createErrPage()
{
    /* create inaccessible details page */

    if (mErrBox)
        return;

    mErrBox = new QWidget();

    QVBoxLayout *vLayout = new QVBoxLayout (mErrBox);
    vLayout->setSpacing (10);

    mErrLabel = new QLabel (mErrBox);
    mErrLabel->setWordWrap (true);
    mErrLabel->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
    vLayout->addWidget (mErrLabel);

    mErrText = new QTextBrowser (mErrBox);
    mErrText->setFocusPolicy (Qt::StrongFocus);
    mErrText->document()->setDefaultStyleSheet ("a { text-decoration: none; }");
    vLayout->addWidget (mErrText);

    if (mRefreshAction)
    {
        mRefreshButton = new QToolButton (mErrBox);
        mRefreshButton->setFocusPolicy (Qt::StrongFocus);

        QHBoxLayout *hLayout = new QHBoxLayout ();
        vLayout->addLayout (hLayout);
        hLayout->addItem (new QSpacerItem (0, 0, QSizePolicy::Expanding,
                                                 QSizePolicy::Minimum));
        hLayout->addWidget (mRefreshButton);

        connect (mRefreshButton, SIGNAL (clicked()),
                 mRefreshAction, SIGNAL (triggered()));
    }

    vLayout->addItem (new QSpacerItem (0, 0, QSizePolicy::Minimum,
                                             QSizePolicy::Expanding));

    addWidget (mErrBox);

    retranslateUi();
}

void VBoxVMDetailsView::retranslateUi()
{
    if (mErrLabel)
        mErrLabel->setText (tr (
            "The selected virtual machine is <i>inaccessible</i>. Please "
            "inspect the error message shown below and press the "
            "<b>Refresh</b> button if you want to repeat the accessibility "
            "check:"));

    if (mRefreshAction && mRefreshButton)
    {
        mRefreshButton->setText (mRefreshAction->text());
        mRefreshButton->setIcon (mRefreshAction->icon());
        mRefreshButton->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    }
}

// VBoxVMDescriptionPage class
////////////////////////////////////////////////////////////////////////////////

/**
 *  Comments page widget to represent VM comments.
 */
class VBoxVMDescriptionPage : public QIWithRetranslateUI<QWidget>
{
    Q_OBJECT;

public:

    VBoxVMDescriptionPage (VBoxSelectorWnd *);
    ~VBoxVMDescriptionPage() {}

    void setMachineItem (VBoxVMItem *aItem);

    void updateState();

protected:

    void retranslateUi();

private slots:

    void goToSettings();

private:

    VBoxVMItem *mItem;

    VBoxSelectorWnd *mParent;
    QToolButton *mBtnEdit;
    QTextBrowser *mBrowser;
    QLabel *mLabel;
};

VBoxVMDescriptionPage::VBoxVMDescriptionPage (VBoxSelectorWnd *aParent)
    : QIWithRetranslateUI<QWidget> (aParent)
    , mItem (NULL), mParent (aParent)
    , mBtnEdit (0), mBrowser (0), mLabel (0)
{
    /* main layout */
    QVBoxLayout *vMainLayout = new QVBoxLayout (this);
    vMainLayout->setSpacing (10);
    VBoxGlobal::setLayoutMargin (vMainLayout, 0);

    /* mBrowser */
    mBrowser = new QTextBrowser (this);
    mBrowser->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    mBrowser->setFocusPolicy (Qt::StrongFocus);
    mBrowser->document()->setDefaultStyleSheet ("a { text-decoration: none; }");
    vMainLayout->addWidget (mBrowser);
    /* hidden by default */
    mBrowser->setHidden (true);

    mLabel = new QLabel (this);
    mLabel->setFrameStyle (mBrowser->frameStyle());
    mLabel->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Expanding);
    mLabel->setAlignment (Qt::AlignCenter);
    mLabel->setWordWrap (true);
    vMainLayout->addWidget (mLabel);
    /* always disabled */
    mLabel->setEnabled (false);

    /* button layout */
    QHBoxLayout *hBtnLayout = new QHBoxLayout ();
    vMainLayout->addLayout (hBtnLayout);
    hBtnLayout->setSpacing (10);
    hBtnLayout->addItem (new QSpacerItem (0, 0,
                                          QSizePolicy::Expanding,
                                          QSizePolicy::Minimum));

    /* button */
    mBtnEdit = new QToolButton (this);
    mBtnEdit->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
    mBtnEdit->setFocusPolicy (Qt::StrongFocus);
    mBtnEdit->setIcon (VBoxGlobal::iconSet (":/edit_description_16px.png",
                                            ":/edit_description_disabled_16px.png"));
    mBtnEdit->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    connect (mBtnEdit, SIGNAL (clicked()), this, SLOT (goToSettings()));
    hBtnLayout->addWidget (mBtnEdit);

    vMainLayout->addItem (new QSpacerItem (0, 0,
                                           QSizePolicy::Expanding,
                                           QSizePolicy::Minimum));

    /* apply language settings */
    retranslateUi();

    updateState();
}

/**
 * The machine list @a aItem is used to access cached machine data w/o making
 * unnecessary RPC calls.
 */
void VBoxVMDescriptionPage::setMachineItem (VBoxVMItem *aItem)
{
    mItem = aItem;

    QString text = aItem ? aItem->machine().GetDescription() : QString::null;

    if (!text.isEmpty())
    {
        mLabel->setHidden (true);
        mBrowser->setText (text);
        mBrowser->setVisible (true);
    }
    else
    {
        mBrowser->setHidden (true);
        mBrowser->clear();
        mLabel->setVisible (true);
    }

    /* check initial machine and session states */
    updateState();
}

void VBoxVMDescriptionPage::retranslateUi()
{
    mLabel->setText (tr ("No description. Press the Edit button below to add it."));

    mBtnEdit->setText (tr ("Edit"));
    mBtnEdit->setShortcut (QKeySequence ("Ctrl+E"));
    mBtnEdit->setToolTip (tr ("Edit (Ctrl+E)"));
    mBtnEdit->adjustSize();
    mBtnEdit->updateGeometry();
}

/**
 * Called by the parent from machineStateChanged() and sessionStateChanged()
 * signal handlers. We cannot connect to these signals ourselves because we
 * use the VBoxVMListBoxItem which needs to be properly updated by the parent
 * first.
 */
void VBoxVMDescriptionPage::updateState()
{
    /// @todo disabling the edit button for a saved VM will not be necessary
    /// when we implement the selective VM Settings dialog, where only fields
    /// that can be changed in the saved state, can be changed.

    if (mItem)
    {
        bool saved = mItem->state() == KMachineState_Saved;
        bool busy = mItem->sessionState() != KSessionState_Closed;
        mBtnEdit->setEnabled (!saved && !busy);
    }
    else
        mBtnEdit->setEnabled (false);
}

void VBoxVMDescriptionPage::goToSettings()
{
    mParent->vmSettings ("#general", "mTeDescription");
}

// VBoxSelectorWnd class
////////////////////////////////////////////////////////////////////////////////

/** \class VBoxSelectorWnd
 *
 *  The VBoxSelectorWnd class is a VM selector window, one of two main VBox
 *  GUI windows.
 *
 *  This window appears when the user starts the VirtualBox executable.
 *  It allows to view the list of configured VMs, their settings
 *  and the current state, create, reconfigure, delete and start VMs.
 */

/**
 *  Constructs the VM selector window.
 *
 *  @param aSelf pointer to a variable where to store |this| right after
 *               this object's constructor is called (necessary to avoid
 *               recursion in VBoxGlobal::selectorWnd())
 */
VBoxSelectorWnd::
VBoxSelectorWnd (VBoxSelectorWnd **aSelf, QWidget* aParent,
                 Qt::WindowFlags aFlags /* = Qt::Window */)
    : QIWithRetranslateUI2<QMainWindow> (aParent, aFlags)
    , doneInaccessibleWarningOnce (false)
{
    if (aSelf)
        *aSelf = this;

    statusBar();

#if defined (Q_WS_MAC) && (QT_VERSION < 0x040402)
    qApp->installEventFilter (this);
#endif /* defined (Q_WS_MAC) && (QT_VERSION < 0x040402) */

#if !(defined (Q_WS_WIN) || defined (Q_WS_MAC))
    /* The application icon. On Win32, it's built-in to the executable. On Mac
     * OS X the icon referenced in info.plist is used. */
    setWindowIcon (QIcon (":/VirtualBox_48px.png"));
#endif

    /* actions */

    fileMediaMgrAction = new QAction (this);
    fileMediaMgrAction->setIcon (VBoxGlobal::iconSet (":/diskimage_16px.png"));
    fileSettingsAction = new QAction(this);
    fileSettingsAction->setMenuRole (QAction::PreferencesRole);
    fileSettingsAction->setIcon (VBoxGlobal::iconSet (":/global_settings_16px.png"));
    fileExitAction = new QAction (this);
    fileExitAction->setMenuRole (QAction::QuitRole);
    fileExitAction->setIcon (VBoxGlobal::iconSet (":/exit_16px.png"));

    vmNewAction = new QAction (this);
    vmNewAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_new_32px.png", ":/new_16px.png"));
    vmConfigAction = new QAction (this);
    vmConfigAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_settings_32px.png", ":/settings_16px.png",
        ":/vm_settings_disabled_32px.png", ":/settings_dis_16px.png"));
    vmDeleteAction = new QAction (this);
    vmDeleteAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_delete_32px.png", ":/delete_16px.png",
        ":/vm_delete_disabled_32px.png", ":/delete_dis_16px.png"));
    vmStartAction = new QAction (this);
    vmStartAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_start_32px.png", ":/start_16px.png",
        ":/vm_start_disabled_32px.png", ":/start_dis_16px.png"));
    vmDiscardAction = new QAction (this);
    vmDiscardAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_discard_32px.png", ":/discard_16px.png",
        ":/vm_discard_disabled_32px.png", ":/discard_dis_16px.png"));
    vmPauseAction = new QAction (this);
    vmPauseAction->setCheckable (true);
    vmPauseAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_pause_32px.png", ":/pause_16px.png",
        ":/vm_pause_disabled_32px.png", ":/pause_disabled_16px.png"));
    vmRefreshAction = new QAction (this);
    vmRefreshAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/refresh_32px.png", ":/refresh_16px.png",
        ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png"));
    vmShowLogsAction = new QAction (this);
    vmShowLogsAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_show_logs_32px.png", ":/show_logs_16px.png",
        ":/vm_show_logs_disabled_32px.png", ":/show_logs_disabled_16px.png"));

    mHelpActions.setup (this);

    /* Central widget @ horizontal layout */
    setCentralWidget (new QWidget (this));
    QHBoxLayout *centralLayout = new QHBoxLayout (centralWidget());

    /* Left vertical box */
    QVBoxLayout *leftVLayout = new QVBoxLayout();
    /* Right vertical box */
    QVBoxLayout *rightVLayout = new QVBoxLayout();
    centralLayout->addLayout (leftVLayout, 1);
    centralLayout->addLayout (rightVLayout, 2);

    /* VM list toolbar */
    VBoxToolBar *vmTools = new VBoxToolBar (this);
#if MAC_LEOPARD_STYLE
    /* Enable unified toolbars on Mac OS X. Available on Qt >= 4.3 */
    addToolBar (vmTools);
    vmTools->setMacToolbar();
    /* No spacing/margin on the mac */
    VBoxGlobal::setLayoutMargin (centralLayout, 0);
    leftVLayout->setSpacing (0);
    rightVLayout->setSpacing (0);
    rightVLayout->insertSpacing (0, 10);
#else /* MAC_LEOPARD_STYLE */
    leftVLayout->addWidget (vmTools);
    centralLayout->setSpacing (9);
    VBoxGlobal::setLayoutMargin (centralLayout, 5);
    leftVLayout->setSpacing (5);
    rightVLayout->setSpacing (5);
#endif /* MAC_LEOPARD_STYLE */

    /* VM list view */
    mVMListView = new VBoxVMListView();
    mVMModel = new VBoxVMModel (mVMListView);
    mVMListView->setModel (mVMModel);

    leftVLayout->addWidget (mVMListView);

    /* VM tab widget containing details and snapshots tabs */
    vmTabWidget = new QTabWidget();
    rightVLayout->addWidget (vmTabWidget);

    /* VM details view */
    vmDetailsView = new VBoxVMDetailsView (NULL, vmRefreshAction);
    vmTabWidget->addTab (vmDetailsView,
                         VBoxGlobal::iconSet (":/settings_16px.png"),
                         QString::null);

    /* VM snapshots list */
    vmSnapshotsWgt = new VBoxSnapshotsWgt (NULL);
    vmTabWidget->addTab (vmSnapshotsWgt,
                         VBoxGlobal::iconSet (":/take_snapshot_16px.png",
                                              ":/take_snapshot_dis_16px.png"),
                         QString::null);
    vmSnapshotsWgt->setContentsMargins (10, 10, 10, 10);

    /* VM comments page */
    vmDescriptionPage = new VBoxVMDescriptionPage (this);
    vmTabWidget->addTab (vmDescriptionPage,
                         VBoxGlobal::iconSet (":/description_16px.png",
                                              ":/description_disabled_16px.png"),
                         QString::null);
    vmDescriptionPage->setContentsMargins (10, 10, 10, 10);

    /* add actions to the toolbar */

    vmTools->setIconSize (QSize (32, 32));
    vmTools->setToolButtonStyle (Qt::ToolButtonTextUnderIcon);
    vmTools->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Preferred);

    vmTools->addAction (vmNewAction);
    vmTools->addAction (vmConfigAction);
#if 0 /* delete action is really rare */
    vmTools->addAction (vmDeleteAction);
#endif
    vmTools->addAction (vmStartAction);
    vmTools->addAction (vmDiscardAction);

    /* add actions to menubar */

    mFileMenu = menuBar()->addMenu (QString::null);
    mFileMenu->addAction (fileMediaMgrAction);
    mFileMenu->addSeparator();
    mFileMenu->addAction (fileSettingsAction);
    mFileMenu->addSeparator();
    mFileMenu->addAction (fileExitAction);

    mVMMenu = menuBar()->addMenu (QString::null);
    mVMMenu->addAction (vmNewAction);
    mVMMenu->addAction (vmConfigAction);
    mVMMenu->addAction (vmDeleteAction);
    mVMMenu->addSeparator();
    mVMMenu->addAction (vmStartAction);
    mVMMenu->addAction (vmDiscardAction);
    mVMMenu->addAction (vmPauseAction);
    mVMMenu->addSeparator();
    mVMMenu->addAction (vmRefreshAction);
    mVMMenu->addAction (vmShowLogsAction);

    mVMCtxtMenu = new QMenu (this);
    mVMCtxtMenu->addAction (vmConfigAction);
    mVMCtxtMenu->addAction (vmDeleteAction);
    mVMCtxtMenu->addSeparator();
    mVMCtxtMenu->addAction (vmStartAction);
    mVMCtxtMenu->addAction (vmDiscardAction);
    mVMCtxtMenu->addAction (vmPauseAction);
    mVMCtxtMenu->addSeparator();
    mVMCtxtMenu->addAction (vmRefreshAction);
    mVMCtxtMenu->addAction (vmShowLogsAction);

    mHelpMenu = menuBar()->addMenu (QString::null);
    mHelpActions.addTo (mHelpMenu);

#ifdef VBOX_GUI_WITH_SYSTRAY
    mTrayShowWindowAction = new QAction (this);
    Assert (mTrayShowWindowAction);
    mTrayShowWindowAction->setIcon (VBoxGlobal::iconSet (":/VirtualBox_16px.png"));
    mTrayShowWindowAction->setText (tr ("Sun xVM VirtualBox"));

    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        mTrayIcon = new VBoxTrayIcon (this, mVMModel);
        Assert (mTrayIcon);
        connect (mTrayIcon, SIGNAL (activated (QSystemTrayIcon::ActivationReason)),
                this, SLOT (trayIconActivated (QSystemTrayIcon::ActivationReason)));
    }
#endif

    retranslateUi();

    /* Restore the position of the window */
    {
        CVirtualBox vbox = vboxGlobal().virtualBox();
        QString winPos = vbox.GetExtraData (VBoxDefs::GUI_LastWindowPosition);

        bool ok = false, max = false;
        int x = 0, y = 0, w = 0, h = 0;
        x = winPos.section (',', 0, 0).toInt (&ok);
        if (ok)
            y = winPos.section (',', 1, 1).toInt (&ok);
        if (ok)
            w = winPos.section (',', 2, 2).toInt (&ok);
        if (ok)
            h = winPos.section (',', 3, 3).toInt (&ok);
        if (ok)
            max = winPos.section (',', 4, 4) == VBoxDefs::GUI_LastWindowPosition_Max;
        if (ok)
        {
            QRect ar = QApplication::desktop()->availableGeometry (QPoint (x, y));

            /* Do some position checks */
            if (x < ar.left() || x > ar.right())
                x = ar.left();
            if (y < ar.top() || y > ar.bottom())
                y = ar.top();

            /* Composing normal parameters */
            normal_size = QSize (w, h).expandedTo (minimumSizeHint())
                          .boundedTo (ar.size());
            normal_pos = QPoint (x, y);

            /* Applying normal parameters */
            resize (normal_size);
            move (normal_pos);
            if (max)
                /* maximize if needed */
                showMaximized();
        }
        else
        {
            QRect ar = QApplication::desktop()->availableGeometry (this);
            resize (QSize (770, 550).expandedTo (minimumSizeHint())
                .boundedTo (ar.size()));
        }
    }

    /* Update the list */
    refreshVMList();
    /* Reset to the first item */
    mVMListView->selectItemByRow (0);
    /* restore the position of vm selector */
    {
        CVirtualBox vbox = vboxGlobal().virtualBox();
        QString prevVMId = vbox.GetExtraData (VBoxDefs::GUI_LastVMSelected);

        mVMListView->selectItemById (QUuid (prevVMId));
    }

    /* signals and slots connections */
    connect (fileMediaMgrAction, SIGNAL (triggered()), this, SLOT (fileMediaMgr()));
    connect (fileSettingsAction, SIGNAL (triggered()), this, SLOT (fileSettings()));
    connect (fileExitAction, SIGNAL (triggered()), this, SLOT (fileExit()));
    connect (vmNewAction, SIGNAL (triggered()), this, SLOT (vmNew()));

    connect (vmConfigAction, SIGNAL (triggered()), this, SLOT (vmSettings()));
    connect (vmDeleteAction, SIGNAL (triggered()), this, SLOT (vmDelete()));
    connect (vmStartAction, SIGNAL (triggered()), this, SLOT (vmStart()));
    connect (vmDiscardAction, SIGNAL (triggered()), this, SLOT (vmDiscard()));
    connect (vmPauseAction, SIGNAL (toggled (bool)), this, SLOT (vmPause (bool)));
    connect (vmRefreshAction, SIGNAL (triggered()), this, SLOT (vmRefresh()));
    connect (vmShowLogsAction, SIGNAL (triggered()), this, SLOT (vmShowLogs()));

#ifdef VBOX_GUI_WITH_SYSTRAY
    connect (mTrayShowWindowAction, SIGNAL (triggered()), this, SLOT (showWindow()));
#endif

    connect (mVMListView, SIGNAL (currentChanged()),
             this, SLOT (vmListViewCurrentChanged()));
    connect (mVMListView, SIGNAL (activated()),
             this, SLOT (vmStart()));
    connect (mVMListView, SIGNAL (customContextMenuRequested (const QPoint &)),
             this, SLOT (showContextMenu (const QPoint &)));

    connect (vmDetailsView, SIGNAL (linkClicked (const QString &)),
            this, SLOT (vmSettings (const QString &)));

    /* listen to media enumeration signals */
    connect (&vboxGlobal(), SIGNAL (mediumEnumStarted()),
             this, SLOT (mediumEnumStarted()));
    connect (&vboxGlobal(), SIGNAL (mediumEnumFinished (const VBoxMediaList &)),
             this, SLOT (mediumEnumFinished (const VBoxMediaList &)));

    /* connect VirtualBox callback events */
    connect (&vboxGlobal(), SIGNAL (machineStateChanged (const VBoxMachineStateChangeEvent &)),
             this, SLOT (machineStateChanged (const VBoxMachineStateChangeEvent &)));
    connect (&vboxGlobal(), SIGNAL (machineDataChanged (const VBoxMachineDataChangeEvent &)),
             this, SLOT (machineDataChanged (const VBoxMachineDataChangeEvent &)));
    connect (&vboxGlobal(), SIGNAL (machineRegistered (const VBoxMachineRegisteredEvent &)),
             this, SLOT (machineRegistered (const VBoxMachineRegisteredEvent &)));
    connect (&vboxGlobal(), SIGNAL (sessionStateChanged (const VBoxSessionStateChangeEvent &)),
             this, SLOT (sessionStateChanged (const VBoxSessionStateChangeEvent &)));
    connect (&vboxGlobal(), SIGNAL (snapshotChanged (const VBoxSnapshotEvent &)),
             this, SLOT (snapshotChanged (const VBoxSnapshotEvent &)));

    /* bring the VM list to the focus */
    mVMListView->setFocus();
}

VBoxSelectorWnd::~VBoxSelectorWnd()
{
    CVirtualBox vbox = vboxGlobal().virtualBox();

    /* Save the position of the window */
    {
        QString winPos = QString ("%1,%2,%3,%4")
                                 .arg (normal_pos.x())
                                 .arg (normal_pos.y())
                                 .arg (normal_size.width())
                                 .arg (normal_size.height());
        if (isMaximized())
            winPos += QString (",%1").arg (VBoxDefs::GUI_LastWindowPosition_Max);

        vbox.SetExtraData (VBoxDefs::GUI_LastWindowPosition, winPos);
    }

    /* Save vm selector position */
    {
        VBoxVMItem *item = mVMListView->selectedItem();
        QString curVMId = item ?
            QString (item->id()) :
            QString::null;
        vbox.SetExtraData (VBoxDefs::GUI_LastVMSelected, curVMId);
    }
    /* Delete the items from our model */
    mVMModel->clear();
}

//
// Public slots
/////////////////////////////////////////////////////////////////////////////

void VBoxSelectorWnd::fileMediaMgr()
{
    VBoxMediaManagerDlg::showModeless();
}

void VBoxSelectorWnd::fileSettings()
{
    VBoxGlobalSettings settings = vboxGlobal().settings();
    CSystemProperties props = vboxGlobal().virtualBox().GetSystemProperties();

    VBoxSettingsDialog *dlg = new VBoxGLSettingsDlg (this);
    dlg->getFrom();

    if (dlg->exec() == QDialog::Accepted)
        dlg->putBackTo();

    delete dlg;
}

void VBoxSelectorWnd::fileExit()
{
    /* We have to check if there are any open windows beside this mainwindow
     * (e.g. VDM) and if so close them. Note that the default behavior is
     * different to Qt3 where a *mainWidget* exists & if this going to close
     * all other windows are closed automatically. We do the same below. */
    foreach (QWidget *widget, QApplication::topLevelWidgets())
    {
        if (widget->isVisible() &&
            widget != this)
            widget->close();
    }
    /* We close this widget last. */
    close();
}

void VBoxSelectorWnd::vmNew()
{
    VBoxNewVMWzd wzd (this);
    if (wzd.exec() == QDialog::Accepted)
    {
        CMachine m = wzd.machine();

        /* wait until the list is updated by OnMachineRegistered() */
        QModelIndex index;
        while (!index.isValid())
        {
            qApp->processEvents();
            index = mVMModel->indexById (m.GetId());
        }
        mVMListView->setCurrentIndex (index);
    }
}

/**
 *  Opens the VM settings dialog.
 */
void VBoxSelectorWnd::vmSettings (const QString &aCategory, const QString &aControl,
                                  const QUuid &aUuid /*= QUuid_null*/)
{
    if (!aCategory.isEmpty() && aCategory [0] != '#')
    {
        /* Assume it's a href from the Details HTML */
        vboxGlobal().openURL (aCategory);
        return;
    }

    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    // open a direct session to modify VM settings
    QUuid id = item->id();
    CSession session = vboxGlobal().openSession (id);
    if (session.isNull())
        return;

    CMachine m = session.GetMachine();
    AssertMsgReturn (!m.isNull(), ("Machine must not be null"), (void) 0);

    VBoxSettingsDialog *dlg = new VBoxVMSettingsDlg (this, m, aCategory, aControl);
    dlg->getFrom();

    if (dlg->exec() == QDialog::Accepted)
    {
        QString oldName = m.GetName();
        dlg->putBackTo();

        m.SaveSettings();
        if (m.isOk())
        {
            if (oldName.compare (m.GetName()))
                mVMModel->sort();
        }
        else
            vboxProblem().cannotSaveMachineSettings (m);

        /* To check use the result in future
         * vboxProblem().cannotApplyMachineSettings (m, res); */
    }

    delete dlg;

    mVMListView->setFocus();

    session.Close();
}

void VBoxSelectorWnd::vmDelete (const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    if (vboxProblem().confirmMachineDeletion (item->machine()))
    {
        CVirtualBox vbox = vboxGlobal().virtualBox();
        QUuid id = item->id();
        bool ok = false;
        if (item->accessible())
        {
            /* Open a direct session to modify VM settings */
            CSession session = vboxGlobal().openSession (id);
            if (session.isNull())
                return;
            CMachine machine = session.GetMachine();
            /* Detach all attached Hard Disks */
            CHardDisk2AttachmentVector vec = machine.GetHardDisk2Attachments();
            for (int i = 0; i < vec.size(); ++ i)
            {
                CHardDisk2Attachment hda = vec [i];
                machine.DetachHardDisk2 (hda.GetBus(), hda.GetChannel(), hda.GetDevice());
                if (!machine.isOk())
                    vboxProblem().cannotDetachHardDisk (this, machine,
                        vboxGlobal().getMedium (CMedium (hda.GetHardDisk())).location(),
                        hda.GetBus(), hda.GetChannel(), hda.GetDevice());
            }
            /* Commit changes */
            machine.SaveSettings();
            if (!machine.isOk())
                vboxProblem().cannotSaveMachineSettings (machine);
            else
                ok = true;
            session.Close();
        }
        else
            ok = true;

        if (ok)
        {
            CMachine machine = item->machine();
            vbox.UnregisterMachine (id);
            if (vbox.isOk() && item->accessible())
            {
                /* delete machine settings */
                machine.DeleteSettings();
                /* remove the item shortly: cmachine it refers to is no longer valid! */
                int row = mVMModel->rowById (item->id());
                mVMModel->removeItem (item);
                delete item;
                mVMListView->ensureSomeRowSelected (row);
            }
            if (!vbox.isOk() || !machine.isOk())
                vboxProblem().cannotDeleteMachine (vbox, machine);
        }
    }
}

void VBoxSelectorWnd::vmStart (const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    /* We always get here when mVMListView emits the activated() signal,
     * so we must explicitly check if the action is enabled or not. */
   if (aUuid.isNull() && vmStartAction)  /* If Uuid is NULL, this function is called from the mVMListView activated() signal. */
   {
       if (!vmStartAction->isEnabled())
           return;
   }

#if defined (VBOX_GUI_SEPARATE_VM_PROCESS)

    AssertMsg (!vboxGlobal().isVMConsoleProcess(),
               ("Must NOT be a VM console process"));

    /* just switch to the VM window if it already exists */
    if (item->canSwitchTo())
    {
        item->switchTo();
        return;
    }

    AssertMsg (item->state() < KMachineState_Running,
               ("Machine must be PoweredOff/Saved/Aborted"));

    QUuid id = item->id();
    CVirtualBox vbox = vboxGlobal().virtualBox();
    CSession session;

    session.createInstance (CLSID_Session);
    if (session.isNull())
    {
        vboxProblem().cannotOpenSession (session);
        return;
    }

#if defined (Q_OS_WIN32)
    /* allow the started VM process to make itself the foreground window */
    AllowSetForegroundWindow (ASFW_ANY);
#endif

    QString env;
#if defined (Q_WS_X11)
    /* make sure the VM process will start on the same display as the Selector */
    {
        const char *display = RTEnvGet ("DISPLAY");
        if (display)
            env.sprintf ("DISPLAY=%s", display);
    }
#endif

    CProgress progress = vbox.OpenRemoteSession (session, id, "GUI/Qt4", env);
    if (!vbox.isOk())
    {
        vboxProblem().cannotOpenSession (vbox, item->machine());
        return;
    }

    /* show the "VM spawning" progress dialog */
    vboxProblem().showModalProgressDialog (progress, item->name(), this);

    if (progress.GetResultCode() != 0)
        vboxProblem().cannotOpenSession (vbox, item->machine(), progress);

    session.Close();

#else // !VBOX_GUI_SEPARATE_VM_PROCESS

    if (!vboxGlobal().startMachine (id))
        return;

    hide();

#endif
}

void VBoxSelectorWnd::vmDiscard (const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    if (!vboxProblem().confirmDiscardSavedState (item->machine()))
        return;

    /* open a session to modify VM settings */
    QUuid id = item->id();
    CSession session;
    CVirtualBox vbox = vboxGlobal().virtualBox();
    session.createInstance (CLSID_Session);
    if (session.isNull())
    {
        vboxProblem().cannotOpenSession (session);
        return;
    }
    vbox.OpenSession (session, id);
    if (!vbox.isOk())
    {
        vboxProblem().cannotOpenSession (vbox, item->machine());
        return;
    }

    CConsole console = session.GetConsole();
    console.DiscardSavedState();
    if (!console.isOk())
        vboxProblem().cannotDiscardSavedState (console);

    session.Close();
}

void VBoxSelectorWnd::vmPause (bool aPause, const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    CSession session = vboxGlobal().openExistingSession (item->id());
    if (session.isNull())
        return;

    CConsole console = session.GetConsole();
    if (console.isNull())
        return;

    if (aPause)
        console.Pause();
    else
        console.Resume();

    bool ok = console.isOk();
    if (!ok)
    {
        if (aPause)
            vboxProblem().cannotPauseMachine (console);
        else
            vboxProblem().cannotResumeMachine (console);
    }

    session.Close();
}

void VBoxSelectorWnd::vmRefresh (const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    refreshVMItem (item->id(),
                   true /* aDetails */,
                   true /* aSnapshot */,
                   true /* aDescription */);
}

void VBoxSelectorWnd::vmShowLogs (const QUuid &aUuid /*= QUuid_null*/)
{
    VBoxVMItem *item = aUuid.isNull() ? mVMListView->selectedItem() :
                       mVMModel->itemById (aUuid);

    AssertMsgReturnVoid (item, ("Item must be always selected here"));

    CMachine machine = item->machine();
    VBoxVMLogViewer::createLogViewer (this, machine);
}

void VBoxSelectorWnd::refreshVMList()
{
    CVirtualBox vbox = vboxGlobal().virtualBox();
    CMachineVector vec = vbox.GetMachines2();
    for (CMachineVector::ConstIterator m = vec.begin();
         m != vec.end(); ++ m)
        mVMModel->addItem (new VBoxVMItem (*m));
    mVMModel->sort();

    vmListViewCurrentChanged();

#ifdef VBOX_GUI_WITH_SYSTRAY
    mTrayIcon->refresh();
#endif
}

void VBoxSelectorWnd::refreshVMItem (const QUuid &aID, bool aDetails,
                                                       bool aSnapshots,
                                                       bool aDescription)
{
    VBoxVMItem *item = mVMModel->itemById (aID);
    if (item)
    {
        mVMModel->refreshItem (item);
        if (item && item->id() == aID)
            vmListViewCurrentChanged (aDetails, aSnapshots, aDescription);
    }
}

void VBoxSelectorWnd::showContextMenu (const QPoint &aPoint)
{
    /* Send a context menu request */
    const QModelIndex &index = mVMListView->indexAt (aPoint);
    if (index.isValid())
        if (VBoxVMItem *item = mVMListView->model()->data (index,
            VBoxVMModel::VBoxVMItemPtrRole).value <VBoxVMItem*>())
                mVMCtxtMenu->exec (mVMListView->mapToGlobal (aPoint));
}

#ifdef VBOX_GUI_WITH_SYSTRAY

void VBoxSelectorWnd::trayIconActivated (QSystemTrayIcon::ActivationReason aReason)
{
    switch (aReason)
    {
        case QSystemTrayIcon::Context:

            mTrayIcon->refresh();
            break;

        case QSystemTrayIcon::Trigger:
            break;

        case QSystemTrayIcon::DoubleClick:

            showWindow();
            break;

        case QSystemTrayIcon::MiddleClick:
            break;

        default:
            break;
    }
}

void VBoxSelectorWnd::showWindow()
{
    showNormal();
    setFocus();
}

#endif // VBOX_GUI_WITH_SYSTRAY

// Protected members
/////////////////////////////////////////////////////////////////////////////

bool VBoxSelectorWnd::event (QEvent *e)
{
    switch (e->type())
    {
        /* By handling every Resize and Move we keep track of the normal
         * (non-minimized and non-maximized) window geometry. Shame on Qt
         * that it doesn't provide this geometry in its public APIs. */

        case QEvent::Resize:
        {
            QResizeEvent *re = (QResizeEvent *) e;
            if ((windowState() & (Qt::WindowMaximized | Qt::WindowMinimized |
                                  Qt::WindowFullScreen)) == 0)
                normal_size = re->size();
            break;
        }
        case QEvent::Move:
        {
            if ((windowState() & (Qt::WindowMaximized | Qt::WindowMinimized |
                                  Qt::WindowFullScreen)) == 0)
            {
                /* On X11 systems window remains un-framed before it
                 * shown and painted for the first time. In this case
                 * qt returns similar values for window's position either
                 * including or excluding window's frame. We no need to
                 * memorize window's position in this case as this is
                 * not normal situation, so just ignoring such events.
                 * Do not trust frameGeometry() in this situation. */

                if (!(pos().x() == geometry().x() && pos().y() == geometry().y()))
                    normal_pos = pos();
            }
            break;
        }

        default:
            break;
    }

    return QMainWindow::event (e);
}

void VBoxSelectorWnd::closeEvent (QCloseEvent *aEvent)
{
    emit closing();
    return QMainWindow::closeEvent (aEvent);
}

#if defined (Q_WS_MAC) && (QT_VERSION < 0x040402)
bool VBoxSelectorWnd::eventFilter (QObject *aObject, QEvent *aEvent)
{
    if (!isActiveWindow())
        return QIWithRetranslateUI2<QMainWindow>::eventFilter (aObject, aEvent);

    if (qobject_cast<QWidget*> (aObject) &&
        qobject_cast<QWidget*> (aObject)->window() != this)
        return QIWithRetranslateUI2<QMainWindow>::eventFilter (aObject, aEvent);

    switch (aEvent->type())
    {
        case QEvent::KeyPress:
            {
                /* Bug in Qt below 4.4.2. The key events are send to the current
                 * window even if a menu is shown & has the focus. See
                 * http://trolltech.com/developer/task-tracker/index_html?method=entry&id=214681. */
                if (::darwinIsMenuOpen())
                    return true;
            }
        default:
            break;
    }
    return QIWithRetranslateUI2<QMainWindow>::eventFilter (aObject, aEvent);
}
#endif /* defined (Q_WS_MAC) && (QT_VERSION < 0x040402) */

/**
 *  Sets the strings of the subwidgets using the current
 *  language.
 */
void VBoxSelectorWnd::retranslateUi()
{
#ifdef VBOX_OSE
    setWindowTitle (tr ("VirtualBox OSE"));
#else
    setWindowTitle (tr ("Sun xVM VirtualBox"));
#endif

    vmTabWidget->setTabText (vmTabWidget->indexOf (vmDetailsView), tr ("&Details"));
    /* note: Snapshots and Details tabs are changed dynamically by
     * vmListViewCurrentChanged() */

    /* ensure the details and screenshot view are updated */
    vmListViewCurrentChanged();

    fileMediaMgrAction->setText (tr ("&Virtual Media Manager..."));
    fileMediaMgrAction->setShortcut (QKeySequence ("Ctrl+D"));
    fileMediaMgrAction->setStatusTip (tr ("Display the Virtual Media Manager dialog"));

#ifdef Q_WS_MAC
    /*
     * Macification: Getting the right menu as application preference menu item.
     *
     * QMenuBar::isCommand() in qmenubar_mac.cpp doesn't recognize "Setting"(s)
     * unless it's in the first position. So, we use the Mac term here to make
     * sure we get picked instead of the VM settings.
     *
     * Now, since both QMenuBar and we translate these strings, it's going to
     * be really interesting to see how this plays on non-english systems...
     */
    fileSettingsAction->setText (tr ("&Preferences...", "global settings"));
#else
    /*
     * ...and on other platforms we use "Preferences" as well. The #ifdef is
     * left because of the possible localization problems on Mac we first need
     * to figure out.
     */
    fileSettingsAction->setText (tr ("&Preferences...", "global settings"));
#endif
    fileSettingsAction->setShortcut (QKeySequence ("Ctrl+G"));
    fileSettingsAction->setStatusTip (tr ("Display the global settings dialog"));

    fileExitAction->setText (tr ("E&xit"));
    fileExitAction->setShortcut (QKeySequence ("Ctrl+Q"));
    fileExitAction->setStatusTip (tr ("Close application"));

    vmNewAction->setText (tr ("&New..."));
    vmNewAction->setShortcut (QKeySequence ("Ctrl+N"));
    vmNewAction->setStatusTip (tr ("Create a new virtual machine"));
    vmNewAction->setToolTip (vmNewAction->text().remove ('&').remove ('.') +
        QString (" (%1)").arg (vmNewAction->shortcut().toString()));

    vmConfigAction->setText (tr ("&Settings..."));
    vmConfigAction->setShortcut (QKeySequence ("Ctrl+S"));
    vmConfigAction->setStatusTip (tr ("Configure the selected virtual machine"));
    vmConfigAction->setToolTip (vmConfigAction->text().remove ('&').remove ('.') +
        QString (" (%1)").arg (vmConfigAction->shortcut().toString()));

    vmDeleteAction->setText (tr ("&Delete"));
    vmDeleteAction->setStatusTip (tr ("Delete the selected virtual machine"));

    /* Note: vmStartAction text is set up in vmListViewCurrentChanged() */

    vmDiscardAction->setText (tr ("D&iscard"));
    vmDiscardAction->setStatusTip (
        tr ("Discard the saved state of the selected virtual machine"));

    vmRefreshAction->setText (tr ("&Refresh"));
    vmRefreshAction->setShortcut (QKeySequence ("Ctrl+R"));
    vmRefreshAction->setStatusTip (
        tr ("Refresh the accessibility state of the selected virtual machine"));

    vmShowLogsAction->setText (tr ("Show &Log..."));
    vmShowLogsAction->setIconText (tr ("Log", "icon text"));
    vmShowLogsAction->setShortcut (QKeySequence ("Ctrl+L"));
    vmShowLogsAction->setStatusTip (
        tr ("Show the log files of the selected virtual machine"));

    mHelpActions.retranslateUi();

    mFileMenu->setTitle (tr("&File"));
    mVMMenu->setTitle (tr ("&Machine"));
    mHelpMenu->setTitle (tr ("&Help"));

#ifdef VBOX_GUI_WITH_SYSTRAY
    mTrayIcon->retranslateUi();
    mTrayIcon->refresh();
#endif
}


// Private members
/////////////////////////////////////////////////////////////////////////////

//
// Private slots
/////////////////////////////////////////////////////////////////////////////

void VBoxSelectorWnd::vmListViewCurrentChanged (bool aRefreshDetails,
                                                bool aRefreshSnapshots,
                                                bool aRefreshDescription)
{
    VBoxVMItem *item = mVMListView->selectedItem();

    if (item && item->accessible())
    {
        CMachine m = item->machine();

        KMachineState state = item->state();
        bool running = item->sessionState() != KSessionState_Closed;
        bool modifyEnabled = !running && state != KMachineState_Saved;

        if (aRefreshDetails)
        {
            vmDetailsView->setDetailsText (
                vboxGlobal().detailsReport (m, false /* isNewVM */,
                                            modifyEnabled /* withLinks */));
        }
        if (aRefreshSnapshots)
        {
            /* update the snapshots tab name */
            QString name = tr ("&Snapshots");
            ULONG count = item->snapshotCount();
            if (count)
                name += QString (" (%1)").arg (count);
            vmTabWidget->setTabText (vmTabWidget->indexOf (vmSnapshotsWgt), name);
            /* refresh the snapshots widget */
            vmSnapshotsWgt->setMachine (m);
            /* ensure the tab is enabled */
            vmTabWidget->setTabEnabled (vmTabWidget->indexOf (vmSnapshotsWgt), true);
        }
        if (aRefreshDescription)
        {
            /* update the description tab name */
            QString name = m.GetDescription().isEmpty() ?
                tr ("D&escription") : tr ("D&escription *");
            vmTabWidget->setTabText (vmTabWidget->indexOf (vmDescriptionPage), name);
            /* refresh the description widget */
            vmDescriptionPage->setMachineItem (item);
            /* ensure the tab is enabled */
            vmTabWidget->setTabEnabled (vmTabWidget->indexOf (vmDescriptionPage), true);
        }

        /* enable/disable modify actions */
        vmConfigAction->setEnabled (modifyEnabled);
        vmDeleteAction->setEnabled (modifyEnabled);
        vmDiscardAction->setEnabled (state == KMachineState_Saved && !running);
        vmPauseAction->setEnabled (state == KMachineState_Running ||
                                   state == KMachineState_Paused);

        /* change the Start button text accordingly */
        if (state >= KMachineState_Running)
        {
            vmStartAction->setText (tr ("S&how"));
            vmStartAction->setStatusTip (
                tr ("Switch to the window of the selected virtual machine"));

            vmStartAction->setEnabled (item->canSwitchTo());
        }
        else
        {
            vmStartAction->setText (tr ("S&tart"));
            vmStartAction->setStatusTip (
                tr ("Start the selected virtual machine"));

            vmStartAction->setEnabled (!running);
        }

        /* change the Pause/Resume button text accordingly */
        if (state == KMachineState_Paused)
        {
            vmPauseAction->setText (tr ("R&esume"));
            vmPauseAction->setShortcut (QKeySequence ("Ctrl+P"));
            vmPauseAction->setStatusTip (
                tr ("Resume the execution of the virtual machine"));
            vmPauseAction->blockSignals (true);
            vmPauseAction->setChecked (true);
            vmPauseAction->blockSignals (false);
        }
        else
        {
            vmPauseAction->setText (tr ("&Pause"));
            vmPauseAction->setShortcut (QKeySequence ("Ctrl+P"));
            vmPauseAction->setStatusTip (
                tr ("Suspend the execution of the virtual machine"));
            vmPauseAction->blockSignals (true);
            vmPauseAction->setChecked (false);
            vmPauseAction->blockSignals (false);
        }

        /* disable Refresh for accessible machines */
        vmRefreshAction->setEnabled (false);

        /* enable the show log item for the selected vm */
        vmShowLogsAction->setEnabled (true);
    }
    else
    {
        /* Note that the machine becomes inaccessible (or if the last VM gets
         * deleted), we have to update all fields, ignoring input
         * arguments. */

        if (item)
        {
            /* the VM is inaccessible */
            vmDetailsView->setErrorText (
                VBoxProblemReporter::formatErrorInfo (item->accessError()));
            vmRefreshAction->setEnabled (true);
        }
        else
        {
            /* default HTML support in Qt is terrible so just try to get
             * something really simple */
            vmDetailsView->setDetailsText
                (tr ("<h3>"
                     "Welcome to VirtualBox!</h3>"
                     "<p>The left part of this window is intended to display "
                     "a list of all virtual machines on your computer. "
                     "The list is empty now because you haven't created any virtual "
                     "machines yet."
                     "<img src=:/welcome.png align=right/></p>"
                     "<p>In order to create a new virtual machine, press the "
                     "<b>New</b> button in the main tool bar located "
                     "at the top of the window.</p>"
                     "<p>You can press the <b>F1</b> key to get instant help, "
                     "or visit "
                     "<a href=http://www.virtualbox.org>www.virtualbox.org</a> "
                     "for the latest information and news.</p>"));
            vmRefreshAction->setEnabled (false);
        }

        /* empty and disable other tabs */

        vmSnapshotsWgt->setMachine (CMachine());
        vmTabWidget->setTabText (vmTabWidget->indexOf (vmSnapshotsWgt), tr ("&Snapshots"));
        vmTabWidget->setTabEnabled (vmTabWidget->indexOf (vmSnapshotsWgt), false);

        vmDescriptionPage->setMachineItem (NULL);
        vmTabWidget->setTabText (vmTabWidget->indexOf (vmDescriptionPage), tr ("D&escription"));
        vmTabWidget->setTabEnabled (vmTabWidget->indexOf (vmDescriptionPage), false);

        /* disable modify actions */
        vmConfigAction->setEnabled (false);
        vmDeleteAction->setEnabled (item != NULL);
        vmDiscardAction->setEnabled (false);
        vmPauseAction->setEnabled (false);

        /* change the Start button text accordingly */
        vmStartAction->setText (tr ("S&tart"));
        vmStartAction->setStatusTip (
            tr ("Start the selected virtual machine"));
        vmStartAction->setEnabled (false);

        /* disable the show log item for the selected vm */
        vmShowLogsAction->setEnabled (false);
    }
}

void VBoxSelectorWnd::mediumEnumStarted()
{
    /* refresh the current details to pick up hard disk sizes */
    vmListViewCurrentChanged (true /* aRefreshDetails */);
}

void VBoxSelectorWnd::mediumEnumFinished (const VBoxMediaList &list)
{
    /* refresh the current details to pick up hard disk sizes */
    vmListViewCurrentChanged (true /* aRefreshDetails */);

    /* we warn about inaccessible media only once (after media emumeration
     * started from main() at startup), to avoid annoying the user */
    if (doneInaccessibleWarningOnce)
        return;

    doneInaccessibleWarningOnce = true;

    do
    {
        /* ignore the signal if a modal widget is currently active (we won't be
         * able to properly show the modeless VDI manager window in this case) */
        if (QApplication::activeModalWidget())
            break;

        /* ignore the signal if a VBoxMediaManagerDlg window is active */
        if (qApp->activeWindow() &&
            !strcmp (qApp->activeWindow()->metaObject()->className(), "VBoxMediaManagerDlg"))
            break;

        /* look for at least one inaccessible media */
        VBoxMediaList::const_iterator it;
        for (it = list.begin(); it != list.end(); ++ it)
            if ((*it).state() == KMediaState_Inaccessible)
                break;

        if (it != list.end() && vboxProblem().remindAboutInaccessibleMedia())
        {
            /* Show the VDM dialog but don't refresh once more after a
             * just-finished refresh */
            VBoxMediaManagerDlg::showModeless (this, false /* aRefresh */);
        }
    }
    while (0);
}

void VBoxSelectorWnd::machineStateChanged (const VBoxMachineStateChangeEvent &e)
{
    refreshVMItem (e.id,
                   false /* aDetails */,
                   false /* aSnapshots */,
                   false /* aDescription */);

    /* simulate a state change signal */
    vmDescriptionPage->updateState();
}

void VBoxSelectorWnd::machineDataChanged (const VBoxMachineDataChangeEvent &e)
{
    refreshVMItem (e.id,
                   true  /* aDetails */,
                   false /* aSnapshots */,
                   true  /* aDescription */);
}

void VBoxSelectorWnd::machineRegistered (const VBoxMachineRegisteredEvent &e)
{
    if (e.registered)
    {
        CVirtualBox vbox = vboxGlobal().virtualBox();
        CMachine m = vbox.GetMachine (e.id);
        if (!m.isNull())
        {
            mVMModel->addItem (new VBoxVMItem (m));
            mVMModel->sort();
            /* Make sure the description, ... pages are properly updated.
             * Actualy we haven't call the next method, but unfortunately Qt
             * seems buggy if the new item is on the same position as the
             * previous one. So go on the safe side and call this by our self. */
            vmListViewCurrentChanged();
        }
        /* m.isNull() is ok (theoretically, the machine could have been
         * already deregistered by some other client at this point) */
    }
    else
    {
        VBoxVMItem *item = mVMModel->itemById (e.id);
        if (item)
        {
            int row = mVMModel->rowById (item->id());
            mVMModel->removeItem (item);
            delete item;
            mVMListView->ensureSomeRowSelected (row);
        }

        /* item = 0 is ok (if we originated this event then the item
         * has been already removed) */
    }
}

void VBoxSelectorWnd::sessionStateChanged (const VBoxSessionStateChangeEvent &e)
{
    refreshVMItem (e.id,
                   true  /* aDetails */,
                   false /* aSnapshots */,
                   false /* aDescription */);

    /* simulate a state change signal */
    vmDescriptionPage->updateState();
}

void VBoxSelectorWnd::snapshotChanged (const VBoxSnapshotEvent &aEvent)
{
    refreshVMItem (aEvent.machineId,
                   false /* aDetails */,
                   true  /* aSnapshot */,
                   false /* aDescription */);
}

#ifdef VBOX_GUI_WITH_SYSTRAY

VBoxTrayIcon::VBoxTrayIcon (VBoxSelectorWnd* aParent, VBoxVMModel* aVMModel)
{
    mVmConfigAction = new QAction (this);
    Assert (mVmConfigAction);
    mVmConfigAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_settings_32px.png", ":/settings_16px.png",
        ":/vm_settings_disabled_32px.png", ":/settings_dis_16px.png"));
    mVmDeleteAction = new QAction (this);
    Assert (mVmDeleteAction);
    mVmDeleteAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_delete_32px.png", ":/delete_16px.png",
        ":/vm_delete_disabled_32px.png", ":/delete_dis_16px.png"));
    mVmStartAction = new QAction (this);
    Assert (mVmStartAction);
    mVmStartAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_start_32px.png", ":/start_16px.png",
        ":/vm_start_disabled_32px.png", ":/start_dis_16px.png"));
    mVmDiscardAction = new QAction (this);
    Assert (mVmDiscardAction);
    mVmDiscardAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_discard_32px.png", ":/discard_16px.png",
        ":/vm_discard_disabled_32px.png", ":/discard_dis_16px.png"));
    mVmPauseAction = new QAction (this);
    Assert (mVmPauseAction);
    mVmPauseAction->setCheckable (true);
    mVmPauseAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_pause_32px.png", ":/pause_16px.png",
        ":/vm_pause_disabled_32px.png", ":/pause_disabled_16px.png"));
    mVmRefreshAction = new QAction (this);
    Assert (mVmRefreshAction);
    mVmRefreshAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/refresh_32px.png", ":/refresh_16px.png",
        ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png"));
    mVmShowLogsAction = new QAction (this);
    Assert (mVmConfigAction);
    mVmShowLogsAction->setIcon (VBoxGlobal::iconSetFull (
        QSize (32, 32), QSize (16, 16),
        ":/vm_show_logs_32px.png", ":/show_logs_16px.png",
        ":/vm_show_logs_disabled_32px.png", ":/show_logs_disabled_16px.png"));

    mTrayIconMenu = new QMenu (aParent);
    Assert (mTrayIconMenu);

    mParent = aParent;
    mVMModel = aVMModel;

    setIcon (QIcon (":/VirtualBox_16px.png"));
    setContextMenu (mTrayIconMenu);

    retranslateUi();
}

VBoxTrayIcon::~VBoxTrayIcon ()
{

}

void VBoxTrayIcon::showSubMenu ()
{
    VBoxVMItem* pItem = NULL;
    QMenu *pMenu = NULL;
    QVariant vID;
    if (pMenu = qobject_cast<QMenu*>(sender()))
    {
        vID = pMenu->menuAction()->data();
        if (vID.canConvert<QUuid>() && mVMModel)
            pItem = mVMModel->itemById (qvariant_cast<QUuid>(vID));
    }

    mVmConfigAction->setData (vID);
    mVmDeleteAction->setData (vID);
    mVmDiscardAction->setData (vID);
    mVmStartAction->setData (vID);
    mVmPauseAction->setData (vID);
    mVmShowLogsAction->setData (vID);

    if (pItem && pItem->accessible())
    {
        /* look at vmListViewCurrentChanged() */
        CMachine m = pItem->machine();
        KMachineState s = pItem->state();
        bool bRunning = pItem->sessionState() != KSessionState_Closed;
        bool bModifyEnabled = !bRunning && s != KMachineState_Saved;

        /* Settings */
        mVmConfigAction->setEnabled (bModifyEnabled);

        /* Delete */
        mVmDeleteAction->setEnabled (bModifyEnabled);

        /* Discard */
        mVmDiscardAction->setEnabled (s == KMachineState_Saved && !bRunning);

        /* Change the Start button text accordingly */
        if (s >= KMachineState_Running)
        {
            mVmStartAction->setText (VBoxVMListView::tr ("S&how"));
            mVmStartAction->setStatusTip (
                  VBoxVMListView::tr ("Switch to the window of the selected virtual machine"));
            mVmStartAction->setEnabled (pItem->canSwitchTo());
        }
        else
        {
            mVmStartAction->setText (VBoxVMListView::tr ("S&tart"));
            mVmStartAction->setStatusTip (
                  VBoxVMListView::tr ("Start the selected virtual machine"));
            mVmStartAction->setEnabled (!bRunning);
        }

        /* Change the Pause/Resume button text accordingly */
        mVmPauseAction->setEnabled (s == KMachineState_Running ||
                                    s == KMachineState_Paused);

        if (s == KMachineState_Paused)
        {
            mVmPauseAction->setText (VBoxVMListView::tr ("R&esume"));
            mVmPauseAction->setStatusTip (
                  VBoxVMListView::tr ("Resume the execution of the virtual machine"));
            mVmPauseAction->blockSignals (true);
            mVmPauseAction->setChecked (true);
            mVmPauseAction->blockSignals (false);
        }
        else
        {
            mVmPauseAction->setText (VBoxVMListView::tr ("&Pause"));
            mVmPauseAction->setStatusTip (
                  VBoxVMListView::tr ("Suspend the execution of the virtual machine"));
            mVmPauseAction->blockSignals (true);
            mVmPauseAction->setChecked (false);
            mVmPauseAction->blockSignals (false);
        }

        mVmShowLogsAction->setEnabled (true);

        /* Disconnect old slot which maybe was connected from another selected sub menu. */
        disconnect (mVmConfigAction, SIGNAL (triggered()), this, SLOT (vmSettings()));
        disconnect (mVmDeleteAction, SIGNAL (triggered()), this, SLOT (vmDelete()));
        disconnect (mVmDiscardAction, SIGNAL (triggered()), this, SLOT (vmDiscard()));
        disconnect (mVmStartAction, SIGNAL (triggered()), this, SLOT (vmStart()));
        disconnect (mVmPauseAction, SIGNAL (toggled (bool)), this, SLOT (vmPause (bool)));
        disconnect (mVmShowLogsAction, SIGNAL (triggered()), this, SLOT (vmShowLogs()));

        /* Connect new sub menu with slots. */
        connect (mVmConfigAction, SIGNAL (triggered()), this, SLOT (vmSettings()));
        connect (mVmDeleteAction, SIGNAL (triggered()), this, SLOT (vmDelete()));
        connect (mVmDiscardAction, SIGNAL (triggered()), this, SLOT (vmDiscard()));
        connect (mVmStartAction, SIGNAL (triggered()), this, SLOT (vmStart()));
        connect (mVmPauseAction, SIGNAL (toggled (bool)), this, SLOT (vmPause (bool)));
        connect (mVmShowLogsAction, SIGNAL (triggered()), this, SLOT (vmShowLogs()));
    }
    else    /* Item is not accessible. */
    {
        mVmConfigAction->setEnabled (false);
        mVmDeleteAction->setEnabled (pItem != NULL);
        mVmDiscardAction->setEnabled (false);
        mVmPauseAction->setEnabled (false);

        /* Set the Start button text accordingly. */
        mVmStartAction->setText (VBoxVMListView::tr ("S&tart"));
        mVmStartAction->setStatusTip (
              VBoxVMListView::tr ("Start the selected virtual machine"));
        mVmStartAction->setEnabled (false);

        /* Disable the show log item for the selected vm. */
        mVmShowLogsAction->setEnabled (false);
    }

    /* Build sub menu entries. */
    pMenu->addAction (mVmConfigAction);
    pMenu->addAction (mVmDeleteAction);
    pMenu->addSeparator();
    pMenu->addAction (mVmStartAction);
    pMenu->addAction (mVmDiscardAction);
    pMenu->addAction (mVmPauseAction);
    pMenu->addSeparator();
    pMenu->addAction (mVmShowLogsAction);
}

void VBoxTrayIcon::retranslateUi ()
{
    mVmConfigAction->setText (VBoxVMListView::tr ("&Settings..."));
    mVmConfigAction->setStatusTip (VBoxVMListView::tr ("Configure the selected virtual machine"));

    mVmDeleteAction->setText (VBoxVMListView::tr ("&Delete"));
    mVmDeleteAction->setStatusTip (VBoxVMListView::tr ("Delete the selected virtual machine"));

    mVmDiscardAction->setText (VBoxVMListView::tr ("D&iscard"));
    mVmDiscardAction->setStatusTip (VBoxVMListView::tr ("Discard the saved state of the selected virtual machine"));

    mVmShowLogsAction->setText (VBoxVMListView::tr ("Show &Log..."));
    mVmShowLogsAction->setStatusTip (VBoxVMListView::tr ("Show the log files of the selected virtual machine"));
}

void VBoxTrayIcon::hideSubMenu ()
{
    VBoxVMItem* pItem = NULL;
    QMenu *pMenu = NULL;
    QVariant vID;
    if (pMenu = qobject_cast<QMenu*>(sender()))
    {
        vID = pMenu->menuAction()->data();
        if (vID.canConvert<QUuid>() && mVMModel)
            pItem = mVMModel->itemById (qvariant_cast<QUuid>(vID));
    }

    /* Nothing to do here yet. */

    Assert (pItem);
}

void VBoxTrayIcon::refresh ()
{
    if (false == QSystemTrayIcon::isSystemTrayAvailable())
        return;

    AssertReturnVoid (mVMModel);
    AssertReturnVoid (mTrayIconMenu);

    setVisible (true);
    mTrayIconMenu->clear();

    VBoxVMItem* pItem = NULL;
    QMenu* pCurMenu = mTrayIconMenu;
    QMenu* pSubMenu = NULL;

    int iCurItemCount = 0;

    for (int i = 0; i < mVMModel->rowCount(); i++, iCurItemCount++)
    {
        pItem = mVMModel->itemByRow(i);
        Assert(pItem);

        if (iCurItemCount > 10) /* 10 machines per sub menu. */
        {
            pSubMenu = new QMenu (tr ("&Other Machines...", "tray menu"));
            Assert (pSubMenu);
            pCurMenu->addMenu (pSubMenu);
            pCurMenu = pSubMenu;
            iCurItemCount = 0;
        }

        QString strStatus;
        if (pItem->state() >= KMachineState_Running)
            strStatus = "(R)";

        pSubMenu = new QMenu (QString ("&%1. %2 %3")
                              .arg ((iCurItemCount + 1) % 100).arg (pItem->name()).arg (strStatus));
        Assert (pSubMenu);
        pSubMenu->setIcon (pItem->osIcon());

        QAction *pAction = NULL;
        QVariant vID;
        vID.setValue (pItem->id());

        pSubMenu->menuAction()->setData (vID);
        connect (pSubMenu, SIGNAL (aboutToShow()), this, SLOT (showSubMenu()));
        connect (pSubMenu, SIGNAL (aboutToHide()), this, SLOT (hideSubMenu()));
        pCurMenu->addMenu (pSubMenu);
    }
}

VBoxVMItem* VBoxTrayIcon::GetItem (QObject* aObject)
{
    QAction *pAction = NULL;
    VBoxVMItem* pItem = NULL;
    if (pAction = qobject_cast<QAction*>(sender()))
    {
        QVariant v = pAction->data();
        if (v.canConvert<QUuid>() && mVMModel)
            pItem = mVMModel->itemById (qvariant_cast<QUuid>(v));
    }

    Assert (pItem);
    return pItem;
}

void VBoxTrayIcon::vmSettings()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmSettings (NULL, NULL, pItem->id());
}

void VBoxTrayIcon::vmDelete()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmDelete (pItem->id());
}

void VBoxTrayIcon::vmStart()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmStart (pItem->id());
}

void VBoxTrayIcon::vmDiscard()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmDiscard (pItem->id());
}

void VBoxTrayIcon::vmPause(bool aPause)
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmPause (aPause, pItem->id());
}

void VBoxTrayIcon::vmRefresh()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmRefresh (pItem->id());
}

void VBoxTrayIcon::vmShowLogs()
{
    VBoxVMItem* pItem = GetItem (sender());
    mParent->vmShowLogs (pItem->id());
}

#endif // VBOX_GUI_WITH_SYSTRAY

#include "VBoxSelectorWnd.moc"
