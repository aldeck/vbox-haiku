/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMSettingsHD class implementation
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

#include "VBoxVMSettingsHD.h"
#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "QIWidgetValidator.h"
#include "VBoxToolBar.h"
#include "VBoxMediaManagerDlg.h"
#include "VBoxNewHDWzd.h"

/* Qt includes */
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QMetaProperty>
#include <QScrollBar>
#include <QStylePainter>

/** SATA Ports count */
static const ULONG SATAPortsCount = 30;

/**
 * Clear the focus from the current focus owner on guard creation.
 * And put it into the desired object on guard deletion.
 *
 * Here this is used to temporary remove the focus from the attachments
 * table to close the temporary editor of this table to prevent
 * any side-process (enumeration) influencing model's data.
 */
class FocusGuardBlock
{
public:
    FocusGuardBlock (QWidget *aReturnTo) : mReturnTo (aReturnTo)
    {
        if (QApplication::focusWidget())
        {
            QApplication::focusWidget()->clearFocus();
            qApp->processEvents();
        }
    }
   ~FocusGuardBlock()
    {
        mReturnTo->setFocus();
        qApp->processEvents();
    }

private:
    QWidget *mReturnTo;
};

/** Type to store disk data */
DiskValue::DiskValue (const QUuid &aId)
    : id (aId)
    , name (QString::null), tip (QString::null), pix (QPixmap())
{
    if (aId.isNull())
        return;

    VBoxMedium medium = vboxGlobal().getMedium (
        CMedium (vboxGlobal().virtualBox().GetHardDisk2 (aId)));
    medium.refresh();
    bool noDiffs = !HDSettings::instance()->showDiffs();
    name = medium.details (noDiffs);
    tip = medium.toolTipCheckRO (noDiffs);
    pix = medium.iconCheckRO (noDiffs);
}

/**
 * QAbstractTableModel class reimplementation.
 * Used to feat slot/disk selection mechanism.
 */
Qt::ItemFlags AttachmentsModel::flags (const QModelIndex &aIndex) const
{
    return aIndex.row() == rowCount() - 1 ?
        QAbstractItemModel::flags (aIndex) ^ Qt::ItemIsSelectable :
        QAbstractItemModel::flags (aIndex) | Qt::ItemIsEditable;
}

QVariant AttachmentsModel::data (const QModelIndex &aIndex, int aRole) const
{
    if (!aIndex.isValid())
        return QVariant();

    if (aIndex.row() < 0 || aIndex.row() >= rowCount())
        return QVariant();

    switch (aRole)
    {
        case Qt::DisplayRole:
        {
            if (aIndex.row() == rowCount() - 1)
                return QVariant();
            else if (aIndex.column() == 0)
                return QVariant (mUsedSlotsList [aIndex.row()].name);
            else if (aIndex.column() == 1)
                return QVariant (mUsedDisksList [aIndex.row()].name);

            Assert (0);
            return QVariant();
        }
        case Qt::DecorationRole:
        {
            return aIndex.row() != rowCount() - 1 &&
                   aIndex.column() == 1 &&
                   (aIndex != mParent->currentIndex() ||
                    !DiskEditor::activeEditor())
                   ? QVariant (mUsedDisksList [aIndex.row()].pix) : QVariant();
        }
        case Qt::EditRole:
        {
            if (aIndex.column() == 0)
                return QVariant (mSlotId, &mUsedSlotsList [aIndex.row()]);
            else if (aIndex.column() == 1)
                return QVariant (mDiskId, &mUsedDisksList [aIndex.row()]);

            Assert (0);
            return QVariant();
        }
        case Qt::ToolTipRole:
        {
            if (aIndex.row() == rowCount() - 1)
                return QVariant (tr ("Double-click to add a new attachment"));

            return QVariant (mUsedDisksList [aIndex.row()].tip);
        }
        default:
        {
            return QVariant();
        }
    }
}

bool AttachmentsModel::setData (const QModelIndex &aIndex,
                                const QVariant &aValue,
                                int /* aRole = Qt::EditRole */)
{
    if (!aIndex.isValid())
        return false;

    if (aIndex.row() < 0 || aIndex.row() >= rowCount())
        return false;

    if (aIndex.column() == 0)
    {
        SlotValue newSlot = aValue.isValid() ?
            aValue.value <SlotValue>() : SlotValue();
        if (mUsedSlotsList [aIndex.row()] != newSlot)
        {
            mUsedSlotsList [aIndex.row()] = newSlot;
            emit dataChanged (aIndex, aIndex);
            return true;
        }
        return false;
    } else
    if (aIndex.column() == 1)
    {
        DiskValue newDisk = aValue.isValid() ?
            aValue.value <DiskValue>() : DiskValue();
        if (mUsedDisksList [aIndex.row()] != newDisk)
        {
            mUsedDisksList [aIndex.row()] = newDisk;
            emit dataChanged (aIndex, aIndex);
            return true;
        }
        return false;
    }
    Assert (0);
    return false;
}

