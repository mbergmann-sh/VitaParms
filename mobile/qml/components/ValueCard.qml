// Eingabekachel für einen Messwert: Farbstreifen der Kurve, große Ziffern, Einheit
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Rectangle {
    id: card
    property string label: ""
    property string unit: ""
    property color accent: Theme.brand
    property int decimals: 0
    property real minValue: 0
    property real maxValue: 1000
    property alias text: input.text
    property alias field: input
    property bool filled: input.text.trim() !== ""
    readonly property real number: parseFloat(input.text.replace(",", "."))
    readonly property bool outOfRange: filled && (isNaN(number) || number < minValue || number > maxValue)
    default property alias extraContent: extra.data
    signal edited()

    implicitHeight: 128
    radius: Theme.radius
    color: Theme.surface
    border.width: input.activeFocus ? 2 : 1
    border.color: outOfRange ? Theme.alert : input.activeFocus ? accent : Theme.rule

    Rectangle { width: 6; height: parent.height - 2 * parent.radius; y: parent.radius; x: 0; color: Theme.series(card.accent); opacity: card.filled ? 1 : 0.35 }
    ColumnLayout {
        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 14; anchors.topMargin: 10; anchors.bottomMargin: 10
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Label { text: card.label; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; elide: Text.ElideRight }
            Item { id: extra; implicitWidth: childrenRect.width; implicitHeight: childrenRect.height }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: input
                Layout.fillWidth: true
                font.family: Theme.family
                font.pixelSize: Theme.fsValue
                font.bold: true
                color: Theme.ink
                placeholderText: "—"
                placeholderTextColor: Theme.ruleStrong
                inputMethodHints: card.decimals > 0 ? Qt.ImhFormattedNumbersOnly : Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: card.decimals > 0 ? /^\d{0,4}([.,]\d{0,2})?$/ : /^\d{0,4}$/ }
                padding: 0
                background: Item {}
                onTextEdited: card.edited()
            }
            Label { text: card.unit; color: Theme.muted; font.pixelSize: Theme.fsM; Layout.alignment: Qt.AlignBottom; bottomPadding: 8 }
        }
        Label { visible: card.outOfRange; text: qsTr("Wert ungewöhnlich (%1–%2)").arg(card.minValue).arg(card.maxValue)
                color: Theme.alert; font.pixelSize: Theme.fsXs }
    }
    MouseArea { anchors.fill: parent; z: -1; onClicked: input.forceActiveFocus() }
}
