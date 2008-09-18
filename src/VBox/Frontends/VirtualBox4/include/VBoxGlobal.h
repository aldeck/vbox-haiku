/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxGlobal class declaration
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

#ifndef __VBoxGlobal_h__
#define __VBoxGlobal_h__

#include "COMDefs.h"

#include "VBoxGlobalSettings.h"

/* Qt includes */
#include <QApplication>
#include <QLayout>
#include <QHash>
#include <QPixmap>
#include <QMenu>
#include <QStyle>
#include <QProcess>

class QAction;
class QLabel;
class QToolButton;

// Auxiliary types
////////////////////////////////////////////////////////////////////////////////

/** Simple media descriptor type. */
struct VBoxMedia
{
    enum Status { Unknown, Ok, Error, Inaccessible };

    VBoxMedia() : type (VBoxDefs::InvalidType), status (Ok) {}

    VBoxMedia (const CUnknown &d, VBoxDefs::DiskType t, Status s)
        : disk (d), type (t), status (s) {}

    CUnknown disk;
    VBoxDefs::DiskType type;
    Status status;
};

typedef QList <VBoxMedia> VBoxMediaList;

// VirtualBox callback events
////////////////////////////////////////////////////////////////////////////////

class VBoxMachineStateChangeEvent : public QEvent
{
public:
    VBoxMachineStateChangeEvent (const QUuid &aId, KMachineState aState)
        : QEvent ((QEvent::Type) VBoxDefs::MachineStateChangeEventType)
        , id (aId), state (aState)
        {}

    const QUuid id;
    const KMachineState state;
};

class VBoxMachineDataChangeEvent : public QEvent
{
public:
    VBoxMachineDataChangeEvent (const QUuid &aId)
        : QEvent ((QEvent::Type) VBoxDefs::MachineDataChangeEventType)
        , id (aId)
        {}

    const QUuid id;
};

class VBoxMachineRegisteredEvent : public QEvent
{
public:
    VBoxMachineRegisteredEvent (const QUuid &aId, bool aRegistered)
        : QEvent ((QEvent::Type) VBoxDefs::MachineRegisteredEventType)
        , id (aId), registered (aRegistered)
        {}

    const QUuid id;
    const bool registered;
};

class VBoxSessionStateChangeEvent : public QEvent
{
public:
    VBoxSessionStateChangeEvent (const QUuid &aId, KSessionState aState)
        : QEvent ((QEvent::Type) VBoxDefs::SessionStateChangeEventType)
        , id (aId), state (aState)
        {}

    const QUuid id;
    const KSessionState state;
};

class VBoxSnapshotEvent : public QEvent
{
public:

    enum What { Taken, Discarded, Changed };

    VBoxSnapshotEvent (const QUuid &aMachineId, const QUuid &aSnapshotId,
                       What aWhat)
        : QEvent ((QEvent::Type) VBoxDefs::SnapshotEventType)
        , what (aWhat)
        , machineId (aMachineId), snapshotId (aSnapshotId)
        {}

    const What what;

    const QUuid machineId;
    const QUuid snapshotId;
};

class VBoxCanShowRegDlgEvent : public QEvent
{
public:
    VBoxCanShowRegDlgEvent (bool aCanShow)
        : QEvent ((QEvent::Type) VBoxDefs::CanShowRegDlgEventType)
        , mCanShow (aCanShow)
        {}

    const bool mCanShow;
};

class VBoxCanShowUpdDlgEvent : public QEvent
{
public:
    VBoxCanShowUpdDlgEvent (bool aCanShow)
        : QEvent ((QEvent::Type) VBoxDefs::CanShowUpdDlgEventType)
        , mCanShow (aCanShow)
        {}

    const bool mCanShow;
};

class VBoxChangeGUILanguageEvent : public QEvent
{
public:
    VBoxChangeGUILanguageEvent (QString aLangId)
        : QEvent ((QEvent::Type) VBoxDefs::ChangeGUILanguageEventType)
        , mLangId (aLangId)
        {}

    const QString mLangId;
};

class Process : public QProcess
{
    Q_OBJECT;

public:

