/**
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * "Shared Folders" settings dialog UI include (Qt Designer)
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
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

/****************************************************************************
** ui.h extension file, included from the uic-generated form implementation.
**
** If you want to add, delete, or rename functions or slots, use
** Qt Designer to update this file, preserving your code.
**
** You should not define a constructor or destructor in this file.
** Instead, write your code in functions called init() and destroy().
** These will automatically be called by the form's constructor and
** destructor.
*****************************************************************************/


class VBoxRichListItem : public QListViewItem
{
public:

    enum { QIRichListItemId = 1010 };

    enum FormatType
    {
        IncorrectFormat = 0,
        EllipsisStart   = 1,
        EllipsisMiddle  = 2,
        EllipsisEnd     = 3,
        EllipsisFile    = 4
    };

    VBoxRichListItem (FormatType aFormat, QListView *aParent,
                      const QString& aName, const QString& aNull,
                      const QString& aKey) :
        QListViewItem (aParent, aName, aNull, aKey), mFormat (aFormat)
    {
    }

    VBoxRichListItem (FormatType aFormat, QListViewItem *aParent,
                      const QString& aName, const QString& aPath) :
        QListViewItem (aParent, aName, aPath), mFormat (aFormat)
    {
        mTextList << aName << aPath;
    }

    int rtti() const { return QIRichListItemId; }

    QString getText (int aIndex)
    {
        return aIndex >= 0 && aIndex < (int)mTextList.size() ?
            mTextList [aIndex] : QString::null;
    }

    void updateText (int aColumn, const QString &aText)
    {
        if (aColumn >= 0 && aColumn < (int)mTextList.size())
            mTextList [aColumn] = aText;
    }

protected:

    void paintCell (QPainter *aPainter, const QColorGroup &aColorGroup,
                    int aColumn, int aWidth, int aAlign)
    {
        processColumn (aColumn, aWidth);
        QListViewItem::paintCell (aPainter, aColorGroup, aColumn, aWidth, aAlign);
    }

    void processColumn (int aColumn, int aWidth)
    {
        QString oneString = aColumn >= 0 && aColumn < (int)mTextList.size() ?
            mTextList [aColumn] : QString::null;
        if (oneString.isNull())
            return;
        int oldSize = listView()->fontMetrics().width (oneString);
        int indentSize = listView()->fontMetrics().width ("...x");

        /* compress text */
        int start = 0;
        int finish = 0;
        int position = 0;
        int textWidth = 0;
        do {
            textWidth = listView()->fontMetrics().width (oneString);
            if (textWidth + indentSize > aWidth)
            {
                start  = 0;
                finish = oneString.length();

                /* selecting remove position */
                switch (mFormat)
                {
                    case EllipsisStart:
                        position = start;
                        break;
                    case EllipsisMiddle:
                        position = (finish - start) / 2;
                        break;
                    case EllipsisEnd:
                        position = finish - 1;
                        break;
                    case EllipsisFile:
                    {
                        QRegExp regExp ("([\\\\/][^\\\\^/]+[\\\\/]?$)");
                        int newFinish = regExp.search (oneString);
                        if (newFinish != -1)
                            finish = newFinish;
                        position = (finish - start) / 2;
                        break;
                    }
                    default:
                        AssertMsgFailed (("Invalid format type\n"));
                }

                if (position == finish)
                   break;
                oneString.remove (position, 1);
            }
        } while (textWidth + indentSize > aWidth);
        if (position || mFormat == EllipsisFile) oneString.insert (position, "...");

        int newSize = listView()->fontMetrics().width (oneString);
        setText (aColumn, newSize < oldSize ? oneString : mTextList [aColumn]);
    }

    FormatType  mFormat;
    QStringList mTextList;
};


class VBoxAddSFDialog : public QDialog
{
    Q_OBJECT

public:

    enum DialogType { AddDialogType, EditDialogType };

