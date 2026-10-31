// Medikamentenbestand und Bestellungen (wie am Computer): Packungen mit Verfallsdatum,
// Reichweite, „bestellen bis“, Bestelllisten (offen → bestellt → geliefert)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "stockPage"
    property var personId: 0
    property int tab: 0                       // 0 Bestand, 1 Bestellungen
    signal closed(string message)
    background: Rectangle { color: Theme.paper }

    property var summary: app.personSummary(personId)
    property var d: ({})
    function reload() { d = app.stockOverview(personId) }
    Connections { target: app; function onDataChanged() { page.reload(); if (pkgPopup.opened) pkgPopup.reload() } }
    Component.onCompleted: reload()

    function stateColor(state, onOrder) {
        if (onOrder && state !== 1) return Theme.brand
        return state === 4 ? Theme.alert : state === 3 ? Theme.warn : state === 2 ? Theme.warn : Theme.ok
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { objectName: "stockCloseButton"; text: qsTr("Schließen"); kind: "flat"; onClicked: page.closed("") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: qsTr("Medikamentenbestand"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                Label { text: page.summary.name || ""; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            }
            VChip { objectName: "stockTab"; checkable: false; text: qsTr("Bestand"); checked: page.tab === 0; onClicked: page.tab = 0 }
            VChip { objectName: "ordersTab"; checkable: false; text: qsTr("Bestellungen") + ((page.d.anyOpen) ? " ⚠" : ""); checked: page.tab === 1; onClicked: page.tab = 1 }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: (page.tab === 0 ? stockCol.implicitHeight : ordersCol.implicitHeight) + 64
        clip: true

        // ---------------- Bestand
        ColumnLayout {
            id: stockCol
            visible: page.tab === 0
            x: Theme.gapL; y: Theme.gap
            width: page.width - 2 * Theme.gapL
            spacing: Theme.gap

            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 900 ? 3 : 1
                columnSpacing: Theme.gapL; rowSpacing: Theme.gapS
                ColumnLayout {
                    spacing: 4
                    Label { text: qsTr("Gestellt wird"); font.pixelSize: Theme.fsS; color: Theme.ink2 }
                    Flow {
                        Layout.fillWidth: true; spacing: Theme.gapS
                        VChip { objectName: "planDaily"; checkable: false; text: qsTr("täglich"); checked: !page.d.weekly
                                onClicked: app.setPrepPlan(page.personId, 1, page.d.weekday || 1) }
                        VChip { objectName: "planWeekly"; checkable: false; text: qsTr("wöchentlich"); checked: page.d.weekly === true
                                onClicked: app.setPrepPlan(page.personId, 7, page.d.weekday || 1) }
                    }
                }
                VCombo { label: qsTr("ab Wochentag"); enabled: page.d.weekly === true; model: page.d.weekdays || []
                         currentIndex: (page.d.weekday || 1) - 1; Layout.fillWidth: true; Layout.maximumWidth: 320
                         onActivated: (i) => app.setPrepPlan(page.personId, 7, i + 1) }
                VStepper { objectName: "leadStepper"; label: qsTr("Vorlauf (Rezept, Bestellung, Lieferung)"); from: 1; to: 60
                           value: page.d.lead || 7; text: qsTr("%1 Tage").arg(value)
                           onValueChanged: if (page.d.lead !== undefined && value !== page.d.lead) app.setLeadDays(value) }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; font.pixelSize: Theme.fsS
                text: qsTr("Erfasst werden Medikamente, für die eine Packung eingebucht ist. Nach jedem Stellen mit dem Assistenten wird die gestellte Menge abgebucht – zuerst aus der Packung, die am frühesten verfällt; verfallene Packungen zählen nicht. „Bestellen bis“ = Reichweite minus Vorlauf, an Wochenenden der Freitag davor. Gestellt wird %1.").arg(page.d.planText || "")
                      + (page.d.weekly ? " " + qsTr("Der Bestand muss am Stelltag für die ganze Woche reichen.") : "")
            }
            Rectangle {
                visible: (page.d.warnCount || 0) > 0
                Layout.fillWidth: true
                implicitHeight: warnLbl.implicitHeight + 24
                radius: Theme.radius; color: Theme.warnWash; border.color: Theme.warn
                Label { id: warnLbl; x: 14; y: 12; width: parent.width - 28; wrapMode: Text.WordWrap; color: Theme.ink; font.bold: true
                        text: qsTr("Medikamente jetzt nachbestellen: %1").arg(page.d.warnCount || 0) }
            }
            Label { visible: (page.d.meds || []).length === 0; text: qsTr("Noch keine Medikamente eingetragen."); color: Theme.muted }
            Repeater {
                model: page.d.meds || []
                delegate: Rectangle {
                    objectName: "stockCard"
                    Layout.fillWidth: true
                    implicitHeight: mc.implicitHeight + 28
                    radius: Theme.radius
                    color: Theme.surface; border.color: Theme.rule
                    ColumnLayout {
                        id: mc
                        x: 16; y: 14; width: parent.width - 32
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.name; font.bold: true; font.pixelSize: Theme.fsL; color: modelData.tracked ? Theme.ink : Theme.muted
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { text: modelData.stateText; font.bold: modelData.tracked; color: modelData.tracked ? page.stateColor(modelData.state, modelData.onOrder) : Theme.muted
                                    Layout.maximumWidth: 360; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignRight }
                        }
                        GridLayout {
                            visible: modelData.tracked
                            Layout.fillWidth: true
                            columns: page.width > 900 ? 5 : 2
                            columnSpacing: Theme.gap; rowSpacing: 2
                            Repeater {
                                model: [[qsTr("Bestand"), (modelData.stock || "") + (modelData.expired ? " " + qsTr("(+%1 verfallen)").arg(modelData.expired) : "")],
                                        [qsTr("Bedarf/Tag"), modelData.daily || "–"],
                                        [qsTr("reicht bis"), modelData.until || "–"],
                                        [qsTr("bestellen bis"), modelData.orderBy || "–"],
                                        [qsTr("nächster Verfall"), modelData.nextExpiry || "–"]]
                                ColumnLayout {
                                    spacing: 0; Layout.fillWidth: true
                                    Label { text: modelData[0]; color: Theme.muted; font.pixelSize: Theme.fsXs }
                                    Label { text: modelData[1]; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true; spacing: Theme.gapS
                            VButton { objectName: "receiveButton"; text: qsTr("Packung einbuchen"); iconText: "+"
                                      onClicked: qtyPopup.openFor("receive", modelData.id, 0, 0, modelData.name, modelData.unit, 100, "", "") }
                            VButton { visible: modelData.tracked; text: qsTr("Packungen und Buchungen"); onClicked: pkgPopup.openFor(modelData.id) }
                            VButton { objectName: "toOrderButton"; text: qsTr("Zur Bestellliste")
                                      onClicked: { if (app.addToOrder(page.personId, modelData.id)) { page.tab = 1; win.toast(qsTr("Zur Bestellliste hinzugefügt.")) } else win.toast(app.lastError, true) } }
                        }
                    }
                }
            }
        }

        // ---------------- Bestellungen
        ColumnLayout {
            id: ordersCol
            visible: page.tab === 1
            x: Theme.gapL; y: Theme.gap
            width: page.width - 2 * Theme.gapL
            spacing: Theme.gap
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: page.d.anyOpen ? Theme.warn : Theme.ink2; font.bold: page.d.anyOpen === true
                text: page.d.anyOpen
                      ? qsTr("Eine Bestellliste ist noch nicht als bestellt bestätigt. Rezept besorgen, bestellen und dann „Als bestellt bestätigen“ – bis dahin erinnert VitaParms daran.")
                      : qsTr("Bestelllisten bleiben hier gespeichert und lassen sich jederzeit erneut als PDF ausgeben.")
            }
            VButton { objectName: "createOrderButton"; text: qsTr("Bestellliste erstellen"); kind: "primary"
                      onClicked: { const n = app.createOrderList(page.personId); if (n === 0) win.toast(qsTr("Zurzeit muss kein Medikament nachbestellt werden.")) } }
            Label { visible: (page.d.orders || []).length === 0; text: qsTr("Noch keine Bestellungen."); color: Theme.muted }
            Repeater {
                model: page.d.orders || []
                delegate: Rectangle {
                    id: oc
                    objectName: "orderCard"
                    readonly property var order: modelData
                    Layout.fillWidth: true
                    implicitHeight: ocol.implicitHeight + 28
                    radius: Theme.radius
                    color: Theme.surface; border.color: order.status === 0 ? Theme.warn : Theme.rule
                    ColumnLayout {
                        id: ocol
                        x: 16; y: 14; width: parent.width - 32
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Bestellliste vom %1").arg(oc.order.created); font.bold: true; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Label { text: oc.order.statusText; font.bold: oc.order.status === 0; color: oc.order.status === 0 ? Theme.warn : oc.order.status === 2 ? Theme.ok : Theme.brand }
                        }
                        Label { visible: !!oc.order.note; text: oc.order.note; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Repeater {
                            model: oc.order.items
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: ic.implicitHeight + 16
                                radius: 8; color: Theme.surface2
                                ColumnLayout {
                                    id: ic
                                    x: 12; y: 8; width: parent.width - 24; spacing: 2
                                    Label { text: modelData.name + (modelData.pzn ? "  ·  PZN " + modelData.pzn : ""); font.bold: true; color: Theme.ink
                                            Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Label { text: [modelData.amount, modelData.reason].filter(x => x).join(" – "); color: Theme.ink2; font.pixelSize: Theme.fsS
                                            Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Label { visible: modelData.delivered; text: "✔ " + modelData.deliveredText; color: Theme.ok; font.pixelSize: Theme.fsS }
                                    Flow {
                                        visible: !modelData.delivered
                                        Layout.fillWidth: true; spacing: Theme.gapS
                                        VButton { visible: oc.order.status === 0; text: qsTr("Menge ändern"); kind: "flat"
                                                  onClicked: amountPopup.openFor(modelData) }
                                        VButton { visible: oc.order.status === 0; text: qsTr("Entfernen"); kind: "flat"
                                                  onClicked: app.removeOrderItem(modelData.id) }
                                        VButton { objectName: "deliverButton"; visible: oc.order.status !== 2; text: qsTr("Lieferung einbuchen"); kind: "flat"
                                                  onClicked: qtyPopup.openFor("deliver", modelData.medId, oc.order.id, modelData.id, modelData.name, "", 100, "", "") }
                                    }
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true; spacing: Theme.gapS
                            VButton { objectName: "confirmOrderButton"; visible: oc.order.status === 0 && oc.order.items.length > 0
                                      text: qsTr("Als bestellt bestätigen"); kind: "primary"; onClicked: confirmPopup.openFor(oc.order) }
                            VButton { visible: oc.order.items.length > 0; text: qsTr("Als PDF")
                                      onClicked: { const f = app.exportReport("order", page.personId, { orderId: oc.order.id }); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
                            VButton { visible: oc.order.status === 0; text: qsTr("Löschen"); kind: "flat"
                                      onClicked: { delConfirm.orderId = oc.order.id; delConfirm.open() } }
                        }
                    }
                }
            }
        }
    }

    Confirm { id: delConfirm; property var orderId: 0; title: qsTr("Bestellliste löschen"); message: qsTr("Diese offene Bestellliste löschen?")
              acceptText: qsTr("Löschen"); danger: true; onAccepted: app.deleteOrder(orderId) }

    // Menge und Verfallsdatum: Packung einbuchen, Bestand korrigieren, Lieferung einbuchen
    Popup {
        id: qtyPopup
        objectName: "qtyPopup"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, page.width - 32)
        padding: 24
        property string mode: "receive"
        property var medId: 0
        property var orderId: 0
        property var itemId: 0       // Lieferung: Position; Korrektur: Packung
        property string name: ""
        function openFor(m, med, order, item, n, unit, qty, expiry, noteText) {
            mode = m; medId = med; orderId = order; itemId = item; name = n
            qtyField.text = String(qty).replace(".", ",")
            hasExpiry.checked = m === "correct" ? expiry !== "" : true
            expiryField.isoDate = expiry !== "" ? expiry : page.inYears(2)
            noteField.text = noteText
            open()
        }
        function accept() {
            const q = parseFloat(qtyField.text.replace(",", "."))
            if (!(q >= 0) || (mode !== "correct" && !(q > 0))) { win.toast(qsTr("Bitte eine Menge angeben."), true); return }
            const exp = hasExpiry.checked ? expiryField.isoDate : ""
            let ok = false
            if (mode === "receive") ok = app.receivePackage(page.personId, medId, q, exp, noteField.text)
            else if (mode === "correct") ok = app.correctPackage(page.personId, medId, itemId, q, exp, noteField.text)
            else ok = app.deliverOrderItem(orderId, itemId, q, exp)
            if (!ok) { win.toast(app.lastError, true); return }
            close()
            win.toast(mode === "correct" ? qsTr("Bestand korrigiert.") : qsTr("Eingebucht."))
        }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gap
            Label { text: qtyPopup.mode === "receive" ? qsTr("Packung einbuchen") : qtyPopup.mode === "correct" ? qsTr("Bestand korrigieren") : qsTr("Lieferung einbuchen")
                    font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            Label { text: qtyPopup.name; color: Theme.ink2; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            VField { id: qtyField; objectName: "qtyField"; label: qtyPopup.mode === "correct" ? qsTr("Gezählte Menge (Stück)") : qtyPopup.mode === "deliver" ? qsTr("Gelieferte Menge (Stück)") : qsTr("Menge (Stück)")
                     inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.fillWidth: true }
            VCheck { id: hasExpiry; text: qsTr("Verfällt am"); font.pixelSize: Theme.fsM }
            DateField { id: expiryField; enabled: hasExpiry.checked; allowEmpty: false; Layout.fillWidth: true }
            Label { visible: hasExpiry.checked && expiryField.isoDate !== "" && expiryField.isoDate < page.inYears(0)
                    text: qsTr("Das Verfallsdatum liegt in der Vergangenheit – die Packung zählt nicht zum Bestand.")
                    color: Theme.warn; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            VField { id: noteField; visible: qtyPopup.mode !== "deliver"; label: qsTr("Notiz"); placeholder: qsTr("z. B. Apotheke, Charge, N3"); Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.gapS
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: qtyPopup.close() }
                VButton { objectName: "qtyOkButton"; text: qtyPopup.mode === "correct" ? qsTr("Übernehmen") : qsTr("Einbuchen"); kind: "primary"; onClicked: qtyPopup.accept() }
            }
        }
    }
    function pad(n) { return (n < 10 ? "0" : "") + n }
    function inYears(y) { const t = new Date(); t.setFullYear(t.getFullYear() + y); return t.getFullYear() + "-" + pad(t.getMonth() + 1) + "-" + pad(t.getDate()) }

    // Packungen und Buchungen eines Medikaments
    Popup {
        id: pkgPopup
        objectName: "packagesPopup"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(860, page.width - 32); height: Math.min(720, page.height - 32)
        padding: 20
        property var medId: 0
        property var info: ({})
        function openFor(id) { medId = id; reload(); open() }
        function reload() { info = app.stockPackages(page.personId, medId) }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gapS
            RowLayout {
                Layout.fillWidth: true
                Label { text: pkgPopup.info.name || ""; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                VButton { text: qsTr("Packung einbuchen"); iconText: "+"
                          onClicked: qtyPopup.openFor("receive", pkgPopup.medId, 0, 0, pkgPopup.info.name, pkgPopup.info.unit, 100, "", "") }
                VButton { text: qsTr("Schließen"); kind: "flat"; onClicked: pkgPopup.close() }
            }
            Flickable {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentHeight: pcol.implicitHeight
                clip: true
                ColumnLayout {
                    id: pcol
                    width: parent.width
                    spacing: 6
                    SectionTitle { text: qsTr("Packungen (zuerst verbraucht wird die oberste)") }
                    Label { visible: (pkgPopup.info.packages || []).length === 0; text: qsTr("Kein Bestand."); color: Theme.muted }
                    Repeater {
                        model: pkgPopup.info.packages || []
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: prow.implicitHeight + 16
                            radius: 8; color: Theme.surface2
                            RowLayout {
                                id: prow
                                x: 12; y: 8; width: parent.width - 24
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 0
                                    Label { text: modelData.qtyText; font.bold: true; color: Theme.ink }
                                    Label { text: (modelData.expiryText ? qsTr("verfällt %1").arg(modelData.expiryText) + (modelData.expired ? " – " + qsTr("verfallen") : "") : qsTr("ohne Verfallsdatum"))
                                                  + " · " + qsTr("Eingang %1").arg(modelData.received) + (modelData.note ? " · " + modelData.note : "")
                                            color: modelData.expired ? Theme.alert : Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                }
                                VButton { text: qsTr("Zählen/Korrigieren"); kind: "flat"
                                          onClicked: qtyPopup.openFor("correct", pkgPopup.medId, 0, modelData.id, pkgPopup.info.name, pkgPopup.info.unit, modelData.qty, modelData.expiry, modelData.note) }
                                VButton { text: qsTr("Entsorgen"); kind: "flat"
                                          onClicked: { discardConfirm.pkgId = modelData.id; discardConfirm.message = qsTr("%1 aus dieser Packung als entsorgt ausbuchen?").arg(modelData.qtyText); discardConfirm.open() } }
                            }
                        }
                    }
                    SectionTitle { text: qsTr("Buchungen") }
                    Repeater {
                        model: pkgPopup.info.moves || []
                        delegate: Label {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fsS; color: Theme.ink2
                            text: modelData.when + " · " + modelData.kind + " " + modelData.qty
                                  + (modelData.forDate ? " · " + qsTr("für %1").arg(modelData.forDate) : "")
                                  + (modelData.by ? " · " + modelData.by : "") + (modelData.note ? " · " + modelData.note : "")
                        }
                    }
                    VButton { visible: pkgPopup.info.tracked === true; text: qsTr("Bestandsführung für dieses Medikament beenden"); kind: "flat"
                              onClicked: { app.setStockTracked(page.personId, pkgPopup.medId, false); pkgPopup.close() } }
                }
            }
        }
    }
    Confirm { id: discardConfirm; property var pkgId: 0; title: qsTr("Entsorgen"); acceptText: qsTr("Entsorgen"); danger: true
              onAccepted: if (!app.discardPackage(pkgPopup.medId, pkgId)) win.toast(app.lastError, true) }

    // Menge einer Bestellposition
    Popup {
        id: amountPopup
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, page.width - 32)
        padding: 24
        property var item: ({})
        function openFor(it) { item = it; amountField.text = it.amount || ""; open() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gap
            Label { text: qsTr("Menge ändern"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            VField { id: amountField; label: qsTr("Menge für %1").arg(amountPopup.item.name || ""); placeholder: qsTr("z. B. „1 Packung N3“, „100 Stück“"); Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: amountPopup.close() }
                VButton { text: qsTr("Übernehmen"); kind: "primary"
                          onClicked: { if (app.setOrderItemAmount(page.personId, amountPopup.item.id, amountPopup.item.medId, amountField.text)) amountPopup.close() } }
            }
        }
    }

    // Bestellung bestätigen
    Popup {
        id: confirmPopup
        objectName: "confirmOrderPopup"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(600, page.width - 32)
        padding: 24
        property var order: ({})
        function openFor(o) { order = o; orderedAt.isoDate = page.inYears(0); orderNote.text = ""; open() }
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gap
            Label { text: qsTr("Bestellung bestätigen"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            Label { text: qsTr("Die Bestellung mit %1 Position(en) wurde aufgegeben. Danach erinnert VitaParms nicht mehr daran.").arg((confirmPopup.order.items || []).length)
                    color: Theme.ink2; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            DateField { id: orderedAt; label: qsTr("Bestellt am"); allowEmpty: false; Layout.fillWidth: true }
            VField { id: orderNote; label: qsTr("Bemerkung"); placeholder: qsTr("z. B. Rezept bei Dr. … angefordert, bestellt bei … Apotheke, Abholung Mi"); Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: confirmPopup.close() }
                VButton { objectName: "confirmOrderOk"; text: qsTr("Bestätigen"); kind: "primary"
                          onClicked: { if (app.confirmOrder(confirmPopup.order.id, orderedAt.isoDate, orderNote.text)) confirmPopup.close(); else win.toast(app.lastError, true) } }
            }
        }
    }
}
