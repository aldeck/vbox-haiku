/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxImportAppliance class implementation
 */

/*
 * Copyright (C) 2009 Sun Microsystems, Inc.
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

#include "VBoxImportAppliance.h"
#include "VBoxGlobal.h"
#include "VBoxProblemReporter.h"
#include "VBoxFilePathSelectorWidget.h"

#include <QItemDelegate>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>

class ModelItem;

////////////////////////////////////////////////////////////////////////////////
// Globals

enum TreeViewSection { DescriptionSection = 0, OriginalValueSection, ConfigValueSection };

////////////////////////////////////////////////////////////////////////////////
// Private helper class declarations

class VirtualSystemSortProxyModel: public QSortFilterProxyModel
{
public:
    VirtualSystemSortProxyModel (QObject *aParent = NULL);

protected:
    bool lessThan (const QModelIndex &aLeft, const QModelIndex &aRight) const;

    static KVirtualSystemDescriptionType mSortList[];
};

class VirtualSystemModel: public QAbstractItemModel
{

public:
    VirtualSystemModel (QVector<CVirtualSystemDescription>& aVSDs, QObject *aParent = NULL);

    inline QModelIndex index (int aRow, int aColumn, const QModelIndex &aParent = QModelIndex()) const;
    inline QModelIndex parent (const QModelIndex &aIndex) const;
    inline int rowCount (const QModelIndex &aParent = QModelIndex()) const;
    inline int columnCount (const QModelIndex &aParent = QModelIndex()) const;
    inline bool setData (const QModelIndex &aIndex, const QVariant &aValue, int aRole);
    inline QVariant data (const QModelIndex &aIndex, int aRole = Qt::DisplayRole) const;
    inline Qt::ItemFlags flags (const QModelIndex &aIndex) const;
    inline QVariant headerData (int aSection, Qt::Orientation aOrientation, int aRole) const;

    inline void restoreDefaults (const QModelIndex& aParent = QModelIndex());
    inline void putBack();

private:
    /* Private member vars */
    ModelItem *mRootItem;
};

////////////////////////////////////////////////////////////////////////////////
// ModelItem

enum ModelItemType { RootType, VirtualSystemType, HardwareType };

/* This & the following derived classes represent the data items of a Virtual
   System. All access/manipulation is done with the help of virtual functions
   to keep the interface clean. ModelItem is able to handle tree structures
   with a parent & several children's. */
class ModelItem
{
public:
    ModelItem (int aNumber, ModelItemType aType, ModelItem *aParent = NULL)
      : mNumber (aNumber)
      , mType (aType)
      , mParentItem (aParent)
    {}

    ~ModelItem()
    {
        qDeleteAll (mChildItems);
    }

    ModelItem *parent() const { return mParentItem; }

    void appendChild (ModelItem *aChild)
    {
        AssertPtr (aChild);
        mChildItems << aChild;
    }
    ModelItem * child (int aRow) const { return mChildItems.value (aRow); }

    int row() const
    {
        if (mParentItem)
            return mParentItem->mChildItems.indexOf (const_cast<ModelItem*> (this));

        return 0;
    }

    int childCount() const { return mChildItems.count(); }
    int columnCount() const { return 3; }

    virtual Qt::ItemFlags itemFlags (int aColumn) const { return Qt::NoItemFlags; }
    virtual bool setData (int aColumn, const QVariant &aValue, int aRole) { return false; }
    virtual QVariant data (int aColumn, int aRole) const { return QVariant(); }
    virtual QWidget * createEditor (QWidget *aParent, const QStyleOptionViewItem &aOption, const QModelIndex &aIndex) const { return NULL; }
    virtual bool setEditorData (QWidget *aEditor, const QModelIndex &aIndex) const { return false; }
    virtual bool setModelData (QWidget *aEditor, QAbstractItemModel *aModel, const QModelIndex &aIndex) { return false; }

    virtual void restoreDefaults() {}
    virtual void putBack (QVector<QString>& aFinalValues, QVector<BOOL>& aFinalStates)
    {
        for (int i = 0; i < childCount(); ++i)
            child (i)->putBack (aFinalValues, aFinalStates);
    }

    ModelItemType type() const { return mType; }

protected:
    int mNumber;
    ModelItemType mType;

    ModelItem *mParentItem;
    QList<ModelItem*> mChildItems;
};

/* This class represent a Virtual System with an index. */
class VirtualSystemItem: public ModelItem
{
public:
    VirtualSystemItem (int aNumber, CVirtualSystemDescription aDesc, ModelItem *aParent)
      : ModelItem (aNumber, VirtualSystemType, aParent)
      , mDesc (aDesc)
    {}

    virtual QVariant data (int aColumn, int aRole) const
    {
        QVariant v;
        if (aColumn == DescriptionSection &&
            aRole == Qt::DisplayRole)
            v = VBoxImportApplianceDlg::tr ("Virtual System %1").arg (mNumber + 1);
        return v;
    }

