/* $Id$ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxDefs implementation
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
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

#include <VBoxDefs.h>

const QUuid QUuid_null;

const char* VBoxDefs::GUI_LastWindowPosition = "GUI/LastWindowPostion";
const char* VBoxDefs::GUI_LastWindowPosition_Max = "max";
const char* VBoxDefs::GUI_Fullscreen = "GUI/Fullscreen";
const char* VBoxDefs::GUI_Seamless = "GUI/Seamless";
const char* VBoxDefs::GUI_AutoresizeGuest = "GUI/AutoresizeGuest";
const char* VBoxDefs::GUI_FirstRun = "GUI/FirstRun";
const char* VBoxDefs::GUI_SaveMountedAtRuntime = "GUI/SaveMountedAtRuntime";
const char* VBoxDefs::GUI_ShowMiniToolBar = "GUI/ShowMiniToolBar";
const char* VBoxDefs::GUI_MiniToolBarAlignment = "GUI/MiniToolBarAlignment";
const char* VBoxDefs::GUI_MiniToolBarAutoHide = "GUI/MiniToolBarAutoHide";
const char* VBoxDefs::GUI_LastCloseAction = "GUI/LastCloseAction";
const char* VBoxDefs::GUI_SuppressMessages = "GUI/SuppressMessages";
const char* VBoxDefs::GUI_PermanentSharedFoldersAtRuntime = "GUI/PermanentSharedFoldersAtRuntime";
#ifdef Q_WS_X11
const char* VBoxDefs::GUI_LicenseKey = "GUI/LicenseAgreed";
#endif
const char* VBoxDefs::GUI_RegistrationDlgWinID = "GUI/RegistrationDlgWinID";
const char* VBoxDefs::GUI_RegistrationData = "GUI/SUNOnlineData";
const char* VBoxDefs::GUI_UpdateDlgWinID = "GUI/UpdateDlgWinID";
const char* VBoxDefs::GUI_UpdateDate = "GUI/UpdateDate";
const char* VBoxDefs::GUI_UpdateCheckCount = "GUI/UpdateCheckCount";
const char* VBoxDefs::GUI_LastVMSelected = "GUI/LastVMSelected";
const char* VBoxDefs::GUI_InfoDlgState = "GUI/InfoDlgState";
const char* VBoxDefs::GUI_RenderMode = "GUI/RenderMode";
#ifdef VBOX_GUI_WITH_SYSTRAY
const char* VBoxDefs::GUI_TrayIconWinID = "GUI/TrayIcon/WinID";
const char* VBoxDefs::GUI_MainWindowCount = "GUI/MainWindowCount";
#endif
#ifdef Q_WS_MAC
const char* VBoxDefs::GUI_RealtimeDockIconUpdateEnabled = "GUI/RealtimeDockIconUpdateEnabled";
const char* VBoxDefs::GUI_PresentationModeEnabled = "GUI/PresentationModeEnabled";
#endif /* Q_WS_MAC */
const char* VBoxDefs::GUI_PassCAD = "GUI/PassCAD";
const char* VBoxDefs::GUI_Export_StorageType = "GUI/Export/StorageType";
const char* VBoxDefs::GUI_Export_Username = "GUI/Export/Username";
const char* VBoxDefs::GUI_Export_Hostname = "GUI/Export/Hostname";
const char* VBoxDefs::GUI_Export_Bucket = "GUI/Export/Bucket";
const char* VBoxDefs::GUI_PreventBetaWarning = "GUI/PreventBetaWarning";
#ifdef VBOX_WITH_VIDEOHWACCEL
const char* VBoxDefs::GUI_Accelerate2D_StretchLinear = "GUI/Accelerate2D/StretchLinear";
const char* VBoxDefs::GUI_Accelerate2D_PixformatYV12 = "GUI/Accelerate2D/PixformatYV12";
const char* VBoxDefs::GUI_Accelerate2D_PixformatUYVY = "GUI/Accelerate2D/PixformatUYVY";
const char* VBoxDefs::GUI_Accelerate2D_PixformatYUY2 = "GUI/Accelerate2D/PixformatYUY2";
const char* VBoxDefs::GUI_Accelerate2D_PixformatAYUV = "GUI/Accelerate2D/PixformatAYUV";
#endif

