/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxConsoleWnd class declaration
 */

/*
 * Copyright (C) 2006-2009 Sun Microsystems, Inc.
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

#ifndef __VBoxConsoleWnd_h__
#define __VBoxConsoleWnd_h__

/* Global includes */
#include <QColor>
#include <QDialog>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPointer>

/* Local includes */
#include "COMDefs.h"
#include "QIWithRetranslateUI.h"
#include "VBoxProblemReporter.h"
#include "VBoxHelpActions.h"

#ifdef VBOX_WITH_DEBUGGER_GUI
# include <VBox/dbggui.h>
#endif
#ifdef Q_WS_MAC
# include "VBoxUtils.h"
# include <ApplicationServices/ApplicationServices.h>
# ifndef QT_MAC_USE_COCOA
#  include <Carbon/Carbon.h>
# endif /* !QT_MAC_USE_COCOA */
#endif

/* Global forwards */
class QAction;
class QActionGroup;
class QLabel;
class QSpacerItem;
class QIWidgetValidator;

/* Local forwards */
class QIMenu;
class QIStateIndicator;
class VBoxChangeDockIconUpdateEvent;
class VBoxChangePresentationModeEvent;
class VBoxConsoleView;
class VBoxMiniToolBar;
class VBoxSwitchMenu;
class VBoxUSBMenu;

class VBoxConsoleWnd : public QIWithRetranslateUI2 <QMainWindow>
{
    Q_OBJECT;

public:

    VBoxConsoleWnd (VBoxConsoleWnd **aSelf, QWidget* aParent = 0, Qt::WindowFlags aFlags = Qt::Window);
    virtual ~VBoxConsoleWnd();

    bool isWindowMaximized()
    {
#ifdef Q_WS_MAC
        /* On Mac OS X we didn't really jump to the fullscreen mode but
         * maximize the window. This situation has to be considered when
         * checking for maximized or fullscreen mode. */
        return !isTrueSeamless() && ::darwinIsWindowMaximized(this);
#else /* Q_WS_MAC */
        return QMainWindow::isMaximized();
#endif /* Q_WS_MAC */
    }
    bool isWindowFullScreen() const
    {
#ifdef Q_WS_MAC
        /* On Mac OS X we didn't really jump to the fullscreen mode but
         * maximize the window. This situation has to be considered when
         * checking for maximized or fullscreen mode. */
        return isTrueFullscreen() || isTrueSeamless();
#else /* Q_WS_MAC */
        return QMainWindow::isFullScreen();
#endif /* Q_WS_MAC */
    }
    bool isTrueFullscreen() const { return mIsFullscreen; }
    bool isTrueSeamless() const { return mIsSeamless; }

    KMachineState machineState() const { return mMachineState; }

    bool openView (const CSession &aSession);

    void setMouseIntegrationLocked (bool aDisabled);

    void popupMainMenu (bool aCenter);

    void setMask (const QRegion &aRegion);
    void clearMask();

    /* informs that the guest display is resized */
    void onDisplayResize (ulong aWidth, ulong aHeight);

    /* used for obtaining the extradata settings */
    CSession &session() { return mSession; }
signals:

    void closing();

protected:

    bool event (QEvent *aEvent);
    void closeEvent (QCloseEvent *aEvent);
#ifdef Q_WS_X11
    bool x11Event (XEvent *aEvent);
#endif

    void retranslateUi();

private slots:

    void finalizeOpenView();
    void tryClose();

    void vmFullscreen (bool aOn);
    void vmSeamless (bool aOn);
    void vmAutoresizeGuest (bool aOn);
    void vmAdjustWindow();
    void vmDisableMouseIntegration (bool aOff);
    void vmTypeCAD();
#ifdef Q_WS_X11
    void vmTypeCABS();
#endif
    void vmTakeSnapshot();
    void vmShowInfoDialog();
    void vmReset();
    void vmPause (bool aOn);
    void vmACPIShutdown();
    void vmClose();

    void devicesSwitchVrdp (bool aOn);
    void devicesOpenNetworkDialog();
    void devicesOpenSFDialog();
    void devicesInstallGuestAdditions();

    void prepareStorageMenu();
    void prepareNetworkMenu();
    void prepareSFMenu();

    void mountMedium();
    void switchUSB (QAction *aAction);

    void showIndicatorContextMenu (QIStateIndicator *aInd, QContextMenuEvent *aEvent);