    static QByteArray singleShot (const QString &aProcessName,
                                  int aTimeout = 5000
                                  /* wait for data maximum 5 seconds */)
    {
        /* Why is it really needed is because of Qt4.3 bug with QProcess.
         * This bug is about QProcess sometimes (~70%) do not receive
         * notification about process was finished, so this makes
         * 'bool QProcess::waitForFinished (int)' block the GUI thread and
         * never dismissed with 'true' result even if process was really
         * started&finished. So we just waiting for some information
         * on process output and destroy the process with force. Due to
         * QProcess::~QProcess() has the same 'waitForFinished (int)' blocker
         * we have to change process state to QProcess::NotRunning. */

        QByteArray result;
        Process process;
        process.start (aProcessName);
        bool firstShotReady = process.waitForReadyRead (aTimeout);
        if (firstShotReady)
            result = process.readAllStandardOutput();
        process.setProcessState (QProcess::NotRunning);
        return result;
    }

protected:

    Process (QWidget *aParent = 0) : QProcess (aParent) {}
};

// VBoxGlobal class
////////////////////////////////////////////////////////////////////////////////

class VBoxSelectorWnd;
class VBoxConsoleWnd;
class VBoxRegistrationDlg;
class VBoxUpdateDlg;

class VBoxGlobal : public QObject
{
    Q_OBJECT

public:

    static VBoxGlobal &instance();

    bool isValid() { return mValid; }

    QString versionString() { return verString; }

    CVirtualBox virtualBox() const { return mVBox; }

    const VBoxGlobalSettings &settings() const { return gset; }
    bool setSettings (const VBoxGlobalSettings &gs);

    VBoxSelectorWnd &selectorWnd();
    VBoxConsoleWnd &consoleWnd();

    /* main window handle storage */
    void setMainWindow (QWidget *aMainWindow) { mMainWindow = aMainWindow; }
    QWidget *mainWindow() const { return mMainWindow; }


    bool isVMConsoleProcess() const { return !vmUuid.isNull(); }
    QUuid managedVMUuid() const { return vmUuid; }

    VBoxDefs::RenderMode vmRenderMode() const { return vm_render_mode; }
    const char *vmRenderModeStr() const { return vm_render_mode_str; }

#ifdef VBOX_WITH_DEBUGGER_GUI
    bool isDebuggerEnabled() const { return mDbgEnabled; }
    bool isDebuggerAutoShowEnabled() const { return mDbgAutoShow; }
    RTLDRMOD getDebuggerModule() const { return mhVBoxDbg; }
#endif

    /* VBox enum to/from string/icon/color convertors */

    QStringList vmGuestOSTypeDescriptions() const;
    QList<QPixmap> vmGuestOSTypeIcons (int aHorizonalMargin, int aVerticalMargin) const;
    CGuestOSType vmGuestOSType (int aIndex) const;
    int vmGuestOSTypeIndex (const QString &aId) const;
    QPixmap vmGuestOSTypeIcon (const QString &aId) const;
    QString vmGuestOSTypeDescription (const QString &aId) const;

    QPixmap toIcon (KMachineState s) const
    {
        QPixmap *pm = mStateIcons.value (s);
        AssertMsg (pm, ("Icon for VM state %d must be defined", s));
        return pm ? *pm : QPixmap();
    }

    const QColor &toColor (KMachineState s) const
    {
        static const QColor none;
        AssertMsg (vm_state_color.value (s), ("No color for %d", s));
        return vm_state_color.value (s) ? *vm_state_color.value(s) : none;
    }

    QString toString (KMachineState s) const
    {
        AssertMsg (!machineStates.value (s).isNull(), ("No text for %d", s));
        return machineStates.value (s);
    }

    QString toString (KSessionState s) const
    {
        AssertMsg (!sessionStates.value (s).isNull(), ("No text for %d", s));
        return sessionStates.value (s);
    }

    /**
     * Returns a string representation of the given KStorageBus enum value.
     * Complementary to #toStorageBusType (const QString &) const.
     */
    QString toString (KStorageBus aBus) const
    {
        AssertMsg (!storageBuses.value (aBus).isNull(), ("No text for %d", aBus));
        return storageBuses [aBus];
    }

    /**
     * Returns a KStorageBus enum value corresponding to the given string
     * representation. Complementary to #toString (KStorageBus) const.
     */
    KStorageBus toStorageBusType (const QString &aBus) const
    {
        QStringVector::const_iterator it =
            qFind (storageBuses.begin(), storageBuses.end(), aBus);
        AssertMsg (it != storageBuses.end(), ("No value for {%s}", aBus.toLatin1().constData()));
        return KStorageBus (it - storageBuses.begin());
    }

    QString toString (KStorageBus aBus, LONG aChannel) const;
    LONG toStorageChannel (KStorageBus aBus, const QString &aChannel) const;