QVariant AttachmentsModel::headerData (int aSection,
                                       Qt::Orientation aOrientation,
                                       int aRole) const
{
    if (aRole != Qt::DisplayRole)
        return QVariant();

    if (aOrientation == Qt::Horizontal)
        return aSection ? tr ("Hard Disk") : tr ("Slot");
    else
        return QVariant();
}

void AttachmentsModel::addItem (const SlotValue &aSlot, const DiskValue &aDisk)
{
    beginInsertRows (QModelIndex(), rowCount() - 1, rowCount() - 1);
    mUsedSlotsList.append (aSlot);
    mUsedDisksList.append (aDisk);
    endInsertRows();
}

void AttachmentsModel::delItem (int aIndex)
{
    beginRemoveRows (QModelIndex(), aIndex, aIndex);
    mUsedSlotsList.removeAt (aIndex);
    mUsedDisksList.removeAt (aIndex);
    endRemoveRows();
}

QList <Attachment> AttachmentsModel::fullUsedList()
{
    QList <Attachment> list;
    QList <SlotValue> slts = usedSlotsList();
    QList <DiskValue> dsks = usedDisksList();
    for (int i = 0; i < slts.size(); ++ i)
        list << Attachment (slts [i], dsks [i]);
    qSort (list.begin(), list.end());
    return list;
}

void AttachmentsModel::removeSata()
{
    QList <SlotValue>::iterator slotIt = mUsedSlotsList.begin();
    QList <DiskValue>::iterator diskIt = mUsedDisksList.begin();
    while (slotIt != mUsedSlotsList.end())
    {
        if ((*slotIt).bus == KStorageBus_SATA)
        {
            slotIt = mUsedSlotsList.erase (slotIt);
            diskIt = mUsedDisksList.erase (diskIt);
        }
        else
        {
            ++ slotIt;
            ++ diskIt;
        }
    }
}

void AttachmentsModel::updateDisks()
{
    QList <DiskValue> newDisks (HDSettings::instance()->disksList());
    for (int i = 0; i < mUsedDisksList.size(); ++ i)
    {
        if (newDisks.isEmpty())
            mUsedDisksList [i] = DiskValue();
        else if (newDisks.contains (mUsedDisksList [i]))
            mUsedDisksList [i] = DiskValue (mUsedDisksList [i].id);
        else
            mUsedDisksList [i] = DiskValue (newDisks [0].id);
    }
    emit dataChanged (index (0, 1), index (rowCount() - 1, 1));
}

/**
 * QComboBox class reimplementation.
 * Used as editor for HD Attachment SLOT field.
 */
SlotEditor::SlotEditor (QWidget *aParent)
    : QComboBox (aParent)
{
    connect (this, SIGNAL (currentIndexChanged (int)), this, SLOT (onActivate()));
    connect (this, SIGNAL (readyToCommit (QWidget*)),
             parent()->parent(), SLOT (commitData (QWidget*)));
}

QVariant SlotEditor::slot() const
{
    int current = currentIndex();
    QVariant result;
    if (current >= 0 && current < mList.size())
        result.setValue (mList [current]);
    return result;
}

void SlotEditor::setSlot (QVariant aSlot)
{
    SlotValue val (aSlot.value <SlotValue>());
    populate (val);
    int current = findText (val.name);
    setCurrentIndex (current == -1 ? 0 : current);
}

void SlotEditor::onActivate()
{
    emit readyToCommit (this);
}

#if 0 /* F2 key binding left for future releases... */
void SlotEditor::keyPressEvent (QKeyEvent *aEvent)
{
    /* Make F2 key to show the popup. */
    if (aEvent->key() == Qt::Key_F2)
    {
        aEvent->accept();
        showPopup();
    }
    else
        aEvent->ignore();
    QComboBox::keyPressEvent (aEvent);
}
#endif

void SlotEditor::populate (const SlotValue &aIncluding)
{
    clear(), mList.clear();
    QList <SlotValue> list (HDSettings::instance()->slotsList (aIncluding, true));
    for (int i = 0; i < list.size() ; ++ i)
    {
        insertItem (i, list [i].name);
        mList << list [i];
    }
}

