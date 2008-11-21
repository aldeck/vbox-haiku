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
#include <QLinkedList>

class QAction;
class QLabel;
class QToolButton;

// Auxiliary types
////////////////////////////////////////////////////////////////////////////////

/**
 * Media descriptor for the GUI.
 *
 * Maintains the results of the last state (accessibility) check and precomposes
 * string parameters such as location, size which can be used in various GUI
 * controls.
 *
 * Many getter methods take the boolean @a aNoDiffs argument. Unless explicitly
 * stated otherwise, this argument, when set to @c true, will cause the
 * corresponding property of this object's root medium to be returned instead of
 * its own one. This is useful when hard disk media is represented in the
 * user-friendly "don't show diffs" mode. For non-hard disk media, the value of
 * this argument is irrelevant because the root object for such medium is
 * the medium itself.
 *
 * Note that this class "abuses" the KMediaState_NotCreated state value to
 * indicate that the accessibility check of the given medium (see
 * #blockAndQueryState()) has not been done yet and therefore some parameters
 * such as #size() are meaningless because they can be read only from the
 * accessible medium. The real KMediaState_NotCreated state is not necessary
 * because this class is only used with created (existing) media.
 */
class VBoxMedium
{
public:

    /**
     * Creates a null medium descriptor which is not associated with any medium.
     * The state field is set to KMediaState_NotCreated.
     */
    VBoxMedium()
        : mType (VBoxDefs::MediaType_Invalid)
        , mState (KMediaState_NotCreated)
        , mIsReadOnly (false), mIsUsedInSnapshots (false)
        , mParent (NULL) {}

    /**
     * Creates a media descriptor associated with the given medium.
     *
     * The state field remain KMediaState_NotCreated until #blockAndQueryState()
     * is called. All precomposed strings are filled up by implicitly calling
     * #refresh(), see the #refresh() details for more info.
     *
     * One of the hardDisk, dvdImage, or floppyImage members is assigned from
     * aMedium according to aType. @a aParent must be always NULL for non-hard
     * disk media.
     */
    VBoxMedium (const CMedium &aMedium, VBoxDefs::MediaType aType,
                VBoxMedium *aParent = NULL)
        : mMedium (aMedium), mType (aType)
        , mState (KMediaState_NotCreated)
        , mIsReadOnly (false), mIsUsedInSnapshots (false)
        , mParent (aParent) { init(); }

    /**
     * Similar to the other non-null constructor but sets the media state to
     * @a aState. Suitable when the media state is known such as right after
     * creation.
     */
    VBoxMedium (const CMedium &aMedium, VBoxDefs::MediaType aType,
                KMediaState aState)
        : mMedium (aMedium), mType (aType)
        , mState (aState)
        , mIsReadOnly (false), mIsUsedInSnapshots (false)
        , mParent (NULL) { init(); }

    void blockAndQueryState();
    void refresh();

    const CMedium &medium() const { return mMedium; };

    VBoxDefs::MediaType type() const { return mType; }

    /**
     * Media state. In "don't show diffs" mode, this is the worst state (in
     * terms of inaccessibility) detected on the given hard disk chain.
     *
     * @param aNoDiffs  @c true to enable user-friendly "don't show diffs" mode.
     */
    KMediaState state (bool aNoDiffs = false) const
    {
        unconst (this)->checkNoDiffs (aNoDiffs);
        return aNoDiffs ? mNoDiffs.state : mState;
    }

    QString lastAccessError() const { return mLastAccessError; }

    /**
     * Result of the last blockAndQueryState() call. Will indicate an error and
     * contain a proper error info if the last state check fails. In "don't show
     * diffs" mode, this is the worst result (in terms of inaccessibility)
     * detected on the given hard disk chain.
     *
     * @param aNoDiffs  @c true to enable user-friendly "don't show diffs" mode.
     */
    const COMResult &result (bool aNoDiffs = false) const
    {
        unconst (this)->checkNoDiffs (aNoDiffs);
        return aNoDiffs ? mNoDiffs.result : mResult;
    }

    const CHardDisk2 &hardDisk() const { return mHardDisk; }
    const CDVDImage2 &dvdImage() const { return mDVDImage; }
    const CFloppyImage2 &floppyImage() const { return mFloppyImage; }

    QUuid id() const { return mId; }

    QString location (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mLocation : mLocation; }
    QString name (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mName : mName; }

    QString size (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mSize : mSize; }

    QString hardDiskFormat (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mHardDiskFormat : mHardDiskFormat; }
    QString hardDiskType (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mHardDiskType : mHardDiskType; }
    QString logicalSize (bool aNoDiffs = false) const
        { return aNoDiffs ? root().mLogicalSize : mLogicalSize; }

    QString usage (bool aNoDiffs = false) const
    { return aNoDiffs ? root().mUsage : mUsage; }