    VBoxAddSFDialog (QWidget *aParent, VBoxAddSFDialog::DialogType aType,
                     bool aEnableSelector /* for "permanent" checkbox */) :
        QDialog (aParent, "VBoxAddSFDialog", true /* modal */),
        mLePath (0), mLeName (0), mCbPermanent (0)
    {
        switch (aType)
        {
            case AddDialogType:
                setCaption (tr ("Add Share"));
                break;
            case EditDialogType:
                setCaption (tr ("Edit Share"));
                break;
            default:
                AssertMsgFailed (("Incorrect SF Dialog type\n"));
        }
        QVBoxLayout *mainLayout = new QVBoxLayout (this, 10, 10, "mainLayout");

        /* Setup Input layout */
        QGridLayout *inputLayout = new QGridLayout (mainLayout, 3, 3, 10, "inputLayout");
        QLabel *lbPath = new QLabel (tr ("Folder Path"), this);
        mLePath = new QLineEdit (this);
        QToolButton *tbPath = new QToolButton (this);
        QLabel *lbName = new QLabel (tr ("Folder Name"), this);
        mLeName = new QLineEdit (this);
        tbPath->setIconSet (VBoxGlobal::iconSet ("select_file_16px.png",
                                                 "select_file_dis_16px.png"));
        tbPath->setFocusPolicy (QWidget::TabFocus);
        connect (mLePath, SIGNAL (textChanged (const QString &)),
                 this, SLOT (validate()));
        connect (mLeName, SIGNAL (textChanged (const QString &)),
                 this, SLOT (validate()));
        connect (tbPath, SIGNAL (clicked()), this, SLOT (showFileDialog()));
        QWhatsThis::add (mLePath, tr ("Displays the path to an existing folder on the host PC."));
        QWhatsThis::add (mLeName, tr ("Displays the name of the shared folder "
                                      "(as it will be seen by the guest OS)."));
        QWhatsThis::add (tbPath, tr ("Opens the dialog to select a folder."));

        inputLayout->addWidget (lbPath,  0, 0);
        inputLayout->addWidget (mLePath, 0, 1);
        inputLayout->addWidget (tbPath,  0, 2);
        inputLayout->addWidget (lbName,  1, 0);
        inputLayout->addMultiCellWidget (mLeName, 1, 1, 1, 2);

        if (aEnableSelector)
        {
            mCbPermanent = new QCheckBox ("&Make Permanent", this);
            mCbPermanent->setChecked (true);
            inputLayout->addMultiCellWidget (mCbPermanent, 2, 2, 0, 2);
        }

        /* Setup Button layout */
        QHBoxLayout *buttonLayout = new QHBoxLayout (mainLayout, 10, "buttonLayout");
        mBtOk = new QPushButton (tr ("OK"), this, "btOk");
        QSpacerItem *spacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
        QPushButton *btCancel = new QPushButton (tr ("Cancel"), this, "btCancel");
        connect (mBtOk, SIGNAL (clicked()), this, SLOT (accept()));
        connect (btCancel, SIGNAL (clicked()), this, SLOT (reject()));

        buttonLayout->addWidget (mBtOk);
        buttonLayout->addItem (spacer);
        buttonLayout->addWidget (btCancel);

        /* Validate fields */
        validate();
    }

    ~VBoxAddSFDialog() {}

    QString getPath() { return mLePath->text(); }
    QString getName() { return mLeName->text(); }
    bool getPermanent()
    {
        return mCbPermanent ? mCbPermanent->isChecked() : true;
    }

    void setPath (const QString &aPath) { mLePath->setText (aPath); }
    void setName (const QString &aName) { mLeName->setText (aName); }
    void setPermanent (bool aPermanent)
    {
        if (mCbPermanent)
        {
            mCbPermanent->setChecked (aPermanent);
            mCbPermanent->setEnabled (!aPermanent);
        }
    }

private slots:

    void validate()
    {
        mBtOk->setEnabled (!mLePath->text().isEmpty() && !mLeName->text().isEmpty());
    }

    void showFileDialog()
    {
        QString folder = vboxGlobal().getExistingDirectory (QDir::rootDirPath(),
                                                  this, "addSharedFolderDialog",
                                                  tr ("Select a folder to share"));
        if (folder.isNull())
            return;

        QString folderName = QDir::convertSeparators (folder);
        QRegExp commonRule ("[\\\\/]([^\\\\^/]+)[\\\\/]?$");
        QRegExp rootRule ("(([a-zA-Z])[^\\\\^/])?[\\\\/]$");
        if (commonRule.search (folderName) != -1)
        {
            /* processing non-root folder */
            mLePath->setText (folderName.remove (QRegExp ("[\\\\/]$")));
            mLeName->setText (commonRule.cap (1));
        }
        else if (rootRule.search (folderName) != -1)
        {
            /* processing root folder */
            mLePath->setText (folderName);
#if defined(Q_WS_WIN32)
            mLeName->setText (rootRule.cap (2) + "_DRIVE");
#elif defined(Q_WS_X11)
            mLeName->setText ("ROOT");
#endif
        }
        else
            return; /* hm, what type of folder it was? */
    }

private:

    void showEvent (QShowEvent *aEvent)
    {
        setFixedHeight (height());
        QDialog::showEvent (aEvent);
    }

