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

#ifndef QLIPPERDATABASE_H
#define QLIPPERDATABASE_H

#include <QList>
#include "qlipperitem.h"

// SQLite-backed store for the dynamic clipboard history. Replaces the old
// QSettings-array storage, which had to re-serialize the *entire* history on
// every single clipboard change (and, before images were cached to disk,
// re-wrote every image's bytes right along with it) -- the main cause of the
// freeze/slowness reported with a non-trivial history. Every mutation here is
// a single targeted statement instead.
//
// Sticky items are unaffected: they're few, user-curated via the preferences
// dialog, and stay on QSettings (see QlipperPreferences).
class QlipperDatabase
{
public:
    static QlipperDatabase *Instance();

    // Loads every dynamic item, most-recent first. On first run (empty table),
    // transparently migrates whatever was stored the old way in QSettings.
    QList<QlipperItem> loadDynamicItems();

    // Inserts a brand-new item as the most recent entry; sets item.dbId().
    void insertDynamicItem(QlipperItem &item);
    // Re-marks an already-stored item (by its dbId()) as the most recent.
    void touchDynamicItem(const QlipperItem &item);
    // Deletes a single stored item by its dbId().
    void removeDynamicItem(const QlipperItem &item);
    // Keeps only the `count` most recent items, deleting the rest.
    void trimDynamicItems(int count);
    // Deletes every dynamic item.
    void clearDynamicItems();

private:
    QlipperDatabase();

    void openAndMigrate();
    qint64 nextRecency();

    bool m_ready = false;

    static QlipperDatabase *m_instance;
};

#endif // QLIPPERDATABASE_H
