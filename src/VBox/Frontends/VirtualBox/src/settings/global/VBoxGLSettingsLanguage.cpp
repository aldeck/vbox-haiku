/* $Id$ */
/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxGLSettingsLanguage class implementation
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "VBoxGLSettingsLanguage.h"
#include "VBoxGlobalSettings.h"
#include "VBoxGlobal.h"

#include <QHeaderView>
#include <QTranslator>
#include <QDir>
#include <QPainter>

#include <iprt/err.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <VBox/version.h>

extern const char *gVBoxLangSubDir;
extern const char *gVBoxLangFileBase;
extern const char *gVBoxLangFileExt;
extern const char *gVBoxLangIDRegExp;
extern const char *gVBoxBuiltInLangName;

class LanguageItem : public QTreeWidgetItem
{
public:

    enum { LanguageItemType = QTreeWidgetItem::UserType + 1 };

    LanguageItem (QTreeWidget *aParent, const QTranslator &aTranslator,
                  const QString &aId, bool aBuiltIn = false)
        : QTreeWidgetItem (aParent, LanguageItemType), mBuiltIn (aBuiltIn)
    {
        Assert (!aId.isEmpty());

        /* Note: context/source/comment arguments below must match strings
         * used in VBoxGlobal::languageName() and friends (the latter are the
         * source of information for the lupdate tool that generates
         * translation files) */

        QString nativeLanguage = tratra (aTranslator,
            "@@@", "English", "Native language name");
        QString nativeCountry = tratra (aTranslator,
            "@@@", "--", "Native language country name "
            "(empty if this language is for all countries)");

        QString englishLanguage = tratra (aTranslator,
            "@@@", "English", "Language name, in English");
        QString englishCountry = tratra (aTranslator,
            "@@@", "--", "Language country name, in English "
            "(empty if native country name is empty)");

        QString translatorsName = tratra (aTranslator,
            "@@@", "Oracle Corporation", "Comma-separated list of translators");

        QString itemName = nativeLanguage;
        QString langName = englishLanguage;

        if (!aBuiltIn)
        {
            if (nativeCountry != "--")
                itemName += " (" + nativeCountry + ")";

            if (englishCountry != "--")
                langName += " (" + englishCountry + ")";

            if (itemName != langName)
                langName = itemName + " / " + langName;
        }
        else
        {
            itemName += VBoxGLSettingsLanguage::tr (" (built-in)", "Language");
            langName += VBoxGLSettingsLanguage::tr (" (built-in)", "Language");
        }

        setText (0, itemName);
        setText (1, aId);
        setText (2, langName);
        setText (3, translatorsName);

        /* Current language appears in bold */
        if (text (1) == VBoxGlobal::languageId())
        {
            QFont fnt = font (0);
            fnt.setBold (true);
            setFont (0, fnt);
        }
    }

    /* Constructs an item for an invalid language ID (i.e. when a language
     * file is missing or corrupt). */
    LanguageItem (QTreeWidget *aParent, const QString &aId)
        : QTreeWidgetItem (aParent, LanguageItemType), mBuiltIn (false)
    {
        Assert (!aId.isEmpty());

        setText (0, QString ("<%1>").arg (aId));
        setText (1, aId);
        setText (2, VBoxGLSettingsLanguage::tr ("<unavailable>", "Language"));
        setText (3, VBoxGLSettingsLanguage::tr ("<unknown>", "Author(s)"));

        /* Invalid language appears in italic */
        QFont fnt = font (0);
        fnt.setItalic (true);
        setFont (0, fnt);
    }

    /* Constructs an item for the default language ID (column 1 will be set
     * to QString::null) */
    LanguageItem (QTreeWidget *aParent)
        : QTreeWidgetItem (aParent, LanguageItemType), mBuiltIn (false)
    {
        setText (0, VBoxGLSettingsLanguage::tr ("Default", "Language"));
        setText (1, QString::null);
        /* Empty strings of some reasonable length to prevent the info part
         * from being shrinked too much when the list wants to be wider */
        setText (2, "                ");
        setText (3, "                ");
    }