/**
 * VBoxMediaComboBox class reimplementation.
 * Used as editor for HD Attachment DISK field.
 */
DiskEditor* DiskEditor::mInstance = 0;
DiskEditor* DiskEditor::activeEditor()
{
    return mInstance;
}

DiskEditor::DiskEditor (QWidget *aParent)
    : VBoxMediaComboBox (aParent)
{
    mInstance = this;
    setIconSize (QSize (iconSize().width() * 2 + 2, iconSize().height()));
    Assert (!HDSettings::instance()->machine().isNull());
    setType (VBoxDefs::MediaType_HardDisk);
    setMachineId (HDSettings::instance()->machine().GetId());
    setShowDiffs (HDSettings::instance()->showDiffs());
    connect (this, SIGNAL (currentIndexChanged (int)), this, SLOT (onActivate()));
    connect (this, SIGNAL (readyToCommit (QWidget *)),
             parent()->parent(), SLOT (commitData (QWidget *)));
    refresh();
}
DiskEditor::~DiskEditor()
{
    if (mInstance == this)
        mInstance = 0;
}

QVariant DiskEditor::disk() const
{
    int current = currentIndex();
    QVariant result;
    if (current >= 0 && current < count())
        result.setValue (DiskValue (id (current)));
    return result;
}

void DiskEditor::setDisk (QVariant aDisk)
{
    setCurrentItem (DiskValue (aDisk.value <DiskValue>()).id);
}

void DiskEditor::paintEvent (QPaintEvent*)
{
    /* Create the style painter to paint the elements. */
    QStylePainter painter (this);
    painter.setPen (palette().color (QPalette::Text));
    /* Initialize combo-box options and draw the elements. */
    QStyleOptionComboBox options;
    initStyleOption (&options);
    painter.drawComplexControl (QStyle::CC_ComboBox, options);
    painter.drawControl (QStyle::CE_ComboBoxLabel, options);
}

void DiskEditor::initStyleOption (QStyleOptionComboBox *aOption) const
{
    /* The base version of Qt4::QComboBox ignores the fact what each
     * combo-box item can have the icon of different size and uses the
     * maximum possible icon-size to draw the icon then performing
     * paintEvent(). As a result, stand-alone icons are painted using
     * the same huge region as the merged paired icons, so we have to
     * perform the size calculation ourself... */

    /* Init all style option by default... */
    VBoxMediaComboBox::initStyleOption (aOption);
    /* But calculate the icon size ourself. */
    QIcon currentItemIcon (itemIcon (currentIndex()));
    QPixmap realPixmap (currentItemIcon.pixmap (iconSize()));
    aOption->iconSize = realPixmap.size();
}

void DiskEditor::onActivate()
{
    emit readyToCommit (this);
}

#if 0 /* F2 key binding left for future releases... */
void DiskEditor::keyPressEvent (QKeyEvent *aEvent)
{
    /* Make F2 key to show the popup. */
    if (aEvent->key() == Qt::Key_F2)
    {
        aEvent->accept();
        showPopup();
    }
    else
        aEvent->ignore();
    VBoxMediaComboBox::keyPressEvent (aEvent);
}
#endif

/**
 * Singleton QObject class reimplementation.
 * Used to make selected HD Attachments slots unique &
 * stores some local data used for HD Settings.
 */
HDSettings* HDSettings::mInstance = 0;
HDSettings* HDSettings::instance (QWidget *aParent,
                                  AttachmentsModel *aWatched)
{
    if (!mInstance)
    {
        Assert (aParent && aWatched);
        mInstance = new HDSettings (aParent, aWatched);
    }
    return mInstance;
}

HDSettings::HDSettings (QWidget *aParent, AttachmentsModel *aWatched)
    : QObject (aParent)
    , mModel (aWatched)
    , mSataCount (SATAPortsCount)
    , mShowDiffs (false)
{
    makeIDEList();
    makeSATAList();
}

HDSettings::~HDSettings()
{
    mInstance = 0;
}

QList <SlotValue> HDSettings::slotsList (const SlotValue &aIncluding,
                                         bool aFilter /* = false */) const
{
    /* Compose the full slots list */
    QList <SlotValue> list (mIDEList + mSATAList);
    if (!aFilter)
        return list;

    /* Current used list */
    QList <SlotValue> usedList (mModel->usedSlotsList());

    /* Filter the list */
    foreach (SlotValue value, usedList)
        if (value != aIncluding)
            list.removeAll (value);

    return list;
}

QList <DiskValue> HDSettings::disksList() const
{
    return mDisksList;
}

