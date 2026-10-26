// Kontakt bearbeiten (Krankenkasse, Hausarzt, Facharzt, Klinik …) – im Stammblatt
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: d
    objectName: "contactPopup"
    property var contact: ({})
    property int index: -1                  // -1 = neu
    signal saved(var contact, int index)
    signal removed(int index)
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, parent ? parent.width - 32 : 760)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : 800)
    padding: 20
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: "#80000000" }

    function openWith(c, i) {
        contact = c || ({ kind: 1 })
        index = i
        kind.currentIndex = contact.kind !== undefined ? contact.kind : 1
        name.text = contact.name || ""; specialty.text = contact.specialty || ""; person.text = contact.person || ""
        insNo.text = contact.insuranceNo || ""; street.text = contact.street || ""; zip.text = contact.zip || ""
        city.text = contact.city || ""; phone.text = contact.phone || ""; mobile.text = contact.mobile || ""
        email.text = contact.email || ""; website.text = contact.website || ""; note.text = contact.note || ""
        error.text = ""
        open()
        name.field.forceActiveFocus()
    }
    function accept() {
        if (name.text.trim() === "") { error.text = qsTr("Bitte einen Namen angeben (Praxis, Klinik, Kasse oder Person)."); return }
        if (email.text.trim() !== "" && !/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email.text.trim())) { error.text = qsTr("Die E-Mail-Adresse ist ungültig."); return }
        const c = { uid: contact.uid || "", kind: kind.currentIndex, kindName: kind.combo.currentText, name: name.text.trim(),
                    specialty: specialty.text.trim(), person: person.text.trim(), insuranceNo: kind.currentIndex === 0 ? insNo.text.trim() : "",
                    street: street.text.trim(), zip: zip.text.trim(), city: city.text.trim(), phone: phone.text.trim(),
                    mobile: mobile.text.trim(), email: email.text.trim(), website: website.text.trim(), note: note.text.trim() }
        c.summary = [c.name + (c.specialty ? " – " + c.specialty : ""), c.phone, c.city].filter(s => s).join(", ")
        close()
        saved(c, index)
    }

    contentItem: Flickable {
        implicitHeight: Math.min(form.implicitHeight, 720)
        contentHeight: form.implicitHeight + 260       // Platz für die Bildschirmtastatur
        clip: true
        ColumnLayout {
            id: form
            width: parent.width
            spacing: Theme.gapS
            Label { text: d.index < 0 ? qsTr("Neuer Kontakt") : qsTr("Kontakt bearbeiten"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            GridLayout {
                Layout.fillWidth: true
                columns: d.width > 600 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VCombo { id: kind; label: qsTr("Art"); model: app.contactKinds(); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                VField { id: name; objectName: "contactName"; label: qsTr("Name *"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                VField { id: specialty; label: qsTr("Fachrichtung / Abteilung"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                VField { id: person; label: qsTr("Ansprechpartner / Beziehung"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                VField { id: insNo; label: qsTr("Versichertennummer"); visible: kind.currentIndex === 0; Layout.fillWidth: true; Layout.preferredWidth: 10 }
                Item { visible: kind.currentIndex === 0 && d.width > 600; Layout.fillWidth: true }
                VField { id: street; label: qsTr("Straße"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredWidth: 10
                    spacing: Theme.gapS
                    VField { id: zip; label: qsTr("PLZ"); Layout.fillWidth: false; Layout.preferredWidth: 110; Layout.maximumWidth: 120; inputMethodHints: Qt.ImhPreferNumbers }
                    VField { id: city; label: qsTr("Ort"); Layout.fillWidth: true; Layout.preferredWidth: 300 }
                }
                VField { id: phone; label: qsTr("Telefon"); Layout.fillWidth: true; Layout.preferredWidth: 10; inputMethodHints: Qt.ImhDialableCharactersOnly }
                VField { id: mobile; label: qsTr("Mobil"); Layout.fillWidth: true; Layout.preferredWidth: 10; inputMethodHints: Qt.ImhDialableCharactersOnly }
                VField { id: email; label: qsTr("E-Mail"); Layout.fillWidth: true; Layout.preferredWidth: 10; inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase }
                VField { id: website; label: qsTr("Website"); Layout.fillWidth: true; Layout.preferredWidth: 10; inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase }
            }
            VArea { id: note; label: qsTr("Notiz"); Layout.fillWidth: true; minHeight: 70 }
            Label { id: error; visible: text !== ""; color: Theme.alert; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VButton { visible: d.index >= 0; text: qsTr("Entfernen"); kind: "flat"; onClicked: { d.close(); d.removed(d.index) } }
                Item { Layout.fillWidth: true }
                VButton { text: qsTr("Abbrechen"); onClicked: d.close() }
                VButton { objectName: "contactApply"; text: qsTr("Übernehmen"); kind: "primary"; onClicked: d.accept() }
            }
        }
    }
}
