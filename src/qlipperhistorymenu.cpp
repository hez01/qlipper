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

#include <QLineEdit>
#include <QListWidget>
#include <QWidgetAction>
#include <QKeyEvent>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QStyle>

#include "qlippermodel.h"
#include "qlipperhistorymenu.h"
#include "qlipperpreferences.h"

Q_DECLARE_METATYPE(QModelIndex)

namespace
{
    // Role on each list item that stores the source model row, so the matching
    // QlipperModel index can be reconstructed when the entry is activated.
    const int ModelRowRole = Qt::UserRole + 1;
}

QlipperHistoryMenu::QlipperHistoryMenu(QlipperModel *model, QWidget *parent)
    : QMenu(parent)
    , m_model(model)
{
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search history..."));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &QlipperHistoryMenu::rebuild);

    QWidgetAction *searchAction = new QWidgetAction(this);
    searchAction->setDefaultWidget(m_search);
    addAction(searchAction);
    addSeparator();

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFrameShape(QFrame::NoFrame);
    // Highlight the entry under the mouse cursor, like a normal menu.
    m_list->setMouseTracking(true);
    m_list->viewport()->setMouseTracking(true);
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem *it) {
        if (it && (it->flags() & Qt::ItemIsEnabled))
            m_list->setCurrentItem(it);
    });
    // A single click on an entry activates it, like a menu item.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { activateCurrent(); });

    QWidgetAction *listAction = new QWidgetAction(this);
    listAction->setDefaultWidget(m_list);
    addAction(listAction);

    connect(this, &QMenu::aboutToShow, this, &QlipperHistoryMenu::onAboutToShow);

    connect(model, &QAbstractItemModel::modelReset, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsInserted, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsMoved, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::dataChanged, this, &QlipperHistoryMenu::rebuild);

    rebuild();
}

void QlipperHistoryMenu::rebuild()
{
    m_list->clear();

    const int icon = QlipperPreferences::Instance()->menuIconSize();
    m_list->setIconSize(QSize(icon, icon));

    const QString filter = m_search->text();
    const bool filtering = !filter.isEmpty();

    // Every matching entry is added; the "Items shown" preference limits how
    // many are *visible* before the list scrolls (see applyHeightLimit()), not
    // how many exist. Filtering scans the whole history via the full,
    // untruncated SearchRole.
    const int rows = m_model->rowCount(QModelIndex());
    for (int i = 0; i < rows; ++i)
    {
        const QModelIndex idx = m_model->index(i, 0);

        if (filtering)
        {
            const QString haystack = idx.data(QlipperModel::SearchRole).toString();
            if (!haystack.contains(filter, Qt::CaseInsensitive))
                continue;
        }

        QListWidgetItem *item = new QListWidgetItem(
            qvariant_cast<QIcon>(idx.data(Qt::DecorationRole)),
            idx.data(Qt::DisplayRole).toString());
        item->setFont(qvariant_cast<QFont>(idx.data(Qt::FontRole)));
        item->setToolTip(idx.data(Qt::ToolTipRole).toString());
        item->setData(ModelRowRole, i);
        m_list->addItem(item);
    }

    if (m_list->count() == 0)
    {
        QListWidgetItem *empty = new QListWidgetItem(tr("No matches"));
        empty->setFlags(Qt::NoItemFlags);
        m_list->addItem(empty);
    }

    // Pre-select the first entry so typing a query and pressing Enter activates
    // the top match. Focus stays in the search box for typing.
    selectFirst();
    applyHeightLimit();
}

void QlipperHistoryMenu::selectFirst()
{
    for (int r = 0; r < m_list->count(); ++r)
    {
        if (m_list->item(r)->flags() & Qt::ItemIsEnabled)
        {
            m_list->setCurrentRow(r);
            return;
        }
    }
}

int QlipperHistoryMenu::rowPixelHeight() const
{
    if (m_list->count() > 0)
    {
        const int h = m_list->sizeHintForRow(0);
        if (h > 0)
            return h;
    }
    return QlipperPreferences::Instance()->menuIconSize() + 6;
}

int QlipperHistoryMenu::contentWidth() const
{
    // Width needed to show `displaySize` characters at the configured font size
    // (Y), plus the icon and scrollbar. This is what makes the menu "as wide as
    // required to fit X characters" rather than a fixed narrow width.
    const int chars = QlipperPreferences::Instance()->displaySize();
    const int pt = QlipperPreferences::Instance()->menuFontPointSize();

    QFont f = m_list->font();
    if (pt > 0)
        f.setPointSize(pt);
    f.setBold(true); // the current entry is bold, i.e. the widest case
    const QFontMetrics fm(f);

    const int avg = qMax(1, fm.averageCharWidth());
    const int textW = avg * (chars + 2); // +2 chars of slack for proportional fonts

    const int iconW = QlipperPreferences::Instance()->menuIconSize();
    const int gap = 8;                                   // icon-to-text gap
    const int itemPadding = 16;                          // list item left+right padding
    const int scrollbar = style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    return iconW + gap + textW + itemPadding + scrollbar + 4;
}

