// Erster Start: Hinweis "kein Medizinprodukt"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    signal done()
    background: Rectangle { color: Theme.paper }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        spacing: Theme.gapL
        Image { source: "qrc:/icons/vitaparms.png"; sourceSize.width: 88; sourceSize.height: 88; Layout.alignment: Qt.AlignLeft }
        Label { text: qsTr("Willkommen bei VitaParms"); font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.StyledText
            font.pixelSize: Theme.fsL; color: Theme.ink2; lineHeight: 1.25
            text: qsTr("VitaParms ist ein <b>persönliches Tagebuch</b> für gesundheitsbezogene Messwerte – <b>kein Medizinprodukt</b>. "
                       + "Es stellt keine Diagnosen und ersetzt keine ärztliche Beratung. Die Verwendung erfolgt auf eigene Gefahr.")
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            font.pixelSize: Theme.fsM; color: Theme.muted; lineHeight: 1.25
            text: qsTr("Die Daten werden auf diesem Tablet verschlüsselt gespeichert und – falls eingerichtet – verschlüsselt über Ihre eigene Website mit VitaParms am Computer abgeglichen.")
        }
        VButton { text: qsTr("Verstanden"); kind: "primary"; Layout.preferredWidth: 220; onClicked: { app.acceptDisclaimer(); done() } }
    }
}