    bool isBuiltIn() const { return mBuiltIn; }

    bool operator< (const QTreeWidgetItem &aOther) const
    {
        QString thisId = text (1);
        QString thatId = aOther.text (1);
        if (thisId.isNull())
            return true;
        if (thatId.isNull())
            return false;
        if (mBuiltIn)
            return true;
        if (aOther.type() == LanguageItemType && ((LanguageItem*) &aOther)->mBuiltIn)
            return false;
        return QTreeWidgetItem::operator< (aOther);
    }

private:

    QString tratra (const QTranslator &aTranslator, const char *aCtxt,
                    const char *aSrc, const char *aCmnt)
    {
        QString msg = aTranslator.translate (aCtxt, aSrc, aCmnt);
        /* return the source text if no translation is found */
        if (msg.isEmpty())
            msg = QString (aSrc);
        return msg;
    }

    bool mBuiltIn : 1;
};


VBoxGLSettingsLanguage::VBoxGLSettingsLanguage()
    : mLanguageChanged(false)
{
    /* Apply UI decorations */
    Ui::VBoxGLSettingsLanguage::setupUi (this);

    /* Setup dialog */
    mTwLanguage->header()->hide();
    mTwLanguage->hideColumn (1);
    mTwLanguage->hideColumn (2);
    mTwLanguage->hideColumn (3);

    /* Setup Connections */
    connect (mTwLanguage, SIGNAL (painted (QTreeWidgetItem *, QPainter *)),
             this, SLOT (mTwItemPainted (QTreeWidgetItem *, QPainter *)));
    connect (mTwLanguage, SIGNAL (currentItemChanged (QTreeWidgetItem *, QTreeWidgetItem *)),
             this, SLOT (mTwLanguageChanged (QTreeWidgetItem *)));

    /* Applying language settings */
    retranslateUi();
}

/* Load data to cashe from corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void VBoxGLSettingsLanguage::loadToCacheFrom(QVariant &data)
{
    /* Fetch data to properties & settings: */
    UISettingsPageGlobal::fetchData(data);

    /* Load to cache: */
    m_cache.m_strLanguageId = m_settings.languageId();

    /* Upload properties & settings to data: */
    UISettingsPageGlobal::uploadData(data);
}

/* Load data to corresponding widgets from cache,
 * this task SHOULD be performed in GUI thread only: */
void VBoxGLSettingsLanguage::getFromCache()
{
    /* Fetch from cache: */
    reload(m_cache.m_strLanguageId);
    mTxName->setFixedHeight(fontMetrics().height() * 4);
}

/* Save data from corresponding widgets to cache,
 * this task SHOULD be performed in GUI thread only: */
void VBoxGLSettingsLanguage::putToCache()
{
    /* Upload to cache: */
    QTreeWidgetItem *pCurrentItem = mTwLanguage->currentItem();
    Assert(pCurrentItem);
    if (pCurrentItem)
        m_cache.m_strLanguageId = pCurrentItem->text(1);
}

/* Save data from cache to corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void VBoxGLSettingsLanguage::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties & settings: */
    UISettingsPageGlobal::fetchData(data);

    /* Save from cache: */
    if (mLanguageChanged)
    {
        m_settings.setLanguageId(m_cache.m_strLanguageId);
        VBoxGlobal::loadLanguage(m_cache.m_strLanguageId);
    }

    /* Upload properties & settings to data: */
    UISettingsPageGlobal::uploadData(data);
}

void VBoxGLSettingsLanguage::setOrderAfter (QWidget *aWidget)
{
    setTabOrder (aWidget, mTwLanguage);
}