    /**
     * Returns @c true if this medium is read-only (either because it is
     * Immutable or because it has child hard disks). Read-only media can only
     * be attached indirectly.
     */
    bool isReadOnly() const { return mIsReadOnly; }

    /**
     * Returns @c true if this medium is attached to any VM (in the current
     * state or in a snapshot) in which case #usage() will contain a string with
     * comma-sparated VM names (with snapshot names, if any, in parenthesis).
     */
    bool isUsed() const { return !mUsage.isNull(); }

    /**
     * Returns @c true if this medium is attached to any VM in any snapshot.
     * which case #usage() will contain a string with comma-sparated VM names.
     */
    bool isUsedInSnapshots() const { return mIsUsedInSnapshots; }

    /**
     * Returns @c true if this medium is attached to the given machine in the
     * current state.
     */
    bool isAttachedInCurStateTo (const QUuid &aMachineId) const
        { return mCurStateMachineIds.indexOf (aMachineId) >= 0; }

    /**
     * Returns a vector of IDs of all machines this medium is attached
     * to in their current state (i.e. excluding snapshots).
     */
    const QList <QUuid> &curStateMachineIds() const
        { return mCurStateMachineIds; }

    /**
     * Returns a parent medium. For non-hard disk media, this is always NULL.
     */
    VBoxMedium *parent() const { return mParent; }

    VBoxMedium &root() const;

    QString toolTip(bool aNoDiffs = false, bool aCheckRO = false) const;
    QPixmap icon (bool aNoDiffs = false, bool aCheckRO = false) const;

    /** Shortcut to <tt>#toolTip (aNoDiffs, true)</tt>. */
    QString toolTipCheckRO (bool aNoDiffs = false) const
        { return toolTip (aNoDiffs, true); }

    /** Shortcut to <tt>#icon (aNoDiffs, true)</tt>. */
    QPixmap iconCheckRO (bool aNoDiffs = false) const
        { return icon (aNoDiffs, true); }

    QString details (bool aNoDiffs = false, bool aPredictDiff = false,
                     bool aUseHTML = false) const;

    /** Shortcut to <tt>#details (aNoDiffs, aPredictDiff, true)</tt>. */
    QString detailsHTML (bool aNoDiffs = false, bool aPredictDiff = false) const
        { return details (aNoDiffs, aPredictDiff, true); }

    /** Returns @c true if this media descriptor is a null object. */
    bool isNull() const { return mMedium.isNull(); }

private:

    void init();

    void checkNoDiffs (bool aNoDiffs);

    CMedium mMedium;

    VBoxDefs::MediaType mType;

    KMediaState mState;
    QString mLastAccessError;
    COMResult mResult;

    CHardDisk2 mHardDisk;
    CDVDImage2 mDVDImage;
    CFloppyImage2 mFloppyImage;

    QUuid mId;
    QString mLocation;
    QString mName;
    QString mSize;

    QString mHardDiskFormat;
    QString mHardDiskType;
    QString mLogicalSize;

    QString mUsage;
    QString mToolTip;

    bool mIsReadOnly        : 1;
    bool mIsUsedInSnapshots : 1;

    QList <QUuid> mCurStateMachineIds;

    VBoxMedium *mParent;

    /**
     * Used to override some attributes in the user-friendly "don't show diffs"
     * mode.
     */
    struct NoDiffs
    {
        NoDiffs() : isSet (false), state (KMediaState_NotCreated) {}

        bool isSet : 1;

        KMediaState state;
        COMResult result;
        QString toolTip;
    }
    mNoDiffs;
};

typedef QLinkedList <VBoxMedium> VBoxMediaList;

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
#else
    bool isDebuggerAutoShowEnabled() const { return false; }