void QlipperHistoryMenu::applyHeightLimit()
{
    const int rowH = rowPixelHeight();
    const int count = m_list->count();
    const int visible = QlipperPreferences::Instance()->visibleCount();

    // How many rows the viewport should show: the preference, or all of them
    // when it is 0 (no limit) or when there are fewer entries than the limit.
    int wanted = (visible > 0) ? qMin(visible, count) : count;
    wanted = qMax(1, wanted);

    // Never let the menu grow past the screen: clamp to what fits, and the
    // list scrolls for the rest.
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (scr)
    {
        const int screenH = scr->availableGeometry().height();
        const int chrome = m_search->sizeHint().height() + 90; // separator, frame, title bar, slack
        const int maxRows = qMax(1, (screenH - chrome) / rowH);
        wanted = qMin(wanted, maxRows);
    }

    const int frame = 2 * m_list->frameWidth();
    const int listH = wanted * rowH + frame;
    m_list->setFixedHeight(listH);

    // Width: wide enough to fit the configured number of characters. Setting a
    // minimum makes the menu pop up at the right width; when already shown, the
    // menu is resized below (its cached size hint is stale).
    const int w = contentWidth();
    m_list->setMinimumWidth(w);
    m_search->setMinimumWidth(w);

    // QMenu fixes its size when it pops up and does not re-fit (its cached size
    // hint is stale) when a child widget-action changes size after filtering.
    // Once the constant chrome above the list has been measured, drive the
    // menu height directly so it hugs the list, keeping the top-left corner.
    if (isVisible() && m_chrome >= 0)
    {
        const int targetH = m_chrome + listH;
        const int targetW = (m_hframe >= 0) ? m_hframe + w : width();
        // Only resize (and re-anchor) when the size actually changed, e.g. after
        // filtering. On the initial show the menu already popped up at the right
        // size, and resizing it there would let the WM shift its position.
        if (targetH != height() || targetW != width())
        {
            const QPoint tl = pos();
            resize(targetW, targetH);
            move(tl);
        }
    }
}

void QlipperHistoryMenu::onAboutToShow()
{
    m_search->clear();
    rebuild();
    m_search->setFocus();
    // Re-assert the selection and sizing once the menu is actually shown (row
    // heights are only exact after layout). Measure the constant chrome above
    // the list on first show so later filtering can resize the menu to fit.
    QTimer::singleShot(0, this, [this]{
        if (m_chrome < 0 && m_list->height() > 0)
        {
            m_chrome = height() - m_list->height();
            m_hframe = width() - m_list->width();
        }
        applyHeightLimit();
        selectFirst();
    });
}

void QlipperHistoryMenu::moveCurrent(int direction)
{
    const int count = m_list->count();
    if (count == 0 || !(m_list->item(0)->flags() & Qt::ItemIsEnabled))
        return;

    int r = m_list->currentRow();
    if (r < 0)
        r = (direction > 0) ? 0 : count - 1;
    else
        r = (r + direction + count) % count;
    m_list->setCurrentRow(r); // also scrolls to keep the row visible
}

void QlipperHistoryMenu::activateCurrent()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it || !(it->flags() & Qt::ItemIsEnabled))
        return;
    const QModelIndex idx = m_model->index(it->data(ModelRowRole).toInt(), 0);
    if (idx.isValid())
        emit triggered(idx);
}

void QlipperHistoryMenu::removeCurrent()
{
    QListWidgetItem *it = m_list->currentItem();
    if (!it || !(it->flags() & Qt::ItemIsEnabled))
        return;
    const QModelIndex idx = m_model->index(it->data(ModelRowRole).toInt(), 0);
    if (!idx.isValid())
        return;

    const int listRow = m_list->currentRow();
    m_model->removeRow(idx.row(), idx.parent());
    // The model's rowsRemoved signal has already run rebuild() synchronously,
    // which reset the selection to the first row; move it back near where the
    // removed entry was.
    if (m_list->count() > 0 && (m_list->item(0)->flags() & Qt::ItemIsEnabled))
        m_list->setCurrentRow(qMin(listRow, m_list->count() - 1));
}

bool QlipperHistoryMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key())
        {
        case Qt::Key_Escape:
            close();
            return true;
        case Qt::Key_Down:
            moveCurrent(1);
            return true;
        case Qt::Key_Up:
            moveCurrent(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateCurrent();
            return true;
        case Qt::Key_Delete:
            // Only steal Delete when a real entry is highlighted; otherwise let
            // the line edit handle it as forward-delete while typing a filter.
            if (QListWidgetItem *it = m_list->currentItem();
                it && (it->flags() & Qt::ItemIsEnabled))
            {
                removeCurrent();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QMenu::eventFilter(watched, event);
}