bool HDSettings::tryToChooseUniqueDisk (DiskValue &aResult) const
{
    bool status = false;

    /* Current used list */
    QList <DiskValue> usedList (mModel->usedDisksList());

    /* Select the first available disk initially */
    aResult = mDisksList.isEmpty() ? DiskValue() : mDisksList [0];

    /* Search for first not busy disk */
    for (int i = 0; i < mDisksList.size(); ++ i)
        if (!usedList.contains (mDisksList [i]))
        {
            aResult = mDisksList [i];
            status = true;
            break;
        }

    return status;
}

void HDSettings::makeIDEList()
{
    mIDEList.clear();

    /* IDE Primary Master */
    mIDEList << SlotValue (KStorageBus_IDE, 0, 0);
    /* IDE Primary Slave */
    mIDEList << SlotValue (KStorageBus_IDE, 0, 1);
    /* IDE Secondary Slave */
    mIDEList << SlotValue (KStorageBus_IDE, 1, 1);
}

void HDSettings::makeSATAList()
{
    mSATAList.clear();

    for (int i = 0; i < mSataCount; ++ i)
        mSATAList << SlotValue (KStorageBus_SATA, i, 0);
}

void HDSettings::makeMediumList()
{
    mDisksList.clear();
    VBoxMediaList list (vboxGlobal().currentMediaList());
    foreach (VBoxMedium medium, list)
    {
        /* Filter out unnecessary mediums */
        if (medium.type() != VBoxDefs::MediaType_HardDisk)
            continue;

        /* If !mShowDiffs we ignore all diffs except ones that are
         * directly attached to the related VM in the current state */
        if (!mShowDiffs && medium.parent() &&
            !medium.isAttachedInCurStateTo (mMachine.GetId()))
            continue;

        /* If !mShowDiffs we have to replace the root medium with his
         * differencing child which is directly used if the parent is found. */
        if (!mShowDiffs && medium.parent())
        {
            int index = mDisksList.indexOf (DiskValue (medium.root().id()));
            if (index != -1)
            {
                mDisksList.replace (index, DiskValue (medium.id()));
                continue;
            }
        }

        mDisksList.append (DiskValue (medium.id()));
    }
}

/**
 * QWidget class reimplementation.
 * Used as HD Settings widget.
 */
