// Beschriftetes Texteingabefeld (Beschriftung über dem Feld, große Trefferfläche)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property alias label: lbl.text
    property alias text: input.text
    property alias placeholder: input.placeholderText
    property alias inputMethodHints: input.inputMethodHints
    property alias echoMode: input.echoMode
    property alias validator: input.validator
    property alias field: input
    property string hint: ""
    property bool error: false
    signal accepted()
    spacing: 4
    opacity: enabled ? 1 : 0.5
    Label { id: lbl; visible: text !== ""; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: Theme.touch
        font.family: Theme.family
        font.pixelSize: Theme.fsM
        color: Theme.ink
        placeholderTextColor: Theme.muted
        selectByMouse: true
        leftPadding: 14; rightPadding: 14
        onAccepted: f.accepted()
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surface
            border.width: input.activeFocus ? 2 : 1
            border.color: f.error ? Theme.alert : input.activeFocus ? Theme.brand : Theme.ruleStrong
        }
    }
    Label { visible: f.hint !== ""; text: f.hint; wrapMode: Text.WordWrap; Layout.fillWidth: true
            font.family: Theme.family; font.pixelSize: Theme.fsXs; color: f.error ? Theme.alert : Theme.muted }
}
