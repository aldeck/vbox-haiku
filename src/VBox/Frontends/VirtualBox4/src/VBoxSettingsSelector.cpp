/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxSettingsSelector class implementation
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
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

#include "VBoxSettingsSelector.h"
#include "VBoxSettingsUtils.h"
#include "VBoxSettingsPage.h"
#include "QITreeWidget.h"
#include "VBoxToolBar.h"

/* Qt includes */
#include <QTabWidget>

enum
{
    /* mTwSelector column numbers */
    treeWidget_Category = 0,
    treeWidget_Id,
    treeWidget_Link
};

class SelectorItem
{
public:
    SelectorItem (const QIcon &aIcon, const QString &aText, int aId, const QString &aLink, VBoxSettingsPage* aPage, int aParentId)
        : mIcon (aIcon)
        , mText (aText)
        , mId (aId)
        , mLink (aLink)
        , mPage (aPage)
        , mParentId (aParentId)
    {}

    QIcon icon() const { return mIcon; }
    QString text() const { return mText; }
    void setText (const QString &aText) { mText = aText; }
    int id() const { return mId; }
    QString link() const { return mLink; }
    VBoxSettingsPage *page() const { return mPage; }
    int parentId() const { return mParentId; }

protected:

    QIcon mIcon;
    QString mText;
    int mId;
    QString mLink;
    VBoxSettingsPage* mPage;
    int mParentId;
};

VBoxSettingsSelector::VBoxSettingsSelector (QWidget *aParent /* = NULL */)
    :QObject (aParent)
{
}

void VBoxSettingsSelector::setItemText (int aId, const QString &aText)
{
    if (SelectorItem *item = findItem (aId))
        item->setText (aText);
}

QString VBoxSettingsSelector::itemTextByPage (VBoxSettingsPage *aPage) const
{
    QString text;
    if (SelectorItem *item = findItemByPage (aPage))
        text = item->text();
    return text;
}

QWidget *VBoxSettingsSelector::idToPage (int aId) const
{
    VBoxSettingsPage *page = NULL;
    if (SelectorItem *item = findItem (aId))
        page = item->page();
    return page;
}

QList<VBoxSettingsPage*> VBoxSettingsSelector::settingPages() const
{
    QList<VBoxSettingsPage*> list;
    foreach (SelectorItem *item, mItemList)
        if (item->page())
            list << item->page();
    return list;
}

QList<QWidget*> VBoxSettingsSelector::rootPages() const
{
    QList<QWidget*> list;
    foreach (SelectorItem *item, mItemList)
        if (item->page())
            list << item->page();
    return list;
}


SelectorItem *VBoxSettingsSelector::findItem (int aId) const
{
    SelectorItem *result = NULL;
    foreach (SelectorItem *item, mItemList)
        if (item->id() == aId)
        {
            result = item;
            break;
        }
    return result;
}

SelectorItem *VBoxSettingsSelector::findItemByLink (const QString &aLink) const
{
    SelectorItem *result = NULL;
    foreach (SelectorItem *item, mItemList)
        if (item->link() == aLink)
        {
            result = item;
            break;
        }
    return result;
}

SelectorItem *VBoxSettingsSelector::findItemByPage (VBoxSettingsPage* aPage) const
{
    SelectorItem *result = NULL;
    foreach (SelectorItem *item, mItemList)
        if (item->page() == aPage)
        {
            result = item;
            break;
        }
    return result;
}

/* VBoxSettingsTreeViewSelector */

/* Returns the path to the item in the form of 'grandparent > parent > item'
 * using the text of the first column of every item. */
static QString path (QTreeWidgetItem *aItem)
{
    static QString sep = ": ";
    QString p;
    QTreeWidgetItem *cur = aItem;
    while (cur)
    {
        if (!p.isNull())
            p = sep + p;
        p = cur->text (treeWidget_Category).simplified() + p;
        cur = cur->parent();
    }
    return p;
}

