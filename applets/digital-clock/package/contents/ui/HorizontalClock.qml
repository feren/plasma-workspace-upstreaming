/*
 * Copyright 2020 Carson Black <uhhadd@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.10
import QtQuick.Layouts 1.10

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PC3

RowLayout {
    height: digitalClock.height

    NestedLabel {
        id: timeLabel
        preferHeight: true
        exportImplicit: true

        text: digitalClock.timeText

        Layout.fillHeight: true
        Layout.maximumHeight: PlasmaCore.Units.gridUnit * 2
        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
    }
    NestedLabel {
        id: tzLabel
        preferHeight: true
        exportImplicit: true

        text: `(${digitalClock.tzText})`

        Layout.fillHeight: true
        Layout.maximumHeight: PlasmaCore.Units.gridUnit * 2
        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
    }

    NestedLabel {
        id: dateLabel
        preferHeight: true
        exportImplicit: true

        text: digitalClock.dateText

        Layout.fillHeight: true
        Layout.maximumHeight: PlasmaCore.Units.gridUnit * 2
        Layout.alignment: Qt.AlignHCenter | Qt.AlignLeft
    }

    Item { Layout.fillWidth: true }
}