    virtual void putBack (QVector<QString>& aFinalValues, QVector<BOOL>& aFinalStates)
    {
        /* Resize the vectors */
        unsigned long count = mDesc.GetCount();
        aFinalValues.resize (count);
        aFinalStates.resize (count);
        /* Recursively fill the vectors */
        ModelItem::putBack (aFinalValues, aFinalStates);
        /* Set all final values at once */
        mDesc.SetFinalValues (aFinalStates, aFinalValues);
    }

private:
    CVirtualSystemDescription mDesc;
};

/* This class represent an hardware item of a Virtual System. All values of
   KVirtualSystemDescriptionType are supported & handled differently. */
class HardwareItem: public ModelItem
{
    friend class VirtualSystemSortProxyModel;
public:

    HardwareItem (int aNumber,
                  KVirtualSystemDescriptionType aType,
                  const QString &aRef,
                  const QString &aOrigValue,
                  const QString &aConfigValue,
                  const QString &aExtraConfigValue,
                  ModelItem *aParent)
        : ModelItem (aNumber, HardwareType, aParent)
        , mType (aType)
        , mRef (aRef)
        , mOrigValue (aOrigValue)
        , mConfigValue (aConfigValue)
        , mConfigDefaultValue (aConfigValue)
        , mExtraConfigValue (aExtraConfigValue)
        , mCheckState (Qt::Checked)
    {}

    virtual void putBack (QVector<QString>& aFinalValues, QVector<BOOL>& aFinalStates)
    {
        aFinalValues[mNumber] = mConfigValue;
        aFinalStates[mNumber] = mCheckState == Qt::Checked;
        ModelItem::putBack (aFinalValues, aFinalStates);
    }

    bool setData (int aColumn, const QVariant &aValue, int aRole)
    {
        bool fDone = false;
        switch (aRole)
        {
            case Qt::CheckStateRole:
            {
                if (aColumn == ConfigValueSection &&
                    (mType == KVirtualSystemDescriptionType_Floppy ||
                     mType == KVirtualSystemDescriptionType_CDROM ||
                     mType == KVirtualSystemDescriptionType_USBController ||
                     mType == KVirtualSystemDescriptionType_SoundCard ||
                     mType == KVirtualSystemDescriptionType_NetworkAdapter))
                {
                    mCheckState = static_cast<Qt::CheckState> (aValue.toInt());
                    fDone = true;
                }
                break;
            }
            default: break;
        }
        return fDone;
    }