VBoxSettingsTreeViewSelector::VBoxSettingsTreeViewSelector (QWidget *aParent /* = NULL */)
    :VBoxSettingsSelector (aParent)
{
    mTwSelector = new QITreeWidget (aParent);
    /* Configure the selector */
    QSizePolicy sizePolicy (QSizePolicy::Minimum, QSizePolicy::Expanding);
    sizePolicy.setHorizontalStretch (0);
    sizePolicy.setVerticalStretch (0);
    sizePolicy.setHeightForWidth (mTwSelector->sizePolicy().hasHeightForWidth());
    mTwSelector->setSizePolicy (sizePolicy);
    mTwSelector->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    mTwSelector->setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
    mTwSelector->setRootIsDecorated (false);
    mTwSelector->setUniformRowHeights (true);
    /* Add the columns */
    mTwSelector->headerItem()->setText (treeWidget_Category, "Category");
    mTwSelector->headerItem()->setText (treeWidget_Id, "[id]");
    mTwSelector->headerItem()->setText (treeWidget_Link, "[link]");
    /* Hide unnecessary columns and header */
    mTwSelector->header()->hide();
    mTwSelector->hideColumn (treeWidget_Id);
    mTwSelector->hideColumn (treeWidget_Link);
    /* Setup connections */
    connect (mTwSelector, SIGNAL (currentItemChanged (QTreeWidgetItem*, QTreeWidgetItem*)),
             this, SLOT (settingsGroupChanged (QTreeWidgetItem *, QTreeWidgetItem*)));
}

QWidget *VBoxSettingsTreeViewSelector::widget() const
{
    return mTwSelector;
}

QWidget *VBoxSettingsTreeViewSelector::addItem (const QString &aBigIcon,
                                                const QString &aSmallIcon,
                                                int aId,
                                                const QString &aLink,
                                                VBoxSettingsPage* aPage /* = NULL */,
                                                int aParentId /* = -1 */)
{
    NOREF (aBigIcon);
    QWidget *result = NULL;
    if (aPage != NULL)
    {
        SelectorItem *item = new SelectorItem (QIcon (aSmallIcon), "", aId, aLink, aPage, aParentId);
        mItemList.append (item);

        QTreeWidgetItem *twitem = new QTreeWidgetItem (mTwSelector, QStringList() << QString ("")
                                                                                  << idToString (aId)
                                                                                  << aLink);
        twitem->setIcon (treeWidget_Category, item->icon());
        aPage->setContentsMargins (0, 0, 0, 0);
        VBoxGlobal::setLayoutMargin (aPage->layout(), 0);
        result = aPage;
    }
    return result;
}

void VBoxSettingsTreeViewSelector::setItemText (int aId, const QString &aText)
{
    VBoxSettingsSelector::setItemText (aId, aText);
    QTreeWidgetItem *item = findItem (mTwSelector, idToString (aId), treeWidget_Id);
    if (item)
        item->setText (treeWidget_Category, QString (" %1 ").arg (aText));
}

QString VBoxSettingsTreeViewSelector::itemText (int aId) const
{
    return pagePath (idToString (aId));
}

int VBoxSettingsTreeViewSelector::currentId () const
{
    int id = -1;
    QTreeWidgetItem *item = mTwSelector->currentItem();
    if (item)
        id = item->text (treeWidget_Id).toInt();
    return id;
}

int VBoxSettingsTreeViewSelector::linkToId (const QString &aLink) const
{
    int id = -1;
    QTreeWidgetItem *item = findItem (mTwSelector, aLink, treeWidget_Link);
    if (item)
        id = item->text (treeWidget_Id).toInt();
    return id;
}


void VBoxSettingsTreeViewSelector::selectById (int aId)
{
    QTreeWidgetItem *item = findItem (mTwSelector, idToString (aId), treeWidget_Id);
    if (item)
        mTwSelector->setCurrentItem (item);
}

void VBoxSettingsTreeViewSelector::setVisibleById (int aId, bool aShow)
{
    QTreeWidgetItem *item = findItem (mTwSelector, idToString (aId), treeWidget_Id);
    if (item)
        item->setHidden (!aShow);
}

