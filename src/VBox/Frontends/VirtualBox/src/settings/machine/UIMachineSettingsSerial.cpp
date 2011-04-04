/* $Id$ */
/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * UIMachineSettingsSerial class implementation
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include "UIMachineSettingsSerial.h"
#include "QIWidgetValidator.h"
#include "VBoxGlobal.h"
#include "QITabWidget.h"

#include <QDir>

/* UIMachineSettingsSerial stuff */
UIMachineSettingsSerial::UIMachineSettingsSerial(UIMachineSettingsSerialPage *pParent)
    : QIWithRetranslateUI<QWidget> (0)
    , m_pParent(pParent)
    , mValidator(0)
    , m_iSlot(-1)
{
    /* Apply UI decorations */
    Ui::UIMachineSettingsSerial::setupUi (this);

    /* Setup validation */
    mLeIRQ->setValidator (new QIULongValidator (0, 255, this));
    mLeIOPort->setValidator (new QIULongValidator (0, 0xFFFF, this));
    mLePath->setValidator (new QRegExpValidator (QRegExp (".+"), this));

    /* Setup constraints */
    mLeIRQ->setFixedWidth (mLeIRQ->fontMetrics().width ("8888"));
    mLeIOPort->setFixedWidth (mLeIOPort->fontMetrics().width ("8888888"));

    /* Set initial values */
    /* Note: If you change one of the following don't forget retranslateUi. */
    mCbNumber->insertItem (0, vboxGlobal().toCOMPortName (0, 0));
    mCbNumber->insertItems (0, vboxGlobal().COMPortNames());

    mCbMode->addItem (""); /* KPortMode_Disconnected */
    mCbMode->addItem (""); /* KPortMode_HostPipe */
    mCbMode->addItem (""); /* KPortMode_HostDevice */
    mCbMode->addItem (""); /* KPortMode_RawFile */

    /* Setup connections */
    connect (mGbSerial, SIGNAL (toggled (bool)),
             this, SLOT (mGbSerialToggled (bool)));
    connect (mCbNumber, SIGNAL (activated (const QString &)),
             this, SLOT (mCbNumberActivated (const QString &)));
    connect (mCbMode, SIGNAL (activated (const QString &)),
             this, SLOT (mCbModeActivated (const QString &)));

    /* Applying language settings */
    retranslateUi();
}

void UIMachineSettingsSerial::polishTab()
{
    mGbSerial->setEnabled(m_pParent->isMachineOffline());
    mLbNumber->setEnabled(m_pParent->isMachineOffline());
    mCbNumber->setEnabled(m_pParent->isMachineOffline());
    mLbIRQ->setEnabled(m_pParent->isMachineOffline());
    mLeIRQ->setEnabled(m_pParent->isMachineOffline());
    mLbIOPort->setEnabled(m_pParent->isMachineOffline());
    mLeIOPort->setEnabled(m_pParent->isMachineOffline());
    mLbMode->setEnabled(m_pParent->isMachineOffline());
    mCbMode->setEnabled(m_pParent->isMachineOffline());
    mCbPipe->setEnabled(m_pParent->isMachineOffline());
    mLbPath->setEnabled(m_pParent->isMachineOffline());
    mLePath->setEnabled(m_pParent->isMachineOffline());
}

void UIMachineSettingsSerial::fetchPortData(const UISerialPortData &data)
{
    /* Load port slot number: */
    m_iSlot = data.m_iSlot;

    /* Fetch port data: */
    mGbSerial->setChecked(data.m_fPortEnabled);
    mCbNumber->setCurrentIndex(mCbNumber->findText(vboxGlobal().toCOMPortName(data.m_uIRQ, data.m_uIOBase)));
    mLeIRQ->setText(QString::number(data.m_uIRQ));
    mLeIOPort->setText("0x" + QString::number(data.m_uIOBase, 16).toUpper());
    mCbMode->setCurrentIndex(mCbMode->findText(vboxGlobal().toString(data.m_hostMode)));
    mCbPipe->setChecked(data.m_fServer);
    mLePath->setText(data.m_strPath);

    /* Ensure everything is up-to-date */
    mGbSerialToggled(mGbSerial->isChecked());
}

