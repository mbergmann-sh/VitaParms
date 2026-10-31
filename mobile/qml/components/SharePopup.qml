// Krankenakte teilen: Dokumente als PDF-Dateien oder als (verschlüsseltes) ZIP-Archiv mit
// Stammblatt und Medikamentenplan – über das Teilen-Menü von Android (E-Mail, Messenger, Cloud …)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: d
    objectName: "sharePopup"
    property var personId: 0
    property var ids: []
    signal shared(string message)
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : 800)
    padding: 22
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: "#80000000" }

    function openFor(pid, list) {
        personId = pid
        ids = list || []
        zip.checked = ids.length !== 1
        pdfs.checked = ids.length === 1
        password.text = ""
        error.text = ""
        open()
    }
    function go() {
        if (pdfs.checked && ids.length === 0) { error.text = qsTr("Bitte zuerst Dokumente auswählen – oder „ZIP-Archiv“ wählen."); return }
        if (zip.checked && password.text !== "" && password.text.length < 8) { error.text = qsTr("Das Passwort sollte mindestens 8 Zeichen lang sein."); return }
        const r = app.shareDocuments(personId, ids, { zip: zip.checked, sheet: sheet.checked, medPlan: medPlan.checked,
                                                      vitals: vitals.checked, password: zip.checked ? password.text : "" })
        if (!r.ok) { error.text = app.lastError; return }
        close()
        shared(zip.checked && password.text !== ""
               ? qsTr("Archiv erstellt. Teilen Sie das Passwort auf anderem Weg mit (z. B. telefonisch).")
               : qsTr("Zum Teilen bereit."))
    }

    contentItem: ColumnLayout {
        spacing: Theme.gapS
        Label { text: qsTr("Akte teilen"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                text: d.ids.length ? qsTr("%1 Dokument(e) ausgewählt.").arg(d.ids.length) : qsTr("Keine Dokumente ausgewählt.") }
        ButtonGroup { id: how }
        RadioButton { id: pdfs; text: qsTr("Als PDF-Datei(en)"); ButtonGroup.group: how; font.pixelSize: Theme.fsM; enabled: d.ids.length > 0 }
        RadioButton { id: zip; objectName: "shareZip"; text: qsTr("Als ZIP-Archiv (mit Inhaltsverzeichnis)"); ButtonGroup.group: how; font.pixelSize: Theme.fsM }
        ColumnLayout {
            visible: zip.checked
            Layout.fillWidth: true
            Layout.leftMargin: 32
            spacing: 2
            VCheck { id: sheet; text: qsTr("Stammblatt"); checked: true; font.pixelSize: Theme.fsM }
            VCheck { id: medPlan; text: qsTr("Medikamentenplan"); checked: true; font.pixelSize: Theme.fsM }
            VCheck { id: vitals; text: qsTr("Verlauf und Kurven (90 Tage)"); checked: false; font.pixelSize: Theme.fsM }
            VField { id: password; objectName: "sharePassword"; Layout.fillWidth: true; label: qsTr("Passwort (empfohlen)")
                     echoMode: TextInput.Password; placeholder: qsTr("mindestens 8 Zeichen")
                     hint: qsTr("AES-256-verschlüsselt; zu öffnen z. B. mit 7-Zip. Das Passwort nicht in dieselbe Nachricht schreiben.") }
        }
        Label { id: error; visible: text !== ""; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS
            Item { Layout.fillWidth: true }
            VButton { text: qsTr("Abbrechen"); onClicked: d.close() }
            VButton { objectName: "shareGo"; text: qsTr("Teilen …"); kind: "primary"; onClicked: d.go() }
        }
    }
}