    QString toString (KStorageBus aBus, LONG aChannel, LONG aDevice) const;
    LONG toStorageDevice (KStorageBus aBus, LONG aChannel, const QString &aDevice) const;

    QString toFullString (KStorageBus aBus, LONG aChannel, LONG aDevice) const;

    QString toString (KHardDiskType t) const
    {
        AssertMsg (!diskTypes.value (t).isNull(), ("No text for %d", t));
        return diskTypes.value (t);
    }

    QString toString (KHardDiskStorageType t) const
    {
        AssertMsg (!diskStorageTypes.value (t).isNull(), ("No text for %d", t));
        return diskStorageTypes.value (t);
    }

    QString toString (KVRDPAuthType t) const
    {
        AssertMsg (!vrdpAuthTypes.value (t).isNull(), ("No text for %d", t));
        return vrdpAuthTypes.value (t);
    }

    QString toString (KPortMode t) const
    {
        AssertMsg (!portModeTypes.value (t).isNull(), ("No text for %d", t));
        return portModeTypes.value (t);
    }

    QString toString (KUSBDeviceFilterAction t) const
    {
        AssertMsg (!usbFilterActionTypes.value (t).isNull(), ("No text for %d", t));
        return usbFilterActionTypes.value (t);
    }

    QString toString (KClipboardMode t) const
    {
        AssertMsg (!clipboardTypes.value (t).isNull(), ("No text for %d", t));
        return clipboardTypes.value (t);
    }

    KClipboardMode toClipboardModeType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (clipboardTypes.begin(), clipboardTypes.end(), s);
        AssertMsg (it != clipboardTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KClipboardMode (it - clipboardTypes.begin());
    }

    QString toString (KIDEControllerType t) const
    {
        AssertMsg (!ideControllerTypes.value (t).isNull(), ("No text for %d", t));
        return ideControllerTypes.value (t);
    }

    KIDEControllerType toIDEControllerType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (ideControllerTypes.begin(), ideControllerTypes.end(), s);
        AssertMsg (it != ideControllerTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KIDEControllerType (it - ideControllerTypes.begin());
    }

    KVRDPAuthType toVRDPAuthType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (vrdpAuthTypes.begin(), vrdpAuthTypes.end(), s);
        AssertMsg (it != vrdpAuthTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KVRDPAuthType (it - vrdpAuthTypes.begin());
    }