void VBoxSettingsTreeViewSelector::polish()
{
    mTwSelector->setFixedWidth (static_cast<QAbstractItemView*> (mTwSelector)
        ->sizeHintForColumn (treeWidget_Category) + 2 * mTwSelector->frameWidth());

    /* Sort selector by the id column */
    mTwSelector->sortItems (treeWidget_Id, Qt::AscendingOrder);
    mTwSelector->resizeColumnToContents (treeWidget_Category);

    /* Add some margin to every item in the tree */
    mTwSelector->addTopBottomMarginToItems (12);
}

void VBoxSettingsTreeViewSelector::settingsGroupChanged (QTreeWidgetItem *aItem,
                                                     QTreeWidgetItem * /* aPrevItem */)
{
    if (aItem)
    {
        int id = aItem->text (treeWidget_Id).toInt();
        Assert (id >= 0);
        emit categoryChanged (id);
    }
}

void VBoxSettingsTreeViewSelector::clear()
{
    mTwSelector->clear();
}

/**
 *  Returns a path to the given page of this settings dialog. See ::path() for
 *  details.
 */
QString VBoxSettingsTreeViewSelector::pagePath (const QString &aMatch) const
{
    QTreeWidgetItem *li =
        findItem (mTwSelector,
                  aMatch,
                  treeWidget_Id);
    return ::path (li);
}

/* Returns first item of 'aView' matching required 'aMatch' value
 * searching the 'aColumn' column. */
QTreeWidgetItem* VBoxSettingsTreeViewSelector::findItem (QTreeWidget *aView,
                                                         const QString &aMatch,
                                                         int aColumn) const
{
    QList<QTreeWidgetItem*> list =
        aView->findItems (aMatch, Qt::MatchExactly, aColumn);

    return list.count() ? list [0] : 0;
}

QString VBoxSettingsTreeViewSelector::idToString (int aId) const
{
    return QString ("%1").arg (aId, 2, 10, QLatin1Char ('0'));
}

/* VBoxSettingsToolBarSelector */


class SelectorActionItem: public SelectorItem
{
public:
    SelectorActionItem (const QIcon &aIcon, const QString &aText, int aId, const QString &aLink, VBoxSettingsPage* aPage, int aParentId, QObject *aParent)
        : SelectorItem (aIcon, aText, aId, aLink, aPage, aParentId)
        , mAction (new QAction (aIcon, aText, aParent))
        , mTabWidget (NULL)
    {
        mAction->setCheckable (true);
    }

    QAction *action() const { return mAction; }

    void setTabWidget (QTabWidget *aTabWidget) { mTabWidget = aTabWidget; }
    QTabWidget *tabWidget() const { return mTabWidget; }

protected:

    QAction *mAction;
    QTabWidget *mTabWidget;
};

VBoxSettingsToolBarSelector::VBoxSettingsToolBarSelector (QWidget *aParent /* = NULL */)
    : VBoxSettingsSelector (aParent)
{
    /* Init the toolbar */
    mTbSelector = new VBoxToolBar (aParent);
    mTbSelector->setUsesTextLabel (true);
    mTbSelector->setIconSize (QSize (32, 32));
    /* Init the action group for house keeping */
    mActionGroup = new QActionGroup (this);
    mActionGroup->setExclusive (true);
    connect (mActionGroup, SIGNAL (triggered (QAction*)),
             this, SLOT (settingsGroupChanged (QAction*)));
}

VBoxSettingsToolBarSelector::~VBoxSettingsToolBarSelector()
{
}

QWidget *VBoxSettingsToolBarSelector::widget() const
{
    return mTbSelector;
}