void VBoxGLSettingsLanguage::reload (const QString &aLangId)
{
    /* Clear languages list */
    mTwLanguage->clear();

    /* Load languages list */
    char szNlsPath [RTPATH_MAX];
    int rc = RTPathAppPrivateNoArch (szNlsPath, sizeof(szNlsPath));
    AssertRC (rc);
    QString nlsPath = QString (szNlsPath) + gVBoxLangSubDir;
    QDir nlsDir (nlsPath);
    QStringList files = nlsDir.entryList (QStringList (QString ("%1*%2")
        .arg (gVBoxLangFileBase, gVBoxLangFileExt)), QDir::Files);

    QTranslator translator;
    /* Add the default language */
    new LanguageItem (mTwLanguage);
    /* Add the built-in language */
    new LanguageItem (mTwLanguage, translator, gVBoxBuiltInLangName, true /* built-in */);
    /* Add all existing languages */
    for (QStringList::Iterator it = files.begin(); it != files.end(); ++ it)
    {
        QString fileName = *it;
        QRegExp regExp (QString (gVBoxLangFileBase) + gVBoxLangIDRegExp);
        int pos = regExp.indexIn (fileName);
        if (pos == -1)
            continue;

        /* Skip any English version, cause this is extra handled. */
        QString lang = regExp.cap (2);
        if (lang.toLower() == "en")
            continue;

        bool loadOk = translator.load (fileName, nlsPath);
        if (!loadOk)
            continue;

        new LanguageItem (mTwLanguage, translator, regExp.cap (1));
    }

    /* Adjust selector list */
#ifdef Q_WS_MAC
    int width = qMax (static_cast<QAbstractItemView*> (mTwLanguage)
        ->sizeHintForColumn (0) + 2 * mTwLanguage->frameWidth() +
        QApplication::style()->pixelMetric (QStyle::PM_ScrollBarExtent),
        220);
    mTwLanguage->setFixedWidth (width);
#else /* Q_WS_MAC */
    mTwLanguage->setMinimumWidth (static_cast<QAbstractItemView*> (mTwLanguage)
        ->sizeHintForColumn (0) + 2 * mTwLanguage->frameWidth() +
        QApplication::style()->pixelMetric (QStyle::PM_ScrollBarExtent));
#endif /* Q_WS_MAC */
    mTwLanguage->resizeColumnToContents (0);

    /* Search for necessary language */
    QList<QTreeWidgetItem*> itemsList =
        mTwLanguage->findItems (aLangId, Qt::MatchExactly, 1);
    QTreeWidgetItem *item = itemsList.isEmpty() ? 0 : itemsList [0];
    if (!item)
    {
        /* Add an item for an invalid language to represent it in the list */
        item = new LanguageItem (mTwLanguage, aLangId);
        mTwLanguage->resizeColumnToContents (0);
    }
    Assert (item);
    if (item)
        mTwLanguage->setCurrentItem (item);

    mTwLanguage->sortItems (0, Qt::AscendingOrder);
    mTwLanguage->scrollToItem (item);
    mLanguageChanged = false;
}

void VBoxGLSettingsLanguage::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxGLSettingsLanguage::retranslateUi (this);
    reload (VBoxGlobal::languageId());
}

void VBoxGLSettingsLanguage::mTwItemPainted (QTreeWidgetItem *aItem, QPainter *aPainter)
{
    if (aItem && aItem->type() == LanguageItem::LanguageItemType)
    {
        LanguageItem *item = static_cast <LanguageItem*> (aItem);
        if (item->isBuiltIn())
        {
            QRect rect = mTwLanguage->visualItemRect (item);
            aPainter->setPen (mTwLanguage->palette().color (QPalette::Mid));
            aPainter->drawLine (rect.x(), rect.y() + rect.height() - 1,
                                rect.x() + rect.width(), rect.y() + rect.height() - 1);
        }
    }
}

void VBoxGLSettingsLanguage::mTwLanguageChanged (QTreeWidgetItem *aItem)
{
    if (!aItem) return;

    /* Disable labels for the Default language item */
    bool enabled = !aItem->text (1).isNull();

    mTxName->setEnabled (enabled);
    mTxName->setText (QString ("<table>"
                               "<tr><td>%1&nbsp;</td><td>%2</td></tr>"
                               "<tr><td>%3&nbsp;</td><td>%4</td></tr>"
                               "</table>")
                      .arg (tr ("Language:"))
                      .arg (aItem->text (2))
                      .arg (tr ("Author(s):"))
                      .arg (aItem->text (3)));

    mLanguageChanged = true;
}