    KPortMode toPortMode (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (portModeTypes.begin(), portModeTypes.end(), s);
        AssertMsg (it != portModeTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KPortMode (it - portModeTypes.begin());
    }

    KUSBDeviceFilterAction toUSBDevFilterAction (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (usbFilterActionTypes.begin(), usbFilterActionTypes.end(), s);
        AssertMsg (it != usbFilterActionTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KUSBDeviceFilterAction (it - usbFilterActionTypes.begin());
    }

    /**
     *  Similar to toString (KHardDiskType), but returns 'Differencing'
     *  for normal hard disks that have a parent hard disk.
     */
    QString hardDiskTypeString (const CHardDisk &aHD) const
    {
        if (!aHD.GetParent().isNull())
        {
            Assert (aHD.GetType() == KHardDiskType_Normal);
            return tr ("Differencing", "hard disk");
        }
        return toString (aHD.GetType());
    }

    QString toString (KDeviceType t) const
    {
        AssertMsg (!deviceTypes.value (t).isNull(), ("No text for %d", t));
        return deviceTypes.value (t);
    }

    KDeviceType toDeviceType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (deviceTypes.begin(), deviceTypes.end(), s);
        AssertMsg (it != deviceTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KDeviceType (it - deviceTypes.begin());
    }

    QStringList deviceTypeStrings() const;

    QString toString (KAudioDriverType t) const
    {
        AssertMsg (!audioDriverTypes.value (t).isNull(), ("No text for %d", t));
        return audioDriverTypes.value (t);
    }

    KAudioDriverType toAudioDriverType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (audioDriverTypes.begin(), audioDriverTypes.end(), s);
        AssertMsg (it != audioDriverTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KAudioDriverType (it - audioDriverTypes.begin());
    }

    QString toString (KAudioControllerType t) const
    {
        AssertMsg (!audioControllerTypes.value (t).isNull(), ("No text for %d", t));
        return audioControllerTypes.value (t);
    }

    KAudioControllerType toAudioControllerType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (audioControllerTypes.begin(), audioControllerTypes.end(), s);
        AssertMsg (it != audioControllerTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KAudioControllerType (it - audioControllerTypes.begin());
    }

    QString toString (KNetworkAdapterType t) const
    {
        AssertMsg (!networkAdapterTypes.value (t).isNull(), ("No text for %d", t));
        return networkAdapterTypes.value (t);
    }

    KNetworkAdapterType toNetworkAdapterType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (networkAdapterTypes.begin(), networkAdapterTypes.end(), s);
        AssertMsg (it != networkAdapterTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KNetworkAdapterType (it - networkAdapterTypes.begin());
    }

    QString toString (KNetworkAttachmentType t) const
    {
        AssertMsg (!networkAttachmentTypes.value (t).isNull(), ("No text for %d", t));
        return networkAttachmentTypes.value (t);
    }

    KNetworkAttachmentType toNetworkAttachmentType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (networkAttachmentTypes.begin(), networkAttachmentTypes.end(), s);
        AssertMsg (it != networkAttachmentTypes.end(), ("No value for {%s}", s.toLatin1().constData()));
        return KNetworkAttachmentType (it - networkAttachmentTypes.begin());
    }

    QString toString (KUSBDeviceState aState) const
    {
        AssertMsg (!USBDeviceStates.value (aState).isNull(), ("No text for %d", aState));
        return USBDeviceStates.value (aState);
    }

    QStringList COMPortNames() const;
    QString toCOMPortName (ulong aIRQ, ulong aIOBase) const;
    bool toCOMPortNumbers (const QString &aName, ulong &aIRQ, ulong &aIOBase) const;

    QStringList LPTPortNames() const;
    QString toLPTPortName (ulong aIRQ, ulong aIOBase) const;
    bool toLPTPortNumbers (const QString &aName, ulong &aIRQ, ulong &aIOBase) const;

    QPixmap snapshotIcon (bool online) const
    {
        return online ? mOnlineSnapshotIcon : mOfflineSnapshotIcon;
    }

    /* details generators */

    QString details (const CHardDisk &aHD, bool aPredict = false,
                     bool aDoRefresh = true);

    QString details (const CUSBDevice &aDevice) const;
    QString toolTip (const CUSBDevice &aDevice) const;
    QString toolTip (const CUSBDeviceFilter &aFilter) const;

    QString prepareFileNameForHTML (const QString &fn) const;

    QString detailsReport (const CMachine &m, bool isNewVM, bool withLinks,
                           bool aDoRefresh = true);

    QString platformInfo();

    /* VirtualBox helpers */

#if defined(Q_WS_X11) && !defined(VBOX_OSE)
    bool showVirtualBoxLicense();
#endif

    void checkForAutoConvertedSettings();

    CSession openSession (const QUuid &aId, bool aExisting = false);

    /** Shortcut to openSession (aId, true). */
    CSession openExistingSession (const QUuid &aId) { return openSession (aId, true); }

    bool startMachine (const QUuid &id);

    void startEnumeratingMedia();

    /**
     *  Returns a list of all currently registered media. This list is used
     *  to globally track the accessiblity state of all media on a dedicated
     *  thread. This the list is initially empty (before the first enumeration
     *  process is started using #startEnumeratingMedia()).
     */
    const VBoxMediaList &currentMediaList() const { return media_list; }

    /** Returns true if the media enumeration is in progress. */
    bool isMediaEnumerationStarted() const { return media_enum_thread != NULL; }

    void addMedia (const VBoxMedia &);
    void updateMedia (const VBoxMedia &);
    void removeMedia (VBoxDefs::DiskType, const QUuid &);

    bool findMedia (const CUnknown &, VBoxMedia &) const;

    /* various helpers */

    QString languageName() const;
    QString languageCountry() const;
    QString languageNameEnglish() const;
    QString languageCountryEnglish() const;
    QString languageTranslators() const;

    void retranslateUi();

    /** @internal made public for internal purposes */
    void cleanup();

    /* public static stuff */

    static bool isDOSType (const QString &aOSTypeId);

    static void adoptLabelPixmap (QLabel *);

    static QString languageId();
    static void loadLanguage (const QString &aLangId = QString::null);
    QString helpFile() const;

    static QIcon iconSet (const char *aNormal,
                          const char *aDisabled = NULL,
                          const char *aActive = NULL);
    static QIcon iconSetEx (const char *aNormal, const char *aSmallNormal,
                            const char *aDisabled = NULL,
                            const char *aSmallDisabled = NULL,
                            const char *aActive = NULL,
                            const char *aSmallActive = NULL);

    static QIcon standardIcon (QStyle::StandardPixmap aStandard, QWidget *aWidget = NULL);

    static void setTextLabel (QToolButton *aToolButton, const QString &aTextLabel);

    static QRect normalizeGeometry (const QRect &aRect, const QRect &aBoundRect,
                                    bool aCanResize = true);

    static void centerWidget (QWidget *aWidget, QWidget *aRelative,
                              bool aCanResize = true);

    static QChar decimalSep();
    static QString sizeRegexp();

    static quint64 parseSize (const QString &);
    static QString formatSize (quint64, int aMode = 0);

    static QString highlight (const QString &aStr, bool aToolTip = false);

    static QString systemLanguageId();

    static QString getExistingDirectory (const QString &aDir, QWidget *aParent,
                                         const QString &aCaption = QString::null,
                                         bool aDirOnly = TRUE,
                                         bool resolveSymlinks = TRUE);

    static QString getOpenFileName (const QString &, const QString &, QWidget*,
                                    const QString &, QString *defaultFilter = 0,
                                    bool resolveSymLinks = true);

    static QString getFirstExistingDir (const QString &);

    static bool activateWindow (WId aWId, bool aSwitchDesktop = true);

    static QString removeAccelMark (const QString &aText);

    static QString insertKeyToActionText (const QString &aText, const QString &aKey);
    static QString extractKeyFromActionText (const QString &aText);

    static QWidget *findWidget (QWidget *aParent, const char *aName,
                                const char *aClassName = NULL,
                                bool aRecursive = false);

    static QList< QPair<QString, QString> > HDDBackends();

    /* Qt 4.2.0 support function */
    static inline void setLayoutMargin (QLayout *aLayout, int aMargin)
    {
#if QT_VERSION < 0x040300
        /* Deprecated since > 4.2 */
        aLayout->setMargin (aMargin);
#else
        /* New since > 4.2 */
        aLayout->setContentsMargins (aMargin, aMargin, aMargin, aMargin);
#endif
    }

signals:

    /**
     *  Emitted at the beginning of the enumeration process started
     *  by #startEnumeratingMedia().
     */
    void mediaEnumStarted();

    /**
     *  Emitted when a new media item from the list has updated
     *  its accessibility state.
     */
    void mediaEnumerated (const VBoxMedia &aMedia, int aIndex);

    /**
     *  Emitted at the end of the enumeration process started
     *  by #startEnumeratingMedia(). The @a aList argument is passed for
     *  convenience, it is exactly the same as returned by #currentMediaList().
     */
    void mediaEnumFinished (const VBoxMediaList &aList);

    /** Emitted when a new media is added using #addMedia(). */
    void mediaAdded (const VBoxMedia &);

    /** Emitted when the media is updated using #updateMedia(). */
    void mediaUpdated (const VBoxMedia &);

    /** Emitted when the media is removed using #removeMedia(). */
    void mediaRemoved (VBoxDefs::DiskType, const QUuid &);

    /* signals emitted when the VirtualBox callback is called by the server
     * (not that currently these signals are emitted only when the application
     * is the in the VM selector mode) */

    void machineStateChanged (const VBoxMachineStateChangeEvent &e);
    void machineDataChanged (const VBoxMachineDataChangeEvent &e);
    void machineRegistered (const VBoxMachineRegisteredEvent &e);
    void sessionStateChanged (const VBoxSessionStateChangeEvent &e);
    void snapshotChanged (const VBoxSnapshotEvent &e);

    void canShowRegDlg (bool aCanShow);
    void canShowUpdDlg (bool aCanShow);

public slots:

    bool openURL (const QString &aURL);

    void showRegistrationDialog (bool aForce = true);
    void showUpdateDialog (bool aForce = true);

protected:

    bool event (QEvent *e);
    bool eventFilter (QObject *, QEvent *);

private:

    VBoxGlobal();
    ~VBoxGlobal();

    void init();

    bool mValid;

    CVirtualBox mVBox;

    VBoxGlobalSettings gset;

    VBoxSelectorWnd *mSelectorWnd;
    VBoxConsoleWnd *mConsoleWnd;
    QWidget* mMainWindow;

#ifdef VBOX_WITH_REGISTRATION
    VBoxRegistrationDlg *mRegDlg;
#endif
    VBoxUpdateDlg *mUpdDlg;

    QUuid vmUuid;

    QThread *media_enum_thread;
    VBoxMediaList media_list;

    VBoxDefs::RenderMode vm_render_mode;
    const char * vm_render_mode_str;

#ifdef VBOX_WITH_DEBUGGER_GUI
    /** Whether the debugger should be accessible or not.
     * Use --dbg, the env.var. VBOX_GUI_DBG_ENABLED, --debug or the env.var.
     * VBOX_GUI_DBG_AUTO_SHOW to enable. */
    bool mDbgEnabled;
    /** Whether to show the debugger automatically with the console.
     * Use --debug or the env.var. VBOX_GUI_DBG_AUTO_SHOW to enable. */
    bool mDbgAutoShow;
    /** VBoxDbg module handle. */
    RTLDRMOD mhVBoxDbg;
#endif

#if defined (Q_WS_WIN32)
    DWORD dwHTMLHelpCookie;
#endif

    CVirtualBoxCallback callback;

    typedef QVector <QString> QStringVector;

    QString verString;

    QVector <CGuestOSType> vm_os_types;
    QHash <QString, QPixmap *> vm_os_type_icons;
    QVector <QColor *> vm_state_color;

    QHash <long int, QPixmap *> mStateIcons;
    QPixmap mOfflineSnapshotIcon, mOnlineSnapshotIcon;

    QStringVector machineStates;
    QStringVector sessionStates;
    QStringVector deviceTypes;
    QStringVector storageBuses;
    QStringVector storageBusDevices;
    QStringVector storageBusChannels;
    QStringVector diskTypes;
    QStringVector diskStorageTypes;
    QStringVector vrdpAuthTypes;
    QStringVector portModeTypes;
    QStringVector usbFilterActionTypes;
    QStringVector audioDriverTypes;
    QStringVector audioControllerTypes;
    QStringVector networkAdapterTypes;
    QStringVector networkAttachmentTypes;
    QStringVector clipboardTypes;
    QStringVector ideControllerTypes;
    QStringVector USBDeviceStates;

    QString mUserDefinedPortName;

    mutable bool detailReportTemplatesReady;

    friend VBoxGlobal &vboxGlobal();
    friend class VBoxCallback;
};

inline VBoxGlobal &vboxGlobal() { return VBoxGlobal::instance(); }

// Helper classes
////////////////////////////////////////////////////////////////////////////////

/**
 *  Generic asyncronous event.
 *
 *  This abstract class is intended to provide a conveinent way to execute
 *  code on the main GUI thread asynchronously to the calling party. This is
 *  done by putting necessary actions to the #handle() function in a subclass
 *  and then posting an instance of the subclass using #post(). The instance
 *  must be allocated on the heap using the <tt>new</tt> operation and will be
 *  automatically deleted after processing. Note that if you don't call #post()
 *  on the created instance, you have to delete it yourself.
 */
class VBoxAsyncEvent : public QEvent
{
public:

    VBoxAsyncEvent() : QEvent ((QEvent::Type) VBoxDefs::AsyncEventType) {}

    /**
     *  Worker function. Gets executed on the GUI thread when the posted event
     *  is processed by the main event loop.
     */
    virtual void handle() = 0;

    /**
     *  Posts this event to the main event loop.
     *  The caller loses ownership of this object after this method returns
     *  and must not delete the object.
     */
    void post()
    {
        QApplication::postEvent (&vboxGlobal(), this);
    }
};

/**
 *  USB Popup Menu class.
 *  This class provides the list of USB devices attached to the host.
 */
class VBoxUSBMenu : public QMenu
{
    Q_OBJECT

public:

    VBoxUSBMenu (QWidget *);

    const CUSBDevice& getUSB (QAction *aAction);

    void setConsole (const CConsole &);

private slots:

    void processAboutToShow();

private:
    bool event(QEvent *aEvent);

    QMap <QAction *, CUSBDevice> mUSBDevicesMap;
    CConsole mConsole;
};

/**
 *  Enable/Disable Menu class.
 *  This class provides enable/disable menu items.
 */
class VBoxSwitchMenu : public QMenu
{
    Q_OBJECT

public:

    VBoxSwitchMenu (QWidget *, QAction *, bool aInverted = false);

    void setToolTip (const QString &);

private slots:

    void processAboutToShow();

private:

    QAction *mAction;
    bool     mInverted;
};

#endif /* __VBoxGlobal_h__ */
