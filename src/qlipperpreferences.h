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

#ifndef QLIPPERPREFERENCES_H
#define QLIPPERPREFERENCES_H

#include <QtCore/QSettings>
#include <QDataStream>
#include "qlipperitem.h"

class QlipperPreferences : public QSettings
{
public:
    enum PSESynchronization
    {
        PSE_NO_SYNC = 0
            , PSE_SYNC_ON_SELECTION = 1
            , PSE_SYNC_INSTANTLY = 2
    };

public:
    static const QString DEFAULT_ICON_PATH;

    static QlipperPreferences *Instance();
    ~QlipperPreferences();

    QList<QlipperItem> getStickyItems();
    void saveStickyItems(QList<QlipperItem> list);

    // Legacy dynamic-history reader, kept only so QlipperDatabase can
    // one-time-migrate pre-existing history into SQLite. Nothing writes
    // dynamic items here anymore (see QlipperDatabase).
    QList<QlipperItem> getDynamicItems();

    QString getPathToIcon() const;
    void savePathToIcon(const QString &path);

    // Content-addressed on-disk cache for full-resolution clipboard images.
    // Keeps large image bytes out of the (frequently re-serialized) history
    // settings; returns the cached file's path, writing it only if it's not
    // already there.
    QString cacheImage(const QByteArray &data, const QString &mimeFormat);
    void removeCachedImage(const QString &path);

    bool trim();
    int displaySize() const;
    QString shortcut() const;
    int historyCount() const;
    bool platformExtensions() const;
    PSESynchronization synchronizePSE() const;
    bool clearItemsOnExit() const;
    bool confirmOnClear() const;

    bool networkSend() const;
    bool networkReceive() const;
    int networkPort() const;

    bool shouldSynchronizeClipboards() const;
    bool shouldSynchronizeClipboardsInstantly() const;

private:
    QlipperPreferences();

    static QlipperPreferences* m_instance;
};

#endif // QLIPPERPREFERENCES_H
