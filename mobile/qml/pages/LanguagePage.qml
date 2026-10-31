// Allererster Start: Sprache wählen (zweisprachig, vor dem Haftungshinweis)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "languagePage"
    signal done()
    background: Rectangle { color: Theme.paper }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 32)
        height: col.implicitHeight + 64
        radius: Theme.radius + 4
        color: Theme.surface
        border.color: Theme.rule
        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 32
            spacing: Theme.gap
            RowLayout {
                spacing: 14
                Image { source: app.light ? "qrc:/icons/vitaparms-light.png" : "qrc:/icons/vitaparms.png"; sourceSize.width: 56; sourceSize.height: 56 }
                Label { text: app.appName; font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; font.pixelSize: Theme.fsL
                    text: "Willkommen! Bitte wählen Sie die Sprache.\nWelcome! Please choose your language." }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                VButton { objectName: "langDe"; text: "Deutsch"; kind: "primary"; Layout.fillWidth: true
                          onClicked: { app.setLanguage("de"); page.done() } }
                VButton { objectName: "langEn"; text: "English"; kind: "primary"; Layout.fillWidth: true
                          onClicked: { app.setLanguage("en"); page.done() } }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                    text: "Später änderbar unter Einstellungen.\nCan be changed later under Settings." }
        }
    }
}
