import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: inspector
    required property var theme

    color: theme.bgPanel
    border.color: theme.border
    border.width: 1

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentHeight: col.implicitHeight
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 12

            // 1. Spectral Index Grid Card
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: idxCol.implicitHeight + 18
                radius: 6
                color: theme.bgCard
                border.color: theme.border

                ColumnLayout {
                    id: idxCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "SPECTRAL INDEX"
                            color: theme.textMuted
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 0.9
                            Layout.fillWidth: true
                        }
                        Label {
                            text: appCtrl.activeIndexName
                            color: theme.accent
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 6
                        columnSpacing: 5
                        rowSpacing: 6

                        Repeater {
                            model: appCtrl.indexMetaList
                            delegate: Rectangle {
                                readonly property bool active: (appCtrl.spectralIndex === modelData.indexPos)
                                readonly property bool enabledChip: modelData.supported
                                Layout.fillWidth: true
                                Layout.preferredWidth: boldChipMeasure.implicitWidth + 12
                                Layout.preferredHeight: 26
                                radius: 4
                                color: !enabledChip ? "#141918" : (active ? theme.accentTint : (chipMa.containsMouse ? theme.bgHover : theme.bgControl))
                                border.color: !enabledChip ? "#1F2825" : (active ? theme.accent : theme.border)
                                border.width: 1
                                opacity: enabledChip ? 1.0 : 0.48

                                // Reserve bold text width so toggling selection never reflows the grid
                                Label {
                                    id: boldChipMeasure
                                    visible: false
                                    text: modelData.shortName
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }

                                Label {
                                    id: chipLbl
                                    anchors.centerIn: parent
                                    text: modelData.shortName
                                    color: !enabledChip ? theme.textDim : (active ? theme.accent : theme.textPrimary)
                                    font.pixelSize: 11
                                    font.weight: active ? Font.Bold : Font.Normal
                                }

                                MouseArea {
                                    id: chipMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: enabledChip
                                    cursorShape: enabledChip ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                                    onClicked: appCtrl.spectralIndex = modelData.indexPos
                                }
                            }
                        }
                    }
                }
            }

            // 3. Simple Background Filter (Calibration & Parallax handled automatically by camera profile)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: pipeCol.implicitHeight + 18
                radius: 6
                color: theme.bgCard
                border.color: theme.border

                ColumnLayout {
                    id: pipeCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Label {
                        text: "BACKGROUND FILTER"
                        color: theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.9
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Repeater {
                            model: [
                                { lbl: "Mask Soil", key: "soil" },
                                { lbl: "Shadow", key: "shadow" },
                                { lbl: "Glare", key: "glare" }
                            ]
                            delegate: Item {
                                width: maskRow.implicitWidth
                                height: 22
                                readonly property bool checkedVal:
                                    modelData.key === "soil" ? appCtrl.maskSoil :
                                    modelData.key === "shadow" ? appCtrl.maskShadow : appCtrl.maskSkyGlare

                                Row {
                                    id: maskRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5

                                    Rectangle {
                                        width: 15
                                        height: 15
                                        radius: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: checkedVal ? theme.accent : theme.bgControl
                                        border.color: checkedVal ? theme.accent : theme.border
                                        Label {
                                            anchors.centerIn: parent
                                            visible: checkedVal
                                            text: "✓"
                                            color: "#07130C"
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }
                                    Label {
                                        text: modelData.lbl
                                        color: theme.textPrimary
                                        font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.key === "soil") appCtrl.maskSoil = !appCtrl.maskSoil;
                                        else if (modelData.key === "shadow") appCtrl.maskShadow = !appCtrl.maskShadow;
                                        else appCtrl.maskSkyGlare = !appCtrl.maskSkyGlare;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 4. Health Zone Distribution & Rough Local Diagnosis
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: statCol.implicitHeight + 18
                radius: 6
                color: theme.bgCard
                border.color: theme.border

                ColumnLayout {
                    id: statCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Label {
                        text: "CROP HEALTH ESTIMATE"
                        color: theme.textMuted
                        font.pixelSize: 10
                        font.weight: Font.Bold
                        font.letterSpacing: 0.9
                    }

                    // Stacked Zone Breakdown Bar
                    Rectangle {
                        Layout.fillWidth: true
                        height: 14
                        radius: 4
                        color: theme.bgDeep
                        clip: true

                        Row {
                            anchors.fill: parent
                            Rectangle { width: parent.width * (appCtrl.healthyPct / 100.0); height: parent.height; color: "#2ECC71" }
                            Rectangle { width: parent.width * (appCtrl.moderatePct / 100.0); height: parent.height; color: "#EAB308" }
                            Rectangle { width: parent.width * (appCtrl.stressedPct / 100.0); height: parent.height; color: "#EF4444" }
                            Rectangle { width: parent.width * (appCtrl.soilPct / 100.0); height: parent.height; color: "#475569" }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 4
                        columnSpacing: 8

                        Label { text: "● Healthy: " + appCtrl.healthyPct.toFixed(1) + "%"; color: "#2ECC71"; font.pixelSize: 11 }
                        Label { text: "● Moderate: " + appCtrl.moderatePct.toFixed(1) + "%"; color: "#EAB308"; font.pixelSize: 11 }
                        Label { text: "● Stressed: " + appCtrl.stressedPct.toFixed(1) + "%"; color: "#EF4444"; font.pixelSize: 11 }
                        Label { text: "● Soil/Mask: " + appCtrl.soilPct.toFixed(1) + "%"; color: "#94A3B8"; font.pixelSize: 11 }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            height: 42
                            radius: 4
                            color: theme.bgDeep
                            Column {
                                anchors.centerIn: parent
                                Label { text: "CANOPY"; color: theme.textDim; font.pixelSize: 9; anchors.horizontalCenter: parent.horizontalCenter }
                                Label { text: appCtrl.canopyCoverPct.toFixed(1) + "%"; color: theme.textPrimary; font.pixelSize: 12; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 42
                            radius: 4
                            color: theme.bgDeep
                            Column {
                                anchors.centerIn: parent
                                Label { text: "MEAN"; color: theme.textDim; font.pixelSize: 9; anchors.horizontalCenter: parent.horizontalCenter }
                                Label { text: appCtrl.meanIndex.toFixed(3); color: theme.accent; font.pixelSize: 12; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 42
                            radius: 4
                            color: theme.bgDeep
                            Column {
                                anchors.centerIn: parent
                                Label { text: "STD DEV"; color: theme.textDim; font.pixelSize: 9; anchors.horizontalCenter: parent.horizontalCenter }
                                Label { text: appCtrl.stdDevIndex.toFixed(3); color: theme.textPrimary; font.pixelSize: 12; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: diagCol.implicitHeight + 14
                        radius: 4
                        color: theme.bgDeep
                        border.color: theme.border

                        ColumnLayout {
                            id: diagCol
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 2
                            Label {
                                text: "ROUGH ISSUE ESTIMATE"
                                color: theme.textDim
                                font.pixelSize: 9
                                font.weight: Font.Bold
                                font.letterSpacing: 0.8
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "WIP"
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }
}
