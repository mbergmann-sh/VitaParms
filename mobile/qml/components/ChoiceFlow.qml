// Mehrfachauswahl als Chips (z. B. Wundrand, Infektionszeichen); Werte als Liste der Texte
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property string label: ""
    property var choices: []
    property var values: []
    spacing: 4
    function toggle(s) {
        const v = f.values.slice()
        const i = v.indexOf(s)
        if (i >= 0) v.splice(i, 1); else v.push(s)
        f.values = v
    }
    Label { visible: f.label !== ""; text: f.label; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    Flow {
        Layout.fillWidth: true
        Layout.preferredWidth: 200          // Breite vom Layout, nicht aus den Chips (Layout-Schleife)
        Layout.preferredHeight: implicitHeight
        spacing: 6
        Repeater {
            model: f.choices
            VChip { text: modelData; checkable: false; checked: f.values.indexOf(modelData) >= 0; onClicked: f.toggle(modelData)
                    font.pixelSize: Theme.fsS }
        }
    }
}
