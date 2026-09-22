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

#ifndef QLIPPERHISTORYMENU_H
#define QLIPPERHISTORYMENU_H

#include <QMenu>
#include <QModelIndex>
#include <QAbstractItemModel>

class QlipperModel;
class QLineEdit;

// A QMenu that renders QlipperModel's rows as actions, with a QLineEdit at
// the top that live-filters the list by display text. Emits triggered(QModelIndex)
// with the same signature QMenuView used, so callers don't need to change.
class QlipperHistoryMenu : public QMenu
{
    Q_OBJECT
public:
    explicit QlipperHistoryMenu(QlipperModel *model, QWidget *parent = nullptr);

    // Cap the menu's height to the configured number of visible entries; the
    // rest scroll. Call after popup() when the menu is laid out.
    void applyHeightLimit();

signals:
    void triggered(const QModelIndex &index) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void rebuild();
    void onAboutToShow();
    void onMenuTriggered(QAction *action);

private:
    QAbstractItemModel *m_model;
    QLineEdit *m_search;
    QList<QAction *> m_itemActions;

    void activateIndex(const QModelIndex &index);
    void selectFirst();
    void highlightStep(int direction);
    void removeHighlighted();
};

#endif // QLIPPERHISTORYMENU_H
