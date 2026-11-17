import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1460
    height: 880
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    title: "AGROVIS - Multispectral Crop Scouting & Spectral Analysis"
    color: theme.bgDeep

    readonly property var theme: ({
        bgDeep: "#0B0F0E",
        bgPanel: "#121816",
        bgCard: "#18211E",
        bgControl: "#1E2925",
        bgHover: "#24322D",
        border: "#293832",
        borderFocus: "#2ECC71",
        accent: "#2ECC71",
        accentDim: "#1C6E3F",
        accentTint: "#163325",
        warn: "#F59E0B",
        danger: "#EF4444",
        info: "#38BDF8",
        textPrimary: "#ECF2EF",
        textMuted: "#8CA197",
        textDim: "#5C7067"
    })

    FolderDialog {
        id: folderPicker
        title: "Select Multispectral Capture Folder"
        onAccepted: appCtrl.loadFolder(selectedFolder)
    }

    FileDialog {
        id: exportDialog
        title: "Export Processed Spectral Overlay"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG image (*.png)"]
        onAccepted: appCtrl.exportOverlayPng(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top Application Command Bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.bgPanel
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 16

                // AGROVIS Brand Emblem (Canvas leaf + spectral crosshair)
                RowLayout {
                    spacing: 10
                    Canvas {
                        width: 26
                        height: 26
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = theme.accent;
                            ctx.lineWidth = 2.0;
                            ctx.beginPath();
                            ctx.arc(13, 13, 11, 0, Math.PI * 2);
                            ctx.stroke();

                            ctx.fillStyle = theme.accent;
                            ctx.beginPath();
                            ctx.moveTo(13, 5);
                            ctx.quadraticCurveTo(20, 10, 17, 19);
                            ctx.quadraticCurveTo(10, 18, 9, 10);
                            ctx.closePath();
                            ctx.fill();

                            ctx.strokeStyle = theme.bgPanel;
                            ctx.lineWidth = 1.6;
                            ctx.beginPath();
                            ctx.moveTo(10, 16);
                            ctx.lineTo(16, 10);
                            ctx.stroke();
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        Label {
                            text: "AGROVIS"
                            color: theme.textPrimary
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                        Label {
                            text: "FIELD CROP SCOUTING"
                            color: theme.textDim
                            font.pixelSize: 9
                            font.letterSpacing: 0.8
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Quick Load & Export Actions
                Rectangle {
                    height: 32
                    width: openLbl.implicitWidth + 22
                    radius: 6
                    color: openMa.containsMouse ? theme.bgHover : theme.bgControl
                    border.color: theme.border
                    border.width: 1

                    Label {
                        id: openLbl
                        anchors.centerIn: parent
                        text: "Open Folder"
                        color: theme.textPrimary
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: openMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: folderPicker.open()
                    }
                }

                Rectangle {
                    height: 32
                    width: exportLbl.implicitWidth + 24
                    radius: 6
                    color: exportMa.containsMouse ? "#35DC7C" : theme.accent

                    Label {
                        id: exportLbl
                        anchors.centerIn: parent
                        text: "Export PNG"
                        color: "#07130C"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: exportMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: exportDialog.open()
                    }
                }
            }
        }

        // Main Workspace Body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CaptureSidebar {
                Layout.preferredWidth: 278
                Layout.fillHeight: true
                theme: root.theme
                onRequestOpenFolder: folderPicker.open()
            }

            SpectralViewport {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: root.theme
            }

            AnalysisInspector {
                Layout.preferredWidth: 348
                Layout.fillHeight: true
                theme: root.theme
            }
        }

        // Bottom Status Strip
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: theme.bgPanel
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 14

                Label {
                    text: appCtrl.activeCaptureSummary
                    color: theme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Rectangle { width: 1; height: 14; color: theme.border }

                Label {
                    text: appCtrl.statusNote
                    color: appCtrl.activeIndexSupported ? theme.textMuted : theme.warn
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Label {
                    text: "Processed in " + appCtrl.processingMs.toFixed(1) + " ms"
                    color: theme.textDim
                    font.pixelSize: 11
                }
            }
        }
    }
}
