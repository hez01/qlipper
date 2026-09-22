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

#include <QApplication>
#include <QIcon>
#include <QFont>
#include <QTimer>

#include "qlippermodel.h"
#include "qlipperpreferences.h"
#include "qlipperdatabase.h"
#include "qlippernetwork.h"
#include "clipboardwrap.h"

namespace
{
    // Dynamic-history images are cached on disk (see QlipperItem::setImageContent);
    // drop the cache file whenever an item actually leaves the history, so the
    // cache doesn't grow without bound.
    void purgeImageCache(const QlipperItem &item)
    {
        if (item.contentType() != QlipperItem::Image)
            return;
        const QString path = QString::fromUtf8(item.content().value(QStringLiteral("x-qlipper/image-path")));
        QlipperPreferences::Instance()->removeCachedImage(path);
    }
}


QlipperModel::QlipperModel(QObject *parent) :
    QAbstractListModel(parent)
{
    m_network = new QlipperNetwork(this);

    m_boldFont.setBold(true);

    m_sticky = QlipperPreferences::Instance()->getStickyItems();
    m_dynamic = QlipperDatabase::Instance()->loadDynamicItems();
    // a little hack-a-magic to have almost
    if (m_sticky.count() + m_dynamic.count() == 0)
    {
        clipboard_changed(QClipboard::Clipboard);
        if (m_dynamic.count() == 0)
        {
            clearHistory();
        }
    }

#ifdef Q_WS_MAC
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));
    m_timer->start(1000);
#endif

    connect(ClipboardWrap::Instance(), &ClipboardWrap::changed, this, &QlipperModel::clipboard_changed);
}

QlipperModel::~QlipperModel()
{
    // Dynamic items are already persisted incrementally as they change; only
    // sticky items still need an explicit save here.
    QlipperPreferences::Instance()->saveStickyItems(m_sticky);
    if (QlipperPreferences::Instance()->clearItemsOnExit())
    {
        QlipperDatabase::Instance()->clearDynamicItems();
        for (const QlipperItem &item : m_dynamic)
            purgeImageCache(item);
    }
    m_dynamic.clear();
    m_sticky.clear();
}

void QlipperModel::resetPreferences()
{
    beginRemoveRows(QModelIndex(), 0, m_sticky.count() - 1);
    m_sticky.clear();
    endRemoveRows();
    QList<QlipperItem> sticky = QlipperPreferences::Instance()->getStickyItems();
    beginInsertRows(QModelIndex(), 0, sticky.count() - 1);
    m_sticky = sticky;
    endInsertRows();
}

int QlipperModel::rowCount(const QModelIndex&) const
{
    return m_sticky.count() + m_dynamic.count();
}

// TODO/FIXME: BETTER API! This is very confusing and potentially dangerous...
QList<QlipperItem> QlipperModel::getList(int & row) const
{
    if (m_sticky.count() > row)
    {
        return m_sticky;
    }
    else
    {
        row = row - m_sticky.count();
        return m_dynamic;
    }
}

QVariant QlipperModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return "";

    int row = index.row();

    QList<QlipperItem> list = getList(row);

    switch (role)
    {
    case Qt::DisplayRole:
        return list.at(row).displayRole();
    case Qt::DecorationRole:
        return list.at(row).decorationRole();
    case Qt::ToolTipRole:
        return list.at(row).tooltipRole();
    case Qt::FontRole:
        return m_currentIndex == index ? m_boldFont : m_normalFont;
    }

    return "";
}

Qt::ItemFlags QlipperModel::flags(const QModelIndex & index) const
{
    Q_UNUSED(index);
    return Qt::ItemIsEditable | Qt::ItemIsEnabled;
}

bool QlipperModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0)
        return false;

    const int total = m_sticky.count() + m_dynamic.count();
    if (row < 0 || row + count > total)
        return false;

    beginRemoveRows(QModelIndex(), row, row + count - 1);

    // Remove from the back of the range forward so earlier indices in the
    // same batch stay valid.
    bool stickyChanged = false;
    for (int r = row + count - 1; r >= row; --r)
    {
        if (r < m_sticky.count())
        {
            m_sticky.removeAt(r);
            stickyChanged = true;
        }
        else
        {
            const int dynIx = r - m_sticky.count();
            const QlipperItem &item = m_dynamic.at(dynIx);
            purgeImageCache(item);
            QlipperDatabase::Instance()->removeDynamicItem(item);
            m_dynamic.removeAt(dynIx);
        }
    }

    endRemoveRows();

    if (stickyChanged)
        QlipperPreferences::Instance()->saveStickyItems(m_sticky);

    return true;
}

