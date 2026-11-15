// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Printers & scanners: the printers CUPS knows and finds on the
// network, which one is the default, a test page, the queue, and adding a
// printer by its address. When the print service is off the page says so
// and offers to turn it on: nothing below can work until it is.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Printers & scanners") }]

    PrinterList {
        id: printers
        onTestPageSent: page.notice = i18nc("@info", "The test page was sent to the printer.")
    }
    property string notice: ""
    property string toRemove: ""

    function stateText(printer) {
        const parts = []
        if (printer.isDefault)
            parts.push(i18nc("@info a printer", "Default"))
        if (printer.state === "printing")
            parts.push(i18nc("@info a printer", "Printing"))
        else if (printer.state === "stopped")
            parts.push(i18nc("@info a printer", "Stopped"))
        else
            parts.push(i18nc("@info a printer", "Ready"))
        if (printer.network)
            parts.push(i18nc("@info a printer", "Found on your network"))
        return parts.join(" · ")
    }

    InfoBar {
        Layout.fillWidth: true
        open: printers.loaded && !printers.serviceRunning
        closable: false
        severity: "warning"
        title: i18nc("@title", "Printing is turned off")
        message: i18nc("@info", "Turn it on to use a printer. It asks for your password once.")
        Button {
            text: i18nc("@action:button", "Turn on")
            accent: true
            enabled: !printers.busy
            onClicked: printers.turnOnService()
        }
    }

    InfoBar {
        Layout.fillWidth: true
        open: printers.error !== ""
        severity: "error"
        title: i18nc("@title", "That didn't work")
        message: printers.error
        onClosed: printers.clearError()
    }

    InfoBar {
        Layout.fillWidth: true
        open: page.notice !== ""
        severity: "success"
        title: page.notice
        onClosed: page.notice = ""
    }

    ButtonCard {
        searchId: "add-printer"
        iconName: "org.raye.settings-printers"
        iconFallback: "printer"
        title: i18nc("@title", "Add a printer")
        description: i18nc("@info", "Printers on your network show up by themselves. Add one here by its address.")
        buttonText: i18nc("@action:button", "Add")
        enabled: printers.serviceRunning && !printers.busy
        onButtonClicked: addDialog.open()
    }

    SectionHeader {
        visible: printers.printers.length > 0
        text: i18nc("@title:group", "Your printers")
    }

    EmptyState {
        Layout.fillWidth: true
        visible: printers.loaded && printers.serviceRunning && printers.printers.length === 0
        iconName: "printer"
        title: i18nc("@title", "No printers yet")
        message: i18nc("@info", "Turn your printer on and connect it to your network or to a USB port.")
    }

    Repeater {
        model: printers.printers
        delegate: Expander {
            required property var modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-printers"
            iconFallback: "printer"
            title: modelData.name
            description: page.stateText(modelData)
            ExpanderRow {
                title: i18nc("@title", "Use this printer by default")
                Button {
                    text: i18nc("@action:button", "Set as default")
                    enabled: !modelData.isDefault && !printers.busy
                    onClicked: printers.setDefault(modelData.name)
                }
            }
            ExpanderRow {
                title: i18nc("@title", "See what is printing")
                Button {
                    text: i18nc("@action:button", "Open print queue")
                    onClicked: printers.openQueue(modelData.name)
                }
            }
            ExpanderRow {
                title: i18nc("@title", "Check that it prints")
                Button {
                    text: i18nc("@action:button", "Print a test page")
                    enabled: !printers.busy
                    onClicked: printers.printTestPage(modelData.name)
                }
            }
            ExpanderRow {
                visible: !modelData.network
                title: i18nc("@title", "Remove this printer from this PC")
                Button {
                    text: i18nc("@action:button", "Remove")
                    enabled: !printers.busy
                    onClicked: {
                        page.toRemove = modelData.name
                        removeDialog.open()
                    }
                }
            }
        }
    }

    ContentDialog {
        id: removeDialog
        title: i18nc("@title:window", "Remove this printer?")
        text: page.toRemove
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: printers.remove(page.toRemove)
    }

    ContentDialog {
        id: addDialog
        title: i18nc("@title:window", "Add a printer")
        primaryText: i18nc("@action:button", "Add")
        primaryEnabled: printers.addressProblem(addressField.text) === "" && printers.nameProblem(nameField.text) === ""
        onAccepted: {
            printers.add(nameField.text, addressField.text)
            addressField.text = ""
            nameField.text = ""
        }
        Text {
            Layout.fillWidth: true
            text: i18nc("@label", "Address of the printer")
            font: Theme.body
            color: Theme.text
        }
        TextField {
            id: addressField
            Layout.fillWidth: true
            placeholderText: "ipp://printer.local/ipp/print"
            onTextEdited: nameField.text = printers.suggestName(text)
        }
        Text {
            Layout.fillWidth: true
            visible: addressField.text !== ""
            wrapMode: Text.Wrap
            text: printers.addressProblem(addressField.text)
            font: Theme.caption
            color: Theme.textSecondary
        }
        Text {
            Layout.fillWidth: true
            text: i18nc("@label", "Name")
            font: Theme.body
            color: Theme.text
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
        }
    }
}
