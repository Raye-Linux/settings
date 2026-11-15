// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Proxy: detect it, a setup script, or a proxy
// server. One of the three applies at a time.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Proxy") }]

    ProxySettings { id: proxySettings }
    NetworkStatus { id: netStatus }

    Text {
        Layout.fillWidth: true
        text: netStatus.wording.proxyIntro
        wrapMode: Text.Wrap
        font: Theme.body
        color: Theme.textSecondary
    }

    SectionHeader { text: i18nc("@title:group", "Automatic proxy setup") }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "proxy-detect"
        iconName: "org.raye.settings-proxy"
        iconFallback: "view-refresh"
        title: i18nc("@label", "Automatically detect settings")
        checked: proxySettings.autoDetect
        onToggled: {
            proxySettings.setAutoDetect(checked)
            checked = Qt.binding(() => proxySettings.autoDetect)
        }
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "proxy-script"
        iconName: "org.raye.settings-proxy"
        iconFallback: "text-x-script"
        title: i18nc("@label", "Use setup script")
        description: proxySettings.useScript ? i18nc("@info", "On") : i18nc("@info", "Off")
        buttonText: i18nc("@action:button", "Set up")
        onButtonClicked: scriptDialog.open()
    }

    SectionHeader { text: i18nc("@title:group", "Manual proxy setup") }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "proxy-manual"
        iconName: "org.raye.settings-proxy"
        iconFallback: "network-server"
        title: i18nc("@label", "Use a proxy server")
        description: proxySettings.useManual ? i18nc("@info, %1 is a server and %2 its port", "On · %1:%2", proxySettings.host, proxySettings.port)
                                     : i18nc("@info", "Off")
        buttonText: i18nc("@action:button", "Set up")
        onButtonClicked: manualDialog.open()
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s5
        closable: false
        severity: "informational"
        title: i18nc("@info", "Applies to apps that follow the system proxy")
        message: i18nc("@info", "Other apps use the proxy server after you sign out and sign back in.")
    }

    ProxyScriptDialog { id: scriptDialog; proxy: proxySettings }
    ProxyManualDialog { id: manualDialog; proxy: proxySettings }
}