    void updateDeviceLights();
    void updateNetworkIPs();
    void updateMachineState (KMachineState aState);
    void updateMouseState (int aState);
    void updateAdditionsState (const QString &aVersion, bool aActive,
                               bool aSeamlessSupported, bool aGraphicsSupported);
    void updateNetworkAdaptersState();
    void updateUsbState();
    void updateMediaDriveState (VBoxDefs::MediumType aType);
    void updateSharedFoldersState();

    void onExitFullscreen();
    void unlockActionsSwitch();

    void mtExitMode();
    void mtCloseVM();
    void mtMaskUpdate();

    void changeDockIconUpdate (const VBoxChangeDockIconUpdateEvent &aEvent);
    void changePresentationMode (const VBoxChangePresentationModeEvent &aEvent);
    void processGlobalSettingChange (const char *aPublicName, const char *aName);

    void installGuestAdditionsFrom (const QString &aSource);

    void sltDownloaderUserManualEmbed();

#ifdef RT_OS_DARWIN /* Stupid moc doesn't recognize Q_WS_MAC */
    void sltDockPreviewModeChanged(QAction *pAction);
#endif /* RT_OS_DARWIN */

#ifdef VBOX_WITH_DEBUGGER_GUI
    void dbgPrepareDebugMenu();
    void dbgShowStatistics();
    void dbgShowCommandLine();
    void dbgLoggingToggled (bool aBool);
#endif

private:

    enum /* Stuff */
    {
        HardDiskStuff               = 0x01,
        DVDStuff                    = 0x02,
        FloppyStuff                 = 0x04,
        PauseAction                 = 0x08,
        NetworkStuff                = 0x10,
        DisableMouseIntegrAction    = 0x20,
        Caption                     = 0x40,
        USBStuff                    = 0x80,
        VRDPStuff                   = 0x100,
        SharedFolderStuff           = 0x200,
        VirtualizationStuff         = 0x400,
        AllStuff                    = 0xFFFF,
    };

    void checkRequiredFeatures();
    void activateUICustomizations();

    void updateAppearanceOf (int aElement);

    bool toggleFullscreenMode (bool aOn, bool aSeamless);
    void switchToFullscreen (bool aOn, bool aSeamless);
    void setViewInSeamlessMode (const QRect &aTargetRect);

    void closeView();

#ifdef VBOX_WITH_DEBUGGER_GUI
    bool dbgCreated();
    void dbgDestroy();
    void dbgAdjustRelativePos();
#endif

    /* COM Variables */
    CSession mSession;

    /* Machine State */
    KMachineState mMachineState;

    /* Window Variables */
    QString mCaptionPrefix;
    int mConsoleStyle;

    /* Menu items */
    QIMenu *mMainMenu;
    QMenu *mVMMenu;
    QMenu *mVMMenuMini;
    QMenu *mDevicesMenu;
    QMenu *mDevicesCDMenu;
    QMenu *mDevicesFDMenu;
    QMenu *mDevicesNetworkMenu;
    QMenu *mDevicesSFMenu;
    VBoxUSBMenu *mDevicesUSBMenu;
    VBoxSwitchMenu *mVmDisMouseIntegrMenu;
#if 0 /* todo: allow to setup */
    VBoxSwitchMenu *mDevicesVRDPMenu;
    VBoxSwitchMenu *mVmAutoresizeMenu;
#endif
#ifdef VBOX_WITH_DEBUGGER_GUI
    QMenu *mDbgMenu;
#endif
    QMenu *mHelpMenu;

    QActionGroup *mRunningActions;
    QActionGroup *mRunningOrPausedActions;

    /* Machine actions */
    QAction *mVmFullscreenAction;
    QAction *mVmSeamlessAction;
    QAction *mVmAutoresizeGuestAction;
    QAction *mVmAdjustWindowAction;
    QAction *mVmDisableMouseIntegrAction;
    QAction *mVmTypeCADAction;
#ifdef Q_WS_X11
    QAction *mVmTypeCABSAction;
#endif
    QAction *mVmTakeSnapshotAction;
    QAction *mVmShowInformationDlgAction;
    QAction *mVmResetAction;
    QAction *mVmPauseAction;
    QAction *mVmACPIShutdownAction;
    QAction *mVmCloseAction;

    /* Devices actions */
    QAction *mDevicesNetworkDialogAction;
    QAction *mDevicesSFDialogAction;
    QAction *mDevicesSwitchVrdpSeparator;
    QAction *mDevicesSwitchVrdpAction;
    QAction *mDevicesInstallGuestToolsAction;

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* Debugger actions */
    QAction *mDbgStatisticsAction;
    QAction *mDbgCommandLineAction;
    QAction *mDbgLoggingAction;
#endif

