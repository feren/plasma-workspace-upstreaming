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
import org.kde.plasma.components 3.0 as PC3

Item {
    id: root

    implicitHeight: exportImplicit ? content.implicitHeight : -1
    implicitWidth: exportImplicit ? content.implicitWidth : -1

    property bool exportImplicit: false
    property bool preferWidth: false
    property bool preferHeight: false
    property alias text: content.text
    property alias horizontalAlignment: content.horizontalAlignment
    property alias verticalAlignment: content.verticalAlignment
    property alias fontSizeMode: content.fontSizeMode
    property alias wrapMode: content.wrapMode

    PC3.Label {
        id: content
        font.pixelSize: root.preferWidth ? root.width * (3/4) :
                        root.preferHeight ? root.height * (3/4) :
                        Math.min(root.height, root.width) * (3/4)

        anchors.fill: parent
    }
}