    QPushButton *mBtOk;
    QLineEdit *mLePath;
    QLineEdit *mLeName;
    QCheckBox *mCbPermanent;
};


void VBoxSharedFoldersSettings::init()
{
    mDialogType = WrongType;
    new QIListViewSelectionPreserver (this, listView);
    listView->setShowToolTips (false);
    listView->setRootIsDecorated (true);
    tbAdd->setIconSet (VBoxGlobal::iconSet ("add_shared_folder_16px.png",
                                            "add_shared_folder_disabled_16px.png"));
    tbEdit->setIconSet (VBoxGlobal::iconSet ("edit_shared_folder_16px.png",
                                             "edit_shared_folder_disabled_16px.png"));
    tbRemove->setIconSet (VBoxGlobal::iconSet ("revome_shared_folder_16px.png",
                                               "revome_shared_folder_disabled_16px.png"));
    QToolTip::add (tbAdd, tr ("Add a new shared folder"));
    QToolTip::add (tbEdit, tr ("Edit the selected shared folder"));
    QToolTip::add (tbRemove, tr ("Remove the selected shared folder"));
    connect (tbAdd, SIGNAL (clicked()), this, SLOT (tbAddPressed()));
    connect (tbEdit, SIGNAL (clicked()), this, SLOT (tbEditPressed()));
    connect (tbRemove, SIGNAL (clicked()), this, SLOT (tbRemovePressed()));
    connect (listView, SIGNAL (currentChanged (QListViewItem *)),
             this, SLOT (processCurrentChanged (QListViewItem *)));
    connect (listView, SIGNAL (onItem (QListViewItem *)),
             this, SLOT (processOnItem (QListViewItem *)));

    mIsListViewChanged = false;
}

void VBoxSharedFoldersSettings::setDialogType (int aType)
{
    mDialogType = aType;
}


void VBoxSharedFoldersSettings::removeSharedFolder (const QString & aName,
                                                    const QString & aPath,
                                                    SFDialogType aType)
{
    switch (aType)
    {
        case GlobalType:
        {
            /* This feature is not implemented yet */
            AssertMsgFailed (("Global shared folders are not implemented yet\n"));
            break;
        }
        case MachineType:
        {
            Assert (!mMachine.isNull());
            mMachine.RemoveSharedFolder (aName);
            if (!mMachine.isOk())
                vboxProblem().cannotRemoveSharedFolder (this, mMachine,
                                                        aName, aPath);
            break;
        }
        case ConsoleType:
        {
            Assert (!mConsole.isNull());
            mConsole.RemoveSharedFolder (aName);
            if (!mConsole.isOk())
                vboxProblem().cannotRemoveSharedFolder (this, mConsole,
                                                        aName, aPath);
            break;
        }
        default:
        {
            AssertMsgFailed (("Incorrect shared folder type\n"));
        }
    }
}

void VBoxSharedFoldersSettings::createSharedFolder (const QString & aName,
                                                    const QString & aPath,
                                                    SFDialogType aType)
{
    switch (aType)
    {
        case GlobalType:
        {
            /* This feature is not implemented yet */
            AssertMsgFailed (("Global shared folders are not implemented yet\n"));
            break;
        }
        case MachineType:
        {
            Assert (!mMachine.isNull());
            mMachine.CreateSharedFolder (aName, aPath);
            if (!mMachine.isOk())
                vboxProblem().cannotCreateSharedFolder (this, mMachine,
                                                        aName, aPath);
            break;
        }
        case ConsoleType:
        {
            Assert (!mConsole.isNull());
            mConsole.CreateSharedFolder (aName, aPath);
            if (!mConsole.isOk())
                vboxProblem().cannotCreateSharedFolder (this, mConsole,
                                                        aName, aPath);
            break;
        }
        default:
        {
            AssertMsgFailed (("Incorrect shared folder type\n"));
        }
    }
}


void VBoxSharedFoldersSettings::getFromGlobal()
{
    /* This feature is not implemented yet */
    AssertMsgFailed (("Global shared folders are not implemented yet\n"));

    /*
    QString name = tr (" Global Folders");
    QString key (QString::number (GlobalType));
    VBoxRichListItem *root = new VBoxRichListItem (VBoxRichListItem::EllipsisEnd,
                                                   listView, name, QString::null, key);
    getFrom (vboxGlobal().virtualBox().GetSharedFolders().Enumerate(), root);
    */
}

