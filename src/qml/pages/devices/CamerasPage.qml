// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Cameras: what is connected, and a look through each one. The
// camera only turns on when asked, and goes off when the page closes.
import QtQuick
import QtQuick.Layouts
import QtMultimedia
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Cameras") }]

    MediaDevices { id: media }
    CameraList { id: kernel }

    property int previewing: -1

    readonly property bool cannotOpen: media.videoInputs.length === 0 && kernel.names.length > 0

    EmptyState {
        Layout.fillWidth: true
        visible: media.videoInputs.length === 0 && kernel.names.length === 0
        iconName: "camera-web"
        title: i18nc("@title", "No camera found")
        message: i18nc("@info", "Plug in a camera, and it shows up here.")
    }

    InfoBar {
        Layout.fillWidth: true
        open: page.cannotOpen
        closable: false
        severity: "warning"
        title: i18nc("@title", "This camera can't be opened")
        message: i18nc("@info %1 is the name of a camera", "%1 is connected, but it doesn't answer. Another app may be using it.",
                       kernel.names.join(", "))
    }

    Repeater {
        model: media.videoInputs
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            readonly property bool showing: page.previewing === index
            Layout.fillWidth: true
            spacing: Theme.s1

            Card {
                Layout.fillWidth: true
                iconName: "org.raye.settings-cameras"
                iconFallback: "camera-web"
                title: row.modelData.description
                description: row.modelData.isDefault ? i18nc("@info", "Default camera") : ""
                Button {
                    text: row.showing ? i18nc("@action:button", "Stop") : i18nc("@action:button", "Preview")
                    onClicked: page.previewing = row.showing ? -1 : row.index
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.preferredHeight: active ? 360 : 0
                active: row.showing
                visible: active
                sourceComponent: Item {
                    CaptureSession {
                        camera: Camera {
                            cameraDevice: row.modelData
                            active: true
                        }
                        videoOutput: output
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusControl
                        color: "black"
                    }
                    VideoOutput {
                        id: output
                        anchors.fill: parent
                    }
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-privacy-camera"
        iconFallback: "camera-web"
        title: i18nc("@title", "Camera privacy")
        description: i18nc("@info", "Which apps may use your camera")
        target: "privacy/camera"
    }
}
