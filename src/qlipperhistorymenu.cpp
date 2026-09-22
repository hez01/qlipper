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
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>

#include "qlippermodel.h"
#include "qlipperhistorymenu.h"
#include "qlipperpreferences.h"

Q_DECLARE_METATYPE(QModelIndex)

namespace
{
    // Roles stored on each list item: the source model row (to reconstruct the
    // QlipperModel index on activation) and whether the entry is an image (so
    // image rows can be drawn taller than text rows).
    const int ModelRowRole = Qt::UserRole + 1;
    const int IsImageRole = Qt::UserRole + 2;

    const int kTextIcon = 20; // small icon column for text/url entries

    // Pixel width one row needs: icon column + the actual (already
    // display-size-truncated) text + padding + scrollbar. Used both to size a
    // row and, taken over all rows, to size the menu to just the longest entry.
    int rowPixelWidth(const QString &text, const QFont &font, int iconSz, const QStyle *st)
    {
        const int textW = QFontMetrics(font).horizontalAdvance(text);
        const int scrollbar = st ? st->pixelMetric(QStyle::PM_ScrollBarExtent) : 16;
        return iconSz + 8 /*gap*/ + textW + 20 /*padding*/ + scrollbar;
    }

    // Draws each history row with a per-entry height: short for text, tall for
    // images (so a copied image shows a real thumbnail). A single QListWidget
    // has one icon size for all rows, so the drawing/sizing is done here.
    class HistoryItemDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
        {
            QFont f = idx.data(Qt::FontRole).value<QFont>();
            if (f.resolveMask() == 0)
                f = opt.font;
            const QStyle *st = opt.widget ? opt.widget->style() : QApplication::style();
            const int w = rowPixelWidth(idx.data(Qt::DisplayRole).toString(), f, iconSizeFor(idx), st);
            return QSize(w, rowHeight(opt, idx));
        }

        void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
        {
            QStyleOptionViewItem o(opt);
            initStyleOption(&o, idx);
            const QWidget *w = o.widget;
            QStyle *st = w ? w->style() : QApplication::style();

            // Paint background / selection highlight only (no built-in icon/text).
            QStyleOptionViewItem bg(o);
            bg.text.clear();
            bg.icon = QIcon();
            bg.features &= ~QStyleOptionViewItem::HasDecoration;
            st->drawControl(QStyle::CE_ItemViewItem, &bg, p, w);

            const QRect r = opt.rect;
            const int iconSz = iconSizeFor(idx);
            int x = r.left() + kHPad;

            const QIcon ic = idx.data(Qt::DecorationRole).value<QIcon>();
            if (!ic.isNull() && iconSz > 0)
            {
                const QPixmap pm = ic.pixmap(QSize(iconSz, iconSz));
                const QSizeF ps = pm.deviceIndependentSize();
                const int px = x + int((iconSz - ps.width()) / 2);
                const int py = r.top() + int((r.height() - ps.height()) / 2);
                p->drawPixmap(px, py, pm);
                x += iconSz + kGap;
            }

            QFont f = idx.data(Qt::FontRole).value<QFont>();
            if (f.resolveMask() == 0)
                f = o.font;
            const QFontMetrics fm(f);
            const QRect textRect(x, r.top(), r.right() - x - kHPad, r.height());
            const QPalette::ColorGroup cg = (o.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
            const QColor col = (o.state & QStyle::State_Selected)
                                 ? o.palette.color(cg, QPalette::HighlightedText)
                                 : o.palette.color(cg, QPalette::Text);
            p->save();
            p->setFont(f);
            p->setPen(col);
            const QString txt = fm.elidedText(idx.data(Qt::DisplayRole).toString(),
                                              Qt::ElideRight, textRect.width());
            p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, txt);
            p->restore();
        }

    private:
        static constexpr int kHPad = 6;
        static constexpr int kGap = 8;
        static constexpr int kVPad = 4;

