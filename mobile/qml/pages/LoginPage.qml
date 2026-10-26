// Anmeldung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "loginPage"
    signal done()
    signal setupRequested()
    signal importRequested()
    background: Rectangle { color: Theme.paper }
    property var accounts: app.accounts()

    function tryLogin() {
        if (app.login(user.currentIndex >= 0 ? accounts[user.currentIndex] : "", pw.text)) { pw.text = ""; done() }
        else msg.text = app.lastError
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
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
                Image { source: "qrc:/icons/vitaparms.png"; sourceSize.width: 56; sourceSize.height: 56 }
                Label { text: "VitaParms"; font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink }
            }
            VCombo { id: user; label: qsTr("Konto"); model: page.accounts; Layout.fillWidth: true
                     Component.onCompleted: currentIndex = page.accounts.length ? 0 : -1 }
            VField { id: pw; label: qsTr("Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true
                     onAccepted: page.tryLogin(); Component.onCompleted: field.forceActiveFocus() }
            Label { id: msg; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text !== "" }
            VButton { text: qsTr("Anmelden"); kind: "primary"; Layout.fillWidth: true; enabled: pw.text !== ""; onClicked: page.tryLogin() }
            VButton { objectName: "importButton"; text: qsTr("Andere Datenbank importieren…"); kind: "flat"; Layout.alignment: Qt.AlignHCenter
                      onClicked: page.importRequested() }
            VButton { objectName: "usbSyncButton"; text: qsTr("USB-Abgleich mit dem Computer"); kind: "flat"; Layout.alignment: Qt.AlignHCenter
                      onClicked: win.openUsbSync() }
            VButton { objectName: "quitButton"; text: qsTr("Beenden"); kind: "flat"; Layout.alignment: Qt.AlignHCenter
                      onClicked: win.quitApp() }
            VButton { objectName: "gettingStartedButton"; text: qsTr("Erste Schritte"); kind: "flat"; Layout.alignment: Qt.AlignHCenter
                      onClicked: if (!app.openGettingStarted()) win.toast(app.lastError, true) }
        }
    }
    Label {
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 20
        text: qsTr("VitaParms %1 – persönliches Gesundheitstagebuch, kein Medizinprodukt").arg(app.version)
        color: Theme.muted; font.pixelSize: Theme.fsXs
    }
}
