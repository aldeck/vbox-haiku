/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * innotek Qt extensions: QIMessageBox class implementation
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

#include "QIMessageBox.h"
#include "VBoxDefs.h"
#include "QIRichLabel.h"

#include <qpixmap.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qhbox.h>
#include <qlayout.h>

#include <qfontmetrics.h>

/** @class QIMessageBox
 *
 *  The QIMessageBox class is a message box similar to QMessageBox.
 *  It partly implements the QMessageBox interface and adds some enhanced
 *  functionality.
 */

/**
 *  This constructor is equivalent to
 *  QMessageBox::QMessageBox (const QString &, const QString &, Icon,
 *      int, int, int, QWidget *, const char *, bool, WFlags).
 *  See QMessageBox for details.
 */
QIMessageBox::QIMessageBox (const QString &aCaption, const QString &aText,
                            Icon aIcon, int aButton0, int aButton1, int aButton2,
                            QWidget *aParent, const char *aName, bool aModal,
                            WFlags aFlags)
    : QDialog (aParent, aName, aModal,
               aFlags | WStyle_Customize | WStyle_NormalBorder |
                        WStyle_Title | WStyle_SysMenu)
{
    setCaption (aCaption);

    mButton0 = aButton0;
    mButton1 = aButton1;
    mButton2 = aButton2;

    QVBoxLayout *layout = new QVBoxLayout (this);
    /* setAutoAdd() behavior is really poor (it messes up with the order
     * of widgets), never use it: layout->setAutoAdd (true); */
    layout->setMargin (11);
    layout->setSpacing (10);
    layout->setResizeMode (QLayout::Minimum);

    QHBox *main = new QHBox (this);
    main->setMargin (0);
    main->setSpacing (10);
    layout->addWidget (main);

    mIconLabel = new QLabel (main);
    mIconLabel->setPixmap (QMessageBox::standardIcon ((QMessageBox::Icon) aIcon));
    mIconLabel->setSizePolicy (QSizePolicy::Fixed, QSizePolicy::Minimum);
    mIconLabel->setAlignment (AlignHCenter | AlignTop);

    mMessageVBox = new QVBox (main);
    mMessageVBox->setMargin (0);
    mMessageVBox->setSpacing (10);

    mTextLabel = new QIRichLabel (aText, mMessageVBox);
    mTextLabel->setAlignment (AlignAuto | AlignTop | ExpandTabs | WordBreak);
    mTextLabel->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Preferred, true);
    mTextLabel->setMinimumWidth (mTextLabel->sizeHint().width());

    mFlagCB_Main = new QCheckBox (mMessageVBox);
    mFlagCB_Main->hide();

    mDetailsVBox = new QVBox (this);
    mDetailsVBox->setMargin (0);
    mDetailsVBox->setSpacing (10);
    layout->addWidget (mDetailsVBox);

    mDetailsText = new QTextEdit (mDetailsVBox);
    {
        /* calculate the minimum size dynamically, approx. for 40 chars and
         * 6 lines */
        QFontMetrics fm = mDetailsText->fontMetrics();
        mDetailsText->setMinimumSize (40 * fm.width ('m'), fm.lineSpacing() * 6);
    }
    mDetailsText->setReadOnly (true);
    mDetailsText->setWrapPolicy (QTextEdit::AtWordOrDocumentBoundary);
    mDetailsText->setSizePolicy (QSizePolicy::Expanding,
                                 QSizePolicy::MinimumExpanding);

    mFlagCB_Details = new QCheckBox (mDetailsVBox);
    mFlagCB_Details->hide();

    mSpacer = new QSpacerItem (0, 0);
    layout->addItem (mSpacer);

    QHBoxLayout *buttons = new QHBoxLayout (new QWidget (this));
    layout->addWidget (buttons->mainWidget());
    buttons->setAutoAdd (true);
    buttons->setSpacing (5);

    mButtonEsc = 0;

    mButton0PB = createButton (buttons->mainWidget(), aButton0);
    if (mButton0PB)
        connect (mButton0PB, SIGNAL (clicked()), SLOT (done0()));
    mButton1PB = createButton (buttons->mainWidget(), aButton1);
    if (mButton1PB)
        connect (mButton1PB, SIGNAL (clicked()), SLOT (done1()));
    mButton2PB = createButton (buttons->mainWidget(), aButton2);
    if (mButton2PB)
        connect (mButton2PB, SIGNAL (clicked()), SLOT (done2()));

    buttons->setAlignment (AlignHCenter);

    /* this call is a must -- it initializes mFlagCB and mSpacer */
    setDetailsShown (false);
}

