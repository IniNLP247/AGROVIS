import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    required property var theme
    signal requestOpenFolder()

    color: theme.bgPanel
    border.color: theme.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "CAPTURE QUEUE"
                color: theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.0
                Layout.fillWidth: true
            }
            Label {
                text: captureList.count + " sets"
                color: theme.textDim
                font.pixelSize: 11
            }
        }

        // Action row: Load Folder + Synthetic Plot
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                height: 30
                radius: 5
                color: loadBtnMa.containsMouse ? theme.bgHover : theme.bgControl
                border.color: theme.border
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "+ Load Capture Dir"
                    color: theme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: loadBtnMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebar.requestOpenFolder()
                }
            }

            Rectangle {
                width: 96
                height: 30
                radius: 5
                color: appCtrl.usingSyntheticSample ? theme.accentTint : (synMa.containsMouse ? theme.bgHover : theme.bgControl)
                border.color: appCtrl.usingSyntheticSample ? theme.accent : theme.border
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "Sample Plot"
                    color: appCtrl.usingSyntheticSample ? theme.accent : theme.textMuted
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: synMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: appCtrl.loadSyntheticSample()
                }
            }
        }

        // Active folder indicator
        Rectangle {
            Layout.fillWidth: true
            visible: appCtrl.loadedFolder.length > 0
            height: 28
            radius: 4
            color: theme.bgCard
            border.color: theme.border
            border.width: 1

            Label {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                text: appCtrl.loadedFolder
                color: theme.textMuted
                font.pixelSize: 10
                elide: Text.ElideLeft
            }
        }

        // Synthetic Plot Card
        Rectangle {
            Layout.fillWidth: true
            height: 68
            radius: 6
            color: appCtrl.usingSyntheticSample ? theme.accentTint : theme.bgCard
            border.color: appCtrl.usingSyntheticSample ? theme.accent : theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 9
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Built-in Calibration Plot"
                        color: theme.textPrimary
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        width: 46
                        height: 16
                        radius: 3
                        color: theme.accentDim
                        Label {
                            anchors.centerIn: parent
                            text: "5-BAND"
                            color: "#FFFFFF"
                            font.pixelSize: 9
                            font.weight: Font.Bold
                        }
                    }
                }
                Label {
                    text: "Crop rows + stress pocket + 50% gray ref card"
                    color: theme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: appCtrl.loadSyntheticSample()
            }
        }

        // Scanned real capture groups list
        ListView {
            id: captureList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: captureModel

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                width: captureList.width
                height: 78
                radius: 6
                readonly property bool isSelected: (!appCtrl.usingSyntheticSample && appCtrl.selectedCaptureIndex === index)
                color: isSelected ? theme.accentTint : (cardMa.containsMouse ? theme.bgHover : theme.bgCard)
                border.color: isSelected ? theme.accent : theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: groupId
                            color: theme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: sizeLabel
                            color: theme.textDim
                            font.pixelSize: 10
                        }
                    }

                    Label {
                        text: timestamp + " · " + bandCount + " files"
                        color: theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Row {
                        spacing: 4
                        Repeater {
                            model: [
                                { lbl: "RGB", on: hasRgb, col: "#38BDF8" },
                                { lbl: "G", on: hasGreen, col: "#2ECC71" },
                                { lbl: "R", on: hasRed, col: "#EF4444" },
                                { lbl: "RE", on: hasRedEdge, col: "#F59E0B" },
                                { lbl: "NIR", on: hasNir, col: "#C084FC" }
                            ]
                            delegate: Rectangle {
                                visible: modelData.on
                                width: pillTxt.implicitWidth + 10
                                height: 16
                                radius: 3
                                color: "#1A2521"
                                border.color: modelData.col
                                border.width: 1

                                Label {
                                    id: pillTxt
                                    anchors.centerIn: parent
                                    text: modelData.lbl
                                    color: modelData.col
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    id: cardMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: appCtrl.selectedCaptureIndex = index
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: captureList.count === 0
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: "Open a folder with your multispectral or camera images to load captures automatically."
                color: theme.textDim
                font.pixelSize: 11
            }
        }
    }
}
