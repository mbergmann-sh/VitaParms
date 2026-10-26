// Auswahlliste mit Beschriftung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

ColumnLayout {
    id: f
    property alias label: lbl.text
    property alias model: box.model
    property alias currentIndex: box.currentIndex
    property alias textRole: box.textRole
    property alias combo: box
    signal activated(int index)
    spacing: 4
    opacity: enabled ? 1 : 0.5
    Label { id: lbl; visible: text !== ""; font.family: Theme.family; font.pixelSize: Theme.fsS; color: Theme.ink2 }
    ComboBox {
        id: box
        Layout.fillWidth: true
        implicitHeight: Theme.touch
        font.family: Theme.family
        font.pixelSize: Theme.fsM
        onActivated: (i) => f.activated(i)
        contentItem: Text {
            leftPadding: 14; rightPadding: box.indicator.width + 8
            text: box.displayText; font: box.font; color: Theme.ink
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        indicator: Text {
            x: box.width - width - 14; y: (box.height - height) / 2
            text: "▾"; color: Theme.ink2; font.pixelSize: Theme.fsL
        }
        background: Rectangle {
            radius: Theme.radius; color: Theme.surface
            border.width: box.activeFocus ? 2 : 1
            border.color: box.activeFocus ? Theme.brand : Theme.ruleStrong
        }
        delegate: ItemDelegate {
            width: box.width
            height: Theme.touch
            contentItem: Text {
                text: box.textRole ? (Array.isArray(box.model) ? modelData[box.textRole] : model[box.textRole]) : modelData
                font: box.font; color: Theme.ink; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            highlighted: box.highlightedIndex === index
            background: Rectangle { color: highlighted ? Theme.brandWash : Theme.surface }
        }
        popup: Popup {
            y: box.height + 4
            width: box.width
            implicitHeight: Math.min(contentItem.implicitHeight + 8, 420)
            padding: 4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: box.popup.visible ? box.delegateModel : null
                currentIndex: box.highlightedIndex
            }
            background: Rectangle { radius: Theme.radius; color: Theme.surface; border.color: Theme.ruleStrong }
        }
    }
}
