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

#include <QDataStream>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtDebug>

#include "qlipperpreferences.h"
#include "qlipperdatabase.h"

namespace
{
    const char CONNECTION_NAME[] = "qlipper_history";

    QByteArray serializeContent(const ClipboardContent &content)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out << content;
        return bytes;
    }

    ClipboardContent deserializeContent(const QByteArray &bytes)
    {
        ClipboardContent content;
        QDataStream in(bytes);
        in >> content;
        return content;
    }
}

QlipperDatabase *QlipperDatabase::m_instance = nullptr;

QlipperDatabase *QlipperDatabase::Instance()
{
    if (!m_instance)
        m_instance = new QlipperDatabase();
    return m_instance;
}

QlipperDatabase::QlipperDatabase()
{
    openAndMigrate();
}

void QlipperDatabase::openAndMigrate()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir))
    {
        qWarning() << "QlipperDatabase: could not create" << dir << "; history will not persist.";
        return;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QString::fromLatin1(CONNECTION_NAME));
    db.setDatabaseName(dir + QStringLiteral("/history.sqlite3"));
    if (!db.open())
    {
        qWarning() << "QlipperDatabase: failed to open history.sqlite3:" << db.lastError().text();
        return;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS dynamic_items ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  mode INTEGER NOT NULL,"
            "  content_type INTEGER NOT NULL,"
            "  content BLOB NOT NULL,"
            "  recency INTEGER NOT NULL"
            ")")))
    {
        qWarning() << "QlipperDatabase: failed to create schema:" << q.lastError().text();
        return;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dynamic_items_recency ON dynamic_items(recency)"));

    m_ready = true;

    // One-time migration from the old QSettings-array storage: only if the
    // new table is still empty and the old data actually exists.
    q.exec(QStringLiteral("SELECT COUNT(*) FROM dynamic_items"));
    if (q.next() && q.value(0).toLongLong() == 0)
    {
        const QList<QlipperItem> legacy = QlipperPreferences::Instance()->getDynamicItems();
        if (!legacy.isEmpty())
        {
            db.transaction();
            QSqlQuery insert(db);
            insert.prepare(QStringLiteral(
                "INSERT INTO dynamic_items (mode, content_type, content, recency) VALUES (?, ?, ?, ?)"));
            // legacy list is most-recent-first; give the first item the highest recency.
            qint64 recency = legacy.count();
            for (const QlipperItem &item : legacy)
            {
                insert.addBindValue(static_cast<int>(item.clipBoardMode()));
                insert.addBindValue(static_cast<int>(item.contentType()));
                insert.addBindValue(serializeContent(item.content()));
                insert.addBindValue(recency--);
                insert.exec();
            }
            db.commit();
        }

        // Never migrate again, and stop shipping the (now redundant) old copy
        // in the settings file.
        QlipperPreferences::Instance()->beginGroup(QStringLiteral("dynamic"));
        QlipperPreferences::Instance()->remove(QStringLiteral("items"));
        QlipperPreferences::Instance()->endGroup();
    }
}

qint64 QlipperDatabase::nextRecency()
{
    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.exec(QStringLiteral("SELECT COALESCE(MAX(recency), 0) + 1 FROM dynamic_items"));
    return q.next() ? q.value(0).toLongLong() : 1;
}

QList<QlipperItem> QlipperDatabase::loadDynamicItems()
{
    QList<QlipperItem> items;
    if (!m_ready)
        return items;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.exec(QStringLiteral("SELECT id, mode, content_type, content FROM dynamic_items ORDER BY recency DESC"));
    while (q.next())
    {
        QlipperItem item(static_cast<QClipboard::Mode>(q.value(1).toInt()),
                          static_cast<QlipperItem::ContentType>(q.value(2).toInt()),
                          deserializeContent(q.value(3).toByteArray()));
        item.setDbId(q.value(0).toLongLong());
        if (item.isValid())
            items.append(item);
    }
    return items;
}

void QlipperDatabase::insertDynamicItem(QlipperItem &item)
{
    if (!m_ready)
        return;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.prepare(QStringLiteral(
        "INSERT INTO dynamic_items (mode, content_type, content, recency) VALUES (?, ?, ?, ?)"));
    q.addBindValue(static_cast<int>(item.clipBoardMode()));
    q.addBindValue(static_cast<int>(item.contentType()));
    q.addBindValue(serializeContent(item.content()));
    q.addBindValue(nextRecency());
    if (q.exec())
        item.setDbId(q.lastInsertId().toLongLong());
    else
        qWarning() << "QlipperDatabase: insert failed:" << q.lastError().text();
}

void QlipperDatabase::touchDynamicItem(const QlipperItem &item)
{
    if (!m_ready || item.dbId() < 0)
        return;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.prepare(QStringLiteral("UPDATE dynamic_items SET recency = ? WHERE id = ?"));
    q.addBindValue(nextRecency());
    q.addBindValue(item.dbId());
    q.exec();
}

void QlipperDatabase::removeDynamicItem(const QlipperItem &item)
{
    if (!m_ready || item.dbId() < 0)
        return;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.prepare(QStringLiteral("DELETE FROM dynamic_items WHERE id = ?"));
    q.addBindValue(item.dbId());
    q.exec();
}

void QlipperDatabase::trimDynamicItems(int count)
{
    if (!m_ready || count < 0)
        return;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.prepare(QStringLiteral(
        "DELETE FROM dynamic_items WHERE id NOT IN "
        "(SELECT id FROM dynamic_items ORDER BY recency DESC LIMIT ?)"));
    q.addBindValue(count);
    q.exec();
}

void QlipperDatabase::clearDynamicItems()
{
    if (!m_ready)
        return;

    QSqlQuery q(QSqlDatabase::database(QString::fromLatin1(CONNECTION_NAME)));
    q.exec(QStringLiteral("DELETE FROM dynamic_items"));
}
