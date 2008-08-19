/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VirtualBox Qt extensions: VBoxFilePathSelectorWidget class declaration
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

#ifndef __VBoxFilePathSelectorWidget_h__
#define __VBoxFilePathSelectorWidget_h__

#include "QIWithRetranslateUI.h"

/* Qt includes */
#include <QComboBox>

class QFileIconProvider;
class QAction;

class VBoxFilePathSelectorWidget: public QIWithRetranslateUI<QComboBox>
{
    Q_OBJECT;

public:

    enum Mode
    {
        Mode_Folder = 0,
        Mode_File
    };

    VBoxFilePathSelectorWidget (QWidget *aParent);
   ~VBoxFilePathSelectorWidget();

    void setMode (Mode aMode);
    Mode mode() const;

    void setResetEnabled (bool aEnabled);
    bool isResetEnabled () const;

    bool isModified() const;
    bool isPathLineChosen() const;

    QString path() const;

public slots:

    void setPath (const QString &aPath);
    void setHomeDir (const QString &aHomeDir);

protected:

    void resizeEvent (QResizeEvent *aEvent);
    void focusInEvent (QFocusEvent *aEvent);
    void focusOutEvent (QFocusEvent *aEvent);
    void retranslateUi();

private slots:

    void onActivated (int aIndex);
    void copyToClipboard();

private:

    void selectPath();
    QIcon defaultIcon() const;
    QString fullPath (bool aAbsolute = true) const;
    QString shrinkText (int aWidth) const;
    void refreshText();

    /* Private member vars */
    QFileIconProvider *mIconProvider;
    QAction *mCopyAction;
    Mode mMode;
    QString mPath;
    QString mHomeDir;
    QString mNoneStr;
    QString mNoneTip;
    bool mIsEditableMode;
};

#endif /* __VBoxFilePathSelectorWidget_h__ */

