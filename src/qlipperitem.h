/*
Qlipper - clipboard history manager
Copyright (C) 2012 Petr Vanek <petr@yarpen.cz>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef QLIPPERITEM_H
#define QLIPPERITEM_H

#include <QtGui/QClipboard>
#include <QtCore/QVariant>
#include <QtDebug>
#include "qlippertypes.h"

class QImage;
class QMimeData;

class QlipperItem
{
public:
    enum ContentType {
        PlainText,
        RichText,
        Binary,
        Url,
        Sticky,
        Image
    };

    enum Action
    {
        NoAction = 0
            , ToCurrent = 1
            , ToOther = 1 << 1
    };
    Q_DECLARE_FLAGS(Actions, Action)


    QlipperItem();
    QlipperItem(QClipboard::Mode mode);
    QlipperItem(QClipboard::Mode mode, ContentType contentType, const ClipboardContent &content);
    QlipperItem(const QString & sticky);

    QClipboard::Mode clipBoardMode() const;
    ClipboardContent content() const { return m_content; }
    QString display() const { return m_display; }
    QlipperItem::ContentType contentType() const { return m_contentType; }

    bool isValid() const { return m_valid; }

    // Row id in QlipperDatabase's dynamic_items table; -1 until the item has
    // actually been persisted there. Not part of content identity/equality.
    qint64 dbId() const { return m_dbId; }
    void setDbId(qint64 id) { m_dbId = id; }

    void toClipboard(const Actions & actions) const;

    QString displayRole() const;
    // Full, untruncated text used for menu search (displayRole() is truncated).
    QString searchRole() const;
    QIcon decorationRole() const;
    QString tooltipRole() const;

    bool operator==(const QlipperItem &other) const;

private:
    QClipboard::Mode m_mode;
    ContentType m_contentType;
    bool m_valid;
    qint64 m_dbId = -1;

    ClipboardContent m_content;
    QString m_display;

    QIcon iconForContentType() const;
    bool setImageContent(const QImage &image, const QMimeData *mimeData);
    QString imageDimensions() const;
};

Q_DECLARE_METATYPE(QlipperItem::ContentType)

QDebug operator<<(QDebug dbg, const QlipperItem &c);

#endif // QLIPPERITEM_H
