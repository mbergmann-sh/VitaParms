// Krankenakte: Dokumente der Person, Suche, Art, Fotografieren, Datei übernehmen, Teilen
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    objectName: "documentsPage"
    property var personId: 0
    property bool active: false              // nur der sichtbare Reiter lädt (Vorschaubilder kosten Zeit)
    property bool stale: true
    signal open(var id)
    signal add(string how)                 // "photo" oder "file"
    signal share(var ids)
    property string query: ""
    property string category: ""
    property var items: []
    property var cats: []
    property bool selecting: false
    property var selected: []
    readonly property bool previews: app.pdfPreviewAvailable()
    function reload() {
        if (!active) { stale = true; return }
        stale = false
        items = app.documents(personId, query, category)
        const used = []
        for (const d of app.documents(personId, "", "")) if (d.category && used.indexOf(d.category) < 0) used.push(d.category)
        cats = used.sort()
        selected = selected.filter(id => items.some(d => d.id === id))
    }
    function toggle(id) {
        const s = selected.slice()
        const i = s.indexOf(id)
        if (i < 0) s.push(id); else s.splice(i, 1)
        selected = s
        if (s.length === 0) selecting = false
    }
    onPersonIdChanged: { selecting = false; selected = []; category = ""; reload() }
    onQueryChanged: reload()
    onCategoryChanged: reload()
    Connections { target: app; function onDataChanged() { page.reload() } }
    onActiveChanged: if (active && stale) reload()
    Component.onCompleted: reload()

    readonly property var years: {
        const y = []
        for (const d of items) if (y.indexOf(d.year) < 0) y.push(d.year)
        return y
    }

    contentHeight: col.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: col
        x: Theme.gapL; y: Theme.gap
        width: page.width - 2 * Theme.gapL
        spacing: Theme.gap

        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            VButton { objectName: "docPhotoButton"; text: qsTr("Foto aufnehmen"); iconText: "+"; kind: "primary"; onClicked: page.add("photo") }
            VButton { objectName: "docFileButton"; text: qsTr("Datei wählen"); onClicked: page.add("file") }
            VButton { visible: !page.selecting && page.items.length > 0; text: qsTr("Auswählen und teilen")
                      onClicked: { page.selecting = true; page.selected = [] } }
            VButton { visible: page.selecting; objectName: "docShareButton"; kind: "primary"
                      text: page.selected.length ? qsTr("%1 teilen …").arg(page.selected.length) : qsTr("Akte teilen …")
                      onClicked: page.share(page.selected) }
            VButton { visible: page.selecting; text: qsTr("Alle"); onClicked: page.selected = page.items.filter(d => d.hasData).map(d => d.id) }
            VButton { visible: page.selecting; text: qsTr("Abbrechen"); kind: "flat"; onClicked: { page.selecting = false; page.selected = [] } }
        }
        VField {
            objectName: "docSearch"
            Layout.fillWidth: true
            placeholder: qsTr("Suchen (Titel, Art, Praxis, Text im Dokument …)")
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextChanged: searchDelay.restart()
            Timer { id: searchDelay; interval: 300; onTriggered: page.query = parent.text }
        }
        Flow {
            visible: page.cats.length > 1
            Layout.fillWidth: true
            spacing: Theme.gapS
            VChip { text: qsTr("Alle"); checkable: false; checked: page.category === ""; onClicked: page.category = "" }
            Repeater {
                model: page.cats
                VChip { text: modelData; checkable: false; checked: page.category === modelData
                        onClicked: page.category = page.category === modelData ? "" : modelData }
            }
        }

        Label {
            visible: page.items.length === 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted
            text: page.query !== "" || page.category !== ""
                  ? qsTr("Keine Dokumente gefunden.")
                  : qsTr("Noch keine Dokumente. Fotografieren Sie einen Arztbrief oder Befund – mehrere Seiten werden zu einem PDF – oder übernehmen Sie eine PDF-Datei bzw. ein Bild aus dem Speicher des Tablets.")
        }

        Repeater {
            model: page.years
            delegate: ColumnLayout {
                readonly property var list: page.items.filter(d => d.year === modelData)
                Layout.fillWidth: true
                spacing: 8
                Label { text: modelData > 0 ? String(modelData) : qsTr("ohne Datum"); font.bold: true; color: Theme.ink2; Layout.topMargin: 6 }
                Repeater {
                    model: list
                    delegate: AbstractButton {
                        id: card
                        objectName: "docCard"
                        readonly property bool isSelected: page.selected.indexOf(modelData.id) >= 0
                        Layout.fillWidth: true
                        implicitHeight: Math.max(112, row.implicitHeight + 24)
                        onClicked: page.selecting ? (modelData.hasData ? page.toggle(modelData.id) : null) : page.open(modelData.id)
                        onPressAndHold: if (modelData.hasData) { page.selecting = true; page.toggle(modelData.id) }
                        background: Rectangle {
                            radius: Theme.radius
                            color: card.isSelected ? Theme.brandWash : card.down ? Theme.brandWash : Theme.surface
                            border.color: card.isSelected ? Theme.brand : Theme.rule
                            border.width: card.isSelected ? 2 : 1
                        }
                        contentItem: RowLayout {
                            id: row
                            anchors.fill: parent; anchors.margins: 12
                            spacing: 14
                            Rectangle {
                                Layout.preferredWidth: 70; Layout.preferredHeight: 92
                                Layout.alignment: Qt.AlignVCenter
                                color: Theme.paper; border.color: Theme.rule; radius: 4
                                clip: true
                                Image {
                                    anchors.fill: parent; anchors.margins: 1
                                    visible: page.previews && modelData.hasData
                                    source: visible ? "image://docpage/" + modelData.id + "/0" : ""
                                    sourceSize.width: 140
                                    fillMode: Image.PreserveAspectCrop
                                    verticalAlignment: Image.AlignTop
                                    asynchronous: false
                                }
                                Label { anchors.centerIn: parent; visible: !page.previews || !modelData.hasData
                                        text: modelData.hasData ? "PDF" : "⏳"; color: Theme.muted; font.bold: true }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label { Layout.fillWidth: true; text: modelData.title; font.bold: true; color: Theme.ink; elide: Text.ElideRight }
                                Label {
                                    Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.ink2; font.pixelSize: Theme.fsS
                                    text: [modelData.dateText, modelData.category, modelData.issuer,
                                           modelData.pages > 0 ? qsTr("%1 Seite(n)").arg(modelData.pages) : ""].filter(s => s).join(" · ")
                                }
                                Label { visible: !!modelData.snippet; Layout.fillWidth: true; text: modelData.snippet; color: Theme.muted
                                        font.pixelSize: Theme.fsS; font.italic: true; elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap }
                                Label { visible: !modelData.hasData; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: Theme.fsS
                                        text: qsTr("Inhalt folgt mit dem nächsten Abgleich.") }
                            }
                            Rectangle {
                                visible: page.selecting
                                Layout.preferredWidth: 30; Layout.preferredHeight: 30; radius: 15
                                color: card.isSelected ? Theme.brand : "transparent"
                                border.width: 2; border.color: card.isSelected ? Theme.brand : Theme.ruleStrong
                                opacity: modelData.hasData ? 1 : 0.3
                                Label { anchors.centerIn: parent; text: "✓"; color: "white"; visible: card.isSelected; font.bold: true }
                            }
                        }
                    }
                }
            }
        }
    }
}
