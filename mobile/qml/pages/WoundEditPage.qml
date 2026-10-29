// Wunde anlegen oder bearbeiten (Wundanamnese nach dem Expertenstandard chronische Wunden)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "woundEditPage"
    property var personId: 0
    property var woundId: 0
    property bool admission: false
    signal closed(string message, var id)
    readonly property var c: app.woundChoices()
    property var w: app.woundData(personId, woundId, admission)
    readonly property var gradeList: c.grades[kind.currentIndex] || []
    background: Rectangle { color: Theme.paper }

    function save() {
        if (!since.valid || !admDate.valid || !closedAt.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        const v = {
            id: page.woundId, label: label.text, kind: kind.currentIndex, grade: grade.currentIndex - 1,
            diagnosis: diagnosis.text, site: site.text, side: side.currentIndex > 0 ? page.c.sides[side.currentIndex - 1] : "",
            siteDetail: siteDetail.text, since: since.isoDate, origin: origin.text,
            onAdmission: onAdm.checked, admissionDate: admDate.isoDate,
            recurrences: rec.value, recurrenceFree: recFree.text, previousCare: previous.text, physician: physician.text,
            goal: goal.currentIndex, limitations: limits.values, selfManagement: selfMgmt.text, photoConsent: consent.checked,
            status: status.currentIndex, closedAt: closedAt.isoDate, notes: notes.text
        }
        const id = app.saveWound(page.personId, v)
        if (id) page.closed(qsTr("Wunde gespeichert."), id)
        else win.toast(app.lastError, true)
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed("", 0) }
            Label { text: page.woundId ? qsTr("Wunde bearbeiten") : page.admission ? qsTr("Wunde bei Aufnahme") : qsTr("Neue Wunde")
                    font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.woundId; text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
            VButton { objectName: "saveWoundButton"; text: qsTr("Speichern"); kind: "primary"; onClicked: page.save() }
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
            SectionTitle { text: qsTr("Wunde") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: label; objectName: "woundLabel"; label: qsTr("Bezeichnung"); text: page.w.label; placeholder: page.w.labelHint; Layout.fillWidth: true }
                VCombo { id: kind; objectName: "woundKind"; label: qsTr("Wundart"); model: page.c.kinds; currentIndex: page.w.kind; Layout.fillWidth: true }
                VCombo { id: grade; objectName: "woundGrade"; label: page.c.gradeSystems[kind.currentIndex] || qsTr("Klassifikation")
                         enabled: page.gradeList.length > 0; model: ["–"].concat(page.gradeList)
                         currentIndex: page.w.grade + 1; Layout.fillWidth: true }
                VField { id: diagnosis; label: qsTr("Medizinische Diagnose / Grunderkrankung"); text: page.w.diagnosis
                         placeholder: qsTr("z. B. Diabetes mellitus, pAVK"); Layout.fillWidth: true }
            }
            SuggestField { id: site; objectName: "woundSite"; label: qsTr("Lokalisation *"); text: page.w.site; suggestions: page.c.sites; Layout.fillWidth: true }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: side; label: qsTr("Seite"); model: ["–"].concat(page.c.sides); currentIndex: Math.max(0, page.c.sides.indexOf(page.w.side) + 1); Layout.fillWidth: true }
                VField { id: siteDetail; label: qsTr("Genauer"); text: page.w.siteDetail; placeholder: qsTr("z. B. 2 cm oberhalb des Innenknöchels"); Layout.fillWidth: true }
            }

            SectionTitle { text: qsTr("Entstehung und Verlauf") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                DateField { id: since; label: qsTr("Entstanden am (leer = unbekannt)"); isoDate: page.w.since; Layout.fillWidth: true }
                SuggestField { id: origin; label: qsTr("Entstanden in/bei"); text: page.w.origin; suggestions: page.c.origins; Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gap
                CheckBox { id: onAdm; objectName: "woundOnAdmission"; text: qsTr("bei Aufnahme bereits vorhanden"); checked: page.w.onAdmission; font.pixelSize: Theme.fsM }
                DateField { id: admDate; visible: onAdm.checked; label: qsTr("Aufnahme am"); isoDate: page.w.admissionDate; allowEmpty: false; Layout.preferredWidth: 260 }
                Item { Layout.fillWidth: true }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VStepper { id: rec; label: qsTr("Rezidive"); from: 0; to: 99; value: page.w.recurrences }
                VField { id: recFree; label: qsTr("Rezidivfreie Zeit"); text: page.w.recurrenceFree; Layout.fillWidth: true }
            }
            VArea { id: previous; label: qsTr("Bisherige Maßnahmen"); text: page.w.previousCare; minHeight: 90
                    placeholder: qsTr("bisherige Diagnostik und Therapie, Kompression, Druckentlastung …"); Layout.fillWidth: true }
            VField { id: physician; label: qsTr("Behandelt von"); text: page.w.physician; placeholder: qsTr("Ärztin/Arzt, Wundexpertin/Wundexperte, Wundambulanz"); Layout.fillWidth: true }

            SectionTitle { text: qsTr("Einschränkungen, Selbstmanagement, Ziel") }
            ChoiceFlow { id: limits; label: qsTr("Wund- und therapiebedingte Einschränkungen"); choices: page.c.limitations; values: page.w.limitations; Layout.fillWidth: true }
            VArea { id: selfMgmt; label: qsTr("Selbstmanagement"); text: page.w.selfManagement; minHeight: 90
                    placeholder: qsTr("Wissen und Fähigkeiten zum Umgang mit der Wunde, Unterstützung durch Angehörige …"); Layout.fillWidth: true }
            VCombo { id: goal; label: qsTr("Therapieziel"); model: page.c.goals; currentIndex: page.w.goal; Layout.fillWidth: true }
            CheckBox { id: consent; objectName: "woundConsent"; text: qsTr("Einwilligung zur Fotodokumentation liegt vor"); checked: page.w.photoConsent; font.pixelSize: Theme.fsM }

            SectionTitle { text: qsTr("Status") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: status; label: qsTr("Status"); model: page.c.statuses; currentIndex: page.w.status; Layout.fillWidth: true }
                DateField { id: closedAt; visible: status.currentIndex > 0; label: qsTr("am"); isoDate: page.w.closedAt; allowEmpty: false; Layout.fillWidth: true }
            }
            VArea { id: notes; label: qsTr("Bemerkung"); text: page.w.notes; minHeight: 80; Layout.fillWidth: true }
            Label { visible: !!page.w.createdText; text: page.w.createdText || ""; color: Theme.muted; font.pixelSize: Theme.fsXs }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsXs
                    text: qsTr("VitaParms ist kein Medizinprodukt – Einschätzung und Therapie gehören in fachliche Hände.") }
        }
    }
    Confirm {
        id: confirmDelete
        title: qsTr("Wunde löschen?")
        message: qsTr("Die Wunde wird mit der gesamten Chronik endgültig gelöscht. Abgeheilte Wunden besser als „abgeheilt“ kennzeichnen – dann bleibt die Dokumentation erhalten.")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deleteWound(page.woundId)) page.closed(qsTr("Wunde gelöscht."), 0); else win.toast(app.lastError, true) }
    }
}