VBoxVMSettingsHD::VBoxVMSettingsHD()
    : mValidator (0)
    , mWasTableSelected (false)
    , mPolished (false)
{
    /* Apply UI decorations */
    Ui::VBoxVMSettingsHD::setupUi (this);

    /* Setup model/view factory */
    int idHDSlot = qRegisterMetaType <SlotValue>();
    int idHDDisk = qRegisterMetaType <DiskValue>();
    QItemEditorFactory *factory = new QItemEditorFactory;
    QItemEditorCreatorBase *slotCreator =
        new QStandardItemEditorCreator <SlotEditor>();
    QItemEditorCreatorBase *diskCreator =
        new QStandardItemEditorCreator <DiskEditor>();
    factory->registerEditor ((QVariant::Type)idHDSlot, slotCreator);
    factory->registerEditor ((QVariant::Type)idHDDisk, diskCreator);
    QItemEditorFactory::setDefaultFactory (factory);

    /* Setup view-model */
    mModel = new AttachmentsModel (mTwAts, idHDSlot, idHDDisk);
    connect (mModel, SIGNAL (dataChanged (const QModelIndex&, const QModelIndex&)),
             this, SIGNAL (hdChanged()));

    /* Initialize HD Settings */
    HDSettings::instance (mTwAts, mModel);

    /* Setup table-view */
    mTwAts->verticalHeader()->setDefaultSectionSize (
        (int) (mTwAts->fontMetrics().height() * 1.30 /* 130% of font height */));
    mTwAts->verticalHeader()->hide();
    mTwAts->horizontalHeader()->setStretchLastSection (true);
    mTwAts->setModel (mModel);
    mTwAts->setToolTip (mModel->data (mModel->index (mModel->rowCount() - 1, 0),
                                      Qt::ToolTipRole).toString());

    /* Prepare actions */
    mNewAction = new QAction (mTwAts);
    mDelAction = new QAction (mTwAts);
    mVdmAction = new QAction (mTwAts);

    mTwAts->addAction (mNewAction);
    mTwAts->addAction (mDelAction);
    mTwAts->addAction (mVdmAction);

    mNewAction->setShortcut (QKeySequence ("Ins"));
    mDelAction->setShortcut (QKeySequence ("Del"));
    mVdmAction->setShortcut (QKeySequence ("Ctrl+Space"));

    mNewAction->setIcon (VBoxGlobal::iconSet (":/vdm_add_16px.png",
                                              ":/vdm_add_disabled_16px.png"));
    mDelAction->setIcon (VBoxGlobal::iconSet (":/vdm_remove_16px.png",
                                              ":/vdm_remove_disabled_16px.png"));
    mVdmAction->setIcon (VBoxGlobal::iconSet (":/select_file_16px.png",
                                              ":/select_file_dis_16px.png"));

    /* Prepare toolbar */
    VBoxToolBar *toolBar = new VBoxToolBar (mGbAts);
    toolBar->setUsesTextLabel (false);
    toolBar->setIconSize (QSize (16, 16));
    toolBar->setOrientation (Qt::Vertical);
    toolBar->addAction (mNewAction);
    toolBar->addAction (mDelAction);
    toolBar->addAction (mVdmAction);
    mGbAts->layout()->addWidget (toolBar);

    /* Setup connections */
    connect (mNewAction, SIGNAL (triggered (bool)),
             this, SLOT (addAttachment()));
    connect (mDelAction, SIGNAL (triggered (bool)),
             this, SLOT (delAttachment()));
    connect (mVdmAction, SIGNAL (triggered (bool)),
             this, SLOT (showMediaManager()));

    connect (mSATACheck, SIGNAL (stateChanged (int)),
             this, SLOT (onSATACheckToggled (int)));
    connect (mShowDiffsCheck, SIGNAL (stateChanged (int)),
             this, SLOT (onShowDiffsCheckToggled (int)));

    connect (mTwAts, SIGNAL (currentChanged (const QModelIndex &)),
             this, SLOT (updateActions (const QModelIndex &)));

    connect (&vboxGlobal(), SIGNAL (mediumAdded (const VBoxMedium &)),
             HDSettings::instance(), SLOT (update()));
    connect (&vboxGlobal(), SIGNAL (mediumUpdated (const VBoxMedium &)),
             HDSettings::instance(), SLOT (update()));
    connect (&vboxGlobal(), SIGNAL (mediumRemoved (VBoxDefs::MediaType, const QUuid &)),
             HDSettings::instance(), SLOT (update()));

    /* Install global event filter */
    qApp->installEventFilter (this);

    /* Applying language settings */
    retranslateUi();
}

void VBoxVMSettingsHD::getFrom (const CMachine &aMachine)
{
    mMachine = aMachine;
    HDSettings::instance()->setMachine (mMachine);

    CSATAController ctl = mMachine.GetSATAController();
    /* Hide the SATA check box if the SATA controller is not available
     * (i.e. in VirtualBox OSE) */
    if (ctl.isNull())
        mSATACheck->setHidden (true);
    else
        mSATACheck->setChecked (ctl.GetEnabled());
    onSATACheckToggled (mSATACheck->checkState());
    onShowDiffsCheckToggled (mShowDiffsCheck->checkState());

    /* Load attachments list */
    CHardDisk2AttachmentVector vec = mMachine.GetHardDisk2Attachments();
    for (int i = 0; i < vec.size(); ++ i)
    {
        CHardDisk2Attachment hda = vec [i];
        SlotValue slot (hda.GetBus(), hda.GetChannel(), hda.GetDevice());
        DiskValue disk (hda.GetHardDisk().GetId());
        mModel->addItem (slot, disk);
    }

    /* Initially select the first table item & update the actions */
    mTwAts->setCurrentIndex (mModel->index (0, 1));
    updateActions (mTwAts->currentIndex());

    /* Validate if possible */
    if (mValidator)
        mValidator->revalidate();
}

