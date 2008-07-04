/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxSettingsDialog class declaration
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

#ifndef __VBoxSettingsDialog_h__
#define __VBoxSettingsDialog_h__

#include "VBoxSettingsDialog.gen.h"
#include "QIMainDialog.h"
#include "QIWithRetranslateUI.h"

class VBoxWarnIconLabel;
class QIWidgetValidator;
class QTimer;
class QStackedWidget;

/*
 * Base dialog class for both Global & VM settings which
 * encapsulates most of their similar functionalities.
 */
class VBoxSettingsDialog : public QIWithRetranslateUI<QIMainDialog>,
                           public Ui::VBoxSettingsDialog
{
    Q_OBJECT;

public:

    VBoxSettingsDialog (QWidget *aParent);

    virtual void getFrom() = 0;
    virtual void putBackTo() = 0;

protected slots:

    virtual void revalidate (QIWidgetValidator * /* aWval */) {}
    void settingsGroupChanged (QTreeWidgetItem *aItem,
                               QTreeWidgetItem *aPrev = 0);

protected:

    static QTreeWidgetItem* findItem (QTreeWidget *aView,
                                      const QString &aMatch,
                                      int aColumn);

    virtual void retranslateUi();

    QString pagePath (QWidget *aPage);
    void setWarning (const QString &aWarning);

    QStackedWidget *mStack;

private slots:

    void enableOk (const QIWidgetValidator *aWval);
    void updateWhatsThis (bool aGotFocus = false);
    void whatsThisCandidateDestroyed (QObject *aObj = 0);

private:

    bool eventFilter (QObject *aObject, QEvent *aEvent);
    void showEvent (QShowEvent *aEvent);

    /* Flags */
    bool mPolished;
    bool mValid;

    /* Warning Stuff */
    VBoxWarnIconLabel *mWarnIconLabel;
    QString mWarnString;

    /* WhatsThis Stuff */
    QTimer *mWhatsThisTimer;
    QWidget *mWhatsThisCandidate;
};

#endif // __VBoxSettingsDialog_h__