void VBoxSharedFoldersSettings::getFromMachine (const CMachine &aMachine)
{
    mMachine = aMachine;
    QString name = tr (" Machine Folders");
    QString key (QString::number (MachineType));
    VBoxRichListItem *root = new VBoxRichListItem (VBoxRichListItem::EllipsisEnd,
                                                   listView, name, QString::null, key);
    getFrom (mMachine.GetSharedFolders().Enumerate(), root);
}

void VBoxSharedFoldersSettings::getFromConsole (const CConsole &aConsole)
{
    mConsole = aConsole;
    QString name = tr (" Transient Folders");
    QString key (QString::number (ConsoleType));
    VBoxRichListItem *root = new VBoxRichListItem (VBoxRichListItem::EllipsisEnd,
                                                   listView, name, QString::null, key);
    getFrom (mConsole.GetSharedFolders().Enumerate(), root);
}

void VBoxSharedFoldersSettings::getFrom (const CSharedFolderEnumerator &aEn,
                                         QListViewItem *aRoot)
{
    aRoot->setSelectable (false);
    while (aEn.HasMore())
    {
        CSharedFolder sf = aEn.GetNext();
        new VBoxRichListItem (VBoxRichListItem::EllipsisFile, aRoot,
                              sf.GetName(), sf.GetHostPath());
    }
    listView->setOpen (aRoot, true);
    listView->setCurrentItem (aRoot->firstChild());
    processCurrentChanged (aRoot->firstChild());
}


void VBoxSharedFoldersSettings::putBackToGlobal()
{
    /* This feature is not implemented yet */
    AssertMsgFailed (("Global shared folders are not implemented yet\n"));

    /*
    if (!mIsListViewChanged) return;
    // This function is only available for GlobalType dialog
    Assert (mDialogType == GlobalType);
    // Searching for GlobalType item's root
    QListViewItem *root = listView->findItem (QString::number (GlobalType), 2);
    Assert (root);
    CSharedFolderEnumerator en = vboxGlobal().virtualBox().GetSharedFolders().Enumerate();
    putBackTo (en, root);
    */
}

void VBoxSharedFoldersSettings::putBackToMachine()
{
    if (!mIsListViewChanged)
        return;

    /* This function is only available for MachineType dialog */
    Assert (mDialogType & MachineType);
    /* Searching for MachineType item's root */
    QListViewItem *root = listView->findItem (QString::number (MachineType), 2);
    Assert (root);
    CSharedFolderEnumerator en = mMachine.GetSharedFolders().Enumerate();
    putBackTo (en, root);
}

void VBoxSharedFoldersSettings::putBackToConsole()
{
    if (!mIsListViewChanged)
        return;

    /* This function is only available for ConsoleType dialog */
    Assert (mDialogType & ConsoleType);
    /* Searching for ConsoleType item's root */
    QListViewItem *root = listView->findItem (QString::number (ConsoleType), 2);
    Assert (root);
    CSharedFolderEnumerator en = mConsole.GetSharedFolders().Enumerate();
    putBackTo (en, root);
}

void VBoxSharedFoldersSettings::putBackTo (CSharedFolderEnumerator &aEn,
                                           QListViewItem *aRoot)
{
    Assert (!aRoot->text (2).isNull());
    SFDialogType type = (SFDialogType)aRoot->text (2).toInt();

    /* deleting all existing folders if the list */
    while (aEn.HasMore())
    {
        CSharedFolder sf = aEn.GetNext();
        removeSharedFolder (sf.GetName(), sf.GetHostPath(), type);
    }

    /* saving all machine related list view items */
    QListViewItem *iterator = aRoot->firstChild();
    while (iterator)
    {
        VBoxRichListItem *item = 0;
        if (iterator->rtti() == VBoxRichListItem::QIRichListItemId)
            item = static_cast<VBoxRichListItem*> (iterator);
        if (item && !item->getText (0).isNull() && !item->getText (1).isNull())
            createSharedFolder (item->getText (0), item->getText (1), type);
        else
            AssertMsgFailed (("Incorrect listview item type\n"));
        iterator = iterator->nextSibling();
    }
}


QListViewItem* VBoxSharedFoldersSettings::searchRoot (bool aIsPermanent)
{
    if (!aIsPermanent)
        return listView->findItem (QString::number (ConsoleType), 2);
    else if (mDialogType & MachineType)
        return listView->findItem (QString::number (MachineType), 2);
    else
        return listView->findItem (QString::number (GlobalType), 2);
}

