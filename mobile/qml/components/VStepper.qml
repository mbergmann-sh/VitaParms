// Zahl mit „−“ und „+“ (große Trefferflächen statt eines kleinen Drehfelds)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: s
    property string label: ""
    property int value: 1
    property int from: 1
    property int to: 99
    property string text: String(value)      // Anzeige, z. B. „alle 2 Wochen“
    spacing: 4
    opacity: enabled ? 1 : 0.5
    Label { visible: s.label !== ""; text: s.label; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    RowLayout {
        spacing: 0
        VButton { text: "−"; implicitWidth: Theme.touch + 8; enabled: s.value > s.from; onClicked: s.value = Math.max(s.from, s.value - 1) }
        Label {
            text: s.text
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: Math.max(120, implicitWidth + 24)
            font.family: Theme.family; font.pixelSize: Theme.fsM; color: Theme.ink
        }
        VButton { text: "+"; implicitWidth: Theme.touch + 8; enabled: s.value < s.to; onClicked: s.value = Math.min(s.to, s.value + 1) }
    }
}
