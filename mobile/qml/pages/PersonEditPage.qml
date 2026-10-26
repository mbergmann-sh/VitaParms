// Stammblatt bearbeiten bzw. neue Person anlegen
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    property var personId: 0
    signal closed(var id, string message)
    property var d: app.personDetail(personId)
    property var marks: app.careMarks()
    property var selectedMarks: d.disabilityMarks || []
    property var contacts: (d.contactList || []).slice()
    property var advance: (d.advanceDocs || []).map(a => Object.assign({}, a))
    // ohne Neuzuweisung ändern – sonst baut der Repeater die Felder beim Tippen neu auf
    function setAdvance(i, key, value) { page.advance[i][key] = value }
    background: Rectangle { color: Theme.paper }

    function pfBest() { const v = parseInt(pfBestField.text); return isNaN(v) || v < 0 ? 0 : v }
    // biologisches Geschlecht folgt dem Feld „Geschlecht“, solange es nicht von Hand gewählt wurde
    property bool bioTouched: d.bioSexDiffers === true
    Connections {
        target: sex
        function onCurrentIndexChanged() { if (!page.bioTouched && (sex.currentIndex === 1 || sex.currentIndex === 2)) bioSex.currentIndex = sex.currentIndex }
    }
    readonly property string bioSexHint: bioSex.currentIndex === 0
        ? qsTr("Bitte wählen – sonst gibt es keine geschlechtsabhängigen Normwerte (z. B. Peak-Flow-Sollwert).")
        : (bioSex.currentIndex !== sex.currentIndex ? qsTr("Weicht vom Feld „Geschlecht“ ab und wird für Normwerte verwendet.")
                                                    : qsTr("Folgt dem Feld „Geschlecht“. Nur ändern, wenn das biologische Geschlecht abweicht."))
    // Peak Flow: Sollwert und Bezug der Warnhinweise (aktualisiert sich beim Bearbeiten)
    readonly property string pfInfo: app.peakFlowInfo({ id: page.personId, birthDate: birth.isoDate,
                                                        sex: ["", "m", "w", "d"][sex.currentIndex] || "",
                                                        peakFlowBest: pfBest(), bioSex: ["", "m", "w"][bioSex.currentIndex] || "" })
    function save() {
        const m = {
            id: page.personId, lastName: lastName.text, firstName: firstName.text, birthDate: birth.isoDate,
            sex: ["", "m", "w", "d"][sex.currentIndex] || "", street: street.text, zip: zip.text, city: city.text,
            phone: phone.text, mobile: mobile.text, diagnoses: diagnoses.text, notes: notes.text,
            careLevel: care.currentIndex, careReview: care.currentIndex > 0 ? review.isoDate : "",
            disabilityDegree: gdb.value, disabilityMarks: gdb.value > 0 ? page.selectedMarks : [],
            disabilityValid: gdb.value > 0 && !unlimited.checked ? valid.isoDate : "",
            armLeft: armL.currentIndex, armRight: armR.currentIndex, legLeft: legL.currentIndex, legRight: legR.currentIndex,
            extra: extraRep.model.map((e, i) => ({ key: e.key, value: extraRep.itemAt(i).value })),
            contactList: page.contacts, advanceDocs: page.advance,
            peakFlowBest: page.pfBest(), bioSex: ["", "m", "w"][bioSex.currentIndex] || ""
        }
        if (!birth.valid || !review.valid || !valid.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        const id = app.savePerson(m)
        if (id) page.closed(id, qsTr("Stammblatt gespeichert."))
        else win.toast(app.lastError, true)
    }

    ContactPopup {
        id: contactPopup
        onSaved: (c, i) => { const l = page.contacts.slice(); if (i < 0) l.push(c); else l[i] = c; page.contacts = l }
        onRemoved: (i) => { const l = page.contacts.slice(); l.splice(i, 1); page.contacts = l }
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed(0, "") }
            Label { text: page.personId ? qsTr("Stammblatt bearbeiten") : qsTr("Neue Person"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.personId && app.isAdmin; text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
            VButton { text: qsTr("Speichern"); kind: "primary"; onClicked: page.save() }
        }
    }

    Flickable {
        objectName: "personFlick"
        anchors.fill: parent
        contentHeight: col.implicitHeight + 64
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(1000, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            SectionTitle { text: qsTr("Person") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: lastName; label: qsTr("Name *"); text: d.lastName; Layout.fillWidth: true }
                VField { id: firstName; label: qsTr("Vorname"); text: d.firstName; Layout.fillWidth: true }
                DateField { id: birth; label: qsTr("Geburtsdatum"); isoDate: d.birthDate; Layout.fillWidth: true }
                VCombo { id: sex; label: qsTr("Geschlecht"); model: ["—", qsTr("männlich"), qsTr("weiblich"), qsTr("divers")]
                         currentIndex: Math.max(0, ["", "m", "w", "d"].indexOf(d.sex)); Layout.fillWidth: true }
                VField { id: street; label: qsTr("Straße"); text: d.street; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.gapS
                    VField { id: zip; label: qsTr("PLZ"); text: d.zip; Layout.preferredWidth: 120; inputMethodHints: Qt.ImhDigitsOnly }
                    VField { id: city; label: qsTr("Ort"); text: d.city; Layout.fillWidth: true }
                }
                VField { id: phone; label: qsTr("Telefon"); text: d.phone; Layout.fillWidth: true; inputMethodHints: Qt.ImhDialableCharactersOnly }
                VField { id: mobile; label: qsTr("Mobiltelefon"); text: d.mobile; Layout.fillWidth: true; inputMethodHints: Qt.ImhDialableCharactersOnly }
            }

            SectionTitle { text: qsTr("Pflegegrad") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: care; label: qsTr("Pflegegrad"); Layout.fillWidth: true; currentIndex: d.careLevel || 0
                         model: [qsTr("kein Pflegegrad"), qsTr("Pflegegrad 1"), qsTr("Pflegegrad 2"), qsTr("Pflegegrad 3"), qsTr("Pflegegrad 4"), qsTr("Pflegegrad 5")] }
                DateField { id: review; label: qsTr("Nächste Überprüfung"); isoDate: d.careReview; enabled: care.currentIndex > 0
                            opacity: enabled ? 1 : 0.45; Layout.fillWidth: true }
            }

            SectionTitle { text: qsTr("Schwerbehindertenausweis") }
            RowLayout {
                spacing: Theme.gapS
                Label { text: qsTr("Grad der Behinderung"); color: Theme.ink2 }
                VButton { text: "−"; width: Theme.touch; onClicked: gdb.value = Math.max(0, gdb.value - 10) }
                Label { id: gdbLabel; text: gdb.value > 0 ? qsTr("GdB %1").arg(gdb.value) : qsTr("kein Ausweis")
                        font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.preferredWidth: 150; horizontalAlignment: Text.AlignHCenter }
                VButton { text: "+"; width: Theme.touch; onClicked: gdb.value = Math.min(100, gdb.value + 10) }
                QtObject { id: gdb; property int value: d.disabilityDegree || 0 }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                enabled: gdb.value > 0
                opacity: enabled ? 1 : 0.45
                Repeater {
                    model: page.marks
                    VChip {
                        text: modelData.code
                        sub: modelData.meaning.length > 38 ? modelData.meaning.substring(0, 36) + "…" : modelData.meaning
                        checked: page.selectedMarks.indexOf(modelData.code) >= 0
                        onClicked: {
                            const s = page.selectedMarks.slice(), i = s.indexOf(modelData.code)
                            if (i >= 0) s.splice(i, 1); else s.push(modelData.code)
                            page.selectedMarks = page.marks.map(m => m.code).filter(c => s.indexOf(c) >= 0)
                        }
                    }
                }
            }
            RowLayout {
                enabled: gdb.value > 0
                opacity: enabled ? 1 : 0.45
                spacing: Theme.gap
                DateField { id: valid; label: qsTr("Gültig bis"); isoDate: d.disabilityValid; enabled: !unlimited.checked; Layout.preferredWidth: 260 }
                CheckBox { id: unlimited; text: qsTr("unbefristet"); checked: (d.disabilityDegree || 0) > 0 && !d.disabilityValid
                           font.pixelSize: Theme.fsM; Layout.alignment: Qt.AlignBottom }
            }

            SectionTitle { text: qsTr("Kontakte") }
            Label { visible: page.contacts.length === 0; text: qsTr("Noch keine Kontakte – z. B. Krankenkasse, Hausarzt, Fachärzte, Kliniken, Apotheke."); color: Theme.muted
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Repeater {
                model: page.contacts
                delegate: AbstractButton {
                    Layout.fillWidth: true
                    padding: 10; leftPadding: 14
                    onClicked: contactPopup.openWith(modelData, index)
                    background: Rectangle { radius: Theme.radius; color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                    contentItem: ColumnLayout {
                        spacing: 2
                        Label { text: modelData.kindName; color: Theme.muted; font.pixelSize: Theme.fsXs }
                        Label { text: modelData.summary || modelData.name; color: Theme.ink; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
            }
            VButton { objectName: "addContactButton"; text: qsTr("Kontakt hinzufügen"); iconText: "+"
                      onClicked: contactPopup.openWith(null, -1) }

            SectionTitle { text: qsTr("Vorsorge") }
            Repeater {
                model: page.advance
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    CheckBox { id: advPresent; text: modelData.name + (modelData.hasPdf ? " " + qsTr("(PDF in der Krankenakte)") : "")
                               checked: modelData.present === true; font.pixelSize: Theme.fsM
                               onToggled: page.setAdvance(index, "present", checked) }
                    GridLayout {
                        visible: advPresent.checked
                        Layout.fillWidth: true; Layout.leftMargin: 36
                        columns: page.width > 900 ? 2 : 1
                        columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                        VField { label: modelData.personLabel; text: modelData.person; Layout.fillWidth: true; Layout.preferredWidth: 10
                                 field.onTextEdited: page.setAdvance(index, "person", text) }
                        VField { label: qsTr("Telefon"); text: modelData.phone; Layout.fillWidth: true; Layout.preferredWidth: 10
                                 inputMethodHints: Qt.ImhDialableCharactersOnly; field.onTextEdited: page.setAdvance(index, "phone", text) }
                        VField { label: qsTr("Anschrift"); text: modelData.address; Layout.fillWidth: true; Layout.preferredWidth: 10
                                 field.onTextEdited: page.setAdvance(index, "address", text) }
                        DateField { label: qsTr("Gültig bis (leer = unbefristet)"); isoDate: modelData.validUntil || ""; Layout.fillWidth: true; Layout.preferredWidth: 10
                                    onIsoDateChanged: page.setAdvance(index, "validUntil", isoDate) }
                    }
                }
            }
            Label { text: qsTr("Das Dokument selbst (PDF) legen Sie am Computer in der Krankenakte ab."); color: Theme.muted
                    font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

            SectionTitle { text: qsTr("Diagnosen und Bemerkungen") }
            VArea { id: diagnoses; label: qsTr("Diagnosen (eine pro Zeile)"); text: d.diagnoses; Layout.fillWidth: true }
            VArea { id: notes; label: qsTr("Bemerkungen"); text: d.notes; Layout.fillWidth: true; minHeight: 80 }

            SectionTitle { text: qsTr("Weitere Angaben") }
            Repeater {
                id: extraRep
                model: d.extra || []
                delegate: Loader {
                    Layout.fillWidth: true
                    property string value: item ? item.text : modelData.value
                    sourceComponent: modelData.multiline ? areaComp : fieldComp
                    Component { id: areaComp; VArea { label: modelData.label; text: modelData.value; minHeight: 70 } }
                    Component { id: fieldComp; VField { label: modelData.label; text: modelData.value } }
                }
            }

            SectionTitle { text: qsTr("Biologisches Geschlecht") }
            VCombo { id: bioSex; objectName: "bioSexCombo"; label: qsTr("Für Normwerte")
                     model: [qsTr("— bitte wählen —"), qsTr("männlich"), qsTr("weiblich")]
                     currentIndex: Math.max(0, ["", "m", "w"].indexOf(d.bioSex || ""))
                     onActivated: page.bioTouched = true
                     Layout.preferredWidth: 360 }
            Label { text: page.bioSexHint; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

            SectionTitle { text: qsTr("Peak Flow (Asthma, COPD)") }
            VField { id: pfBestField; objectName: "peakFlowBestField"; label: qsTr("Persönlicher Bestwert (l/min)")
                     text: d.peakFlowBest ? String(d.peakFlowBest) : ""; placeholder: qsTr("mit dem Arzt festgelegt")
                     inputMethodHints: Qt.ImhDigitsOnly; Layout.preferredWidth: 360 }
            Label { objectName: "peakFlowInfo"; text: page.pfInfo; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

            SectionTitle { text: qsTr("Fehlende Gliedmaßen (für den BMI)") }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: armL; label: qsTr("Arm links"); model: app.armOptions(); currentIndex: d.armLeft || 0; Layout.fillWidth: true }
                VCombo { id: armR; label: qsTr("Arm rechts"); model: app.armOptions(); currentIndex: d.armRight || 0; Layout.fillWidth: true }
                VCombo { id: legL; label: qsTr("Bein links"); model: app.legOptions(); currentIndex: d.legLeft || 0; Layout.fillWidth: true }
                VCombo { id: legR; label: qsTr("Bein rechts"); model: app.legOptions(); currentIndex: d.legRight || 0; Layout.fillWidth: true }
            }
        }
    }

    Confirm {
        id: confirmDelete
        title: qsTr("Person löschen?")
        message: qsTr("Das Stammblatt mit allen Messwerten, Medikamenten und Dokumenten wird gelöscht. Das lässt sich nicht rückgängig machen.")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deletePerson(page.personId)) page.closed(0, qsTr("Person gelöscht.")); else win.toast(app.lastError, true) }
    }
}
