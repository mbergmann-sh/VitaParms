// Rückfrage mit zwei Knöpfen
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: d
    property string title: ""
    property string message: ""
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Abbrechen")
    property bool danger: false
    signal accepted()
    signal rejected()
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 48 : 560)
    padding: 24
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: "#80000000" }
    contentItem: ColumnLayout {
        spacing: Theme.gap
        Label { text: d.title; visible: text !== ""; font.family: Theme.family; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Label { text: d.message; font.family: Theme.family; font.pixelSize: Theme.fsM; color: Theme.ink2
                Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.gapS
            VButton { text: d.rejectText; visible: d.rejectText !== ""; onClicked: { d.close(); d.rejected() } }
            VButton { text: d.acceptText; kind: d.danger ? "danger" : "primary"; onClicked: { d.close(); d.accepted() } }
        }
    }
}