void VBoxVMSettingsHD::putBackTo()
{
    CSATAController ctl = mMachine.GetSATAController();
    if (!ctl.isNull())
        ctl.SetEnabled (mSATACheck->isChecked());

    /* Detach all attached Hard Disks */
    CHardDisk2AttachmentVector vec = mMachine.GetHardDisk2Attachments();
    for (int i = 0; i < vec.size(); ++ i)
    {
        CHardDisk2Attachment hda = vec [i];
        mMachine.DetachHardDisk2 (hda.GetBus(), hda.GetChannel(), hda.GetDevice());

        /* [dsen] check this */
        if (!mMachine.isOk())
            vboxProblem().cannotDetachHardDisk (this, mMachine,
                vboxGlobal().getMedium (CMedium (hda.GetHardDisk())).location(),
                hda.GetBus(), hda.GetChannel(), hda.GetDevice());
    }

    /* Attach all listed Hard Disks */
    LONG maxSATAPort = 1;
    QList <Attachment> list (mModel->fullUsedList());
    for (int i = 0; i < list.size(); ++ i)
    {
        if (list [i].slot.bus == KStorageBus_SATA)
            maxSATAPort = maxSATAPort < (list [i].slot.channel + 1) ?
                          (list [i].slot.channel + 1) : maxSATAPort;
        mMachine.AttachHardDisk2 (list [i].disk.id,
            list [i].slot.bus, list [i].slot.channel, list [i].slot.device);

        /* [dsen] check this */
        if (!mMachine.isOk())
            vboxProblem().cannotAttachHardDisk (this, mMachine,
                vboxGlobal().getMedium (CMedium (vboxGlobal().virtualBox()
                .GetHardDisk2 (list [i].disk.id))).location(),
                list [i].slot.bus, list [i].slot.channel, list [i].slot.device);
    }

    if (!ctl.isNull())
        ctl.SetPortCount (maxSATAPort);
}

void VBoxVMSettingsHD::setValidator (QIWidgetValidator *aVal)
{
    mValidator = aVal;
    connect (mModel, SIGNAL (dataChanged (const QModelIndex&, const QModelIndex&)),
             mValidator, SLOT (revalidate()));
}

bool VBoxVMSettingsHD::revalidate (QString &aWarning, QString &)
{
    QList <SlotValue> slotList (mModel->usedSlotsList());
    QList <DiskValue> diskList (mModel->usedDisksList());
    for (int i = 0; i < diskList.size(); ++ i)
    {
        /* Check for emptiness */
        if (diskList [i].id.isNull())
        {
            aWarning = tr ("No hard disk is selected for <i>%1</i>")
                           .arg (slotList [i].name);
            break;
        }

        /* Check for coincidence */
        if (diskList.count (diskList [i]) > 1)
        {
            int first = diskList.indexOf (diskList [i]);
            int second = diskList.indexOf (diskList [i], first + 1);
            Assert (first != -1 && second != -1);
            aWarning = tr ("<i>%1</i> uses the hard disk that is "
                           "already attached to <i>%2</i>")
                           .arg (slotList [second].name,
                                 slotList [first].name);
            break;
        }
    }

    return aWarning.isNull();
}

void VBoxVMSettingsHD::setOrderAfter (QWidget *aWidget)
{
    setTabOrder (aWidget, mSATACheck);
    setTabOrder (mSATACheck, mTwAts);
    setTabOrder (mTwAts, mShowDiffsCheck);
}

void VBoxVMSettingsHD::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxVMSettingsHD::retranslateUi (this);

    mNewAction->setText (tr ("&Add Attachment"));
    mDelAction->setText (tr ("&Remove Attachment"));
    mVdmAction->setText (tr ("&Select Hard Disk"));

    mNewAction->setToolTip (mNewAction->text().remove ('&') +
        QString (" (%1)").arg (mNewAction->shortcut().toString()));
    mDelAction->setToolTip (mDelAction->text().remove ('&') +
        QString (" (%1)").arg (mDelAction->shortcut().toString()));
    mVdmAction->setToolTip (mVdmAction->text().remove ('&') +
        QString (" (%1)").arg (mVdmAction->shortcut().toString()));

    mNewAction->setWhatsThis (tr ("Adds a new hard disk attachment."));
    mDelAction->setWhatsThis (tr ("Removes the highlighted hard disk attachment."));
    mVdmAction->setWhatsThis (tr ("Invokes the Virtual Media Manager to select "
                                  "a hard disk to attach to the currently "
                                  "highlighted slot."));
}

void VBoxVMSettingsHD::addAttachment()
{
    /* Temporary disable corresponding action now to prevent calling it again
     * before it will be disabled by current-changed processing. This can
     * happens if the user just pressed & hold the shortcut combination. */
    mNewAction->setEnabled (false);

    QUuid newId;

    {   /* Clear the focus */
        FocusGuardBlock guard (mTwAts);

        bool uniqueDiskSelected = false;
        HDSettings *hds = HDSettings::instance();

        {   /* Add new item with default values */
            SlotValue slot (hds->slotsList (SlotValue(), true) [0]);
            DiskValue disk;
            uniqueDiskSelected = hds->tryToChooseUniqueDisk (disk);
            mModel->addItem (slot, disk);
        }   /* Add new item with default values */

        /* If there are not enough unique disks */
        if (!uniqueDiskSelected)
        {
            /* Ask the user for method to add new disk */
            int confirm = vboxProblem().confirmRunNewHDWzdOrVDM (this);
            newId = confirm == QIMessageBox::Yes ? getWithNewHDWizard() :
                    confirm == QIMessageBox::No ? getWithMediaManager() : QUuid();
        }
    }   /* Clear the focus */

    /* Set the right column of new index to be the current */
    mTwAts->setCurrentIndex (mModel->index (mModel->rowCount() - 2, 1));

    if (!newId.isNull())
    {
        /* Compose & apply resulting disk */
        QVariant newValue;
        newValue.setValue (DiskValue (newId));
        mModel->setData (mTwAts->currentIndex(), newValue);
    }

    /* Validate if possible */
    if (mValidator)
        mValidator->revalidate();
    emit hdChanged();
}

