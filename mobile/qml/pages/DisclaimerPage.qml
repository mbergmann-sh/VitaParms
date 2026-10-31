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
        Image { source: app.light ? "qrc:/icons/vitaparms-light.png" : "qrc:/icons/vitaparms.png"; sourceSize.width: 88; sourceSize.height: 88; Layout.alignment: Qt.AlignLeft }
        Label { text: qsTr("Willkommen bei %1").arg(app.appName); font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.StyledText
            font.pixelSize: Theme.fsL; color: Theme.ink2; lineHeight: 1.25
            text: qsTr("%1 ist ein <b>persönliches Tagebuch</b> für gesundheitsbezogene Messwerte – <b>kein Medizinprodukt</b>. "
                       + "Es stellt keine Diagnosen und ersetzt keine ärztliche Beratung. Die Verwendung erfolgt auf eigene Gefahr.").arg(app.appName)
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            font.pixelSize: Theme.fsM; color: Theme.muted; lineHeight: 1.25
            text: app.light ? qsTr("Die Daten werden auf diesem Gerät verschlüsselt gespeichert und – wenn Sie möchten – über das USB-Kabel mit VitaParms light am Computer abgeglichen.")
                            : qsTr("Die Daten werden auf diesem Tablet verschlüsselt gespeichert und – falls eingerichtet – verschlüsselt über Ihre eigene Website mit VitaParms am Computer abgeglichen.")
        }
        VButton { text: qsTr("Verstanden"); kind: "primary"; Layout.preferredWidth: 220; onClicked: { app.acceptDisclaimer(); done() } }
    }
}
