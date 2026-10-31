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
    signal addWound()
    signal openWound(var id)
    // Wunden bei Aufnahme (Wunddokumentation); neu laden, wenn sich Daten ändern
    property var admissionWounds: { app.dataRevision; return page.personId ? app.woundList(page.personId, true).filter(w => w.onAdmission) : [] }
    property var d: app.personDetail(personId)
    property var marks: app.careMarks()
    property var selectedMarks: d.disabilityMarks || []
    property var contacts: (d.contactList || []).slice()
    property var advance: (d.advanceDocs || []).map(a => Object.assign({}, a))
    // ohne Neuzuweisung ändern – sonst baut der Repeater die Felder beim Tippen neu auf
    function setAdvance(i, key, value) { page.advance[i][key] = value }
    // PDF aus der Krankenakte verknüpfen (sofort gespeichert) – hier bewusst mit Neuaufbau der Liste
    function linkAdvance(i, docId, title) {
        if (!app.linkAdvanceDocument(page.personId, page.advance[i].kind, docId)) { win.toast(app.lastError, true); return }
        const l = page.advance.map(a => Object.assign({}, a))
        l[i].docId = docId; l[i].docTitle = title; l[i].hasPdf = docId > 0
        if (docId > 0) l[i].present = true
        page.advance = l
        win.toast(docId > 0 ? qsTr("Dokument verknüpft.") : qsTr("Verknüpfung gelöst."))
    }
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
            peakFlowBest: page.pfBest(), bioSex: ["", "m", "w"][bioSex.currentIndex] || "",
            inrLow: inrLow.text, inrHigh: inrHigh.text,
            fluidMin: fluidMin.text, fluidMax: fluidMax.text, weightWatch: weightWatch.text
        }
        if (!birth.valid || !review.valid || !valid.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        const id = app.savePerson(m)
        if (id) page.closed(id, qsTr("Stammblatt gespeichert."))
        else win.toast(app.lastError, true)
    }

    Popup {
        id: advPicker
        objectName: "advancePicker"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(640, page.width - 32); height: Math.min(560, page.height - 32)
        padding: 20
        property int row: -1
        property string name: ""
        property var docs: []
        function openFor(i, n) { row = i; name = n; docs = app.advanceCandidates(page.personId); open() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gapS
            Label { text: qsTr("%1: Dokument aus der Krankenakte").arg(advPicker.name); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { visible: advPicker.docs.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted
                    text: qsTr("In der Krankenakte liegt noch kein Dokument. Nehmen Sie es dort zuerst auf (Reiter „Krankenakte“).") }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 4
                model: advPicker.docs
                delegate: AbstractButton {
                    width: ListView.view.width
                    implicitHeight: dl.implicitHeight + 20
                    onClicked: { page.linkAdvance(advPicker.row, modelData.id, modelData.title + (modelData.dateText ? " (" + modelData.dateText + ")" : "")); advPicker.close() }
                    background: Rectangle { radius: 8; color: parent.down ? Theme.brandWash : Theme.surface2; border.color: Theme.rule }
                    contentItem: ColumnLayout {
                        id: dl
                        spacing: 0
                        Label { text: modelData.title; font.bold: true; color: Theme.ink; leftPadding: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: [modelData.category, modelData.dateText].filter(x => x).join(" · "); color: Theme.muted; font.pixelSize: Theme.fsS; leftPadding: 12 }
                    }
                }
            }
            VButton { text: qsTr("Abbrechen"); kind: "flat"; Layout.alignment: Qt.AlignRight; onClicked: advPicker.close() }
        }
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
            VButton { text: win.phone ? "✕" : qsTr("Abbrechen"); Accessible.name: qsTr("Abbrechen"); kind: "flat"; font.pixelSize: win.phone ? Theme.fsL + 6 : Theme.fsM; leftPadding: win.phone ? 8 : 18; rightPadding: win.phone ? 8 : 18; onClicked: page.closed(0, "") }
            Label { text: page.personId || app.light ? qsTr("Stammblatt") : qsTr("Neue Person"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.personId && app.isAdmin && !app.light; text: qsTr("Löschen"); kind: "flat"; leftPadding: win.phone ? 8 : 18; rightPadding: win.phone ? 8 : 18; onClicked: confirmDelete.open() }
            VButton { text: qsTr("Speichern"); kind: "primary"; leftPadding: win.phone ? 12 : 18; rightPadding: win.phone ? 12 : 18; onClicked: page.save() }
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

            ColumnLayout {   // Pflegegrad und Ausweis: nicht in VitaParms light
                visible: !app.light
                Layout.fillWidth: true
                spacing: Theme.gap
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
                    Label { text: qsTr("Grad der Behinderung"); color: Theme.ink2; wrapMode: Text.WordWrap; Layout.fillWidth: win.phone; Layout.minimumWidth: 0 }
                    VButton { text: "−"; width: Theme.touch; onClicked: gdb.value = Math.max(0, gdb.value - 10) }
                    Label { id: gdbLabel; text: gdb.value > 0 ? qsTr("GdB %1").arg(gdb.value) : qsTr("kein Ausweis")
                            font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.preferredWidth: win.phone ? 104 : 150; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
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
                    DateField { id: valid; label: qsTr("Gültig bis"); isoDate: d.disabilityValid; enabled: !unlimited.checked; Layout.preferredWidth: win.phone ? 170 : 260 }
                    VCheck { id: unlimited; text: qsTr("unbefristet"); checked: (d.disabilityDegree || 0) > 0 && !d.disabilityValid
                               font.pixelSize: Theme.fsM; Layout.alignment: Qt.AlignBottom }
                }

            
            }

            SectionTitle { text: app.light ? qsTr("Ärzte, Angehörige und weitere Kontakte") : qsTr("Kontakte") }
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

            ColumnLayout {   // Vorsorge: nicht in VitaParms light
                visible: !app.light
                Layout.fillWidth: true
                spacing: Theme.gap
            SectionTitle { text: qsTr("Vorsorge") }
                Repeater {
                    model: page.advance
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gapS
                        VCheck { id: advPresent; text: modelData.name + (modelData.hasPdf ? " " + qsTr("(PDF in der Krankenakte)") : "")
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
                        // hinterlegtes Dokument (PDF in der Krankenakte)
                        RowLayout {
                            visible: advPresent.checked
                            Layout.fillWidth: true; Layout.leftMargin: 36
                            spacing: Theme.gapS
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fsS
                                    color: modelData.docId > 0 ? Theme.ink : Theme.muted
                                    text: modelData.docId > 0 ? qsTr("Dokument: %1").arg(modelData.docTitle || qsTr("PDF in der Krankenakte"))
                                                              : page.personId ? qsTr("Kein Dokument hinterlegt.") : qsTr("Nach dem Speichern lässt sich ein Dokument aus der Krankenakte verknüpfen.") }
                            VButton { visible: modelData.docId > 0; text: qsTr("Anzeigen"); kind: "flat"
                                      onClicked: if (!app.openDocument(modelData.docId)) win.toast(app.lastError, true) }
                            VButton { objectName: "advanceLinkButton"; visible: page.personId > 0
                                      text: modelData.docId > 0 ? qsTr("Anderes Dokument") : qsTr("Aus der Krankenakte wählen"); kind: "flat"
                                      onClicked: advPicker.openFor(index, modelData.name) }
                            VButton { visible: modelData.docId > 0; text: qsTr("Verknüpfung lösen"); kind: "flat"
                                      onClicked: page.linkAdvance(index, 0, "") }
                        }
                    }
                }
                Label { text: qsTr("Das unterschriebene Dokument nehmen Sie zuerst in die Krankenakte auf (fotografieren oder als PDF übernehmen, Art „Vorsorge“) und verknüpfen es dann hier."); color: Theme.muted
                        font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

            
            }

            SectionTitle { text: qsTr("Diagnosen und Bemerkungen") }
            VArea { id: diagnoses; label: qsTr("Diagnosen (eine pro Zeile)"); text: d.diagnoses; Layout.fillWidth: true }
            VArea { id: notes; label: qsTr("Bemerkungen"); text: d.notes; Layout.fillWidth: true; minHeight: 80 }

            ColumnLayout {   // Wunden: nicht in VitaParms light
                visible: !app.light
                Layout.fillWidth: true
                spacing: Theme.gap
            SectionTitle { text: qsTr("Wunden bei Aufnahme") }
                Label { visible: !page.personId; text: qsTr("Wunden lassen sich erfassen, sobald das Stammblatt einmal gespeichert ist.")
                        color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Label { visible: page.personId && page.admissionWounds.length === 0; text: qsTr("Keine Wunden bei Aufnahme erfasst."); color: Theme.muted }
                Repeater {
                    model: page.admissionWounds
                    delegate: AbstractButton {
                        Layout.fillWidth: true
                        implicitHeight: wl.implicitHeight + 20
                        onClicked: page.openWound(modelData.id)
                        background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule; radius: 6 }
                        contentItem: Label { id: wl; leftPadding: 14; rightPadding: 14; text: "• " + modelData.summary + (modelData.size ? " – " + modelData.size : "")
                                             color: Theme.ink; wrapMode: Text.WordWrap }
                    }
                }
                VButton { objectName: "admissionWoundButton"; visible: !!page.personId; text: qsTr("Wunde bei Aufnahme erfassen"); iconText: "+"
                          onClicked: page.addWound() }

            
            }

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

            ColumnLayout {   // Normwerte, Peak Flow, INR, Trinkmenge: nicht in VitaParms light
                visible: !app.light
                Layout.fillWidth: true
                spacing: Theme.gap
            SectionTitle { text: qsTr("Biologisches Geschlecht") }
                VCombo { id: bioSex; objectName: "bioSexCombo"; label: qsTr("Für Normwerte")
                         model: [qsTr("— bitte wählen —"), qsTr("männlich"), qsTr("weiblich")]
                         currentIndex: Math.max(0, ["", "m", "w"].indexOf(d.bioSex || ""))
                         onActivated: page.bioTouched = true
                         Layout.preferredWidth: Math.min(360, page.width - 2 * Theme.gapL) }
                Label { text: page.bioSexHint; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

                SectionTitle { text: qsTr("Peak Flow (Asthma, COPD)") }
                VField { id: pfBestField; objectName: "peakFlowBestField"; label: qsTr("Persönlicher Bestwert (l/min)")
                         text: d.peakFlowBest ? String(d.peakFlowBest) : ""; placeholder: qsTr("mit dem Arzt festgelegt")
                         inputMethodHints: Qt.ImhDigitsOnly; Layout.preferredWidth: Math.min(360, page.width - 2 * Theme.gapL) }
                Label { objectName: "peakFlowInfo"; text: page.pfInfo; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

                SectionTitle { text: qsTr("Gerinnung (INR)") }
                RowLayout {
                    spacing: Theme.gap
                    VField { id: inrLow; objectName: "inrLowField"; label: qsTr("Zielbereich von"); text: d.inrLow || ""
                             placeholder: "2,0"; inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.preferredWidth: win.phone ? 140 : 170 }
                    VField { id: inrHigh; objectName: "inrHighField"; label: qsTr("bis"); text: d.inrHigh || ""
                             placeholder: "3,0"; inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.preferredWidth: win.phone ? 140 : 170 }
                }
                Label { text: qsTr("Ärztlich festgelegt (z. B. 2,0–3,0, bei künstlichen Herzklappen oft 2,5–3,5). Leer: Standard 2,0–3,0.")
                        color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

                SectionTitle { text: qsTr("Trinkmenge und Gewicht") }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    VField { id: fluidMin; objectName: "fluidMinField"; label: qsTr("Trinkmenge mindestens (ml/Tag)"); text: d.fluidMin || ""
                             inputMethodHints: Qt.ImhDigitsOnly; width: 300 }
                    VField { id: fluidMax; objectName: "fluidMaxField"; label: qsTr("Trinkmenge höchstens (ml/Tag)"); text: d.fluidMax || ""
                             inputMethodHints: Qt.ImhDigitsOnly; width: 300; placeholder: qsTr("nur bei ärztlicher Begrenzung") }
                    VField { id: weightWatch; objectName: "weightWatchField"; label: qsTr("Gewichtszunahme melden ab (kg in 3 Tagen)"); text: d.weightWatch || ""
                             inputMethodHints: Qt.ImhFormattedNumbersOnly; width: 300; placeholder: qsTr("z. B. 2 bei Herzschwäche") }
                }

            
            }

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