void VBoxVMSettingsHD::delAttachment()
{
    Assert (mTwAts->currentIndex().isValid());

    /* Temporary disable corresponding action now to prevent calling it again
     * before it will be disabled by current-changed processing. This can
     * happens if the user just pressed & hold the shortcut combination. */
    mDelAction->setEnabled (false);

    /* Clear the focus */
    FocusGuardBlock guard (mTwAts);

    /* Storing current attributes */
    int row = mTwAts->currentIndex().row();
    int col = mTwAts->currentIndex().column();

    /* Erase current index */
    mTwAts->setCurrentIndex (QModelIndex());

    /* Calculate new current index */
    int newRow = row < mModel->rowCount() - 2 ? row :
                 row > 0 ? row - 1 : -1;
    QModelIndex next = newRow == -1 ? mModel->index (0, col) :
                                      mModel->index (newRow, col);

    /* Delete current index */
    mModel->delItem (row);

    /* Set the new index to be the current */
    mTwAts->setCurrentIndex (next);
    updateActions (next);

    if (mValidator)
        mValidator->revalidate();
    emit hdChanged();
}

void VBoxVMSettingsHD::showMediaManager()
{
    Assert (mTwAts->currentIndex().isValid());

    /* Clear the focus */
    FocusGuardBlock guard (mTwAts);

    DiskValue current (mModel->data (mTwAts->currentIndex(), Qt::EditRole)
                       .value <DiskValue>());

    QUuid id = getWithMediaManager (current.id);

    if (!id.isNull())
    {
        /* Compose & apply resulting disk */
        QVariant newValue;
        newValue.setValue (DiskValue (id));
        mModel->setData (mTwAts->currentIndex(), newValue);
    }
}

void VBoxVMSettingsHD::updateActions (const QModelIndex& /* aIndex */)
{
    mNewAction->setEnabled (mModel->rowCount() - 1 <
        HDSettings::instance()->slotsList().count());
    mDelAction->setEnabled (mTwAts->currentIndex().row() != mModel->rowCount() - 1);
    mVdmAction->setEnabled (mTwAts->currentIndex().row() != mModel->rowCount() - 1 &&
                            mTwAts->currentIndex().column() == 1);
}

void VBoxVMSettingsHD::onSATACheckToggled (int aState)
{
    if (aState == Qt::Unchecked)
    {
        /* Search the list for at least one SATA port in */
        QList <SlotValue> list (mModel->usedSlotsList());
        int firstSataPort = 0;
        for (; firstSataPort < list.size(); ++ firstSataPort)
            if (list [firstSataPort].bus == KStorageBus_SATA)
                break;

        /* If list contains at least one SATA port */
        if (firstSataPort < list.size())
        {
            if (vboxProblem().confirmDetachSATASlots (this) != QIMessageBox::Ok)
            {
                /* Switch check-box back to "Qt::Checked" */
                mSATACheck->blockSignals (true);
                mSATACheck->setCheckState (Qt::Checked);
                mSATACheck->blockSignals (false);
                return;
            }
            else
            {
                /* Delete SATA items */
                mModel->removeSata();

                /* Set column #1 of first index to be the current */
                mTwAts->setCurrentIndex (mModel->index (0, 1));

                if (mValidator)
                    mValidator->revalidate();
            }
        }
    }

    HDSettings::instance()->setSataCount (aState == Qt::Checked ?
                                          SATAPortsCount : 0);
    updateActions (mTwAts->currentIndex());
}

void VBoxVMSettingsHD::onShowDiffsCheckToggled (int aState)
{
    HDSettings::instance()->setShowDiffs (aState == Qt::Checked);
}

