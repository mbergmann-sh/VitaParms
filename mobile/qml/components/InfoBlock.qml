// Abschnitt mit Bezeichnung/Wert-Zeilen (leere Werte entfallen)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: b
    property string title: ""
    property var rows: []
    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    spacing: 6
    SectionTitle { text: b.title }
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
    Repeater {
        model: b.rows.filter(r => r[1] !== undefined && r[1] !== null && String(r[1]).trim() !== "")
        delegate: RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gap
            Label { visible: modelData[0] !== ""; text: modelData[0]; color: Theme.muted; Layout.preferredWidth: 210; Layout.alignment: Qt.AlignTop; wrapMode: Text.WordWrap }
            Label { text: modelData[1]; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }
}