#endif

    /* VBox enum to/from string/icon/color convertors */

    QList <CGuestOSType> vmGuestOSFamilyList() const;
    QList <CGuestOSType> vmGuestOSTypeList (const QString &aFamilyId) const;
    QPixmap vmGuestOSTypeIcon (const QString &aTypeId) const;
    CGuestOSType vmGuestOSType (const QString &aTypeId,
                                const QString &aFamilyId = QString::null) const;
    QString vmGuestOSTypeDescription (const QString &aTypeId) const;

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

    /**
     * Similar to toString (KHardDiskType), but returns 'Differencing' for
     * normal hard disks that have a parent.
     */
    QString hardDiskTypeString (const CHardDisk2 &aHD) const
    {
        if (!aHD.GetParent().isNull())
        {
            Assert (aHD.GetType() == KHardDiskType_Normal);
            return diskTypes_Differencing;
        }
        return toString (aHD.GetType());
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

    QPixmap warningIcon() const { return mWarningIcon; }
    QPixmap errorIcon() const { return mErrorIcon; }

    /* details generators */

    QString details (const CHardDisk2 &aHD, bool aPredictDiff);

    QString details (const CUSBDevice &aDevice) const;
    QString toolTip (const CUSBDevice &aDevice) const;
    QString toolTip (const CUSBDeviceFilter &aFilter) const;

    QString detailsReport (const CMachine &aMachine, bool aIsNewVM,
                           bool aWithLinks);

    QString platformInfo();

    /* VirtualBox helpers */

#if defined(Q_WS_X11) && !defined(VBOX_OSE)
    double findLicenseFile (const QStringList &aFilesList, QRegExp aPattern, QString &aLicenseFile) const;
    bool showVirtualBoxLicense();
#endif

    bool checkForAutoConvertedSettings (bool aAfterRefresh = false);

    void checkForAutoConvertedSettingsAfterRefresh()
    { checkForAutoConvertedSettings (true); }

    CSession openSession (const QUuid &aId, bool aExisting = false);

    /** Shortcut to openSession (aId, true). */
    CSession openExistingSession (const QUuid &aId) { return openSession (aId, true); }

    bool startMachine (const QUuid &id);

    void startEnumeratingMedia();

    /**
     * Returns a list of all currently registered media. This list is used to
     * globally track the accessiblity state of all media on a dedicated thread.
     *
     * Note that the media list is initially empty (i.e. before the enumeration
     * process is started for the first time using #startEnumeratingMedia()).
     * See #startEnumeratingMedia() for more information about how meida are
     * sorted in the returned list.
     */
    const VBoxMediaList &currentMediaList() const { return mMediaList; }

    /** Returns true if the media enumeration is in progress. */
    bool isMediaEnumerationStarted() const { return mMediaEnumThread != NULL; }

    void addMedium (const VBoxMedium &);
    void updateMedium (const VBoxMedium &);
    void removeMedium (VBoxDefs::MediaType, const QUuid &);

    bool findMedium (const CMedium &, VBoxMedium &) const;

    /** Compact version of #findMediumTo(). Asserts if not found. */
    VBoxMedium getMedium (const CMedium &aObj) const
    {
        VBoxMedium medium;
        if (!findMedium (aObj, medium))
            AssertFailed();
        return medium;
    }

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
    static QIcon iconSetFull (const QSize &aNormalSize, const QSize &aSmallSize,
                              const char *aNormal, const char *aSmallNormal,
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

    static quint64 requiredVideoMemory (CMachine *aMachine = 0);

    static QString locationForHTML (const QString &aFileName);

    static QString highlight (const QString &aStr, bool aToolTip = false);

    static QString systemLanguageId();

    static QString getExistingDirectory (const QString &aDir, QWidget *aParent,
                                         const QString &aCaption = QString::null,
                                         bool aDirOnly = TRUE,
                                         bool resolveSymlinks = TRUE);

    static QString getOpenFileName (const QString &aStartWith, const QString &aFilters, QWidget *aParent,
                                    const QString &aCaption, QString *aSelectedFilter = NULL,
                                    bool aResolveSymLinks = true);

    static QStringList getOpenFileNames (const QString &aStartWith, const QString &aFilters, QWidget *aParent,
                                         const QString &aCaption, QString *aSelectedFilter = NULL,
                                         bool aResolveSymLinks = true,
                                         bool aSingleFile = false);

    static QString getFirstExistingDir (const QString &);

    static bool activateWindow (WId aWId, bool aSwitchDesktop = true);

    static QString removeAccelMark (const QString &aText);

    static QString insertKeyToActionText (const QString &aText, const QString &aKey);
    static QString extractKeyFromActionText (const QString &aText);

    static QPixmap joinPixmaps (const QPixmap &aPM1, const QPixmap &aPM2);

    static QWidget *findWidget (QWidget *aParent, const char *aName,
                                const char *aClassName = NULL,
                                bool aRecursive = false);

    static QList <QPair <QString, QString> > HDDBackends();

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
     * Emitted at the beginning of the enumeration process started by
     * #startEnumeratingMedia().
     */
    void mediumEnumStarted();

    /**
     * Emitted when a new medium item from the list has updated its
     * accessibility state.
     */
    void mediumEnumerated (const VBoxMedium &aMedum);

    /**
     * Emitted at the end of the enumeration process started by
     * #startEnumeratingMedia(). The @a aList argument is passed for
     * convenience, it is exactly the same as returned by #currentMediaList().
     */
    void mediumEnumFinished (const VBoxMediaList &aList);

    /** Emitted when a new media is added using #addMedia(). */
    void mediumAdded (const VBoxMedium &);

    /** Emitted when the media is updated using #updateMedia(). */
    void mediumUpdated (const VBoxMedium &);

    /** Emitted when the media is removed using #removeMedia(). */
    void mediumRemoved (VBoxDefs::MediaType, const QUuid &);

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

    QThread *mMediaEnumThread;
    VBoxMediaList mMediaList;

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

    QList <QString> mFamilyIDs;
    QList <QList <CGuestOSType> > mTypes;
    QHash <QString, QPixmap *> mOsTypeIcons;
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
    QString diskTypes_Differencing;

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

    QPixmap mWarningIcon, mErrorIcon;

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
