// Verwaltung: eigenes Passwort, Anmeldekonten, Wiederherstellungsschlüssel, Sicherung, Demo
// (wie am Computer: Konten, Schlüssel und Sicherung nur für Administratoren)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "adminPage"
    signal closed()
    background: Rectangle { color: Theme.paper }
    property var accounts: app.accountList()
    property string code: ""
    Connections { target: app; function onAccountsChanged() { page.accounts = app.accountList() } }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            VButton { text: qsTr("Zurück"); kind: "flat"; onClicked: page.closed() }
            Label { text: qsTr("Verwaltung"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            Item { width: 100 }
        }
    }

    Flickable {
        id: flick
        objectName: "adminFlick"
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(900, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            // ---- eigenes Konto
            SectionTitle { text: qsTr("Ihr Konto") }
            Label { text: qsTr("Angemeldet als %1 (%2)").arg(app.userName).arg(app.isAdmin ? qsTr("Administrator") : qsTr("Anwender"))
                    color: Theme.ink2; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 900 ? 3 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VField { id: oldPw; objectName: "oldPassword"; label: qsTr("Bisheriges Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true }
                VField { id: newPw; objectName: "newPassword"; label: qsTr("Neues Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true }
                VField { id: newPw2; objectName: "newPassword2"; label: qsTr("Neues Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true }
            }
            Label { visible: newPw.text !== ""; text: app.passwordProblem(newPw.text) || qsTr("Passwort in Ordnung.")
                    color: app.passwordProblem(newPw.text) ? Theme.warn : Theme.ok; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            VButton { objectName: "changePasswordButton"; text: qsTr("Passwort ändern"); kind: "primary"
                      enabled: oldPw.text !== "" && newPw.text !== ""
                      onClicked: { if (app.changeOwnPassword(oldPw.text, newPw.text, newPw2.text)) { oldPw.text = ""; newPw.text = ""; newPw2.text = ""; win.toast(qsTr("Passwort geändert.")) } else win.toast(app.lastError, true) } }

            // ---- Konten (Administrator)
            SectionTitle { visible: app.isAdmin && !app.light; text: qsTr("Anmeldekonten") }
            Label { visible: app.isAdmin && !app.light; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Administratoren verwalten Konten, Sicherungen und die Website; Anwender erfassen und sehen Daten. Die Konten gelten für diese Datenbank – nach dem nächsten Abgleich bzw. Kopieren der Datenbank auch am Computer (Schlüsseldatei).") }
            Repeater {
                model: app.isAdmin && !app.light ? page.accounts : []      // VitaParms light: ein Konto, keine Rollen
                delegate: Rectangle {
                    objectName: "accountRow"
                    Layout.fillWidth: true
                    implicitHeight: arow.implicitHeight + 20
                    radius: 8; color: Theme.surface; border.color: Theme.rule
                    RowLayout {
                        id: arow
                        x: 14; y: 10; width: parent.width - 28
                        spacing: Theme.gapS
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 0
                            Label { text: modelData.name + (modelData.self ? "  " + qsTr("(Sie)") : ""); font.bold: true; color: Theme.ink }
                            Label { text: modelData.roleText + (modelData.created ? " · " + qsTr("angelegt %1").arg(modelData.created) : ""); color: Theme.muted; font.pixelSize: Theme.fsS }
                        }
                        VChip { text: qsTr("Administrator"); checkable: false; checked: modelData.admin
                                onClicked: if (!app.setAccountAdmin(modelData.name, !modelData.admin)) win.toast(app.lastError, true) }
                        VButton { text: qsTr("Passwort neu setzen"); kind: "flat"; onClicked: resetPopup.openFor(modelData.name) }
                        VButton { visible: !modelData.self; text: qsTr("Löschen"); kind: "flat"
                                  onClicked: { delConfirm.account = modelData.name; delConfirm.message = qsTr("Das Konto „%1“ löschen? Die Daten bleiben erhalten.").arg(modelData.name); delConfirm.open() } }
                    }
                }
            }
            Rectangle {
                visible: app.isAdmin && !app.light
                Layout.fillWidth: true
                implicitHeight: addCol.implicitHeight + 28
                radius: Theme.radius; color: Theme.brandWash
                ColumnLayout {
                    id: addCol
                    x: 14; y: 14; width: parent.width - 28
                    spacing: Theme.gapS
                    Label { text: qsTr("Neues Konto"); font.bold: true; color: Theme.ink }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.width > 900 ? 3 : 1
                        columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                        VField { id: accName; objectName: "newAccountName"; label: qsTr("Name"); Layout.fillWidth: true
                                 inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
                        VField { id: accPw; label: qsTr("Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true }
                        VField { id: accPw2; label: qsTr("Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true }
                    }
                    VCheck { id: accAdmin; text: qsTr("Administrator"); font.pixelSize: Theme.fsM }
                    VButton { objectName: "addAccountButton"; text: qsTr("Konto anlegen"); iconText: "+"; enabled: accName.text !== "" && accPw.text !== ""
                              onClicked: { if (app.addAccount(accName.text, accPw.text, accPw2.text, accAdmin.checked)) { win.toast(qsTr("Konto „%1“ angelegt.").arg(accName.text)); accName.text = ""; accPw.text = ""; accPw2.text = ""; accAdmin.checked = false } else win.toast(app.lastError, true) } }
                }
            }

            // ---- Wiederherstellungsschlüssel (Administrator)
            SectionTitle { visible: app.isAdmin; text: qsTr("Wiederherstellungsschlüssel") }
            Label { visible: app.isAdmin; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Der Schlüssel öffnet die Datenbank, wenn alle Passwörter vergessen sind (Anmeldung → „Passwort vergessen?“). Er gilt für Computer und Tablet. Bewahren Sie ihn ausgedruckt an einem sicheren Ort auf – nicht auf dem Tablet.") }
            Flow {
                visible: app.isAdmin
                Layout.fillWidth: true; spacing: Theme.gapS
                VButton { objectName: "showRecoveryButton"; text: page.code ? qsTr("Ausblenden") : qsTr("Anzeigen")
                          onClicked: page.code = page.code ? "" : app.recoveryCode() }
                VButton { text: qsTr("Als PDF (zum Ausdrucken)"); kind: "flat"
                          onClicked: { const f = app.recoveryCodePdf(); if (f && app.openFile(f)) return; win.toast(app.lastError || qsTr("Keine App zum Anzeigen von PDF-Dateien gefunden."), true) } }
            }
            Rectangle {
                visible: page.code !== ""
                Layout.fillWidth: true
                implicitHeight: codeLbl.implicitHeight + 32
                radius: Theme.radius; color: Theme.surface; border.color: Theme.warn; border.width: 2
                Label { id: codeLbl; objectName: "recoveryCodeLabel"; x: 16; y: 16; width: parent.width - 32; text: page.code; wrapMode: Text.WrapAnywhere
                        font.family: "monospace"; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; horizontalAlignment: Text.AlignHCenter }
            }

            // ---- Sicherung (Administrator)
            SectionTitle { visible: app.isAdmin; text: qsTr("Sicherung") }
            Label { visible: app.isAdmin; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Die Sicherung ist eine Datei (.vpdb) mit der verschlüsselten Datenbank und den Anmeldekonten, wie „Datenbank exportieren“ am Computer. Über das Teilen-Menü legen Sie sie z. B. in „Dateien“, in der Cloud oder auf dem Computer ab. Ohne Kontopasswort oder Wiederherstellungsschlüssel lässt sie sich nicht öffnen.") }
            Flow {
                visible: app.isAdmin
                Layout.fillWidth: true; spacing: Theme.gapS
                VButton { objectName: "backupButton"; text: qsTr("Sicherung erstellen und ablegen"); kind: "primary"
                          onClicked: if (!app.shareBackup()) win.toast(app.lastError, true) }
                VButton { objectName: "restoreButton"; text: qsTr("Sicherung wiederherstellen …"); kind: "flat"; onClicked: restoreConfirm.open() }
            }

            // ---- Demo
            SectionTitle { text: qsTr("Demo-Datenbank") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: app.demo ? qsTr("Die Demo läuft. „Demo beenden“ (oder Sperren) kehrt zur eigenen Datenbank zurück; dort melden Sie sich wie gewohnt an.")
                                   : qsTr("Die Demo zeigt VitaParms mit der erfundenen Person Erika Mustermann: ein Jahr Messwerte, Medikamente, Termine und Befunde. Sie hat eine eigene Datenbank und wird bei jedem Start neu erzeugt – Ihre Daten bleiben unberührt. Die eigene Datenbank wird dafür gesperrt.") }
            VButton { objectName: "demoButton"; text: app.demo ? qsTr("Demo beenden") : qsTr("Demo starten")
                      onClicked: { if (app.demo) { app.stopDemo(); win.goStart() } else demoConfirm.open() } }
        }
    }

    Confirm { id: delConfirm; property string account: ""; title: qsTr("Konto löschen"); acceptText: qsTr("Löschen"); danger: true
              onAccepted: if (!app.removeAccount(account)) win.toast(app.lastError, true) }
    Confirm { id: restoreConfirm; title: qsTr("Sicherung wiederherstellen")
              message: qsTr("Die Datenbank dieses Tablets wird durch die gewählte Sicherung ersetzt; der bisherige Stand bleibt als Kopie erhalten. Danach melden Sie sich mit einem Konto der Sicherung an. Fortfahren?")
              acceptText: qsTr("Datei wählen …"); onAccepted: win.openImport() }
    Confirm { id: demoConfirm; title: qsTr("Demo starten")
              message: qsTr("Die eigene Datenbank wird gesperrt und die Demo erzeugt (das dauert einige Sekunden). Konto „demo“, Passwort „demo1234“.")
              acceptText: qsTr("Demo starten")
              onAccepted: { busy.open(); Qt.callLater(function() { const ok = app.startDemo(); busy.close(); if (ok) win.goStart(); else win.toast(app.lastError, true) }) } }
    Popup {
        id: busy
        modal: true; closePolicy: Popup.NoAutoClose
        anchors.centerIn: Overlay.overlay
        padding: 24
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        contentItem: RowLayout {
            spacing: Theme.gap
            BusyIndicator { running: busy.opened }
            Label { text: qsTr("Die Demo wird erzeugt …"); color: Theme.ink; font.pixelSize: Theme.fsM }
        }
    }

    // Passwort eines anderen Kontos neu setzen
    Popup {
        id: resetPopup
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, page.width - 32)
        padding: 24
        property string account: ""
        function openFor(n) { account = n; rpw.text = ""; rpw2.text = ""; open() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gap
            Label { text: qsTr("Neues Passwort für „%1“").arg(resetPopup.account); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: qsTr("Teilen Sie es dem Kontoinhaber persönlich mit."); color: Theme.ink2; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            VField { id: rpw; label: qsTr("Neues Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true }
            VField { id: rpw2; label: qsTr("Neues Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: resetPopup.close() }
                VButton { text: qsTr("Übernehmen"); kind: "primary"
                          onClicked: { if (app.resetAccountPassword(resetPopup.account, rpw.text, rpw2.text)) { resetPopup.close(); win.toast(qsTr("Passwort neu gesetzt.")) } else win.toast(app.lastError, true) } }
            }
        }
    }
}
