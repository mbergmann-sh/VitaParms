// Umschalter in Chip-Form (z. B. Messzeitpunkt, Kurvenbereiche, Merkzeichen)
import QtQuick
import QtQuick.Controls
import ".."

AbstractButton {
    id: c
    checkable: true
    property var colors: []
    property string sub: ""
    property color dotColor: "transparent"       // farbiger Punkt vor dem Text (z. B. Priorität)
    implicitHeight: Math.max(Theme.touch - 6, row.implicitHeight + 12)
    implicitWidth: row.implicitWidth + 28
    // höchstens so breit wie der umgebende Bereich (Smartphone); lange Texte werden dann gekürzt
    readonly property real textRoom: Math.max(60, (c.parent ? c.parent.width : 4000) - 28 - row.spacing * 2
                                                  - (c.dotColor.a > 0 ? 20 : 0) - c.colors.length * 18)
    font.family: Theme.family
    font.pixelSize: Theme.fsM
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Rectangle { visible: c.dotColor.a > 0; width: 12; height: 12; radius: 6; color: c.dotColor; anchors.verticalCenter: parent.verticalCenter }
            Repeater {
                model: c.colors
                Rectangle { width: 10; height: 10; radius: 5; color: Theme.series(modelData); anchors.verticalCenter: parent.verticalCenter }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { text: c.text; font.family: c.font.family; font.pixelSize: c.font.pixelSize; color: c.checked ? Theme.ink : Theme.ink2; font.bold: c.checked
                       width: Math.min(implicitWidth, c.textRoom); elide: Text.ElideRight }
                Text { visible: c.sub !== ""; text: c.sub; font.family: Theme.family; font.pixelSize: Theme.fsXs; color: Theme.muted
                       width: Math.min(implicitWidth, c.textRoom); elide: Text.ElideRight }
            }
        }
    }
    background: Rectangle {
        radius: height / 2
        color: c.checked ? Theme.brandWash : Theme.surface
        border.width: c.checked ? 2 : 1
        border.color: c.checked ? Theme.brand : Theme.ruleStrong
    }
}