/** @fn QIMessageBox::flagText() const
 *
 *  Returns the text of the optional message box flag. If the flag is hidden
 *  (by default) a null string is returned.
 *
 *  @see #setFlagText()
 */

/**
 *  Sets the text for the optional message box flag (check box) that is
 *  displayed under the message text. Passing the null string as the argument
 *  will hide the flag. By default, the flag is hidden.
 */
void QIMessageBox::setFlagText (const QString &aText)
{
    if (aText.isNull())
    {
        mFlagCB->hide();
    }
    else
    {
        mFlagCB->setText (aText);
        mFlagCB->show();
        mFlagCB->setFocus();
    }
}

/** @fn QIMessageBox::isFlagChecked() const
 *
 *  Returns true if the optional message box flag is checked and false
 *  otherwise. By default, the flag is not checked.
 *
 *  @see #setFlagChecked()
 *  @see #setFlagText()
 */

/** @fn QIMessageBox::setFlagChecked (bool)
 *
 *  Sets the state of the optional message box flag to a value of the argument.
 *
 *  @see #isFlagChecked()
 *  @see #setFlagText()
 */

QPushButton *QIMessageBox::createButton (QWidget *aParent, int aButton)
{
    if (aButton == 0)
        return 0;

    QString text;
    switch (aButton & ButtonMask)
    {
        case Ok:        text = tr ("OK"); break;
        case Yes:       text = tr ("Yes"); break;
        case No:        text = tr ("No"); break;
        case Cancel:    text = tr ("Cancel"); break;
        case Ignore:    text = tr ("Ignore"); break;
        default:
            AssertMsgFailed (("Type %d is not implemented", aButton));
            return NULL;
    }

    QPushButton *b = new QPushButton (text, aParent);

    if (aButton & Default)
    {
        b->setDefault (true);
        b->setFocus();
    }

    if (aButton & Escape)
        mButtonEsc = aButton & ButtonMask;

    return b;
}

/** @fn QIMessageBox::detailsText() const
 *
 *  Returns the text of the optional details box. The details box is empty
 *  by default, so QString::null will be returned.
 *
 *  @see #setDetailsText()
 */

/**
 *  Sets the text for the optional details box. Note that the details box
 *  is hidden by default, call #setDetailsShown(true) to make it visible.
 *
 *  @see #detailsText()
 *  @see #setDetailsShown()
 */
void QIMessageBox::setDetailsText (const QString &aText)
{
    mDetailsText->setText (aText);
}

/** @fn QIMessageBox::isDetailsShown() const
 *
 *  Returns true if the optional details box is shown and false otherwise.
 *  By default, the details box is not shown.
 *
 *  @see #setDetailsShown()
 *  @see #setDetailsText()
 */

/**
 *  Sets the visibility state of the optional details box
 *  to a value of the argument.
 *
 *  @see #isDetailsShown()
 *  @see #setDetailsText()
 */
void QIMessageBox::setDetailsShown (bool aShown)
{
    if (aShown)
    {
        mFlagCB_Details->setShown (mFlagCB_Main->isShown());
        mFlagCB_Details->setChecked (mFlagCB_Main->isChecked());
        mFlagCB_Details->setText (mFlagCB_Main->text());
        if (mFlagCB_Main->hasFocus())
            mFlagCB_Details->setFocus();
        mFlagCB_Main->setShown (false);
        mFlagCB = mFlagCB_Details;
        mSpacer->changeSize (0, 0, QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    mDetailsVBox->setShown (aShown);

    if (!aShown)
    {
        mFlagCB_Main->setShown (mFlagCB_Details->isShown());
        mFlagCB_Main->setChecked (mFlagCB_Details->isChecked());
        mFlagCB_Main->setText (mFlagCB_Details->text());
        if (mFlagCB_Details->hasFocus())
            mFlagCB_Main->setFocus();
        mFlagCB = mFlagCB_Main;
        mSpacer->changeSize (0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    }
}

