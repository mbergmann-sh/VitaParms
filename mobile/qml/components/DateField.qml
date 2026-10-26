// Datumsfeld zum Tippen (TT.MM.JJJJ) – Wert als ISO-Datum; leer erlaubt
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property string label: ""
    property string isoDate: ""          // "2026-10-20" oder ""
    property bool allowEmpty: true
    property string emptyText: "—"
    readonly property bool valid: input.text.trim() === "" ? allowEmpty : parse(input.text) !== ""
    spacing: 4
    opacity: enabled ? 1 : 0.5

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function toText(iso) {
        if (!iso) return ""
        const p = iso.split("-")
        return p.length === 3 ? p[2] + "." + p[1] + "." + p[0] : ""
    }
    function parse(t) {
        const m = t.trim().match(/^(\d{1,2})\.(\d{1,2})\.(\d{2}|\d{4})$/)
        if (!m) return ""
        let y = parseInt(m[3]); if (y < 100) y += 2000
        const d = new Date(y, parseInt(m[2]) - 1, parseInt(m[1]))
        if (d.getDate() !== parseInt(m[1]) || d.getMonth() !== parseInt(m[2]) - 1) return ""
        return y + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
    }
    onIsoDateChanged: if (parse(input.text) !== isoDate) input.text = toText(isoDate)
    Component.onCompleted: input.text = toText(isoDate)

    Label { visible: f.label !== ""; text: f.label; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: Theme.touch
        font.family: Theme.family; font.pixelSize: Theme.fsM; color: Theme.ink
        placeholderText: qsTr("TT.MM.JJJJ")
        placeholderTextColor: Theme.muted
        inputMethodHints: Qt.ImhPreferNumbers
        leftPadding: 14; rightPadding: 14
        onTextEdited: { const p = f.parse(text); f.isoDate = text.trim() === "" ? "" : p }
        background: Rectangle {
            radius: Theme.radius; color: Theme.surface
            border.width: input.activeFocus ? 2 : 1
            border.color: !f.valid ? Theme.alert : input.activeFocus ? Theme.brand : Theme.ruleStrong
        }
    }
}
