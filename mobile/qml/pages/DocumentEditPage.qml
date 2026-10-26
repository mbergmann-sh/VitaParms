// Krankenakte: neues Dokument (Fotos oder Datei) bzw. Angaben eines Dokuments bearbeiten
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "documentEditPage"
    property var personId: 0
    property var docId: 0                      // 0 = neues Dokument
    property string startWith: ""              // "photo" oder "file" (neues Dokument)
    signal closed(string message)
    background: Rectangle { color: Theme.paper }

    property var doc: docId ? app.documentInfo(docId) : ({})
    property var draft: ({})
    property string pendingCapture: ""
    property int captureTries: 0
    readonly property bool isNew: !docId
    readonly property bool previews: app.pdfPreviewAvailable()

    function refreshDraft(first) {
        draft = app.draftInfo(personId)
        if (first) {                          // Vorschläge nur beim ersten Inhalt übernehmen
            if (title.text === "" && draft.title) title.text = draft.title
            if (draft.date) date.isoDate = draft.date
            if (cat.text === "" && draft.category) cat.text = draft.category
        }
    }
    function closeLater() { Qt.callLater(function() { page.closed("") }) }
    function capture() {
        const p = app.startCapture()
        if (!p) { win.toast(app.lastError, true); if (!draft.kind) closeLater(); return }
        pendingCapture = p
        captureTries = 0
    }
    function checkCapture() {
        if (pendingCapture === "") return
        const st = app.captureState(pendingCapture)
        if (st === "pending") { if (++captureTries < 20) captureTimer.restart(); return }
        const p = pendingCapture
        pendingCapture = ""
        if (st === "ok") {
            const first = !draft.kind
            if (app.draftAddImage(p)) refreshDraft(first); else win.toast(app.lastError, true)
        } else if (st === "error") {
            win.toast(qsTr("Auf diesem Gerät ist keine Kamera-App verfügbar."), true)
        }
        if (!draft.kind) closeLater()             // nichts aufgenommen
    }
    Timer { id: captureTimer; interval: 400; onTriggered: page.checkCapture() }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && page.pendingCapture !== "") captureTimer.restart() }
    }
    function addFiles(urls) {
        const first = !draft.kind
        const r = app.draftAddFiles(urls)
        if (!r.ok) win.toast(app.lastError, true)
        refreshDraft(first)
        if (!draft.kind) closeLater()
    }

    function save() {
        if (!date.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        const meta = { id: page.docId, title: title.text, date: date.isoDate, category: cat.text, issuer: issuer.text, notes: notes.text }
        if (page.isNew) {
            const id = app.saveDraft(page.personId, meta)
            if (id) page.closed(qsTr("Dokument in die Akte übernommen."))
            else win.toast(app.lastError, true)
        } else {
            if (app.saveDocument(meta)) page.closed(qsTr("Angaben gespeichert."))
            else win.toast(app.lastError, true)
        }
    }
    Component.onCompleted: {
        if (!isNew) return
        app.draftClear()
        refreshDraft(false)
        if (startWith === "photo") capture()
        else if (startWith === "file") fileDlg.open()
    }
    Component.onDestruction: if (isNew) app.draftClear()

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed("") }
            Label { text: page.isNew ? qsTr("Neues Dokument") : qsTr("Angaben bearbeiten"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { objectName: "docSaveButton"; text: page.isNew ? qsTr("In die Akte") : qsTr("Speichern"); kind: "primary"
                      enabled: !page.isNew || !!page.draft.kind; onClicked: page.save() }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(1000, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            // Seiten des neuen Dokuments
            SectionTitle { visible: page.isNew; text: page.draft.kind === "pdf" ? qsTr("PDF-Datei: %1").arg(page.draft.fileName || "")
                                                                               : qsTr("Seiten (%1)").arg(page.draft.pages || 0) }
            Flow {
                visible: page.isNew && !!page.draft.kind
                Layout.fillWidth: true
                spacing: Theme.gap
                Repeater {
                    model: page.draft.kind === "pdf" ? (page.previews ? Math.min(page.draft.pages || 0, 6) : 0) : (page.draft.pages || 0)
                    delegate: ColumnLayout {
                        spacing: 4
                        Rectangle {
                            implicitWidth: 150; implicitHeight: 200
                            color: Theme.surface; border.color: Theme.rule; radius: 4
                            Image {
                                anchors.fill: parent; anchors.margins: 2
                                source: "image://docpage/draft/" + index + "?r=" + page.draft.revision
                                sourceSize.width: 300
                                fillMode: Image.PreserveAspectFit
                                cache: false
                            }
                            Label { anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 6
                                    text: String(index + 1); color: Theme.muted; font.pixelSize: Theme.fsXs }
                        }
                        RowLayout {
                            visible: page.draft.kind === "images"
                            spacing: 4
                            VButton { text: "↻"; implicitWidth: 72; onClicked: { app.draftRotatePage(index); page.refreshDraft(false) } }
                            VButton { text: "✕"; implicitWidth: 72; kind: "flat"
                                      onClicked: { app.draftRemovePage(index); page.refreshDraft(false) } }
                        }
                    }
                }
            }
            Label { visible: page.isNew && page.draft.kind === "pdf" && !page.previews; color: Theme.ink2
                    text: qsTr("%1 Seite(n)").arg(page.draft.pages || 0) }
            Flow {
                visible: page.isNew && page.draft.kind !== "pdf"
                Layout.fillWidth: true
                spacing: Theme.gapS
                VButton { text: page.draft.kind ? qsTr("Weitere Seite fotografieren") : qsTr("Foto aufnehmen"); iconText: "+"; onClicked: page.capture() }
                VButton { text: page.draft.kind ? qsTr("Weitere Bilder wählen") : qsTr("Datei wählen"); onClicked: fileDlg.open() }
            }
            Label { visible: page.isNew && page.draft.kind === "images"; Layout.fillWidth: true; wrapMode: Text.WordWrap
                    color: Theme.muted; font.pixelSize: Theme.fsS
                    text: qsTr("Mehrere Seiten (z. B. eines Arztbriefs) werden zu einem PDF zusammengefasst. Tipp: Dokument flach auf einen dunklen Untergrund legen, von oben und ohne Blitz fotografieren.") }

            // Angaben (wie am Computer)
            SectionTitle { text: qsTr("Angaben") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: title; objectName: "docTitle"; label: qsTr("Titel *"); text: page.doc.title || ""; Layout.fillWidth: true
                         placeholder: qsTr("z. B. Arztbrief Kardiologie, Blutbild") }
                DateField { id: date; label: qsTr("Datum des Dokuments"); isoDate: page.doc.date || ""; Layout.fillWidth: true }
                VField { id: cat; objectName: "docCategory"; label: qsTr("Art"); text: page.doc.category || ""; Layout.fillWidth: true }
                VField { id: issuer; label: qsTr("Von (Praxis, Klinik, Labor)"); text: page.doc.issuer || ""; Layout.fillWidth: true }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: app.documentCategories(page.personId)
                    VChip { text: modelData; checkable: false; checked: cat.text === modelData; onClicked: cat.text = modelData }
                }
            }
            Flow {
                visible: issuers.count > 0
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    id: issuers
                    model: app.documentIssuers(page.personId)
                    VChip { text: modelData; checkable: false; checked: issuer.text === modelData; onClicked: issuer.text = modelData }
                }
            }
            VArea { id: notes; label: qsTr("Notizen"); text: page.doc.notes || ""; Layout.fillWidth: true; minHeight: 90 }
            Label { visible: !page.isNew; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                    text: qsTr("Datei: %1 · %2 · aufgenommen %3").arg(page.doc.fileName || "").arg(page.doc.size || "").arg(page.doc.added || "") }
        }
    }

    FileDialog {
        id: fileDlg
        title: qsTr("Dokument wählen")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("PDF und Bilder (*.pdf *.jpg *.jpeg *.png *.heic *.webp)"), qsTr("Alle Dateien (*)")]
        onAccepted: page.addFiles(selectedFiles)
        onRejected: if (!page.draft.kind) page.closeLater()
    }
}