    /* Help actions */
    VBoxHelpActions mHelpActions;

    /* Widgets */
    VBoxConsoleView *mConsole;
    VBoxMiniToolBar *mMiniToolBar;
#ifdef VBOX_WITH_DEBUGGER_GUI
    /** The handle to the debugger gui. */
    PDBGGUI mDbgGui;
    /** The virtual method table for the debugger GUI. */
    PCDBGGUIVT mDbgGuiVT;
#endif

#ifdef Q_WS_MAC
    QMenu *m_pDockMenu;
    QMenu *m_pDockSettingsMenu;
    QAction *m_pDockDisablePreview;
    QAction *m_pDockEnablePreviewMonitor;
#endif /* Q_WS_MAC */

    /* Timer to update LEDs */
    QTimer *mIdleTimer;
    QTimer *mNetworkTimer;

    /* LEDs */
    QIStateIndicator *mHDLed;
    QIStateIndicator *mCDLed;
#if 0 /* todo: allow to setup */
    QIStateIndicator *mFDLed;
#endif
    QIStateIndicator *mNetLed;
    QIStateIndicator *mUSBLed;
    QIStateIndicator *mSFLed;
    QIStateIndicator *mVirtLed;
    QIStateIndicator *mMouseLed;
    QIStateIndicator *mHostkeyLed;
    QWidget *mHostkeyLedContainer;
    QLabel *mHostkeyName;
#if 0 /* todo: allow to setup */
    QIStateIndicator *mVrdpLed;
    QIStateIndicator *mAutoresizeLed;
#endif

    /* Normal Mode */
    QRect mNormalGeo;

    /* Fullscreen/Seamless Mode */
    QList < QPointer <QWidget> > mHiddenChildren;
    QSpacerItem *mShiftingSpacerLeft;
    QSpacerItem *mShiftingSpacerTop;
    QSpacerItem *mShiftingSpacerRight;
    QSpacerItem *mShiftingSpacerBottom;
    QPalette mErasePalette;
    QSize mPrevMinSize;
    QSize mMaskShift;
    QRegion mStrictedRegion;
#ifdef Q_WS_WIN
    QRegion mPrevRegion;
#endif
#ifdef Q_WS_MAC
    //QRegion mCurrRegion;
# ifndef QT_MAC_USE_COCOA
    //EventHandlerRef mDarwinRegionEventHandlerRef;
# endif
    /* For seamless maximizing */
    QRect mNormalGeometry;
    Qt::WindowFlags mSavedFlags;
    /* For the fade effect if the the window goes fullscreen */
    CGDisplayFadeReservationToken mFadeToken;
#endif

    /* Different bool flags */
    bool mIsOpenViewFinished : 1;
    bool mIsFirstTimeStarted : 1;
    bool mIsAutoSaveMedia : 1;
    bool mNoAutoClose : 1;
    bool mIsFullscreen : 1;
    bool mIsSeamless : 1;
    bool mIsSeamlessSupported : 1;
    bool mIsGraphicsSupported : 1;
    bool mIsWaitingModeResize : 1;
    bool mWasMax : 1;
};

/* We want to make the first action highlighted but not
 * selected, but Qt makes the both or neither one of this,
 * so, just move the focus to the next eligible object,
 * which will be the first menu action. This little
 * subclass made only for that purpose. */
class QIMenu : public QMenu
{
    Q_OBJECT;

public:

    QIMenu (QWidget *aParent) : QMenu (aParent) {}

    void selectFirstAction() { QMenu::focusNextChild(); }
};

class VBoxSettingsPage;
class VBoxNetworkDialog : public QIWithRetranslateUI <QDialog>
{
    Q_OBJECT;

public:

    VBoxNetworkDialog (QWidget *aParent, CSession &aSession);

protected:

    void retranslateUi();

protected slots:

    virtual void accept();

protected:

    void showEvent (QShowEvent *aEvent);

private:

    VBoxSettingsPage *mSettings;
    CSession &mSession;
};

class VBoxVMSettingsSF;
class VBoxSFDialog : public QIWithRetranslateUI <QDialog>
{
    Q_OBJECT;

public:

    VBoxSFDialog (QWidget *aParent, CSession &aSession);

protected:

    void retranslateUi();

protected slots:

    virtual void accept();

protected:

    void showEvent (QShowEvent *aEvent);

private:

    VBoxVMSettingsSF *mSettings;
    CSession &mSession;
};

#endif // __VBoxConsoleWnd_h__