void UIMachineSettingsSerial::uploadPortData(UISerialPortData &data)
{
    /* Upload port data: */
    data.m_fPortEnabled = mGbSerial->isChecked();
    data.m_uIRQ = mLeIRQ->text().toULong(NULL, 0);
    data.m_uIOBase = mLeIOPort->text().toULong (NULL, 0);
    data.m_fServer = mCbPipe->isChecked();
    data.m_hostMode = vboxGlobal().toPortMode(mCbMode->currentText());
    data.m_strPath = QDir::toNativeSeparators(mLePath->text());
}

void UIMachineSettingsSerial::setValidator (QIWidgetValidator *aVal)
{
    Assert (aVal);
    mValidator = aVal;
    connect (mLeIRQ, SIGNAL (textChanged (const QString &)),
             mValidator, SLOT (revalidate()));
    connect (mLeIOPort, SIGNAL (textChanged (const QString &)),
             mValidator, SLOT (revalidate()));
    connect (mLePath, SIGNAL (textChanged (const QString &)),
             mValidator, SLOT (revalidate()));
    mValidator->revalidate();
}

QWidget* UIMachineSettingsSerial::setOrderAfter (QWidget *aAfter)
{
    setTabOrder (aAfter, mGbSerial);
    setTabOrder (mGbSerial, mCbNumber);
    setTabOrder (mCbNumber, mLeIRQ);
    setTabOrder (mLeIRQ, mLeIOPort);
    setTabOrder (mLeIOPort, mCbMode);
    setTabOrder (mCbMode, mCbPipe);
    setTabOrder (mCbPipe, mLePath);
    return mLePath;
}

QString UIMachineSettingsSerial::pageTitle() const
{
    return QString(tr("Port %1", "serial ports")).arg(QString("&%1").arg(m_iSlot + 1));
}

bool UIMachineSettingsSerial::isUserDefined()
{
    ulong a, b;
    return !vboxGlobal().toCOMPortNumbers (mCbNumber->currentText(), a, b);
}

void UIMachineSettingsSerial::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::UIMachineSettingsSerial::retranslateUi (this);

    mCbNumber->setItemText (mCbNumber->count() - 1, vboxGlobal().toCOMPortName (0, 0));

    mCbMode->setItemText (3, vboxGlobal().toString (KPortMode_RawFile));
    mCbMode->setItemText (2, vboxGlobal().toString (KPortMode_HostDevice));
    mCbMode->setItemText (1, vboxGlobal().toString (KPortMode_HostPipe));
    mCbMode->setItemText (0, vboxGlobal().toString (KPortMode_Disconnected));
}

void UIMachineSettingsSerial::mGbSerialToggled (bool aOn)
{
    if (aOn)
    {
        mCbNumberActivated (mCbNumber->currentText());
        mCbModeActivated (mCbMode->currentText());
    }
    if (mValidator)
        mValidator->revalidate();
}

void UIMachineSettingsSerial::mCbNumberActivated (const QString &aText)
{
    ulong IRQ, IOBase;
    bool std = vboxGlobal().toCOMPortNumbers (aText, IRQ, IOBase);

    mLeIRQ->setEnabled (!std);
    mLeIOPort->setEnabled (!std);
    if (std)
    {
        mLeIRQ->setText (QString::number (IRQ));
        mLeIOPort->setText ("0x" + QString::number (IOBase, 16).toUpper());
    }
}

