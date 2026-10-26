// Verlaufskurven: Tag/Woche/Monat/Jahr, Bereiche wählen, Kurven per Legende ein-/ausblenden
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import VitaParms 1.0
import "../components"
import ".."

Item {
    id: page
    property var personId: 0
    property bool active: false
    property var selected: ["bp", "glucose", "weight"]

    function togglePanel(id) {
        const s = selected.slice()
        const i = s.indexOf(id)
        if (i >= 0) s.splice(i, 1); else s.push(id)
        // Reihenfolge wie in der Auswahl
        selected = chart.availablePanels.map(p => p.id).filter(x => s.indexOf(x) >= 0)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gapL; anchors.rightMargin: Theme.gapL; anchors.topMargin: Theme.gap
        spacing: Theme.gapS

        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            Row {
                spacing: 0
                Repeater {
                    model: [qsTr("Tag"), qsTr("Woche"), qsTr("Monat"), qsTr("Jahr")]
                    delegate: AbstractButton {
                        width: Math.max(84, lbl.implicitWidth + 28); height: Theme.touch
                        onClicked: chart.scale = index
                        readonly property bool on: chart.scale === index
                        contentItem: Label { id: lbl; text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                             color: on ? Theme.brandInk : Theme.ink; font.bold: on }
                        background: Rectangle {
                            color: on ? Theme.brand : parent.down ? Theme.brandWash : Theme.surface
                            border.color: Theme.ruleStrong
                            radius: index === 0 || index === 3 ? Theme.radius : 0
                            Rectangle { visible: index > 0 && index < 3; anchors.fill: parent; color: "transparent"; border.color: Theme.ruleStrong }
                        }
                    }
                }
            }
            VButton { text: "◀"; width: Theme.touch; onClicked: chart.step(-1) }
            Label { text: chart.periodText; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    height: Theme.touch; verticalAlignment: Text.AlignVCenter; leftPadding: 6; rightPadding: 6 }
            VButton { text: "▶"; width: Theme.touch; onClicked: chart.step(1) }
            VButton { text: qsTr("Heute"); onClicked: chart.today() }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            Repeater {
                model: chart.availablePanels.filter(p => p.hasData)
                VChip { text: modelData.title; colors: modelData.colors; checked: page.selected.indexOf(modelData.id) >= 0
                        onClicked: page.togglePanel(modelData.id) }
            }
            VButton { text: qsTr("Als PDF"); onClicked: {
                const f = app.exportReport("chart", page.personId, { scale: chart.scale, ref: chart.refDate, panels: page.selected, hidden: chart.hiddenKeys })
                if (f) app.openFile(f); else win.toast(app.lastError, true) } }
        }
        Label { text: qsTr("Tipp: Auf einen Legendeneintrag tippen blendet die Kurve aus oder ein; auf einen Messpunkt tippen zeigt den Wert.")
                color: Theme.muted; font.pixelSize: Theme.fsXs; Layout.fillWidth: true; wrapMode: Text.WordWrap }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: chart.height
            clip: true
            Rectangle {
                width: parent.width; height: chart.height
                color: Theme.surface; border.color: Theme.rule
                Chart {
                    id: chart
                    width: parent.width
                    height: Math.max(1, page.selected.length) * Math.max(300, Math.min(420, page.height * 0.42))
                    patientId: page.personId
                    panels: page.selected
                    revision: app.dataRevision
                    dark: Theme.dark
                    TapHandler { onTapped: (ev) => chart.tapAt(ev.position.x, ev.position.y) }
                }
                Label { anchors.centerIn: parent; visible: chart.recordCount === 0; color: Theme.muted
                        text: qsTr("Keine Messungen in diesem Zeitraum.") }
                // Wert des angetippten Messpunkts
                Rectangle {
                    visible: chart.tipText !== ""
                    x: Math.min(parent.width - width - 8, Math.max(8, chart.tipPos.x - width / 2))
                    y: Math.max(8, chart.tipPos.y - height - 18)
                    width: tip.implicitWidth + 24; height: tip.implicitHeight + 16
                    radius: 8; color: Theme.ink
                    Label { id: tip; anchors.centerIn: parent; text: chart.tipText; color: Theme.paper; font.pixelSize: Theme.fsS }
                }
            }
        }
    }
}
