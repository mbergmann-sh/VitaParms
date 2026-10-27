// Messung erfassen oder bearbeiten
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    property var personId: 0
    property var vitalId: 0
    // BZ-Tagesprofil: Messzeitpunkt, Betreff (Beobachtungen) und Termin, der nach dem Speichern erledigt ist
    property string presetContext: ""
    property string presetObservation: ""
    property var profileApptId: 0
    signal closed(string message)
    property var rec: vitalId ? app.vital(vitalId) : app.newVital(personId)
    property var summary: app.personSummary(personId)
    property var fields: app.vitalFields()
    property string context: rec.context || ""
    property string bmiText: ""
    background: Rectangle { color: Theme.paper }

    function field(key) { return fields.find(f => f.key === key) || {} }
    function fmtNum(v) { return v === undefined || v === null ? "" : String(v).replace(".", Qt.locale().decimalPoint) }
    function pad(n) { return (n < 10 ? "0" : "") + n }

    // Zeitpunkt
    property date when: new Date(rec.measuredAt)
    function setNow() { const d = new Date(); d.setSeconds(0, 0); when = d; dateField.isoDate = isoDay(d); timeField.text = hhmm(d) }
    function isoDay(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    function hhmm(d) { return pad(d.getHours()) + ":" + pad(d.getMinutes()) }
    function measuredAt() {
        const t = timeField.text.match(/^(\d{1,2})[:.](\d{2})$/)
        if (!dateField.isoDate || !t) return ""
        return dateField.isoDate + "T" + pad(parseInt(t[1])) + ":" + t[2] + ":00"
    }

    function collect() {
        const values = {}
        for (let i = 0; i < cards.count; ++i) {
            const c = cards.itemAt(i)
            if (c.text.trim() !== "") values[c.key] = parseFloat(c.text.replace(",", "."))
        }
        if (sys.text.trim() !== "") values.systolic = parseInt(sys.text)
        if (dia.text.trim() !== "") values.diastolic = parseInt(dia.text)
        if (resp.text.trim() !== "") values["Atemfrequenz"] = parseInt(resp.text)
        if (pain.value >= 0) values["Schmerz (NRS)"] = pain.value
        if (fluidIn.text.trim() !== "") values.fluidIn = parseInt(fluidIn.text)
        if (fluidOut.text.trim() !== "") values.fluidOut = parseInt(fluidOut.text)
        if (page.edemaGrade >= 0) values.edema = page.edemaGrade
        return { id: page.vitalId, measuredAt: measuredAt(), context: page.context, values: values, observations: obs.text,
                 stool: stoolCheck.checked ? stoolCombo.currentIndex : null, stoolDetail: stoolDetail(),
                 fluidInKind: page.fluidInKind, fluidOutKind: page.fluidOutKind,
                 edemaSite: page.edemaGrade > 0 ? edemaSite.text : "", edemaSide: page.edemaGrade > 0 ? page.edemaSide : "" }
    }
    // INR, Ein- und Ausfuhr, Ödeme
    readonly property var fluidInfo: app.fluidChoices()
    property string fluidInKind: fluidInfo.inKinds[0]
    property string fluidOutKind: fluidInfo.outKinds[0]
    property int edemaGrade: -1
    property string edemaSide: ""
    property var inrState: ({})
    property var fluidState: ({})
    property string edemaHint: ""
    function updateCare() {
        const c = cards.byKey("inr")
        inrState = app.inrCheck(personId, c ? c.text : "")
        fluidState = app.fluidDay(personId, collect())
        edemaHint = app.edemaAdvice({ values: page.edemaGrade >= 0 ? { edema: page.edemaGrade } : {},
                                      edemaSite: edemaSite.text, edemaSide: page.edemaSide })
    }
    onEdemaGradeChanged: updateCare()
    onEdemaSideChanged: updateCare()
    // Stuhlgang: Bristol-Typ (0 = ohne Angabe) und Angaben fürs Stuhlprotokoll
    readonly property var stoolInfo: app.stoolChoices()
    property alias stoolCombo: stoolComboBox.combo
    readonly property var stoolStatus: app.stoolStatus(personId)
    function stoolDetail() {
        if (!stoolCheck.checked) return {}
        return { amount: stoolAmount.text, color: stoolColor.text, admixture: stoolAdmix.text, symptoms: stoolSymptoms.text }
    }
    function updateBmi() {
        const w = cards.byKey("weight"), h = cards.byKey("height")
        bmiText = app.bmiPreview(personId, w ? w.text.replace(",", ".") : "", h ? h.text.replace(",", ".") : "")
    }
    // Peak Flow: Bereich zum Soll- bzw. Bestwert (grün/gelb/rot) mit Hinweis
    property var pfCheck: ({})
    function updatePeakFlow() {
        const p = cards.byKey("peakflow"), h = cards.byKey("height")
        if (!p) return
        const v = p.text.trim() !== "" ? parseInt(p.text) : 0
        pfCheck = app.peakFlowCheck(personId, isNaN(v) ? 0 : v, h ? h.text.replace(",", ".") : "", measuredAt())
    }
    Connections { target: dateField; function onIsoDateChanged() { page.updatePeakFlow() } }
    readonly property bool pfAlert: pfCheck.zone === "yellow" || pfCheck.zone === "red"
    property bool another: false
    function save(next) {
        if (measuredAt() === "") { win.toast(qsTr("Bitte Datum (TT.MM.JJJJ) und Uhrzeit (HH:MM) prüfen."), true); return }
        const m = collect()
        const w = app.checkVital(m)
        if (w.length && w[0] === qsTr("Bitte mindestens einen Wert eingeben.")) { win.toast(w[0], true); return }
        another = next
        if (w.length) { warn.message = w.join("\n\n") + "\n\n" + qsTr("Trotzdem speichern?"); warn.open(); return }
        doSave()
    }
    function doSave() {
        const id = app.saveVital(personId, collect())
        if (!id) { win.toast(app.lastError, true); return }
        savedId = id
        if (profileApptId) { app.setAppointmentDone(profileApptId, true); profileApptId = 0 }
        if (pfAlert) {        // gelber oder roter Bereich: deutlich hinweisen, dann weiter
            pfWarn.title = qsTr("Peak Flow – %1").arg(pfCheck.zoneName)
            pfWarn.message = pfCheck.text + "\n\n" + pfCheck.advice + "\n\n"
                    + qsTr("Bezug: %1. VitaParms ist kein Medizinprodukt – maßgeblich ist Ihr ärztlicher Asthma-Aktionsplan.").arg(pfCheck.reference)
            pfWarn.open()
            return
        }
        checkStool()
    }
    // schwarzer oder blutiger Stuhl: auf ärztliche Abklärung hinweisen
    property var savedId: 0
    function checkStool() {
        if (stoolCheck.checked && app.stoolNeedsAttention(stoolDetail())) { stoolWarn.open(); return }
        checkCare()
    }
    // INR außerhalb des Zielbereichs, auffällige Ödeme, Gewichtszunahme, Trinkmenge
    function checkCare() {
        const w = savedId ? app.careWarnings(personId, savedId) : []
        if (w.length) { careWarn.message = w.join("\n\n"); careWarn.open(); return }
        afterSave()
    }
    function afterSave() {
        if (another) {
            win.toast(qsTr("Messung gespeichert – nächste Messung."))
            page.vitalId = 0
            page.rec = app.newVital(personId)
            reset()
        } else page.closed(qsTr("Messung gespeichert."))
    }
    function reset() {
        for (let i = 0; i < cards.count; ++i) cards.itemAt(i).text = ""
        sys.text = ""; dia.text = ""; resp.text = ""; pain.value = -1; obs.text = ""
        stoolCheck.checked = false; stoolCombo.currentIndex = 0
        stoolAmount.text = ""; stoolColor.text = ""; stoolAdmix.text = ""; stoolSymptoms.text = ""
        fluidIn.text = ""; fluidOut.text = ""; page.edemaGrade = -1; edemaSite.text = ""; page.edemaSide = ""
        updateCare()
        setNow()
        updatePeakFlow()
        sys.field.forceActiveFocus()
    }
    Component.onCompleted: {
        const v = rec.values || {}
        sys.text = fmtNum(v.systolic); dia.text = fmtNum(v.diastolic)
        resp.text = fmtNum(v["Atemfrequenz"])
        pain.value = v["Schmerz (NRS)"] !== undefined ? v["Schmerz (NRS)"] : -1
        fluidIn.text = fmtNum(v.fluidIn); fluidOut.text = fmtNum(v.fluidOut)
        if (rec.fluidInKind) page.fluidInKind = rec.fluidInKind
        if (rec.fluidOutKind) page.fluidOutKind = rec.fluidOutKind
        page.edemaGrade = v.edema !== undefined ? v.edema : -1
        edemaSite.text = rec.edemaSite || ""; page.edemaSide = rec.edemaSide || ""
        if (rec.stool !== undefined && rec.stool !== null) {
            stoolCheck.checked = true
            stoolCombo.currentIndex = rec.stool
            const d = rec.stoolDetail || {}
            stoolAmount.text = d.amount || ""; stoolColor.text = d.color || ""
            stoolAdmix.text = d.admixture || ""; stoolSymptoms.text = d.symptoms || ""
        }
        dateField.isoDate = isoDay(when); timeField.text = hhmm(when)
        if (presetContext !== "") page.context = presetContext
        if (presetObservation !== "" && !vitalId) obs.text = presetObservation
        updateBmi()
        updatePeakFlow()
        updateCare()
        if (!vitalId) {
            const g = presetContext !== "" ? cards.byKey("glucose") : null      // BZ-Tagesprofil: gleich zum Blutzucker
            if (g) g.field.forceActiveFocus(); else sys.field.forceActiveFocus()
        }
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed("") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: page.vitalId ? qsTr("Messung bearbeiten") : page.presetObservation !== "" ? page.presetObservation : qsTr("Neue Messung"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                        Layout.alignment: Qt.AlignHCenter }
                Label { text: page.summary.name + (page.summary.birthText ? " · " + qsTr("geb. %1").arg(page.summary.birthText) : "")
                        color: Theme.muted; font.pixelSize: Theme.fsS; Layout.alignment: Qt.AlignHCenter }
            }
            VButton { visible: page.vitalId; text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
            VButton { visible: !page.vitalId && win.wide; text: qsTr("Speichern und weitere"); onClicked: page.save(true) }
            VButton { text: qsTr("Speichern"); kind: "primary"; onClicked: page.save(false) }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300        // Platz für die Bildschirmtastatur
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(1100, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            // Zeitpunkt und Messzeitpunkt
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                DateField { id: dateField; width: 200; allowEmpty: false }
                VField { id: timeField; width: 120; placeholder: "HH:MM"; inputMethodHints: Qt.ImhPreferNumbers }
                VButton { text: qsTr("Jetzt"); onClicked: page.setNow(); y: 0 }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater {
                    model: app.contexts()
                    VChip { text: modelData; checked: page.context === modelData
                            onClicked: { page.context = page.context === modelData ? "" : modelData; ctxFree.text = "" } }
                }
            }
            // eigener Messzeitpunkt (Freitext), z. B. „nach dem Spaziergang“
            VField {
                id: ctxFree
                objectName: "contextFreeText"
                Layout.preferredWidth: Math.min(520, col.width)
                label: qsTr("Oder eigener Messzeitpunkt")
                placeholder: qsTr("z. B. nach dem Spaziergang")
                Component.onCompleted: text = app.contexts().indexOf(page.context) < 0 ? page.context : ""
                field.onTextEdited: page.context = text.trim()
            }

            // Werte
            GridLayout {
                id: grid
                Layout.fillWidth: true
                columns: Math.max(1, Math.floor(width / 260))
                columnSpacing: 12; rowSpacing: 12

                // Blutdruck als Doppelkachel
                Rectangle {
                    Layout.fillWidth: true
                    Layout.columnSpan: Math.min(2, grid.columns)
                    implicitHeight: 128
                    radius: Theme.radius
                    color: Theme.surface
                    border.width: sys.field.activeFocus || dia.field.activeFocus ? 2 : 1
                    border.color: sys.field.activeFocus || dia.field.activeFocus ? page.field("systolic").color : Theme.rule
                    RowLayout {
                        anchors.fill: parent; spacing: 0
                        ValueCard { id: sys; Layout.fillWidth: true; Layout.fillHeight: true; border.width: 0; color: "transparent"
                                    label: page.field("systolic").label; unit: "mmHg"; accent: page.field("systolic").color
                                    minValue: page.field("systolic").min; maxValue: page.field("systolic").max
                                    field.onTextEdited: if (sys.text.length >= 3) dia.field.forceActiveFocus() }
                        Label { text: "/"; font.pixelSize: Theme.fsXxl; color: Theme.muted }
                        ValueCard { id: dia; Layout.fillWidth: true; Layout.fillHeight: true; border.width: 0; color: "transparent"
                                    label: page.field("diastolic").label; unit: "mmHg"; accent: page.field("diastolic").color
                                    minValue: page.field("diastolic").min; maxValue: page.field("diastolic").max }
                    }
                }
                Repeater {
                    id: cards
                    function byKey(k) { for (let i = 0; i < count; ++i) if (itemAt(i).key === k) return itemAt(i); return null }
                    model: page.fields.filter(f => !f.extra && f.key !== "systolic" && f.key !== "diastolic")
                    delegate: ValueCard {
                        property string key: modelData.key
                        Layout.fillWidth: true
                        label: modelData.label; unit: modelData.unit; accent: modelData.color; decimals: modelData.decimals
                        minValue: modelData.min; maxValue: modelData.max
                        text: page.fmtNum((page.rec.values || {})[modelData.key])
                        onEdited: {
                            if (key === "weight" || key === "height") page.updateBmi()
                            if (key === "peakflow" || key === "height") page.updatePeakFlow()
                            if (key === "inr") page.updateCare()
                        }
                    }
                }
                // Atemfrequenz mit Minutenuhr
                ValueCard {
                    id: resp
                    Layout.fillWidth: true
                    label: page.field("Atemfrequenz").label; unit: "/min"; accent: Theme.muted
                    minValue: 3; maxValue: 80
                    VButton {
                        id: countBtn
                        property int remaining: 0
                        height: 40
                        text: remaining > 0 ? qsTr("%1 s – Stopp").arg(remaining) : qsTr("1 Minute zählen")
                        kind: remaining > 0 ? "primary" : "ghost"
                        onClicked: {
                            if (remaining > 0) { remaining = 0; countTimer.stop(); return }
                            remaining = 60; countTimer.start(); app.playSound("countStart")
                        }
                        Timer {
                            id: countTimer; interval: 1000; repeat: true
                            onTriggered: {
                                countBtn.remaining -= 1
                                if (countBtn.remaining <= 0) { stop(); app.playSound("countStop"); resp.field.forceActiveFocus(); resp.field.selectAll() }
                            }
                        }
                    }
                }
            }
            Label { visible: page.bmiText !== ""; text: page.bmiText; color: Theme.ink2; font.pixelSize: Theme.fsM; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label {
                objectName: "peakFlowHint"
                visible: !!page.pfCheck.text
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: (page.pfCheck.text || "") + (page.pfCheck.advice ? "\n" + page.pfCheck.advice : "")
                color: page.pfCheck.color || Theme.ink2
                font.pixelSize: Theme.fsM; font.bold: page.pfAlert
            }

            Label {
                objectName: "inrHint"
                visible: (cards.byKey("inr") !== null) && !!page.inrState.text
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: (page.inrState.text || "") + (page.inrState.advice ? "\n" + page.inrState.advice : "")
                color: page.inrState.color || Theme.ink2
                font.pixelSize: Theme.fsM; font.bold: page.inrState.zone === "veryHigh"
            }

            // Schmerz 0–10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: page.field("Schmerz (NRS)").label; color: Theme.ink2; font.pixelSize: Theme.fsS }
                QtObject { id: pain; property int value: -1 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: 11
                        AbstractButton {
                            width: 56; height: 56
                            onClicked: pain.value = pain.value === index ? -1 : index
                            readonly property bool on: pain.value === index
                            contentItem: Label { text: index; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                                 font.pixelSize: Theme.fsL; font.bold: on; color: on ? "#FFFFFF" : Theme.ink }
                            background: Rectangle {
                                radius: Theme.radius
                                // grün → rot, wie auf Schmerzskalen üblich
                                color: on ? Qt.hsla((1 - index / 10) * 0.33, 0.62, 0.40, 1) : Theme.surface
                                border.color: Theme.ruleStrong
                            }
                        }
                    }
                    Label { text: pain.value < 0 ? qsTr("nicht erfasst") : pain.value === 0 ? qsTr("kein Schmerz") : pain.value >= 8 ? qsTr("stärkster Schmerz") : ""
                            color: Theme.muted; height: 56; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                }
            }

            // Ein- und Ausfuhr
            Frame {
                objectName: "fluidSection"
                Layout.fillWidth: true
                padding: 14
                contentWidth: 300
                background: Rectangle { radius: Theme.radius; color: Theme.surface; border.color: Theme.rule }
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    Label { text: qsTr("Ein- und Ausfuhr"); font.pixelSize: Theme.fsL; color: Theme.ink }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.width > 900 ? 2 : 1
                        columnSpacing: Theme.gapL; rowSpacing: Theme.gap
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                            spacing: 6
                            VField { id: fluidIn; objectName: "fluidIn"; Layout.fillWidth: true; label: qsTr("Einfuhr (ml)")
                                     inputMethodHints: Qt.ImhDigitsOnly; field.onTextEdited: page.updateCare() }
                            Flow {
                                Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.preferredHeight: implicitHeight
                                spacing: 6
                                Repeater {
                                    model: page.fluidInfo.amounts
                                    VChip { text: modelData + " ml"; checkable: false; font.pixelSize: Theme.fsS
                                            onClicked: { fluidIn.text = String((parseInt(fluidIn.text) || 0) + parseInt(modelData)); page.updateCare() } }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.preferredHeight: implicitHeight
                                spacing: 6
                                Repeater {
                                    model: page.fluidInfo.inKinds
                                    VChip { text: modelData; checkable: false; checked: page.fluidInKind === modelData; font.pixelSize: Theme.fsS
                                            onClicked: page.fluidInKind = modelData }
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                            spacing: 6
                            VField { id: fluidOut; objectName: "fluidOut"; Layout.fillWidth: true; label: qsTr("Ausfuhr (ml)")
                                     inputMethodHints: Qt.ImhDigitsOnly; field.onTextEdited: page.updateCare() }
                            Flow {
                                Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.preferredHeight: implicitHeight
                                spacing: 6
                                Repeater {
                                    model: page.fluidInfo.outKinds
                                    VChip { text: modelData; checkable: false; checked: page.fluidOutKind === modelData; font.pixelSize: Theme.fsS
                                            onClicked: page.fluidOutKind = modelData }
                                }
                            }
                        }
                    }
                    Label {
                        objectName: "fluidHint"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: page.fluidState.text || ""
                        color: page.fluidState.over ? Theme.warn : Theme.ink2
                        font.pixelSize: Theme.fsS
                    }
                }
            }

            // Ödeme
            Frame {
                objectName: "edemaSection"
                Layout.fillWidth: true
                padding: 14
                contentWidth: 300
                background: Rectangle { radius: Theme.radius; color: Theme.surface; border.color: page.edemaGrade > 0 ? Theme.brand : Theme.rule }
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    Label { text: qsTr("Ödeme (Dellentiefe nach Fingerdruck)"); font.pixelSize: Theme.fsL; color: Theme.ink }
                    Flow {
                        Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.preferredHeight: implicitHeight
                        spacing: 6
                        VChip { text: qsTr("nicht erfasst"); checkable: false; checked: page.edemaGrade < 0; onClicked: page.edemaGrade = -1 }
                        Repeater {
                            model: page.fluidInfo.edemaGrades
                            VChip { objectName: "edemaGrade" + modelData.value
                                    text: modelData.name; checkable: false; checked: page.edemaGrade === modelData.value
                                    onClicked: page.edemaGrade = modelData.value }
                        }
                    }
                    SuggestField { id: edemaSite; objectName: "edemaSite"; visible: page.edemaGrade > 0; Layout.fillWidth: true
                                   label: qsTr("Ort"); suggestions: page.fluidInfo.edemaSites }
                    Connections { target: edemaSite.field.field; function onTextChanged() { page.updateCare() } }
                    Flow {
                        visible: page.edemaGrade > 0
                        Layout.fillWidth: true; Layout.preferredWidth: 200; Layout.preferredHeight: implicitHeight
                        spacing: 6
                        Repeater {
                            model: page.fluidInfo.edemaSides
                            VChip { text: modelData; checkable: false; checked: page.edemaSide === modelData
                                    onClicked: page.edemaSide = page.edemaSide === modelData ? "" : modelData }
                        }
                    }
                    Label {
                        objectName: "edemaHint"
                        visible: page.edemaHint !== ""
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: page.edemaHint
                        color: page.edemaHint.indexOf("\n") >= 0 ? Theme.warn : Theme.ink2
                        font.pixelSize: Theme.fsS
                    }
                }
            }

            // Stuhlgang: Häkchen schaltet die Auswahl nach der Bristol-Skala frei
            Frame {
                objectName: "stoolSection"
                Layout.fillWidth: true
                padding: 14
                contentWidth: 300                // Breite kommt vom Layout (fillWidth)
                background: Rectangle { radius: Theme.radius; color: Theme.surface; border.color: stoolCheck.checked ? Theme.brand : Theme.rule }
                ColumnLayout {
                    id: stoolCol
                    width: parent.width
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap
                        CheckBox {
                            id: stoolCheck
                            objectName: "stoolCheck"
                            text: qsTr("Stuhlgang")
                            font.pixelSize: Theme.fsL
                            Layout.preferredWidth: 200
                        }
                        VCombo {
                            id: stoolComboBox
                            Layout.fillWidth: true
                            enabled: stoolCheck.checked
                            model: page.stoolInfo.types.map(t => t.value ? t.name + " – " + t.text : t.text)
                        }
                    }
                    Label {
                        visible: !stoolCheck.checked && page.stoolStatus.warn === true
                        objectName: "stoolStatusHint"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: page.stoolStatus.text || ""
                        color: Theme.warn; font.pixelSize: Theme.fsS
                    }
                    Label {
                        visible: stoolCheck.checked
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        readonly property var t: page.stoolInfo.types[stoolCombo.currentIndex] || {}
                        text: t.group ? qsTr("Bristol %1: %2").arg(t.name).arg(t.group) : page.stoolInfo.scale
                        color: t.group ? t.color : Theme.muted; font.pixelSize: Theme.fsS
                    }
                    GridLayout {
                        visible: stoolCheck.checked
                        Layout.fillWidth: true
                        columns: page.width > 900 ? 2 : 1
                        columnSpacing: Theme.gapL; rowSpacing: Theme.gap
                        SuggestField { id: stoolAmount; objectName: "stoolAmount"; Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                                       label: qsTr("Menge"); suggestions: page.stoolInfo.amounts }
                        SuggestField { id: stoolColor; objectName: "stoolColor"; Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                                       label: qsTr("Farbe"); suggestions: page.stoolInfo.colors }
                        SuggestField { id: stoolAdmix; objectName: "stoolAdmixture"; Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                                       label: qsTr("Beimengungen / Geruch"); suggestions: page.stoolInfo.admixtures; multi: true }
                        VField { id: stoolSymptoms; objectName: "stoolSymptoms"; Layout.fillWidth: true; Layout.preferredWidth: 400; Layout.alignment: Qt.AlignTop
                                 label: qsTr("Begleitsymptome / Besonderheiten"); placeholder: page.stoolInfo.symptomsHint }
                    }
                }
            }

            VArea { id: obs; label: qsTr("Beobachtungen"); text: page.rec.observations || ""; placeholder: qsTr("z. B. Befinden, Besonderheiten, Messbedingungen …")
                    Layout.fillWidth: true }
            VButton { visible: !page.vitalId && !win.wide; text: qsTr("Speichern und weitere Messung"); Layout.fillWidth: true; onClicked: page.save(true) }
        }
    }

    Confirm { id: warn; title: qsTr("Bitte prüfen"); acceptText: qsTr("Trotzdem speichern"); onAccepted: page.doSave() }
    Confirm {
        id: pfWarn
        objectName: "peakFlowWarning"
        acceptText: qsTr("Verstanden"); rejectText: ""
        danger: page.pfCheck.zone === "red"
        closePolicy: Popup.NoAutoClose
        onAccepted: page.checkStool()
    }
    Confirm {
        id: stoolWarn
        objectName: "stoolWarning"
        title: qsTr("Stuhlgang")
        message: page.stoolInfo.attention
        acceptText: qsTr("Verstanden"); rejectText: ""
        closePolicy: Popup.NoAutoClose
        onAccepted: page.afterSave()
    }
    Confirm {
        id: confirmDelete
        title: qsTr("Messung löschen?")
        message: qsTr("Die Messung wird endgültig gelöscht.")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deleteVital(page.vitalId)) page.closed(qsTr("Messung gelöscht.")); else win.toast(app.lastError, true) }
    }
}