void QlipperModel::clipboard_changed(QClipboard::Mode mode)
{
    if ((mode == QClipboard::Selection || mode == QClipboard::FindBuffer)
            && !QlipperPreferences::Instance()->platformExtensions())
    {
        return;
    }

    QlipperItem item(mode);
    if (item.isValid() && item.contentType() == QlipperItem::Binary)
    {
        // Unhandled binary content (anything that isn't text/html/url/image)
        // is never kept in history; leave the clipboard's own content
        // untouched and just skip recording it.
        return;
    }
    if (!item.isValid())
    {
        // See QlipperItem constructor: On X11 clipboard content is owned by the
        //    application, so naturally closing the application drops
        //    clipboard content. In this case the latest item should be set again.
        for (QList<QlipperItem>::const_iterator i = m_dynamic.begin(), i_e = m_dynamic.end(); i_e != i; ++i)
        {
            if (i->clipBoardMode() == item.clipBoardMode())
            {
                i->toClipboard(QlipperItem::ToCurrent);
                m_currentIndex = index(m_sticky.count() + (i - m_dynamic.begin()));
                break;
            }
        }
        return;
    }

    if (QlipperPreferences::Instance()->shouldSynchronizeClipboardsInstantly())
    {
        item.toClipboard(QlipperItem::ToOther);
    }

    // evaluate sticky items...
    int i = m_sticky.indexOf(item);
    if (i != -1)
    {
        m_currentIndex = index(i);
        return;
    }

    int ix = m_dynamic.indexOf(item);
    if (ix == -1)
    {
        const int sticky_count = m_sticky.count();
        beginInsertRows(QModelIndex(), sticky_count, sticky_count);
        m_dynamic.prepend(item);
        endInsertRows();
        QlipperDatabase::Instance()->insertDynamicItem(m_dynamic[0]);

        const int max_history = QlipperPreferences::Instance()->historyCount();
        // A max of 0 (or less) means unlimited: never trim.
        if (max_history > 0 && m_dynamic.count() > max_history)
        {
            beginRemoveRows(QModelIndex(), sticky_count + max_history - 1, sticky_count + m_dynamic.count() - 1);
            for (auto it = m_dynamic.begin() + (max_history - 1); it != m_dynamic.end(); ++it)
                purgeImageCache(*it);
            m_dynamic.erase(m_dynamic.begin() + (max_history - 1), m_dynamic.end());
            endRemoveRows();
            QlipperDatabase::Instance()->trimDynamicItems(max_history);
        }
        ix = 0;
    }
    setCurrentDynamic(ix);
}

void QlipperModel::setCurrentDynamic(int ix)
{
    // move if not already on top
    if (ix != 0)
    {
        const int sticky_count = m_sticky.count();
        beginMoveRows(QModelIndex(), sticky_count + ix, sticky_count + ix, QModelIndex(), sticky_count);
        m_dynamic.move(ix, 0);
        endMoveRows();
        QlipperDatabase::Instance()->touchDynamicItem(m_dynamic.at(0));
    }

    m_currentIndex = index(m_sticky.count());
    m_network->sendData(m_dynamic.at(0).content());
}


void QlipperModel::clearHistory()
{
    const int sticky_count = m_sticky.count();
    beginRemoveRows(QModelIndex(), sticky_count, sticky_count + m_dynamic.count() - 1);
    for (const QlipperItem &item : m_dynamic)
        purgeImageCache(item);
    m_dynamic.clear();
    endRemoveRows();
    QlipperDatabase::Instance()->clearDynamicItems();

    ClipboardContent tmp;
    tmp["text/plain"] = tr("Welcome to the Qlipper clipboard history applet").toUtf8();
    QlipperItem item(QClipboard::Clipboard, QlipperItem::PlainText, tmp);
    beginInsertRows(QModelIndex(), sticky_count, sticky_count);
    m_dynamic.append(item);
    endInsertRows();
    QlipperDatabase::Instance()->insertDynamicItem(m_dynamic[0]);
    m_currentIndex = index(sticky_count);
}

void QlipperModel::indexTriggered(const QModelIndex & index)
{
    if (!index.isValid())
        return;

    int row = index.row();
    QList<QlipperItem> list = getList(row);
    QlipperItem::Actions actions(QlipperItem::ToCurrent);
    actions |= QlipperPreferences::Instance()->shouldSynchronizeClipboards() ? QlipperItem::ToOther : QlipperItem::NoAction;
    list.at(row).toClipboard(actions);
    m_currentIndex = index;
    if (m_sticky.size() <= index.row())
    {
        setCurrentDynamic(row);
    }
}

void QlipperModel::timer_timeout()
{
#ifdef Q_WS_MAC
    m_timer->stop();
    clipboard_changed(QClipboard::Clipboard);
    m_timer->start();
#endif
}
