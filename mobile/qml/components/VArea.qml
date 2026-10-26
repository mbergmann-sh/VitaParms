// Mehrzeiliges Textfeld mit Beschriftung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property alias label: lbl.text
    property alias text: area.text
    property alias placeholder: area.placeholderText
    property int minHeight: 110
    spacing: 4
    opacity: enabled ? 1 : 0.5
    Label { id: lbl; visible: text !== ""; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    TextArea {
        id: area
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(f.minHeight, contentHeight + 28)
        wrapMode: TextEdit.Wrap
        font.family: Theme.family
        font.pixelSize: Theme.fsM
        color: Theme.ink
        placeholderTextColor: Theme.muted
        leftPadding: 14; rightPadding: 14; topPadding: 12; bottomPadding: 12
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surface
            border.width: area.activeFocus ? 2 : 1
            border.color: area.activeFocus ? Theme.brand : Theme.ruleStrong
        }
    }
}