void VBoxSharedFoldersSettings::tbAddPressed()
{
    /* Invoke Add-Box Dialog */
    VBoxAddSFDialog dlg (this, VBoxAddSFDialog::AddDialogType,
                         mDialogType & ConsoleType);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString name = dlg.getName();
    QString path = dlg.getPath();
    bool isPermanent = dlg.getPermanent();
    /* Shared folder's name & path could not be empty */
    Assert (!name.isEmpty() && !path.isEmpty());
    /* Searching root for the new listview item */
    QListViewItem *root = searchRoot (isPermanent);
    Assert (root);
    /* Appending a new listview item to the root */
    VBoxRichListItem *item = new VBoxRichListItem (
        VBoxRichListItem::EllipsisFile, root, name, path);
    /* Make the created item selected */
    listView->ensureItemVisible (item);
    listView->setCurrentItem (item);
    processCurrentChanged (item);
    listView->setFocus();

    mIsListViewChanged = true;
}

void VBoxSharedFoldersSettings::tbEditPressed()
{
    /* Check selected item */
    QListViewItem *selectedItem = listView->selectedItem();
    VBoxRichListItem *item =
        selectedItem->rtti() == VBoxRichListItem::QIRichListItemId ?
        static_cast<VBoxRichListItem*> (selectedItem) : 0;
    Assert (item);
    Assert (item->parent());
    /* Invoke Edit-Box Dialog */
    VBoxAddSFDialog dlg (this, VBoxAddSFDialog::EditDialogType,
                         mDialogType & ConsoleType);
    dlg.setPath (item->getText (1));
    dlg.setName (item->getText (0));
    dlg.setPermanent ((SFDialogType)item->parent()->text (2).toInt()
                      != ConsoleType);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString name = dlg.getName();
    QString path = dlg.getPath();
    bool isPermanent = dlg.getPermanent();
    /* Shared folder's name & path could not be empty */
    Assert (!name.isEmpty() && !path.isEmpty());
    /* Searching new root for the selected listview item */
    QListViewItem *root = searchRoot (isPermanent);
    Assert (root);
    /* Updating an edited listview item */
    item->updateText (1, path);
    item->updateText (0, name);
    if (item->parent() != root)
    {
        /* Move the selected item into new location */
        item->parent()->takeItem (item);
        root->insertItem (item);

        /* Make the created item selected */
        listView->ensureItemVisible (item);
        listView->setCurrentItem (item);
        processCurrentChanged (item);
        listView->setFocus();
    }

    mIsListViewChanged = true;
}

void VBoxSharedFoldersSettings::tbRemovePressed()
{
    Assert (listView->selectedItem());
    delete listView->selectedItem();
    mIsListViewChanged = true;
}


void VBoxSharedFoldersSettings::processOnItem (QListViewItem *aItem)
{
    VBoxRichListItem *item = 0;
    if (aItem->rtti() == VBoxRichListItem::QIRichListItemId)
        item = static_cast<VBoxRichListItem*> (aItem);
    Assert (item);
    QString tip = tr ("<nobr>Name:&nbsp;&nbsp;%1</nobr><br>"
                      "<nobr>Path:&nbsp;&nbsp;%2</nobr>")
                      .arg (item->getText (0)).arg (item->getText (1));
    if (!item->getText (0).isNull() && !item->getText (1).isNull())
        QToolTip::add (listView->viewport(), listView->itemRect (aItem), tip);
    else
        QToolTip::remove (listView->viewport());
}

void VBoxSharedFoldersSettings::processCurrentChanged (QListViewItem *aItem)
{
    if (aItem && aItem->isSelectable() && listView->selectedItem() != aItem)
        listView->setSelected (aItem, true);
    bool addEnabled = aItem &&
                      (isEditable (aItem->text (2)) ||
                       aItem->parent() && isEditable (aItem->parent()->text (2)));
    bool removeEnabled = aItem && aItem->parent() &&
                         isEditable (aItem->parent()->text (2));
    tbAdd->setEnabled (addEnabled);
    tbEdit->setEnabled (removeEnabled);
    tbRemove->setEnabled (removeEnabled);
}

void VBoxSharedFoldersSettings::processDoubleClick (QListViewItem *aItem)
{
    bool editEnabled = aItem && aItem->parent() &&
        isEditable (aItem->parent()->text (2));
    if (editEnabled)
        tbEditPressed();
}

bool VBoxSharedFoldersSettings::isEditable (const QString &aKey)
{
    /* mDialogType should be sutup already */
    Assert (mDialogType);
    /* simple item has no key information */
    if (aKey.isEmpty())
        return false;
    SFDialogType type = (SFDialogType)aKey.toInt();
    if (!type)
        AssertMsgFailed (("Incorrect listview item key value\n"));
    return mDialogType & type;
}


#include "VBoxSharedFoldersSettings.ui.moc"