    virtual QVariant data (int aColumn, int aRole) const
    {
        QVariant v;
        switch (aRole)
        {
            case Qt::DisplayRole:
            {
                if (aColumn == DescriptionSection)
                {
                    switch (mType)
                    {
                        case KVirtualSystemDescriptionType_Name: v = VBoxImportApplianceDlg::tr ("Name"); break;
                        case KVirtualSystemDescriptionType_OS: v = VBoxImportApplianceDlg::tr ("Guest OS Type"); break;
                        case KVirtualSystemDescriptionType_CPU: v = VBoxImportApplianceDlg::tr ("CPU"); break;
                        case KVirtualSystemDescriptionType_Memory: v = VBoxImportApplianceDlg::tr ("RAM"); break;
                        case KVirtualSystemDescriptionType_HardDiskControllerIDE: v = VBoxImportApplianceDlg::tr ("Hard Disk Controller IDE"); break;
                        case KVirtualSystemDescriptionType_HardDiskControllerSATA: v = VBoxImportApplianceDlg::tr ("Hard Disk Controller SATA"); break;
                        case KVirtualSystemDescriptionType_HardDiskControllerSCSI: v = VBoxImportApplianceDlg::tr ("Hard Disk Controller SCSI"); break;
                        case KVirtualSystemDescriptionType_CDROM: v = VBoxImportApplianceDlg::tr ("DVD"); break;
                        case KVirtualSystemDescriptionType_Floppy: v = VBoxImportApplianceDlg::tr ("Floppy"); break;
                        case KVirtualSystemDescriptionType_NetworkAdapter: v = VBoxImportApplianceDlg::tr ("Network Adapter"); break;
                        case KVirtualSystemDescriptionType_USBController: v = VBoxImportApplianceDlg::tr ("USB Controller"); break;
                        case KVirtualSystemDescriptionType_SoundCard: v = VBoxImportApplianceDlg::tr ("Sound Card"); break;
                        case KVirtualSystemDescriptionType_HardDiskImage: v = VBoxImportApplianceDlg::tr ("Virtual Disk Image"); break;
                        default: v = VBoxImportApplianceDlg::tr ("Unknown Hardware Item"); break;
                    }
                }
                else if (aColumn == OriginalValueSection)
                    v = mOrigValue;
                else if (aColumn == ConfigValueSection)
                {
                    switch (mType)
                    {
                        case KVirtualSystemDescriptionType_OS: v = vboxGlobal().vmGuestOSTypeDescription (mConfigValue); break;
                        case KVirtualSystemDescriptionType_Memory: v = mConfigValue + " " + VBoxImportApplianceDlg::tr ("MB"); break;
                        case KVirtualSystemDescriptionType_SoundCard: v = vboxGlobal().toString (static_cast<KAudioControllerType> (mConfigValue.toInt())); break;
                        case KVirtualSystemDescriptionType_NetworkAdapter: v = vboxGlobal().toString (static_cast<KNetworkAdapterType> (mConfigValue.toInt())); break;
                        default: v = mConfigValue; break;
                    }
                }
                break;
            }
            case Qt::ToolTipRole:
            {
                if (aColumn == ConfigValueSection)
                {
                    if (!mOrigValue.isEmpty())
                        v = VBoxImportApplianceDlg::tr ("<b>Original Value:</b> %1").arg (mOrigValue);
                }
                break;
            }
            case Qt::DecorationRole:
            {
                if (aColumn == DescriptionSection)
                {
                    switch (mType)
                    {
                        case KVirtualSystemDescriptionType_Name:
                        case KVirtualSystemDescriptionType_OS:
                        case KVirtualSystemDescriptionType_CPU:
                        case KVirtualSystemDescriptionType_Memory: v = QIcon (":/machine_16px.png"); break;
                        case KVirtualSystemDescriptionType_HardDiskControllerIDE:
                        case KVirtualSystemDescriptionType_HardDiskControllerSATA:
                        case KVirtualSystemDescriptionType_HardDiskControllerSCSI:
                        case KVirtualSystemDescriptionType_HardDiskImage: v = QIcon (":/hd_16px.png"); break;
                        case KVirtualSystemDescriptionType_CDROM: v = QIcon (":/cd_16px.png"); break;
                        case KVirtualSystemDescriptionType_Floppy: v = QIcon (":/fd_16px.png"); break;
                        case KVirtualSystemDescriptionType_NetworkAdapter: v = QIcon (":/nw_16px.png"); break;
                        case KVirtualSystemDescriptionType_USBController: v = QIcon (":/usb_16px.png"); break;
                        case KVirtualSystemDescriptionType_SoundCard: v = QIcon (":/sound_16px.png"); break;
                        default: break;
                    }
                }
                else if (aColumn == ConfigValueSection &&
                         mType == KVirtualSystemDescriptionType_OS)
                {
                    v = vboxGlobal().vmGuestOSTypeIcon (mConfigValue).scaledToHeight (16, Qt::SmoothTransformation);
                }
                break;
            }
            case Qt::FontRole:
            {
                /* If the item is unchecked mark it with italic text. */
                if (aColumn == ConfigValueSection &&
                    mCheckState == Qt::Unchecked)
                {
                    QFont font = qApp->font();
                    font.setItalic (true);
                    v = font;
                }
                break;
            }
            case Qt::ForegroundRole:
            {
                /* If the item is unchecked mark it with gray text. */
                if (aColumn == ConfigValueSection &&
                    mCheckState == Qt::Unchecked)
                {
                    QPalette pal = qApp->palette();
                    v = pal.brush (QPalette::Disabled, QPalette::WindowText);
                }
                break;
            }
            case Qt::CheckStateRole:
            {
                if (aColumn == ConfigValueSection &&
                    (mType == KVirtualSystemDescriptionType_Floppy ||
                     mType == KVirtualSystemDescriptionType_CDROM ||
                     mType == KVirtualSystemDescriptionType_USBController ||
                     mType == KVirtualSystemDescriptionType_SoundCard ||
                     mType == KVirtualSystemDescriptionType_NetworkAdapter))
                    v = mCheckState;
                break;
            }
        }
        return v;
    }

    virtual Qt::ItemFlags itemFlags (int aColumn) const
    {
        Qt::ItemFlags flags = Qt::NoItemFlags;
        if (aColumn == ConfigValueSection)
        {
            /* Some items are checkable */
            if (mType == KVirtualSystemDescriptionType_Floppy ||
                mType == KVirtualSystemDescriptionType_CDROM ||
                mType == KVirtualSystemDescriptionType_USBController ||
                mType == KVirtualSystemDescriptionType_SoundCard ||
                mType == KVirtualSystemDescriptionType_NetworkAdapter)
                flags |= Qt::ItemIsUserCheckable;
            /* Some items are editable */
            if (mType != KVirtualSystemDescriptionType_CPU && /* CPU not editable for now */
                mType != KVirtualSystemDescriptionType_Floppy && /* The following items didn't have any associated data to edit */
                mType != KVirtualSystemDescriptionType_CDROM &&
                mType != KVirtualSystemDescriptionType_USBController &&
                mCheckState == Qt::Checked) /* Item has to be enabled */
                flags |= Qt::ItemIsEditable;
        }
        return flags;
    }