void UIMachineSettingsSerial::mCbModeActivated (const QString &aText)
{
    KPortMode mode = vboxGlobal().toPortMode (aText);
    mCbPipe->setEnabled (mode == KPortMode_HostPipe);
    mLePath->setEnabled (mode != KPortMode_Disconnected);
    if (mValidator)
        mValidator->revalidate();
}


/* UIMachineSettingsSerialPage stuff */
UIMachineSettingsSerialPage::UIMachineSettingsSerialPage()
    : mValidator(0)
    , mTabWidget(0)
{
    /* TabWidget creation */
    mTabWidget = new QITabWidget (this);
    QVBoxLayout *layout = new QVBoxLayout (this);
    layout->setContentsMargins (0, 5, 0, 5);
    layout->addWidget (mTabWidget);

    /* Load port data: */
    ulong uCount = vboxGlobal().virtualBox().GetSystemProperties().GetSerialPortCount();
    /* Apply internal variables data to QWidget(s): */
    for (ulong iSlot = 0; iSlot < uCount; ++iSlot)
    {
        /* Creating port's page: */
        UIMachineSettingsSerial *pPage = new UIMachineSettingsSerial(this);

        /* Attach port's page to Tab Widget: */
        mTabWidget->addTab(pPage, pPage->pageTitle());

    }

    /* Applying language settings */
    retranslateUi();
}

/* Load data to cashe from corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void UIMachineSettingsSerialPage::loadToCacheFrom(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Load port data: */
    ulong uCount = vboxGlobal().virtualBox().GetSystemProperties().GetSerialPortCount();
    for (ulong uSlot = 0; uSlot < uCount; ++uSlot)
    {
        /* Get port: */
        const CSerialPort &port = m_machine.GetSerialPort(uSlot);

        /* Prepare port's data container: */
        UISerialPortData data;

        /* Load options: */
        data.m_iSlot = uSlot;
        data.m_fPortEnabled = port.GetEnabled();
        data.m_uIRQ = port.GetIRQ();
        data.m_uIOBase = port.GetIOBase();
        data.m_hostMode = port.GetHostMode();
        data.m_fServer = port.GetServer();
        data.m_strPath = port.GetPath();

        /* Append adapter's data container: */
        m_cache.m_items << data;
    }

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

/* Load data to corresponding widgets from cache,
 * this task SHOULD be performed in GUI thread only: */
void UIMachineSettingsSerialPage::getFromCache()
{
    Assert(firstWidget());
    setTabOrder(firstWidget(), mTabWidget->focusProxy());
    QWidget *pLastFocusWidget = mTabWidget->focusProxy();

    ulong uCount = qMin(mTabWidget->count(), m_cache.m_items.size());
    /* Apply internal variables data to QWidget(s): */
    for (ulong iSlot = 0; iSlot < uCount; ++iSlot)
    {
        /* Getting adapter's page: */
        UIMachineSettingsSerial *pPage = qobject_cast<UIMachineSettingsSerial*>(mTabWidget->widget(iSlot));

        /* Loading port's data into page: */
        pPage->fetchPortData(m_cache.m_items[iSlot]);

        /* Setup page validation: */
        pPage->setValidator(mValidator);

        /* Setup tab order: */
        pLastFocusWidget = pPage->setOrderAfter(pLastFocusWidget);
    }

    /* Applying language settings: */
    retranslateUi();

    /* Revalidate if possible: */
    if (mValidator) mValidator->revalidate();
}

/* Save data from corresponding widgets to cache,
 * this task SHOULD be performed in GUI thread only: */
void UIMachineSettingsSerialPage::putToCache()
{
    /* Gather internal variables data from QWidget(s): */
    for (int iSlot = 0; iSlot < m_cache.m_items.size(); ++iSlot)
    {
        /* Getting adapter's page: */
        UIMachineSettingsSerial *pPage = qobject_cast<UIMachineSettingsSerial*>(mTabWidget->widget(iSlot));

        /* Loading Adapter's data from page: */
        pPage->uploadPortData(m_cache.m_items[iSlot]);
    }
}