QWidget *VBoxSettingsToolBarSelector::addItem (const QString &aBigIcon,
                                               const QString &aSmallIcon,
                                               int aId,
                                               const QString &aLink,
                                               VBoxSettingsPage* aPage /* = NULL */,
                                               int aParentId /* = -1 */)
{
    QWidget *result = NULL;
    SelectorActionItem *item = new SelectorActionItem (QIcon (aBigIcon), "", aId, aLink, aPage, aParentId, this);
    mItemList.append (item);

    if (aParentId == -1 &&
        aPage != NULL)
    {
        mActionGroup->addAction (item->action());
        mTbSelector->addAction (item->action());
        aPage->setContentsMargins (0, 0, 0, 0);
        VBoxGlobal::setLayoutMargin (aPage->layout(), 0);
        result = aPage;
    }
    else if (aParentId == -1 &&
             aPage == NULL)
    {
        mActionGroup->addAction (item->action());
        mTbSelector->addAction (item->action());
        QTabWidget *tabWidget= new QTabWidget ();
        tabWidget->setContentsMargins (0, 0, 0, 0);
//        connect (tabWidget, SIGNAL (currentChanged (int)),
//                 this, SLOT (settingsGroupChanged (int)));
        item->setTabWidget (tabWidget);
        result = tabWidget;
    }else
    {
        SelectorActionItem *parent = findActionItem (aParentId);
        if (parent)
        {
            QTabWidget *tabWidget = parent->tabWidget();
            aPage->setContentsMargins (9, 5, 9, 9);
            VBoxGlobal::setLayoutMargin (aPage->layout(), 0);
            if (tabWidget)
                tabWidget->addTab (aPage, QIcon (aSmallIcon), "");
        }
    }
    return result;
}

void VBoxSettingsToolBarSelector::setItemText (int aId, const QString &aText)
{
    if (SelectorActionItem *item = findActionItem (aId))
    {
        item->setText (aText);
        if (item->action())
            item->action()->setText (aText);
        if (item->parentId() &&
            item->page())
        {
            SelectorActionItem *parent = findActionItem (item->parentId());
            if (parent &&
                parent->tabWidget())
                parent->tabWidget()->setTabText (
                    parent->tabWidget()->indexOf (item->page()), aText);
        }
    }
}

QString VBoxSettingsToolBarSelector::itemText (int aId) const
{
    QString result;
    if (SelectorItem *item = findItem (aId))
        result = item->text();
    return result;
}

int VBoxSettingsToolBarSelector::currentId () const
{
    SelectorActionItem *action = findActionItemByAction (mActionGroup->checkedAction());
    int id = -1;
    if (action)
        id = action->id();
    return id;
}

int VBoxSettingsToolBarSelector::linkToId (const QString &aLink) const
{
    int id = -1;
    SelectorItem *item = VBoxSettingsSelector::findItemByLink (aLink);
    if (item)
        id = item->id();
    return id;
}

QWidget *VBoxSettingsToolBarSelector::idToPage (int aId) const
{
    QWidget *page = NULL;
    if (SelectorActionItem *item = findActionItem (aId))
    {
        page = item->page();
        if (!page)
            page = item->tabWidget();
    }
    return page;
}

QWidget *VBoxSettingsToolBarSelector::rootPage (int aId) const
{
    QWidget *page = NULL;
    if (SelectorActionItem *item = findActionItem (aId))
    {
        if (item->parentId() > -1)
            page = rootPage (item->parentId());
        else if (item->page())
            page = item->page();
        else
            page = item->tabWidget();
    }
    return page;
}

void VBoxSettingsToolBarSelector::selectById (int aId)
{
    if (SelectorActionItem *item = findActionItem (aId))
    {
        if (item->parentId() != -1)
        {
            SelectorActionItem *parent = findActionItem (item->parentId());
            if (parent &&
                parent->tabWidget())
            {
                parent->action()->trigger();
                parent->tabWidget()->setCurrentIndex (
                    parent->tabWidget()->indexOf (item->page()));
            }
        }
        else
            item->action()->trigger();
    }
}


