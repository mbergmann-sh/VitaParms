// Textfeld mit Vorschlägen als Chips: antippen übernimmt den Vorschlag (bei „multi“ werden
// mehrere mit Komma verbunden, erneutes Antippen entfernt ihn wieder). Freitext bleibt möglich.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property alias label: field.label
    property alias text: field.text
    property alias placeholder: field.placeholder
    property alias field: field
    property var suggestions: []
    property bool multi: false
    spacing: 6
    function parts() { return f.text.split(",").map(s => s.trim()).filter(s => s !== "") }
    function has(s) { return multi ? parts().indexOf(s) >= 0 : f.text.trim() === s }
    function toggle(s) {
        if (!multi) { f.text = has(s) ? "" : s; return }
        const p = parts()
        const i = p.indexOf(s)
        if (i >= 0) p.splice(i, 1); else p.push(s)
        f.text = p.join(", ")
    }
    VField { id: field; Layout.fillWidth: true }
    Flow {
        Layout.fillWidth: true
        Layout.preferredWidth: 200          // Breite kommt vom Layout, nicht aus den Chips (sonst Layout-Schleife)
        Layout.preferredHeight: implicitHeight
        spacing: 6
        Repeater {
            model: f.suggestions
            VChip { text: modelData; checkable: false; checked: f.has(modelData); onClicked: f.toggle(modelData)
                    font.pixelSize: Theme.fsS }
        }
    }
}
