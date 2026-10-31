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
                Image { source: app.light ? "qrc:/icons/vitaparms-light.png" : "qrc:/icons/vitaparms.png"; sourceSize.width: 56; sourceSize.height: 56 }
                Label { text: app.appName; font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink }
            }
            VCombo { id: user; label: qsTr("Konto"); model: page.accounts; Layout.fillWidth: true
                     Component.onCompleted: currentIndex = page.accounts.length ? 0 : -1 }
            VField { id: pw; label: qsTr("Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true
                     onAccepted: page.tryLogin(); Component.onCompleted: field.forceActiveFocus() }
            Label { id: msg; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text !== "" }
            VButton { text: qsTr("Anmelden"); kind: "primary"; Layout.fillWidth: true; enabled: pw.text !== ""; onClicked: page.tryLogin() }
            VButton { objectName: "forgotButton"; text: qsTr("Passwort vergessen?"); kind: "flat"; Layout.alignment: Qt.AlignHCenter
                      onClicked: recover.open() }
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
    // Wiederherstellung: mit dem Wiederherstellungsschlüssel ein Administrator-Konto neu setzen bzw. anlegen
    Popup {
        id: recover
        objectName: "recoverPopup"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(620, page.width - 32)
        padding: 24
        onOpened: { rname.text = user.currentIndex >= 0 ? page.accounts[user.currentIndex] : ""; rmsg.text = "" }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gapS
            Label { text: qsTr("Passwort vergessen?"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Mit dem Wiederherstellungsschlüssel (beim Einrichten ausgedruckt bzw. am Computer unter Extras → Anmeldekonten) erhält ein Konto ein neues Passwort und Administratorrechte. Gibt es das Konto nicht, wird es angelegt.") }
            VField { id: rcode; objectName: "recoveryCodeField"; label: qsTr("Wiederherstellungsschlüssel"); Layout.fillWidth: true
                     inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
            VField { id: rname; objectName: "recoveryName"; label: qsTr("Konto"); Layout.fillWidth: true
                     inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            VField { id: rpw; objectName: "recoveryPassword"; label: qsTr("Neues Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true }
            VField { id: rpw2; objectName: "recoveryPassword2"; label: qsTr("Neues Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true }
            Label { id: rmsg; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap; visible: text !== "" }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: recover.close() }
                VButton { objectName: "recoverButton"; text: qsTr("Entsperren"); kind: "primary"; enabled: rcode.text !== "" && rpw.text !== ""
                          onClicked: { if (app.recoverAccess(rcode.text, rname.text, rpw.text, rpw2.text)) { rcode.text = ""; rpw.text = ""; rpw2.text = ""; recover.close(); page.done() } else rmsg.text = app.lastError } }
            }
        }
    }

    Label {
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 20
        text: qsTr("%2 %1 – persönliches Gesundheitstagebuch, kein Medizinprodukt").arg(app.version).arg(app.appName)
        color: Theme.muted; font.pixelSize: Theme.fsXs
    }
}
