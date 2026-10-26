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
        return { id: page.vitalId, measuredAt: measuredAt(), context: page.context, values: values, observations: obs.text }
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
        if (profileApptId) { app.setAppointmentDone(profileApptId, true); profileApptId = 0 }
        if (pfAlert) {        // gelber oder roter Bereich: deutlich hinweisen, dann weiter
            pfWarn.title = qsTr("Peak Flow – %1").arg(pfCheck.zoneName)
            pfWarn.message = pfCheck.text + "\n\n" + pfCheck.advice + "\n\n"
                    + qsTr("Bezug: %1. VitaParms ist kein Medizinprodukt – maßgeblich ist Ihr ärztlicher Asthma-Aktionsplan.").arg(pfCheck.reference)
            pfWarn.open()
            return
        }
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
        setNow()
        updatePeakFlow()
        sys.field.forceActiveFocus()
    }
    Component.onCompleted: {
        const v = rec.values || {}
        sys.text = fmtNum(v.systolic); dia.text = fmtNum(v.diastolic)
        resp.text = fmtNum(v["Atemfrequenz"])
        pain.value = v["Schmerz (NRS)"] !== undefined ? v["Schmerz (NRS)"] : -1
        dateField.isoDate = isoDay(when); timeField.text = hhmm(when)
        if (presetContext !== "") page.context = presetContext
        if (presetObservation !== "" && !vitalId) obs.text = presetObservation
        updateBmi()
        updatePeakFlow()
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
