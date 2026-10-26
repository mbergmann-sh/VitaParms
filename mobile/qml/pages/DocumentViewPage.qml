// Krankenakte: Dokument ansehen (Seiten), in anderer App öffnen, teilen, bearbeiten, löschen
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "documentViewPage"
    property var personId: 0
    property var docId: 0
    signal closed(string message)
    signal edit(var id)
    signal share(var ids)
    property var doc: app.documentInfo(docId)
    Connections { target: app; function onDataChanged() { const d = app.documentInfo(page.docId); if (d.id) page.doc = d } }
    readonly property bool previews: app.pdfPreviewAvailable() && page.doc.hasData === true
    property real zoom: 1.0
    background: Rectangle { color: Theme.paper }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Zurück"); kind: "flat"; onClicked: page.closed("") }
            Label { text: page.doc.title || ""; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; elide: Text.ElideRight
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.previews; text: "−"; implicitWidth: Theme.touch; onClicked: page.zoom = Math.max(1, page.zoom - 0.5) }
            VButton { visible: page.previews; text: "+"; implicitWidth: Theme.touch; onClicked: page.zoom = Math.min(3, page.zoom + 0.5) }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Angaben und Aktionen
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: info.implicitHeight + 24
            color: Theme.surface
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
            ColumnLayout {
                id: info
                x: Theme.gapL; y: 12
                width: parent.width - 2 * Theme.gapL
                spacing: 6
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: [page.doc.dateText, page.doc.category, page.doc.issuer,
                           page.doc.pages ? qsTr("%1 Seite(n)").arg(page.doc.pages) : "", page.doc.size].filter(s => s).join(" · ")
                }
                Label { visible: !!page.doc.notes; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; text: page.doc.notes || "" }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    VButton { objectName: "docOpenButton"; text: qsTr("In PDF-App öffnen"); enabled: page.doc.hasData === true
                              onClicked: if (!app.openDocument(page.docId)) win.toast(app.lastError, true) }
                    VButton { text: qsTr("Teilen …"); enabled: page.doc.hasData === true; onClicked: page.share([page.docId]) }
                    VButton { text: qsTr("Angaben bearbeiten"); onClicked: page.edit(page.docId) }
                    VButton { text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
                }
            }
        }
        // Seiten
        ListView {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.previews
            clip: true
            spacing: 12
            topMargin: 12; bottomMargin: 24
            model: page.previews ? (page.doc.pages || 1) : 0
            contentWidth: width * page.zoom
            flickableDirection: page.zoom > 1 ? Flickable.HorizontalAndVerticalFlick : Flickable.VerticalFlick
            delegate: Item {
                width: pages.width * page.zoom
                height: img.status === Image.Ready ? img.paintedHeight : width * 1.414
                Image {
                    id: img
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width - 24, 1400 * page.zoom)
                    source: "image://docpage/" + page.docId + "/" + index
                    sourceSize.width: Math.round(Math.min(2000, width * Screen.devicePixelRatio))
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                    Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.rule }
                }
            }
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        Label {
            visible: !page.previews
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.margins: Theme.gapL
            wrapMode: Text.WordWrap; color: Theme.muted
            verticalAlignment: Text.AlignTop
            text: page.doc.hasData === false
                  ? qsTr("Der Inhalt dieses Dokuments fehlt noch. Es wurde auf einem anderen Gerät erfasst; der Inhalt kommt mit dem nächsten Abgleich (Website oder USB-Kabel).")
                  : qsTr("Eine Vorschau ist in dieser Version nicht eingebaut – „In PDF-App öffnen“ zeigt das Dokument an.")
        }
    }

    Confirm {
        id: confirmDelete
        title: qsTr("Dokument löschen?")
        message: qsTr("„%1“ wird aus der Krankenakte gelöscht – auf allen Geräten, sobald abgeglichen wird.").arg(page.doc.title || "")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deleteDocument(page.docId)) page.closed(qsTr("Dokument gelöscht.")); else win.toast(app.lastError, true) }
    }
}
