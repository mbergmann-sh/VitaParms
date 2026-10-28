// Ersteinrichtung: Datenbank vom Computer übernehmen (Abgleich) oder neu anlegen
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    signal done()
    property int mode: 0          // 0 Auswahl, 1 vom Computer, 2 neu, 3 Datei
    property bool fromMenu: false // aus Anmeldung/Verwaltung geöffnet: „Zurück“ schließt die Seite
    property string dbFile: ""
    property string keysFile: ""
    property string recovery: ""
    background: Rectangle { color: Theme.paper }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 96
        clip: true
        ColumnLayout {
            id: col
            width: Math.min(680, page.width - 48)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 48
            spacing: Theme.gap

            Label { text: qsTr("VitaParms einrichten"); font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink }

            // ---------- Auswahl
            ColumnLayout {
                visible: page.mode === 0
                spacing: Theme.gap
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; font.pixelSize: Theme.fsL
                        text: qsTr("Wie soll dieses Tablet beginnen?") }
                VButton { objectName: "gettingStartedButton"; text: qsTr("Erste Schritte – Kurzanleitung öffnen"); kind: "flat"
                          onClicked: if (!app.openGettingStarted()) win.toast(app.lastError, true) }
                Repeater {
                    model: [
                        { title: qsTr("Daten vom Computer übernehmen"), text: qsTr("VitaParms am Computer gleicht die verschlüsselte Datenbank über Ihre Website ab. Sie brauchen die Adresse der Website und den Upload-Schlüssel (am Computer unter Extras → Website…)."), m: 1 },
                        { title: qsTr("Über das USB-Kabel vom Computer übernehmen"), text: qsTr("Tablet per USB-Kabel an den Computer anschließen; VitaParms am Computer überträgt seine Datenbank direkt (Datei → Mit Tablet über USB abgleichen…). USB-Debugging muss eingeschaltet sein."), m: 4 },
                        { title: qsTr("Datenbank-Datei übernehmen"), text: qsTr("Eine Datei vom Computer übernehmen – ohne Website, z. B. per USB-Kabel oder eigenem Cloud-Speicher: am Computer „Datei → Datenbank exportieren…“ (*.vpdb) oder eine Sicherung (*.sqlite mit .keys-Datei)."), m: 3 },
                        { title: qsTr("Neu beginnen"), text: qsTr("Eine neue, leere Datenbank nur auf diesem Tablet anlegen. Den Abgleich können Sie später einrichten."), m: 2 }
                    ]
                    delegate: AbstractButton {
                        Layout.fillWidth: true
                        padding: 20
                        onClicked: if (modelData.m === 4) win.openUsbSync(); else page.mode = modelData.m
                        background: Rectangle { radius: Theme.radius + 2; color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.ruleStrong }
                        contentItem: ColumnLayout {
                            id: opt
                            spacing: 6
                            Label { text: modelData.title; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.brand }
                            Label { text: modelData.text; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2 }
                        }
                    }
                }
            }

            // ---------- Vom Computer
            ColumnLayout {
                visible: page.mode === 1
                spacing: Theme.gap
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                        text: qsTr("Am Computer: Extras → Website… → „Datenbank mit anderen Geräten abgleichen“ einschalten und einmal abgleichen. Mit „Kopplungsdatei speichern…“ lassen sich Adresse und Schlüssel auch als Datei übertragen.") }
                VField { id: url; label: qsTr("Adresse der Website"); placeholder: "https://vitaparms.example.de"; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
                VField { id: key; label: qsTr("Upload-Schlüssel"); Layout.fillWidth: true
                         inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
                RowLayout {
                    spacing: Theme.gapS
                    VButton { text: qsTr("Kopplungsdatei öffnen…"); onClicked: fileDlg.open() }
                    VButton { text: qsTr("Verbindung prüfen"); enabled: url.text !== "" && key.text !== ""
                              onClicked: status.text = app.serverInfo(url.text, key.text) }
                }
                Label { id: status; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2 }
                RowLayout {
                    spacing: Theme.gapS
                    VButton { text: qsTr("Zurück"); onClicked: if (page.fromMenu) win.popPage(); else page.mode = 0 }
                    VButton {
                        text: qsTr("Datenbank laden"); kind: "primary"; enabled: url.text !== "" && key.text !== ""
                        onClicked: {
                            status.text = qsTr("Wird geladen …")
                            if (app.setupFromServer(url.text, key.text)) { win.toast(qsTr("Datenbank übernommen – bitte anmelden.")); page.done() }
                            else status.text = app.lastError
                        }
                    }
                }
            }

            // ---------- Datei
            ColumnLayout {
                visible: page.mode === 3
                spacing: Theme.gap
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                        text: qsTr("Wählen Sie die Übertragungsdatei (*.vpdb). Bei einer Sicherung (*.sqlite) wählen Sie zusätzlich die zugehörige Schlüsseldatei (*.keys). Eine bereits vorhandene Datenbank auf diesem Tablet bleibt als Kopie erhalten.") }
                RowLayout {
                    spacing: Theme.gapS
                    VButton { text: qsTr("Datenbank-Datei wählen…"); onClicked: dbDlg.open() }
                    Label { text: page.dbFile ? app.fileName(page.dbFile) : qsTr("keine Datei gewählt"); color: Theme.ink2; Layout.fillWidth: true; elide: Text.ElideMiddle }
                }
                RowLayout {
                    visible: page.dbFile !== "" && !app.isDatabaseBundle(page.dbFile)
                    spacing: Theme.gapS
                    VButton { text: qsTr("Schlüsseldatei wählen…"); onClicked: keysDlg.open() }
                    Label { text: page.keysFile ? app.fileName(page.keysFile) : qsTr("keine Schlüsseldatei gewählt"); color: Theme.ink2; Layout.fillWidth: true; elide: Text.ElideMiddle }
                }
                Label { id: fileStatus; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.alert; visible: text !== "" }
                RowLayout {
                    spacing: Theme.gapS
                    VButton { text: qsTr("Zurück"); onClicked: if (page.fromMenu) win.popPage(); else page.mode = 0 }
                    VButton {
                        text: qsTr("Datenbank übernehmen"); kind: "primary"; enabled: page.dbFile !== ""
                        onClicked: {
                            fileStatus.text = ""
                            if (app.importDatabase(page.dbFile, page.keysFile)) { win.toast(qsTr("Datenbank übernommen – bitte anmelden.")); page.done() }
                            else fileStatus.text = app.lastError
                        }
                    }
                }
            }

            // ---------- Neu
            ColumnLayout {
                visible: page.mode === 2 && page.recovery === ""
                spacing: Theme.gap
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                        text: qsTr("Legen Sie das Administrator-Konto an. Mit ihm melden Sie sich an und verwalten weitere Konten.") }
                VField { id: admin; label: qsTr("Name des Kontos"); text: "admin"; Layout.fillWidth: true; inputMethodHints: Qt.ImhNoAutoUppercase }
                VField { id: pw1; label: qsTr("Passwort"); echoMode: TextInput.Password; Layout.fillWidth: true
                         hint: app.passwordProblem(text) }
                VField { id: pw2; label: qsTr("Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true
                         error: text !== "" && text !== pw1.text; hint: error ? qsTr("Die Passwörter stimmen nicht überein.") : "" }
                Label { id: newStatus; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                RowLayout {
                    spacing: Theme.gapS
                    VButton { text: qsTr("Zurück"); onClicked: if (page.fromMenu) win.popPage(); else page.mode = 0 }
                    VButton {
                        text: qsTr("Datenbank anlegen"); kind: "primary"
                        enabled: admin.text.trim() !== "" && pw1.text !== "" && pw1.text === pw2.text && app.passwordProblem(pw1.text) === ""
                        onClicked: {
                            const r = app.createDatabase(admin.text, pw1.text)
                            if (r !== "") page.recovery = r
                            else newStatus.text = app.lastError
                        }
                    }
                }
            }
            ColumnLayout {
                visible: page.recovery !== ""
                spacing: Theme.gap
                Layout.fillWidth: true
                Label { text: qsTr("Wiederherstellungsschlüssel"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                        text: qsTr("Notieren Sie diesen Schlüssel und bewahren Sie ihn sicher auf. Ohne ihn sind die Daten bei vergessenem Passwort verloren.") }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: rc.implicitHeight + 32; radius: Theme.radius
                    color: Theme.warnWash; border.color: Theme.warn
                    Label { id: rc; anchors.centerIn: parent; width: parent.width - 32; text: page.recovery; wrapMode: Text.WrapAnywhere
                            font.family: "monospace"; font.pixelSize: Theme.fsL; color: Theme.ink; horizontalAlignment: Text.AlignHCenter }
                }
                VButton { text: qsTr("Notiert – weiter"); kind: "primary"; onClicked: page.done() }
            }
        }
    }

    FileDialog {
        id: dbDlg
        title: qsTr("Datenbank-Datei wählen")
        nameFilters: [qsTr("VitaParms-Datenbank (*.vpdb *.sqlite *.db)"), qsTr("Alle Dateien (*)")]
        onAccepted: { page.dbFile = selectedFile.toString(); page.keysFile = ""; fileStatus.text = "" }
    }
    FileDialog {
        id: keysDlg
        title: qsTr("Schlüsseldatei wählen")
        nameFilters: [qsTr("Schlüsseldatei (*.keys)"), qsTr("Alle Dateien (*)")]
        onAccepted: page.keysFile = selectedFile.toString()
    }
    FileDialog {
        id: fileDlg
        title: qsTr("Kopplungsdatei öffnen")
        nameFilters: [qsTr("Kopplungsdatei (*.vpcouple *.json)"), qsTr("Alle Dateien (*)")]
        onAccepted: {
            const m = app.readCouplingFile(selectedFile)
            if (m.url) { url.text = m.url; key.text = m.uploadKey; status.text = qsTr("Kopplungsdatei gelesen.") }
            else status.text = app.lastError
        }
    }
}
