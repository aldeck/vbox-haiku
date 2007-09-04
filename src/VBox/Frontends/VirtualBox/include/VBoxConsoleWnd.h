/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxConsoleWnd class declaration
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBoxConsoleWnd_h__
#define __VBoxConsoleWnd_h__

#include "COMDefs.h"

#include <qmainwindow.h>

#include <qmap.h>
#include <qobjectlist.h>
#include <qcolor.h>
#include <qdialog.h>

#ifdef VBOX_WITH_DEBUGGER_GUI
# include <VBox/dbggui.h>
#endif
#ifdef Q_WS_MAC
# undef PAGE_SIZE
# undef PAGE_SHIFT
# include <Carbon/Carbon.h>
#endif

class QAction;
class QActionGroup;
class QHBox;
class QLabel;

class VBoxConsoleView;
class QIStateIndicator;

class VBoxUSBMenu;
class VBoxSwitchMenu;

class VBoxConsoleWnd : public QMainWindow
{
    Q_OBJECT

public:

    VBoxConsoleWnd (VBoxConsoleWnd **aSelf,
                     QWidget* aParent = 0, const char* aName = 0,
                     WFlags aFlags = WType_TopLevel);
    virtual ~VBoxConsoleWnd();

    bool openView (const CSession &session);
    void closeView();

    void refreshView();

    bool isTrueFullscreen() const { return mIsFullscreen; }

    bool isTrueSeamless() const { return mIsSeamless; }

    bool isManualResize() const { return mManualResize; }

    void setMouseIntegrationLocked (bool aDisabled);

    void popupMainMenu (bool aCenter);

    void installGuestAdditionsFrom (const QString &aSource);

#ifdef Q_WS_WIN32
    void setMask (const QRegion &aRegion);
#endif

public slots:

protected:

    // events
    bool event (QEvent *e);
    void resizeEvent (QResizeEvent *e);
    void closeEvent (QCloseEvent *e);
#if defined(Q_WS_X11)
    bool x11Event (XEvent *event);
#endif
#ifdef VBOX_WITH_DEBUGGER_GUI
    bool dbgCreated();
    void dbgDestroy();
    void dbgAdjustRelativePos();
#endif

protected slots:

private:

    enum /* Stuff */
    {
        FloppyStuff                 = 0x01,
        DVDStuff                    = 0x02,
        HardDiskStuff               = 0x04,
        PauseAction                 = 0x08,
        NetworkStuff                = 0x10,
        DisableMouseIntegrAction    = 0x20,
        Caption                     = 0x40,
        USBStuff                    = 0x80,
        VRDPStuff                   = 0x100,
        SharedFolderStuff           = 0x200,
        AllStuff                    = 0xFFFF,
    };

    void languageChange();

    void updateAppearanceOf (int element);

    void toggleFullscreenMode (bool, bool);

private slots:

    void finalizeOpenView();

    void activateUICustomizations();

    void vmFullscreen (bool on);
    void vmSeamless (bool on);
    void vmAutoresizeGuest (bool on);
    void vmAdjustWindow();

    void vmTypeCAD();
    void vmTypeCABS();
    void vmReset();
    void vmPause(bool);
    void vmACPIShutdown();
    void vmClose();
    void vmTakeSnapshot();
    void vmDisableMouseIntegr (bool);

    void devicesMountFloppyImage();
    void devicesUnmountFloppy();
    void devicesMountDVDImage();
    void devicesUnmountDVD();
    void devicesSwitchVrdp (bool);
    void devicesOpenSFDialog();
    void devicesInstallGuestAdditions();

    void prepareFloppyMenu();
    void prepareDVDMenu();
    void prepareNetworkMenu();

    void setDynamicMenuItemStatusTip (int aId);

    void captureFloppy (int aId);
    void captureDVD (int aId);
    void activateNetworkMenu (int aId);
    void switchUSB (int aId);

    void statusTipChanged (const QString &);
    void clearStatusBar();

    void showIndicatorContextMenu (QIStateIndicator *ind, QContextMenuEvent *e);

    void updateDeviceLights();
    void updateMachineState (CEnums::MachineState state);
    void updateMouseState (int state);
    void updateAdditionsState (const QString&, bool, bool);
    void updateNetworkAdarptersState();
    void updateUsbState();
    void updateMediaState (VBoxDefs::DiskType aType);
    void updateSharedFoldersState();

    void tryClose();

    void processGlobalSettingChange (const char *publicName, const char *name);

    void dbgShowStatistics();
    void dbgShowCommandLine();

    void onEnterFullscreen();
    void onExitFullscreen();

private:

    /** Popup version of the main menu */
    QPopupMenu *mMainMenu;

    QActionGroup *mRunningActions;
    QActionGroup *mRunningOrPausedActions;