        static int iconSizeFor(const QModelIndex &idx)
        {
            return idx.data(IsImageRole).toBool()
                     ? QlipperPreferences::Instance()->menuIconSize()
                     : kTextIcon;
        }
        int rowHeight(const QStyleOptionViewItem &opt, const QModelIndex &idx) const
        {
            QFont f = idx.data(Qt::FontRole).value<QFont>();
            if (f.resolveMask() == 0)
                f = opt.font;
            const int fh = QFontMetrics(f).height();
            return qMax(fh, iconSizeFor(idx)) + 2 * kVPad;
        }
    };
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
    m_list->setUniformItemSizes(false); // rows vary: short text, tall images
    m_list->setItemDelegate(new HistoryItemDelegate(m_list));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFrameShape(QFrame::NoFrame);
    // Highlight the entry under the mouse cursor, like a normal menu.
    m_list->setMouseTracking(true);
    m_list->viewport()->setMouseTracking(true);
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem *it) {
        if (it && (it->flags() & Qt::ItemIsEnabled))
        {
            m_keyboardNavigated = false;
            m_list->setCurrentItem(it);
        }
    });
    // A single click on an entry activates it, like a menu item.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { activateCurrent(); });

    QWidgetAction *listAction = new QWidgetAction(this);
    listAction->setDefaultWidget(m_list);
    addAction(listAction);

    connect(this, &QMenu::aboutToShow, this, &QlipperHistoryMenu::onAboutToShow);

    // Only rebuild for model changes while the menu is visible; a hidden menu is
    // rebuilt from scratch in onAboutToShow(), so rebuilding it on every
    // clipboard change would be wasted work.
    connect(model, &QAbstractItemModel::modelReset, this, &QlipperHistoryMenu::rebuildIfVisible);
    connect(model, &QAbstractItemModel::rowsInserted, this, &QlipperHistoryMenu::rebuildIfVisible);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &QlipperHistoryMenu::rebuildIfVisible);
    connect(model, &QAbstractItemModel::rowsMoved, this, &QlipperHistoryMenu::rebuildIfVisible);
    connect(model, &QAbstractItemModel::dataChanged, this, &QlipperHistoryMenu::rebuildIfVisible);

    rebuild();
}

void QlipperHistoryMenu::rebuild()
{
    m_keyboardNavigated = false;
    m_list->clear();

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
        item->setData(IsImageRole, idx.data(QlipperModel::IsImageRole).toBool());
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

void QlipperHistoryMenu::rebuildIfVisible()
{
    if (isVisible())
        rebuild();
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

int QlipperHistoryMenu::contentWidth() const
{
    const int imgIcon = QlipperPreferences::Instance()->menuIconSize();
    const QStyle *st = m_list->style();
    int maxW = 0;
    for (int r = 0; r < m_list->count(); ++r)
    {
        const QListWidgetItem *it = m_list->item(r);
        const int iconSz = it->data(IsImageRole).toBool() ? imgIcon : kTextIcon;
        maxW = qMax(maxW, rowPixelWidth(it->text(), it->font(), iconSz, st));
    }
    // The display text is already truncated to the display-size limit, so this
    // never exceeds that limit; when entries are shorter, the menu is narrower.
    return qMax(160, maxW);
}

void QlipperHistoryMenu::applyHeightLimit()
{
    const int count = m_list->count();
    const int visible = QlipperPreferences::Instance()->visibleCount();

    // How many rows to show before scrolling: the preference, or all of them
    // when it is 0 (no limit) or when there are fewer entries than the limit.
    int wanted = (visible > 0) ? qMin(visible, count) : count;
    wanted = qMax(1, wanted);

    // Rows now vary in height (short text, tall images), so sum the heights of
    // the first `wanted` rows rather than multiplying a single row height.
    int contentH = 0;
    for (int r = 0; r < wanted; ++r)
        contentH += m_list->sizeHintForRow(r);

    // Never let the menu grow past the screen; the list scrolls for the rest.
    const QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (scr)
    {
        const int screenH = scr->availableGeometry().height();
        const int chrome = m_search->sizeHint().height() + 90; // separator, frame, title bar, slack
        contentH = qMin(contentH, qMax(50, screenH - chrome));
    }

    const int frame = 2 * m_list->frameWidth();
    const int listH = contentH + frame;
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

    m_keyboardNavigated = true;
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
    // which reset the selection to the first row (and the keyboard-nav flag);
    // move it back near where the removed entry was and stay in keyboard-delete
    // mode so repeated Delete keeps removing entries.
    if (m_list->count() > 0 && (m_list->item(0)->flags() & Qt::ItemIsEnabled))
    {
        m_list->setCurrentRow(qMin(listRow, m_list->count() - 1));
        m_keyboardNavigated = true;
    }
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
            // Only remove an entry when the user has navigated to it with the
            // arrow keys; otherwise let the line edit handle Delete as ordinary
            // forward-delete while typing a filter.
            if (m_keyboardNavigated)
            {
                if (QListWidgetItem *it = m_list->currentItem();
                    it && (it->flags() & Qt::ItemIsEnabled))
                {
                    removeCurrent();
                    return true;
                }
            }
            break;
        default:
            break;
        }
    }
    return QMenu::eventFilter(watched, event);
}