void VBoxSettingsToolBarSelector::setVisibleById (int aId, bool aShow)
{
    SelectorActionItem *item = findActionItem (aId);

    if (item)
    {
        item->action()->setVisible (aShow);
        if (item->parentId() > -1 &&
            item->page())
        {
            SelectorActionItem *parent = findActionItem (item->parentId());
            if (parent &&
                parent->tabWidget())
            {
                if (aShow &&
                    parent->tabWidget()->indexOf (item->page()) == -1)
                    parent->tabWidget()->addTab (item->page(), item->text());
                else if (!aShow &&
                         parent->tabWidget()->indexOf (item->page()) > -1)
                    parent->tabWidget()->removeTab (
                        parent->tabWidget()->indexOf (item->page()));
            }
        }
    }

}

void VBoxSettingsToolBarSelector::clear()
{
    QList<QAction*> list = mActionGroup->actions();
    foreach (QAction *action, list)
       delete action;
}

int VBoxSettingsToolBarSelector::minWidth() const
{
    return mTbSelector->sizeHint().width() + 2 * 10;
}

void VBoxSettingsToolBarSelector::settingsGroupChanged (QAction *aAction)
{
    SelectorActionItem *item = findActionItemByAction (aAction);
    if (item)
    {
        emit categoryChanged (item->id());
//        if (item->page() &&
//            !item->tabWidget())
//            emit categoryChanged (item->id());
//        else
//        {
//
//            item->tabWidget()->blockSignals (true);
//            item->tabWidget()->setCurrentIndex (0);
//            item->tabWidget()->blockSignals (false);
//            printf ("%s\n", qPrintable(item->text()));
//            SelectorActionItem *child = static_cast<SelectorActionItem*> (
//                findItemByPage (static_cast<VBoxSettingsPage*> (item->tabWidget()->currentWidget())));
//            if (child)
//                emit categoryChanged (child->id());
//        }
    }
}

void VBoxSettingsToolBarSelector::settingsGroupChanged (int aIndex)
{
    SelectorActionItem *item = findActionItemByTabWidget (qobject_cast<QTabWidget*> (sender()), aIndex);
    if (item)
    {
        if (item->page() &&
            !item->tabWidget())
            emit categoryChanged (item->id());
        else
        {
            SelectorActionItem *child = static_cast<SelectorActionItem*> (
                findItemByPage (static_cast<VBoxSettingsPage*> (item->tabWidget()->currentWidget())));
            if (child)
                emit categoryChanged (child->id());
        }
    }
}

SelectorActionItem* VBoxSettingsToolBarSelector::findActionItem (int aId) const
{
    return static_cast<SelectorActionItem*> (VBoxSettingsSelector::findItem (aId));
}

SelectorActionItem *VBoxSettingsToolBarSelector::findActionItemByTabWidget (QTabWidget* aTabWidget, int aIndex) const
{
    SelectorActionItem *result = NULL;
    foreach (SelectorItem *item, mItemList)
        if (static_cast<SelectorActionItem*> (item)->tabWidget() == aTabWidget)
        {
            QTabWidget *tw = static_cast<SelectorActionItem*> (item)->tabWidget();
            result = static_cast<SelectorActionItem*> (
                findItemByPage (static_cast<VBoxSettingsPage*> (tw->widget (aIndex))));
            break;
        }

    return result;

}

QList<QWidget*> VBoxSettingsToolBarSelector::rootPages() const
{
    QList<QWidget*> list;
    foreach (SelectorItem *item, mItemList)
    {
        SelectorActionItem *ai = static_cast<SelectorActionItem*> (item);
        if (ai->parentId() == -1 &&
            ai->page())
            list << ai->page();
        else if (ai->tabWidget())
            list << ai->tabWidget();
    }
    return list;
}

SelectorActionItem *VBoxSettingsToolBarSelector::findActionItemByAction (QAction *aAction) const
{
    SelectorActionItem *result = NULL;
    foreach (SelectorItem *item, mItemList)
        if (static_cast<SelectorActionItem*> (item)->action() == aAction)
        {
            result = static_cast<SelectorActionItem*> (item);
            break;
        }

    return result;
}

