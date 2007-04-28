/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * Declarations of utility classes and functions
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
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

#ifndef __VBoxUtils_h__
#define __VBoxUtils_h__

#include <qobject.h>
#include <qevent.h>
#include <qlistview.h>
#include <qtextedit.h>

/**
 *  Simple ListView filter to disable unselecting all items by clicking in the
 *  unused area of the list (which is actually very annoying for the Single
 *  selection mode).
 */
class QIListViewSelectionPreserver : protected QObject
{
public:

    QIListViewSelectionPreserver (QObject *parent, QListView *alv)
        : QObject (parent), lv (alv)
    {
        lv->viewport()->installEventFilter (this);
    }

protected:

    bool eventFilter (QObject * /* o */, QEvent *e)
    {
        if (e->type() == QEvent::MouseButtonPress ||
            e->type() == QEvent::MouseButtonRelease ||
            e->type() == QEvent::MouseButtonDblClick)
        {
            QMouseEvent *me = (QMouseEvent *) e;
            if (!lv->itemAt (me->pos()))
                return true;
        }

        return false;
    }

private:

    QListView *lv;
};

/**
 *  Simple class that filters out presses and releases of the given key
 *  directed to a widget (the widget acts like if it would never handle
 *  this key).
 */
class QIKeyFilter : protected QObject
{
public:

    QIKeyFilter (QObject *aParent, Key aKey) : QObject (aParent), mKey (aKey) {}

    void watchOn (QObject *o) { o->installEventFilter (this); }

protected:

    bool eventFilter (QObject * /*o*/, QEvent *e)
    {
        if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
        {
            QKeyEvent *ke = (QKeyEvent *) e;
            if (ke->key() == mKey ||
                (mKey == Qt::Key_Enter && ke->key() == Qt::Key_Return))
            {
                ke->ignore();
                return false;
            }
        }

        return false;
    }

    Key mKey;
};

/**
 *  Simple class that filters out all key presses and releases
 *  got while the Alt key is pressed. For some very strange reason,
 *  QLineEdit accepts those combinations that are not used as accelerators,
 *  and inserts the corresponding characters to the entry field.
 */
class QIAltKeyFilter : protected QObject
{
public:

    QIAltKeyFilter (QObject *aParent) : QObject (aParent) {}

    void watchOn (QObject *o) { o->installEventFilter (this); }

protected:

    bool eventFilter (QObject * /*o*/, QEvent *e)
    {
        if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
        {
            QKeyEvent *ke = (QKeyEvent *) e;
            if (ke->state() & Qt::AltButton)
                return true;
        }
        return false;
    }
};

/** 
 *  Watches the given widget and makes sure the minimum widget size set by the layout
 *  manager does never get smaller than the previous minimum size set by the
 *  layout manager. This way, widgets with dynamic contents (i.e. text on some
 *  toggle buttons) will be able only to grow, never shrink, to avoid flicker
 *  during alternate contents updates (Pause -> Resume -> Pause -> ...).
 * 
 *  @todo not finished
 */
class QIConstraintKeeper : public QObject
{
    Q_OBJECT

public:

    QIConstraintKeeper (QWidget *aParent) : QObject (aParent)
    {
        aParent->setMinimumSize (aParent->size());
        aParent->installEventFilter (this);
    }

private:

    bool eventFilter (QObject *aObject, QEvent *aEvent)
    {
        if (aObject == parent() && aEvent->type() == QEvent::Resize)
        {
            QResizeEvent *ev = static_cast<QResizeEvent*> (aEvent);
            QSize oldSize = ev->oldSize();
            QSize newSize = ev->size();
            int maxWidth = newSize.width() > oldSize.width() ?
                newSize.width() : oldSize.width();
            int maxHeight = newSize.height() > oldSize.height() ?
                newSize.height() : oldSize.height();
            if (maxWidth > oldSize.width() || maxHeight > oldSize.height())
                ((QWidget*)parent())->setMinimumSize (maxWidth, maxHeight);
        }
        return QObject::eventFilter (aObject, aEvent);
    }
};


/** 
 *  Simple QTextEdit subclass to return its minimumSizeHint() as sizeHint()
 *  for getting more compact layout.
 */
class QITextEdit : public QTextEdit
{
public:

    QITextEdit (QWidget *aParent)
        : QTextEdit (aParent) {}

    QSize sizeHint() const
    {
        return minimumSizeHint();
    }

    QSize minimumSizeHint() const
    {
        /// @todo (r=dmik) this looks a bit meanignless. any comment?
        int w = 0;
        int h = heightForWidth (w);
        return QSize (w, h);
    }
};


#ifdef Q_WS_MAC
# undef PAGE_SIZE
# undef PAGE_SHIFT
# include <Carbon/Carbon.h>
class QImage;
class QPixmap;
CGImageRef DarwinQImageToCGImage (const QImage *aImage);
CGImageRef DarwinQPixmapToCGImage (const QPixmap *aPixmap);
CGImageRef DarwinQPixmapFromMimeSourceToCGImage (const char *aSource);
CGImageRef DarwinCreateDockBadge (const char *aSource);
#endif /* Q_WS_MAC */

#endif // __VBoxUtils_h__