    // Machine actions
    QAction *vmFullscreenAction;
    QAction *vmSeamlessAction;
    QAction *vmAutoresizeGuestAction;
    QAction *vmAdjustWindowAction;
    QAction *vmTypeCADAction;
#if defined(Q_WS_X11)
    QAction *vmTypeCABSAction;
#endif
    QAction *vmResetAction;
    QAction *vmPauseAction;
    QAction *vmACPIShutdownAction;
    QAction *vmCloseAction;
    QAction *vmTakeSnapshotAction;
    QAction *vmDisableMouseIntegrAction;

    // Devices actions
    QAction *devicesMountFloppyImageAction;
    QAction *devicesUnmountFloppyAction;
    QAction *devicesMountDVDImageAction;
    QAction *devicesUnmountDVDAction;
    QAction *devicesSwitchVrdpAction;
    QAction *devicesSFDialogAction;
    QAction *devicesInstallGuestToolsAction;

#ifdef VBOX_WITH_DEBUGGER_GUI
    // Debugger actions
    QAction *dbgStatisticsAction;
    QAction *dbgCommandLineAction;
#endif

    // Help actions
    QAction *helpWebAction;
    QAction *helpAboutAction;
    QAction *helpResetMessagesAction;

    // Machine popup menus
    VBoxSwitchMenu *vmAutoresizeMenu;
    VBoxSwitchMenu *vmDisMouseIntegrMenu;

    // Devices popup menus
    QPopupMenu *devicesMenu;
    QPopupMenu *devicesMountFloppyMenu;
    QPopupMenu *devicesMountDVDMenu;
    QPopupMenu *devicesSFMenu;
    QPopupMenu *devicesNetworkMenu;
    VBoxUSBMenu *devicesUSBMenu;
    VBoxSwitchMenu *devicesVRDPMenu;

    int devicesUSBMenuSeparatorId;
    int devicesVRDPMenuSeparatorId;
    int devicesSFMenuSeparatorId;

    bool waitForStatusBarChange;
    bool statusBarChangedInside;

#ifdef VBOX_WITH_DEBUGGER_GUI
    // Debugger popup menu
    QPopupMenu *dbgMenu;
#endif

    // Menu identifiers
    enum {
        vmMenuId = 1,
        devicesMenuId,
        devicesMountFloppyMenuId,
        devicesMountDVDMenuId,
        devicesUSBMenuId,
        devicesNetworkMenuId,
#ifdef VBOX_WITH_DEBUGGER_GUI
        dbgMenuId,
#endif
        helpMenuId,
    };

    CSession csession;

    // widgets
    VBoxConsoleView *console;
    QIStateIndicator *hd_light, *cd_light, *fd_light, *net_light, *usb_light, *sf_light;
    QIStateIndicator *mouse_state, *hostkey_state;
    QIStateIndicator *autoresize_state;
    QIStateIndicator *vrdp_state;
    QHBox *hostkey_hbox;
    QLabel *hostkey_name;

    QTimer *idle_timer;
    CEnums::MachineState machine_state;
    QString caption_prefix;

    bool no_auto_close : 1;

    QMap <int, CHostDVDDrive> hostDVDMap;
    QMap <int, CHostFloppyDrive> hostFloppyMap;

    QPoint normal_pos;
    QSize normal_size;
    QSize prev_min_size;
    QSize mPrevSize;
    QSize mAwaitingSize;

#ifdef Q_WS_WIN32
    QRegion mPrevRegion;
#endif

    // variables for dealing with true fullscreen
    bool mDoMaximize : 1;
    bool mManualResize : 1;
    bool mAllowOneResize : 1;
    bool mIsFullscreen : 1;
    bool mIsSeamless : 1;
    bool mIsSeamlessSupported : 1;
    int normal_wflags;
    bool was_max : 1;
    QObjectList hidden_children;
    int console_style;
    QColor erase_color;

    bool mIsOpenViewFinished : 1;
    bool mIsFirstTimeStarted : 1;
    bool mIsAutoSaveMedia : 1;

#ifdef VBOX_WITH_DEBUGGER_GUI
    // Debugger GUI
    PDBGGUI dbg_gui;
#endif

#ifdef Q_WS_MAC
    // Dock images.
    CGImageRef dockImgStateRunning;
    CGImageRef dockImgStatePaused;
    CGImageRef dockImgStateSaving;
    CGImageRef dockImgStateRestoring;
    CGImageRef dockImgBack75x75;
    CGImageRef dockImgBack100x75;
    CGImageRef dockImgOS;
#endif
};


class VBoxSharedFoldersSettings;
class VBoxSFDialog : public QDialog
{
    Q_OBJECT

public:

    VBoxSFDialog (QWidget*, CSession&);

protected slots:

    virtual void accept();

protected:

    void showEvent (QShowEvent*);

private:

    VBoxSharedFoldersSettings *mSettings;
    CSession &mSession;
};


#endif // __VBoxConsoleWnd_h__
