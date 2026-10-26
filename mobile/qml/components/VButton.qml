// Schaltfläche: "primary" (gefüllt), "ghost" (umrandet), "danger", "flat" (nur Text)
import QtQuick
import QtQuick.Controls
import ".."

Button {
    id: b
    property string kind: "ghost"
    property string iconText: ""
    implicitHeight: Theme.touch
    leftPadding: 18; rightPadding: 18
    font.family: Theme.family
    font.pixelSize: Theme.fsM
    font.bold: kind === "primary" || kind === "danger"
    contentItem: Row {
        spacing: 8
        anchors.centerIn: parent
        Text {
            visible: b.iconText !== ""
            text: b.iconText
            font.pixelSize: b.font.pixelSize + 2
            color: label.color
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: label
            text: b.text
            font: b.font
            color: !b.enabled ? Theme.muted
                 : b.kind === "primary" ? Theme.brandInk
                 : b.kind === "danger" ? "#FFFFFF"
                 : b.kind === "flat" ? Theme.brand : Theme.ink
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: b.kind === "primary" ? (b.enabled ? (b.down ? Qt.darker(Theme.brand, 1.15) : Theme.brand) : Theme.rule)
             : b.kind === "danger" ? (b.down ? Qt.darker(Theme.alert, 1.15) : Theme.alert)
             : b.kind === "flat" ? (b.down ? Theme.brandWash : "transparent")
             : (b.down || b.checked ? Theme.brandWash : Theme.surface)
        border.width: b.kind === "ghost" ? 1 : 0
        border.color: b.checked ? Theme.brand : Theme.ruleStrong
    }
}
