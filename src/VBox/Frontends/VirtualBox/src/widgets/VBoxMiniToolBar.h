/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxMiniToolBar class declaration & implementation. This is the toolbar shown on fullscreen mode.
 */

/*
 * Copyright (C) 2009 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __VBoxMiniToolBar_h__
#define __VBoxMiniToolBar_h__

/* VBox includes */
#include <VBoxToolBar.h>

/* Qt includes */
#include <QBasicTimer>

class QLabel;
class QMenu;

/**
 *  The VBoxMiniToolBar class is a toolbar shown inside full screen mode or seamless mode.
 *  It supports auto hiding and animated sliding up/down.
 */
class VBoxMiniToolBar : public VBoxToolBar
{
    Q_OBJECT;

public:

    enum Alignment
    {
        AlignTop,
        AlignBottom
    };

    VBoxMiniToolBar (QWidget *aParent, Alignment aAlignment, bool aIsActive, bool aAutoHide);

    VBoxMiniToolBar& operator<< (QList <QMenu*> aMenus);

    void setSeamlessMode (bool aIsSeamless);
    void setDisplayText (const QString &aText);

    bool isAutoHide() const;

    void updateDisplay (bool aShow, bool aSetHideFlag);

signals:

    void exitAction();
    void closeAction();
    void geometryUpdated();

protected:

    void mouseMoveEvent (QMouseEvent *aEvent);
    void timerEvent (QTimerEvent *aEvent);
    void showEvent (QShowEvent *aEvent);
    void paintEvent (QPaintEvent *aEvent);
    bool eventFilter(QObject *pObj, QEvent *pEvent);

private slots:

    void togglePushpin (bool aOn);

private:

    void initialize();
    void recreateMask();
    void moveToBase();
    QPoint mapFromScreen (const QPoint &aPoint);

    QAction *mAutoHideAct;
    QLabel *mDisplayLabel;

    QBasicTimer mScrollTimer;
    QBasicTimer mAutoScrollTimer;

    bool mActive;
    bool mPolished;
    bool mSeamless;
    bool mAutoHide;
    bool mSlideToScreen;
    bool mHideAfterSlide;

    int mAutoHideCounter;
    int mPositionX;
    int mPositionY;

    /* Lists of used spacers */
    QList <QWidget*> mMargins;
    QList <QWidget*> mSpacings;
    QList <QWidget*> mLabelMargins;

    /* Menu insert position */
    QAction *mInsertPosition;

    /* Tool-bar alignment */
    Alignment mAlignment;

    /* Wether to animate showing/hiding the toolbar */
    bool mAnimated;

    /* Interval (in milli seconds) for scrolling the toolbar, default is 20 msec */
    int mScrollDelay;

    /* The wait time while the cursor is not over the window after this amount of time (in msec),
     * the toolbar will auto hide if autohide is on. The default is 100msec. */
    int mAutoScrollDelay;

    /* Number of total steps before hiding. If it is 10 then wait 10 (steps) * 100ms (mAutoScrollDelay) = 1000ms delay.
     * The default is 10. */
    int mAutoHideTotalCounter;
};

#endif // __VBoxMiniToolBar_h__

