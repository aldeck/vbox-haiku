/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * innotek Qt extensions: QIStateIndicator class implementation
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

#include "QIStateIndicator.h"

#include <qpainter.h>

/** @clas QIStateIndicator
 *
 *  The QIStateIndicator class is a simple class that can visually indicate
 *  the state of some thing, as described by the state property.
 */

/**
 *  Constructs a new QIStateIndicator instance. This instance is useless
 *  until icons are specified for necessary states.
 *
 *  @param aState
 *      the initial indicator state
 */
QIStateIndicator::QIStateIndicator (int aState,
                                    QWidget *aParent, const char *aName,
                                    WFlags aFlags)
    : QFrame (aParent, aName, aFlags | WStaticContents | WMouseNoMask)
{
    mState = aState;
    mSize = QSize (0, 0);
    mStateIcons.setAutoDelete (true);

    setSizePolicy (QSizePolicy (QSizePolicy::Fixed, QSizePolicy::Fixed));

    /* we will precompose the pixmap background using the widget bacground in
     * drawContents(), so try to set the correct bacground origin for the
     * case when a pixmap is used as a widget background. */
    if (aParent)
        setBackgroundOrigin (aParent->backgroundOrigin());
}

QSize QIStateIndicator::sizeHint() const
{
    return mSize;
}

QPixmap QIStateIndicator::stateIcon (int aState) const
{
    Icon *icon = mStateIcons [aState];
    return icon ? icon->pixmap : QPixmap();
}

/**
 *  Sets an icon for the specified state. The first icon set by this method
 *  defines the preferred size of this indicator. All other icons will be
 *  scaled to fit this size.
 *
 *  @note If this widget is constructed with the WNoAutoErase flag, then all
 *  transparent areas of the new state icon are filled with the widget
 *  background color or pixmap (as taken from the widget palette), to provide
 *  flicker free state redraws in one single operation (which is useful for
 *  indicators that frequently change their state).
 */
void QIStateIndicator::setStateIcon (int aState, const QPixmap &aPixmap)
{
    /* Here we just set the original pixmap. All actual work from the @note
     * above takes place in #drawContents(). */
    mStateIcons.insert (aState, new Icon (aPixmap));

    if (mSize.isNull())
        mSize = aPixmap.size();
}

void QIStateIndicator::setState (int aState)
{
    mState = aState;
    repaint (false);
}

void QIStateIndicator::drawContents (QPainter *aPainter)
{
    Icon *icon = mStateIcons [mState];
    if (!icon)
    {
        erase();
    }
    else
    {
        if (testWFlags (WNoAutoErase))
        {
            QColor bgColor = paletteBackgroundColor();
            const QPixmap *bgPixmap = paletteBackgroundPixmap();
            QPoint bgOff = backgroundOffset();

            bool bgOffChanged = icon->bgOff != bgOff;
            bool bgPixmapChanged = icon->bgPixmap != bgPixmap ||
                (icon->bgPixmap != NULL &&
                 icon->bgPixmap->serialNumber() != bgPixmap->serialNumber());
            bool bgColorChanged = icon->bgColor != bgColor;

            /* re-precompose the pixmap if any of these have changed */
            if (icon->cached.isNull() ||
                bgOffChanged || bgPixmapChanged || bgColorChanged)
            {
                int w = icon->pixmap.width();
                int h = icon->pixmap.height();
                if (icon->cached.isNull())
                    icon->cached = QPixmap (w, h);

                if (bgPixmap || bgOffChanged || bgPixmapChanged)
                {
                    QPainter p (&icon->cached);
                    p.drawTiledPixmap (QRect (0, 0, w, h), *bgPixmap, bgOff);
                }
                else
                {
                    icon->cached.fill (bgColor);
                }
                /* paint the icon on top of the widget background sample */
                bitBlt (&icon->cached, 0, 0, &icon->pixmap,
                        0, 0, w, h, CopyROP, false);
                /* store the new values */
                icon->bgColor = bgColor;
                icon->bgPixmap = bgPixmap;
                icon->bgOff = bgOff;
            }
            /* draw the precomposed pixmap */
            aPainter->drawPixmap (contentsRect(), icon->cached);
        }
        else
        {
            aPainter->drawPixmap (contentsRect(), icon->pixmap);
        }
    }
}

void QIStateIndicator::mouseDoubleClickEvent (QMouseEvent * e)
{
    emit mouseDoubleClicked (this, e);
}

void QIStateIndicator::contextMenuEvent (QContextMenuEvent * e)
{
    emit contextMenuRequested (this, e);
}