/* Save data from cache to corresponding external object(s),
 * this task COULD be performed in other than GUI thread: */
void UIMachineSettingsSerialPage::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    if (isMachineOffline())
    {
        /* Gather corresponding values from internal variables: */
        for (int iSlot = 0; iSlot < m_cache.m_items.size(); ++iSlot)
        {
            /* Get adapter: */
            CSerialPort port = m_machine.GetSerialPort(iSlot);

            /* Get cached data for this slot: */
            const UISerialPortData &data = m_cache.m_items[iSlot];

            /* Save options: */
            port.SetEnabled(data.m_fPortEnabled);
            port.SetIRQ(data.m_uIRQ);
            port.SetIOBase(data.m_uIOBase);
            port.SetServer(data.m_fServer);
            port.SetPath(data.m_strPath);
            /* This *must* be last. The host mode will be changed to disconnected if
             * some of the necessary settings above will not meet the requirements for
             * the selected mode. */
            port.SetHostMode(data.m_hostMode);
        }
    }

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsSerialPage::setValidator (QIWidgetValidator * aVal)
{
    mValidator = aVal;
}

bool UIMachineSettingsSerialPage::revalidate (QString &aWarning, QString &aTitle)
{
    bool valid = true;
    QStringList ports;
    QStringList paths;

    int index = 0;
    for (; index < mTabWidget->count(); ++ index)
    {
        QWidget *tab = mTabWidget->widget (index);
        UIMachineSettingsSerial *page =
            static_cast<UIMachineSettingsSerial*> (tab);

        if (!page->mGbSerial->isChecked())
            continue;

        /* Check the predefined port number unicity */
        if (!page->isUserDefined())
        {
            QString port = page->mCbNumber->currentText();
            valid = !ports.contains (port);
            if (!valid)
            {
                aWarning = tr ("Duplicate port number selected ");
                aTitle += ": " +
                    vboxGlobal().removeAccelMark (mTabWidget->tabText (mTabWidget->indexOf (tab)));
                break;
            }
            ports << port;
        }

        /* Check the port path emptiness & unicity */
        KPortMode mode =
            vboxGlobal().toPortMode (page->mCbMode->currentText());
        if (mode != KPortMode_Disconnected)
        {
            QString path = page->mLePath->text();
            valid = !path.isEmpty() && !paths.contains (path);
            if (!valid)
            {
                if (!page->mGbSerial->isChecked())
                    page->mCbMode->setCurrentIndex (KPortMode_Disconnected);
                else
                {
                    aWarning = path.isEmpty() ?
                        tr ("Port path not specified ") :
                        tr ("Duplicate port path entered ");
                    aTitle += ": " +
                        vboxGlobal().removeAccelMark (mTabWidget->tabText (mTabWidget->indexOf (tab)));
                    break;
                }
            }
            paths << path;
        }
    }

    return valid;
}

void UIMachineSettingsSerialPage::retranslateUi()
{
    for (int i = 0; i < mTabWidget->count(); ++ i)
    {
        UIMachineSettingsSerial *page =
            static_cast<UIMachineSettingsSerial*> (mTabWidget->widget (i));
        mTabWidget->setTabText (i, page->pageTitle());
    }
}

void UIMachineSettingsSerialPage::polishPage()
{
    /* Get the count of serial port tabs: */
    for (int iTabIndex = 0; iTabIndex < mTabWidget->count(); ++iTabIndex)
    {
        mTabWidget->setTabEnabled(iTabIndex,
                                  isMachineOffline() ||
                                  (isMachineInValidMode() && m_cache.m_items[iTabIndex].m_fPortEnabled));
        UIMachineSettingsSerial *pTab = qobject_cast<UIMachineSettingsSerial*>(mTabWidget->widget(iTabIndex));
        Assert(pTab);
        pTab->polishTab();
    }
}