bool VBoxVMSettingsHD::eventFilter (QObject *aObject, QEvent *aEvent)
{
    if (!aObject->isWidgetType())
        return QWidget::eventFilter (aObject, aEvent);

    QWidget *widget = static_cast <QWidget*> (aObject);
    if (widget->inherits ("SlotEditor") ||
        widget->inherits ("DiskEditor"))
    {
        if (aEvent->type() == QEvent::KeyPress)
        {
            QKeyEvent *e = static_cast <QKeyEvent*> (aEvent);
            QModelIndex cur = mTwAts->currentIndex();
            switch (e->key())
            {
                case Qt::Key_Up:
                {
                    if (cur.row() > 0)
                        mTwAts->setCurrentIndex (mModel->index (cur.row() - 1,
                                                                cur.column()));
                    return true;
                }
                case Qt::Key_Down:
                {
                    if (cur.row() < mModel->rowCount() - 1)
                        mTwAts->setCurrentIndex (mModel->index (cur.row() + 1,
                                                                cur.column()));
                    return true;
                }
                case Qt::Key_Right:
                {
                    if (cur.column() == 0)
                        mTwAts->setCurrentIndex (mModel->index (cur.row(), 1));
                    return true;
                }
                case Qt::Key_Left:
                {
                    if (cur.column() == 1)
                        mTwAts->setCurrentIndex (mModel->index (cur.row(), 0));
                    return true;
                }
                case Qt::Key_Tab:
                {
                    focusNextPrevChild (true);
                    return true;
                }
                case Qt::Key_Backtab:
                {
                    /* Due to table on getting focus back from the child
                     * put it instantly to this child again, make a hack
                     * to put focus to the real previous owner. */
                    mSATACheck->setFocus();
                    return true;
                }
                default:
                    break;
            }
        } else
        if (aEvent->type() == QEvent::WindowDeactivate)
        {
            /* Store focus state if it is on temporary editor. */
            if (widget->hasFocus())
                mWasTableSelected = true;
        }
    } else
    if (widget == mTwAts->viewport() &&
        aEvent->type() == QEvent::MouseButtonDblClick)
    {
        QMouseEvent *e = static_cast <QMouseEvent*> (aEvent);
        QModelIndex index = mTwAts->indexAt (e->pos());
        if (mNewAction->isEnabled() &&
            (index.row() == mModel->rowCount() - 1 || !index.isValid()))
            addAttachment();
    } else
    if (aEvent->type() == QEvent::WindowActivate)
    {
        if (mWasTableSelected)
        {
            /* Restore focus state if it was on temporary editor. */
            mWasTableSelected = false;
            mTwAts->setFocus();
        }
    }

    return QWidget::eventFilter (aObject, aEvent);
}

void VBoxVMSettingsHD::showEvent (QShowEvent *aEvent)
{
    QWidget::showEvent (aEvent);

    if (mPolished)
        return;
    mPolished = true;

    /* Some delayed polishing */
    mTwAts->horizontalHeader()->resizeSection (0,
        style()->pixelMetric (QStyle::PM_ScrollBarExtent, 0, this) +
        maxNameLength() + 9 * 2 /* 2 margins */);

    /* Activate edit triggers only now to avoid influencing
     * HD Attachments table during data loading. */
    mTwAts->setEditTriggers (QAbstractItemView::CurrentChanged |
                             QAbstractItemView::SelectedClicked |
                             QAbstractItemView::EditKeyPressed);

    /* That little hack allows avoid one of qt4 children focusing bug */
    QWidget *current = QApplication::focusWidget();
    mTwAts->setFocus (Qt::TabFocusReason);
    if (current)
        current->setFocus (Qt::TabFocusReason);
}

QUuid VBoxVMSettingsHD::getWithMediaManager (const QUuid &aInitialId)
{
    /* Run Media Manager */
    VBoxMediaManagerDlg dlg (this);
    dlg.setup (VBoxDefs::MediaType_HardDisk,
               true /* do select? */,
               false /* do refresh? */,
               mMachine,
               aInitialId,
               HDSettings::instance()->showDiffs());

    return dlg.exec() == QDialog::Accepted ? dlg.selectedId() : QUuid();
}

QUuid VBoxVMSettingsHD::getWithNewHDWizard()
{
    /* Run New HD Wizard */
    VBoxNewHDWzd dlg (this);

    return dlg.exec() == QDialog::Accepted ? dlg.hardDisk().GetId() : QUuid();
}

int VBoxVMSettingsHD::maxNameLength() const
{
    QList <SlotValue> slts (HDSettings::instance()->slotsList());
    int nameLength = 0;
    for (int i = 0; i < slts.size(); ++ i)
    {
        int length = mTwAts->fontMetrics().width (slts [i].name);
        nameLength = length > nameLength ? length : nameLength;
    }
    return nameLength;
}

