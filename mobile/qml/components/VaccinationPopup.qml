// Impfung eintragen oder ändern (Impfpass im Stammblatt): Datum, Impfung gegen, Impfstoff,
// Chargennummer, Arzt/Praxis, Bemerkung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: d
    objectName: "vaccinationPopup"
    property var entry: ({})
    property int index: -1                  // -1 = neu
    property var againstList: []            // Vorschläge
    property var productList: []
    property var doctorList: []
    signal saved(var entry, int index)
    signal removed(int index)
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, parent ? parent.width - 32 : 760)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : 800)      // sonst blättert der Inhalt
    padding: 20
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: "#80000000" }

    // Vorschläge passend zur Eingabe (höchstens „max“ Stück, damit die Liste übersichtlich bleibt)
    function filtered(list, text, max) {
        const t = (text || "").trim().toLowerCase()
        const l = (list || []).filter(s => t === "" || s.toLowerCase().indexOf(t) >= 0 || s === text)
        return l.slice(0, max)
    }
    function pad(n) { return (n < 10 ? "0" : "") + n }
    function todayIso() { const t = new Date(); return t.getFullYear() + "-" + pad(t.getMonth() + 1) + "-" + pad(t.getDate()) }

    function openWith(e, i) {
        entry = e || ({})
        index = i
        date.isoDate = entry.date || (i < 0 ? todayIso() : "")
        against.text = entry.against || ""; product.text = entry.product || ""; batch.text = entry.batch || ""
        doctor.text = entry.doctor || ""; note.text = entry.note || ""
        error.text = ""
        open()
        against.field.forceActiveFocus()
    }
    function accept() {
        if (!date.valid) { error.text = qsTr("Bitte das Datum als TT.MM.JJJJ eingeben."); return }
        if (against.text.trim() === "" && product.text.trim() === "") {
            error.text = qsTr("Bitte angeben, wogegen geimpft wurde, oder den Impfstoff eintragen."); return
        }
        const iso = date.isoDate || ""
        const e = { uid: entry.uid || "", date: iso, against: against.text.trim(), product: product.text.trim(),
                    batch: batch.text.trim(), doctor: doctor.text.trim(), note: note.text.trim() }
        e.dateText = iso !== "" ? date.toText(iso) : qsTr("ohne Datum")
        e.summary = [e.against, e.product, e.batch ? qsTr("Ch.-B. %1").arg(e.batch) : ""].filter(s => s).join(", ")
        close()
        saved(e, index)
    }

    contentItem: Flickable {
        implicitHeight: form.implicitHeight
        contentHeight: form.implicitHeight + 260       // Platz für die Bildschirmtastatur
        clip: true
        ColumnLayout {
            id: form
            width: parent.width
            spacing: Theme.gapS
            Label { text: d.index < 0 ? qsTr("Impfung eintragen") : qsTr("Impfung ändern"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            DateField { id: date; objectName: "vaccDate"; label: qsTr("Datum der Impfung"); Layout.preferredWidth: 220; Layout.maximumWidth: 240 }
            SuggestField { id: against; objectName: "vaccAgainst"; label: qsTr("Impfung gegen"); Layout.fillWidth: true; Layout.preferredWidth: 10
                           placeholder: qsTr("z. B. Tetanus, Diphtherie, Pertussis")
                           suggestions: d.filtered(d.againstList, text, 10)
                           onTextChanged: error.text = "" }
            GridLayout {
                Layout.fillWidth: true
                columns: d.width > 600 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                SuggestField { id: product; objectName: "vaccProduct"; label: qsTr("Impfstoff (Präparat)"); Layout.fillWidth: true; Layout.preferredWidth: 10
                               Layout.alignment: Qt.AlignTop; suggestions: d.filtered(d.productList, text, 6)
                               onTextChanged: error.text = "" }
                VField { id: batch; objectName: "vaccBatch"; label: qsTr("Chargennummer (Ch.-B.)"); Layout.fillWidth: true; Layout.preferredWidth: 10
                         Layout.alignment: Qt.AlignTop; field.inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            }
            SuggestField { id: doctor; objectName: "vaccDoctor"; label: qsTr("Arzt / Praxis"); Layout.fillWidth: true; Layout.preferredWidth: 10
                           suggestions: d.filtered(d.doctorList, text, 6) }
            VField { id: note; objectName: "vaccNote"; label: qsTr("Bemerkung"); placeholder: qsTr("z. B. 2. Dosis, Auffrischung"); Layout.fillWidth: true }
            Label { id: error; objectName: "vaccError"; visible: text !== ""; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                layoutDirection: Qt.RightToLeft
                spacing: Theme.gapS
                VButton { objectName: "vaccApply"; text: qsTr("Übernehmen"); kind: "primary"; onClicked: d.accept() }
                VButton { text: qsTr("Abbrechen"); onClicked: d.close() }
                VButton { visible: d.index >= 0; objectName: "vaccRemove"; text: qsTr("Entfernen"); kind: "flat"; onClicked: { d.close(); d.removed(d.index) } }
            }
        }
    }
}
