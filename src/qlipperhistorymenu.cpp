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
#include <QWidgetAction>
#include <QKeyEvent>
#include <QTimer>
#include <QProxyStyle>
#include <QStyle>

#include "qlippermodel.h"
#include "qlipperhistorymenu.h"
#include "qlipperpreferences.h"

Q_DECLARE_METATYPE(QModelIndex)

namespace
{
    // Proxy style applied only to the history menu. It does two things:
    //  * enlarges item icons (PM_SmallIconSize) so image thumbnails are big
    //    enough to actually recognise, and
    //  * enables menu scrolling (SH_Menu_Scrollable) so a long history scrolls
    //    with the wheel / scroll indicators instead of growing off-screen.
    // Scoped to this menu via QWidget::setStyle, so it never affects the rest
    // of the application (child widgets keep the application style).
    class HistoryMenuStyle : public QProxyStyle
    {
    public:
        int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                        const QWidget *widget = nullptr) const override
        {
            // Read live so the size setting takes effect the next time the menu
            // is shown, without restarting. Bounded to the stored thumbnail
            // resolution (256px, see QlipperItem) so icons stay crisp.
            if (metric == PM_SmallIconSize)
                return QlipperPreferences::Instance()->menuIconSize();
            return QProxyStyle::pixelMetric(metric, option, widget);
        }

        int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                      const QWidget *widget = nullptr,
                      QStyleHintReturn *returnData = nullptr) const override
        {
            if (hint == SH_Menu_Scrollable)
                return 1;
            return QProxyStyle::styleHint(hint, option, widget, returnData);
        }
    };
}

QlipperHistoryMenu::QlipperHistoryMenu(QlipperModel *model, QWidget *parent)
    : QMenu(parent)
    , m_model(model)
{
    // Larger icons + scrollable menu, scoped to this widget only.
    auto *menuStyle = new HistoryMenuStyle;
    menuStyle->setParent(this);
    setStyle(menuStyle);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search history..."));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &QlipperHistoryMenu::rebuild);

    QWidgetAction *searchAction = new QWidgetAction(this);
    searchAction->setDefaultWidget(m_search);
    addAction(searchAction);
    addSeparator();

    connect(this, &QMenu::aboutToShow, this, &QlipperHistoryMenu::onAboutToShow);
    connect(this, &QMenu::triggered, this, &QlipperHistoryMenu::onMenuTriggered);

    connect(model, &QAbstractItemModel::modelReset, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsInserted, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::rowsMoved, this, &QlipperHistoryMenu::rebuild);
    connect(model, &QAbstractItemModel::dataChanged, this, &QlipperHistoryMenu::rebuild);

    rebuild();
}

void QlipperHistoryMenu::rebuild()
{
    const QList<QAction *> oldActions = m_itemActions;
    for (QAction *action : oldActions)
    {
        removeAction(action);
        action->deleteLater();
    }
    m_itemActions.clear();

    const QString filter = m_search->text();
    const bool filtering = !filter.isEmpty();
    // When not filtering, cap the menu at the first N entries (0 = show all).
    // Filtering always scans the whole history (SearchRole is the full,
    // untruncated content), so a match below the cap is still found and shown.
    const int visible = QlipperPreferences::Instance()->visibleCount();

    const int rows = m_model->rowCount(QModelIndex());
    int shown = 0;
    for (int i = 0; i < rows; ++i)
    {
        const QModelIndex idx = m_model->index(i, 0);

        if (filtering)
        {
            const QString haystack = idx.data(QlipperModel::SearchRole).toString();
            if (!haystack.contains(filter, Qt::CaseInsensitive))
                continue;
        }
        else if (visible > 0 && shown >= visible)
        {
            break;
        }

        const QString text = idx.data(Qt::DisplayRole).toString();
        QAction *action = new QAction(qvariant_cast<QIcon>(idx.data(Qt::DecorationRole)), text, this);
        action->setFont(qvariant_cast<QFont>(idx.data(Qt::FontRole)));
        action->setToolTip(idx.data(Qt::ToolTipRole).toString());
        QVariant v;
        v.setValue(idx);
        action->setData(v);

        addAction(action);
        m_itemActions.append(action);
        ++shown;
    }

    if (m_itemActions.isEmpty())
    {
        QAction *empty = new QAction(tr("No matches"), this);
        empty->setEnabled(false);
        addAction(empty);
        m_itemActions.append(empty);
    }

    // Pre-select the first entry so that after opening the menu (or after
    // typing a filter) the top match is already highlighted and a single
    // Enter activates it. Focus stays in the search box for typing.
    selectFirst();
}

void QlipperHistoryMenu::selectFirst()
{
    if (!m_itemActions.isEmpty() && m_itemActions.first()->isEnabled())
        setActiveAction(m_itemActions.first());
}

void QlipperHistoryMenu::onAboutToShow()
{
    m_search->clear();
    rebuild();
    m_search->setFocus();
    // rebuild() already highlighted the first entry, but QMenu resets its
    // current action when it is actually shown (and highlights whatever sits
    // under the cursor when it pops up there). Re-assert once the menu is up.
    QTimer::singleShot(0, this, [this]{ selectFirst(); });
}

void QlipperHistoryMenu::onMenuTriggered(QAction *action)
{
    const QVariant v = action->data();
    if (v.canConvert<QModelIndex>())
        activateIndex(qvariant_cast<QModelIndex>(v));
}

void QlipperHistoryMenu::activateIndex(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    emit triggered(index);
}

void QlipperHistoryMenu::highlightStep(int direction)
{
    if (m_itemActions.isEmpty() || !m_itemActions.first()->isEnabled())
        return;

    const int count = m_itemActions.count();
    const int current = m_itemActions.indexOf(activeAction());
    const int next = current == -1 ? (direction > 0 ? 0 : count - 1)
                                    : (current + direction + count) % count;
    setActiveAction(m_itemActions.at(next));
}

void QlipperHistoryMenu::removeHighlighted()
{
    QAction *a = activeAction();
    if (!a)
        return;
    const QVariant v = a->data();
    if (!v.canConvert<QModelIndex>())
        return;
    const QModelIndex idx = qvariant_cast<QModelIndex>(v);
    if (!idx.isValid())
        return;

    const int pos = m_itemActions.indexOf(a);
    m_model->removeRow(idx.row(), idx.parent());
    // rebuild() has already run synchronously, via the model's rowsRemoved signal.
    if (!m_itemActions.isEmpty())
    {
        const int next = qMin(pos, m_itemActions.count() - 1);
        if (m_itemActions.at(next)->isEnabled())
            setActiveAction(m_itemActions.at(next));
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
            highlightStep(1);
            return true;
        case Qt::Key_Up:
            highlightStep(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        {
            QAction *a = activeAction();
            if ((!a || !a->isEnabled()) && !m_itemActions.isEmpty() && m_itemActions.first()->isEnabled())
                a = m_itemActions.first();
            if (a && a->isEnabled())
            {
                const QVariant v = a->data();
                if (v.canConvert<QModelIndex>())
                    activateIndex(qvariant_cast<QModelIndex>(v));
            }
            return true;
        }
        case Qt::Key_Delete:
            // Only steal Delete when an entry is actually highlighted (i.e. the
            // user has navigated with arrow keys); otherwise let the line edit
            // handle it as ordinary forward-delete while typing a filter.
            if (QAction *a = activeAction(); a && a->isEnabled() && m_itemActions.contains(a))
            {
                removeHighlighted();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QMenu::eventFilter(watched, event);
}