    virtual QWidget * createEditor (QWidget *aParent, const QStyleOptionViewItem &aOption, const QModelIndex &aIndex) const
    {
        QWidget *editor = NULL;
        if (aIndex.column() == ConfigValueSection)
        {
            switch (mType)
            {
                case KVirtualSystemDescriptionType_OS:
                {
                    QComboBox *e = new QComboBox (aParent);
                    /* Create a list of all possible OS types */
                    QList <CGuestOSType> families = vboxGlobal().vmGuestOSFamilyList();
                    QList <CGuestOSType> types;
                    foreach (const CGuestOSType& family, families)
                        types << vboxGlobal().vmGuestOSTypeList (family.GetFamilyId());
                    /* Fill the combobox */
                    foreach (const CGuestOSType& type, types)
                        e->addItem (vboxGlobal().vmGuestOSTypeIcon (type.GetId()), type.GetDescription(), type.GetId());
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_Name:
                {
                    QLineEdit *e = new QLineEdit (aParent);
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_CPU:
                {
                    QSpinBox *e = new QSpinBox (aParent);
                    e->setRange (VBoxImportAppliance::minGuestCPUCount(), VBoxImportAppliance::maxGuestCPUCount());
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_Memory:
                {
                    QSpinBox *e = new QSpinBox (aParent);
                    e->setRange (VBoxImportAppliance::minGuestRAM(), VBoxImportAppliance::maxGuestRAM());
                    e->setSuffix (" " + VBoxImportApplianceDlg::tr ("MB"));
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_SoundCard:
                {
                    QComboBox *e = new QComboBox (aParent);
                    e->addItem (vboxGlobal().toString (KAudioControllerType_AC97), KAudioControllerType_AC97);
                    e->addItem (vboxGlobal().toString (KAudioControllerType_SB16), KAudioControllerType_SB16);
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_NetworkAdapter:
                {
                    QComboBox *e = new QComboBox (aParent);
                    e->addItem (vboxGlobal().toString (KNetworkAdapterType_Am79C970A), KNetworkAdapterType_Am79C970A);
                    e->addItem (vboxGlobal().toString (KNetworkAdapterType_Am79C973), KNetworkAdapterType_Am79C973);
#ifdef VBOX_WITH_E1000
                    e->addItem (vboxGlobal().toString (KNetworkAdapterType_I82540EM), KNetworkAdapterType_I82540EM);
                    e->addItem (vboxGlobal().toString (KNetworkAdapterType_I82543GC), KNetworkAdapterType_I82543GC);
#endif /* VBOX_WITH_E1000 */
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_HardDiskControllerIDE:
                {
                    QComboBox *e = new QComboBox (aParent);
                    e->addItem (vboxGlobal().toString (KIDEControllerType_PIIX3), "PIIX3");
                    e->addItem (vboxGlobal().toString (KIDEControllerType_PIIX4), "PIIX4");
                    editor = e;
                    break;
                }
                case KVirtualSystemDescriptionType_HardDiskImage:
                {
                /* disabled for now
                    VBoxFilePathSelectorWidget *e = new VBoxFilePathSelectorWidget (aParent);
                    e->setMode (VBoxFilePathSelectorWidget::Mode_File);
                    e->setResetEnabled (false);
                */
                    QLineEdit *e = new QLineEdit (aParent);
                    editor = e;
                    break;
                }
                default: break;
            }
        }
        return editor;
    }

    virtual bool setEditorData (QWidget *aEditor, const QModelIndex &aIndex) const
    {
        bool fDone = false;
        switch (mType)
        {
            case KVirtualSystemDescriptionType_OS:
            case KVirtualSystemDescriptionType_HardDiskControllerIDE:
            {
                if (QComboBox *e = qobject_cast<QComboBox*> (aEditor))
                {
                    int i = e->findData (mConfigValue);
                    if (i != -1)
                        e->setCurrentIndex (i);
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_CPU:
            case KVirtualSystemDescriptionType_Memory:
            {
                if (QSpinBox *e = qobject_cast<QSpinBox*> (aEditor))
                {
                    e->setValue (mConfigValue.toInt());
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_Name:
            {
                if (QLineEdit *e = qobject_cast<QLineEdit*> (aEditor))
                {
                    e->setText (mConfigValue);
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_SoundCard:
            case KVirtualSystemDescriptionType_NetworkAdapter:
            {
                if (QComboBox *e = qobject_cast<QComboBox*> (aEditor))
                {
                    int i = e->findData (mConfigValue.toInt());
                    if (i != -1)
                        e->setCurrentIndex (i);
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_HardDiskImage:
            {
                /* disabled for now
                if (VBoxFilePathSelectorWidget *e = qobject_cast<VBoxFilePathSelectorWidget*> (aEditor))
                {
                    e->setPath (mConfigValue);
                */
                if (QLineEdit *e = qobject_cast<QLineEdit*> (aEditor))
                {
                    e->setText (mConfigValue);
                    fDone = true;
                }
                break;
            }
            default: break;
        }
        return fDone;
    }

    virtual bool setModelData (QWidget *aEditor, QAbstractItemModel *aModel, const QModelIndex &aIndex)
    {
        bool fDone = false;
        switch (mType)
        {
            case KVirtualSystemDescriptionType_OS:
            case KVirtualSystemDescriptionType_HardDiskControllerIDE:
            {
                if (QComboBox *e = qobject_cast<QComboBox*> (aEditor))
                {
                    mConfigValue = e->itemData (e->currentIndex()).toString();
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_CPU:
            case KVirtualSystemDescriptionType_Memory:
            {
                if (QSpinBox *e = qobject_cast<QSpinBox*> (aEditor))
                {
                    mConfigValue = QString::number (e->value());
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_Name:
            {
                if (QLineEdit *e = qobject_cast<QLineEdit*> (aEditor))
                {
                    mConfigValue = e->text();
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_SoundCard:
            case KVirtualSystemDescriptionType_NetworkAdapter:
            {
                if (QComboBox *e = qobject_cast<QComboBox*> (aEditor))
                {
                    mConfigValue = e->itemData (e->currentIndex()).toString();
                    fDone = true;
                }
                break;
            }
            case KVirtualSystemDescriptionType_HardDiskImage:
            {
                /* disabled for now
                if (VBoxFilePathSelectorWidget *e = qobject_cast<VBoxFilePathSelectorWidget*> (aEditor))
                {
                    mConfigValue = e->path();
                */
                if (QLineEdit *e = qobject_cast<QLineEdit*> (aEditor))
                {
                    mConfigValue = e->text();
                    fDone = true;
                }
                break;
            }
            default: break;
        }
        return fDone;
    }

    virtual void restoreDefaults()
    {
        mConfigValue = mConfigDefaultValue;
        mCheckState = Qt::Checked;
    }

private:

    /* Private member vars */
    KVirtualSystemDescriptionType mType;
    QString mRef;
    QString mOrigValue;
    QString mConfigValue;
    QString mConfigDefaultValue;
    QString mExtraConfigValue;
    Qt::CheckState mCheckState;
};

////////////////////////////////////////////////////////////////////////////////
// VirtualSystemDelegate

/* The delegate is used for creating/handling the different editors for the
   various types we support. This class forward the requests to the virtual
   methods of our different ModelItems. If this is not possible the default
   methods of QItemDelegate are used to get some standard behavior. Note: We
   have to handle the proxy model ourself. I really don't understand why Qt is
   not doing this for us. */
class VirtualSystemDelegate: public QItemDelegate
{
public:
    VirtualSystemDelegate (QAbstractProxyModel *aProxy, QObject *aParent = NULL)
        : QItemDelegate (aParent)
        , mProxy (aProxy)
    {}

    QWidget * createEditor (QWidget *aParent, const QStyleOptionViewItem &aOption, const QModelIndex &aIndex) const
    {
        if (!aIndex.isValid())
            return QItemDelegate::createEditor (aParent, aOption, aIndex);

        QModelIndex index (aIndex);
        if (mProxy)
            index = mProxy->mapToSource (aIndex);

        ModelItem *item = static_cast<ModelItem*> (index.internalPointer());
        QWidget *editor = item->createEditor (aParent, aOption, index);

        if (editor == NULL)
            return QItemDelegate::createEditor (aParent, aOption, index);
        else
            return editor;
    }

    void setEditorData (QWidget *aEditor, const QModelIndex &aIndex) const
    {
        if (!aIndex.isValid())
            return QItemDelegate::setEditorData (aEditor, aIndex);

        QModelIndex index (aIndex);
        if (mProxy)
            index = mProxy->mapToSource (aIndex);

        ModelItem *item = static_cast<ModelItem*> (index.internalPointer());

        if (!item->setEditorData (aEditor, index))
            QItemDelegate::setEditorData (aEditor, index);
    }

    void setModelData (QWidget *aEditor, QAbstractItemModel *aModel, const QModelIndex &aIndex) const
    {
        if (!aIndex.isValid())
            return QItemDelegate::setModelData (aEditor, aModel, aIndex);

        QModelIndex index = aModel->index (aIndex.row(), aIndex.column());
        if (mProxy)
            index = mProxy->mapToSource (aIndex);

        ModelItem *item = static_cast<ModelItem*> (index.internalPointer());
        if (!item->setModelData (aEditor, aModel, index))
            QItemDelegate::setModelData (aEditor, aModel, aIndex);
    }

    void updateEditorGeometry (QWidget *aEditor, const QStyleOptionViewItem &aOption, const QModelIndex & /* aIndex */) const
    {
        if (aEditor)
            aEditor->setGeometry (aOption.rect);
    }

    QSize sizeHint (const QStyleOptionViewItem &aOption, const QModelIndex &aIndex) const
    {
        QSize size = QItemDelegate::sizeHint (aOption, aIndex);
        size.setHeight (RT_MAX (24, size.height()));
        return size;
    }
private:
    /* Private member vars */
    QAbstractProxyModel *mProxy;
};

////////////////////////////////////////////////////////////////////////////////
// VirtualSystemSortProxyModel

/* How to sort the items in the tree view */
KVirtualSystemDescriptionType VirtualSystemSortProxyModel::mSortList[] =
{
    KVirtualSystemDescriptionType_Name,
    KVirtualSystemDescriptionType_OS,
    KVirtualSystemDescriptionType_CPU,
    KVirtualSystemDescriptionType_Memory,
    KVirtualSystemDescriptionType_Floppy,
    KVirtualSystemDescriptionType_CDROM,
    KVirtualSystemDescriptionType_USBController,
    KVirtualSystemDescriptionType_SoundCard,
    KVirtualSystemDescriptionType_NetworkAdapter,
    KVirtualSystemDescriptionType_HardDiskControllerIDE,
    KVirtualSystemDescriptionType_HardDiskControllerSATA,
    KVirtualSystemDescriptionType_HardDiskControllerSCSI
};

VirtualSystemSortProxyModel::VirtualSystemSortProxyModel (QObject *aParent)
    : QSortFilterProxyModel (aParent)
{}

bool VirtualSystemSortProxyModel::lessThan (const QModelIndex &aLeft, const QModelIndex &aRight) const
{
    if (!aLeft.isValid() ||
        !aRight.isValid())
        return false;

    ModelItem *leftItem = static_cast<ModelItem*> (aLeft.internalPointer());
    ModelItem *rightItem = static_cast<ModelItem*> (aRight.internalPointer());

    /* We sort hardware types only */
    if (!(leftItem->type() == HardwareType &&
          rightItem->type() == HardwareType))
        return false;

    HardwareItem *hwLeft = static_cast<HardwareItem*> (leftItem);
    HardwareItem *hwRight = static_cast<HardwareItem*> (rightItem);

    for (unsigned int i = 0; i < RT_ELEMENTS (mSortList); ++i)
        if (hwLeft->mType == mSortList[i])
        {
            for (unsigned int a = 0; a <= i; ++a)
                if (hwRight->mType == mSortList[a])
                    return true;
            return false;
        }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
// VirtualSystemModel

/* This class is a wrapper model for our ModelItem. It could be used with any
   TreeView & forward mostly all calls to the methods of ModelItem. The
   ModelItems itself are stored as internal pointers in the QModelIndex class. */
VirtualSystemModel::VirtualSystemModel (QVector<CVirtualSystemDescription>& aVSDs, QObject *aParent /* = NULL */)
   : QAbstractItemModel (aParent)
{
    mRootItem = new ModelItem (0, RootType);
    for (int a = 0; a < aVSDs.size(); ++a)
    {
        CVirtualSystemDescription vs = aVSDs[a];

        VirtualSystemItem *vi = new VirtualSystemItem (a, vs, mRootItem);
        mRootItem->appendChild (vi);

        /* @todo: ask Dmitry about include/COMDefs.h:232 */
        QVector<KVirtualSystemDescriptionType> types;
        QVector<QString> refs;
        QVector<QString> origValues;
        QVector<QString> configValues;
        QVector<QString> extraConfigValues;

        QList<int> hdIndizies;
        QMap<int, HardwareItem*> controllerMap;
        vs.GetDescription (types, refs, origValues, configValues, extraConfigValues);
        for (int i = 0; i < types.size(); ++i)
        {
            /* We add the hard disk images in an second step, so save a
               reference to them. */
            if (types[i] == KVirtualSystemDescriptionType_HardDiskImage)
                hdIndizies << i;
            else
            {
                HardwareItem *hi = new HardwareItem (i, types[i], refs[i], origValues[i], configValues[i], extraConfigValues[i], vi);
                vi->appendChild (hi);
                /* Save the hard disk controller types in an extra map */
                if (types[i] == KVirtualSystemDescriptionType_HardDiskControllerIDE ||
                    types[i] == KVirtualSystemDescriptionType_HardDiskControllerSATA ||
                    types[i] == KVirtualSystemDescriptionType_HardDiskControllerSCSI)
                    controllerMap[i] = hi;
            }
        }
        QRegExp rx ("controller=(\\d+);?");
        /* Now process the hard disk images */
        for (int a = 0; a < hdIndizies.size(); ++a)
        {
            int i = hdIndizies[a];
            QString ecnf = extraConfigValues[i];
            if (rx.indexIn (ecnf) != -1)
            {
                /* Get the controller */
                HardwareItem *ci = controllerMap[rx.cap (1).toInt()];
                if (ci)
                {
                    /* New hardware item as child of the controller */
                    HardwareItem *hi = new HardwareItem (i, types[i], refs[i], origValues[i], configValues[i], extraConfigValues[i], ci);
                    ci->appendChild (hi);
                }
            }
        }
    }
}

QModelIndex VirtualSystemModel::index (int aRow, int aColumn, const QModelIndex &aParent /* = QModelIndex() */) const
{
    if (!hasIndex (aRow, aColumn, aParent))
        return QModelIndex();

    ModelItem *parentItem;

    if (!aParent.isValid())
        parentItem = mRootItem;
    else
        parentItem = static_cast<ModelItem*> (aParent.internalPointer());

    ModelItem *childItem = parentItem->child (aRow);
    if (childItem)
        return createIndex (aRow, aColumn, childItem);
    else
        return QModelIndex();
}

QModelIndex VirtualSystemModel::parent (const QModelIndex &aIndex) const
{
    if (!aIndex.isValid())
        return QModelIndex();

    ModelItem *childItem = static_cast<ModelItem*> (aIndex.internalPointer());
    ModelItem *parentItem = childItem->parent();

    if (parentItem == mRootItem)
        return QModelIndex();

    return createIndex (parentItem->row(), 0, parentItem);
}

int VirtualSystemModel::rowCount (const QModelIndex &aParent /* = QModelIndex() */) const
{
    ModelItem *parentItem;
    if (aParent.column() > 0)
        return 0;

    if (!aParent.isValid())
        parentItem = mRootItem;
    else
        parentItem = static_cast<ModelItem*> (aParent.internalPointer());

    return parentItem->childCount();
}

int VirtualSystemModel::columnCount (const QModelIndex &aParent /* = QModelIndex() */) const
{
    if (aParent.isValid())
        return static_cast<ModelItem*> (aParent.internalPointer())->columnCount();
    else
        return mRootItem->columnCount();
}

bool VirtualSystemModel::setData (const QModelIndex &aIndex, const QVariant &aValue, int aRole)
{
    if (!aIndex.isValid())
        return false;

    ModelItem *item = static_cast<ModelItem*> (aIndex.internalPointer());

    return item->setData (aIndex.column(), aValue, aRole);
}

QVariant VirtualSystemModel::data (const QModelIndex &aIndex, int aRole /* = Qt::DisplayRole */) const
{
    if (!aIndex.isValid())
        return QVariant();

    ModelItem *item = static_cast<ModelItem*> (aIndex.internalPointer());

    return item->data (aIndex.column(), aRole);
}

Qt::ItemFlags VirtualSystemModel::flags (const QModelIndex &aIndex) const
{
    if (!aIndex.isValid())
        return 0;

    ModelItem *item = static_cast<ModelItem*> (aIndex.internalPointer());

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | item->itemFlags (aIndex.column());
}

QVariant VirtualSystemModel::headerData (int aSection, Qt::Orientation aOrientation, int aRole) const
{
    if (aRole != Qt::DisplayRole ||
        aOrientation != Qt::Horizontal)
        return QVariant();

    QString title;
    switch (aSection)
    {
        case DescriptionSection: title = VBoxImportApplianceDlg::tr ("Description"); break;
        case ConfigValueSection: title = VBoxImportApplianceDlg::tr ("Configuration"); break;
    }
    return title;
}

void VirtualSystemModel::restoreDefaults (const QModelIndex& aParent /* = QModelIndex() */)
{
    ModelItem *parentItem;

    if (!aParent.isValid())
        parentItem = mRootItem;
    else
        parentItem = static_cast<ModelItem*> (aParent.internalPointer());

    for (int i = 0; i < parentItem->childCount(); ++i)
    {
        parentItem->child (i)->restoreDefaults();
        restoreDefaults (index (i, 0, aParent));
    }
    emit dataChanged (index (0, 0, aParent), index (parentItem->childCount()-1, 0, aParent));
}

void VirtualSystemModel::putBack()
{
    QVector<QString> v1;
    QVector<BOOL> v2;
    mRootItem->putBack (v1, v2);
}

////////////////////////////////////////////////////////////////////////////////
// VBoxImportAppliance

int VBoxImportAppliance::mMinGuestRAM = -1;
int VBoxImportAppliance::mMaxGuestRAM = -1;
int VBoxImportAppliance::mMinGuestCPUCount = -1;
int VBoxImportAppliance::mMaxGuestCPUCount = -1;

/* static */
void VBoxImportAppliance::import (QWidget *aParent /* = NULL */)
{
    initSystemSettings();

    /* We need a file to import; request one from the user */
    QString file = VBoxGlobal::getOpenFileName ("",
                                                VBoxGlobal::tr ("Open Virtualization Format (%1)").arg ("*.ovf"),
                                                aParent,
                                                VBoxGlobal::tr ("Select an appliance to import"));
    if (!file.isEmpty())
    {
        CVirtualBox vbox = vboxGlobal().virtualBox();
        /* Open the appliance */
        CAppliance appliance = vbox.OpenAppliance (file);
        if (appliance.isOk())
        {
            /* Now we have to interpret that stuff */
            appliance.Interpret();
            if (appliance.isOk())
            {
                /* Let the user do some tuning */
                VBoxImportApplianceDlg settingsDlg (&appliance, aParent);
                if (settingsDlg.exec() == QDialog::Accepted)
                {
                    /* Start the import asynchronously */
                    CProgress progress;
                    progress = appliance.ImportAppliance();
                    if (!appliance.isOk())
                    {
                        vboxProblem().cannotImportAppliance (appliance);
                        return;
                    }
                    /* Show some progress, so the user know whats going on */
                    vboxProblem().showModalProgressDialog (progress, VBoxImportApplianceDlg::tr ("Importing Appliance ..."), aParent);
                    if (!progress.isOk() || progress.GetResultCode() != 0)
                    {
                        vboxProblem().cannotImportAppliance (progress, appliance);
                        return;
                    }
                }
            }
            else
            {
                vboxProblem().cannotImportAppliance (appliance);
                return;
            }
        }
        else
        {
            vboxProblem().cannotImportAppliance (appliance);
            return;
        }
    }
}

/* static */
void VBoxImportAppliance::initSystemSettings()
{
    if (mMinGuestRAM == -1)
    {
        /* We need some global defaults from the current VirtualBox
           installation */
        CSystemProperties sp = vboxGlobal().virtualBox().GetSystemProperties();
        mMinGuestRAM = sp.GetMinGuestRAM();
        mMaxGuestRAM = sp.GetMaxGuestRAM();
        mMinGuestCPUCount = sp.GetMinGuestCPUCount();
        mMaxGuestCPUCount = sp.GetMaxGuestCPUCount();
    }
}

////////////////////////////////////////////////////////////////////////////////
// VBoxImportApplianceDlg

VBoxImportApplianceDlg::VBoxImportApplianceDlg (CAppliance *aAppliance, QWidget *aParent)
    : QIWithRetranslateUI<QIDialog> (aParent)
    , mAppliance (aAppliance)
{
    AssertPtr (aAppliance);

    /* Apply UI decorations */
    Ui::VBoxImportApplianceDlg::setupUi (this);

    QVector<CVirtualSystemDescription> vsds = aAppliance->GetVirtualSystemDescriptions();

    mModel = new VirtualSystemModel (vsds, this);

    VirtualSystemSortProxyModel *proxy = new VirtualSystemSortProxyModel (this);
    proxy->setSourceModel (mModel);
    proxy->sort (DescriptionSection, Qt::DescendingOrder);

    VirtualSystemDelegate *delegate = new VirtualSystemDelegate (proxy, this);

    /* Set our own model */
    mTvSettings->setModel (proxy);
    /* Set our own delegate */
    mTvSettings->setItemDelegate (delegate);
    /* For now we hide the original column. This data is displayed as tooltip
       also. */
    mTvSettings->setColumnHidden (OriginalValueSection, true);
    /* Make the tree looking nicer */
    mTvSettings->setRootIsDecorated (false);
    mTvSettings->setAlternatingRowColors (true);
    mTvSettings->expandAll();
    mTvSettings->header()->setStretchLastSection (true);
    mTvSettings->header()->setResizeMode (QHeaderView::ResizeToContents);

    /* Check for warnings & if there are one display them. */
    bool fWarningsEnabled = false;
    for (int i = 0; i < vsds.size(); ++i)
    {
        QVector<QString> warnings = vsds[i].GetWarnings();
        if (warnings.size() > 0)
        {
            mWarningTextEdit->append (tr("Virtual System %1:").arg (i+1));
            foreach (const QString& text, warnings)
                mWarningTextEdit->append ("- " + text);
            fWarningsEnabled = true;
        }
    }
    mWarningWidget->setShown (fWarningsEnabled);

    connect (mButtonBox->button (QDialogButtonBox::RestoreDefaults), SIGNAL (clicked ()),
             this, SLOT (restoreDefaults ()));

    /* Applying language settings */
    retranslateUi();
}

void VBoxImportApplianceDlg::accept()
{
    mModel->putBack();
    QIDialog::accept();
}

void VBoxImportApplianceDlg::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxImportApplianceDlg::retranslateUi (this);

    mButtonBox->button (QDialogButtonBox::Ok)->setText (tr ("&Import"));
}

void VBoxImportApplianceDlg::restoreDefaults()
{
    mModel->restoreDefaults();
}

