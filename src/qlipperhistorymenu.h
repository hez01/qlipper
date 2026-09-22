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
class QListWidget;
class QListWidgetItem;

// A QMenu shell hosting a search box and a QListWidget that renders
// QlipperModel's rows. The list is used (instead of plain menu actions) so the
// history scrolls inside a fixed-height viewport with a normal scrollbar,
// rather than QMenu's own scroll which repositions the whole popup. Emits
// triggered(QModelIndex) so callers don't need to change.
class QlipperHistoryMenu : public QMenu
{
    Q_OBJECT
public:
    explicit QlipperHistoryMenu(QlipperModel *model, QWidget *parent = nullptr);

    // Size the list viewport to the configured number of visible entries; the
    // rest are reachable via the scrollbar. Call after popup() when the widget
    // has been laid out (row heights are only known then).
    void applyHeightLimit();

signals:
    void triggered(const QModelIndex &index) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void rebuild();
    void onAboutToShow();

private:
    QAbstractItemModel *m_model;
    QLineEdit *m_search;
    QListWidget *m_list;
    // Height of everything above the list (search box + separator + menu
    // frame) and the menu's horizontal frame, measured once when first shown;
    // -1 until then.
    int m_chrome = -1;
    int m_hframe = -1;

    void selectFirst();
    void moveCurrent(int direction);
    void activateCurrent();
    void removeCurrent();
    int rowPixelHeight() const;
    int contentWidth() const;
};

#endif // QLIPPERHISTORYMENU_H
